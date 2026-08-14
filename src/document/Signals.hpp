#pragma once

#include "document/Identifiers.hpp"

#include <glm/glm.hpp>

#include <string>

namespace gfxlab::document {

enum class SignalDomain { Document, Screen2D, Surface, World2D, World3D };
enum class SignalShape { Scalar, Vector2, Vector3, Vector4, Spectrum16 };
enum class SignalSemantic {
  Generic,
  Color,
  Luminance,
  DeviceDepth,
  LinearDepth,
  Normal,
  FieldStrength,
  SignedDistance,
  MaskCoverage,
  EdgeStrength,
  EdgeDirection,
  Measurement,
  Spectrum
};
enum class SignalSpace { Unspecified, Object, World, View, Screen, UV };
enum class SignalEncoding {
  Unspecified,
  Linear,
  EncodedRgb,
  Signed,
  UnsignedNormalized,
  SignedUnitVectorPacked
};

struct SignalMetadata {
  SignalDomain domain = SignalDomain::Screen2D;
  SignalSemantic semantic = SignalSemantic::Generic;
  SignalSpace space = SignalSpace::Unspecified;
  SignalEncoding encoding = SignalEncoding::Unspecified;
  glm::ivec3 extent{0};
  std::string units;
  bool hasKnownRange = false;
  glm::vec2 knownRange{0.0f, 1.0f};
};

struct SignalDescriptor {
  SignalId id;
  OperationId producer;
  std::string key;
  SignalShape shape = SignalShape::Vector4;
  std::string name;
  SignalMetadata metadata;
};

struct SignalRef {
  SignalId id;
  int frameOffset = 0;

  [[nodiscard]] explicit operator bool() const { return static_cast<bool>(id); }
  auto operator<=>(const SignalRef&) const = default;
};

[[nodiscard]] const char* signalShapeLabel(SignalShape shape);
[[nodiscard]] const char* signalSemanticLabel(SignalSemantic semantic);
[[nodiscard]] const char* signalDomainLabel(SignalDomain domain);
[[nodiscard]] const char* signalSpaceLabel(SignalSpace space);
[[nodiscard]] const char* signalEncodingLabel(SignalEncoding encoding);
[[nodiscard]] std::string signalDescriptorSummary(const SignalDescriptor& signal);

[[nodiscard]] inline bool isScreenImage(const SignalDescriptor& signal) {
  return signal.metadata.domain == SignalDomain::Screen2D;
}

[[nodiscard]] inline bool isScreenScalar(const SignalDescriptor& signal) {
  return isScreenImage(signal) && signal.shape == SignalShape::Scalar;
}

[[nodiscard]] inline bool isColor(const SignalDescriptor& signal) {
  return isScreenImage(signal) && signal.metadata.semantic == SignalSemantic::Color;
}

} // namespace gfxlab::document
