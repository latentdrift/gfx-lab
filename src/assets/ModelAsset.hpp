#pragma once

#include "renderer/Mesh.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gfxlab {

struct ModelAsset {
  std::string name;
  std::string sourcePath;
  std::vector<Vertex> vertices;
  glm::vec3 sourceBoundsMinimum{0.0f};
  glm::vec3 sourceBoundsMaximum{0.0f};
  float normalizationScale = 1.0f;
  std::size_t sourceMeshCount = 0;
  std::size_t triangleCount = 0;
  bool hasTextureCoordinates = false;
  bool hasVertexColors = false;
  bool hasTangents = false;
  std::uint64_t contentHash = 0;
};

struct ModelImportResult {
  std::shared_ptr<const ModelAsset> asset;
  std::string error;

  [[nodiscard]] explicit operator bool() const { return asset != nullptr; }
};

[[nodiscard]] ModelImportResult importModelAsset(const std::string& path);

} // namespace gfxlab
