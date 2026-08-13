#pragma once

#include "renderer/Mesh.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace gfxlab {

enum class TextureColorSpace { Srgb, Linear };

struct TextureAsset {
  std::string name;
  std::string sourcePath;
  int width = 0;
  int height = 0;
  std::vector<std::uint8_t> rgba8;
  TextureColorSpace colorSpace = TextureColorSpace::Srgb;
  bool hasAlpha = false;
  bool embedded = false;
  std::uint64_t contentHash = 0;
};

struct MaterialAsset {
  std::string name;
  glm::vec4 baseColor{1.0f};
  int baseColorTexture = -1;
};

struct SubmeshAsset {
  std::string name;
  std::size_t firstVertex = 0;
  std::size_t vertexCount = 0;
  std::size_t materialIndex = 0;
};

struct ModelAsset {
  std::string name;
  std::string sourcePath;
  std::vector<Vertex> vertices;
  std::vector<SubmeshAsset> submeshes;
  std::vector<MaterialAsset> materials;
  std::vector<TextureAsset> textures;
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

struct TextureImportResult {
  std::shared_ptr<const TextureAsset> asset;
  std::string error;

  [[nodiscard]] explicit operator bool() const { return asset != nullptr; }
};

[[nodiscard]] ModelImportResult importModelAsset(const std::string& path);
[[nodiscard]] TextureImportResult importTextureAsset(const std::string& path);

} // namespace gfxlab
