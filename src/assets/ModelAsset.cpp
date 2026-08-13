#include "assets/ModelAsset.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/geometric.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <string>

namespace gfxlab {
namespace {

constexpr std::size_t maximumTriangles = 2'000'000;

bool finite(const glm::vec3& value) {
  return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

glm::vec3 vector(const aiVector3D& value) { return {value.x, value.y, value.z}; }

glm::vec3 fallbackTangent(const glm::vec3& normal) {
  const glm::vec3 axis = std::abs(normal.y) < 0.9f ? glm::vec3(0, 1, 0) : glm::vec3(1, 0, 0);
  return glm::normalize(glm::cross(axis, normal));
}

void hashFloat(std::uint64_t& hash, const float value) {
  hash ^= std::bit_cast<std::uint32_t>(value);
  hash *= 1099511628211ull;
}

std::uint64_t hashVertices(const std::vector<Vertex>& vertices) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const Vertex& vertex : vertices) {
    for (const float value : {vertex.position.x, vertex.position.y, vertex.position.z,
      vertex.normal.x, vertex.normal.y, vertex.normal.z, vertex.uv.x, vertex.uv.y,
      vertex.color.r, vertex.color.g, vertex.color.b, vertex.tangent.x, vertex.tangent.y,
      vertex.tangent.z, vertex.tangent.w}) hashFloat(hash, value);
  }
  return hash;
}

} // namespace

ModelImportResult importModelAsset(const std::string& path) {
  const std::filesystem::path sourcePath(path);
  std::string extension = sourcePath.extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
    [](const unsigned char character) { return static_cast<char>(std::tolower(character)); });
  if (extension != ".obj" && extension != ".gltf" && extension != ".glb")
    return {nullptr, "Supported model formats are OBJ, glTF, and binary GLB."};

  Assimp::Importer importer;
  constexpr unsigned int flags = aiProcess_Triangulate | aiProcess_JoinIdenticalVertices |
    aiProcess_GenSmoothNormals | aiProcess_CalcTangentSpace | aiProcess_PreTransformVertices |
    aiProcess_ImproveCacheLocality | aiProcess_SortByPType | aiProcess_FindInvalidData |
    aiProcess_ValidateDataStructure;
  const aiScene* scene = importer.ReadFile(path, flags);
  if (scene == nullptr)
    return {nullptr, std::string("Assimp could not import the model: ") + importer.GetErrorString()};
  if (scene->mNumMeshes == 0) return {nullptr, "The file contains no meshes."};

  auto asset = std::make_shared<ModelAsset>();
  asset->name = sourcePath.filename().string();
  asset->sourcePath = std::filesystem::absolute(sourcePath).lexically_normal().string();
  asset->sourceMeshCount = scene->mNumMeshes;
  glm::vec3 minimum(std::numeric_limits<float>::max());
  glm::vec3 maximum(std::numeric_limits<float>::lowest());

  for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
    const aiMesh& mesh = *scene->mMeshes[meshIndex];
    aiColor4D materialColor(0.72f, 0.75f, 0.78f, 1.0f);
    if (mesh.mMaterialIndex < scene->mNumMaterials) {
      const aiMaterial* material = scene->mMaterials[mesh.mMaterialIndex];
      if (AI_SUCCESS != aiGetMaterialColor(material, AI_MATKEY_BASE_COLOR, &materialColor))
        static_cast<void>(aiGetMaterialColor(material, AI_MATKEY_COLOR_DIFFUSE, &materialColor));
    }
    asset->hasTextureCoordinates = asset->hasTextureCoordinates || mesh.HasTextureCoords(0);
    asset->hasVertexColors = asset->hasVertexColors || mesh.HasVertexColors(0);
    asset->hasTangents = asset->hasTangents || mesh.HasTangentsAndBitangents();

    for (unsigned int faceIndex = 0; faceIndex < mesh.mNumFaces; ++faceIndex) {
      const aiFace& face = mesh.mFaces[faceIndex];
      if (face.mNumIndices != 3) continue;
      if (++asset->triangleCount > maximumTriangles)
        return {nullptr, "The model exceeds the 2,000,000 triangle import limit."};
      for (unsigned int corner = 0; corner < 3; ++corner) {
        const unsigned int index = face.mIndices[corner];
        if (index >= mesh.mNumVertices) return {nullptr, "The model contains an invalid vertex index."};
        const glm::vec3 position = vector(mesh.mVertices[index]);
        glm::vec3 normal = mesh.HasNormals() ? vector(mesh.mNormals[index]) : glm::vec3(0, 1, 0);
        if (!finite(position) || !finite(normal)) return {nullptr, "The model contains non-finite vertex data."};
        normal = glm::length(normal) > 0.00001f ? glm::normalize(normal) : glm::vec3(0, 1, 0);
        const glm::vec2 uv = mesh.HasTextureCoords(0)
          ? glm::vec2(mesh.mTextureCoords[0][index].x, mesh.mTextureCoords[0][index].y) : glm::vec2(0.0f);
        const glm::vec3 color = mesh.HasVertexColors(0)
          ? glm::vec3(mesh.mColors[0][index].r, mesh.mColors[0][index].g, mesh.mColors[0][index].b)
          : glm::vec3(materialColor.r, materialColor.g, materialColor.b);
        glm::vec3 tangent = mesh.HasTangentsAndBitangents() ? vector(mesh.mTangents[index]) : fallbackTangent(normal);
        tangent = glm::length(tangent) > 0.00001f ? glm::normalize(tangent) : fallbackTangent(normal);
        float handedness = 1.0f;
        if (mesh.HasTangentsAndBitangents())
          handedness = glm::dot(glm::cross(normal, tangent), vector(mesh.mBitangents[index])) < 0.0f ? -1.0f : 1.0f;
        glm::vec3 barycentric(0.0f);
        barycentric[corner] = 1.0f;
        asset->vertices.push_back({position, normal, uv, barycentric, color, glm::vec4(tangent, handedness)});
        minimum = glm::min(minimum, position);
        maximum = glm::max(maximum, position);
      }
    }
  }
  if (asset->vertices.empty()) return {nullptr, "The file contains no triangle geometry."};

  asset->sourceBoundsMinimum = minimum;
  asset->sourceBoundsMaximum = maximum;
  const glm::vec3 center = (minimum + maximum) * 0.5f;
  const glm::vec3 extent = maximum - minimum;
  const float largestExtent = std::max(extent.x, std::max(extent.y, extent.z));
  if (!std::isfinite(largestExtent) || largestExtent <= 0.000001f)
    return {nullptr, "The model bounds are degenerate."};
  asset->normalizationScale = 3.0f / largestExtent;
  for (Vertex& vertex : asset->vertices)
    vertex.position = (vertex.position - center) * asset->normalizationScale;
  asset->contentHash = hashVertices(asset->vertices);
  return {std::move(asset), {}};
}

} // namespace gfxlab
