#pragma once

#include "app/State.hpp"
#include "app/RenderOperationState.hpp"
#include "document/Document.hpp"
#include "evaluation/EvaluationPlan.hpp"
#include "evaluation/SignalRegistry.hpp"

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
  unsigned int compareSignals(unsigned int a, unsigned int b, RelationOperator operation,
    float gain = 1.0f, float bias = 0.0f);
  unsigned int renderPass(const RenderPass& pass, const CameraOrbit& camera, TestScene scene,
    std::size_t targetIndex);
  unsigned int evaluate(const document::Document& document,
    const evaluation::EvaluationPlan& plan, evaluation::SignalRegistry& signals,
    std::uint64_t revision, float timeSeconds);
  unsigned int previewSignal(const evaluation::SignalResource& resource,
    const RendererState& state, std::size_t targetIndex);
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
