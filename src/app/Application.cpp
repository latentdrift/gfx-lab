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

void applyEditedLocalPass(RenderStack& stack, const RenderPass& displayedBefore, const RenderPass& edited,
    const float timeSeconds = 0.0f) {
  RenderPass& definition = stack.selected();
  const RenderPass definitionBefore = definition;
  definition.name = edited.name;
  definition.enabled = edited.enabled;
  definition.output = edited.output;
  definition.composite = edited.composite;
  definition.animation = edited.animation;
  const RenderPass evaluatedGlobal = evaluateRenderPass(stack.global(), timeSeconds);
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    const glm::vec4 displayedValue = animationPropertyValue(displayedBefore, property);
    const glm::vec4 editedValue = animationPropertyValue(edited, property);
    if (animationPropertyValuesEqual(property, displayedValue, editedValue)) continue;
    if (findPropertyTrack(edited, property) != nullptr) continue;
    if (animationPropertyIsPassLocal(property)) {
      setAnimationPropertyValue(definition, property, editedValue);
      continue;
    }
    const glm::vec4 globalValue = animationPropertyValue(evaluatedGlobal, property);
    if (animationPropertyValuesEqual(property, globalValue, editedValue))
      static_cast<void>(clearRenderPassOverride(definition, property));
    else
      setRenderPassOverride(definition, property, editedValue);
  }
  if (edited.importedTexture != displayedBefore.importedTexture) {
    definition.importedTextureOverride = edited.importedTexture != stack.global().importedTexture;
    definition.importedTexture = definition.importedTextureOverride ? edited.importedTexture : nullptr;
  } else {
    definition.importedTextureOverride = definitionBefore.importedTextureOverride;
    definition.importedTexture = definitionBefore.importedTexture;
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
    validationStack.global().renderer.lighting.ambient = 0.31f;
    validationStack.global().perturbation.modelTranslation = {0.2f, 0.0f, 0.0f};
    setRenderPassOverride(validationStack.selected(), AnimationProperty::Ambient, glm::vec4(0.77f));
    setRenderPassOverride(validationStack.selected(), AnimationProperty::UvOffset,
      glm::vec4(0.125f, 0.0f, 0.0f, 0.0f));
    validationStack.global().renderer.post.fogStart = 2.0f;
    setPropertyKeyframe(validationStack.global(), AnimationProperty::FogStart, 0.0f);
    validationStack.global().renderer.post.fogStart = 6.0f;
    setPropertyKeyframe(validationStack.global(), AnimationProperty::FogStart, 2.0f);
    validationStack.global().renderer.lighting.ambient = 0.0f;
    setPropertyKeyframe(validationStack.global(), AnimationProperty::Ambient, 0.0f);
    validationStack.global().renderer.lighting.ambient = 1.0f;
    setPropertyKeyframe(validationStack.global(), AnimationProperty::Ambient, 2.0f);
    validationStack.selected().renderer.lighting.ambient = 0.6f;
    setPropertyKeyframe(validationStack.selected(), AnimationProperty::Ambient, 0.0f);
    validationStack.selected().renderer.lighting.ambient = 0.8f;
    setPropertyKeyframe(validationStack.selected(), AnimationProperty::Ambient, 2.0f);
    const RenderPass hierarchicalPass = materializeRenderPass(validationStack,
      validationStack.selectedIndex(), 1.0f);
    const RenderPass staticallyResolvedPass = resolveRenderPass(validationStack, validationStack.selectedIndex());
    if (std::abs(hierarchicalPass.renderer.lighting.ambient - 0.7f) > 0.0001f ||
        std::abs(hierarchicalPass.perturbation.modelTranslation.x - 0.2f) > 0.0001f ||
        std::abs(hierarchicalPass.perturbation.uvOffset.x - 0.125f) > 0.0001f ||
        std::abs(hierarchicalPass.renderer.post.fogStart - 4.0f) > 0.0001f ||
        std::abs(staticallyResolvedPass.renderer.post.fogStart - 6.0f) > 0.0001f ||
        validationStack.selected().overrides.size() != 2)
      fail("global base, sparse override, or hierarchical animation precedence failed validation");
    validationStack.duplicateSelected();
    if (validationStack.selected().overrides.size() != 2 ||
        findPropertyTrack(validationStack.selected(), AnimationProperty::Ambient) == nullptr)
      fail("pass duplication did not preserve only its authored local deviations");
    RenderStack editScopeValidation;
    editScopeValidation.global().renderer.lighting.ambient = 0.2f;
    const RenderPass inheritedBefore = materializeRenderPass(editScopeValidation, 1, 0.0f);
    RenderPass locallyEdited = inheritedBefore;
    locallyEdited.renderer.lighting.ambient = 0.8f;
    editScopeValidation.select(1);
    applyEditedLocalPass(editScopeValidation, inheritedBefore, locallyEdited);
    editScopeValidation.global().renderer.lighting.ambient = 0.4f;
    if (std::abs(materializeRenderPass(editScopeValidation, 1).renderer.lighting.ambient - 0.8f) > 0.0001f ||
        findRenderPassOverride(editScopeValidation.selected(), AnimationProperty::Ambient) == nullptr)
      fail("local inspector edit did not create a stable sparse override");
    const RenderPass overriddenBefore = materializeRenderPass(editScopeValidation, 1, 0.0f);
    RenderPass reverted = overriddenBefore;
    reverted.renderer.lighting.ambient = 0.4f;
    applyEditedLocalPass(editScopeValidation, overriddenBefore, reverted);
    if (findRenderPassOverride(editScopeValidation.selected(), AnimationProperty::Ambient) != nullptr)
      fail("local inspector edit matching the global base did not restore inheritance");
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
    animationValidation.selected().renderer.surface.wireframe = false;
    setPropertyKeyframe(animationValidation.selected(), AnimationProperty::WireframeOverlay, 0.0f);
    animationValidation.selected().renderer.surface.wireframe = true;
    setPropertyKeyframe(animationValidation.selected(), AnimationProperty::WireframeOverlay, 2.0f);
    const PropertyAnimationTrack* wireframeTrack = findPropertyTrack(animationValidation.selected(),
      AnimationProperty::WireframeOverlay);
    if (wireframeTrack == nullptr || wireframeTrack->interpolation != KeyframeInterpolation::Step ||
        evaluateRenderStack(animationValidation, 1.0f).selected().renderer.surface.wireframe ||
        !evaluateRenderStack(animationValidation, 2.0f).selected().renderer.surface.wireframe ||
        animationPropertyInfo(AnimationProperty::MultisampleCount).behavior != AnimationBehavior::NotAnimatable ||
        animationPropertyInfo(AnimationProperty::WireframeOverlay).kind != AnimationValueKind::Boolean)
      fail("typed stepped animation property validation failed");
    RenderPass catalogValidation;
    std::size_t animatablePropertyCount = 0;
    for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
      const AnimationProperty property = static_cast<AnimationProperty>(index);
      setPropertyKeyframe(catalogValidation, property, 0.0f);
      const PropertyAnimationTrack* track = findPropertyTrack(catalogValidation, property);
      if (animationPropertyIsAnimatable(property)) {
        ++animatablePropertyCount;
        if (track == nullptr || (animationPropertyInfo(property).behavior == AnimationBehavior::Step &&
            track->interpolation != KeyframeInterpolation::Step))
          fail("typed animation catalog omitted or mistyped an animatable property");
      } else if (track != nullptr) {
        fail("typed animation catalog keyed a GPU-resource allocation property");
      }
    }
    if (catalogValidation.animation.tracks.size() != animatablePropertyCount || animatablePropertyCount < 90)
      fail("typed animation catalog coverage failed validation");
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
    setRenderPassOverride(historyStack.selected(), AnimationProperty::Ambient, glm::vec4(0.4f));
    setRenderPassOverride(historyStack.selected(), AnimationProperty::UvOffset, glm::vec4(0.25f, -0.125f, 0, 0));
    setRenderPassOverride(historyStack.selected(), AnimationProperty::TextureSource,
      glm::vec4(static_cast<float>(TextureSource::ImportedOverride)));
    historyStack.selected().importedTexture = importedTexture.asset;
    historyStack.selected().importedTextureOverride = true;
    setPropertyKeyframe(historyStack.selected(), AnimationProperty::UvOffset, 1.0f);
    historyCamera.yaw = 1.25f;
    historyScene = TestScene::Lighting;
    historyProfile = HardwareProfile::Nintendo64;
    historyTimeline.durationSeconds = 9.0f;
    historyStack.global().renderer.camera.nearPlane = 0.12f;
    historyValidation.observe(captureEditorSnapshot(historyStack, historyCamera, historyScene, historyProfile,
      historyTimeline, importedFixture.asset), true);
    setRenderPassOverride(historyStack.selected(), AnimationProperty::Ambient, glm::vec4(0.7f));
    setRenderPassOverride(historyStack.selected(), AnimationProperty::CameraYaw, glm::vec4(0.2f));
    historyValidation.observe(captureEditorSnapshot(historyStack, historyCamera, historyScene, historyProfile,
      historyTimeline, importedFixture.asset), true);
    const EditorSnapshot historyChanged = captureEditorSnapshot(historyStack, historyCamera, historyScene,
      historyProfile, historyTimeline, importedFixture.asset);
    historyValidation.observe(historyChanged, false);
    EditorSnapshot historyRestored;
    if (!historyValidation.undo(historyChanged, historyRestored) ||
        std::abs(materializeRenderPass(historyRestored.renderStack,
          historyRestored.renderStack.selectedIndex()).renderer.lighting.ambient - 0.22f) > 0.0001f ||
        historyRestored.renderStack.passes().size() != 2 || historyRestored.scene != TestScene::Torus ||
        historyRestored.hardwareProfile != HardwareProfile::Unrestricted ||
        historyRestored.importedModel != nullptr ||
        std::abs(historyRestored.renderStack.global().renderer.camera.nearPlane - 0.05f) > 0.0001f ||
        std::abs(historyRestored.camera.yaw - CameraOrbit{}.yaw) > 0.0001f ||
        std::abs(historyRestored.timeline.durationSeconds - 4.0f) > 0.0001f ||
        historyValidation.canUndo() || !historyValidation.canRedo())
      fail("editor history undo or interaction coalescing failed validation");
    if (!historyValidation.redo(historyRestored, historyRestored) ||
        std::abs(materializeRenderPass(historyRestored.renderStack,
          historyRestored.renderStack.selectedIndex()).renderer.lighting.ambient - 0.7f) > 0.0001f ||
        std::abs(materializeRenderPass(historyRestored.renderStack,
          historyRestored.renderStack.selectedIndex()).perturbation.cameraYaw - 0.2f) > 0.0001f ||
        historyRestored.renderStack.passes().size() != 3 ||
        historyRestored.renderStack.selected().animation.tracks.size() != 1 ||
        materializeRenderPass(historyRestored.renderStack,
          historyRestored.renderStack.selectedIndex()).textureSource != TextureSource::ImportedOverride ||
        materializeRenderPass(historyRestored.renderStack,
          historyRestored.renderStack.selectedIndex()).importedTexture == nullptr ||
        materializeRenderPass(historyRestored.renderStack,
          historyRestored.renderStack.selectedIndex()).importedTexture->contentHash != importedTexture.asset->contentHash ||
        historyRestored.scene != TestScene::Lighting ||
        historyRestored.hardwareProfile != HardwareProfile::Nintendo64 ||
        historyRestored.importedModel == nullptr ||
        historyRestored.importedModel->contentHash != importedFixture.asset->contentHash ||
        std::abs(historyRestored.renderStack.global().renderer.camera.nearPlane - 0.12f) > 0.0001f ||
        std::abs(historyRestored.camera.yaw - 1.25f) > 0.0001f ||
        std::abs(historyRestored.timeline.durationSeconds - 9.0f) > 0.0001f)
      fail("editor history redo failed validation");
    RenderStack compositeValidation;
    compositeValidation.select(1);
    compositeValidation.duplicateSelected();
    setRenderPassOverride(compositeValidation.passes()[1], AnimationProperty::CameraYaw, glm::vec4(0.01f));
    setRenderPassOverride(compositeValidation.passes()[2], AnimationProperty::UvOffset,
      glm::vec4(1.0f / 256.0f, 0.0f, 0, 0));
    setRenderPassOverride(compositeValidation.passes()[2], AnimationProperty::TextureSource,
      glm::vec4(static_cast<float>(TextureSource::ImportedOverride)));
    compositeValidation.passes()[2].importedTexture = importedTexture.asset;
    compositeValidation.passes()[2].importedTextureOverride = true;
    setPropertyKeyframe(compositeValidation.passes()[2], AnimationProperty::WireframeOverlay, 0.0f);
    compositeValidation.passes()[2].composite.operation = RelationOperator::Exclusion;
    const RenderStack materializedCompositeValidation = evaluateRenderStack(compositeValidation, 0.0f);
    for (std::size_t passIndex = 0; passIndex < materializedCompositeValidation.passes().size(); ++passIndex)
      renderer.renderPass(materializedCompositeValidation.passes()[passIndex], camera, scene, passIndex);
    if (renderer.composite(compositeValidation) == 0)
      fail("sequential render-pass compositing failed validation");
    for (int mask = static_cast<int>(CompositeMask::None); mask <= static_cast<int>(CompositeMask::PassEdges); ++mask) {
      compositeValidation.passes()[2].composite.mask = static_cast<CompositeMask>(mask);
      compositeValidation.passes()[2].composite.invertMask = mask != static_cast<int>(CompositeMask::None);
      if (renderer.composite(compositeValidation) == 0) fail("render-pass composite mask failed validation");
    }
    const std::string stackConfig = renderStackConfigJson(compositeValidation, camera, scene,
      HardwareProfile::Unrestricted, nullptr, importedFixture.asset.get());
    if (stackConfig.find("graphics-lab.render-stack.v2") == std::string::npos ||
        stackConfig.find("global base, global track, local override, local track") == std::string::npos ||
        stackConfig.find("\"passes\"") == std::string::npos ||
        stackConfig.find("\"global_base\"") == std::string::npos ||
        stackConfig.find("\"overrides\"") == std::string::npos ||
        stackConfig.find("\"perturbation\"") == std::string::npos ||
        stackConfig.find("\"composite_into_previous\"") == std::string::npos ||
        stackConfig.find("\"animation\"") == std::string::npos ||
        stackConfig.find("\"property_tracks\"") == std::string::npos ||
        stackConfig.find("\"value_kind\"") == std::string::npos ||
        stackConfig.find("\"animation_behavior\"") == std::string::npos ||
        stackConfig.find("\"texture_source\": \"imported_override\"") == std::string::npos ||
        stackConfig.find("\"imported_texture\"") == std::string::npos ||
        stackConfig.find("\"imported_model\"") == std::string::npos ||
        stackConfig.find("\"effective_renderer_at_time_zero\"") == std::string::npos)
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
