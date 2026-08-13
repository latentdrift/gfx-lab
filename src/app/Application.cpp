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
#include "app/RenderStack.hpp"
#include "assets/ModelAsset.hpp"
#include "handbook/Handbook.hpp"
#include "renderer/Renderer.hpp"
#include "ui/AnimationEditor.hpp"
#include "ui/Inspector.hpp"
#include "ui/PassInspector.hpp"

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

bool animationValueEqual(const glm::vec4& a, const glm::vec4& b, const int components) {
  for (int component = 0; component < components; ++component)
    if (std::abs(a[component] - b[component]) > 0.000001f) return false;
  return true;
}

void applyEditedPass(RenderPass& authored, const RenderPass& displayedBefore, RenderPass edited) {
  const RenderPass authoredBefore = authored;
  const PassAnimation editedAnimation = edited.animation;
  std::array<bool, static_cast<std::size_t>(AnimationProperty::Count)> propertyChanged{};
  std::array<glm::vec4, static_cast<std::size_t>(AnimationProperty::Count)> displayedValues{};
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    const std::size_t propertyIndex = static_cast<std::size_t>(index);
    displayedValues[propertyIndex] = animationPropertyValue(edited, property);
    propertyChanged[propertyIndex] = !animationValueEqual(animationPropertyValue(displayedBefore, property),
      displayedValues[propertyIndex], animationPropertyInfo(property).components);
    setAnimationPropertyValue(edited, property, animationPropertyValue(authoredBefore, property));
  }
  edited.animation = editedAnimation;
  authored = std::move(edited);
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    const std::size_t propertyIndex = static_cast<std::size_t>(index);
    if (propertyChanged[propertyIndex] && findPropertyTrack(authored, property) == nullptr)
      setAnimationPropertyValue(authored, property, displayedValues[propertyIndex]);
  }
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
  bool animationPanelOpen = true;
  bool previewAnimation = true;
  AnimationTimeline animationTimeline;
  double previousFrameTime = glfwGetTime();
  double configCopiedAt = -10.0;
  handbook::Handbook graphicsHandbook;

  if (std::getenv("GRAPHICS_LAB_VALIDATE_HANDBOOK")) {
    const ModelImportResult importedFixture = importModelAsset("tests/fixtures/import_triangle.obj");
    if (!importedFixture || importedFixture.asset->triangleCount != 1 ||
        importedFixture.asset->vertices.size() != 3 || !importedFixture.asset->hasTextureCoordinates ||
        importedFixture.asset->submeshes.size() != 1 || importedFixture.asset->materials.empty() ||
        importedFixture.asset->textures.size() != 1 ||
        importedFixture.asset->materials[importedFixture.asset->submeshes.front().materialIndex].baseColorTexture != 0 ||
        importedFixture.asset->textures.front().width != 2 || importedFixture.asset->textures.front().height != 2 ||
        std::abs((importedFixture.asset->sourceBoundsMaximum.x - importedFixture.asset->sourceBoundsMinimum.x) *
          importedFixture.asset->normalizationScale - 3.0f) > 0.0001f)
      fail("OBJ model, material, texture, or normalization import failed validation");
    const TextureImportResult importedTexture = importTextureAsset("tests/fixtures/import_checker.pgm");
    if (!importedTexture || importedTexture.asset->rgba8.size() != 16 || importedTexture.asset->contentHash == 0)
      fail("standalone texture import failed validation");
    if (importModelAsset("tests/fixtures/not_a_model.txt"))
      fail("model importer accepted an unsupported file type");
    while (glGetError() != GL_NO_ERROR) {}
    renderer.setImportedModel(*importedFixture.asset);
    renderer.render(RendererState{}, CameraOrbit{}, TestScene::ImportedModel, false);
    RenderStack textureRenderValidation;
    textureRenderValidation.selected().textureSource = TextureSource::ImportedOverride;
    textureRenderValidation.selected().importedTexture = importedTexture.asset;
    renderer.renderPass(textureRenderValidation.selected(), CameraOrbit{}, TestScene::ImportedModel, 0);
    textureRenderValidation.selected().textureSource = TextureSource::White;
    renderer.renderPass(textureRenderValidation.selected(), CameraOrbit{}, TestScene::ImportedModel, 0);
    if (glGetError() != GL_NO_ERROR) fail("scene-material or override texture rendering failed validation");
    renderer.clearImportedModel();
    RenderStack validationStack;
    if (validationStack.passes().size() != 2 || !validationStack.duplicateSelected() ||
        validationStack.passes().size() != 3 || !validationStack.moveSelected(-1) ||
        !validationStack.removeSelected() || validationStack.passes().size() != 2)
      fail("render-pass stack operations failed validation");
    RenderStack animationValidation;
    animationValidation.select(1);
    animationValidation.selected().perturbation.modelTranslation.x = 0.0f;
    animationValidation.selected().composite.gain = 1.0f;
    setPropertyKeyframe(animationValidation.selected(), AnimationProperty::ModelTranslation, 0.0f);
    setPropertyKeyframe(animationValidation.selected(), AnimationProperty::CompositeGain, 0.0f);
    animationValidation.selected().perturbation.modelTranslation.x = 2.0f;
    animationValidation.selected().composite.gain = 5.0f;
    setPropertyKeyframe(animationValidation.selected(), AnimationProperty::ModelTranslation, 2.0f);
    setPropertyKeyframe(animationValidation.selected(), AnimationProperty::CompositeGain, 2.0f);
    const RenderStack evaluatedAnimation = evaluateRenderStack(animationValidation, 1.0f);
    if (std::abs(evaluatedAnimation.selected().perturbation.modelTranslation.x - 1.0f) > 0.0001f ||
        std::abs(evaluatedAnimation.selected().composite.gain - 3.0f) > 0.0001f ||
        !removePropertyKeyframe(animationValidation.selected(), AnimationProperty::ModelTranslation, 2.0f) ||
        findPropertyTrack(animationValidation.selected(), AnimationProperty::CompositeGain) == nullptr ||
        animationValidation.selected().animation.tracks.size() != 2)
      fail("render-pass keyframe interpolation failed validation");
    PropertyAnimationTrack* gainTrack = findPropertyTrack(animationValidation.selected(),
      AnimationProperty::CompositeGain);
    gainTrack->interpolation = KeyframeInterpolation::Step;
    if (std::abs(evaluateRenderStack(animationValidation, 1.0f).selected().composite.gain - 1.0f) > 0.0001f ||
        !propertyHasKeyAt(animationValidation.selected(), AnimationProperty::CompositeGain, 2.0f))
      fail("independent property tracks or step interpolation failed validation");
    AnimationTimeline timelineValidation;
    timelineValidation.durationSeconds = 2.0f;
    timelineValidation.timeSeconds = 1.5f;
    timelineValidation.playing = true;
    timelineValidation.advance(1.0f);
    if (std::abs(timelineValidation.timeSeconds - 0.5f) > 0.0001f)
      fail("animation timeline looping failed validation");
    RenderStack historyStack;
    AnimationTimeline historyTimeline;
    CameraOrbit historyCamera;
    TestScene historyScene = TestScene::Torus;
    HardwareProfile historyProfile = HardwareProfile::Unrestricted;
    const EditorSnapshot historyInitial = captureEditorSnapshot(historyStack, historyCamera, historyScene,
      historyProfile, historyTimeline);
    EditorHistory historyValidation(historyInitial);
    historyStack.select(0);
    historyStack.duplicateSelected();
    historyStack.selected().renderer.lighting.ambient = 0.4f;
    historyStack.selected().perturbation.uvOffset = {0.25f, -0.125f};
    setPropertyKeyframe(historyStack.selected(), AnimationProperty::UvOffset, 1.0f);
    historyCamera.yaw = 1.25f;
    historyScene = TestScene::Lighting;
    historyProfile = HardwareProfile::Nintendo64;
    historyTimeline.durationSeconds = 9.0f;
    historyValidation.observe(captureEditorSnapshot(historyStack, historyCamera, historyScene, historyProfile,
      historyTimeline, importedFixture.asset), true);
    historyStack.selected().renderer.lighting.ambient = 0.7f;
    historyStack.selected().perturbation.cameraYaw = 0.2f;
    historyValidation.observe(captureEditorSnapshot(historyStack, historyCamera, historyScene, historyProfile,
      historyTimeline, importedFixture.asset), true);
    const EditorSnapshot historyChanged = captureEditorSnapshot(historyStack, historyCamera, historyScene,
      historyProfile, historyTimeline, importedFixture.asset);
    historyValidation.observe(historyChanged, false);
    EditorSnapshot historyRestored;
    if (!historyValidation.undo(historyChanged, historyRestored) ||
        std::abs(historyRestored.renderStack.selected().renderer.lighting.ambient - 0.22f) > 0.0001f ||
        historyRestored.renderStack.passes().size() != 2 || historyRestored.scene != TestScene::Torus ||
        historyRestored.hardwareProfile != HardwareProfile::Unrestricted ||
        historyRestored.importedModel != nullptr ||
        std::abs(historyRestored.camera.yaw - CameraOrbit{}.yaw) > 0.0001f ||
        std::abs(historyRestored.timeline.durationSeconds - 4.0f) > 0.0001f ||
        historyValidation.canUndo() || !historyValidation.canRedo())
      fail("editor history undo or interaction coalescing failed validation");
    if (!historyValidation.redo(historyRestored, historyRestored) ||
        std::abs(historyRestored.renderStack.selected().renderer.lighting.ambient - 0.7f) > 0.0001f ||
        std::abs(historyRestored.renderStack.selected().perturbation.cameraYaw - 0.2f) > 0.0001f ||
        historyRestored.renderStack.passes().size() != 3 ||
        historyRestored.renderStack.selected().animation.tracks.size() != 1 ||
        historyRestored.scene != TestScene::Lighting ||
        historyRestored.hardwareProfile != HardwareProfile::Nintendo64 ||
        historyRestored.importedModel == nullptr ||
        historyRestored.importedModel->contentHash != importedFixture.asset->contentHash ||
        std::abs(historyRestored.camera.yaw - 1.25f) > 0.0001f ||
        std::abs(historyRestored.timeline.durationSeconds - 9.0f) > 0.0001f)
      fail("editor history redo failed validation");
    RenderStack compositeValidation;
    compositeValidation.select(1);
    compositeValidation.duplicateSelected();
    compositeValidation.passes()[1].perturbation.cameraYaw = 0.01f;
    compositeValidation.passes()[2].perturbation.uvOffset = {1.0f / 256.0f, 0.0f};
    compositeValidation.passes()[2].textureSource = TextureSource::ImportedOverride;
    compositeValidation.passes()[2].importedTexture = importedTexture.asset;
    compositeValidation.passes()[2].composite.operation = RelationOperator::Exclusion;
    for (std::size_t passIndex = 0; passIndex < compositeValidation.passes().size(); ++passIndex)
      renderer.renderPass(compositeValidation.passes()[passIndex], camera, scene, passIndex);
    if (renderer.composite(compositeValidation) == 0)
      fail("sequential render-pass compositing failed validation");
    for (int mask = static_cast<int>(CompositeMask::None); mask <= static_cast<int>(CompositeMask::PassEdges); ++mask) {
      compositeValidation.passes()[2].composite.mask = static_cast<CompositeMask>(mask);
      compositeValidation.passes()[2].composite.invertMask = mask != static_cast<int>(CompositeMask::None);
      if (renderer.composite(compositeValidation) == 0) fail("render-pass composite mask failed validation");
    }
    const std::string stackConfig = renderStackConfigJson(compositeValidation, camera, scene,
      HardwareProfile::Unrestricted, nullptr, importedFixture.asset.get());
    if (stackConfig.find("graphics-lab.render-stack.v1") == std::string::npos ||
        stackConfig.find("\"passes\"") == std::string::npos ||
        stackConfig.find("\"perturbation\"") == std::string::npos ||
        stackConfig.find("\"composite_into_previous\"") == std::string::npos ||
        stackConfig.find("\"animation\"") == std::string::npos ||
        stackConfig.find("\"property_tracks\"") == std::string::npos ||
        stackConfig.find("\"texture_source\": \"imported_override\"") == std::string::npos ||
        stackConfig.find("\"imported_texture\"") == std::string::npos ||
        stackConfig.find("\"imported_model\"") == std::string::npos)
      fail("render-pass stack missing from config export");
    constexpr std::array examples = {handbook::Example::VertexQuantization, handbook::Example::Projection,
      handbook::Example::AffineMapping, handbook::Example::TextureMinification, handbook::Example::NormalMapping,
      handbook::Example::LightingInterpolation, handbook::Example::DepthPrecision, handbook::Example::Transparency,
      handbook::Example::Stencil, handbook::Example::LinearLight, handbook::Example::ColorQuantization,
      handbook::Example::InternalResolution, handbook::Example::ShadowMapping, handbook::Example::Overdraw,
      handbook::Example::ClutTextures, handbook::Example::VertexDepthCue,
      handbook::Example::Ps1Semitransparency, handbook::Example::OrderingTable,
      handbook::Example::N64ThreePoint, handbook::Example::N64Combiner,
      handbook::Example::N64TextureFormats, handbook::Example::N64Mipmap,
      handbook::Example::N64Coverage, handbook::Example::N64VideoInterface};
    for (handbook::Example example : examples) {
      for (bool alternative : {false, true}) {
        applyHandbookExample(example, alternative, current, camera, scene, category);
        renderer.render(current, camera, scene, alternative);
      }
    }
    for (int operation = static_cast<int>(RelationOperator::AbsoluteDifference);
         operation <= static_cast<int>(RelationOperator::RelativeDifference); ++operation)
      renderer.renderRelation(static_cast<RelationOperator>(operation), 2.0f, 0.5f);
    const std::string relationConfig = relationConfigJson(current, reference, camera, scene,
      HardwareProfile::Unrestricted, RelationOperator::AbsoluteDifference, 4.0f, 0.0f);
    if (relationConfig.find("graphics-lab.render-algebra.v1") == std::string::npos ||
        relationConfig.find("\"renderer_a\"") == std::string::npos ||
        relationConfig.find("\"renderer_b\"") == std::string::npos)
      fail("render-algebra state missing from config export");
    applyRecommendedSetup(TestScene::Transparency, current, camera);
    scene = TestScene::Transparency;
    for (int transparencyMode = 6; transparencyMode <= 9; ++transparencyMode) {
      current.surface.transparency = transparencyMode;
      for (bool orderingTable : {false, true}) {
        current.depth.orderingTable = orderingTable;
        renderer.render(current, camera, scene, false);
      }
    }
    applyRecommendedSetup(TestScene::Torus, current, camera);
    scene = TestScene::Torus;
    current.lighting.depthCue = true;
    for (int textureColorMode = 0; textureColorMode <= 2; ++textureColorMode) {
      current.texture.colorMode = textureColorMode;
      renderer.render(current, camera, scene, false);
    }
    current = RendererState{};
    current.camera.orthographic = true;
    current.rasterization.samples = 8;
    current.surface.normalMapping = true;
    current.texture.mipmapping = true;
    current.lighting.model = 4;
    current.lighting.shadows = true;
    current.depth.testing = true;
    current.stencil.enabled = true;
    current.color.bitsPerChannel = 8;
    current.color.linearLight = true;
    current.post.fog = true;
    current.output.width = 1280;
    current.output.height = 960;
    normalizeForHardwareProfile(HardwareProfile::PlayStation, current);
    if (current.camera.orthographic || !current.rasterization.affineMapping || current.rasterization.samples != 1 ||
        current.surface.normalMapping || !current.texture.nearestFiltering || current.texture.mipmapping ||
        current.lighting.model > 1 || current.lighting.shadows || !current.depth.testing || !current.depth.writing ||
        current.stencil.enabled || current.color.bitsPerChannel != 5 || current.color.linearLight || current.post.fog ||
        current.output.width > 320 || current.output.height > 240 || !current.output.nearestUpscaling)
      fail("PlayStation hardware profile did not normalize renderer state");
    for (int sceneIndex = 0; sceneIndex < 5; ++sceneIndex)
      renderer.render(current, camera, static_cast<TestScene>(sceneIndex), false);
    if (configJson(current, camera, TestScene::Torus, HardwareProfile::PlayStation).find(
          "\"hardware_target\": \"sony_playstation_ps1\"") == std::string::npos)
      fail("hardware target missing from renderer config export");
    current = RendererState{};
    normalizeForHardwareProfile(HardwareProfile::Nintendo64, current);
    if (!current.n64.enabled || current.lighting.model > 1 || current.surface.normalMapping ||
        !current.depth.testing || current.stencil.enabled || current.color.linearLight ||
        current.rasterization.samples != 4)
      fail("Nintendo 64 hardware profile did not normalize renderer state");
    for (int textureFormat = 0; textureFormat <= 8; ++textureFormat) {
      current.n64.textureFormat = textureFormat;
      for (int textureFilter = 0; textureFilter <= 2; ++textureFilter) {
        current.n64.textureFilter = textureFilter;
        renderer.render(current, camera, TestScene::Torus, false);
      }
    }
    for (int mipmapMode = 0; mipmapMode <= 4; ++mipmapMode) {
      current.n64.mipmapMode = mipmapMode;
      current.n64.cycleType = mipmapMode >= 2 ? 2 : 1;
      renderer.render(current, camera, TestScene::TexturePlane, false);
    }
    current.n64.alphaCompare = 2;
    current.n64.textureGeneration = true;
    current.n64.viDivot = true;
    renderer.render(current, camera, TestScene::Torus, false);
    const std::string n64Config = configJson(current, camera, TestScene::Torus, HardwareProfile::Nintendo64);
    if (n64Config.find("\"hardware_target\": \"nintendo_64\"") == std::string::npos ||
        n64Config.find("\"n64_rdp\"") == std::string::npos ||
        n64Config.find("\"combiner_equation\": \"(a - b) * c + d\"") == std::string::npos)
      fail("Nintendo 64 state missing from renderer config export");
    current = RendererState{};
    reference = current;
    camera = CameraOrbit{};
    scene = TestScene::Torus;
    category = Category::Geometry;
  }

  RenderStack renderStack;
  renderStack.passes()[0].renderer = current;
  renderStack.passes()[1].renderer = reference;
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
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Graphics Lab", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("GRAPHICS LAB");
    ImGui::SameLine(125);
    ImGui::BeginDisabled(!editorHistory.canUndo());
    if (ImGui::Button("Undo")) restoreHistory(false);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Undo  Ctrl+Z");
    ImGui::SameLine();
    ImGui::BeginDisabled(!editorHistory.canRedo());
    if (ImGui::Button("Redo")) restoreHistory(true);
    ImGui::EndDisabled();
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled)) ImGui::SetTooltip("Redo  Ctrl+Shift+Z or Ctrl+Y");
    ImGui::SameLine();
    ImGui::TextDisabled("Target");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    int hardwareProfileIndex = static_cast<int>(hardwareProfile);
    const char* hardwareProfileLabels[] = {"Unrestricted", "PlayStation (PS1)", "Nintendo 64"};
    if (ImGui::Combo("##hardware-profile", &hardwareProfileIndex, hardwareProfileLabels, 3)) {
      hardwareProfile = static_cast<HardwareProfile>(hardwareProfileIndex);
      if (hardwareProfile == HardwareProfile::Unrestricted) {
        for (RenderPass& pass : renderStack.passes()) pass.renderer.n64.enabled = false;
      }
      for (RenderPass& pass : renderStack.passes()) normalizeForHardwareProfile(hardwareProfile, pass.renderer);
      if (!categoryAvailableForHardwareProfile(hardwareProfile, category)) category = Category::Geometry;
      if (hardwareProfile != HardwareProfile::Unrestricted && scene == TestScene::StencilMask) scene = TestScene::Torus;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hardwareProfileDescription(hardwareProfile));
    ImGui::SameLine();
    ImGui::TextDisabled("Scene");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    const char* standardSceneLabels[] = {"Torus", "Texture minification", "Depth precision", "Transparency",
      "Lighting comparison", "Stencil mask"};
    const char* currentSceneLabel = scene == TestScene::ImportedModel && importedModel != nullptr
      ? importedModel->name.c_str() : standardSceneLabels[std::min(static_cast<int>(scene), 5)];
    if (ImGui::BeginCombo("##test-scene", currentSceneLabel)) {
      for (int option = 0; option < 5; ++option) {
        const TestScene candidate = static_cast<TestScene>(option);
        if (ImGui::Selectable(standardSceneLabels[option], scene == candidate)) scene = candidate;
      }
      if (hardwareProfile == HardwareProfile::Unrestricted &&
          ImGui::Selectable(standardSceneLabels[5], scene == TestScene::StencilMask))
        scene = TestScene::StencilMask;
      if (importedModel != nullptr &&
          ImGui::Selectable(importedModel->name.c_str(), scene == TestScene::ImportedModel))
        scene = TestScene::ImportedModel;
      ImGui::EndCombo();
    }
    ImGui::SameLine();
    if (ImGui::Button("Import model")) {
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
      if (!modelImportError.empty()) ImGui::OpenPopup("Model import failed");
    }
    if (ImGui::BeginPopupModal("Model import failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
      ImGui::TextWrapped("%s", modelImportError.c_str());
      if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
      ImGui::EndPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset neutral")) renderStack.selected().renderer = RendererState{};
    ImGui::SameLine();
    if (ImGui::Button("Reset scene setup")) applyRecommendedSetup(scene, renderStack.selected().renderer, camera);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Apply the recommended renderer state and camera framing for the selected scene.");
    ImGui::SameLine();
    if (ImGui::Button("Copy stack JSON")) {
      const std::string exportedConfig = renderStackConfigJson(renderStack, camera, scene, hardwareProfile,
        &animationTimeline, importedModel.get());
      ImGui::SetClipboardText(exportedConfig.c_str());
      configCopiedAt = glfwGetTime();
    }
    if (glfwGetTime() - configCopiedAt < 2.0) {
      ImGui::SameLine();
      ImGui::TextDisabled("Copied");
    }
    ImGui::SameLine();
    if (ImGui::Button("Handbook")) graphicsHandbook.open();
    ImGui::SameLine();
    if (ImGui::Button(animationPanelOpen ? "Hide animation" : "Animation"))
      animationPanelOpen = !animationPanelOpen;
    ImGui::TextDisabled("Compare:");
    ImGui::SameLine();
    if (ImGui::RadioButton("Selected pass", compare == CompareMode::A)) compare = CompareMode::A;
    ImGui::SameLine();
    if (ImGui::RadioButton("Base pass", compare == CompareMode::B)) compare = CompareMode::B;
    ImGui::SameLine();
    if (ImGui::RadioButton("Split base/selected", compare == CompareMode::Split)) compare = CompareMode::Split;
    ImGui::SameLine();
    if (ImGui::RadioButton("Composite stack", compare == CompareMode::Relation)) compare = CompareMode::Relation;
    ImGui::SameLine(ImGui::GetWindowWidth() - 260);
    ImGui::TextDisabled("LMB orbit   MMB/RMB pan   Wheel zoom");
    ImGui::Separator();
    if (animationPanelOpen) {
      drawAnimationEditor(animationTimeline, renderStack, previewAnimation);
      ImGui::Separator();
    }

    const float contentHeight = ImGui::GetContentRegionAvail().y;
    constexpr float pipelineWidth = 200.0f;
    ImGui::BeginChild("Pipeline", ImVec2(pipelineWidth, contentHeight), true);
    if (importedModel != nullptr) {
      ImGui::TextDisabled("IMPORTED MODEL");
      ImGui::TextWrapped("%s", importedModel->name.c_str());
      ImGui::TextDisabled("%zu triangles  %zu meshes", importedModel->triangleCount,
        importedModel->sourceMeshCount);
      ImGui::TextDisabled("UV0 %s   colors %s", importedModel->hasTextureCoordinates ? "yes" : "no",
        importedModel->hasVertexColors ? "yes" : "no");
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s\nLongest source bounds extent normalized to 3.0 lab units.",
          importedModel->sourcePath.c_str());
      if (scene != TestScene::ImportedModel && ImGui::Button("Use model", ImVec2(-1, 0)))
        scene = TestScene::ImportedModel;
      if (ImGui::Button("Unload model", ImVec2(-1, 0))) {
        importedModel.reset();
        renderer.clearImportedModel();
        if (scene == TestScene::ImportedModel) scene = TestScene::Torus;
      }
      ImGui::Separator();
    }
    ImGui::TextDisabled("RENDER PASSES");
    ImGui::Spacing();
    for (std::size_t passIndex = 0; passIndex < renderStack.passes().size(); ++passIndex) {
      RenderPass& pass = renderStack.passes()[passIndex];
      ImGui::PushID(static_cast<int>(passIndex));
      ImGui::Checkbox("##enabled", &pass.enabled);
      ImGui::SameLine();
      if (ImGui::Selectable(pass.name.c_str(), renderStack.selectedIndex() == passIndex, 0, ImVec2(0, 24)))
        renderStack.select(passIndex);
      ImGui::PopID();
    }
    if (ImGui::Button("Duplicate pass", ImVec2(-1, 0))) renderStack.duplicateSelected();
    if (ImGui::Button("Up")) renderStack.moveSelected(-1);
    ImGui::SameLine();
    if (ImGui::Button("Down")) renderStack.moveSelected(1);
    ImGui::SameLine();
    ImGui::BeginDisabled(renderStack.passes().size() <= 1);
    if (ImGui::Button("Delete")) renderStack.removeSelected();
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::TextDisabled("PIPELINE");
    ImGui::Spacing();
    constexpr std::array<Category, 11> categories = {Category::Geometry, Category::Camera, Category::Rasterization,
      Category::Surface, Category::Texture, Category::Lighting, Category::Depth, Category::Stencil, Category::Color,
      Category::Post, Category::Output};
    for (Category candidate : categories) {
      if (!categoryAvailableForHardwareProfile(hardwareProfile, candidate)) continue;
      if (ImGui::Selectable(categoryName(candidate), category == candidate, 0, ImVec2(0, 28))) category = candidate;
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 5);

    const float inspectorWidth = 340;
    const float viewportWidth = std::max(100.0f, ImGui::GetContentRegionAvail().x - inspectorWidth - 5);
    ImGui::BeginChild("Viewport", ImVec2(viewportWidth, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 paneOrigin = ImGui::GetCursorScreenPos();
    constexpr float cameraWidth = 960.0f;
    constexpr float cameraHeight = 720.0f;
    const float presentationScale = std::min(available.x / cameraWidth, available.y / cameraHeight);
    const ImVec2 presentationSize(cameraWidth * presentationScale, cameraHeight * presentationScale);
    const ImVec2 origin(
      paneOrigin.x + std::floor((available.x - presentationSize.x) * 0.5f),
      paneOrigin.y + std::floor((available.y - presentationSize.y) * 0.5f));
    const ImVec2 end(origin.x + presentationSize.x, origin.y + presentationSize.y);
    for (RenderPass& pass : renderStack.passes()) normalizeForHardwareProfile(hardwareProfile, pass.renderer);
    const bool evaluateAnimation = animationTimeline.playing || previewAnimation;
    RenderStack evaluatedStack = evaluateAnimation
      ? evaluateRenderStack(renderStack, animationTimeline.timeSeconds) : renderStack;
    for (RenderPass& pass : evaluatedStack.passes()) normalizeForHardwareProfile(hardwareProfile, pass.renderer);
    std::array<GLuint, RenderStack::maximumPasses> passTextures{};
    for (std::size_t passIndex = 0; passIndex < evaluatedStack.passes().size(); ++passIndex) {
      passTextures[passIndex] = renderer.renderPass(evaluatedStack.passes()[passIndex], camera, scene, passIndex);
    }
    const GLuint selectedTexture = passTextures[renderStack.selectedIndex()];
    const GLuint baseTexture = passTextures.front();
    const GLuint renderedComposite = renderer.composite(evaluatedStack);
    const GLuint compositeTexture = renderedComposite != 0 ? renderedComposite : selectedTexture;
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, IM_COL32(27, 29, 31, 255));
    if (compare == CompareMode::Split) {
      const float middle = origin.x + std::floor(presentationSize.x * 0.5f);
      draw->PushClipRect(origin, ImVec2(middle, end.y), true);
      draw->AddImage(static_cast<ImTextureID>(baseTexture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      draw->PopClipRect();
      draw->PushClipRect(ImVec2(middle + 1, origin.y), end, true);
      draw->AddImage(static_cast<ImTextureID>(selectedTexture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      draw->PopClipRect();
      draw->AddLine(ImVec2(middle, origin.y), ImVec2(middle, end.y), IM_COL32(225, 225, 225, 210));
      draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240,240,240,220), "BASE PASS");
      draw->AddText(ImVec2(middle + 10, origin.y + 8), IM_COL32(240,240,240,220),
        renderStack.selected().name.c_str());
    } else {
      const GLuint texture = compare == CompareMode::A ? selectedTexture
        : compare == CompareMode::Relation ? compositeTexture : baseTexture;
      draw->AddImage(static_cast<ImTextureID>(texture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      const char* label = compare == CompareMode::A ? renderStack.selected().name.c_str()
        : compare == CompareMode::B ? "BASE PASS" : "COMPOSITE STACK";
      draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240,240,240,220), label);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("viewport-input", presentationSize);
    viewportHovered = ImGui::IsItemHovered();
    ImGui::EndChild();
    ImGui::SameLine(0, 5);

    ImGui::BeginChild("Inspector", ImVec2(inspectorWidth, contentHeight), true);
      RenderStack inspectorStack = renderStack;
      const RenderPass displayedBefore = (animationTimeline.playing || previewAnimation)
        ? evaluateRenderPass(renderStack.selected(), animationTimeline.timeSeconds) : renderStack.selected();
      inspectorStack.selected() = displayedBefore;
      drawPassInspector(inspectorStack, animationTimeline);
      ImGui::Spacing();
      ImGui::Separator();
      drawInspector(category, inspectorStack.selected(), hardwareProfile, animationTimeline);
      applyEditedPass(renderStack.selected(), displayedBefore, inspectorStack.selected());
    ImGui::EndChild();
    ImGui::End();

    const handbook::Action handbookAction = graphicsHandbook.draw(hardwareProfile);
    if (handbookAction.type != handbook::ActionType::None && isNintendo64Example(handbookAction.example))
      hardwareProfile = HardwareProfile::Nintendo64;
    if (handbookAction.type == handbook::ActionType::ApplyToA) {
      applyHandbookExample(handbookAction.example, false, renderStack.passes().front().renderer, camera, scene, category);
    } else if (handbookAction.type == handbook::ActionType::ApplyToB) {
      if (renderStack.selectedIndex() == 0) {
        renderStack.select(0);
        if (renderStack.passes().size() < 2) renderStack.duplicateSelected();
        else renderStack.select(1);
      }
      applyHandbookExample(handbookAction.example, true, renderStack.selected().renderer, camera, scene, category);
    } else if (handbookAction.type == handbook::ActionType::LoadComparison) {
      renderStack = RenderStack{};
      applyHandbookExample(handbookAction.example, false, renderStack.passes()[0].renderer, camera, scene, category);
      RendererState comparisonState;
      CameraOrbit comparisonCamera = camera;
      TestScene comparisonScene = scene;
      Category comparisonCategory = category;
      applyHandbookExample(handbookAction.example, true, comparisonState, comparisonCamera, comparisonScene, comparisonCategory);
      renderStack.passes()[1].renderer = comparisonState;
      renderStack.select(1);
      camera = comparisonCamera;
      scene = comparisonScene;
      category = comparisonCategory;
      compare = CompareMode::Split;
    }
    for (RenderPass& pass : renderStack.passes()) normalizeForHardwareProfile(hardwareProfile, pass.renderer);
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
