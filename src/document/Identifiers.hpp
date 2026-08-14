#pragma once

#include <compare>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>

namespace gfxlab::document {

template <typename Tag>
struct Identifier {
  std::uint64_t value = 0;

  [[nodiscard]] explicit operator bool() const { return value != 0; }
  auto operator<=>(const Identifier&) const = default;
};

struct OperationTag;
struct PropertyTag;
struct ResourceTag;

using OperationId = Identifier<OperationTag>;
using PropertyId = Identifier<PropertyTag>;
using ResourceId = Identifier<ResourceTag>;

enum class ObjectKind : std::uint8_t { None, Scene, RenderDefaults, Presentation, Operation };

struct ObjectId {
  ObjectKind kind = ObjectKind::None;
  std::uint64_t value = 0;

  [[nodiscard]] explicit operator bool() const { return kind != ObjectKind::None; }
  auto operator<=>(const ObjectId&) const = default;
};

struct SignalId {
  OperationId producer;
  std::string port;

  [[nodiscard]] explicit operator bool() const { return static_cast<bool>(producer) && !port.empty(); }
  auto operator<=>(const SignalId&) const = default;
};

inline constexpr ObjectId sceneObject{ObjectKind::Scene, 0};
inline constexpr ObjectId renderDefaultsObject{ObjectKind::RenderDefaults, 0};
inline constexpr ObjectId presentationObject{ObjectKind::Presentation, 0};

[[nodiscard]] inline ObjectId operationObject(const OperationId id) {
  return {ObjectKind::Operation, id.value};
}

[[nodiscard]] inline SignalId operationSignal(const OperationId operation, std::string_view port) {
  return {operation, std::string(port)};
}

} // namespace gfxlab::document

namespace std {

template <typename Tag>
struct hash<gfxlab::document::Identifier<Tag>> {
  std::size_t operator()(const gfxlab::document::Identifier<Tag> id) const noexcept {
    return std::hash<std::uint64_t>{}(id.value);
  }
};

template <>
struct hash<gfxlab::document::ObjectId> {
  std::size_t operator()(const gfxlab::document::ObjectId id) const noexcept {
    return std::hash<std::uint64_t>{}((id.value << 8U) |
      static_cast<std::uint64_t>(id.kind));
  }
};

template <>
struct hash<gfxlab::document::SignalId> {
  std::size_t operator()(const gfxlab::document::SignalId& id) const noexcept {
    const std::size_t operation = std::hash<std::uint64_t>{}(id.producer.value);
    const std::size_t port = std::hash<std::string>{}(id.port);
    return operation ^ (port + 0x9e3779b97f4a7c15ULL + (operation << 6U) + (operation >> 2U));
  }
};

} // namespace std
