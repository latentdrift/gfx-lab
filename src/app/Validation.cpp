#include "app/Validation.hpp"

#include "app/Animation.hpp"
#include "app/EditorHistory.hpp"
#include "app/FileDialog.hpp"
#include "app/HardwareProfile.hpp"
#include "app/PassEditing.hpp"
#include "app/RenderStack.hpp"
#include "assets/ModelAsset.hpp"
#include "handbook/Handbook.hpp"
#include "renderer/Renderer.hpp"

#include <GL/glew.h>

#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>

namespace gfxlab {
namespace {

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "graphics-lab: %s\n", message.c_str());
  std::exit(EXIT_FAILURE);
}

} // namespace

void runStartupValidationIfRequested(Renderer& renderer, RendererState& current, RendererState& reference,
    CameraOrbit& camera, TestScene& scene, Category& category) {
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
    if (renderer.texturePreview(importedTexture.asset.get()) == 0)
      fail("standalone texture preview upload failed validation");
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
    RenderStack identityValidation;
    identityValidation.select(0);
    if (!identityValidation.duplicateSelected()) fail("render-pass identity setup failed validation");
    const int referencedId = identityValidation.passes().back().id;
    identityValidation.selected().composite.sourceA = CompositeSource::RenderPass;
    identityValidation.selected().composite.sourceAPassId = referencedId;
    if (!identityValidation.moveSelected(1) ||
        identityValidation.selected().composite.sourceAPassId != referencedId)
      fail("render-pass operand identity changed during reorder");
    validationStack.global().renderer.lighting.ambient = 0.31f;
    validationStack.global().perturbation.modelTranslation = {0.2f, 0.0f, 0.0f};
    setRenderPassOverride(validationStack.selected(), AnimationProperty::Ambient, glm::vec4(0.77f));
    setRenderPassOverride(validationStack.selected(), AnimationProperty::UvOffset,
      glm::vec4(0.125f, 0.0f, 0.0f, 0.0f));
    setRenderPassOverride(validationStack.selected(), AnimationProperty::UvMapping,
      glm::vec4(static_cast<float>(UvMapping::PlanarXz)));
    setRenderPassOverride(validationStack.selected(), AnimationProperty::UvRotation,
      glm::vec4(0.25f));
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
        hierarchicalPass.perturbation.uvMapping != UvMapping::PlanarXz ||
        std::abs(hierarchicalPass.perturbation.uvRotation - 0.25f) > 0.0001f ||
        std::abs(hierarchicalPass.renderer.post.fogStart - 4.0f) > 0.0001f ||
        std::abs(staticallyResolvedPass.renderer.post.fogStart - 6.0f) > 0.0001f ||
        validationStack.selected().overrides.size() != 4)
      fail("global base, sparse override, or hierarchical animation precedence failed validation");
    validationStack.duplicateSelected();
    if (validationStack.selected().overrides.size() != 4 ||
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
    AnimationTimeline autoKeyValidation;
    autoKeyValidation.autoKey = true;
    autoKeyValidation.timeSeconds = 1.25f;
    animationValidation.selected().perturbation.modelScale = 1.2f;
    recordPropertyAnimationEdit(animationValidation.selected(), AnimationProperty::ModelScale,
      autoKeyValidation, true);
    if (!propertyHasKeyAt(animationValidation.selected(), AnimationProperty::ModelScale, 1.25f))
      fail("viewport-style Auto Key edit failed validation");
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
    for (int source = static_cast<int>(CompositeSource::Accumulator);
         source <= static_cast<int>(CompositeSource::PreviousFrame); ++source) {
      compositeValidation.passes()[2].composite.sourceA = static_cast<CompositeSource>(source);
      compositeValidation.passes()[2].composite.sourceB = static_cast<CompositeSource>(source);
      compositeValidation.passes()[2].composite.sourceAPassId = compositeValidation.passes()[0].id;
      compositeValidation.passes()[2].composite.sourceBPassId = compositeValidation.passes()[1].id;
      compositeValidation.passes()[2].composite.fixedColor = glm::vec4(0.8f, 0.2f, 0.6f, 1.0f);
      if (renderer.composite(compositeValidation) == 0) fail("render-pass composite source failed validation");
    }
    renderer.resetFrameHistory();
    compositeValidation.passes()[2].composite.operation = RelationOperator::BitwiseXor;
    compositeValidation.passes()[2].composite.bitDepth = 5;
    if (renderer.composite(compositeValidation) == 0 || renderer.composite(compositeValidation) == 0)
      fail("quantized temporal compositing failed validation");
    compositeValidation.display().enabled = true;
    for (int signal = static_cast<int>(DisplaySignal::DirectRgb);
         signal <= static_cast<int>(DisplaySignal::CompositeNtsc); ++signal) {
      compositeValidation.display().signal = static_cast<DisplaySignal>(signal);
      const unsigned int composite = renderer.composite(compositeValidation);
      if (renderer.reconstructDisplay(composite, compositeValidation.display(),
          static_cast<std::size_t>(signal)) == 0)
        fail("display reconstruction failed validation");
    }
    if (glGetError() != GL_NO_ERROR) fail("display reconstruction produced an OpenGL error");
    const std::string stackConfig = renderStackConfigJson(compositeValidation, camera, scene,
      HardwareProfile::Unrestricted, nullptr, importedFixture.asset.get());
    if (stackConfig.find("graphics-lab.render-stack.v7") == std::string::npos ||
        stackConfig.find("global base, global track, local override, local track") == std::string::npos ||
        stackConfig.find("\"passes\"") == std::string::npos ||
        stackConfig.find("\"global_base\"") == std::string::npos ||
        stackConfig.find("\"overrides\"") == std::string::npos ||
        stackConfig.find("\"perturbation\"") == std::string::npos ||
        stackConfig.find("\"coordinate_source\"") == std::string::npos ||
        stackConfig.find("\"uv_rotation_radians\"") == std::string::npos ||
        stackConfig.find("\"composite_into_previous\"") == std::string::npos ||
        stackConfig.find("\"source_a\"") == std::string::npos ||
        stackConfig.find("\"pass_id\"") == std::string::npos ||
        stackConfig.find("\"fixed_color_rgba\"") == std::string::npos ||
        stackConfig.find("\"previous_frame\"") == std::string::npos ||
        stackConfig.find("\"display_reconstruction\"") == std::string::npos ||
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
         operation <= static_cast<int>(RelationOperator::BitwiseXor); ++operation)
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
}

} // namespace gfxlab
