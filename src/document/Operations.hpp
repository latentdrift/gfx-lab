#pragma once

#include "app/RenderOperationState.hpp"
#include "document/Signals.hpp"

#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gfxlab {
struct TextureAsset;
}

namespace gfxlab::document {

struct TextureBinding {
  TextureSource source = TextureSource::SceneMaterial;
  std::shared_ptr<const TextureAsset> imported;
  bool srgb = true;
};

struct TimeTransform {
  float scale = 1.0f;
  float offsetSeconds = 0.0f;

  [[nodiscard]] float apply(float globalTimeSeconds) const {
    return globalTimeSeconds * scale + offsetSeconds;
  }
};

enum class RenderFieldMode { SampleOnly, SurfaceOnly, SceneAndSurface };

struct RenderOperation {
  SignalRef field;
  RenderFieldMode fieldMode = RenderFieldMode::SampleOnly;
  std::vector<PropertyOverride> overrides;
  PassPerturbation perturbation;
  PassOutput presentedOutput = PassOutput::Color;
  TextureBinding texture;
  TimeTransform time;
};

struct SdfPrimitiveOperation {
  int type = 0;
  glm::vec3 position{0.0f};
  glm::vec3 parameters{1.0f, 0.35f, 0.35f};
};

struct SdfCombineOperation {
  SignalRef a;
  SignalRef b;
  int combination = 3;
  float smoothness = 0.45f;
};

struct WaveFieldOperation {
  glm::vec3 sourceA{-1.35f, 0.0f, 0.0f};
  glm::vec3 sourceB{1.35f, 0.0f, 0.0f};
  float wavelength = 0.72f;
  float phaseOffset = 0.0f;
  float amplitudeA = 1.0f;
  float amplitudeB = 1.0f;
  float falloff = 0.08f;
  float bandSharpness = 1.35f;
  int output = 3;
};

struct ElementalFieldOperation {
  glm::vec3 injectorPosition{-2.6f, 0.0f, -1.7f};
  float injectorRadius = 0.8f;
  float heatRate = 1.0f;
  float fuelRate = 1.0f;
  float jetDirection = 0.0f;
  float jetStrength = 0.4f;
  int channel = 0;
};

struct InterpretOperation {
  SignalRef spectrum;
  CompositeInterpretation observer = CompositeInterpretation::SpectralHuman;
  float exposureStops = 0.0f;
  float gain = 1.0f;
  float bias = 0.0f;
};

struct CompositeArithmetic {
  RelationOperator operation = RelationOperator::Normal;
  float gain = 1.0f;
  float bias = 0.0f;
  float opacity = 1.0f;
  int bitDepth = 8;
  CompositeColorSpace colorSpace = CompositeColorSpace::LinearLight;
  CompositeRange range = CompositeRange::Clamp;
};

struct ObserverParameters {
  float exposureStops = 0.0f;
  float rodSensitivity = 4.0f;
  float opponentGain = 4.0f;
};

struct FeedbackSettings {
  float decay = 0.96f;
  glm::vec2 uvOffset{0.0f};
  glm::vec2 uvScale{1.0f};
};

struct CompositeOperation {
  SignalRef a;
  SignalRef b;
  CompositeInterpretation interpretationA = CompositeInterpretation::RawRgb;
  CompositeInterpretation interpretationB = CompositeInterpretation::RawRgb;
  ObserverParameters observer;
  CompositeArithmetic arithmetic;
  SignalRef mask;
  bool invertMask = false;
  std::optional<FeedbackSettings> feedback;
};

struct LuminanceOperation { SignalRef input; };

struct RemapOperation {
  SignalRef input;
  float inputLow = 0.0f;
  float inputHigh = 1.0f;
  float outputLow = 0.0f;
  float outputHigh = 1.0f;
  bool clamp = true;
  SignalSemantic outputSemantic = SignalSemantic::Generic;
};

struct EdgeOperation {
  SignalRef input;
  float strength = 1.0f;
};

struct BlurOperation {
  SignalRef input;
  SignalShape outputShape = SignalShape::Vector4;
  SignalSemantic outputSemantic = SignalSemantic::Color;
  float radiusPixels = 2.0f;
};

struct ThresholdOperation {
  SignalRef input;
  float threshold = 0.5f;
  float softness = 0.05f;
};

struct GradientMapOperation {
  SignalRef input;
  glm::vec4 lowColor{0.02f, 0.0f, 0.08f, 1.0f};
  glm::vec4 highColor{1.0f, 0.55f, 0.08f, 1.0f};
};

struct WarpOperation {
  SignalRef image;
  SignalRef displacement;
  float strengthPixels = 12.0f;
};

struct ConstantOperation {
  glm::vec4 value{1.0f};
  SignalShape shape = SignalShape::Vector4;
  SignalSemantic semantic = SignalSemantic::Color;
};

struct StereoOperation {
  SignalRef left;
  SignalRef right;
  StereoAnalysisMode mode = StereoAnalysisMode::AbsoluteDisparity;
  float maximumDisparityPixels = 64.0f;
  float occlusionTolerance = 0.0025f;
};

struct MeasureOperation {
  SignalRef input;
  MeasurementMetric metric = MeasurementMetric::Coverage;
  float threshold = 0.05f;
  bool absoluteMagnitude = true;
};

using OperationData = std::variant<RenderOperation, SdfPrimitiveOperation, SdfCombineOperation,
  WaveFieldOperation, ElementalFieldOperation, InterpretOperation, CompositeOperation,
  ConstantOperation, StereoOperation, MeasureOperation, LuminanceOperation, RemapOperation,
  EdgeOperation, BlurOperation, ThresholdOperation, GradientMapOperation, WarpOperation>;

struct Operation {
  OperationId id;
  std::string name;
  bool enabled = true;
  OperationData data{RenderOperation{}};
  std::vector<SignalDescriptor> outputs;
};

[[nodiscard]] const char* operationTypeLabel(const Operation& operation);
[[nodiscard]] SignalRef primaryOutput(const Operation& operation);
void synchronizeOperationSignalMetadata(Operation& operation);
[[nodiscard]] Operation makeRenderOperation(OperationId id, std::string name);
[[nodiscard]] Operation makeSdfPrimitiveOperation(OperationId id, std::string name);
[[nodiscard]] Operation makeSdfCombineOperation(OperationId id, std::string name,
  SignalRef a = {}, SignalRef b = {});
[[nodiscard]] Operation makeWaveFieldOperation(OperationId id, std::string name);
[[nodiscard]] Operation makeElementalFieldOperation(OperationId id, std::string name);
[[nodiscard]] Operation makeInterpretOperation(OperationId id, std::string name, SignalRef spectrum);
[[nodiscard]] Operation makeCompositeOperation(OperationId id, std::string name,
  SignalRef a, SignalRef b);
[[nodiscard]] Operation makeStereoOperation(OperationId id, std::string name,
  SignalRef left, SignalRef right);
[[nodiscard]] Operation makeMeasureOperation(OperationId id, std::string name, SignalRef input);
[[nodiscard]] Operation makeConstantOperation(OperationId id, std::string name,
  glm::vec4 value, SignalShape shape = SignalShape::Vector4,
  SignalSemantic semantic = SignalSemantic::Color);
[[nodiscard]] Operation makeLuminanceOperation(OperationId id, std::string name, SignalRef input);
[[nodiscard]] Operation makeRemapOperation(OperationId id, std::string name, SignalRef input,
  SignalSemantic outputSemantic = SignalSemantic::Generic);
[[nodiscard]] Operation makeEdgeOperation(OperationId id, std::string name, SignalRef input);
[[nodiscard]] Operation makeBlurOperation(OperationId id, std::string name, SignalRef input,
  SignalShape outputShape, SignalSemantic outputSemantic);
[[nodiscard]] Operation makeThresholdOperation(OperationId id, std::string name, SignalRef input);
[[nodiscard]] Operation makeGradientMapOperation(OperationId id, std::string name, SignalRef input);
[[nodiscard]] Operation makeWarpOperation(OperationId id, std::string name, SignalRef image,
  SignalRef displacement);

} // namespace gfxlab::document
