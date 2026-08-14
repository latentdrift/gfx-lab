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
// A stack operation describes what a row does, independently of its position.
// LegacyRenderComposite exists solely so v8 documents retain their original
// "render this row, then composite it" evaluation semantics.
enum class StackOperationKind { Render, Interpret, Composite, StereoAnalysis, Measure, LegacyRenderComposite };
enum class StereoAnalysisMode { Anaglyph, SignedDisparity, AbsoluteDisparity, Correspondence, MonocularOcclusion };
enum class MeasurementMetric { MeanMagnitude, RmsMagnitude, PeakMagnitude, Coverage, MeanRed, MeanGreen, MeanBlue };
enum class CompositeInterpretation {
  RawRgb, LResponse, MResponse, SResponse, ConeLuminance, RodResponse,
  RedGreenOpponent, BlueYellowOpponent, SpectralHuman, SpectralAlternate, SpectralRod
};
enum class PassOutput { Color, Depth, Normals, VertexColor, FieldSignal };
enum class CompositeMask { None, PassLuminance, PassDepth, PassEdges, PassField };
enum class CompositeSource {
  Accumulator, CurrentPass, RenderPass, FixedColor, PreviousFrame, RenderPassField, RenderPassSpectrum
};
enum class TextureSource { SceneMaterial, BuiltInChecker, ImportedOverride, White };
enum class UvMapping { MeshUv0, PlanarXy, PlanarXz, PlanarYz };
enum class DisplaySignal {
  DirectRgb, CompositeNtsc,
  LmsReceptorTriplet, RodResponse, MesopicMix,
  LResponse, MResponse, SResponse,
  RedGreenOpponent, BlueYellowOpponent,
  RodConeDifference, RodConeXor
};

struct DisplayReconstructionState {
  bool enabled = false;
  DisplaySignal signal = DisplaySignal::DirectRgb;
  float chromaBleed = 0.55f;
  float lumaChromaCrosstalk = 0.20f;
  float scanlineStrength = 0.18f;
  float phosphorMaskStrength = 0.10f;
  float bloomStrength = 0.12f;
  float bloomRadiusPixels = 2.0f;
  float observerExposureStops = 0.0f;
  float darkAdaptation = 0.65f;
  float rodSensitivity = 4.0f;
  float opponentGain = 4.0f;
  int receptorXorBits = 5;
};

struct PassPerturbation {
  glm::vec3 modelTranslation{0.0f};
  float modelScale = 1.0f;
  float normalInflation = 0.0f;
  glm::vec2 uvOffset{0.0f};
  glm::vec2 uvScale{1.0f};
  float uvRotation = 0.0f;
  glm::vec2 uvPivot{0.5f};
  UvMapping uvMapping = UvMapping::MeshUv0;
  float cameraYaw = 0.0f;
  float cameraPitch = 0.0f;
  float cameraDistance = 0.0f;
  float cameraLateral = 0.0f;
  float stereoConvergence = 4.0f;
  float fieldOfView = 0.0f;
};

struct PassCameraMatrices {
  glm::vec3 eye{0.0f};
  glm::mat4 view{1.0f};
  glm::mat4 projection{1.0f};
};

[[nodiscard]] PassCameraMatrices buildPassCamera(const CameraOrbit& camera, const RendererState& state,
  const PassPerturbation& perturbation, float aspect);

struct CompositeStep {
  RelationOperator operation = RelationOperator::AbsoluteDifference;
  CompositeSource sourceA = CompositeSource::Accumulator;
  CompositeSource sourceB = CompositeSource::CurrentPass;
  int sourceAPassId = 1;
  int sourceBPassId = 1;
  CompositeInterpretation interpretationA = CompositeInterpretation::RawRgb;
  CompositeInterpretation interpretationB = CompositeInterpretation::RawRgb;
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
  int id = 0;
  std::string name;
  bool enabled = true;
  StackOperationKind kind = StackOperationKind::Render;
  RendererState renderer;
  PassPerturbation perturbation;
  PassOutput output = PassOutput::Color;
  TextureSource textureSource = TextureSource::SceneMaterial;
  std::shared_ptr<const TextureAsset> importedTexture;
  bool importedTextureSrgb = true;
  CompositeStep composite;
  StereoAnalysisMode stereoAnalysis = StereoAnalysisMode::AbsoluteDisparity;
  float stereoMaximumDisparityPixels = 64.0f;
  float stereoOcclusionTolerance = 0.0025f;
  float measurementThreshold = 0.05f;
  bool measurementAbsolute = true;
  MeasurementMetric measurementMetric = MeasurementMetric::Coverage;
  bool measurementModulationEnabled = false;
  int measurementTargetPassId = 1;
  AnimationProperty measurementTargetProperty = AnimationProperty::Ambient;
  float measurementInputMinimum = 0.0f;
  float measurementInputMaximum = 1.0f;
  float measurementOutputMinimum = 0.0f;
  float measurementOutputMaximum = 1.0f;
  bool measurementClamp = true;
  float measurementSmoothingSeconds = 0.15f;
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
  [[nodiscard]] DisplayReconstructionState& display() { return display_; }
  [[nodiscard]] const DisplayReconstructionState& display() const { return display_; }

  void select(std::size_t index);
  bool duplicateSelected();
  bool addOperation(StackOperationKind kind);
  bool removeSelected();
  bool moveSelected(int direction);
  void replacePasses(std::vector<RenderPass> passes);

private:
  RenderPass global_;
  DisplayReconstructionState display_;
  std::vector<RenderPass> passes_;
  std::size_t selected_ = 0;
  unsigned int nextPassNumber_ = 3;
  int nextPassId_ = 3;
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
[[nodiscard]] const char* stackOperationKindLabel(StackOperationKind kind);
[[nodiscard]] const char* stackOperationKindId(StackOperationKind kind);
[[nodiscard]] const char* relationOperatorId(RelationOperator operation);
[[nodiscard]] const char* relationOperatorEquation(RelationOperator operation);
[[nodiscard]] const char* relationOperatorMeaning(RelationOperator operation);
[[nodiscard]] const char* measurementMetricLabel(MeasurementMetric metric);
[[nodiscard]] const char* measurementMetricId(MeasurementMetric metric);
[[nodiscard]] bool measurementTargetPropertyCompatible(StackOperationKind targetKind,
  AnimationProperty property);
void resetCompositeTransform(CompositeStep& step);
[[nodiscard]] std::string renderStackConfigJson(const RenderStack& stack, const CameraOrbit& camera,
  TestScene scene, HardwareProfile profile, const AnimationTimeline* timeline = nullptr,
  const ModelAsset* importedModel = nullptr);

} // namespace gfxlab
