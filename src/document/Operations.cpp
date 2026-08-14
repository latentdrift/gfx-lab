#include "document/Operations.hpp"

#include <tuple>
#include <type_traits>

namespace gfxlab::document {
namespace {

SignalDescriptor signal(const OperationId producer, std::string key,
    const SignalKind kind, std::string name) {
  SignalMetadata metadata;
  metadata.domain = kind == SignalKind::Scalar ? SignalDomain::Document : SignalDomain::Screen2D;
  metadata.encoding = kind == SignalKind::Color ? SignalEncoding::Linear
    : kind == SignalKind::Field ? SignalEncoding::Signed : SignalEncoding::Unspecified;
  return {operationSignal(producer, key), producer, std::move(key), kind,
    std::move(name), std::move(metadata)};
}

Operation operation(OperationId id, std::string name, OperationData data,
    std::initializer_list<std::tuple<const char*, SignalKind, const char*>> outputs) {
  Operation result;
  result.id = id;
  result.name = std::move(name);
  result.data = std::move(data);
  for (const auto& [key, kind, outputName] : outputs)
    result.outputs.push_back(signal(id, key, kind, outputName));
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
    return "Unknown";
  }, operation.data);
}

SignalRef primaryOutput(const Operation& operation) {
  return operation.outputs.empty() ? SignalRef{} : SignalRef{operation.outputs.front().id, 0};
}

Operation makeRenderOperation(const OperationId id, std::string name) {
  return operation(id, std::move(name), RenderOperation{}, {
    {"color", SignalKind::Color, "Color"}, {"depth", SignalKind::Depth, "Depth"},
    {"normal", SignalKind::Normal, "Normal"}, {"field", SignalKind::Field, "Field"},
    {"spectrum16", SignalKind::Spectrum16, "Spectrum16"}});
}

Operation makeInterpretOperation(const OperationId id, std::string name,
    const SignalRef spectrum) {
  return operation(id, std::move(name), InterpretOperation{spectrum},
    {{"color", SignalKind::Color, "Interpreted color"}});
}

Operation makeCompositeOperation(const OperationId id, std::string name,
    const SignalRef a, const SignalRef b) {
  CompositeOperation composite;
  composite.a = a;
  composite.b = b;
  return operation(id, std::move(name), std::move(composite),
    {{"color", SignalKind::Color, "Composite color"}});
}

Operation makeStereoOperation(const OperationId id, std::string name,
    const SignalRef left, const SignalRef right) {
  return operation(id, std::move(name), StereoOperation{left, right},
    {{"color", SignalKind::Color, "Stereo analysis"}});
}

Operation makeMeasureOperation(const OperationId id, std::string name,
    const SignalRef input) {
  return operation(id, std::move(name), MeasureOperation{input},
    {{"value", SignalKind::Scalar, "Measurement"}});
}

Operation makeConstantOperation(const OperationId id, std::string name,
    const glm::vec4 value, const SignalKind kind) {
  return operation(id, std::move(name), ConstantOperation{value, kind},
    {{"value", kind, "Constant"}});
}

} // namespace gfxlab::document
