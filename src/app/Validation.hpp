#pragma once

#include "app/State.hpp"

namespace gfxlab {

class Renderer;

void runStartupValidationIfRequested(Renderer& renderer, RendererState& current, RendererState& reference,
  CameraOrbit& camera, TestScene& scene, Category& category);

} // namespace gfxlab
