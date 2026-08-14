#pragma once

#include <compare>
#include <cstdint>
#include <functional>

namespace gfxlab::document {

template <typename Tag>
struct Identifier {
  std::uint64_t value = 0;

  [[nodiscard]] explicit operator bool() const { return value != 0; }
  auto operator<=>(const Identifier&) const = default;
};

struct ObjectTag;
struct OperationTag;
struct SignalTag;
struct PropertyTag;
struct ResourceTag;

using ObjectId = Identifier<ObjectTag>;
using OperationId = Identifier<OperationTag>;
using SignalId = Identifier<SignalTag>;
using PropertyId = Identifier<PropertyTag>;
using ResourceId = Identifier<ResourceTag>;

inline constexpr ObjectId sceneObject{1};
inline constexpr ObjectId renderDefaultsObject{2};
inline constexpr ObjectId presentationObject{3};
inline constexpr std::uint64_t operationObjectOffset = 1024;

[[nodiscard]] inline ObjectId operationObject(const OperationId id) {
  return {operationObjectOffset + id.value};
}

[[nodiscard]] inline SignalId operationSignal(const OperationId operation, const std::uint8_t ordinal) {
  return {(operation.value << 8U) | (static_cast<std::uint64_t>(ordinal) + 1U)};
}

} // namespace gfxlab::document

namespace std {

template <typename Tag>
struct hash<gfxlab::document::Identifier<Tag>> {
  std::size_t operator()(const gfxlab::document::Identifier<Tag> id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value);
  }
};

} // namespace std
