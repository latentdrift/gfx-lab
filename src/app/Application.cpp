#include "app/Application.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "app/State.hpp"
#include "app/StackDocument.hpp"
#include "app/EditorHistory.hpp"
#include "app/FileDialog.hpp"
#include "app/HardwareProfile.hpp"
#include "app/PassEditing.hpp"
#include "app/RenderStack.hpp"
#include "app/Validation.hpp"
#include "app/ViewportRecorder.hpp"
#include "assets/ModelAsset.hpp"
#include "handbook/Handbook.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/TextureReadback.hpp"
#include "ui/AnimationEditor.hpp"
#include "ui/ContextInspector.hpp"
#include "ui/Inspector.hpp"
#include "ui/PassDifferenceAudit.hpp"
#include "ui/TextureInspector.hpp"
#include "ui/Workspace.hpp"
#include "ui/Windowing.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace {

using namespace gfxlab;
using namespace gfxlab::ui;

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "graphics-lab: %s\n", message.c_str());
  std::exit(EXIT_FAILURE);
}


void glfwError(int, const char* descriptionText) { std::fprintf(stderr, "GLFW: %s\n", descriptionText); }

bool isNintendo64Example(handbook::Example example) {
  return example >= handbook::Example::N64ThreePoint && example <= handbook::Example::N64VideoInterface;
}

bool shouldEnableNativeViewports() {
#if defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_WAYLAND)
  // Native Wayland intentionally has no desktop-global window coordinates, so Dear ImGui cannot
  // implement platform windows there. We select X11/XWayland when DISPLAY is available below.
  if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND) return false;
#endif
  return true;
}

constexpr std::array<float, 8> uiScales = {0.75f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f};

std::array<ImFont*, uiScales.size()> createUiFonts() {
  std::array<ImFont*, uiScales.size()> fonts{};
  ImGuiIO& io = ImGui::GetIO();
  for (std::size_t index = 0; index < uiScales.size(); ++index) {
    ImFontConfig config;
    config.SizePixels = 13.0f * uiScales[index];
    fonts[index] = io.Fonts->AddFontDefault(&config);
  }
  return fonts;
}

std::size_t uiScaleIndex(const float scale) {
  const auto closest = std::min_element(uiScales.begin(), uiScales.end(), [scale](const float a, const float b) {
    return std::abs(a - scale) < std::abs(b - scale);
  });
  return static_cast<std::size_t>(std::distance(uiScales.begin(), closest));
}

void applyUiScale(const float scale, const std::array<ImFont*, uiScales.size()>& fonts,
    const ImGuiStyle& baseStyle) {
  ImGui::GetStyle() = baseStyle;
  ImGui::GetStyle().ScaleAllSizes(scale);
  ImGui::GetIO().FontDefault = fonts[uiScaleIndex(scale)];
}

void stepUiScale(float& scale, const int direction) {
  if (direction > 0) {
    const auto next = std::find_if(uiScales.begin(), uiScales.end(), [scale](const float candidate) {
      return candidate > scale + 0.001f;
    });
    scale = next == uiScales.end() ? uiScales.back() : *next;
  } else {
    const auto next = std::find_if(uiScales.rbegin(), uiScales.rend(), [scale](const float candidate) {
      return candidate < scale - 0.001f;
    });
    scale = next == uiScales.rend() ? uiScales.front() : *next;
  }
}

struct MeasurementRuntime {
  SignalMeasurement measurement;
  double lastSampleTime = -1.0;
  float smoothedControl = 0.0f;
  float mappedOutput = 0.0f;
  MeasurementMetric smoothedMetric = MeasurementMetric::Coverage;
  bool smoothingInitialized = false;
  bool modulationApplied = false;
};

