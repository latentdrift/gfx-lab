#include "assets/ModelAsset.hpp"

#include <assimp/Importer.hpp>
#include <assimp/material.h>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/geometric.hpp>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_FAILURE_USERMSG
#include <stb/stb_image.h>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

std::uint64_t hashBytes(const std::vector<std::uint8_t>& bytes) {
  std::uint64_t hash = 1469598103934665603ull;
  for (const std::uint8_t byte : bytes) {
    hash ^= byte;
    hash *= 1099511628211ull;
  }
  return hash;
}

void flipRows(TextureAsset& texture) {
  const std::size_t stride = static_cast<std::size_t>(texture.width) * 4;
  for (int y = 0; y < texture.height / 2; ++y) {
    const std::size_t top = static_cast<std::size_t>(y) * stride;
    const std::size_t bottom = static_cast<std::size_t>(texture.height - y - 1) * stride;
    for (std::size_t x = 0; x < stride; ++x) std::swap(texture.rgba8[top + x], texture.rgba8[bottom + x]);
  }
}

void finishTexture(TextureAsset& texture) {
  flipRows(texture);
  texture.hasAlpha = false;
  for (std::size_t index = 3; index < texture.rgba8.size(); index += 4)
    texture.hasAlpha = texture.hasAlpha || texture.rgba8[index] != 255;
  texture.contentHash = hashBytes(texture.rgba8);
}

TextureImportResult decodeTextureMemory(const unsigned char* encoded, const int byteCount,
    std::string name, std::string sourcePath, const bool embedded) {
  int width = 0;
  int height = 0;
  int sourceChannels = 0;
  stbi_uc* decoded = stbi_load_from_memory(encoded, byteCount, &width, &height, &sourceChannels, STBI_rgb_alpha);
  if (decoded == nullptr)
    return {nullptr, std::string("Could not decode texture: ") + stbi_failure_reason()};
  auto texture = std::make_shared<TextureAsset>();
  texture->name = std::move(name);
  texture->sourcePath = std::move(sourcePath);
  texture->width = width;
  texture->height = height;
  texture->embedded = embedded;
  const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  texture->rgba8.assign(decoded, decoded + bytes);
  stbi_image_free(decoded);
  finishTexture(*texture);
  return {std::move(texture), {}};
}

TextureImportResult decodeEmbeddedTexture(const aiTexture& source, const std::string& sourcePath) {
  if (source.mHeight == 0)
    return decodeTextureMemory(reinterpret_cast<const unsigned char*>(source.pcData),
      static_cast<int>(source.mWidth), source.mFilename.length > 0 ? source.mFilename.C_Str() : "Embedded texture",
      sourcePath, true);
  auto texture = std::make_shared<TextureAsset>();
  texture->name = source.mFilename.length > 0 ? source.mFilename.C_Str() : "Embedded texture";
  texture->sourcePath = sourcePath;
  texture->width = static_cast<int>(source.mWidth);
  texture->height = static_cast<int>(source.mHeight);
  texture->embedded = true;
  texture->rgba8.resize(static_cast<std::size_t>(texture->width) * static_cast<std::size_t>(texture->height) * 4);
  for (std::size_t index = 0; index < texture->rgba8.size() / 4; ++index) {
    texture->rgba8[index * 4 + 0] = source.pcData[index].r;
    texture->rgba8[index * 4 + 1] = source.pcData[index].g;
    texture->rgba8[index * 4 + 2] = source.pcData[index].b;
    texture->rgba8[index * 4 + 3] = source.pcData[index].a;
  }
  finishTexture(*texture);
  return {std::move(texture), {}};
}

} // namespace

