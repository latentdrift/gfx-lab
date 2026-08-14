#pragma once

#include "document/Identifiers.hpp"

#include <glm/glm.hpp>

#include <string>

namespace gfxlab::document {

enum class SignalKind { Color, Depth, Normal, Field, Spectrum16, Scalar, Vector2 };
enum class SignalDomain { Document, Screen2D, Surface, World2D, World3D };
enum class SignalEncoding { Unspecified, Linear, EncodedRgb, Signed, UnsignedNormalized };

struct SignalMetadata {
  SignalDomain domain = SignalDomain::Screen2D;
  SignalEncoding encoding = SignalEncoding::Unspecified;
  glm::ivec3 extent{0};
  std::string units;
};

struct SignalDescriptor {
  SignalId id;
  OperationId producer;
  std::string key;
  SignalKind kind = SignalKind::Color;
  std::string name;
  SignalMetadata metadata;
};

struct SignalRef {
  SignalId id;
  int frameOffset = 0;

  [[nodiscard]] explicit operator bool() const { return static_cast<bool>(id); }
  auto operator<=>(const SignalRef&) const = default;
};

[[nodiscard]] const char* signalKindLabel(SignalKind kind);
[[nodiscard]] const char* signalDomainLabel(SignalDomain domain);

} // namespace gfxlab::document
