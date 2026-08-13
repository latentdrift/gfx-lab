#include "renderer/TestGeometry.hpp"

#include <glm/gtc/constants.hpp>

#include <array>
#include <cmath>

namespace gfxlab {

void appendTriangle(std::vector<Vertex>& vertices, Vertex a, Vertex b, Vertex c) {
  a.barycentric = {1, 0, 0};
  b.barycentric = {0, 1, 0};
  c.barycentric = {0, 0, 1};
  vertices.push_back(a);
  vertices.push_back(b);
  vertices.push_back(c);
}

std::vector<Vertex> makePlane(float width, float depth, int xSegments, int zSegments) {
  std::vector<Vertex> vertices;
  for (int z = 0; z < zSegments; ++z) {
    for (int x = 0; x < xSegments; ++x) {
      const float x0 = (static_cast<float>(x) / xSegments - 0.5f) * width;
      const float x1 = (static_cast<float>(x + 1) / xSegments - 0.5f) * width;
      const float z0 = (static_cast<float>(z) / zSegments - 0.5f) * depth;
      const float z1 = (static_cast<float>(z + 1) / zSegments - 0.5f) * depth;
      const float u0 = static_cast<float>(x) / xSegments * 8.0f;
      const float u1 = static_cast<float>(x + 1) / xSegments * 8.0f;
      const float v0 = static_cast<float>(z) / zSegments * 16.0f;
      const float v1 = static_cast<float>(z + 1) / zSegments * 16.0f;
      const glm::vec3 n(0, 1, 0), color(0.65f, 0.72f, 0.78f);
      const glm::vec4 tangent(1, 0, 0, -1);
      Vertex a{{x0, 0, z0}, n, {u0, v0}, {}, color, tangent};
      Vertex b{{x0, 0, z1}, n, {u0, v1}, {}, color, tangent};
      Vertex c{{x1, 0, z1}, n, {u1, v1}, {}, color, tangent};
      Vertex d{{x1, 0, z0}, n, {u1, v0}, {}, color, tangent};
      appendTriangle(vertices, a, b, c);
      appendTriangle(vertices, a, c, d);
    }
  }
  return vertices;
}

std::vector<Vertex> makeQuad() {
  std::vector<Vertex> vertices;
  const glm::vec3 n(0, 0, 1), color(0.5f, 0.8f, 0.7f);
  const glm::vec4 tangent(1, 0, 0, 1);
  Vertex a{{-1, -1, 0}, n, {0, 0}, {}, color, tangent};
  Vertex b{{ 1, -1, 0}, n, {4, 0}, {}, color, tangent};
  Vertex c{{ 1,  1, 0}, n, {4, 4}, {}, color, tangent};
  Vertex d{{-1,  1, 0}, n, {0, 4}, {}, color, tangent};
  appendTriangle(vertices, a, b, c);
  appendTriangle(vertices, a, c, d);
  return vertices;
}

std::vector<Vertex> makeSphere(int longitudeSegments, int latitudeSegments) {
  std::vector<Vertex> vertices;
  auto point = [=](int longitude, int latitude) {
    const float u = static_cast<float>(longitude) / longitudeSegments;
    const float v = static_cast<float>(latitude) / latitudeSegments;
    const float a = u * glm::two_pi<float>();
    const float b = (v - 0.5f) * glm::pi<float>();
    const glm::vec3 n(std::cos(b) * std::sin(a), std::sin(b), std::cos(b) * std::cos(a));
    const glm::vec3 tangent = glm::normalize(glm::vec3(std::cos(a), 0.0f, -std::sin(a)));
    return Vertex{n, n, {u * 4.0f, v * 2.0f}, {}, n * 0.5f + 0.5f, glm::vec4(tangent, 1.0f)};
  };
  for (int y = 0; y < latitudeSegments; ++y) {
    for (int x = 0; x < longitudeSegments; ++x) {
      Vertex a = point(x, y), b = point(x + 1, y), c = point(x + 1, y + 1), d = point(x, y + 1);
      appendTriangle(vertices, a, b, c);
      appendTriangle(vertices, a, c, d);
    }
  }
  return vertices;
}

std::vector<Vertex> makeTorus(const int majorSegments, const int minorSegments) {
  constexpr float majorRadius = 1.15f;
  constexpr float minorRadius = 0.46f;
  std::vector<Vertex> vertices;
  vertices.reserve(majorSegments * minorSegments * 6);

  auto point = [=](int majorIndex, int minorIndex) {
    const float u = static_cast<float>(majorIndex) / majorSegments;
    const float v = static_cast<float>(minorIndex) / minorSegments;
    const float a = u * glm::two_pi<float>();
    const float b = v * glm::two_pi<float>();
    const glm::vec3 normal(std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b));
    const glm::vec3 center(majorRadius * std::cos(a), 0.0f, majorRadius * std::sin(a));
    const glm::vec3 color = 0.5f + 0.5f * glm::vec3(std::cos(a), std::sin(b), std::sin(a));
    const glm::vec3 tangent(-std::sin(a), 0.0f, std::cos(a));
    return Vertex{center + minorRadius * normal, normal, glm::vec2(u * 4.0f, v * 2.0f), {0, 0, 0}, color,
      glm::vec4(tangent, -1.0f)};
  };

  for (int i = 0; i < majorSegments; ++i) {
    for (int j = 0; j < minorSegments; ++j) {
      std::array<Vertex, 4> q = {point(i, j), point(i + 1, j), point(i + 1, j + 1), point(i, j + 1)};
      // The torus parameterization's +u x +v direction points inward, so emit
      // each quad in the opposite order to keep outward faces counter-clockwise.
      const std::array<int, 6> order = {0, 2, 1, 0, 3, 2};
      for (int k = 0; k < 6; ++k) {
        Vertex vertex = q[order[k]];
        vertex.barycentric = glm::vec3(0.0f);
        vertex.barycentric[k % 3] = 1.0f;
        vertices.push_back(vertex);
      }
    }
  }
  return vertices;
}

} // namespace gfxlab