TextureImportResult importTextureAsset(const std::string& path) {
  const std::filesystem::path sourcePath(path);
  int width = 0;
  int height = 0;
  int sourceChannels = 0;
  stbi_uc* decoded = stbi_load(path.c_str(), &width, &height, &sourceChannels, STBI_rgb_alpha);
  if (decoded == nullptr)
    return {nullptr, std::string("Could not load texture '") + sourcePath.filename().string() + "': " +
      stbi_failure_reason()};
  auto texture = std::make_shared<TextureAsset>();
  texture->name = sourcePath.filename().string();
  texture->sourcePath = std::filesystem::absolute(sourcePath).lexically_normal().string();
  texture->width = width;
  texture->height = height;
  const std::size_t bytes = static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4;
  texture->rgba8.assign(decoded, decoded + bytes);
  stbi_image_free(decoded);
  finishTexture(*texture);
  return {std::move(texture), {}};
}

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
  std::unordered_map<std::string, int> textureIndices;
  asset->materials.reserve(std::max(1u, scene->mNumMaterials));
  for (unsigned int materialIndex = 0; materialIndex < scene->mNumMaterials; ++materialIndex) {
    const aiMaterial& source = *scene->mMaterials[materialIndex];
    MaterialAsset material;
    aiString materialName;
    if (source.Get(AI_MATKEY_NAME, materialName) == AI_SUCCESS) material.name = materialName.C_Str();
    if (material.name.empty()) material.name = "Material " + std::to_string(materialIndex + 1);
    aiColor4D color(1.0f, 1.0f, 1.0f, 1.0f);
    if (AI_SUCCESS != aiGetMaterialColor(&source, AI_MATKEY_BASE_COLOR, &color))
      static_cast<void>(aiGetMaterialColor(&source, AI_MATKEY_COLOR_DIFFUSE, &color));
    material.baseColor = {color.r, color.g, color.b, color.a};
    aiString textureReference;
    const aiTextureType textureType = source.GetTextureCount(aiTextureType_BASE_COLOR) > 0
      ? aiTextureType_BASE_COLOR : aiTextureType_DIFFUSE;
    if (source.GetTexture(textureType, 0, &textureReference) == AI_SUCCESS) {
      const std::string reference = textureReference.C_Str();
      const auto existing = textureIndices.find(reference);
      if (existing != textureIndices.end()) {
        material.baseColorTexture = existing->second;
      } else {
        TextureImportResult importedTexture;
        if (const aiTexture* embedded = scene->GetEmbeddedTexture(textureReference.C_Str())) {
          importedTexture = decodeEmbeddedTexture(*embedded, asset->sourcePath + "#" + reference);
        } else {
          std::filesystem::path texturePath(reference);
          if (texturePath.is_relative()) texturePath = sourcePath.parent_path() / texturePath;
          importedTexture = importTextureAsset(texturePath.lexically_normal().string());
        }
        if (importedTexture) {
          material.baseColorTexture = static_cast<int>(asset->textures.size());
          textureIndices.emplace(reference, material.baseColorTexture);
          asset->textures.push_back(*importedTexture.asset);
        } else {
          asset->importWarnings.push_back(material.name + ": " + importedTexture.error);
        }
      }
    }
    asset->materials.push_back(std::move(material));
  }
  if (asset->materials.empty()) asset->materials.push_back({"Default material"});
  glm::vec3 minimum(std::numeric_limits<float>::max());
  glm::vec3 maximum(std::numeric_limits<float>::lowest());

  for (unsigned int meshIndex = 0; meshIndex < scene->mNumMeshes; ++meshIndex) {
    const aiMesh& mesh = *scene->mMeshes[meshIndex];
    const std::size_t firstVertex = asset->vertices.size();
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
    if (asset->vertices.size() > firstVertex) {
      SubmeshAsset submesh;
      submesh.name = mesh.mName.length > 0 ? mesh.mName.C_Str() : "Mesh " + std::to_string(meshIndex + 1);
      submesh.firstVertex = firstVertex;
      submesh.vertexCount = asset->vertices.size() - firstVertex;
      submesh.materialIndex = std::min<std::size_t>(mesh.mMaterialIndex, asset->materials.size() - 1);
      asset->submeshes.push_back(std::move(submesh));
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
  for (const MaterialAsset& material : asset->materials) {
    for (const float component : {material.baseColor.r, material.baseColor.g, material.baseColor.b,
      material.baseColor.a}) hashFloat(asset->contentHash, component);
    asset->contentHash ^= static_cast<std::uint64_t>(material.baseColorTexture + 1);
    asset->contentHash *= 1099511628211ull;
  }
  for (const TextureAsset& texture : asset->textures) {
    asset->contentHash ^= texture.contentHash;
    asset->contentHash *= 1099511628211ull;
  }
  return {std::move(asset), {}};
}

} // namespace gfxlab
