#include "document/Operations.hpp"

#include <type_traits>

namespace gfxlab::document {

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

} // namespace gfxlab::document
