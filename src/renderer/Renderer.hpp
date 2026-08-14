#pragma once

#include "app/State.hpp"
#include "app/RenderStack.hpp"

#include <array>
#include <memory>

namespace gfxlab {

struct ModelAsset;
struct TextureAsset;

class Renderer {
public:
  Renderer();
  ~Renderer();

  Renderer(const Renderer&) = delete;
  Renderer& operator=(const Renderer&) = delete;

  unsigned int render(const RendererState& state, const CameraOrbit& camera, TestScene scene,
    bool referenceTarget);
  unsigned int renderRelation(RelationOperator operation, float gain, float bias);
  unsigned int renderPass(const RenderPass& pass, const CameraOrbit& camera, TestScene scene,
    std::size_t targetIndex);
  unsigned int composite(const RenderStack& stack);
  [[nodiscard]] unsigned int stackOperationResult(std::size_t operationIndex) const;
  [[nodiscard]] unsigned int stackOperationDepthResult(std::size_t operationIndex) const;
  [[nodiscard]] unsigned int stackOperationFieldResult(std::size_t operationIndex) const;
  [[nodiscard]] std::array<unsigned int, 4> stackOperationSpectrumResult(std::size_t operationIndex) const;
  unsigned int reconstructDisplay(unsigned int sourceTexture, const DisplayReconstructionState& state,
    std::size_t targetIndex);
  void updateElementalSimulation(float deltaSeconds, const RendererState& state, TestScene scene);
  void resetElementalSimulation();
  void resetFrameHistory();
  unsigned int texturePreview(const TextureAsset* texture);
  void setImportedModel(const ModelAsset& asset);
  void clearImportedModel();

private:
  class Impl;
  std::unique_ptr<Impl> impl_;
};

} // namespace gfxlab
