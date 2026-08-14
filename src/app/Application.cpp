#include "app/Application.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>
#include <ImGuizmo.h>
#include <imnodes.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "app/State.hpp"
#include "app/FileDialog.hpp"
#include "app/HardwareProfile.hpp"
#include "app/Validation.hpp"
#include "app/ViewportRecorder.hpp"
#include "assets/ModelAsset.hpp"
#include "document/Persistence.hpp"
#include "editor/EditorState.hpp"
#include "editor/Commands.hpp"
#include "evaluation/Compiler.hpp"
#include "evaluation/SignalRegistry.hpp"
#include "handbook/Handbook.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/TextureReadback.hpp"
#include "ui/DocumentInspector.hpp"
#include "ui/DocumentTimeline.hpp"
#include "ui/Inspector.hpp"
#include "ui/OperationGraph.hpp"
#include "ui/ScopePanel.hpp"
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

void applyMeasurementModulations(document::Document& evaluated,
    std::unordered_map<int, MeasurementRuntime>& runtime, const float deltaSeconds) {
  for (auto& [id, state] : runtime) {
    static_cast<void>(id);
    state.modulationApplied = false;
  }
  for (const document::ModulationRoute& route : evaluated.automation.modulation) {
    const document::SignalDescriptor* sourceSignal = document::findSignal(evaluated, route.source.id);
    const document::Operation* sourceOperation = sourceSignal == nullptr ? nullptr
      : document::findOperation(evaluated, sourceSignal->producer);
    const auto* measure = sourceOperation == nullptr ? nullptr
      : std::get_if<document::MeasureOperation>(&sourceOperation->data);
    if (measure == nullptr) continue;
    const auto sampled = runtime.find(static_cast<int>(sourceOperation->id.value));
    if (sampled == runtime.end() || sampled->second.measurement.sampleCount == 0) continue;
    const document::PropertyDescriptor* targetDescriptor =
      document::propertyDescriptor(route.target.property);
    if (targetDescriptor == nullptr) continue;
    const std::optional<AnimationProperty> targetProperty =
      document::animationProperty(route.target.property);

    MeasurementRuntime& state = sampled->second;
    const float measured = measurementMetricValue(state.measurement, measure->metric);
    if (!state.smoothingInitialized || state.smoothedMetric != measure->metric) {
      state.smoothedControl = measured;
      state.smoothedMetric = measure->metric;
      state.smoothingInitialized = true;
    } else {
      const float timeConstant = std::max(route.smoothingSeconds, 0.0f);
      const float response = timeConstant <= 0.00001f ? 1.0f
        : 1.0f - std::exp(-std::max(deltaSeconds, 0.0f) / timeConstant);
      state.smoothedControl += (measured - state.smoothedControl) * response;
    }
    const float inputSpan = route.inputRange.y - route.inputRange.x;
    float normalized = std::abs(inputSpan) <= 0.000001f ? 0.0f
      : (state.smoothedControl - route.inputRange.x) / inputSpan;
    if (route.clamp) normalized = std::clamp(normalized, 0.0f, 1.0f);
    const float output = std::clamp(route.outputRange.x + normalized *
      (route.outputRange.y - route.outputRange.x), targetDescriptor->minimum, targetDescriptor->maximum);

    if (route.target.owner == document::renderDefaultsObject) {
      if (!targetProperty.has_value()) continue;
      RenderPass carrier;
      carrier.renderer = evaluated.renderDefaults.renderer;
      carrier.textureSource = evaluated.renderDefaults.texture.source;
      carrier.importedTextureSrgb = evaluated.renderDefaults.texture.srgb;
      setAnimationPropertyValue(carrier, *targetProperty, glm::vec4(output));
      evaluated.renderDefaults.renderer = carrier.renderer;
      evaluated.renderDefaults.texture.source = carrier.textureSource;
      evaluated.renderDefaults.texture.srgb = carrier.importedTextureSrgb;
    } else {
      const std::optional<document::OperationId> targetId =
        document::operationFromObject(route.target.owner);
      document::Operation* target = targetId.has_value()
        ? document::findOperation(evaluated, *targetId) : nullptr;
      if (target == nullptr) continue;
      if (auto* render = std::get_if<document::RenderOperation>(&target->data)) {
        if (route.target.property == document::timeScaleProperty()) render->time.scale = output;
        else if (route.target.property == document::timeOffsetProperty()) render->time.offsetSeconds = output;
        else if (targetProperty.has_value()) {
          const auto found = std::find_if(render->overrides.begin(), render->overrides.end(),
            [&](const PropertyOverride& value) { return value.property == *targetProperty; });
          if (found == render->overrides.end()) render->overrides.push_back({*targetProperty, glm::vec4(output)});
          else found->value = glm::vec4(output);
        } else continue;
      } else if (auto* composite = std::get_if<document::CompositeOperation>(&target->data)) {
        if (!targetProperty.has_value()) continue;
        if (*targetProperty == AnimationProperty::CompositeGain) composite->arithmetic.gain = output;
        else if (*targetProperty == AnimationProperty::CompositeBias) composite->arithmetic.bias = output;
        else if (*targetProperty == AnimationProperty::CompositeOpacity) composite->arithmetic.opacity = output;
        else if (*targetProperty == AnimationProperty::CompositeHistoryDecay && composite->feedback.has_value())
          composite->feedback->decay = output;
        else continue;
      } else if (auto* stereo = std::get_if<document::StereoOperation>(&target->data)) {
        if (!targetProperty.has_value()) continue;
        if (*targetProperty == AnimationProperty::StereoMaximumDisparity)
          stereo->maximumDisparityPixels = output;
        else if (*targetProperty == AnimationProperty::StereoOcclusionTolerance)
          stereo->occlusionTolerance = output;
        else continue;
      } else continue;
    }
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
  ImNodes::CreateContext();
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
  TestScene scene = TestScene::Torus;
  bool viewportHovered = false;
  bool viewportAcceptsCameraInput = false;
  bool cameraWasEditing = false;
  bool previewAnimation = true;
  editor::EditorState editorState;
  EditorSelection& editorSelection = editorState.selection;
  float uiScale = 1.0f;
  float appliedUiScale = 1.0f;
  WorkspaceWindows workspaceWindows;
  AnimationTimeline animationTimeline;
  double previousFrameTime = glfwGetTime();
  handbook::Handbook graphicsHandbook;

  runStartupValidationIfRequested(renderer, current, reference, camera, scene, category);

  document::Document authoredDocument = document::makeDefaultDocument();
  authoredDocument.renderDefaults.renderer = current;
  authoredDocument.scene.authoredCamera = camera;
  TestScene historyScene = authoredDocument.scene.testScene;
  HardwareProfile historyProfile = authoredDocument.hardwareProfile;
  const ModelAsset* historyModel = nullptr;
  const auto normalizeDocument = [](document::Document& document) {
    if (document.hardwareProfile == HardwareProfile::Unrestricted)
      document.renderDefaults.renderer.n64.enabled = false;
    normalizeForHardwareProfile(document.hardwareProfile, document.renderDefaults.renderer);
  };
  evaluation::SignalRegistry signalRegistry;
  std::uint64_t signalRevision = 0;
  std::string modelImportError;
  std::string recordingMessage;
  bool recordingFailed = false;
  std::string documentMessage;
  bool documentOperationFailed = false;
  std::unordered_map<int, MeasurementRuntime> measurementRuntime;
  editor::CommandHistory editorHistory;
  const auto restoreHistory = [&](const bool redo) {
    const std::shared_ptr<const ModelAsset> previousModel = authoredDocument.scene.importedModel;
    const bool changed = redo ? editorHistory.redo(authoredDocument)
      : editorHistory.undo(authoredDocument);
    if (!changed) return;
    measurementRuntime.clear();
    if (previousModel != authoredDocument.scene.importedModel) {
      if (authoredDocument.scene.importedModel != nullptr)
        renderer.setImportedModel(*authoredDocument.scene.importedModel);
      else renderer.clearImportedModel();
    }
    renderer.resetFrameHistory();
    if (!categoryAvailableForHardwareProfile(authoredDocument.hardwareProfile, category))
      category = Category::Geometry;
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
      document::Document cameraEdit = authoredDocument;
      bool cameraChanged = false;
      if (viewportAcceptsCameraInput && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        cameraEdit.scene.authoredCamera.yaw -= io.MouseDelta.x * 0.008f;
        cameraEdit.scene.authoredCamera.pitch = std::clamp(
          cameraEdit.scene.authoredCamera.pitch + io.MouseDelta.y * 0.008f, -1.45f, 1.45f);
        cameraChanged = true;
      }
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        const glm::mat4 inverseView = glm::inverse(cameraEdit.scene.authoredCamera.view());
        const glm::vec3 right = glm::vec3(inverseView[0]);
        const glm::vec3 up = glm::vec3(inverseView[1]);
        cameraEdit.scene.authoredCamera.target += (-right * io.MouseDelta.x + up * io.MouseDelta.y) *
          cameraEdit.scene.authoredCamera.distance * 0.0015f;
        cameraChanged = true;
      }
      if (io.MouseWheel != 0.0f) {
        cameraEdit.scene.authoredCamera.distance = std::clamp(
          cameraEdit.scene.authoredCamera.distance * std::pow(0.88f, io.MouseWheel), 1.4f, 14.0f);
        cameraChanged = true;
      }
      if (cameraChanged) {
        static_cast<void>(editorHistory.executeContinuous(authoredDocument,
          editor::ReplaceDocument{std::move(cameraEdit)}));
        cameraWasEditing = true;
      }
    }
    if (cameraWasEditing && !ImGui::IsMouseDown(ImGuiMouseButton_Left) &&
        !ImGui::IsMouseDown(ImGuiMouseButton_Middle) && !ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
      editorHistory.finishContinuous(authoredDocument);
      cameraWasEditing = false;
    }

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    const auto importModelFromDialog = [&]() {
      const FileDialogResult dialog = openModelFileDialog();
      if (!dialog.error.empty()) modelImportError = dialog.error;
      else if (dialog.path.has_value()) {
        const ModelImportResult imported = importModelAsset(*dialog.path);
        if (imported) {
          document::Document edited = authoredDocument;
          edited.scene.importedModel = imported.asset;
          edited.scene.testScene = TestScene::ImportedModel;
          edited.scene.authoredCamera = CameraOrbit{};
          static_cast<void>(editorHistory.execute(authoredDocument,
            editor::ReplaceDocument{std::move(edited)}));
          renderer.setImportedModel(*authoredDocument.scene.importedModel);
          modelImportError.clear();
        } else {
          modelImportError = imported.error;
        }
      }
    };

    HardwareProfile menuHardwareProfile = authoredDocument.hardwareProfile;
    const WorkspaceActions workspaceActions = beginWorkspace(workspaceWindows,
      editorHistory.canUndo(), editorHistory.canRedo(), uiScale, viewportRecorder.recording(),
      viewportRecorder.durationSeconds(), menuHardwareProfile);
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
        document::DocumentLoadResult loaded = document::loadDocumentFile(*dialog.path);
        if (loaded) {
          authoredDocument = std::move(*loaded.document);
          const document::Automation::Timeline& timeline = authoredDocument.automation.timeline;
          animationTimeline.timeSeconds = timeline.currentTimeSeconds;
          animationTimeline.durationSeconds = timeline.durationSeconds;
          animationTimeline.playbackRate = timeline.playbackRate;
          animationTimeline.loop = timeline.loop;
          animationTimeline.autoKey = timeline.autoKey;
          animationTimeline.showAllPasses = timeline.showAllOperations;
          animationTimeline.snapToFrames = timeline.snapToFrames;
          animationTimeline.framesPerSecond = timeline.framesPerSecond;
        } else {
          documentMessage = loaded.error;
          documentOperationFailed = true;
        }
        if (!documentOperationFailed) {
          measurementRuntime.clear();
          if (authoredDocument.scene.importedModel != nullptr)
            renderer.setImportedModel(*authoredDocument.scene.importedModel);
          else renderer.clearImportedModel();
          renderer.resetFrameHistory();
          renderer.resetElementalSimulation();
          editorState = {};
          editorHistory.clear();
          documentMessage = "Loaded document:\n" + *dialog.path;
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
        document::Automation::Timeline& timeline = authoredDocument.automation.timeline;
        timeline.currentTimeSeconds = animationTimeline.timeSeconds;
        timeline.durationSeconds = animationTimeline.durationSeconds;
        timeline.playbackRate = animationTimeline.playbackRate;
        timeline.loop = animationTimeline.loop;
        timeline.autoKey = animationTimeline.autoKey;
        timeline.showAllOperations = animationTimeline.showAllPasses;
        timeline.snapToFrames = animationTimeline.snapToFrames;
        timeline.framesPerSecond = animationTimeline.framesPerSecond;
        if (document::saveDocumentFile(*dialog.path, authoredDocument, documentMessage))
          documentMessage = "Saved typed document:\n" + *dialog.path;
        else documentOperationFailed = true;
      }
      if (!documentMessage.empty()) ImGui::OpenPopup("Stack document");
    }
    if (workspaceActions.copyJson) {
      ImGui::SetClipboardText(document::documentJson(authoredDocument).c_str());
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

    editor::synchronizeEditorState(editorState, authoredDocument);
    const SceneWindowResult sceneResult = drawOperationGraph(workspaceWindows.document,
      authoredDocument, editorState, editorHistory);
    if (sceneResult.importModel) importModelFromDialog();
    if (sceneResult.unloadModel) {
      document::Document edited = authoredDocument;
      edited.scene.importedModel.reset();
      if (edited.scene.testScene == TestScene::ImportedModel)
        edited.scene.testScene = TestScene::Torus;
      static_cast<void>(editorHistory.execute(authoredDocument,
        editor::ReplaceDocument{std::move(edited)}));
      renderer.clearImportedModel();
    }
    if (workspaceActions.hardwareProfileChanged) {
      document::Document edited = authoredDocument;
      edited.hardwareProfile = menuHardwareProfile;
      normalizeDocument(edited);
      const TestScene selectedScene = edited.scene.testScene;
      if (edited.hardwareProfile != HardwareProfile::Unrestricted &&
          (selectedScene == TestScene::StencilMask || selectedScene == TestScene::FieldInterference ||
           selectedScene == TestScene::SdfIsoSurface || selectedScene == TestScene::SpectralMetamers ||
           selectedScene == TestScene::ElementalChamber))
        edited.scene.testScene = TestScene::Torus;
      static_cast<void>(editorHistory.execute(authoredDocument,
        editor::ReplaceDocument{std::move(edited)}));
      if (!categoryAvailableForHardwareProfile(authoredDocument.hardwareProfile, category))
        category = Category::Geometry;
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

    if (authoredDocument.scene.testScene != historyScene ||
        authoredDocument.hardwareProfile != historyProfile ||
        authoredDocument.scene.importedModel.get() != historyModel) {
      renderer.resetFrameHistory();
      renderer.resetElementalSimulation();
      historyScene = authoredDocument.scene.testScene;
      historyProfile = authoredDocument.hardwareProfile;
      historyModel = authoredDocument.scene.importedModel.get();
    }

    if (workspaceWindows.animation) {
      if (ImGui::Begin("Timeline", &workspaceWindows.animation)) {
        keepCurrentWindowVisible();
        drawDocumentTimeline(animationTimeline, authoredDocument, editorState, editorHistory);
      }
      ImGui::End();
    }
    authoredDocument.automation.timeline.currentTimeSeconds = animationTimeline.timeSeconds;
    authoredDocument.automation.timeline.durationSeconds = animationTimeline.durationSeconds;
    authoredDocument.automation.timeline.playbackRate = animationTimeline.playbackRate;
    authoredDocument.automation.timeline.loop = animationTimeline.loop;
    authoredDocument.automation.timeline.autoKey = animationTimeline.autoKey;
    authoredDocument.automation.timeline.showAllOperations = animationTimeline.showAllPasses;
    authoredDocument.automation.timeline.snapToFrames = animationTimeline.snapToFrames;
    authoredDocument.automation.timeline.framesPerSecond = animationTimeline.framesPerSecond;

    const bool evaluateAnimation = animationTimeline.playing || previewAnimation;
    document::Document evaluatedDocument = authoredDocument;
    normalizeDocument(evaluatedDocument);
    applyMeasurementModulations(evaluatedDocument, measurementRuntime, deltaSeconds);
    const evaluation::EvaluationPlan evaluationPlan = evaluation::compileDocument(evaluatedDocument);
    renderer.updateElementalSimulation(deltaSeconds, evaluatedDocument.renderDefaults.renderer,
      evaluatedDocument.scene.testScene);
    const GLuint renderedComposite = renderer.evaluate(evaluatedDocument, evaluationPlan,
      signalRegistry, ++signalRevision,
      evaluateAnimation ? animationTimeline.timeSeconds : 0.0f);
    GLuint selectedTexture = signalRegistry.displayTexture(editorState.viewer.viewed.id);
    if (selectedTexture == 0) selectedTexture = renderedComposite;
    const SignalMeasurement* selectedMeasurementPointer = nullptr;
    for (const document::Operation& operation : authoredDocument.operations) {
      const auto* measure = std::get_if<document::MeasureOperation>(&operation.data);
      if (!operation.enabled || measure == nullptr) continue;
      const evaluation::SignalResource* inputResource = signalRegistry.find(measure->input.id);
      const GLuint measuredTexture = inputResource == nullptr || inputResource->textureCount == 0
        ? 0 : inputResource->textures[0];
      if (measuredTexture == 0) continue;
      MeasurementRuntime& runtime = measurementRuntime[static_cast<int>(operation.id.value)];
      if (runtime.lastSampleTime < 0.0 || frameTime - runtime.lastSampleTime >= 0.10) {
        runtime.measurement = measureTextureSignal(measuredTexture, measure->threshold,
          measure->absoluteMagnitude);
        runtime.lastSampleTime = frameTime;
      }
    }
    if (editorSelection.kind == EditorSelectionKind::Operation &&
        document::findOperation(authoredDocument, editorSelection.operation) != nullptr &&
        std::holds_alternative<document::MeasureOperation>(
          document::findOperation(authoredDocument, editorSelection.operation)->data)) {
      const auto selectedRuntime = measurementRuntime.find(
        static_cast<int>(editorSelection.operation.value));
      if (selectedRuntime != measurementRuntime.end()) {
        selectedMeasurementPointer = &selectedRuntime->second.measurement;
      }
    }
    editor::synchronizeEditorState(editorState, authoredDocument);
    const document::SignalDescriptor* viewedDescriptor = document::findSignal(authoredDocument,
      editorState.viewer.viewed.id);
    const document::SignalDescriptor* comparisonDescriptor = editorState.viewer.comparison.has_value()
      ? document::findSignal(authoredDocument, editorState.viewer.comparison->id) : nullptr;
    GLuint finalTexture = signalRegistry.displayTexture(authoredDocument.presentation.input.id);
    if (finalTexture == 0) finalTexture = renderedComposite != 0 ? renderedComposite : selectedTexture;
    GLuint viewedTexture = signalRegistry.displayTexture(editorState.viewer.viewed.id);
    if (viewedTexture == 0) viewedTexture = finalTexture;
    GLuint comparisonTexture = editorState.viewer.comparison.has_value()
      ? signalRegistry.displayTexture(editorState.viewer.comparison->id) : 0;
    if (comparisonTexture == 0) comparisonTexture = viewedTexture;
    const bool applyPresentation = editorState.viewer.applyPresentation;
    const GLuint displayedViewed = applyPresentation
      ? renderer.reconstructDisplay(viewedTexture, authoredDocument.presentation.reconstruction, 0) : viewedTexture;
    const GLuint displayedComparison = applyPresentation
      ? renderer.reconstructDisplay(comparisonTexture, authoredDocument.presentation.reconstruction, 1) : comparisonTexture;
    const GLuint displayedFinal = applyPresentation
      ? renderer.reconstructDisplay(finalTexture, authoredDocument.presentation.reconstruction, 2) : finalTexture;
    const GLuint differenceTexture = renderer.compareSignals(displayedViewed, displayedComparison,
      RelationOperator::AbsoluteDifference, 1.0f, 0.0f);
    const CompareMode recordingMode = editorState.viewer.mode == editor::ViewerMode::Split
      ? CompareMode::Split : editorState.viewer.mode == editor::ViewerMode::AbsoluteDifference
        ? CompareMode::Relation : CompareMode::A;
    viewportRecorder.capture(displayedViewed, displayedComparison, differenceTexture, recordingMode, frameTime);
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
    const ViewportWindowResult viewportResult = drawViewportWindow(workspaceWindows.viewport,
      {displayedViewed, displayedComparison, differenceTexture, displayedFinal,
       viewedDescriptor != nullptr ? viewedDescriptor->name.c_str() : "Final output",
       comparisonDescriptor != nullptr ? comparisonDescriptor->name.c_str() : "Comparison signal"},
      editorState.viewer, authoredDocument, editorState, editorHistory);
    viewportHovered = viewportResult.hovered;
    viewportAcceptsCameraInput = viewportResult.acceptsCameraInput;
    const float inspectorTime = evaluateAnimation ? animationTimeline.timeSeconds : 0.0f;
    const TextureAsset* selectedTextureAsset = authoredDocument.renderDefaults.texture.imported.get();
    if (editorSelection.kind == EditorSelectionKind::Operation) {
      const document::Operation* operation = document::findOperation(authoredDocument,
        editorSelection.operation);
      const auto* render = operation == nullptr ? nullptr
        : std::get_if<document::RenderOperation>(&operation->data);
      if (render != nullptr) selectedTextureAsset = render->texture.imported.get();
    }
    const GLuint importedTexturePreview = renderer.texturePreview(selectedTextureAsset);
    drawDocumentInspector(workspaceWindows.inspector, authoredDocument, editorState,
      editorHistory, animationTimeline, inspectorTime, importedTexturePreview,
      selectedMeasurementPointer);
    drawScopePanel(workspaceWindows.scope, authoredDocument, evaluationPlan, signalRegistry, editorState);

    const handbook::Action handbookAction = graphicsHandbook.draw(authoredDocument.hardwareProfile);
    drawTextureInspector(workspaceWindows.textureInspector,
      {viewedTexture, comparisonTexture, finalTexture}, viewedDescriptor != nullptr
        ? viewedDescriptor->name : "Final output");
    if (handbookAction.type == handbook::ActionType::ApplyToA) {
      document::Document edited = authoredDocument;
      if (isNintendo64Example(handbookAction.example))
        edited.hardwareProfile = HardwareProfile::Nintendo64;
      applyHandbookExample(handbookAction.example, false, edited.renderDefaults.renderer,
        edited.scene.authoredCamera, edited.scene.testScene, category);
      normalizeDocument(edited);
      const editor::CommandResult result = editorHistory.execute(authoredDocument,
        editor::ReplaceDocument{std::move(edited)});
      if (result.applied) editorSelection = {};
    } else if (handbookAction.type == handbook::ActionType::ApplyToB) {
      document::Operation* selected = editorSelection.kind == EditorSelectionKind::Operation
        ? document::findOperation(authoredDocument, editorSelection.operation) : nullptr;
      if (selected == nullptr || !std::holds_alternative<document::RenderOperation>(selected->data)) {
        selected = nullptr;
        for (document::Operation& operation : authoredDocument.operations) {
          if (std::holds_alternative<document::RenderOperation>(operation.data)) selected = &operation;
        }
      }
      if (selected != nullptr) {
        document::Document edited = authoredDocument;
        if (isNintendo64Example(handbookAction.example))
          edited.hardwareProfile = HardwareProfile::Nintendo64;
        document::Operation* editedOperation = document::findOperation(edited, selected->id);
        auto* render = editedOperation == nullptr ? nullptr
          : std::get_if<document::RenderOperation>(&editedOperation->data);
        if (render != nullptr) {
          RendererState variantState = edited.renderDefaults.renderer;
          CameraOrbit ignoredCamera = edited.scene.authoredCamera;
          TestScene ignoredScene = edited.scene.testScene;
          Category ignoredCategory = category;
          applyHandbookExample(handbookAction.example, true, variantState,
            ignoredCamera, ignoredScene, ignoredCategory);
          RenderPass global;
          global.renderer = edited.renderDefaults.renderer;
          RenderPass variant = global;
          variant.renderer = variantState;
          render->overrides.clear();
          for (int propertyIndex = 0; propertyIndex < static_cast<int>(AnimationProperty::Count);
               ++propertyIndex) {
            const AnimationProperty property = static_cast<AnimationProperty>(propertyIndex);
            if (animationPropertyIsPassLocal(property)) continue;
            const glm::vec4 baseValue = animationPropertyValue(global, property);
            const glm::vec4 variantValue = animationPropertyValue(variant, property);
            if (!animationPropertyValuesEqual(property, baseValue, variantValue))
              render->overrides.push_back({property, variantValue});
          }
          normalizeDocument(edited);
          static_cast<void>(editorHistory.execute(authoredDocument,
            editor::ReplaceDocument{std::move(edited)}));
        }
      }
    } else if (handbookAction.type == handbook::ActionType::LoadComparison) {
      document::Document edited = document::makeDefaultDocument();
      document::Operation comparisonRender = document::makeRenderOperation({2}, "Render copy");
      document::Operation comparisonComposite = document::makeCompositeOperation({3}, "Composite",
        document::primaryOutput(edited.operations.front()), document::primaryOutput(comparisonRender));
      edited.operations.push_back(std::move(comparisonRender));
      edited.operations.push_back(std::move(comparisonComposite));
      edited.presentation.input = document::primaryOutput(edited.operations.back());
      edited.nextOperationIdentity = 4;
      if (isNintendo64Example(handbookAction.example))
        edited.hardwareProfile = HardwareProfile::Nintendo64;
      applyHandbookExample(handbookAction.example, false, edited.renderDefaults.renderer,
        edited.scene.authoredCamera, edited.scene.testScene, category);
      RendererState comparisonState;
      CameraOrbit comparisonCamera = edited.scene.authoredCamera;
      TestScene comparisonScene = edited.scene.testScene;
      Category comparisonCategory = category;
      applyHandbookExample(handbookAction.example, true, comparisonState, comparisonCamera, comparisonScene, comparisonCategory);
      auto* comparison = std::get_if<document::RenderOperation>(&edited.operations[1].data);
      if (comparison != nullptr) {
        RenderPass global;
        global.renderer = edited.renderDefaults.renderer;
        RenderPass variant = global;
        variant.renderer = comparisonState;
        comparison->overrides.clear();
        for (int propertyIndex = 0; propertyIndex < static_cast<int>(AnimationProperty::Count);
             ++propertyIndex) {
          const AnimationProperty property = static_cast<AnimationProperty>(propertyIndex);
          if (animationPropertyIsPassLocal(property)) continue;
          const glm::vec4 baseValue = animationPropertyValue(global, property);
          const glm::vec4 variantValue = animationPropertyValue(variant, property);
          if (!animationPropertyValuesEqual(property, baseValue, variantValue))
            comparison->overrides.push_back({property, variantValue});
        }
      }
      edited.scene.authoredCamera = comparisonCamera;
      edited.scene.testScene = comparisonScene;
      normalizeDocument(edited);
      category = comparisonCategory;
      const document::OperationId leftOperation = edited.operations[0].id;
      const document::OperationId rightOperation = edited.operations[1].id;
      const editor::CommandResult result = editorHistory.execute(authoredDocument,
        editor::ReplaceDocument{std::move(edited)});
      if (result.applied) {
        measurementRuntime.clear();
        editorState.viewer.comparison = document::SignalRef{
          document::operationSignal(leftOperation, "color"), 0};
        editorState.viewer.viewed = document::SignalRef{
          document::operationSignal(rightOperation, "color"), 0};
        editorState.viewer.mode = editor::ViewerMode::Split;
        editorSelection = {EditorSelectionKind::Operation, rightOperation};
      }
    }

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
  ImNodes::DestroyContext();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

} // namespace gfxlab
