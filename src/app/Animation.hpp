#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace gfxlab {

struct RenderPass;
class RenderStack;

enum class KeyframeInterpolation { Step, Linear, SmoothStep };

enum class AnimationProperty {
  ModelTranslation,
  ModelScale,
  NormalInflation,
  UvOffset,
  UvScale,
  CameraYaw,
  CameraPitch,
  CameraDistance,
  FieldOfViewOffset,
  CompositeGain,
  CompositeBias,
  CompositeOpacity,
  VertexQuantization,
  NormalStrength,
  Ambient,
  LightAzimuth,
  LightElevation,
  Shininess,
  DepthCueStart,
  DepthCueEnd,
  FarColor,
  FogStart,
  FogEnd,
  PrimitiveColor,
  EnvironmentColor,
  AlphaThreshold,
  Count,
};

struct AnimationPropertyInfo {
  std::string_view id;
  std::string_view label;
  std::string_view group;
  int components = 1;
};

struct PropertyKeyframe {
  float timeSeconds = 0.0f;
  glm::vec4 value{0.0f};
};

struct PropertyAnimationTrack {
  AnimationProperty property = AnimationProperty::ModelTranslation;
  KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
  std::vector<PropertyKeyframe> keyframes;
};

struct PassAnimation {
  bool enabled = true;
  std::vector<PropertyAnimationTrack> tracks;
};

struct AnimationTimeline {
  float timeSeconds = 0.0f;
  float durationSeconds = 4.0f;
  float playbackRate = 1.0f;
  bool playing = false;
  bool loop = true;
  bool autoKey = false;
  bool showAllPasses = false;

  void advance(float deltaSeconds);
};

[[nodiscard]] const AnimationPropertyInfo& animationPropertyInfo(AnimationProperty property);
[[nodiscard]] glm::vec4 animationPropertyValue(const RenderPass& pass, AnimationProperty property);
void setAnimationPropertyValue(RenderPass& pass, AnimationProperty property, const glm::vec4& value);
[[nodiscard]] PropertyAnimationTrack* findPropertyTrack(RenderPass& pass, AnimationProperty property);
[[nodiscard]] const PropertyAnimationTrack* findPropertyTrack(const RenderPass& pass, AnimationProperty property);
[[nodiscard]] glm::vec4 samplePropertyTrack(const PropertyAnimationTrack& track, float timeSeconds);
void setPropertyKeyframe(RenderPass& pass, AnimationProperty property, float timeSeconds,
  const glm::vec4* explicitValue = nullptr);
[[nodiscard]] bool removePropertyKeyframe(RenderPass& pass, AnimationProperty property, float timeSeconds,
  float toleranceSeconds = 1.0f / 120.0f);
[[nodiscard]] std::size_t propertyKeyframeIndexNear(const RenderPass& pass, AnimationProperty property,
  float timeSeconds, float toleranceSeconds = 1.0f / 120.0f);
[[nodiscard]] bool propertyHasKeyAt(const RenderPass& pass, AnimationProperty property, float timeSeconds,
  float toleranceSeconds = 1.0f / 120.0f);
[[nodiscard]] RenderPass evaluateRenderPass(const RenderPass& source, float timeSeconds);
[[nodiscard]] RenderStack evaluateRenderStack(const RenderStack& source, float timeSeconds);

} // namespace gfxlab
