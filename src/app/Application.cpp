#include "app/Application.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "app/State.hpp"
#include "app/EditorHistory.hpp"
#include "app/FileDialog.hpp"
#include "app/HardwareProfile.hpp"
#include "app/PassEditing.hpp"
#include "app/RenderStack.hpp"
#include "app/Validation.hpp"
#include "assets/ModelAsset.hpp"
#include "handbook/Handbook.hpp"
#include "renderer/Renderer.hpp"
#include "ui/AnimationControls.hpp"
#include "ui/AnimationEditor.hpp"
#include "ui/Inspector.hpp"
#include "ui/PassDifferenceAudit.hpp"
#include "ui/PassInspector.hpp"
#include "ui/Workspace.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
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

} // namespace

namespace gfxlab {

int runApplication() {
  glfwSetErrorCallback(glfwError);
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
  setStyle();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 410 core");

  Renderer renderer;
  if (std::getenv("GRAPHICS_LAB_VALIDATE_FILE_DIALOG")) {
    const FileDialogResult dialog = openModelFileDialog();
    if (!dialog.error.empty()) fail("native model file dialog failed validation: " + dialog.error);
  }
  RendererState current;
  RendererState reference = current;
  CameraOrbit camera;
  Category category = Category::Geometry;
  TestScene scene = TestScene::Torus;
  CompareMode compare = CompareMode::A;
  HardwareProfile hardwareProfile = HardwareProfile::Unrestricted;
  bool viewportHovered = false;
  bool previewAnimation = true;
  bool inspectorGlobalScope = true;
  WorkspaceWindows workspaceWindows;
  AnimationTimeline animationTimeline;
  double previousFrameTime = glfwGetTime();
  handbook::Handbook graphicsHandbook;

  runStartupValidationIfRequested(renderer, current, reference, camera, scene, category);

  RenderStack renderStack;
  renderStack.global().renderer = current;
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
  EditorHistory editorHistory(captureEditorSnapshot(renderStack, camera, scene, hardwareProfile,
    animationTimeline, importedModel));
  const auto restoreHistory = [&](const bool redo) {
    const EditorSnapshot present = captureEditorSnapshot(renderStack, camera, scene, hardwareProfile,
      animationTimeline, importedModel);
    EditorSnapshot restored;
    const bool changed = redo ? editorHistory.redo(present, restored) : editorHistory.undo(present, restored);
    if (!changed) return;
    const std::shared_ptr<const ModelAsset> previousModel = importedModel;
    restoreEditorSnapshot(restored, renderStack, camera, scene, hardwareProfile, animationTimeline,
      &importedModel);
    if (previousModel != importedModel) {
      if (importedModel != nullptr) renderer.setImportedModel(*importedModel);
      else renderer.clearImportedModel();
    }
    if (!categoryAvailableForHardwareProfile(hardwareProfile, category)) category = Category::Geometry;
  };

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    const double frameTime = glfwGetTime();
    const float deltaSeconds = static_cast<float>(std::min(frameTime - previousFrameTime, 0.1));
    previousFrameTime = frameTime;
    animationTimeline.advance(deltaSeconds);
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    const bool commandModifier = io.KeyCtrl || io.KeySuper;
    if (!io.WantTextInput && commandModifier && ImGui::IsKeyPressed(ImGuiKey_Z, false))
      restoreHistory(io.KeyShift);
    else if (!io.WantTextInput && commandModifier && ImGui::IsKeyPressed(ImGuiKey_Y, false))
      restoreHistory(true);
    if (viewportHovered) {
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
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
      editorHistory.canUndo(), editorHistory.canRedo());
    if (workspaceActions.undo) restoreHistory(false);
    if (workspaceActions.redo) restoreHistory(true);
    if (workspaceActions.importModel) importModelFromDialog();
    if (workspaceActions.copyJson) {
      const std::string exported = renderStackConfigJson(renderStack, camera, scene, hardwareProfile,
        &animationTimeline, importedModel.get());
      ImGui::SetClipboardText(exported.c_str());
    }
    if (workspaceActions.handbook) graphicsHandbook.open();
    if (workspaceActions.quit) glfwSetWindowShouldClose(window, GLFW_TRUE);

    const SceneWindowResult sceneResult = drawSceneWindow(workspaceWindows.scene, scene,
      hardwareProfile, importedModel.get());
    if (sceneResult.importModel) importModelFromDialog();
    if (sceneResult.unloadModel) {
      importedModel.reset();
      renderer.clearImportedModel();
      if (scene == TestScene::ImportedModel) scene = TestScene::Torus;
    }
    if (sceneResult.hardwareProfileChanged) {
      normalizeDocument(hardwareProfile);
      if (!categoryAvailableForHardwareProfile(hardwareProfile, category)) category = Category::Geometry;
      if (hardwareProfile != HardwareProfile::Unrestricted && scene == TestScene::StencilMask)
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

    drawRenderPassesWindow(workspaceWindows.renderPasses, renderStack, animationTimeline,
      inspectorGlobalScope);
    drawPipelineWindow(workspaceWindows.pipeline, category, hardwareProfile);

    if (workspaceWindows.animation) {
      ImGui::SetNextWindowSize(ImVec2(980.0f, 300.0f), ImGuiCond_FirstUseEver);
      if (ImGui::Begin("Animation Timeline", &workspaceWindows.animation))
        drawAnimationEditor(animationTimeline, renderStack, previewAnimation, inspectorGlobalScope);
      ImGui::End();
    }

    normalizeDocument(hardwareProfile);
    const bool evaluateAnimation = animationTimeline.playing || previewAnimation;
    RenderStack evaluatedStack = evaluateRenderStack(renderStack,
      evaluateAnimation ? animationTimeline.timeSeconds : 0.0f);
    for (RenderPass& pass : evaluatedStack.passes())
      normalizeForHardwareProfile(hardwareProfile, pass.renderer);
    std::array<GLuint, RenderStack::maximumPasses> passTextures{};
    for (std::size_t passIndex = 0; passIndex < evaluatedStack.passes().size(); ++passIndex)
      passTextures[passIndex] = renderer.renderPass(evaluatedStack.passes()[passIndex], camera, scene, passIndex);
    const GLuint selectedTexture = passTextures[renderStack.selectedIndex()];
    const GLuint baseTexture = passTextures.front();
    const GLuint renderedComposite = renderer.composite(evaluatedStack);
    const GLuint compositeTexture = renderedComposite != 0 ? renderedComposite : selectedTexture;
    viewportHovered = drawViewportWindow(workspaceWindows.viewport,
      {selectedTexture, baseTexture, compositeTexture}, compare, renderStack);

    if (workspaceWindows.inspector) {
      if (ImGui::Begin("Pipeline Inspector", &workspaceWindows.inspector)) {
        ImGui::TextDisabled("EDITING");
        if (ImGui::RadioButton("Global base", inspectorGlobalScope)) inspectorGlobalScope = true;
        ImGui::SameLine();
        if (ImGui::RadioButton("Selected pass", !inspectorGlobalScope)) inspectorGlobalScope = false;
        ImGui::SameLine();
        ImGui::TextDisabled("/ %s", categoryName(category));
        if (inspectorGlobalScope) {
          if (ImGui::Button("Reset global renderer")) renderStack.global().renderer = RendererState{};
        } else {
          if (ImGui::Button("Clear pass overrides")) {
            renderStack.selected().overrides.clear();
            renderStack.selected().importedTextureOverride = false;
            renderStack.selected().importedTexture.reset();
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset scene setup")) {
          if (inspectorGlobalScope) {
            applyRecommendedSetup(scene, renderStack.global().renderer, camera);
          } else {
            RenderPass recommended = materializeRenderPass(renderStack, renderStack.selectedIndex(), 0.0f);
            applyRecommendedSetup(scene, recommended.renderer, camera);
            replaceRenderPassOverrides(renderStack.selected(), renderStack.global(), recommended);
          }
        }
        ImGui::Separator();

        RenderStack inspectorStack = renderStack;
        const float inspectorTime = evaluateAnimation ? animationTimeline.timeSeconds : 0.0f;
        const RenderPass displayedBefore = inspectorGlobalScope
          ? evaluateRenderPass(renderStack.global(), inspectorTime)
          : materializeRenderPass(renderStack, renderStack.selectedIndex(), inspectorTime);
        inspectorStack.selected() = displayedBefore;
        drawPassInspector(inspectorStack, animationTimeline, inspectorGlobalScope);
        ImGui::Spacing();
        ImGui::Separator();
        drawInspector(category, inspectorStack.selected(), hardwareProfile, animationTimeline,
          importedModel.get(), scene);
        if (inspectorGlobalScope)
          applyEditedPass(renderStack.global(), displayedBefore, inspectorStack.selected());
        else
          applyEditedLocalPass(renderStack, displayedBefore, inspectorStack.selected(), inspectorTime);
      }
      ImGui::End();
    }

    const handbook::Action handbookAction = graphicsHandbook.draw(hardwareProfile);
    drawPassDifferenceAudit(workspaceWindows.passDifferences, renderStack);
    if (handbookAction.type != handbook::ActionType::None && isNintendo64Example(handbookAction.example))
      hardwareProfile = HardwareProfile::Nintendo64;
    if (handbookAction.type == handbook::ActionType::ApplyToA) {
      applyHandbookExample(handbookAction.example, false, renderStack.global().renderer, camera, scene, category);
      inspectorGlobalScope = true;
    } else if (handbookAction.type == handbook::ActionType::ApplyToB) {
      if (renderStack.selectedIndex() == 0) {
        renderStack.select(0);
        if (renderStack.passes().size() < 2) renderStack.duplicateSelected();
        else renderStack.select(1);
      }
      RenderPass comparison = materializeRenderPass(renderStack, renderStack.selectedIndex(), 0.0f);
      applyHandbookExample(handbookAction.example, true, comparison.renderer, camera, scene, category);
      replaceRenderPassOverrides(renderStack.selected(), renderStack.global(), comparison);
      inspectorGlobalScope = false;
    } else if (handbookAction.type == handbook::ActionType::LoadComparison) {
      renderStack = RenderStack{};
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
      compare = CompareMode::Split;
      inspectorGlobalScope = false;
    }
    normalizeDocument(hardwareProfile);
    const bool viewportInteraction = viewportHovered && (ImGui::IsMouseDown(ImGuiMouseButton_Left) ||
      ImGui::IsMouseDown(ImGuiMouseButton_Middle) || ImGui::IsMouseDown(ImGuiMouseButton_Right));
    editorHistory.observe(captureEditorSnapshot(renderStack, camera, scene, hardwareProfile, animationTimeline,
      importedModel),
      ImGui::IsAnyItemActive() || viewportInteraction);

    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.08f, 0.085f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}

} // namespace gfxlab
