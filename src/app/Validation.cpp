#include "app/Validation.hpp"

#include "app/Animation.hpp"
#include "app/EditorHistory.hpp"
#include "app/FileDialog.hpp"
#include "app/HardwareProfile.hpp"
#include "app/PassEditing.hpp"
#include "app/RenderStack.hpp"
#include "app/StackDocument.hpp"
#include "app/Spectral.hpp"
#include "assets/ModelAsset.hpp"
#include "document/LegacyAdapter.hpp"
#include "editor/Commands.hpp"
#include "evaluation/Compiler.hpp"
#include "handbook/Handbook.hpp"
#include "renderer/Renderer.hpp"
#include "renderer/TextureReadback.hpp"
#include "simulation/ElementalSimulation.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
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
    const auto referenceA = spectral::humanResponse(spectral::reflectanceA, spectral::daylight);
    const auto referenceB = spectral::humanResponse(spectral::reflectanceB, spectral::daylight);
    float referenceDelta = 0.0f;
    for (int channel = 0; channel < 3; ++channel)
      referenceDelta = std::max(referenceDelta, std::abs(referenceA[channel] - referenceB[channel]));
    const auto tungstenA = spectral::humanResponse(spectral::reflectanceA, spectral::tungsten);
    const auto tungstenB = spectral::humanResponse(spectral::reflectanceB, spectral::tungsten);
    float tungstenDelta = 0.0f;
    for (int channel = 0; channel < 3; ++channel)
      tungstenDelta = std::max(tungstenDelta, std::abs(tungstenA[channel] - tungstenB[channel]));
    if (referenceDelta > 0.00002f || tungstenDelta < 0.04f)
      fail("spectral metamer fixture does not match under reference light and separate under tungsten");
    RendererState spectralState;
    CameraOrbit spectralCamera;
    applyRecommendedSetup(TestScene::SpectralMetamers, spectralState, spectralCamera);
    if (renderer.render(spectralState, spectralCamera, TestScene::SpectralMetamers, false) == 0)
      fail("spectral metamer scene failed render validation");
    const StackDocumentLoadResult spectralDocument = loadStackDocumentFile(
      "examples/spectral-metamer-observer.json");
    if (!spectralDocument || spectralDocument.document->scene != TestScene::SpectralMetamers ||
        spectralDocument.document->renderStack.passes().size() != 2 ||
        spectralDocument.document->renderStack.passes()[0].kind != StackOperationKind::Render ||
        spectralDocument.document->renderStack.passes()[1].kind != StackOperationKind::Composite ||
        spectralDocument.document->renderStack.passes()[1].composite.sourceA !=
          CompositeSource::RenderPassSpectrum)
      fail("spectral metamer example failed document validation");
    const document::Document typedSpectral = document::migrateLegacyDocument(*spectralDocument.document);
    if (typedSpectral.operations.size() != 2 ||
        !std::holds_alternative<document::RenderOperation>(typedSpectral.operations[0].data) ||
        !std::holds_alternative<document::CompositeOperation>(typedSpectral.operations[1].data) ||
        document::findSignal(typedSpectral, typedSpectral.presentation.input.id) == nullptr ||
        typedSpectral.operations[0].outputs.size() != 5 ||
        typedSpectral.operations[0].outputs[4].kind != document::SignalKind::Spectrum16)
      fail("legacy document did not migrate to typed operations and signals");
    const evaluation::EvaluationPlan typedSpectralPlan = evaluation::compileDocument(typedSpectral);
    if (!typedSpectralPlan.valid() || typedSpectralPlan.nodes.size() != typedSpectral.operations.size() ||
        typedSpectralPlan.finalSignal != typedSpectral.presentation.input)
      fail("typed document did not compile into a valid evaluation plan");
    document::Document commandedDocument = typedSpectral;
    editor::CommandHistory commandHistory;
    const document::PropertyAddress ambientTarget{
      document::operationObject(commandedDocument.operations.front().id),
      document::propertyId(AnimationProperty::Ambient)};
    if (!commandHistory.execute(commandedDocument,
          editor::SetKeyframe{ambientTarget, 1.0f, glm::vec4(0.42f)}).applied ||
        commandedDocument.automation.animation.empty() || !commandHistory.canUndo() ||
        !commandHistory.undo(commandedDocument) || !commandedDocument.automation.animation.empty() ||
        !commandHistory.redo(commandedDocument) || commandedDocument.automation.animation.empty())
      fail("typed document command history did not preserve animation edits");
    const document::Document validCommandedDocument = commandedDocument;
    const editor::CommandResult invalidMove = commandHistory.execute(commandedDocument,
      editor::MoveOperation{commandedDocument.operations.back().id, 0});
    if (invalidMove.applied || invalidMove.error.empty() ||
        commandedDocument.operations.front().id != validCommandedDocument.operations.front().id)
      fail("typed document command gate accepted an invalid dataflow reorder");
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
    if (validationStack.passes().size() != 3 || !validationStack.duplicateSelected() ||
        validationStack.passes().size() != 4 || !validationStack.moveSelected(-1) ||
        !validationStack.removeSelected() || validationStack.passes().size() != 3)
      fail("render-pass stack operations failed validation");
    if (!validationStack.addOperation(StackOperationKind::Interpret) ||
        validationStack.selected().kind != StackOperationKind::Interpret ||
        validationStack.selected().composite.sourceA != CompositeSource::RenderPassSpectrum)
      fail("typed signal operation creation failed validation");
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
    timelineValidation.snapToFrames = true;
    timelineValidation.framesPerSecond = 30;
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
        historyRestored.renderStack.passes().size() != 3 || historyRestored.scene != TestScene::Torus ||
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
        historyRestored.renderStack.passes().size() != 4 ||
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
    for (int mask = static_cast<int>(CompositeMask::None); mask <= static_cast<int>(CompositeMask::PassField); ++mask) {
      compositeValidation.passes()[2].composite.mask = static_cast<CompositeMask>(mask);
      compositeValidation.passes()[2].composite.invertMask = mask != static_cast<int>(CompositeMask::None);
      if (renderer.composite(compositeValidation) == 0) fail("render-pass composite mask failed validation");
    }
    for (int source = static_cast<int>(CompositeSource::Accumulator);
         source <= static_cast<int>(CompositeSource::RenderPassSpectrum); ++source) {
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
    compositeValidation.passes()[2].composite.sourceA = CompositeSource::PreviousFrame;
    compositeValidation.passes()[2].composite.sourceB = CompositeSource::PreviousFrame;
    compositeValidation.passes()[2].composite.operation = RelationOperator::Add;
    compositeValidation.passes()[2].composite.range = CompositeRange::Preserve;
    compositeValidation.passes()[2].composite.historyDecay = 1.0f;
    compositeValidation.passes()[2].composite.gain = 16.0f;
    for (int feedbackFrame = 0; feedbackFrame < 32; ++feedbackFrame)
      if (renderer.composite(compositeValidation) == 0)
        fail("extreme temporal composite feedback failed validation");
    glFinish();
    if (glGetError() != GL_NO_ERROR)
      fail("extreme temporal composite feedback produced an OpenGL error");
    compositeValidation.display().enabled = true;
    for (int signal = static_cast<int>(DisplaySignal::DirectRgb);
         signal <= static_cast<int>(DisplaySignal::RodConeXor); ++signal) {
      compositeValidation.display().signal = static_cast<DisplaySignal>(signal);
      const unsigned int composite = renderer.composite(compositeValidation);
      if (renderer.reconstructDisplay(composite, compositeValidation.display(),
          static_cast<std::size_t>(signal)) == 0)
        fail("display reconstruction failed validation");
    }
    if (glGetError() != GL_NO_ERROR) fail("display reconstruction produced an OpenGL error");
    compositeValidation.passes()[2].composite.sourceA = CompositeSource::RenderPassField;
    compositeValidation.passes()[2].composite.sourceAPassId = compositeValidation.passes()[0].id;
    compositeValidation.passes()[2].output = PassOutput::FieldSignal;
    compositeValidation.select(2);
    if (!compositeValidation.addOperation(StackOperationKind::Measure) ||
        compositeValidation.selected().composite.sourceAPassId != compositeValidation.passes()[2].id)
      fail("signal measurement consumer setup failed validation");
    const int measurementOperationId = compositeValidation.selected().id;
    compositeValidation.selected().measurementThreshold = 0.01f;
    compositeValidation.selected().measurementMetric = MeasurementMetric::RmsMagnitude;
    compositeValidation.selected().measurementModulationEnabled = true;
    compositeValidation.selected().measurementTargetPassId = compositeValidation.passes()[0].id;
    compositeValidation.selected().measurementTargetProperty = AnimationProperty::Ambient;
    compositeValidation.selected().measurementInputMinimum = 0.1f;
    compositeValidation.selected().measurementInputMaximum = 0.7f;
    compositeValidation.selected().measurementOutputMinimum = 0.2f;
    compositeValidation.selected().measurementOutputMaximum = 0.9f;
    compositeValidation.selected().measurementSmoothingSeconds = 0.25f;
    const unsigned int measuredStackOutput = renderer.composite(compositeValidation);
    const unsigned int measuredSignal = renderer.stackOperationResult(compositeValidation.selectedIndex());
    const SignalMeasurement measurement = measureTextureSignal(measuredSignal, 0.01f, true);
    if (measuredStackOutput == 0 || measuredSignal == 0 || measurement.sampleCount != 4096 ||
        measurement.peakMagnitude < measurement.meanMagnitude || measurement.coverage < 0.0f ||
        measurement.coverage > 1.0f)
      fail("signal measurement consumer failed validation");
    const std::string stackConfig = renderStackConfigJson(compositeValidation, camera, scene,
      HardwareProfile::Unrestricted, &timelineValidation, importedFixture.asset.get());
    if (stackConfig.find("graphics-lab.render-stack.v8") == std::string::npos ||
        stackConfig.find("typed_operations_v1") == std::string::npos ||
        stackConfig.find("\"operation_kind\"") == std::string::npos ||
        stackConfig.find("\"operation_kind\": \"measure\"") == std::string::npos ||
        stackConfig.find("\"measurement\"") == std::string::npos ||
        stackConfig.find("\"metric\": \"rms_magnitude\"") == std::string::npos ||
        stackConfig.find("\"target_property\": \"ambient\"") == std::string::npos ||
        stackConfig.find("\"field_resources\"") == std::string::npos ||
        stackConfig.find("render_pass_field") == std::string::npos ||
        stackConfig.find("pass_field") == std::string::npos ||
        stackConfig.find("global base, global track, local override, local track") == std::string::npos ||
        stackConfig.find("\"passes\"") == std::string::npos ||
        stackConfig.find("\"global_base\"") == std::string::npos ||
        stackConfig.find("\"overrides\"") == std::string::npos ||
        stackConfig.find("\"perturbation\"") == std::string::npos ||
        stackConfig.find("\"coordinate_source\"") == std::string::npos ||
        stackConfig.find("\"uv_rotation_radians\"") == std::string::npos ||
        stackConfig.find("\"camera_lateral_offset_units\"") == std::string::npos ||
        stackConfig.find("\"stereo_convergence_distance_units\"") == std::string::npos ||
        stackConfig.find("\"composite_into_previous\"") == std::string::npos ||
        stackConfig.find("\"source_a\"") == std::string::npos ||
        stackConfig.find("\"pass_id\"") == std::string::npos ||
        stackConfig.find("\"interpretation_a\"") == std::string::npos ||
        stackConfig.find("\"interpretation_b\"") == std::string::npos ||
        stackConfig.find("\"fixed_color_rgba\"") == std::string::npos ||
        stackConfig.find("\"previous_frame\"") == std::string::npos ||
        stackConfig.find("\"display_reconstruction\"") == std::string::npos ||
        stackConfig.find("rod_cone_quantized_xor") == std::string::npos ||
        stackConfig.find("\"spectral_rendering\": false") == std::string::npos ||
        stackConfig.find("\"animation\"") == std::string::npos ||
        stackConfig.find("\"property_tracks\"") == std::string::npos ||
        stackConfig.find("\"value_kind\"") == std::string::npos ||
        stackConfig.find("\"animation_behavior\"") == std::string::npos ||
        stackConfig.find("\"snap_to_frames\"") == std::string::npos ||
        stackConfig.find("\"frames_per_second\"") == std::string::npos ||
        stackConfig.find("\"texture_source\": \"imported_override\"") == std::string::npos ||
        stackConfig.find("\"imported_texture\"") == std::string::npos ||
        stackConfig.find("\"imported_model\"") == std::string::npos ||
        stackConfig.find("\"effective_renderer_at_time_zero\"") == std::string::npos)
      fail("render-pass stack missing from config export");
    const std::filesystem::path measurementRoundTripPath =
      "graphics-lab-measurement-consumer-validation.json";
    std::string measurementIoError;
    const std::string measurementRoundTripJson = renderStackConfigJson(compositeValidation, camera, scene,
      HardwareProfile::Unrestricted, &timelineValidation);
    if (!saveStackDocumentFile(measurementRoundTripPath.string(), measurementRoundTripJson,
        measurementIoError))
      fail("measurement consumer save validation failed: " + measurementIoError);
    const StackDocumentLoadResult measurementRoundTrip =
      loadStackDocumentFile(measurementRoundTripPath.string());
    std::error_code measurementRemoveError;
    std::filesystem::remove(measurementRoundTripPath, measurementRemoveError);
    if (!measurementRoundTrip)
      fail("measurement consumer save/load round trip could not load: " + measurementRoundTrip.error);
    const auto restoredMeasurement = std::find_if(
      measurementRoundTrip.document->renderStack.passes().begin(),
      measurementRoundTrip.document->renderStack.passes().end(),
      [measurementOperationId](const RenderPass& pass) { return pass.id == measurementOperationId; });
    if (restoredMeasurement == measurementRoundTrip.document->renderStack.passes().end() ||
        restoredMeasurement->kind != StackOperationKind::Measure ||
        std::abs(restoredMeasurement->measurementThreshold - 0.01f) > 0.0001f ||
        restoredMeasurement->composite.sourceAPassId != compositeValidation.passes()[2].id ||
        restoredMeasurement->measurementMetric != MeasurementMetric::RmsMagnitude ||
        !restoredMeasurement->measurementModulationEnabled ||
        restoredMeasurement->measurementTargetPassId != compositeValidation.passes()[0].id ||
        restoredMeasurement->measurementTargetProperty != AnimationProperty::Ambient ||
        std::abs(restoredMeasurement->measurementInputMinimum - 0.1f) > 0.0001f ||
        std::abs(restoredMeasurement->measurementOutputMaximum - 0.9f) > 0.0001f ||
        std::abs(restoredMeasurement->measurementSmoothingSeconds - 0.25f) > 0.0001f)
      fail("measurement consumer save/load round trip failed validation");
    const StackDocumentLoadResult exampleDocument = loadStackDocumentFile("examples/rod-cone-xor-sdf.json");
    if (!exampleDocument || exampleDocument.document->scene != TestScene::SdfIsoSurface ||
        exampleDocument.document->renderStack.passes().size() != 2 ||
        exampleDocument.document->renderStack.display().signal != DisplaySignal::RodConeXor ||
        !exampleDocument.document->renderStack.display().enabled ||
        exampleDocument.document->renderStack.passes()[1].composite.operation != RelationOperator::BitwiseXor ||
        findRenderPassOverride(exampleDocument.document->renderStack.passes()[0],
          AnimationProperty::SdfAType) == nullptr)
      fail("rod/cone XOR example stack failed document loading validation");
    const std::filesystem::path roundTripPath = std::filesystem::temp_directory_path() /
      "graphics-lab-stack-document-validation.json";
    std::string documentIoError;
    const std::string roundTripJson = renderStackConfigJson(exampleDocument.document->renderStack,
      exampleDocument.document->camera, exampleDocument.document->scene,
      exampleDocument.document->hardwareProfile, &exampleDocument.document->timeline);
    if (!saveStackDocumentFile(roundTripPath.string(), roundTripJson, documentIoError))
      fail("stack document save validation failed: " + documentIoError);
    const StackDocumentLoadResult roundTripDocument = loadStackDocumentFile(roundTripPath.string());
    std::error_code removeError;
    std::filesystem::remove(roundTripPath, removeError);
    if (!roundTripDocument || roundTripDocument.document->renderStack.passes().size() != 2 ||
        roundTripDocument.document->renderStack.display().signal != DisplaySignal::RodConeXor ||
        roundTripDocument.document->renderStack.passes()[1].composite.operation != RelationOperator::BitwiseXor ||
        std::abs(roundTripDocument.document->camera.yaw - exampleDocument.document->camera.yaw) > 0.0001f)
      fail("stack document save/load round trip failed validation");
    StackDocumentLoadResult binocularDocument =
      loadStackDocumentFile("examples/binocular-disparity-difference.json");
    if (!binocularDocument || binocularDocument.document->scene != TestScene::Lighting ||
        binocularDocument.document->renderStack.passes().size() != 3 ||
        std::abs(materializeRenderPass(binocularDocument.document->renderStack, 0).perturbation.cameraLateral +
          0.0325f) > 0.0001f ||
        std::abs(materializeRenderPass(binocularDocument.document->renderStack, 1).perturbation.cameraLateral -
          0.0325f) > 0.0001f ||
        std::abs(materializeRenderPass(binocularDocument.document->renderStack, 1).perturbation.stereoConvergence -
          4.0f) > 0.0001f ||
        binocularDocument.document->renderStack.passes()[2].kind != StackOperationKind::StereoAnalysis ||
        binocularDocument.document->renderStack.passes()[2].stereoAnalysis !=
          StereoAnalysisMode::AbsoluteDisparity)
      fail("binocular disparity example failed document loading validation");
    const StackDocumentLoadResult detonationDocument =
      loadStackDocumentFile("examples/interference-detonation.json");
    if (!detonationDocument || detonationDocument.document->scene != TestScene::FieldInterference ||
        detonationDocument.document->renderStack.passes().size() != 5 ||
        detonationDocument.document->renderStack.passes()[0].animation.tracks.size() != 4 ||
        detonationDocument.document->renderStack.passes()[3].composite.sourceB !=
          CompositeSource::PreviousFrame ||
        detonationDocument.document->renderStack.passes()[4].kind != StackOperationKind::Measure ||
        !detonationDocument.document->renderStack.passes()[4].measurementModulationEnabled ||
        detonationDocument.document->renderStack.passes()[4].measurementTargetProperty !=
          AnimationProperty::FieldEmissionInfluence)
      fail("interference detonation example failed document loading validation");
    const StackDocumentLoadResult apparitionDocument =
      loadStackDocumentFile("examples/field-apparition.json");
    if (!apparitionDocument || apparitionDocument.document->scene != TestScene::SdfIsoSurface ||
        apparitionDocument.document->renderStack.passes().size() != 5 ||
        apparitionDocument.document->renderStack.passes()[0].animation.tracks.size() != 5 ||
        apparitionDocument.document->renderStack.passes()[4].kind != StackOperationKind::Measure ||
        apparitionDocument.document->renderStack.passes()[4].composite.sourceA !=
          CompositeSource::RenderPassField ||
        !apparitionDocument.document->renderStack.passes()[4].measurementModulationEnabled ||
        apparitionDocument.document->renderStack.passes()[4].measurementTargetProperty !=
          AnimationProperty::IsoLevel)
      fail("field apparition example failed document loading validation");
    const StackDocumentLoadResult elementalDocument =
      loadStackDocumentFile("examples/elemental-combustion-chamber.json");
    if (!elementalDocument || elementalDocument.document->scene != TestScene::ElementalChamber ||
        elementalDocument.document->renderStack.passes().size() != 6 ||
        elementalDocument.document->renderStack.global().animation.tracks.size() != 3 ||
        elementalDocument.document->renderStack.passes()[0].output != PassOutput::FieldSignal ||
        elementalDocument.document->renderStack.passes()[3].kind != StackOperationKind::Measure ||
        elementalDocument.document->renderStack.passes()[3].composite.sourceA !=
          CompositeSource::RenderPassField ||
        elementalDocument.document->renderStack.passes()[3].measurementTargetProperty !=
          AnimationProperty::CompositeGain ||
        elementalDocument.document->renderStack.passes()[5].kind != StackOperationKind::Composite)
      fail("elemental combustion chamber example failed document loading validation");
    ElementalSimulation elementalSimulation;
    RendererState::Field elementalControls;
    elementalControls.producerKind = 2;
    elementalControls.sourceA = {-2.6f, 0.0f, -1.7f};
    elementalControls.wavelength = 0.8f;
    elementalControls.amplitudeA = 2.5f;
    elementalControls.amplitudeB = 2.0f;
    elementalControls.falloff = 1.0f;
    for (int step = 0; step < 180; ++step) elementalSimulation.update(1.0f / 60.0f, elementalControls);
    if (elementalSimulation.revision() < 100 || elementalSimulation.totalCombustion() <= 0.0f ||
        elementalSimulation.matterPixels().size() !=
          static_cast<std::size_t>(ElementalSimulation::width * ElementalSimulation::height))
      fail("persistent elemental simulation failed validation");
    renderer.resetElementalSimulation();
    renderer.updateElementalSimulation(1.0f / 30.0f,
      elementalDocument.document->renderStack.global().renderer, TestScene::ElementalChamber);
    if (renderer.renderPass(elementalDocument.document->renderStack.passes()[0],
        elementalDocument.document->camera, TestScene::ElementalChamber, 0) == 0)
      fail("elemental chamber field rendering failed validation");
    CameraOrbit stereoCamera;
    stereoCamera.yaw = 0.0f;
    stereoCamera.pitch = 0.0f;
    stereoCamera.distance = 5.0f;
    RendererState stereoState;
    PassPerturbation leftEye;
    leftEye.cameraLateral = -0.1f;
    leftEye.stereoConvergence = 4.0f;
    PassPerturbation rightEye = leftEye;
    rightEye.cameraLateral = 0.1f;
    const PassCameraMatrices leftCamera = buildPassCamera(stereoCamera, stereoState, leftEye, 4.0f / 3.0f);
    const PassCameraMatrices rightCamera = buildPassCamera(stereoCamera, stereoState, rightEye, 4.0f / 3.0f);
    const glm::vec3 convergencePoint = stereoCamera.eye() +
      glm::normalize(stereoCamera.target - stereoCamera.eye()) * leftEye.stereoConvergence;
    const glm::vec4 leftClip = leftCamera.projection * leftCamera.view * glm::vec4(convergencePoint, 1.0f);
    const glm::vec4 rightClip = rightCamera.projection * rightCamera.view * glm::vec4(convergencePoint, 1.0f);
    if (std::abs(leftClip.x / leftClip.w) > 0.0001f || std::abs(rightClip.x / rightClip.w) > 0.0001f ||
        std::abs(glm::distance(leftCamera.eye, rightCamera.eye) - 0.2f) > 0.0001f)
      fail("off-axis stereo camera convergence failed validation");
    for (std::size_t passIndex = 0;
         passIndex < binocularDocument.document->renderStack.passes().size(); ++passIndex) {
      const RenderPass materialized = materializeRenderPass(binocularDocument.document->renderStack, passIndex);
      if (materialized.kind == StackOperationKind::Render)
        renderer.renderPass(materialized, binocularDocument.document->camera,
          binocularDocument.document->scene, passIndex);
    }
    if (renderer.composite(binocularDocument.document->renderStack) == 0)
      fail("binocular disparity example failed render validation");
    for (int mode = static_cast<int>(StereoAnalysisMode::Anaglyph);
         mode <= static_cast<int>(StereoAnalysisMode::MonocularOcclusion); ++mode) {
      binocularDocument.document->renderStack.passes()[2].stereoAnalysis =
        static_cast<StereoAnalysisMode>(mode);
      if (renderer.composite(binocularDocument.document->renderStack) == 0)
        fail("binocular analysis mode failed render validation");
    }
    glFinish();
    if (glGetError() != GL_NO_ERROR)
      fail("binocular analysis produced an OpenGL error");
    StackDocumentLoadResult observerOperandDocument =
      loadStackDocumentFile("examples/single-world-cone-rod-xor.json");
    if (!observerOperandDocument || observerOperandDocument.document->renderStack.passes().size() != 2 ||
        observerOperandDocument.document->renderStack.passes()[1].composite.sourceA !=
          CompositeSource::RenderPass ||
        observerOperandDocument.document->renderStack.passes()[1].composite.sourceAPassId != 1 ||
        observerOperandDocument.document->renderStack.passes()[1].composite.sourceBPassId != 1 ||
        observerOperandDocument.document->renderStack.passes()[1].composite.interpretationA !=
          CompositeInterpretation::ConeLuminance ||
        observerOperandDocument.document->renderStack.passes()[1].composite.interpretationB !=
          CompositeInterpretation::RodResponse ||
        std::abs(observerOperandDocument.document->renderStack.passes()[1].composite.rodSensitivity -
          observerOperandDocument.document->renderStack.display().rodSensitivity) > 0.0001f ||
        observerOperandDocument.document->renderStack.passes()[1].composite.operation !=
          RelationOperator::BitwiseXor)
      fail("observer-operand example failed document loading validation");
    for (std::size_t passIndex = 0;
         passIndex < observerOperandDocument.document->renderStack.passes().size(); ++passIndex)
      renderer.renderPass(materializeRenderPass(observerOperandDocument.document->renderStack, passIndex),
        observerOperandDocument.document->camera, observerOperandDocument.document->scene, passIndex);
    for (int interpretation = static_cast<int>(CompositeInterpretation::RawRgb);
         interpretation <= static_cast<int>(CompositeInterpretation::SpectralRod); ++interpretation) {
      observerOperandDocument.document->renderStack.passes()[1].composite.interpretationA =
        static_cast<CompositeInterpretation>(interpretation);
      observerOperandDocument.document->renderStack.passes()[1].composite.interpretationB =
        static_cast<CompositeInterpretation>(interpretation);
      if (renderer.composite(observerOperandDocument.document->renderStack) == 0)
        fail("composite observer interpretation failed render validation");
    }
    observerOperandDocument.document->renderStack.passes()[1].composite.interpretationA =
      CompositeInterpretation::ConeLuminance;
    observerOperandDocument.document->renderStack.passes()[1].composite.interpretationB =
      CompositeInterpretation::RodResponse;
    observerOperandDocument.document->renderStack.passes()[1].composite.observerExposureStops = 1.25f;
    observerOperandDocument.document->renderStack.passes()[1].composite.rodSensitivity = 7.5f;
    observerOperandDocument.document->renderStack.passes()[1].composite.opponentGain = 2.75f;
    const std::string observerRoundTripJson = renderStackConfigJson(
      observerOperandDocument.document->renderStack, observerOperandDocument.document->camera,
      observerOperandDocument.document->scene, observerOperandDocument.document->hardwareProfile,
      &observerOperandDocument.document->timeline);
    if (!saveStackDocumentFile(roundTripPath.string(), observerRoundTripJson, documentIoError))
      fail("observer-operand document save validation failed: " + documentIoError);
    const StackDocumentLoadResult observerRoundTrip = loadStackDocumentFile(roundTripPath.string());
    std::filesystem::remove(roundTripPath, removeError);
    if (!observerRoundTrip ||
        observerRoundTrip.document->renderStack.passes()[1].composite.interpretationA !=
          CompositeInterpretation::ConeLuminance ||
        observerRoundTrip.document->renderStack.passes()[1].composite.interpretationB !=
          CompositeInterpretation::RodResponse ||
        std::abs(observerRoundTrip.document->renderStack.passes()[1].composite.observerExposureStops - 1.25f) >
          0.0001f ||
        std::abs(observerRoundTrip.document->renderStack.passes()[1].composite.rodSensitivity - 7.5f) > 0.0001f ||
        std::abs(observerRoundTrip.document->renderStack.passes()[1].composite.opponentGain - 2.75f) > 0.0001f)
      fail("operation observer settings failed JSON round-trip validation");
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
    RendererState fieldValidation;
    CameraOrbit fieldCamera;
    applyRecommendedSetup(TestScene::FieldInterference, fieldValidation, fieldCamera);
    if (!fieldValidation.field.enabled ||
        renderer.render(fieldValidation, fieldCamera, TestScene::FieldInterference, false) == 0)
      fail("world-space field interference scene failed validation");
    RenderPass animatedField;
    animatedField.renderer = fieldValidation;
    setPropertyKeyframe(animatedField, AnimationProperty::FieldPhaseOffset, 0.0f);
    animatedField.renderer.field.phaseOffset = 3.14159265f;
    setPropertyKeyframe(animatedField, AnimationProperty::FieldPhaseOffset, 2.0f);
    setPropertyKeyframe(animatedField, AnimationProperty::FieldVertexDisplacement, 0.0f);
    animatedField.renderer.field.vertexDisplacement = 0.6f;
    setPropertyKeyframe(animatedField, AnimationProperty::FieldVertexDisplacement, 2.0f);
    const RenderPass evaluatedField = evaluateRenderPass(animatedField, 1.0f);
    fieldValidation.field.discardBelowEnabled = true;
    fieldValidation.field.discardThreshold = 0.45f;
    fieldValidation.field.surfaceColorInfluence = 0.4f;
    fieldValidation.field.emissionInfluence = 0.8f;
    fieldValidation.lighting.shadows = true;
    renderer.render(fieldValidation, fieldCamera, TestScene::FieldInterference, false);
    if (std::abs(evaluateRenderPass(animatedField, 1.0f).renderer.field.phaseOffset - 1.5707963f) > 0.0001f ||
        std::abs(evaluatedField.renderer.field.vertexDisplacement - 0.41f) > 0.0001f ||
        configJson(fieldValidation, fieldCamera, TestScene::FieldInterference,
          HardwareProfile::Unrestricted).find("\"consumers\"") == std::string::npos)
      fail("field animation or renderer-state export failed validation");
    RendererState sdfValidation;
    CameraOrbit sdfCamera;
    applyRecommendedSetup(TestScene::SdfIsoSurface, sdfValidation, sdfCamera);
    if (!sdfValidation.field.enabled || sdfValidation.field.producerKind != 1 ||
        !sdfValidation.field.isoSurfaceEnabled)
      fail("SDF iso-surface recommended setup failed validation");
    while (glGetError() != GL_NO_ERROR) {}
    for (int producerType = 0; producerType <= 2; ++producerType) {
      sdfValidation.field.sdfA.type = producerType;
      sdfValidation.field.sdfB.type = 2 - producerType;
      for (int operation = 0; operation <= 3; ++operation) {
        sdfValidation.field.sdfOperation = operation;
        if (renderer.render(sdfValidation, sdfCamera, TestScene::SdfIsoSurface, false) == 0)
          fail("analytic SDF producer or iso-surface render failed validation");
      }
    }
    if (glGetError() != GL_NO_ERROR)
      fail("analytic SDF producer or depth-writing iso-surface produced an OpenGL error");
    RenderPass animatedSdf;
    animatedSdf.renderer = sdfValidation;
    animatedSdf.renderer.field.sdfA.position = {-1.0f, 0.0f, 0.0f};
    setPropertyKeyframe(animatedSdf, AnimationProperty::SdfAPosition, 0.0f);
    animatedSdf.renderer.field.sdfA.position = {1.0f, 0.5f, 0.0f};
    setPropertyKeyframe(animatedSdf, AnimationProperty::SdfAPosition, 2.0f);
    animatedSdf.renderer.field.isoLevel = -0.2f;
    setPropertyKeyframe(animatedSdf, AnimationProperty::IsoLevel, 0.0f);
    animatedSdf.renderer.field.isoLevel = 0.2f;
    setPropertyKeyframe(animatedSdf, AnimationProperty::IsoLevel, 2.0f);
    const RenderPass evaluatedSdf = evaluateRenderPass(animatedSdf, 1.0f);
    const std::string sdfConfig = configJson(sdfValidation, sdfCamera, TestScene::SdfIsoSurface,
      HardwareProfile::Unrestricted);
    if (glm::distance(evaluatedSdf.renderer.field.sdfA.position, glm::vec3(0.0f, 0.25f, 0.0f)) > 0.0001f ||
        std::abs(evaluatedSdf.renderer.field.isoLevel) > 0.0001f ||
        sdfConfig.find("\"producer_kind\": \"signed_distance_field\"") == std::string::npos ||
        sdfConfig.find("\"iso_surface\"") == std::string::npos)
      fail("SDF animation or renderer-state export failed validation");
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
