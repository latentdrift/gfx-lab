#pragma once

#include "app/Animation.hpp"
#include "app/State.hpp"

#include <glm/glm.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace gfxlab {

struct ModelAsset;
struct TextureAsset;

enum class CompositeColorSpace { EncodedRgb, LinearLight };
enum class CompositeRange { Clamp, Preserve, Wrap };
enum class PassOutput { Color, Depth, Normals, VertexColor };
enum class CompositeMask { None, PassLuminance, PassDepth, PassEdges };
enum class CompositeSource { Accumulator, CurrentPass, RenderPass, FixedColor, PreviousFrame };
enum class TextureSource { SceneMaterial, BuiltInChecker, ImportedOverride, White };

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
  CompositeSource sourceA = CompositeSource::Accumulator;
  CompositeSource sourceB = CompositeSource::CurrentPass;
  int sourceAPass = 0;
  int sourceBPass = 0;
  glm::vec4 fixedColor{1.0f};
  int bitDepth = 8;
  float historyDecay = 0.96f;
  glm::vec2 historyUvOffset{0.0f};
  glm::vec2 historyUvScale{1.0f};
  float gain = 4.0f;
  float bias = 0.0f;
  float opacity = 1.0f;
  CompositeColorSpace colorSpace = CompositeColorSpace::EncodedRgb;
  CompositeRange range = CompositeRange::Clamp;
  CompositeMask mask = CompositeMask::None;
  bool invertMask = false;
};

struct PropertyOverride {
  AnimationProperty property = AnimationProperty::VertexQuantization;
  glm::vec4 value{0.0f};
};

struct RenderPass {
  std::string name;
  bool enabled = true;
  RendererState renderer;
  PassPerturbation perturbation;
  PassOutput output = PassOutput::Color;
  TextureSource textureSource = TextureSource::SceneMaterial;
  std::shared_ptr<const TextureAsset> importedTexture;
  bool importedTextureSrgb = true;
  CompositeStep composite;
  PassAnimation animation;
  std::vector<PropertyOverride> overrides;
  bool importedTextureOverride = false;
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
  [[nodiscard]] RenderPass& global() { return global_; }
  [[nodiscard]] const RenderPass& global() const { return global_; }

  void select(std::size_t index);
  bool duplicateSelected();
  bool removeSelected();
  bool moveSelected(int direction);

private:
  RenderPass global_;
  std::vector<RenderPass> passes_;
  std::size_t selected_ = 0;
  unsigned int nextPassNumber_ = 3;
};

[[nodiscard]] bool animationPropertyIsPassLocal(AnimationProperty property);
[[nodiscard]] const PropertyOverride* findRenderPassOverride(const RenderPass& pass, AnimationProperty property);
void setRenderPassOverride(RenderPass& pass, AnimationProperty property, const glm::vec4& value);
[[nodiscard]] bool clearRenderPassOverride(RenderPass& pass, AnimationProperty property);
void replaceRenderPassOverrides(RenderPass& pass, const RenderPass& global, const RenderPass& materialized);
[[nodiscard]] RenderPass resolveRenderPass(const RenderStack& stack, std::size_t passIndex);
[[nodiscard]] RenderPass materializeRenderPass(const RenderStack& stack, std::size_t passIndex,
  float timeSeconds = 0.0f);

[[nodiscard]] const char* relationOperatorLabel(RelationOperator operation);
[[nodiscard]] const char* relationOperatorId(RelationOperator operation);
[[nodiscard]] const char* relationOperatorEquation(RelationOperator operation);
[[nodiscard]] const char* relationOperatorMeaning(RelationOperator operation);
void resetCompositeTransform(CompositeStep& step);
[[nodiscard]] std::string renderStackConfigJson(const RenderStack& stack, const CameraOrbit& camera,
  TestScene scene, HardwareProfile profile, const AnimationTimeline* timeline = nullptr,
  const ModelAsset* importedModel = nullptr);

} // namespace gfxlab
