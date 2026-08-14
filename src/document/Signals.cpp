#include "document/Signals.hpp"

namespace gfxlab::document {

const char* signalShapeLabel(const SignalShape shape) {
  switch (shape) {
    case SignalShape::Scalar: return "Scalar";
    case SignalShape::Vector2: return "Vector2";
    case SignalShape::Vector3: return "Vector3";
    case SignalShape::Vector4: return "Vector4";
    case SignalShape::Spectrum16: return "Spectrum16";
  }
  return "Unknown";
}

const char* signalSemanticLabel(const SignalSemantic semantic) {
  switch (semantic) {
    case SignalSemantic::Generic: return "Generic";
    case SignalSemantic::Color: return "Color";
    case SignalSemantic::Luminance: return "Luminance";
    case SignalSemantic::DeviceDepth: return "Device depth";
    case SignalSemantic::LinearDepth: return "Linear depth";
    case SignalSemantic::Normal: return "Normal";
    case SignalSemantic::FieldStrength: return "Field strength";
    case SignalSemantic::SignedDistance: return "Signed distance";
    case SignalSemantic::MaskCoverage: return "Mask coverage";
    case SignalSemantic::EdgeStrength: return "Edge strength";
    case SignalSemantic::EdgeDirection: return "Edge direction";
    case SignalSemantic::Measurement: return "Measurement";
    case SignalSemantic::Spectrum: return "Spectrum";
  }
  return "Unknown";
}

const char* signalDomainLabel(const SignalDomain domain) {
  switch (domain) {
    case SignalDomain::Document: return "Document";
    case SignalDomain::Screen2D: return "Screen";
    case SignalDomain::Surface: return "Surface";
    case SignalDomain::World2D: return "World 2D";
    case SignalDomain::World3D: return "World 3D";
  }
  return "Unknown";
}

const char* signalSpaceLabel(const SignalSpace space) {
  switch (space) {
    case SignalSpace::Unspecified: return "Unspecified";
    case SignalSpace::Object: return "Object";
    case SignalSpace::World: return "World";
    case SignalSpace::View: return "View";
    case SignalSpace::Screen: return "Screen";
    case SignalSpace::UV: return "UV";
  }
  return "Unknown";
}

const char* signalEncodingLabel(const SignalEncoding encoding) {
  switch (encoding) {
    case SignalEncoding::Unspecified: return "Unspecified";
    case SignalEncoding::Linear: return "Linear";
    case SignalEncoding::EncodedRgb: return "Encoded RGB";
    case SignalEncoding::Signed: return "Signed";
    case SignalEncoding::UnsignedNormalized: return "Normalized 0..1";
    case SignalEncoding::SignedUnitVectorPacked: return "Signed unit vector packed as 0..1";
  }
  return "Unknown";
}

std::string signalDescriptorSummary(const SignalDescriptor& signal) {
  std::string result = std::string(signalDomainLabel(signal.metadata.domain)) + " · " +
    signalShapeLabel(signal.shape) + " · " + signalSemanticLabel(signal.metadata.semantic);
  if (signal.metadata.space != SignalSpace::Unspecified)
    result += std::string(" · ") + signalSpaceLabel(signal.metadata.space) + " space";
  return result;
}

} // namespace gfxlab::document
