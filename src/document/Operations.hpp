#pragma once

#include "app/RenderStack.hpp"
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

struct RenderOperation {
  std::vector<PropertyOverride> overrides;
  PassPerturbation perturbation;
  PassOutput presentedOutput = PassOutput::Color;
  TextureBinding texture;
};

struct InterpretOperation {
  SignalRef spectrum;
  CompositeInterpretation observer = CompositeInterpretation::SpectralHuman;
  float exposureStops = 0.0f;
  float gain = 1.0f;
  float bias = 0.0f;
};

struct CompositeArithmetic {
  RelationOperator operation = RelationOperator::AbsoluteDifference;
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
  CompositeMask mask = CompositeMask::None;
  bool invertMask = false;
  std::optional<FeedbackSettings> feedback;
};

struct ConstantOperation {
  glm::vec4 value{1.0f};
  SignalKind kind = SignalKind::Color;
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

using OperationData = std::variant<RenderOperation, InterpretOperation, CompositeOperation,
  ConstantOperation, StereoOperation, MeasureOperation>;

struct Operation {
  OperationId id;
  std::string name;
  bool enabled = true;
  OperationData data{RenderOperation{}};
  std::vector<SignalDescriptor> outputs;
};

[[nodiscard]] const char* operationTypeLabel(const Operation& operation);
[[nodiscard]] SignalRef primaryOutput(const Operation& operation);

} // namespace gfxlab::document
