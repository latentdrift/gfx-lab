#include "app/RenderOperationState.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <array>
#include <cmath>

namespace gfxlab {

PassCameraMatrices buildPassCamera(const CameraOrbit& camera, const RendererState& state,
    const PassPerturbation& perturbation, const float aspect) {
  CameraOrbit adjusted = camera;
  adjusted.yaw += perturbation.cameraYaw;
  adjusted.pitch = std::clamp(adjusted.pitch + perturbation.cameraPitch, -1.45f, 1.45f);
  adjusted.distance = std::clamp(adjusted.distance + perturbation.cameraDistance, 1.4f, 14.0f);
  const glm::vec3 unshiftedEye = adjusted.eye();
  const glm::vec3 viewForward = glm::normalize(adjusted.target - unshiftedEye);
  const glm::vec3 cameraRight = glm::normalize(glm::cross(viewForward, glm::vec3(0.0f, 1.0f, 0.0f)));
  const glm::vec3 eyeOffset = cameraRight * perturbation.cameraLateral;
  PassCameraMatrices result;
  result.eye = unshiftedEye + eyeOffset;
  result.view = glm::lookAt(result.eye, adjusted.target + eyeOffset, glm::vec3(0.0f, 1.0f, 0.0f));
  if (state.camera.orthographic) {
    const float halfHeight = state.camera.orthographicSize * 0.5f;
    result.projection = glm::ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight,
      state.camera.nearPlane, 100.0f);
  } else {
    const float halfHeight = state.camera.nearPlane * std::tan(glm::radians(
      std::clamp(state.camera.fieldOfView + perturbation.fieldOfView, 5.0f, 150.0f)) * 0.5f);
    const float halfWidth = halfHeight * aspect;
    const float convergence = std::max(perturbation.stereoConvergence, state.camera.nearPlane + 0.001f);
    const float shift = -perturbation.cameraLateral * state.camera.nearPlane / convergence;
    result.projection = glm::frustum(-halfWidth + shift, halfWidth + shift, -halfHeight, halfHeight,
      state.camera.nearPlane, 100.0f);
  }
  return result;
}

bool animationPropertyIsPassLocal(const AnimationProperty property) {
  switch (property) {
  case AnimationProperty::PassEnabled:
  case AnimationProperty::PassOutput:
  case AnimationProperty::CompositeGain:
  case AnimationProperty::CompositeBias:
  case AnimationProperty::CompositeOpacity:
  case AnimationProperty::CompositeOperation:
  case AnimationProperty::CompositeSourceA:
  case AnimationProperty::CompositeSourceB:
  case AnimationProperty::CompositeSourceAPass:
  case AnimationProperty::CompositeSourceBPass:
  case AnimationProperty::CompositeInterpretationA:
  case AnimationProperty::CompositeInterpretationB:
  case AnimationProperty::CompositeFixedColor:
  case AnimationProperty::CompositeBitDepth:
  case AnimationProperty::CompositeHistoryDecay:
  case AnimationProperty::CompositeHistoryUvOffset:
  case AnimationProperty::CompositeHistoryUvScale:
  case AnimationProperty::CompositeColorSpace:
  case AnimationProperty::CompositeRange:
  case AnimationProperty::CompositeMask:
  case AnimationProperty::CompositeMaskInverted:
  case AnimationProperty::StereoAnalysisMode:
  case AnimationProperty::StereoMaximumDisparity:
  case AnimationProperty::StereoOcclusionTolerance: return true;
  default: return false;
  }
}

const PropertyOverride* findRenderPassOverride(const RenderPass& pass,
    const AnimationProperty property) {
  const auto found = std::find_if(pass.overrides.begin(), pass.overrides.end(),
    [property](const PropertyOverride& candidate) { return candidate.property == property; });
  return found == pass.overrides.end() ? nullptr : &*found;
}

bool clearRenderPassOverride(RenderPass& pass, const AnimationProperty property) {
  const auto before = pass.overrides.size();
  std::erase_if(pass.overrides,
    [property](const PropertyOverride& candidate) { return candidate.property == property; });
  return pass.overrides.size() != before;
}

const char* relationOperatorLabel(const RelationOperator operation) {
  constexpr std::array<const char*, 20> labels = {
    "Absolute difference", "Signed A - B", "Positive A - B", "Positive B - A", "Multiply", "Screen",
    "Exclusion", "Minimum", "Maximum", "A x (1 - B)", "Centered sum", "Relative A / B",
    "Add", "Average (add + half)", "Subtract", "Reverse subtract", "Quarter-add B", "Signed color offset",
    "Bitwise XOR", "Normal"
  };
  return labels[static_cast<std::size_t>(std::clamp(static_cast<int>(operation), 0,
    static_cast<int>(labels.size()) - 1))];
}

} // namespace gfxlab
