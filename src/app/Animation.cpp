#include "app/Animation.hpp"

#include "app/RenderStack.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

namespace gfxlab {
namespace {

float interpolate(const float a, const float b, const float amount) {
  return a + (b - a) * amount;
}

PassAnimationValues interpolateValues(const PassAnimationValues& a, const PassAnimationValues& b,
    const float amount) {
  PassAnimationValues result;
  result.modelTranslation = glm::mix(a.modelTranslation, b.modelTranslation, amount);
  result.modelScale = interpolate(a.modelScale, b.modelScale, amount);
  result.normalInflation = interpolate(a.normalInflation, b.normalInflation, amount);
  result.uvOffset = glm::mix(a.uvOffset, b.uvOffset, amount);
  result.uvScale = glm::mix(a.uvScale, b.uvScale, amount);
  result.cameraYaw = interpolate(a.cameraYaw, b.cameraYaw, amount);
  result.cameraPitch = interpolate(a.cameraPitch, b.cameraPitch, amount);
  result.cameraDistance = interpolate(a.cameraDistance, b.cameraDistance, amount);
  result.fieldOfViewOffset = interpolate(a.fieldOfViewOffset, b.fieldOfViewOffset, amount);
  result.compositeGain = interpolate(a.compositeGain, b.compositeGain, amount);
  result.compositeBias = interpolate(a.compositeBias, b.compositeBias, amount);
  result.compositeOpacity = interpolate(a.compositeOpacity, b.compositeOpacity, amount);
  result.vertexQuantization = interpolate(a.vertexQuantization, b.vertexQuantization, amount);
  result.normalStrength = interpolate(a.normalStrength, b.normalStrength, amount);
  result.ambient = interpolate(a.ambient, b.ambient, amount);
  result.lightAzimuth = interpolate(a.lightAzimuth, b.lightAzimuth, amount);
  result.lightElevation = interpolate(a.lightElevation, b.lightElevation, amount);
  result.shininess = interpolate(a.shininess, b.shininess, amount);
  result.depthCueStart = interpolate(a.depthCueStart, b.depthCueStart, amount);
  result.depthCueEnd = interpolate(a.depthCueEnd, b.depthCueEnd, amount);
  result.farColor = glm::mix(a.farColor, b.farColor, amount);
  result.fogStart = interpolate(a.fogStart, b.fogStart, amount);
  result.fogEnd = interpolate(a.fogEnd, b.fogEnd, amount);
  result.primitiveColor = glm::mix(a.primitiveColor, b.primitiveColor, amount);
  result.environmentColor = glm::mix(a.environmentColor, b.environmentColor, amount);
  result.alphaThreshold = interpolate(a.alphaThreshold, b.alphaThreshold, amount);
  return result;
}

PassAnimationValues sampleTrack(const PassAnimationTrack& track, const float timeSeconds) {
  if (track.keyframes.size() == 1 || timeSeconds <= track.keyframes.front().timeSeconds)
    return track.keyframes.front().values;
  if (timeSeconds >= track.keyframes.back().timeSeconds) return track.keyframes.back().values;

  const auto upper = std::upper_bound(track.keyframes.begin(), track.keyframes.end(), timeSeconds,
    [](const float time, const PassKeyframe& keyframe) { return time < keyframe.timeSeconds; });
  const PassKeyframe& b = *upper;
  const PassKeyframe& a = *(upper - 1);
  float amount = (timeSeconds - a.timeSeconds) / std::max(b.timeSeconds - a.timeSeconds, 0.00001f);
  if (track.interpolation == KeyframeInterpolation::Step) amount = 0.0f;
  else if (track.interpolation == KeyframeInterpolation::SmoothStep) amount = amount * amount * (3.0f - 2.0f * amount);
  return interpolateValues(a.values, b.values, amount);
}

} // namespace

void AnimationTimeline::advance(const float deltaSeconds) {
  if (!playing || durationSeconds <= 0.0f) return;
  timeSeconds += deltaSeconds * playbackRate;
  if (loop) {
    timeSeconds = std::fmod(timeSeconds, durationSeconds);
    if (timeSeconds < 0.0f) timeSeconds += durationSeconds;
  } else if (timeSeconds >= durationSeconds) {
    timeSeconds = durationSeconds;
    playing = false;
  } else if (timeSeconds <= 0.0f) {
    timeSeconds = 0.0f;
    playing = false;
  }
}

PassAnimationValues captureAnimationValues(const RenderPass& pass) {
  PassAnimationValues values;
  values.modelTranslation = pass.perturbation.modelTranslation;
  values.modelScale = pass.perturbation.modelScale;
  values.normalInflation = pass.perturbation.normalInflation;
  values.uvOffset = pass.perturbation.uvOffset;
  values.uvScale = pass.perturbation.uvScale;
  values.cameraYaw = pass.perturbation.cameraYaw;
  values.cameraPitch = pass.perturbation.cameraPitch;
  values.cameraDistance = pass.perturbation.cameraDistance;
  values.fieldOfViewOffset = pass.perturbation.fieldOfView;
  values.compositeGain = pass.composite.gain;
  values.compositeBias = pass.composite.bias;
  values.compositeOpacity = pass.composite.opacity;
  values.vertexQuantization = pass.renderer.geometry.vertexQuantization;
  values.normalStrength = pass.renderer.surface.normalStrength;
  values.ambient = pass.renderer.lighting.ambient;
  values.lightAzimuth = pass.renderer.lighting.azimuth;
  values.lightElevation = pass.renderer.lighting.elevation;
  values.shininess = pass.renderer.lighting.shininess;
  values.depthCueStart = pass.renderer.lighting.depthCueStart;
  values.depthCueEnd = pass.renderer.lighting.depthCueEnd;
  values.farColor = pass.renderer.lighting.farColor;
  values.fogStart = pass.renderer.post.fogStart;
  values.fogEnd = pass.renderer.post.fogEnd;
  values.primitiveColor = pass.renderer.n64.primitiveColor;
  values.environmentColor = pass.renderer.n64.environmentColor;
  values.alphaThreshold = pass.renderer.n64.alphaThreshold;
  return values;
}

void applyAnimationValues(RenderPass& pass, const PassAnimationValues& values) {
  pass.perturbation.modelTranslation = values.modelTranslation;
  pass.perturbation.modelScale = values.modelScale;
  pass.perturbation.normalInflation = values.normalInflation;
  pass.perturbation.uvOffset = values.uvOffset;
  pass.perturbation.uvScale = values.uvScale;
  pass.perturbation.cameraYaw = values.cameraYaw;
  pass.perturbation.cameraPitch = values.cameraPitch;
  pass.perturbation.cameraDistance = values.cameraDistance;
  pass.perturbation.fieldOfView = values.fieldOfViewOffset;
  pass.composite.gain = values.compositeGain;
  pass.composite.bias = values.compositeBias;
  pass.composite.opacity = values.compositeOpacity;
  pass.renderer.geometry.vertexQuantization = values.vertexQuantization;
  pass.renderer.surface.normalStrength = values.normalStrength;
  pass.renderer.lighting.ambient = values.ambient;
  pass.renderer.lighting.azimuth = values.lightAzimuth;
  pass.renderer.lighting.elevation = values.lightElevation;
  pass.renderer.lighting.shininess = values.shininess;
  pass.renderer.lighting.depthCueStart = values.depthCueStart;
  pass.renderer.lighting.depthCueEnd = values.depthCueEnd;
  pass.renderer.lighting.farColor = values.farColor;
  pass.renderer.post.fogStart = values.fogStart;
  pass.renderer.post.fogEnd = values.fogEnd;
  pass.renderer.n64.primitiveColor = values.primitiveColor;
  pass.renderer.n64.environmentColor = values.environmentColor;
  pass.renderer.n64.alphaThreshold = values.alphaThreshold;
}

void setPassKeyframe(RenderPass& pass, const float timeSeconds) {
  const std::size_t nearby = keyframeIndexNear(pass, timeSeconds);
  if (nearby != std::numeric_limits<std::size_t>::max()) {
    pass.animation.keyframes[nearby] = {timeSeconds, captureAnimationValues(pass)};
  } else {
    pass.animation.keyframes.push_back({timeSeconds, captureAnimationValues(pass)});
  }
  std::sort(pass.animation.keyframes.begin(), pass.animation.keyframes.end(),
    [](const PassKeyframe& a, const PassKeyframe& b) { return a.timeSeconds < b.timeSeconds; });
}

bool removePassKeyframe(RenderPass& pass, const float timeSeconds, const float toleranceSeconds) {
  const std::size_t nearby = keyframeIndexNear(pass, timeSeconds, toleranceSeconds);
  if (nearby == std::numeric_limits<std::size_t>::max()) return false;
  pass.animation.keyframes.erase(pass.animation.keyframes.begin() + static_cast<std::ptrdiff_t>(nearby));
  return true;
}

std::size_t keyframeIndexNear(const RenderPass& pass, const float timeSeconds, const float toleranceSeconds) {
  std::size_t closest = std::numeric_limits<std::size_t>::max();
  float distance = toleranceSeconds;
  for (std::size_t index = 0; index < pass.animation.keyframes.size(); ++index) {
    const float candidate = std::abs(pass.animation.keyframes[index].timeSeconds - timeSeconds);
    if (candidate <= distance) {
      closest = index;
      distance = candidate;
    }
  }
  return closest;
}

RenderStack evaluateRenderStack(const RenderStack& source, const float timeSeconds) {
  RenderStack evaluated = source;
  for (RenderPass& pass : evaluated.passes()) {
    if (pass.animation.enabled && !pass.animation.keyframes.empty())
      applyAnimationValues(pass, sampleTrack(pass.animation, timeSeconds));
  }
  return evaluated;
}

} // namespace gfxlab
