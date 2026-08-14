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

} // namespace gfxlab::document
