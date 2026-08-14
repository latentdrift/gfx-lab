#include "document/Operations.hpp"

#include <tuple>
#include <type_traits>

namespace gfxlab::document {
namespace {

SignalMetadata metadataFor(const SignalSemantic semantic) {
  SignalMetadata metadata;
  metadata.domain = semantic == SignalSemantic::Measurement ? SignalDomain::Document : SignalDomain::Screen2D;
  metadata.semantic = semantic;
  metadata.encoding = semantic == SignalSemantic::Color || semantic == SignalSemantic::Luminance
    ? SignalEncoding::Linear : semantic == SignalSemantic::SignedDistance ||
      semantic == SignalSemantic::FieldStrength ? SignalEncoding::Signed : SignalEncoding::Unspecified;
  if (semantic == SignalSemantic::Normal) {
    metadata.space = SignalSpace::World;
    metadata.encoding = SignalEncoding::Signed;
    metadata.units = "unit direction";
    metadata.hasKnownRange = true;
    metadata.knownRange = {-1.0f, 1.0f};
  }
  if (semantic == SignalSemantic::EdgeDirection) {
    metadata.space = SignalSpace::Screen;
    metadata.encoding = SignalEncoding::SignedUnitVectorPacked;
    metadata.units = "unit direction";
    metadata.hasKnownRange = true;
    metadata.knownRange = {-1.0f, 1.0f};
  }
  if (semantic == SignalSemantic::DeviceDepth || semantic == SignalSemantic::MaskCoverage ||
      semantic == SignalSemantic::EdgeStrength)
    metadata.encoding = SignalEncoding::UnsignedNormalized;
  if (semantic == SignalSemantic::DeviceDepth) metadata.units = "device depth";
  if (semantic == SignalSemantic::SignedDistance) metadata.units = "world units";
  if (semantic == SignalSemantic::SignedDistance) {
    metadata.domain = SignalDomain::World3D;
    metadata.space = SignalSpace::World;
  }
  if (semantic == SignalSemantic::Luminance || semantic == SignalSemantic::EdgeStrength ||
      semantic == SignalSemantic::MaskCoverage) metadata.units = "unitless";
  if (semantic == SignalSemantic::DeviceDepth || semantic == SignalSemantic::MaskCoverage ||
      semantic == SignalSemantic::Luminance || semantic == SignalSemantic::EdgeStrength) {
    metadata.hasKnownRange = true;
    metadata.knownRange = {0.0f, 1.0f};
  }
  return metadata;
}

SignalDescriptor signal(const OperationId producer, std::string key,
    const SignalShape shape, const SignalSemantic semantic, std::string name) {
  return {operationSignal(producer, key), producer, std::move(key), shape,
    std::move(name), metadataFor(semantic)};
}

Operation operation(OperationId id, std::string name, OperationData data,
    std::initializer_list<std::tuple<const char*, SignalShape, SignalSemantic, const char*>> outputs) {
  Operation result;
  result.id = id;
  result.name = std::move(name);
  result.data = std::move(data);
  for (const auto& [key, shape, semantic, outputName] : outputs)
    result.outputs.push_back(signal(id, key, shape, semantic, outputName));
  synchronizeOperationSignalMetadata(result);
  return result;
}

} // namespace

const char* operationTypeLabel(const Operation& operation) {
  return std::visit([](const auto& data) -> const char* {
    using Type = std::decay_t<decltype(data)>;
    if constexpr (std::is_same_v<Type, RenderOperation>) return "Render";
    if constexpr (std::is_same_v<Type, SdfPrimitiveOperation>) return "SDF Primitive";
    if constexpr (std::is_same_v<Type, SdfCombineOperation>) return "SDF Combine";
    if constexpr (std::is_same_v<Type, WaveFieldOperation>) return "Wave Field";
    if constexpr (std::is_same_v<Type, ElementalFieldOperation>) return "Elemental Field";
    if constexpr (std::is_same_v<Type, InterpretOperation>) return "Interpret";
    if constexpr (std::is_same_v<Type, CompositeOperation>) return "Composite";
    if constexpr (std::is_same_v<Type, ConstantOperation>) return "Constant";
    if constexpr (std::is_same_v<Type, StereoOperation>) return "Stereo";
    if constexpr (std::is_same_v<Type, MeasureOperation>) return "Measure";
    if constexpr (std::is_same_v<Type, LuminanceOperation>) return "Luminance";
    if constexpr (std::is_same_v<Type, RemapOperation>) return "Remap";
    if constexpr (std::is_same_v<Type, EdgeOperation>) return "Edge";
    if constexpr (std::is_same_v<Type, BlurOperation>) return "Blur";
    if constexpr (std::is_same_v<Type, ThresholdOperation>) return "Threshold";
    if constexpr (std::is_same_v<Type, GradientMapOperation>) return "Gradient Map";
    if constexpr (std::is_same_v<Type, WarpOperation>) return "Warp";
    return "Unknown";
  }, operation.data);
}

SignalRef primaryOutput(const Operation& operation) {
  return operation.outputs.empty() ? SignalRef{} : SignalRef{operation.outputs.front().id, 0};
}

void synchronizeOperationSignalMetadata(Operation& operation) {
  for (SignalDescriptor& output : operation.outputs) {
    const glm::ivec3 extent = output.metadata.extent;
    output.metadata = metadataFor(output.metadata.semantic);
    output.metadata.extent = extent;
  }
  if (std::holds_alternative<RenderOperation>(operation.data)) {
    for (SignalDescriptor& output : operation.outputs) {
      if (output.key != "field") continue;
      // Render publishes a camera-sampled image of its world field. The
      // connected field definition itself remains a separate World3D signal.
      output.metadata.domain = SignalDomain::Screen2D;
      output.metadata.space = SignalSpace::Screen;
    }
  }
  if (std::holds_alternative<WaveFieldOperation>(operation.data) ||
      std::holds_alternative<ElementalFieldOperation>(operation.data)) {
    for (SignalDescriptor& output : operation.outputs) {
      output.metadata.domain = SignalDomain::World3D;
      output.metadata.space = SignalSpace::World;
    }
  }
  if (const auto* constant = std::get_if<ConstantOperation>(&operation.data)) {
    if (!operation.outputs.empty()) {
      operation.outputs.front().shape = constant->shape;
      operation.outputs.front().metadata = metadataFor(constant->semantic);
    }
  } else if (const auto* remap = std::get_if<RemapOperation>(&operation.data)) {
    if (!operation.outputs.empty()) {
      operation.outputs.front().name = remap->outputSemantic == SignalSemantic::MaskCoverage
        ? "Mask coverage" : "Remapped value";
      SignalMetadata& metadata = operation.outputs.front().metadata;
      metadata = metadataFor(remap->outputSemantic);
      metadata.encoding = SignalEncoding::Linear;
      metadata.units = "unitless";
      metadata.hasKnownRange = remap->clamp;
      metadata.knownRange = {std::min(remap->outputLow, remap->outputHigh),
        std::max(remap->outputLow, remap->outputHigh)};
    }
  } else if (const auto* blur = std::get_if<BlurOperation>(&operation.data)) {
    if (!operation.outputs.empty()) {
      operation.outputs.front().shape = blur->outputShape;
      operation.outputs.front().metadata = metadataFor(blur->outputSemantic);
    }
  }
}

Operation makeRenderOperation(const OperationId id, std::string name) {
  return operation(id, std::move(name), RenderOperation{}, {
    {"color", SignalShape::Vector4, SignalSemantic::Color, "Color"},
    {"depth", SignalShape::Scalar, SignalSemantic::DeviceDepth, "Device depth"},
    {"normal", SignalShape::Vector3, SignalSemantic::Normal, "World normal"},
    {"field", SignalShape::Scalar, SignalSemantic::FieldStrength, "Field"},
    {"spectrum16", SignalShape::Spectrum16, SignalSemantic::Spectrum, "Spectrum16"}});
}

Operation makeSdfPrimitiveOperation(const OperationId id, std::string name) {
  return operation(id, std::move(name), SdfPrimitiveOperation{},
    {{"distance", SignalShape::Scalar, SignalSemantic::SignedDistance, "Signed distance"}});
}

Operation makeSdfCombineOperation(const OperationId id, std::string name,
    const SignalRef a, const SignalRef b) {
  return operation(id, std::move(name), SdfCombineOperation{a, b},
    {{"distance", SignalShape::Scalar, SignalSemantic::SignedDistance, "Signed distance"}});
}

Operation makeWaveFieldOperation(const OperationId id, std::string name) {
  Operation result = operation(id, std::move(name), WaveFieldOperation{},
    {{"field", SignalShape::Scalar, SignalSemantic::FieldStrength, "Wave field"}});
  result.outputs.front().metadata.domain = SignalDomain::World3D;
  result.outputs.front().metadata.space = SignalSpace::World;
  return result;
}

Operation makeElementalFieldOperation(const OperationId id, std::string name) {
  Operation result = operation(id, std::move(name), ElementalFieldOperation{},
    {{"field", SignalShape::Scalar, SignalSemantic::FieldStrength, "Elemental channel"}});
  result.outputs.front().metadata.domain = SignalDomain::World3D;
  result.outputs.front().metadata.space = SignalSpace::World;
  return result;
}

Operation makeInterpretOperation(const OperationId id, std::string name,
    const SignalRef spectrum) {
  return operation(id, std::move(name), InterpretOperation{spectrum},
    {{"color", SignalShape::Vector4, SignalSemantic::Color, "Interpreted color"}});
}

Operation makeCompositeOperation(const OperationId id, std::string name,
    const SignalRef a, const SignalRef b) {
  CompositeOperation composite;
  composite.a = a;
  composite.b = b;
  return operation(id, std::move(name), std::move(composite),
    {{"color", SignalShape::Vector4, SignalSemantic::Color, "Composite color"}});
}

Operation makeStereoOperation(const OperationId id, std::string name,
    const SignalRef left, const SignalRef right) {
  return operation(id, std::move(name), StereoOperation{left, right},
    {{"color", SignalShape::Vector4, SignalSemantic::Color, "Stereo analysis"}});
}

Operation makeMeasureOperation(const OperationId id, std::string name,
    const SignalRef input) {
  return operation(id, std::move(name), MeasureOperation{input},
    {{"value", SignalShape::Scalar, SignalSemantic::Measurement, "Measurement"}});
}

Operation makeConstantOperation(const OperationId id, std::string name,
    const glm::vec4 value, const SignalShape shape, const SignalSemantic semantic) {
  return operation(id, std::move(name), ConstantOperation{value, shape, semantic},
    {{"value", shape, semantic, "Constant"}});
}

Operation makeLuminanceOperation(const OperationId id, std::string name, const SignalRef input) {
  return operation(id, std::move(name), LuminanceOperation{input},
    {{"value", SignalShape::Scalar, SignalSemantic::Luminance, "Luminance"}});
}

Operation makeRemapOperation(const OperationId id, std::string name, const SignalRef input,
    const SignalSemantic outputSemantic) {
  RemapOperation remap;
  remap.input = input;
  remap.outputSemantic = outputSemantic;
  return operation(id, std::move(name), remap,
    {{"value", SignalShape::Scalar, outputSemantic, outputSemantic == SignalSemantic::MaskCoverage
      ? "Mask coverage" : "Remapped value"}});
}

Operation makeEdgeOperation(const OperationId id, std::string name, const SignalRef input) {
  return operation(id, std::move(name), EdgeOperation{input},
    {{"strength", SignalShape::Scalar, SignalSemantic::EdgeStrength, "Edge strength"},
      {"direction", SignalShape::Vector2, SignalSemantic::EdgeDirection, "Edge direction"}});
}

Operation makeBlurOperation(const OperationId id, std::string name, const SignalRef input,
    const SignalShape outputShape, const SignalSemantic outputSemantic) {
  return operation(id, std::move(name), BlurOperation{input, outputShape, outputSemantic},
    {{"image", outputShape, outputSemantic, "Blurred image"}});
}

Operation makeThresholdOperation(const OperationId id, std::string name, const SignalRef input) {
  return operation(id, std::move(name), ThresholdOperation{input},
    {{"value", SignalShape::Scalar, SignalSemantic::MaskCoverage, "Threshold mask"}});
}

Operation makeGradientMapOperation(const OperationId id, std::string name, const SignalRef input) {
  return operation(id, std::move(name), GradientMapOperation{input},
    {{"color", SignalShape::Vector4, SignalSemantic::Color, "Mapped color"}});
}

Operation makeWarpOperation(const OperationId id, std::string name, const SignalRef image,
    const SignalRef displacement) {
  return operation(id, std::move(name), WarpOperation{image, displacement},
    {{"color", SignalShape::Vector4, SignalSemantic::Color, "Warped color"}});
}

} // namespace gfxlab::document