void applyMeasurementModulations(RenderStack& evaluated,
    std::unordered_map<int, MeasurementRuntime>& runtime, const float deltaSeconds) {
  for (auto& entry : runtime) entry.second.modulationApplied = false;
  for (const RenderPass& controller : evaluated.passes()) {
    if (!controller.enabled || controller.kind != StackOperationKind::Measure ||
        !controller.measurementModulationEnabled) continue;
    const auto sample = runtime.find(controller.id);
    if (sample == runtime.end() || sample->second.measurement.sampleCount == 0) continue;
    const auto target = std::find_if(evaluated.passes().begin(), evaluated.passes().end(),
      [&](const RenderPass& candidate) { return candidate.id == controller.measurementTargetPassId; });
    if (target == evaluated.passes().end() || !measurementTargetPropertyCompatible(target->kind,
        controller.measurementTargetProperty)) continue;

    MeasurementRuntime& state = sample->second;
    const float measured = measurementMetricValue(state.measurement, controller.measurementMetric);
    if (!state.smoothingInitialized || state.smoothedMetric != controller.measurementMetric) {
      state.smoothedControl = measured;
      state.smoothedMetric = controller.measurementMetric;
      state.smoothingInitialized = true;
    } else {
      const float timeConstant = std::max(controller.measurementSmoothingSeconds, 0.0f);
      const float response = timeConstant <= 0.00001f ? 1.0f :
        1.0f - std::exp(-std::max(deltaSeconds, 0.0f) / timeConstant);
      state.smoothedControl += (measured - state.smoothedControl) * response;
    }
    const float inputSpan = controller.measurementInputMaximum - controller.measurementInputMinimum;
    float normalized = std::abs(inputSpan) <= 0.000001f ? 0.0f :
      (state.smoothedControl - controller.measurementInputMinimum) / inputSpan;
    if (controller.measurementClamp) normalized = std::clamp(normalized, 0.0f, 1.0f);
    float output = controller.measurementOutputMinimum +
      normalized * (controller.measurementOutputMaximum - controller.measurementOutputMinimum);
    const AnimationPropertyInfo& targetInfo = animationPropertyInfo(controller.measurementTargetProperty);
    output = std::clamp(output, targetInfo.minimum, targetInfo.maximum);
    setAnimationPropertyValue(*target, controller.measurementTargetProperty, glm::vec4(output));
    state.mappedOutput = output;
    state.modulationApplied = true;
  }
}

} // namespace

