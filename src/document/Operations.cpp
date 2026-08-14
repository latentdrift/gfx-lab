#include "document/Operations.hpp"

#include <tuple>
#include <type_traits>

namespace gfxlab::document {
namespace {

SignalDescriptor signal(const OperationId producer, std::string key,
    const SignalShape shape, const SignalSemantic semantic, std::string name) {
  SignalMetadata metadata;
  metadata.domain = semantic == SignalSemantic::Measurement ? SignalDomain::Document : SignalDomain::Screen2D;
  metadata.semantic = semantic;
  metadata.encoding = semantic == SignalSemantic::Color || semantic == SignalSemantic::Luminance
    ? SignalEncoding::Linear : semantic == SignalSemantic::SignedDistance ||
      semantic == SignalSemantic::FieldStrength ? SignalEncoding::Signed : SignalEncoding::Unspecified;
  if (semantic == SignalSemantic::Normal) {
    metadata.space = SignalSpace::World;
    metadata.encoding = SignalEncoding::UnsignedNormalized;
  }
  if (semantic == SignalSemantic::DeviceDepth || semantic == SignalSemantic::MaskCoverage ||
      semantic == SignalSemantic::EdgeStrength)
    metadata.encoding = SignalEncoding::UnsignedNormalized;
  if (semantic == SignalSemantic::DeviceDepth) metadata.units = "device depth";
  if (semantic == SignalSemantic::SignedDistance) metadata.units = "world units";
  if (semantic == SignalSemantic::DeviceDepth || semantic == SignalSemantic::MaskCoverage ||
      semantic == SignalSemantic::Luminance || semantic == SignalSemantic::EdgeStrength) {
    metadata.hasKnownRange = true;
    metadata.knownRange = {0.0f, 1.0f};
  }
  return {operationSignal(producer, key), producer, std::move(key), shape,
    std::move(name), std::move(metadata)};
}

Operation operation(OperationId id, std::string name, OperationData data,
    std::initializer_list<std::tuple<const char*, SignalShape, SignalSemantic, const char*>> outputs) {
  Operation result;
  result.id = id;
  result.name = std::move(name);
  result.data = std::move(data);
  for (const auto& [key, shape, semantic, outputName] : outputs)
    result.outputs.push_back(signal(id, key, shape, semantic, outputName));
  return result;
}

} // namespace

const char* operationTypeLabel(const Operation& operation) {
  return std::visit([](const auto& data) -> const char* {
    using Type = std::decay_t<decltype(data)>;
    if constexpr (std::is_same_v<Type, RenderOperation>) return "Render";
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
    return "Unknown";
  }, operation.data);
}

SignalRef primaryOutput(const Operation& operation) {
  return operation.outputs.empty() ? SignalRef{} : SignalRef{operation.outputs.front().id, 0};
}

Operation makeRenderOperation(const OperationId id, std::string name) {
  return operation(id, std::move(name), RenderOperation{}, {
    {"color", SignalShape::Vector4, SignalSemantic::Color, "Color"},
    {"depth", SignalShape::Scalar, SignalSemantic::DeviceDepth, "Device depth"},
    {"normal", SignalShape::Vector3, SignalSemantic::Normal, "World normal"},
    {"field", SignalShape::Scalar, SignalSemantic::FieldStrength, "Field"},
    {"spectrum16", SignalShape::Spectrum16, SignalSemantic::Spectrum, "Spectrum16"}});
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

Operation makeRemapOperation(const OperationId id, std::string name, const SignalRef input) {
  return operation(id, std::move(name), RemapOperation{input},
    {{"value", SignalShape::Scalar, SignalSemantic::Generic, "Remapped value"}});
}

Operation makeEdgeOperation(const OperationId id, std::string name, const SignalRef input) {
  return operation(id, std::move(name), EdgeOperation{input},
    {{"strength", SignalShape::Scalar, SignalSemantic::EdgeStrength, "Edge strength"}});
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

} // namespace gfxlab::document
