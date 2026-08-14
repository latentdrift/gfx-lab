#include "document/Signals.hpp"

namespace gfxlab::document {

const char* signalKindLabel(const SignalKind kind) {
  switch (kind) {
    case SignalKind::Color: return "Color";
    case SignalKind::Depth: return "Depth";
    case SignalKind::Normal: return "Normal";
    case SignalKind::Field: return "Field";
    case SignalKind::Spectrum16: return "Spectrum16";
    case SignalKind::Scalar: return "Scalar";
    case SignalKind::Vector2: return "Vector2";
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
