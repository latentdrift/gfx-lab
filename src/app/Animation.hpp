#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <vector>

namespace gfxlab {

struct RenderPass;
class RenderStack;

enum class KeyframeInterpolation { Step, Linear, SmoothStep };

struct PassAnimationValues {
  glm::vec3 modelTranslation{0.0f};
  float modelScale = 1.0f;
  float normalInflation = 0.0f;
  glm::vec2 uvOffset{0.0f};
  glm::vec2 uvScale{1.0f};
  float cameraYaw = 0.0f;
  float cameraPitch = 0.0f;
  float cameraDistance = 0.0f;
  float fieldOfViewOffset = 0.0f;

  float compositeGain = 1.0f;
  float compositeBias = 0.0f;
  float compositeOpacity = 1.0f;

  float vertexQuantization = 0.0f;
  float normalStrength = 1.0f;
  float ambient = 0.22f;
  float lightAzimuth = 34.0f;
  float lightElevation = 52.0f;
  float shininess = 32.0f;
  float depthCueStart = 3.0f;
  float depthCueEnd = 7.0f;
  glm::vec3 farColor{0.12f, 0.16f, 0.22f};
  float fogStart = 3.0f;
  float fogEnd = 7.0f;
  glm::vec4 primitiveColor{1.0f};
  glm::vec4 environmentColor{0.18f, 0.24f, 0.30f, 1.0f};
  float alphaThreshold = 0.5f;
};

struct PassKeyframe {
  float timeSeconds = 0.0f;
  PassAnimationValues values;
};

struct PassAnimationTrack {
  bool enabled = true;
  KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
  std::vector<PassKeyframe> keyframes;
};

struct AnimationTimeline {
  float timeSeconds = 0.0f;
  float durationSeconds = 4.0f;
  float playbackRate = 1.0f;
  bool playing = false;
  bool loop = true;

  void advance(float deltaSeconds);
};

[[nodiscard]] PassAnimationValues captureAnimationValues(const RenderPass& pass);
void applyAnimationValues(RenderPass& pass, const PassAnimationValues& values);
void setPassKeyframe(RenderPass& pass, float timeSeconds);
[[nodiscard]] bool removePassKeyframe(RenderPass& pass, float timeSeconds, float toleranceSeconds = 1.0f / 120.0f);
[[nodiscard]] std::size_t keyframeIndexNear(const RenderPass& pass, float timeSeconds,
  float toleranceSeconds = 1.0f / 120.0f);
[[nodiscard]] RenderStack evaluateRenderStack(const RenderStack& source, float timeSeconds);

} // namespace gfxlab
