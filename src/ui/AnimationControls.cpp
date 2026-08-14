#include "ui/AnimationControls.hpp"

#include "app/RenderStack.hpp"

#include <imgui.h>

#include <cmath>
#include <cstdio>
#include <optional>
#include <string>

namespace gfxlab::ui {
namespace {

struct PropertyInheritanceContext {
  RenderPass* authored = nullptr;
  RenderPass inherited;
};

std::optional<PropertyInheritanceContext> inheritanceContext;

std::string propertyValueText(const AnimationProperty property, const glm::vec4& value) {
  const AnimationPropertyInfo& info = animationPropertyInfo(property);
  char text[128]{};
  if (info.kind == AnimationValueKind::Boolean) return value.x >= 0.5f ? "On" : "Off";
  if (info.kind == AnimationValueKind::Enumeration) {
    const int integer = static_cast<int>(std::round(value.x));
    if (const char* label = animationPropertyDiscreteValueLabel(property, integer)) return label;
    std::snprintf(text, sizeof(text), "%d", integer);
  } else if (info.kind == AnimationValueKind::Integer) {
    std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::round(value.x)));
  } else if (info.kind == AnimationValueKind::Angle) {
    const float degrees = info.maximum > 7.0f ? value.x : value.x * 57.2957795f;
    std::snprintf(text, sizeof(text), "%.3g deg", degrees);
  } else if (info.components == 1) {
    std::snprintf(text, sizeof(text), "%.5g", value.x);
  } else if (info.components == 2) {
    std::snprintf(text, sizeof(text), "[%.4g, %.4g]", value.x, value.y);
  } else if (info.components == 3) {
    std::snprintf(text, sizeof(text), "[%.4g, %.4g, %.4g]", value.x, value.y, value.z);
  } else {
    std::snprintf(text, sizeof(text), "[%.4g, %.4g, %.4g, %.4g]", value.x, value.y, value.z, value.w);
  }
  return text;
}

} // namespace

void setPropertyInheritanceContext(RenderPass* authoredPass, const RenderPass& inheritedPass) {
  inheritanceContext = PropertyInheritanceContext{authoredPass, inheritedPass};
}

void clearPropertyInheritanceContext() { inheritanceContext.reset(); }

void animationKeyControlAt(RenderPass& pass, const AnimationProperty property,
    AnimationTimeline& timeline, const bool valueChanged, const float anchorScreenY) {
  recordPropertyAnimationEdit(pass, property, timeline, valueChanged);
  PropertyAnimationTrack* track = findPropertyTrack(pass, property);
  const bool keyed = propertyHasKeyAt(pass, property, timeline.timeSeconds);
  const bool animated = track != nullptr;
  const ImVec2 nextCursor = ImGui::GetCursorScreenPos();
  const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
  ImGui::PushID(static_cast<int>(property));
  if (inheritanceContext.has_value() && inheritanceContext->authored != nullptr &&
      !animationPropertyIsPassLocal(property)) {
    RenderPass& authored = *inheritanceContext->authored;
    const bool overridden = findRenderPassOverride(authored, property) != nullptr;
    const bool locallyAnimated = findPropertyTrack(authored, property) != nullptr;
    ImGui::SetCursorScreenPos(ImVec2(right - 94.0f, anchorScreenY));
    if (overridden) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.31f, 0.24f, 0.14f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.42f, 0.31f, 0.16f, 1.0f));
      if (ImGui::Button("override##inheritance", ImVec2(70.0f, 18.0f))) {
        static_cast<void>(clearRenderPassOverride(authored, property));
        if (!locallyAnimated) setAnimationPropertyValue(pass, property,
          animationPropertyValue(inheritanceContext->inherited, property));
      }
      ImGui::PopStyleColor(2);
    } else {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.13f, 0.18f, 0.19f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::Button("inherited##inheritance", ImVec2(70.0f, 18.0f));
      ImGui::PopStyleColor(2);
    }
    if (ImGui::IsItemHovered()) {
      const glm::vec4 inheritedValue = animationPropertyValue(inheritanceContext->inherited, property);
      const glm::vec4 effectiveValue = animationPropertyValue(pass, property);
      ImGui::BeginTooltip();
      ImGui::TextUnformatted(overridden ? "Locally overridden" : "Inherited from Scene Defaults");
      ImGui::Separator();
      ImGui::Text("Scene Defaults: %s", propertyValueText(property, inheritedValue).c_str());
      ImGui::Text("Effective now: %s", propertyValueText(property, effectiveValue).c_str());
      if (locallyAnimated) ImGui::TextDisabled("A local animation track takes final precedence.");
      else if (overridden) ImGui::TextDisabled("Click to remove the override and resume inheritance.");
      ImGui::EndTooltip();
    }
  }
  ImGui::SetCursorScreenPos(ImVec2(right - 19.0f, anchorScreenY));
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
  // Newer ImGui versions reject a cursor restore that can affect content bounds unless an item is
  // submitted at the restored position. A zero-size item preserves the surrounding layout.
  ImGui::Dummy(ImVec2(0.0f, 0.0f));
}

void animationKeyControl(RenderPass& pass, const AnimationProperty property,
    AnimationTimeline& timeline, const bool valueChanged) {
  animationKeyControlAt(pass, property, timeline, valueChanged, ImGui::GetItemRectMin().y);
}

} // namespace gfxlab::ui
