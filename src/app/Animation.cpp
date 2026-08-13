#include "app/Animation.hpp"

#include "app/RenderStack.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace gfxlab {
namespace {

constexpr std::array<AnimationPropertyInfo, static_cast<std::size_t>(AnimationProperty::Count)> propertyInfo = {{
  {"model_translation", "Model translation", "Geometry perturbation", 3},
  {"model_scale", "Model scale", "Geometry perturbation", 1},
  {"normal_inflation", "Normal inflation", "Geometry perturbation", 1},
  {"uv_offset", "UV offset", "Sampling perturbation", 2},
  {"uv_scale", "UV scale", "Sampling perturbation", 2},
  {"camera_yaw", "Camera yaw offset", "View perturbation", 1},
  {"camera_pitch", "Camera pitch offset", "View perturbation", 1},
  {"camera_distance", "Camera distance offset", "View perturbation", 1},
  {"field_of_view_offset", "Field-of-view offset", "View perturbation", 1},
  {"composite_gain", "Composite gain", "Composite", 1},
  {"composite_bias", "Composite bias", "Composite", 1},
  {"composite_opacity", "Composite opacity", "Composite", 1},
  {"vertex_quantization", "Vertex position precision", "Geometry", 1},
  {"normal_strength", "Normal-map strength", "Surface", 1},
  {"ambient", "Ambient term", "Lighting", 1},
  {"light_azimuth", "Light azimuth", "Lighting", 1},
  {"light_elevation", "Light elevation", "Lighting", 1},
  {"shininess", "Specular exponent", "Lighting", 1},
  {"depth_cue_start", "Depth cue start", "Lighting", 1},
  {"depth_cue_end", "Depth cue end", "Lighting", 1},
  {"far_color", "Depth cue far color", "Lighting", 3},
  {"fog_start", "Fog start", "Post", 1},
  {"fog_end", "Fog end", "Post", 1},
  {"primitive_color", "N64 primitive color", "N64 surface", 4},
  {"environment_color", "N64 environment color", "N64 surface", 4},
  {"alpha_threshold", "N64 alpha threshold", "N64 surface", 1},
}};

std::size_t nearbyIndex(const std::vector<PropertyKeyframe>& keyframes, const float timeSeconds,
    const float toleranceSeconds) {
  std::size_t closest = std::numeric_limits<std::size_t>::max();
  float distance = toleranceSeconds;
  for (std::size_t index = 0; index < keyframes.size(); ++index) {
    const float candidate = std::abs(keyframes[index].timeSeconds - timeSeconds);
    if (candidate <= distance) {
      closest = index;
      distance = candidate;
    }
  }
  return closest;
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

const AnimationPropertyInfo& animationPropertyInfo(const AnimationProperty property) {
  return propertyInfo[std::min(static_cast<std::size_t>(property), propertyInfo.size() - 1)];
}

glm::vec4 animationPropertyValue(const RenderPass& pass, const AnimationProperty property) {
  switch (property) {
  case AnimationProperty::ModelTranslation: return glm::vec4(pass.perturbation.modelTranslation, 0.0f);
  case AnimationProperty::ModelScale: return glm::vec4(pass.perturbation.modelScale);
  case AnimationProperty::NormalInflation: return glm::vec4(pass.perturbation.normalInflation);
  case AnimationProperty::UvOffset: return glm::vec4(pass.perturbation.uvOffset, 0.0f, 0.0f);
  case AnimationProperty::UvScale: return glm::vec4(pass.perturbation.uvScale, 0.0f, 0.0f);
  case AnimationProperty::CameraYaw: return glm::vec4(pass.perturbation.cameraYaw);
  case AnimationProperty::CameraPitch: return glm::vec4(pass.perturbation.cameraPitch);
  case AnimationProperty::CameraDistance: return glm::vec4(pass.perturbation.cameraDistance);
  case AnimationProperty::FieldOfViewOffset: return glm::vec4(pass.perturbation.fieldOfView);
  case AnimationProperty::CompositeGain: return glm::vec4(pass.composite.gain);
  case AnimationProperty::CompositeBias: return glm::vec4(pass.composite.bias);
  case AnimationProperty::CompositeOpacity: return glm::vec4(pass.composite.opacity);
  case AnimationProperty::VertexQuantization: return glm::vec4(pass.renderer.geometry.vertexQuantization);
  case AnimationProperty::NormalStrength: return glm::vec4(pass.renderer.surface.normalStrength);
  case AnimationProperty::Ambient: return glm::vec4(pass.renderer.lighting.ambient);
  case AnimationProperty::LightAzimuth: return glm::vec4(pass.renderer.lighting.azimuth);
  case AnimationProperty::LightElevation: return glm::vec4(pass.renderer.lighting.elevation);
  case AnimationProperty::Shininess: return glm::vec4(pass.renderer.lighting.shininess);
  case AnimationProperty::DepthCueStart: return glm::vec4(pass.renderer.lighting.depthCueStart);
  case AnimationProperty::DepthCueEnd: return glm::vec4(pass.renderer.lighting.depthCueEnd);
  case AnimationProperty::FarColor: return glm::vec4(pass.renderer.lighting.farColor, 1.0f);
  case AnimationProperty::FogStart: return glm::vec4(pass.renderer.post.fogStart);
  case AnimationProperty::FogEnd: return glm::vec4(pass.renderer.post.fogEnd);
  case AnimationProperty::PrimitiveColor: return pass.renderer.n64.primitiveColor;
  case AnimationProperty::EnvironmentColor: return pass.renderer.n64.environmentColor;
  case AnimationProperty::AlphaThreshold: return glm::vec4(pass.renderer.n64.alphaThreshold);
  case AnimationProperty::Count: break;
  }
  return glm::vec4(0.0f);
}

void setAnimationPropertyValue(RenderPass& pass, const AnimationProperty property, const glm::vec4& value) {
  switch (property) {
  case AnimationProperty::ModelTranslation: pass.perturbation.modelTranslation = glm::vec3(value); break;
  case AnimationProperty::ModelScale: pass.perturbation.modelScale = value.x; break;
  case AnimationProperty::NormalInflation: pass.perturbation.normalInflation = value.x; break;
  case AnimationProperty::UvOffset: pass.perturbation.uvOffset = glm::vec2(value); break;
  case AnimationProperty::UvScale: pass.perturbation.uvScale = glm::vec2(value); break;
  case AnimationProperty::CameraYaw: pass.perturbation.cameraYaw = value.x; break;
  case AnimationProperty::CameraPitch: pass.perturbation.cameraPitch = value.x; break;
  case AnimationProperty::CameraDistance: pass.perturbation.cameraDistance = value.x; break;
  case AnimationProperty::FieldOfViewOffset: pass.perturbation.fieldOfView = value.x; break;
  case AnimationProperty::CompositeGain: pass.composite.gain = value.x; break;
  case AnimationProperty::CompositeBias: pass.composite.bias = value.x; break;
  case AnimationProperty::CompositeOpacity: pass.composite.opacity = value.x; break;
  case AnimationProperty::VertexQuantization: pass.renderer.geometry.vertexQuantization = value.x; break;
  case AnimationProperty::NormalStrength: pass.renderer.surface.normalStrength = value.x; break;
  case AnimationProperty::Ambient: pass.renderer.lighting.ambient = value.x; break;
  case AnimationProperty::LightAzimuth: pass.renderer.lighting.azimuth = value.x; break;
  case AnimationProperty::LightElevation: pass.renderer.lighting.elevation = value.x; break;
  case AnimationProperty::Shininess: pass.renderer.lighting.shininess = value.x; break;
  case AnimationProperty::DepthCueStart: pass.renderer.lighting.depthCueStart = value.x; break;
  case AnimationProperty::DepthCueEnd: pass.renderer.lighting.depthCueEnd = value.x; break;
  case AnimationProperty::FarColor: pass.renderer.lighting.farColor = glm::vec3(value); break;
  case AnimationProperty::FogStart: pass.renderer.post.fogStart = value.x; break;
  case AnimationProperty::FogEnd: pass.renderer.post.fogEnd = value.x; break;
  case AnimationProperty::PrimitiveColor: pass.renderer.n64.primitiveColor = value; break;
  case AnimationProperty::EnvironmentColor: pass.renderer.n64.environmentColor = value; break;
  case AnimationProperty::AlphaThreshold: pass.renderer.n64.alphaThreshold = value.x; break;
  case AnimationProperty::Count: break;
  }
}

PropertyAnimationTrack* findPropertyTrack(RenderPass& pass, const AnimationProperty property) {
  const auto track = std::find_if(pass.animation.tracks.begin(), pass.animation.tracks.end(),
    [property](const PropertyAnimationTrack& candidate) { return candidate.property == property; });
  return track == pass.animation.tracks.end() ? nullptr : &*track;
}

const PropertyAnimationTrack* findPropertyTrack(const RenderPass& pass, const AnimationProperty property) {
  const auto track = std::find_if(pass.animation.tracks.begin(), pass.animation.tracks.end(),
    [property](const PropertyAnimationTrack& candidate) { return candidate.property == property; });
  return track == pass.animation.tracks.end() ? nullptr : &*track;
}

glm::vec4 samplePropertyTrack(const PropertyAnimationTrack& track, const float timeSeconds) {
  if (track.keyframes.empty()) return glm::vec4(0.0f);
  if (track.keyframes.size() == 1 || timeSeconds <= track.keyframes.front().timeSeconds)
    return track.keyframes.front().value;
  if (timeSeconds >= track.keyframes.back().timeSeconds) return track.keyframes.back().value;
  const auto upper = std::upper_bound(track.keyframes.begin(), track.keyframes.end(), timeSeconds,
    [](const float time, const PropertyKeyframe& keyframe) { return time < keyframe.timeSeconds; });
  const PropertyKeyframe& b = *upper;
  const PropertyKeyframe& a = *(upper - 1);
  float amount = (timeSeconds - a.timeSeconds) / std::max(b.timeSeconds - a.timeSeconds, 0.00001f);
  if (track.interpolation == KeyframeInterpolation::Step) amount = 0.0f;
  else if (track.interpolation == KeyframeInterpolation::SmoothStep)
    amount = amount * amount * (3.0f - 2.0f * amount);
  return glm::mix(a.value, b.value, amount);
}

void setPropertyKeyframe(RenderPass& pass, const AnimationProperty property, const float timeSeconds,
    const glm::vec4* explicitValue) {
  PropertyAnimationTrack* track = findPropertyTrack(pass, property);
  if (track == nullptr) {
    pass.animation.tracks.push_back({property, KeyframeInterpolation::Linear, {}});
    track = &pass.animation.tracks.back();
  }
  const glm::vec4 value = explicitValue == nullptr ? animationPropertyValue(pass, property) : *explicitValue;
  const std::size_t nearby = nearbyIndex(track->keyframes, timeSeconds, 1.0f / 120.0f);
  if (nearby == std::numeric_limits<std::size_t>::max()) track->keyframes.push_back({timeSeconds, value});
  else track->keyframes[nearby] = {timeSeconds, value};
  std::sort(track->keyframes.begin(), track->keyframes.end(),
    [](const PropertyKeyframe& a, const PropertyKeyframe& b) { return a.timeSeconds < b.timeSeconds; });
}

bool removePropertyKeyframe(RenderPass& pass, const AnimationProperty property, const float timeSeconds,
    const float toleranceSeconds) {
  PropertyAnimationTrack* track = findPropertyTrack(pass, property);
  if (track == nullptr) return false;
  const std::size_t nearby = nearbyIndex(track->keyframes, timeSeconds, toleranceSeconds);
  if (nearby == std::numeric_limits<std::size_t>::max()) return false;
  track->keyframes.erase(track->keyframes.begin() + static_cast<std::ptrdiff_t>(nearby));
  if (track->keyframes.empty()) {
    pass.animation.tracks.erase(std::remove_if(pass.animation.tracks.begin(), pass.animation.tracks.end(),
      [property](const PropertyAnimationTrack& candidate) { return candidate.property == property; }),
      pass.animation.tracks.end());
  }
  return true;
}

std::size_t propertyKeyframeIndexNear(const RenderPass& pass, const AnimationProperty property,
    const float timeSeconds, const float toleranceSeconds) {
  const PropertyAnimationTrack* track = findPropertyTrack(pass, property);
  return track == nullptr ? std::numeric_limits<std::size_t>::max()
    : nearbyIndex(track->keyframes, timeSeconds, toleranceSeconds);
}

bool propertyHasKeyAt(const RenderPass& pass, const AnimationProperty property, const float timeSeconds,
    const float toleranceSeconds) {
  return propertyKeyframeIndexNear(pass, property, timeSeconds, toleranceSeconds) !=
    std::numeric_limits<std::size_t>::max();
}

RenderPass evaluateRenderPass(const RenderPass& source, const float timeSeconds) {
  RenderPass evaluated = source;
  if (!source.animation.enabled) return evaluated;
  for (const PropertyAnimationTrack& track : source.animation.tracks) {
    if (!track.keyframes.empty()) setAnimationPropertyValue(evaluated, track.property, samplePropertyTrack(track, timeSeconds));
  }
  return evaluated;
}

RenderStack evaluateRenderStack(const RenderStack& source, const float timeSeconds) {
  RenderStack evaluated = source;
  for (std::size_t index = 0; index < source.passes().size(); ++index)
    evaluated.passes()[index] = evaluateRenderPass(source.passes()[index], timeSeconds);
  return evaluated;
}

} // namespace gfxlab
