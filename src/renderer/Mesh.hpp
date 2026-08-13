#pragma once

#include <glm/glm.hpp>

namespace gfxlab {

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec3 barycentric;
  glm::vec3 color;
  glm::vec4 tangent;
};

struct MeshRange {
  int first = 0;
  int count = 0;
};

} // namespace gfxlab
