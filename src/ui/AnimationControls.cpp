#include "ui/AnimationControls.hpp"

#include "app/RenderStack.hpp"

#include <imgui.h>

namespace gfxlab::ui {

void animationKeyControlAt(RenderPass& pass, const AnimationProperty property,
    AnimationTimeline& timeline, const bool valueChanged, const float anchorScreenY) {
  recordPropertyAnimationEdit(pass, property, timeline, valueChanged);
  PropertyAnimationTrack* track = findPropertyTrack(pass, property);
  const bool keyed = propertyHasKeyAt(pass, property, timeline.timeSeconds);
  const bool animated = track != nullptr;
  const ImVec2 nextCursor = ImGui::GetCursorScreenPos();
  const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  ImGui::SetCursorScreenPos(ImVec2(right - 19.0f, anchorScreenY));
  ImGui::PushID(static_cast<int>(property));
  ImGui::InvisibleButton("##animation-key", ImVec2(18.0f, 18.0f));
  const ImVec2 minimum = ImGui::GetItemRectMin();
  const ImVec2 maximum = ImGui::GetItemRectMax();
  const ImVec2 center((minimum.x + maximum.x) * 0.5f, (minimum.y + maximum.y) * 0.5f);
  constexpr float radius = 5.0f;
  const ImVec2 points[] = {{center.x, center.y - radius}, {center.x + radius, center.y},
    {center.x, center.y + radius}, {center.x - radius, center.y}};
  const ImU32 color = keyed ? IM_COL32(245, 187, 72, 255)
    : animated ? IM_COL32(112, 170, 202, 255) : IM_COL32(91, 95, 101, 255);
  ImGui::GetWindowDrawList()->AddConvexPolyFilled(points, 4, color);
  if (!animated) ImGui::GetWindowDrawList()->AddPolyline(points, 4, IM_COL32(155, 158, 164, 255),
    ImDrawFlags_Closed, 1.0f);
  if (ImGui::IsItemClicked()) {
    timeline.playing = false;
    if (keyed) static_cast<void>(removePropertyKeyframe(pass, property, timeline.timeSeconds));
    else {
      const glm::vec4 value = animationPropertyValue(pass, property);
      setPropertyKeyframe(pass, property, timeline.timeSeconds, &value);
    }
  }
  if (ImGui::IsItemHovered()) {
    if (keyed) ImGui::SetTooltip("Keyed here. Click to remove this key.");
    else if (animated) ImGui::SetTooltip("Animated on this pass. Click to key the evaluated value here.");
    else ImGui::SetTooltip("Not animated. Click to create its first property track and key.");
  }
  ImGui::PopID();
  ImGui::SetCursorScreenPos(nextCursor);
}

void animationKeyControl(RenderPass& pass, const AnimationProperty property,
    AnimationTimeline& timeline, const bool valueChanged) {
  animationKeyControlAt(pass, property, timeline, valueChanged, ImGui::GetItemRectMin().y);
}

} // namespace gfxlab::ui
