#pragma once

#include "app/RenderStack.hpp"
#include "document/Document.hpp"
#include "evaluation/SignalRegistry.hpp"

namespace gfxlab {
class Renderer;
}

namespace gfxlab::evaluation {

// Transitional backend binding: typed signals are populated from the existing
// OpenGL pass targets while operation execution migrates out of Renderer.
void publishLegacyResults(const document::Document& document, const RenderStack& legacyStack,
  const Renderer& renderer, SignalRegistry& registry, std::uint64_t revision);

} // namespace gfxlab::evaluation
