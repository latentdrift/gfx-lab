#pragma once

#include "app/State.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <string>
#include <vector>

namespace gfxlab {

enum class CompositeColorSpace { EncodedRgb, LinearLight };
enum class CompositeRange { Clamp, Preserve, Wrap };
enum class PassOutput { Color, Depth, Normals, VertexColor };
enum class CompositeMask { None, PassLuminance, PassDepth, GeometryEdges };

struct PassPerturbation {
  glm::vec3 modelTranslation{0.0f};
  float modelScale = 1.0f;
  float normalInflation = 0.0f;
  glm::vec2 uvOffset{0.0f};
  glm::vec2 uvScale{1.0f};
  float cameraYaw = 0.0f;
  float cameraPitch = 0.0f;
  float cameraDistance = 0.0f;
  float fieldOfView = 0.0f;
};

struct CompositeStep {
  RelationOperator operation = RelationOperator::AbsoluteDifference;
  float gain = 4.0f;
  float bias = 0.0f;
  float opacity = 1.0f;
  CompositeColorSpace colorSpace = CompositeColorSpace::EncodedRgb;
  CompositeRange range = CompositeRange::Clamp;
  CompositeMask mask = CompositeMask::None;
  bool invertMask = false;
};

struct RenderPass {
  std::string name;
  bool enabled = true;
  RendererState renderer;
  PassPerturbation perturbation;
  PassOutput output = PassOutput::Color;
  CompositeStep composite;
};

class RenderStack {
public:
  static constexpr std::size_t maximumPasses = 8;

  RenderStack();

  [[nodiscard]] std::vector<RenderPass>& passes() { return passes_; }
  [[nodiscard]] const std::vector<RenderPass>& passes() const { return passes_; }
  [[nodiscard]] std::size_t selectedIndex() const { return selected_; }
  [[nodiscard]] RenderPass& selected();
  [[nodiscard]] const RenderPass& selected() const;

  void select(std::size_t index);
  bool duplicateSelected();
  bool removeSelected();
  bool moveSelected(int direction);

private:
  std::vector<RenderPass> passes_;
  std::size_t selected_ = 0;
  unsigned int nextPassNumber_ = 3;
};

} // namespace gfxlab