namespace gfxlab {

int runApplication() {
  glfwSetErrorCallback(glfwError);
#if defined(GLFW_PLATFORM) && defined(GLFW_PLATFORM_X11)
  // Dear ImGui platform windows need desktop-global positions, which native Wayland intentionally
  // does not expose. XWayland supplies those semantics; ImGui 1.92.8 also corrects its X11-specific
  // framebuffer/content-scale handling so rendering and pointer hit testing share one coordinate space.
  if (std::getenv("DISPLAY") != nullptr) glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
#endif
  if (!glfwInit()) fail("GLFW initialization failed");
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 0);
  GLFWwindow* window = glfwCreateWindow(1440, 900, "Graphics Lab", nullptr, nullptr);
  if (!window) fail("window creation failed");
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) fail("OpenGL function loading failed");
  glGetError();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
  if (shouldEnableNativeViewports())
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_ViewportsEnable;
  const std::array<ImFont*, uiScales.size()> uiFonts = createUiFonts();
  ImGui::GetIO().FontDefault = uiFonts[uiScaleIndex(1.0f)];
  setStyle();
  const ImGuiStyle baseUiStyle = ImGui::GetStyle();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 410 core");

  Renderer renderer;
  ViewportRecorder viewportRecorder;
  if (std::getenv("GRAPHICS_LAB_VALIDATE_FILE_DIALOG")) {
    const FileDialogResult dialog = openModelFileDialog();
    if (!dialog.error.empty()) fail("native model file dialog failed validation: " + dialog.error);
  }
  RendererState current;
  RendererState reference = current;
  CameraOrbit camera;
  Category category = Category::Geometry;
  bool pipelineFocusRequested = true;
  TestScene scene = TestScene::Torus;
  CompareMode compare = CompareMode::A;
  HardwareProfile hardwareProfile = HardwareProfile::Unrestricted;
  bool viewportHovered = false;
  bool viewportAcceptsCameraInput = false;
  bool viewportGizmoUsing = false;
  bool previewAnimation = true;
  EditorSelection editorSelection;
  float uiScale = 1.0f;
  float appliedUiScale = 1.0f;
  WorkspaceWindows workspaceWindows;
  AnimationTimeline animationTimeline;
  double previousFrameTime = glfwGetTime();
  handbook::Handbook graphicsHandbook;

  runStartupValidationIfRequested(renderer, current, reference, camera, scene, category);

  RenderStack renderStack;
  renderStack.global().renderer = current;
  TestScene historyScene = scene;
  HardwareProfile historyProfile = hardwareProfile;
  const ModelAsset* historyModel = nullptr;
  const auto normalizeDocument = [&renderStack](const HardwareProfile profile) {
    if (profile == HardwareProfile::Unrestricted) renderStack.global().renderer.n64.enabled = false;
    normalizeForHardwareProfile(profile, renderStack.global().renderer);
    for (std::size_t passIndex = 0; passIndex < renderStack.passes().size(); ++passIndex) {
      RenderPass normalized = resolveRenderPass(renderStack, passIndex);
      normalizeForHardwareProfile(profile, normalized.renderer);
      replaceRenderPassOverrides(renderStack.passes()[passIndex], renderStack.global(), normalized);
    }
  };
  std::shared_ptr<const ModelAsset> importedModel;
  std::string modelImportError;
  std::string recordingMessage;
  bool recordingFailed = false;
  std::string documentMessage;
  bool documentOperationFailed = false;
  std::unordered_map<int, MeasurementRuntime> measurementRuntime;
  EditorHistory editorHistory(captureEditorSnapshot(renderStack, camera, scene, hardwareProfile,
    animationTimeline, importedModel));
  const auto restoreHistory = [&](const bool redo) {
    const EditorSnapshot present = captureEditorSnapshot(renderStack, camera, scene, hardwareProfile,
      animationTimeline, importedModel);
    EditorSnapshot restored;
    const bool changed = redo ? editorHistory.redo(present, restored) : editorHistory.undo(present, restored);
    if (!changed) return;
    measurementRuntime.clear();
    const std::shared_ptr<const ModelAsset> previousModel = importedModel;
    restoreEditorSnapshot(restored, renderStack, camera, scene, hardwareProfile, animationTimeline,
      &importedModel);
    if (previousModel != importedModel) {
      if (importedModel != nullptr) renderer.setImportedModel(*importedModel);
      else renderer.clearImportedModel();
    }
    renderer.resetFrameHistory();
    if (!categoryAvailableForHardwareProfile(hardwareProfile, category)) category = Category::Geometry;
  };

  const char* recordingValidationPath = std::getenv("GRAPHICS_LAB_VALIDATE_RECORDING");
  if (recordingValidationPath != nullptr) {
    std::string validationError;
    if (!viewportRecorder.start(recordingValidationPath, glfwGetTime(), validationError))
      fail("viewport recording validation could not start: " + validationError);
  }

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    const double frameTime = glfwGetTime();
    const float deltaSeconds = static_cast<float>(std::min(frameTime - previousFrameTime, 0.1));
    previousFrameTime = frameTime;
    animationTimeline.advance(deltaSeconds);
    if (std::abs(uiScale - appliedUiScale) > 0.001f) {
      applyUiScale(uiScale, uiFonts, baseUiStyle);
      appliedUiScale = uiScale;
    }
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    ImGuizmo::BeginFrame();

    ImGuiIO& io = ImGui::GetIO();
    const bool commandModifier = io.KeyCtrl || io.KeySuper;
    const bool openDocumentShortcut = !io.WantTextInput && commandModifier &&
      ImGui::IsKeyPressed(ImGuiKey_O, false);
    const bool saveDocumentShortcut = !io.WantTextInput && commandModifier &&
      ImGui::IsKeyPressed(ImGuiKey_S, false);
    if (!io.WantTextInput && commandModifier && ImGui::IsKeyPressed(ImGuiKey_Z, false))
      restoreHistory(io.KeyShift);
    else if (!io.WantTextInput && commandModifier && ImGui::IsKeyPressed(ImGuiKey_Y, false))
      restoreHistory(true);
    if (!io.WantTextInput && commandModifier && ImGui::IsKeyPressed(ImGuiKey_0, false)) uiScale = 1.0f;
    if (!io.WantTextInput && commandModifier && (ImGui::IsKeyPressed(ImGuiKey_Equal, false) ||
        ImGui::IsKeyPressed(ImGuiKey_KeypadAdd, false))) stepUiScale(uiScale, 1);
    if (!io.WantTextInput && commandModifier && ImGui::IsKeyPressed(ImGuiKey_Minus, false))
      stepUiScale(uiScale, -1);
    if (!io.WantTextInput && !commandModifier && !ImGui::IsAnyItemActive() &&
        ImGui::IsKeyPressed(ImGuiKey_Space, false))
      animationTimeline.playing = !animationTimeline.playing;
    if (viewportHovered) {
      if (viewportAcceptsCameraInput && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        camera.yaw -= io.MouseDelta.x * 0.008f;
        camera.pitch = std::clamp(camera.pitch + io.MouseDelta.y * 0.008f, -1.45f, 1.45f);
      }
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        const glm::mat4 inverseView = glm::inverse(camera.view());
        const glm::vec3 right = glm::vec3(inverseView[0]);
        const glm::vec3 up = glm::vec3(inverseView[1]);
        camera.target += (-right * io.MouseDelta.x + up * io.MouseDelta.y) * camera.distance * 0.0015f;
      }
      if (io.MouseWheel != 0.0f) camera.distance = std::clamp(camera.distance * std::pow(0.88f, io.MouseWheel), 1.4f, 14.0f);
    }

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    const auto importModelFromDialog = [&]() {
      const FileDialogResult dialog = openModelFileDialog();
      if (!dialog.error.empty()) modelImportError = dialog.error;
      else if (dialog.path.has_value()) {
        const ModelImportResult imported = importModelAsset(*dialog.path);
        if (imported) {
          importedModel = imported.asset;
          renderer.setImportedModel(*importedModel);
          scene = TestScene::ImportedModel;
          camera = CameraOrbit{};
          modelImportError.clear();
        } else {
          modelImportError = imported.error;
        }
      }
    };

    const WorkspaceActions workspaceActions = beginWorkspace(workspaceWindows,
      editorHistory.canUndo(), editorHistory.canRedo(), uiScale, viewportRecorder.recording(),
      viewportRecorder.durationSeconds(), hardwareProfile);
    if (workspaceActions.undo) restoreHistory(false);
    if (workspaceActions.redo) restoreHistory(true);
    if (workspaceActions.importModel) importModelFromDialog();
    if (workspaceActions.loadJson || openDocumentShortcut) {
      documentMessage.clear();
      documentOperationFailed = false;
      const FileDialogResult dialog = openStackDocumentDialog();
      if (!dialog.error.empty()) {
        documentMessage = dialog.error;
        documentOperationFailed = true;
      } else if (dialog.path.has_value()) {
        StackDocumentLoadResult loaded = loadStackDocumentFile(*dialog.path);
        if (!loaded) {
          documentMessage = loaded.error;
          documentOperationFailed = true;
        } else {
          StackDocument document = std::move(*loaded.document);
          renderStack = std::move(document.renderStack);
          measurementRuntime.clear();
          camera = document.camera;
          scene = document.scene;
          hardwareProfile = document.hardwareProfile;
          animationTimeline = document.timeline;
          importedModel = std::move(document.importedModel);
          if (importedModel != nullptr) renderer.setImportedModel(*importedModel);
          else renderer.clearImportedModel();
          renderer.resetFrameHistory();
          renderer.resetElementalSimulation();
          editorSelection = {};
          compare = CompareMode::Relation;
          documentMessage = "Loaded stack document:\n" + *dialog.path;
        }
      }
      if (!documentMessage.empty()) ImGui::OpenPopup("Stack document");
    }
    if (workspaceActions.saveJson || saveDocumentShortcut) {
      documentMessage.clear();
      documentOperationFailed = false;
      const FileDialogResult dialog = saveStackDocumentDialog();
      if (!dialog.error.empty()) {
        documentMessage = dialog.error;
        documentOperationFailed = true;
      } else if (dialog.path.has_value()) {
        const std::string exported = renderStackConfigJson(renderStack, camera, scene, hardwareProfile,
          &animationTimeline, importedModel.get());
        if (saveStackDocumentFile(*dialog.path, exported, documentMessage))
          documentMessage = "Saved stack document:\n" + *dialog.path;
        else
          documentOperationFailed = true;
      }
      if (!documentMessage.empty()) ImGui::OpenPopup("Stack document");
    }
    if (workspaceActions.copyJson) {
      const std::string exported = renderStackConfigJson(renderStack, camera, scene, hardwareProfile,
        &animationTimeline, importedModel.get());
      ImGui::SetClipboardText(exported.c_str());
    }
    if (workspaceActions.toggleViewportRecording) {
      recordingMessage.clear();
      recordingFailed = false;
      if (viewportRecorder.recording()) {
        const std::string outputPath = viewportRecorder.outputPath();
        if (viewportRecorder.stop(recordingMessage))
          recordingMessage = "Saved viewport recording to:\n" + outputPath;
        else
          recordingFailed = true;
        ImGui::OpenPopup("Viewport recording");
      } else {
        const FileDialogResult dialog = saveViewportRecordingDialog();
        if (!dialog.error.empty()) {
          recordingMessage = dialog.error;
          recordingFailed = true;
          ImGui::OpenPopup("Viewport recording");
        } else if (dialog.path.has_value() &&
            !viewportRecorder.start(*dialog.path, frameTime, recordingMessage)) {
          recordingFailed = true;
          ImGui::OpenPopup("Viewport recording");
        }
      }
    }
    if (workspaceActions.resetFrameHistory) {
      renderer.resetFrameHistory();
      renderer.resetElementalSimulation();
    }
    if (workspaceActions.handbook) graphicsHandbook.open();
    if (workspaceActions.quit) glfwSetWindowShouldClose(window, GLFW_TRUE);

    const SceneWindowResult sceneResult = drawDocumentNavigator(workspaceWindows.document, scene,
      renderStack, animationTimeline, editorSelection, hardwareProfile, importedModel.get());
    if (sceneResult.importModel) importModelFromDialog();
    if (sceneResult.unloadModel) {
      importedModel.reset();
      renderer.clearImportedModel();
      if (scene == TestScene::ImportedModel) scene = TestScene::Torus;
    }
    if (workspaceActions.hardwareProfileChanged) {
      normalizeDocument(hardwareProfile);
      if (!categoryAvailableForHardwareProfile(hardwareProfile, category)) category = Category::Geometry;
      if (hardwareProfile != HardwareProfile::Unrestricted &&
          (scene == TestScene::StencilMask || scene == TestScene::FieldInterference ||
           scene == TestScene::SdfIsoSurface || scene == TestScene::SpectralMetamers ||
           scene == TestScene::ElementalChamber))
        scene = TestScene::Torus;
    }
    if (!modelImportError.empty()) ImGui::OpenPopup("Model import failed");
    if (ImGui::BeginPopupModal("Model import failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", modelImportError.c_str());
      if (ImGui::Button("Close")) {
        modelImportError.clear();
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Viewport recording", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      if (recordingFailed)
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "Recording export failed");
      ImGui::TextWrapped("%s", recordingMessage.c_str());
      if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    if (ImGui::BeginPopupModal("Stack document", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      if (documentOperationFailed)
        ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "Stack document operation failed");
      ImGui::TextWrapped("%s", documentMessage.c_str());
      if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }

    if (scene != historyScene || hardwareProfile != historyProfile || importedModel.get() != historyModel) {
      renderer.resetFrameHistory();
      renderer.resetElementalSimulation();
      historyScene = scene;
      historyProfile = hardwareProfile;
      historyModel = importedModel.get();
    }

    const bool inspectorGlobalScope = editorSelection.kind == EditorSelectionKind::SceneDefaults;

    if (workspaceWindows.animation) {
      if (ImGui::Begin("Timeline", &workspaceWindows.animation)) {
        keepCurrentWindowVisible();
        if (editorSelection.kind == EditorSelectionKind::FinalOutput)
          ImGui::TextWrapped("Final Output has no animatable properties. Select Scene Defaults or an operation to edit its tracks.");
        else
          drawAnimationEditor(animationTimeline, renderStack, previewAnimation, inspectorGlobalScope);
      }
      ImGui::End();
    }

    normalizeDocument(hardwareProfile);
    const bool evaluateAnimation = animationTimeline.playing || previewAnimation;
    RenderStack evaluatedStack = evaluateRenderStack(renderStack,
      evaluateAnimation ? animationTimeline.timeSeconds : 0.0f);
    applyMeasurementModulations(evaluatedStack, measurementRuntime, deltaSeconds);
    renderer.updateElementalSimulation(deltaSeconds, evaluatedStack.global().renderer, scene);
    for (RenderPass& pass : evaluatedStack.passes())
      normalizeForHardwareProfile(hardwareProfile, pass.renderer);
    std::array<GLuint, RenderStack::maximumPasses> passTextures{};
    for (std::size_t passIndex = 0; passIndex < evaluatedStack.passes().size(); ++passIndex) {
      const StackOperationKind kind = evaluatedStack.passes()[passIndex].kind;
      if (kind == StackOperationKind::Render || kind == StackOperationKind::LegacyRenderComposite)
        passTextures[passIndex] = renderer.renderPass(evaluatedStack.passes()[passIndex], camera, scene, passIndex);
    }
    const GLuint renderedComposite = renderer.composite(evaluatedStack);
    GLuint selectedTexture = renderer.stackOperationResult(renderStack.selectedIndex());
    if (selectedTexture == 0) selectedTexture = renderedComposite;
    const SignalMeasurement* selectedMeasurementPointer = nullptr;
    float selectedSmoothedControl = 0.0f;
    float selectedMappedOutput = 0.0f;
    bool selectedModulationApplied = false;
    for (std::size_t index = 0; index < renderStack.passes().size(); ++index) {
      const RenderPass& measure = renderStack.passes()[index];
      if (!measure.enabled || measure.kind != StackOperationKind::Measure) continue;
      const GLuint measuredTexture = renderer.stackOperationResult(index);
      if (measuredTexture == 0) continue;
      MeasurementRuntime& runtime = measurementRuntime[measure.id];
      if (runtime.lastSampleTime < 0.0 || frameTime - runtime.lastSampleTime >= 0.10) {
        runtime.measurement = measureTextureSignal(measuredTexture, measure.measurementThreshold,
          measure.measurementAbsolute);
        runtime.lastSampleTime = frameTime;
      }
    }
    if (renderStack.selected().kind == StackOperationKind::Measure) {
      const auto selectedRuntime = measurementRuntime.find(renderStack.selected().id);
      if (selectedRuntime != measurementRuntime.end()) {
        selectedMeasurementPointer = &selectedRuntime->second.measurement;
        selectedSmoothedControl = selectedRuntime->second.smoothedControl;
        selectedMappedOutput = selectedRuntime->second.mappedOutput;
        selectedModulationApplied = selectedRuntime->second.modulationApplied;
      }
    }
    GLuint baseTexture = 0;
    GLuint leftEyeTexture = 0;
    GLuint rightEyeTexture = 0;
    for (std::size_t index = 0; index < evaluatedStack.passes().size(); ++index) {
      if (evaluatedStack.passes()[index].kind == StackOperationKind::Render ||
          evaluatedStack.passes()[index].kind == StackOperationKind::LegacyRenderComposite) {
        if (baseTexture == 0) baseTexture = passTextures[index];
        if (leftEyeTexture == 0) leftEyeTexture = passTextures[index];
        else if (rightEyeTexture == 0) rightEyeTexture = passTextures[index];
      }
    }
    if (renderStack.selected().kind == StackOperationKind::StereoAnalysis) {
      const int leftId = renderStack.selected().composite.sourceAPassId;
      const int rightId = renderStack.selected().composite.sourceBPassId;
      for (std::size_t index = 0; index < evaluatedStack.passes().size(); ++index) {
        if (evaluatedStack.passes()[index].id == leftId) leftEyeTexture = passTextures[index];
        if (evaluatedStack.passes()[index].id == rightId) rightEyeTexture = passTextures[index];
      }
    }
    if (baseTexture == 0) baseTexture = selectedTexture;
    const GLuint compositeTexture = renderedComposite != 0 ? renderedComposite : selectedTexture;
    const GLuint displayedSelected = renderer.reconstructDisplay(selectedTexture, renderStack.display(), 0);
    const GLuint displayedBase = renderer.reconstructDisplay(baseTexture, renderStack.display(), 1);
    const GLuint displayedComposite = renderer.reconstructDisplay(compositeTexture, renderStack.display(), 2);
    viewportRecorder.capture(displayedSelected, displayedBase, displayedComposite, compare, frameTime);
    if (recordingValidationPath != nullptr && viewportRecorder.durationSeconds() >= 0.25) {
      std::string validationError;
      if (!viewportRecorder.stop(validationError))
        fail("viewport recording validation could not encode: " + validationError);
      std::error_code fileError;
      if (std::filesystem::file_size(recordingValidationPath, fileError) < 1024 || fileError)
        fail("viewport recording validation produced no usable MP4 file");
      glfwSetWindowShouldClose(window, GLFW_TRUE);
      recordingValidationPath = nullptr;
    }
    const float viewportTime = evaluateAnimation ? animationTimeline.timeSeconds : 0.0f;
    const RenderPass viewportBefore = inspectorGlobalScope
      ? evaluateRenderPass(renderStack.global(), viewportTime)
      : materializeRenderPass(renderStack, renderStack.selectedIndex(), viewportTime);
    RenderPass viewportPass = viewportBefore;
    const ViewportWindowResult viewportResult = drawViewportWindow(workspaceWindows.viewport,
      {displayedSelected, displayedBase, displayedComposite, leftEyeTexture, rightEyeTexture},
      compare, renderStack, viewportPass, camera,
      animationTimeline, inspectorGlobalScope,
      editorSelection.kind != EditorSelectionKind::FinalOutput);
    viewportHovered = viewportResult.hovered;
    viewportAcceptsCameraInput = viewportResult.acceptsCameraInput;
    viewportGizmoUsing = viewportResult.gizmoUsing;
    if (editorSelection.kind == EditorSelectionKind::SceneDefaults)
      applyEditedPass(renderStack.global(), viewportBefore, viewportPass);
    else if (editorSelection.kind == EditorSelectionKind::Operation)
      applyEditedLocalPass(renderStack, viewportBefore, viewportPass, viewportTime);

    const float inspectorTime = evaluateAnimation ? animationTimeline.timeSeconds : 0.0f;
    const RenderPass textureMappingPass = inspectorGlobalScope
      ? evaluateRenderPass(renderStack.global(), inspectorTime)
      : materializeRenderPass(renderStack, renderStack.selectedIndex(), inspectorTime);
    const GLuint importedTexturePreview = renderer.texturePreview(textureMappingPass.importedTexture.get());
    drawContextInspector(workspaceWindows.inspector, renderStack, animationTimeline, editorSelection,
      hardwareProfile, importedModel.get(), scene, camera, inspectorTime, importedTexturePreview,
      category, pipelineFocusRequested, selectedMeasurementPointer, selectedSmoothedControl,
      selectedMappedOutput, selectedModulationApplied);
    pipelineFocusRequested = false;

    const handbook::Action handbookAction = graphicsHandbook.draw(hardwareProfile);
    drawPassDifferenceAudit(workspaceWindows.passDifferences, renderStack);
    drawTextureInspector(workspaceWindows.textureInspector,
      {selectedTexture, baseTexture, compositeTexture}, renderStack.selected().name);
    if (handbookAction.type != handbook::ActionType::None && isNintendo64Example(handbookAction.example))
      hardwareProfile = HardwareProfile::Nintendo64;
    if (handbookAction.type == handbook::ActionType::ApplyToA) {
      applyHandbookExample(handbookAction.example, false, renderStack.global().renderer, camera, scene, category);
      editorSelection = {};
      pipelineFocusRequested = true;
    } else if (handbookAction.type == handbook::ActionType::ApplyToB) {
      if (renderStack.selectedIndex() == 0) {
        renderStack.select(0);
        if (renderStack.passes().size() < 2) renderStack.duplicateSelected();
        else renderStack.select(1);
      }
      RenderPass comparison = materializeRenderPass(renderStack, renderStack.selectedIndex(), 0.0f);
      applyHandbookExample(handbookAction.example, true, comparison.renderer, camera, scene, category);
      replaceRenderPassOverrides(renderStack.selected(), renderStack.global(), comparison);
      editorSelection = {EditorSelectionKind::Operation, renderStack.selected().id};
      pipelineFocusRequested = true;
    } else if (handbookAction.type == handbook::ActionType::LoadComparison) {
      renderStack = RenderStack{};
      measurementRuntime.clear();
      applyHandbookExample(handbookAction.example, false, renderStack.global().renderer, camera, scene, category);
      RendererState comparisonState;
      CameraOrbit comparisonCamera = camera;
      TestScene comparisonScene = scene;
      Category comparisonCategory = category;
      applyHandbookExample(handbookAction.example, true, comparisonState, comparisonCamera, comparisonScene, comparisonCategory);
      RenderPass comparison = materializeRenderPass(renderStack, 1, 0.0f);
      comparison.renderer = comparisonState;
      replaceRenderPassOverrides(renderStack.passes()[1], renderStack.global(), comparison);
      renderStack.select(1);
      camera = comparisonCamera;
      scene = comparisonScene;
      category = comparisonCategory;
      pipelineFocusRequested = true;
      compare = CompareMode::Split;
      editorSelection = {EditorSelectionKind::Operation, renderStack.selected().id};
    }
    normalizeDocument(hardwareProfile);
    const bool viewportInteraction = viewportHovered && (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
      ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right));
    editorHistory.observe(captureEditorSnapshot(renderStack, camera, scene, hardwareProfile, animationTimeline,
      importedModel),
      ImGui::IsAnyItemActive() || viewportInteraction || viewportGizmoUsing);

    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.08f, 0.085f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) != 0) {
      GLFWwindow* primaryContext = glfwGetCurrentContext();
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      glfwMakeContextCurrent(primaryContext);
    }
    glfwSwapBuffers(window);
  }

  if (viewportRecorder.recording()) {
    std::string ignoredRecordingError;
    viewportRecorder.stop(ignoredRecordingError);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

} // namespace gfxlab
