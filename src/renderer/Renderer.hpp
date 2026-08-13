#pragma once

#include "app/State.hpp"

#include <memory>

namespace gfxlab {

class Renderer {
public:
  Renderer();
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  unsigned int render(const RendererState& state, const CameraOrbit& camera, TestScene scene,
    bool referenceTarget);
  unsigned int renderRelation(RelationOperator operation, float gain, float bias);

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace gfxlab
