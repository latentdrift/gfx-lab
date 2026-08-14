#include "document/LegacyAdapter.hpp"

#include <algorithm>
#include <cstdint>

namespace gfxlab::document {
namespace {

// This vocabulary exists only while translating the removed stack format.
enum class SignalKind { Color, Depth, Normal, Field, Spectrum16, Scalar, Vector2, ScalarImage };

SignalShape shapeOf(const SignalKind kind) {
  if (kind == SignalKind::Scalar || kind == SignalKind::Depth || kind == SignalKind::Field ||
      kind == SignalKind::ScalarImage) return SignalShape::Scalar;
  if (kind == SignalKind::Vector2) return SignalShape::Vector2;
  if (kind == SignalKind::Normal) return SignalShape::Vector3;
  if (kind == SignalKind::Spectrum16) return SignalShape::Spectrum16;
  return SignalShape::Vector4;
}

SignalSemantic semanticOf(const SignalKind kind) {
  switch (kind) {
    case SignalKind::Color: return SignalSemantic::Color;
    case SignalKind::Depth: return SignalSemantic::DeviceDepth;
    case SignalKind::Normal: return SignalSemantic::Normal;
    case SignalKind::Field: return SignalSemantic::FieldStrength;
    case SignalKind::Spectrum16: return SignalSemantic::Spectrum;
    case SignalKind::Scalar: return SignalSemantic::Measurement;
    case SignalKind::Vector2:
    case SignalKind::ScalarImage: return SignalSemantic::Generic;
  }
  return SignalSemantic::Generic;
}

SignalKind signalKind(const PassOutput output) {
  switch (output) {
    case PassOutput::Color: return SignalKind::Color;
    case PassOutput::Depth: return SignalKind::Depth;
    case PassOutput::Normals: return SignalKind::Normal;
    case PassOutput::VertexColor: return SignalKind::Color;
    case PassOutput::FieldSignal: return SignalKind::Field;
  }
  return SignalKind::Color;
}

const char* signalKey(const SignalKind kind) {
  switch (kind) {
    case SignalKind::Color: return "color";
    case SignalKind::Depth: return "depth";
    case SignalKind::Normal: return "normal";
    case SignalKind::Field: return "field";
    case SignalKind::Spectrum16: return "spectrum16";
    case SignalKind::Scalar: return "value";
    case SignalKind::Vector2: return "value";
    case SignalKind::ScalarImage: return "image";
  }
  return "value";
}

SignalDescriptor makeSignal(const OperationId producer, const SignalKind kind, std::string name) {
  SignalMetadata metadata;
  metadata.domain = kind == SignalKind::Scalar ? SignalDomain::Document : SignalDomain::Screen2D;
  metadata.semantic = semanticOf(kind);
  metadata.encoding = kind == SignalKind::Color ? SignalEncoding::Linear
    : kind == SignalKind::Field ? SignalEncoding::Signed : SignalEncoding::Unspecified;
  const std::string key = signalKey(kind);
  return {operationSignal(producer, key), producer, key, shapeOf(kind), std::move(name), std::move(metadata)};
}

SignalRef signalFromOperation(const Document& document, const OperationId producer, const SignalKind kind) {
  const Operation* operation = findOperation(document, producer);
  if (operation == nullptr) return {};
  const auto found = std::find_if(operation->outputs.begin(), operation->outputs.end(),
    [kind](const SignalDescriptor& descriptor) {
      return descriptor.shape == shapeOf(kind) && descriptor.metadata.semantic == semanticOf(kind);
    });
  return found == operation->outputs.end() ? primaryOutput(*operation) : SignalRef{found->id, 0};
}

TextureBinding textureBinding(const RenderPass& pass) {
  return {pass.textureSource, pass.importedTexture, pass.importedTextureSrgb};
}

void addRenderSignals(Operation& operation, const PassOutput primary) {
  const SignalKind primaryKind = signalKind(primary);
  operation.outputs.push_back(makeSignal(operation.id, primaryKind,
    std::string(signalSemanticLabel(semanticOf(primaryKind)))));
  constexpr SignalKind additional[] = {SignalKind::Color, SignalKind::Depth, SignalKind::Normal,
    SignalKind::Field, SignalKind::Spectrum16};
  for (const SignalKind kind : additional)
    if (kind != primaryKind) operation.outputs.push_back(makeSignal(operation.id, kind,
      signalSemanticLabel(semanticOf(kind))));
}

} // namespace

Document migrateLegacyDocument(const StackDocument& legacy) {
  Document result;
  result.scene.testScene = legacy.scene;
  result.scene.importedModel = legacy.importedModel;
  result.scene.authoredCamera = legacy.camera;
  result.scene.cameraAuthored = true;
  result.hardwareProfile = legacy.hardwareProfile;
  result.renderDefaults.renderer = legacy.renderStack.global().renderer;
  result.renderDefaults.texture = textureBinding(legacy.renderStack.global());
  result.presentation.reconstruction = legacy.renderStack.display();
  result.automation.timeline.currentTimeSeconds = legacy.timeline.timeSeconds;
  result.automation.timeline.durationSeconds = legacy.timeline.durationSeconds;
  result.automation.timeline.playbackRate = legacy.timeline.playbackRate;
  result.automation.timeline.loop = legacy.timeline.loop;
  result.automation.timeline.autoKey = legacy.timeline.autoKey;
  result.automation.timeline.showAllOperations = legacy.timeline.showAllPasses;
  result.automation.timeline.snapToFrames = legacy.timeline.snapToFrames;
  result.automation.timeline.framesPerSecond = legacy.timeline.framesPerSecond;

  for (const PropertyAnimationTrack& track : legacy.renderStack.global().animation.tracks)
    result.automation.animation.push_back({{renderDefaultsObject, propertyId(track.property)}, track.interpolation,
      track.keyframes});

  const auto operationIdForPass = [](const RenderPass& pass) {
    return OperationId{static_cast<std::uint64_t>(std::max(pass.id, 1))};
  };
  std::uint64_t nextSyntheticId = 1;
  for (const RenderPass& pass : legacy.renderStack.passes())
    nextSyntheticId = std::max(nextSyntheticId, operationIdForPass(pass).value + 1);

  SignalRef previousOutput;
  const auto sourceSignal = [&](const CompositeSource source, const int passId,
      const OperationId currentOperation) -> SignalRef {
    switch (source) {
      case CompositeSource::Accumulator: return previousOutput;
      case CompositeSource::CurrentPass:
        return signalFromOperation(result, currentOperation, SignalKind::Color);
      case CompositeSource::RenderPass:
        return signalFromOperation(result, OperationId{static_cast<std::uint64_t>(std::max(passId, 1))},
          SignalKind::Color);
      case CompositeSource::RenderPassField:
        return signalFromOperation(result, OperationId{static_cast<std::uint64_t>(std::max(passId, 1))},
          SignalKind::Field);
      case CompositeSource::RenderPassSpectrum:
        return signalFromOperation(result, OperationId{static_cast<std::uint64_t>(std::max(passId, 1))},
          SignalKind::Spectrum16);
      case CompositeSource::PreviousFrame: {
        SignalRef history = previousOutput;
        history.frameOffset = -1;
        return history;
      }
      case CompositeSource::FixedColor: return {};
    }
    return {};
  };

  for (const RenderPass& pass : legacy.renderStack.passes()) {
    const SignalRef accumulatorBefore = previousOutput;
    const OperationId id = operationIdForPass(pass);
    Operation operation;
    operation.id = id;
    operation.name = pass.name;
    operation.enabled = pass.enabled;

    const auto makeConstant = [&](const glm::vec4 value) -> SignalRef {
      Operation constant;
      constant.id = {nextSyntheticId++};
      constant.name = pass.name + " / constant";
      constant.enabled = pass.enabled;
      constant.data = ConstantOperation{value, SignalShape::Vector4, SignalSemantic::Color};
      constant.outputs.push_back(makeSignal(constant.id, SignalKind::Color, "Color"));
      const SignalRef output = primaryOutput(constant);
      result.operations.push_back(std::move(constant));
      return output;
    };
    const auto compositeData = [&](const OperationId consumer) {
      SignalRef a = sourceSignal(pass.composite.sourceA, pass.composite.sourceAPassId, consumer);
      SignalRef b = sourceSignal(pass.composite.sourceB, pass.composite.sourceBPassId, consumer);
      if (pass.composite.sourceA == CompositeSource::FixedColor) a = makeConstant(pass.composite.fixedColor);
      if (pass.composite.sourceB == CompositeSource::FixedColor) b = makeConstant(pass.composite.fixedColor);
      CompositeOperation composite;
      composite.a = a;
      composite.b = b;
      composite.interpretationA = pass.composite.interpretationA;
      composite.interpretationB = pass.composite.interpretationB;
      composite.observer = {pass.composite.observerExposureStops, pass.composite.rodSensitivity,
        pass.composite.opponentGain};
      composite.arithmetic = {pass.composite.operation, pass.composite.gain, pass.composite.bias,
        pass.composite.opacity, pass.composite.bitDepth, pass.composite.colorSpace, pass.composite.range};
      if (pass.composite.mask != CompositeMask::None) {
        Operation conversion;
        const OperationId conversionId{nextSyntheticId++};
        if (pass.composite.mask == CompositeMask::PassLuminance)
          conversion = makeLuminanceOperation(conversionId, pass.name + " / luminance mask", b);
        else if (pass.composite.mask == CompositeMask::PassEdges)
          conversion = makeEdgeOperation(conversionId, pass.name + " / edge mask", b);
        else {
          const char* port = pass.composite.mask == CompositeMask::PassDepth ? "depth" : "field";
          conversion = makeRemapOperation(conversionId, pass.name + " / mask",
            {operationSignal(b.id.producer, port), 0});
          if (pass.composite.mask == CompositeMask::PassField) {
            auto& remap = std::get<RemapOperation>(conversion.data);
            remap.inputLow = -1.0f; remap.inputHigh = 1.0f;
          }
        }
        composite.mask = primaryOutput(conversion);
        result.operations.push_back(std::move(conversion));
      }
      composite.invertMask = pass.composite.invertMask;
      if (pass.composite.sourceA == CompositeSource::PreviousFrame ||
          pass.composite.sourceB == CompositeSource::PreviousFrame)
        composite.feedback = FeedbackSettings{pass.composite.historyDecay,
          pass.composite.historyUvOffset, pass.composite.historyUvScale};
      return composite;
    };

    const bool splitLegacyComposite = pass.kind == StackOperationKind::LegacyRenderComposite;

    switch (pass.kind) {
      case StackOperationKind::Render:
      case StackOperationKind::LegacyRenderComposite: {
        operation.data = RenderOperation{pass.overrides, pass.perturbation, pass.output,
          textureBinding(pass), {}};
        addRenderSignals(operation, pass.output);
        break;
      }
      case StackOperationKind::Interpret: {
        SignalRef input = sourceSignal(pass.composite.sourceA, pass.composite.sourceAPassId, id);
        if (pass.composite.sourceA == CompositeSource::FixedColor) input = makeConstant(pass.composite.fixedColor);
        operation.data = InterpretOperation{input, pass.composite.interpretationA,
          pass.composite.observerExposureStops, pass.composite.gain, pass.composite.bias};
        operation.outputs.push_back(makeSignal(id, SignalKind::Color, "Interpreted color"));
        break;
      }
      case StackOperationKind::Composite: {
        operation.data = compositeData(id);
        operation.outputs.push_back(makeSignal(id, SignalKind::Color, "Composite color"));
        break;
      }
      case StackOperationKind::StereoAnalysis: {
        operation.data = StereoOperation{
          sourceSignal(pass.composite.sourceA, pass.composite.sourceAPassId, id),
          sourceSignal(pass.composite.sourceB, pass.composite.sourceBPassId, id),
          pass.stereoAnalysis, pass.stereoMaximumDisparityPixels, pass.stereoOcclusionTolerance};
        operation.outputs.push_back(makeSignal(id, SignalKind::Color, "Stereo analysis"));
        break;
      }
      case StackOperationKind::Measure: {
        SignalRef input = sourceSignal(pass.composite.sourceA, pass.composite.sourceAPassId, id);
        if (pass.composite.sourceA == CompositeSource::FixedColor) input = makeConstant(pass.composite.fixedColor);
        operation.data = MeasureOperation{input, pass.measurementMetric, pass.measurementThreshold,
          pass.measurementAbsolute};
        operation.outputs.push_back(makeSignal(id, SignalKind::Scalar, "Measurement"));
        break;
      }
    }

    for (const PropertyAnimationTrack& track : pass.animation.tracks)
      result.automation.animation.push_back({{operationObject(id), propertyId(track.property)}, track.interpolation,
        track.keyframes});

    result.operations.push_back(std::move(operation));
    previousOutput = primaryOutput(result.operations.back());

    if (splitLegacyComposite && accumulatorBefore) {
      previousOutput = accumulatorBefore;
      Operation composite;
      composite.id = {nextSyntheticId++};
      composite.name = pass.name + " / composite";
      composite.enabled = pass.enabled;
      composite.data = compositeData(id);
      composite.outputs.push_back(makeSignal(composite.id, SignalKind::Color, "Composite color"));
      result.operations.push_back(std::move(composite));
      previousOutput = primaryOutput(result.operations.back());
    }

    if (pass.kind == StackOperationKind::Measure && pass.measurementModulationEnabled) {
      const SignalRef source = primaryOutput(result.operations.back());
      result.automation.modulation.push_back({source,
        {operationObject(OperationId{static_cast<std::uint64_t>(std::max(pass.measurementTargetPassId, 1))}),
          propertyId(pass.measurementTargetProperty)},
        {pass.measurementInputMinimum, pass.measurementInputMaximum},
        {pass.measurementOutputMinimum, pass.measurementOutputMaximum}, pass.measurementClamp,
        pass.measurementSmoothingSeconds});
    }
  }

  result.presentation.input = previousOutput;
  result.nextOperationIdentity = nextSyntheticId;
  return result;
}

} // namespace gfxlab::document
