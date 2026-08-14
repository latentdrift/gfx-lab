#include "app/Validation.hpp"

#include "app/Animation.hpp"
#include "app/FileDialog.hpp"
#include "app/HardwareProfile.hpp"
#include "app/RenderOperationState.hpp"
#include "app/Spectral.hpp"
#include "assets/ModelAsset.hpp"
#include "document/Persistence.hpp"
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
    const std::filesystem::path typedPath = std::filesystem::temp_directory_path() /
      "graphics-lab-typed-validation.json";
    document::Document typedFixture = document::makeDefaultDocument();
    typedFixture.graphLayout.operations.push_back({typedFixture.operations.front().id,
      glm::vec2(40.0f, 60.0f)});
    const editor::CommandResult constructed = editor::applyCommand(typedFixture,
      editor::DuplicateAndCompare{{1}, {2}, {3}});
    const auto* comparison = typedFixture.operations.size() == 3
      ? std::get_if<document::CompositeOperation>(&typedFixture.operations.back().data) : nullptr;
    if (!constructed.applied || typedFixture.operations.size() != 3 || comparison == nullptr ||
        comparison->arithmetic.operation != RelationOperator::AbsoluteDifference ||
        typedFixture.operations.back().name != "Compare" ||
        typedFixture.graphLayout.operations.size() != 3 ||
        !typedFixture.graphLayout.outputPositionAuthored)
      fail("Duplicate + Compare did not construct and lay out a valid comparison graph");
    const document::OperationId compositeId = typedFixture.operations.back().id;
    const document::SignalRef compositeOutput = document::primaryOutput(typedFixture.operations.back());
    const editor::CommandResult cycle = editor::applyCommand(typedFixture,
      editor::ConnectSignal{compositeId, editor::InputSocket::A, compositeOutput});
    if (cycle.applied || cycle.error.empty() ||
        !evaluation::compileDocument(typedFixture).valid())
      fail("typed graph accepted a same-frame dependency cycle");
    typedFixture.operations[1].name = "Round-trip variant";
    const auto variantLayout = std::find_if(typedFixture.graphLayout.operations.begin(),
      typedFixture.graphLayout.operations.end(), [](const document::GraphNodePosition& position) {
        return position.operation == document::OperationId{2};
      });
    if (variantLayout == typedFixture.graphLayout.operations.end())
      fail("Duplicate + Compare did not author the variant graph position");
    variantLayout->position = glm::vec2(320.0f, 180.0f);
    typedFixture.graphLayout.outputPosition = glm::vec2(740.0f, 210.0f);
    typedFixture.graphLayout.outputPositionAuthored = true;
    auto* typedVariant = std::get_if<document::RenderOperation>(&typedFixture.operations[1].data);
    typedVariant->time.scale = -0.75f;
    typedVariant->time.offsetSeconds = 0.25f;
    typedFixture.automation.animation.push_back({
      {document::operationObject(typedFixture.operations[1].id), document::timeOffsetProperty()},
      KeyframeInterpolation::Linear, {{0.0f, glm::vec4(0.0f)}, {2.0f, glm::vec4(0.5f)}}});
    std::string typedSaveError;
    if (!document::saveDocumentFile(typedPath.string(), typedFixture, typedSaveError))
      fail("typed document save validation failed: " + typedSaveError);
    const document::DocumentLoadResult typedRoundTrip = document::loadDocumentFile(typedPath.string());
    std::filesystem::remove(typedPath);
    if (!typedRoundTrip || typedRoundTrip.document->operations.size() != 3 ||
        typedRoundTrip.document->operations[1].name != "Round-trip variant" ||
        std::abs(std::get<document::RenderOperation>(typedRoundTrip.document->operations[1].data).time.scale +
          0.75f) > 0.0001f || typedRoundTrip.document->automation.animation.size() != 1 ||
        typedRoundTrip.document->automation.animation.front().target.property != document::timeOffsetProperty() ||
        typedRoundTrip.document->graphLayout.operations.size() != 3 ||
        glm::distance(typedRoundTrip.document->graphLayout.outputPosition,
          typedFixture.graphLayout.outputPosition) > 0.0001f ||
        !evaluation::compileDocument(*typedRoundTrip.document).valid())
      fail("typed document round-trip validation failed");
    document::Document sdfGraph = document::makeDefaultDocument();
    document::Operation sdfField = document::makeSdfFieldOperation({2}, "Animated volume");
    auto& sdfDefinition = std::get<document::SdfFieldOperation>(sdfField.data);
    sdfDefinition.a.type = 3;
    sdfDefinition.a.parameters = {1.8f, 0.7f, 1.25f};
    sdfDefinition.combination = 2;
    const document::SignalRef worldDistance = document::primaryOutput(sdfField);
    sdfGraph.operations.push_back(std::move(sdfField));
    sdfGraph.nextOperationIdentity = 3;
    std::get<document::RenderOperation>(sdfGraph.operations.front().data).field = worldDistance;
    const document::SignalDescriptor* worldDistanceDescriptor =
      document::findSignal(sdfGraph, worldDistance.id);
    const evaluation::EvaluationPlan sdfPlan = evaluation::compileDocument(sdfGraph);
    const std::filesystem::path sdfGraphPath = "graphics-lab-sdf-graph-validation.json";
    std::string sdfSaveError;
    const bool sdfSaved = document::saveDocumentFile(sdfGraphPath.string(), sdfGraph, sdfSaveError);
    const document::DocumentLoadResult sdfRoundTrip = sdfSaved
      ? document::loadDocumentFile(sdfGraphPath.string()) : document::DocumentLoadResult{};
    std::error_code sdfRemoveError;
    std::filesystem::remove(sdfGraphPath, sdfRemoveError);
    if (!sdfPlan.valid() || worldDistanceDescriptor == nullptr ||
        worldDistanceDescriptor->metadata.domain != document::SignalDomain::World3D ||
        worldDistanceDescriptor->metadata.semantic != document::SignalSemantic::SignedDistance ||
        !sdfRoundTrip || sdfRoundTrip.document->operations.size() != 2 ||
        std::get<document::RenderOperation>(sdfRoundTrip.document->operations.front().data).field !=
          worldDistance ||
        std::get<document::SdfFieldOperation>(sdfRoundTrip.document->operations.back().data)
          .combination != 2 || !evaluation::compileDocument(*sdfRoundTrip.document).valid())
      fail("world-space SDF graph typing, connection, or persistence failed validation");
    document::Document workingSignals = document::makeDefaultDocument();
    workingSignals.renderDefaults.renderer.output.width = 384;
    workingSignals.renderDefaults.renderer.output.height = 216;
    const document::SignalRef color = document::primaryOutput(workingSignals.operations.front());
    const document::SignalRef depth{document::operationSignal(workingSignals.operations.front().id, "depth"), 0};
    document::Operation remap = document::makeRemapOperation({2}, "Depth Mask Remap", depth,
      document::SignalSemantic::MaskCoverage);
    document::Operation edge = document::makeEdgeOperation({3}, "Depth Edge", document::primaryOutput(remap));
    document::Operation blur = document::makeBlurOperation({4}, "Edge Blur",
      document::primaryOutput(edge), document::SignalShape::Scalar,
      document::SignalSemantic::EdgeStrength);
    document::Operation threshold = document::makeThresholdOperation({5}, "Edge Mask",
      document::primaryOutput(blur));
    document::Operation gradient = document::makeGradientMapOperation({6}, "Edge Color",
      document::primaryOutput(threshold));
    document::Operation luminance = document::makeLuminanceOperation({7}, "Luminance", color);
    const document::SignalRef edgeDirection{document::operationSignal(edge.id, "direction"), 0};
    const document::SignalDescriptor* edgeDirectionDescriptor = document::findSignal(
      workingSignals, edgeDirection.id);
    if (edgeDirectionDescriptor != nullptr)
      fail("edge direction descriptor resolved before its operation entered the document");
    document::Operation warp = document::makeWarpOperation({8}, "Edge Warp", color, edgeDirection);
    document::Operation composite = document::makeCompositeOperation({9}, "Masked Composite",
      document::primaryOutput(warp), document::primaryOutput(gradient));
    auto& compositeData = std::get<document::CompositeOperation>(composite.data);
    compositeData.mask = document::primaryOutput(threshold);
    workingSignals.operations.push_back(std::move(remap));
    workingSignals.operations.push_back(std::move(edge));
    workingSignals.operations.push_back(std::move(blur));
    workingSignals.operations.push_back(std::move(threshold));
    workingSignals.operations.push_back(std::move(gradient));
    workingSignals.operations.push_back(std::move(luminance));
    workingSignals.operations.push_back(std::move(warp));
    workingSignals.operations.push_back(std::move(composite));
    workingSignals.presentation.input = document::primaryOutput(workingSignals.operations.back());
    workingSignals.nextOperationIdentity = 10;
    edgeDirectionDescriptor = document::findSignal(workingSignals, edgeDirection.id);
    const document::SignalDescriptor* remapDescriptor = document::findSignal(workingSignals,
      document::primaryOutput(workingSignals.operations[1]).id);
    if (edgeDirectionDescriptor == nullptr || remapDescriptor == nullptr ||
        edgeDirectionDescriptor->shape != document::SignalShape::Vector2 ||
        edgeDirectionDescriptor->metadata.semantic != document::SignalSemantic::EdgeDirection ||
        edgeDirectionDescriptor->metadata.encoding != document::SignalEncoding::SignedUnitVectorPacked ||
        !edgeDirectionDescriptor->metadata.hasKnownRange ||
        edgeDirectionDescriptor->metadata.knownRange != glm::vec2(-1.0f, 1.0f) ||
        remapDescriptor->metadata.semantic != document::SignalSemantic::MaskCoverage ||
        !remapDescriptor->metadata.hasKnownRange ||
        remapDescriptor->metadata.knownRange != glm::vec2(0.0f, 1.0f))
      fail("working-signal descriptors did not preserve shape, meaning, encoding, or range");
    const evaluation::EvaluationPlan workingPlan = evaluation::compileDocument(workingSignals);
    evaluation::SignalRegistry workingResources;
    if (!workingPlan.valid() || renderer.evaluate(workingSignals, workingPlan,
        workingResources, 99, 0.0f) == 0 ||
        workingResources.displayTexture(document::primaryOutput(workingSignals.operations[4]).id) == 0 ||
        workingResources.displayTexture(document::primaryOutput(workingSignals.operations[5]).id) == 0 ||
        workingResources.displayTexture(edgeDirection.id) == 0 ||
        workingResources.displayTexture(document::primaryOutput(workingSignals.operations[7]).id) == 0)
      fail("working-signal operation graph failed GPU evaluation");
    const evaluation::SignalResource* finalWorkingResource = workingResources.find(
      workingSignals.presentation.input.id);
    GLint finalWorkingWidth = 0;
    GLint finalWorkingHeight = 0;
    if (finalWorkingResource != nullptr && finalWorkingResource->textureCount > 0) {
      glBindTexture(GL_TEXTURE_2D, finalWorkingResource->textures[0]);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &finalWorkingWidth);
      glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &finalWorkingHeight);
    }
    if (finalWorkingResource == nullptr || finalWorkingResource->extent != glm::ivec3(384, 216, 1) ||
        finalWorkingResource->descriptor.metadata.extent != glm::ivec3(384, 216, 1) ||
        finalWorkingWidth != 384 || finalWorkingHeight != 216)
      fail("working-signal graph did not preserve runtime image extent");
    const auto previewResource = [&](const document::SignalRef signal, const std::size_t slot) {
      const evaluation::SignalResource* resource = workingResources.find(signal.id);
      if (resource == nullptr || resource->textureCount == 0) return 0U;
      return renderer.previewSignal(*resource, workingSignals.renderDefaults.renderer, slot);
    };
    const document::SignalRef normal{document::operationSignal(workingSignals.operations.front().id,
      "normal"), 0};
    const document::SignalRef field{document::operationSignal(workingSignals.operations.front().id,
      "field"), 0};
    const document::SignalRef spectrum{document::operationSignal(workingSignals.operations.front().id,
      "spectrum16"), 0};
    if (previewResource(depth, 0) == 0 || previewResource(normal, 0) == 0 ||
        previewResource(field, 1) == 0 || previewResource(edgeDirection, 1) == 0 ||
        previewResource(spectrum, 2) == 0)
      fail("semantic signal preview adapter failed validation");
    const std::filesystem::path workingSignalsPath =
      "graphics-lab-working-signals-validation.json";
    std::string workingSignalsIoError;
    if (!document::saveDocumentFile(workingSignalsPath.string(), workingSignals,
        workingSignalsIoError))
      fail("working-signal document save failed: " + workingSignalsIoError);
    const document::DocumentLoadResult restoredWorkingSignals =
      document::loadDocumentFile(workingSignalsPath.string());
    std::error_code workingSignalsRemoveError;
    std::filesystem::remove(workingSignalsPath, workingSignalsRemoveError);
    if (!restoredWorkingSignals)
      fail("working-signal document reload failed: " + restoredWorkingSignals.error);
    const evaluation::EvaluationPlan restoredWorkingPlan =
      evaluation::compileDocument(*restoredWorkingSignals.document);
    if (!restoredWorkingPlan.valid() || restoredWorkingSignals.document->operations.size() != 9 ||
        restoredWorkingSignals.document->nextOperationIdentity != 10 ||
        restoredWorkingSignals.document->presentation.input != workingSignals.presentation.input ||
        std::get<document::RemapOperation>(restoredWorkingSignals.document->operations[1].data)
          .outputSemantic != document::SignalSemantic::MaskCoverage)
      fail("working-signal document did not preserve stable operation and port identities");
    document::Document longGraph = document::makeDefaultDocument();
    document::SignalRef longGraphSignal{document::operationSignal(
      longGraph.operations.front().id, "depth"), 0};
    constexpr std::size_t longGraphOperationCount = 20;
    for (std::size_t index = 1; index < longGraphOperationCount; ++index) {
      const document::OperationId id{index + 1};
      document::Operation remapOperation = document::makeRemapOperation(id,
        "Long graph remap " + std::to_string(index), longGraphSignal);
      longGraphSignal = document::primaryOutput(remapOperation);
      longGraph.operations.push_back(std::move(remapOperation));
    }
    const document::OperationId disconnectedId{longGraphOperationCount + 1};
    longGraph.operations.push_back(document::makeRenderOperation(disconnectedId,
      "Disconnected render"));
    longGraph.nextOperationIdentity = disconnectedId.value + 1;
    longGraph.presentation.input = longGraphSignal;
    const evaluation::EvaluationPlan longFullPlan = evaluation::compileDocument(longGraph);
    const evaluation::EvaluationPlan longReachablePlan = evaluation::restrictEvaluationPlan(
      longFullPlan, {longGraph.presentation.input});
    evaluation::SignalRegistry longGraphResources;
    if (!longFullPlan.valid() || longFullPlan.nodes.size() != longGraphOperationCount + 1 ||
        longReachablePlan.nodes.size() != longGraphOperationCount ||
        renderer.evaluate(longGraph, longReachablePlan, longGraphResources, 100, 0.0f) == 0 ||
        longGraphResources.find(document::primaryOutput(longGraph.operations[1]).id) != nullptr ||
        longGraphResources.find(document::primaryOutput(longGraph.operations.back()).id) != nullptr)
      fail("dynamic reachable evaluation did not exceed the former operation cap, release transients, or prune a branch");
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
    const document::DocumentLoadResult spectralDocument = document::loadDocumentFile(
      "examples/spectral-metamer-observer.json");
    if (!spectralDocument || spectralDocument.document->scene.testScene != TestScene::SpectralMetamers)
      fail("spectral metamer example failed document validation");
    const document::Document& typedSpectral = *spectralDocument.document;
    if (typedSpectral.operations.size() != 2 ||
        !std::holds_alternative<document::RenderOperation>(typedSpectral.operations[0].data) ||
        !std::holds_alternative<document::CompositeOperation>(typedSpectral.operations[1].data) ||
        document::findSignal(typedSpectral, typedSpectral.presentation.input.id) == nullptr ||
        typedSpectral.operations[0].outputs.size() != 5 ||
        typedSpectral.operations[0].outputs[4].shape != document::SignalShape::Spectrum16)
      fail("spectral example is not a native typed operation graph");
    const evaluation::EvaluationPlan typedSpectralPlan = evaluation::compileDocument(typedSpectral);
    if (!typedSpectralPlan.valid() || typedSpectralPlan.nodes.size() != typedSpectral.operations.size() ||
        typedSpectralPlan.finalSignal != typedSpectral.presentation.input)
      fail("typed document did not compile into a valid evaluation plan");
    document::Document commandedDocument = typedSpectral;
    editor::CommandHistory commandHistory;
    const document::OperationId animatedOperation = commandedDocument.operations.front().id;
    const document::PropertyAddress ambientTarget{
      document::operationObject(commandedDocument.operations.front().id),
      document::propertyId(AnimationProperty::Ambient)};
    if (!commandHistory.execute(commandedDocument,
          editor::SetKeyframe{ambientTarget, 1.0f, glm::vec4(0.42f)}).applied ||
        commandedDocument.automation.animation.empty() || !commandHistory.canUndo() ||
        !commandHistory.undo(commandedDocument) || !commandedDocument.automation.animation.empty() ||
        !commandHistory.redo(commandedDocument) || commandedDocument.automation.animation.empty())
      fail("typed document command history did not preserve animation edits");
    const editor::CommandResult graphReorder = commandHistory.execute(commandedDocument,
      editor::MoveOperation{commandedDocument.operations.back().id, 0});
    if (!graphReorder.applied || !evaluation::compileDocument(commandedDocument).valid())
      fail("typed graph could not compile independently of its display order");
    const document::OperationId duplicateId = document::nextOperationId(commandedDocument);
    if (!commandHistory.execute(commandedDocument, editor::DuplicateOperation{
          animatedOperation, duplicateId, 1}).applied ||
        commandedDocument.nextOperationIdentity <= duplicateId.value ||
        std::count_if(commandedDocument.automation.animation.begin(),
          commandedDocument.automation.animation.end(), [duplicateId](const document::AnimationTrack& track) {
            return track.target.owner == document::operationObject(duplicateId);
          }) != 1)
      fail("typed duplicate did not allocate persistent identity or clone automation");
    if (!commandHistory.execute(commandedDocument, editor::RemoveOperation{duplicateId}).applied ||
        document::nextOperationId(commandedDocument) == duplicateId ||
        std::any_of(commandedDocument.automation.animation.begin(), commandedDocument.automation.animation.end(),
          [duplicateId](const document::AnimationTrack& track) {
            return track.target.owner == document::operationObject(duplicateId);
          }))
      fail("typed removal reused identity or retained orphan automation");
    const auto compositeFixture = std::find_if(commandedDocument.operations.begin(),
      commandedDocument.operations.end(), [](const document::Operation& operation) {
        return std::holds_alternative<document::CompositeOperation>(operation.data);
      });
    auto* commandedComposite = compositeFixture == commandedDocument.operations.end() ? nullptr
      : std::get_if<document::CompositeOperation>(&compositeFixture->data);
    if (commandedComposite == nullptr) fail("typed disconnect validation lost composite fixture");
    commandedComposite->mask = {document::operationSignal(commandedDocument.operations.front().id,
      "depth"), 0};
    const document::SignalRef maskBeforeDisconnect = commandedComposite->mask;
    if (!commandHistory.execute(commandedDocument, editor::DisconnectSignal{
          compositeFixture->id, editor::InputSocket::Mask,
          maskBeforeDisconnect}).applied || commandedComposite->mask)
      fail("optional typed graph input could not be disconnected");
    const editor::CommandResult removeProducer = commandHistory.execute(commandedDocument,
      editor::RemoveOperation{animatedOperation});
    if (removeProducer.applied || removeProducer.error.empty() ||
        document::findOperation(commandedDocument, animatedOperation) == nullptr)
      fail("typed removal did not protect a referenced operation");
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
    RenderPass textureRenderValidation;
    textureRenderValidation.textureSource = TextureSource::ImportedOverride;
    textureRenderValidation.importedTexture = importedTexture.asset;
    renderer.renderPass(textureRenderValidation, CameraOrbit{}, TestScene::ImportedModel, 0);
    textureRenderValidation.textureSource = TextureSource::White;
    renderer.renderPass(textureRenderValidation, CameraOrbit{}, TestScene::ImportedModel, 0);
    if (glGetError() != GL_NO_ERROR) fail("scene-material or override texture rendering failed validation");
    renderer.clearImportedModel();
    RenderPass animationValidation;
    animationValidation.perturbation.modelTranslation.x = 0.0f;
    animationValidation.composite.gain = 1.0f;
    setPropertyKeyframe(animationValidation, AnimationProperty::ModelTranslation, 0.0f);
    setPropertyKeyframe(animationValidation, AnimationProperty::CompositeGain, 0.0f);
    animationValidation.perturbation.modelTranslation.x = 2.0f;
    animationValidation.composite.gain = 5.0f;
    setPropertyKeyframe(animationValidation, AnimationProperty::ModelTranslation, 2.0f);
    setPropertyKeyframe(animationValidation, AnimationProperty::CompositeGain, 2.0f);
    const RenderPass evaluatedAnimation = evaluateRenderPass(animationValidation, 1.0f);
    if (std::abs(evaluatedAnimation.perturbation.modelTranslation.x - 1.0f) > 0.0001f ||
        std::abs(evaluatedAnimation.composite.gain - 3.0f) > 0.0001f ||
        !removePropertyKeyframe(animationValidation, AnimationProperty::ModelTranslation, 2.0f) ||
        findPropertyTrack(animationValidation, AnimationProperty::CompositeGain) == nullptr ||
        animationValidation.animation.tracks.size() != 2)
      fail("render-pass keyframe interpolation failed validation");
    PropertyAnimationTrack* gainTrack = findPropertyTrack(animationValidation,
      AnimationProperty::CompositeGain);
    gainTrack->interpolation = KeyframeInterpolation::Step;
    if (std::abs(evaluateRenderPass(animationValidation, 1.0f).composite.gain - 1.0f) > 0.0001f ||
        !propertyHasKeyAt(animationValidation, AnimationProperty::CompositeGain, 2.0f))
      fail("independent property tracks or step interpolation failed validation");
    AnimationTimeline autoKeyValidation;
    autoKeyValidation.autoKey = true;
    autoKeyValidation.timeSeconds = 1.25f;
    animationValidation.perturbation.modelScale = 1.2f;
    recordPropertyAnimationEdit(animationValidation, AnimationProperty::ModelScale,
      autoKeyValidation, true);
    if (!propertyHasKeyAt(animationValidation, AnimationProperty::ModelScale, 1.25f))
      fail("viewport-style Auto Key edit failed validation");
    animationValidation.renderer.surface.wireframe = false;
    setPropertyKeyframe(animationValidation, AnimationProperty::WireframeOverlay, 0.0f);
    animationValidation.renderer.surface.wireframe = true;
    setPropertyKeyframe(animationValidation, AnimationProperty::WireframeOverlay, 2.0f);
    const PropertyAnimationTrack* wireframeTrack = findPropertyTrack(animationValidation,
      AnimationProperty::WireframeOverlay);
    if (wireframeTrack == nullptr || wireframeTrack->interpolation != KeyframeInterpolation::Step ||
        evaluateRenderPass(animationValidation, 1.0f).renderer.surface.wireframe ||
        !evaluateRenderPass(animationValidation, 2.0f).renderer.surface.wireframe ||
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
    constexpr std::array<const char*, 7> typedExamplePaths = {
      "examples/binocular-disparity-difference.json",
      "examples/elemental-combustion-chamber.json",
      "examples/field-apparition.json",
      "examples/interference-detonation.json",
      "examples/rod-cone-xor-sdf.json",
      "examples/single-world-cone-rod-xor.json",
      "examples/spectral-metamer-observer.json"};
    for (const char* path : typedExamplePaths) {
      const document::DocumentLoadResult loaded = document::loadDocumentFile(path);
      if (!loaded) fail(std::string("typed example failed to load: ") + path + ": " + loaded.error);
      const evaluation::EvaluationPlan plan = evaluation::compileDocument(*loaded.document);
      if (!plan.valid()) fail(std::string("typed example graph is invalid: ") + path);
      evaluation::SignalRegistry exampleSignals;
      if (renderer.evaluate(*loaded.document, plan, exampleSignals, 1, 0.0f) == 0)
        fail(std::string("typed example failed to render: ") + path);
    }
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
    renderer.updateElementalSimulation(1.0f / 30.0f, RendererState{}, TestScene::ElementalChamber);
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
    glFinish();
    if (glGetError() != GL_NO_ERROR)
      fail("binocular analysis produced an OpenGL error");
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
         operation <= static_cast<int>(RelationOperator::Normal); ++operation)
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
