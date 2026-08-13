#include "ui/PassDifferenceAudit.hpp"

#include "app/Animation.hpp"
#include "app/RenderStack.hpp"
#include "assets/ModelAsset.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <string_view>

namespace gfxlab::ui {
namespace {

bool tracksEqual(const PropertyAnimationTrack* a, const PropertyAnimationTrack* b) {
  if (a == nullptr || b == nullptr) return a == b;
  if (a->interpolation != b->interpolation || a->keyframes.size() != b->keyframes.size()) return false;
  for (std::size_t index = 0; index < a->keyframes.size(); ++index) {
    if (std::abs(a->keyframes[index].timeSeconds - b->keyframes[index].timeSeconds) > 0.000001f ||
        !animationPropertyValuesEqual(a->property, a->keyframes[index].value, b->keyframes[index].value))
      return false;
  }
  return true;
}

std::string valueText(const AnimationProperty property, const glm::vec4& value) {
  const AnimationPropertyInfo& info = animationPropertyInfo(property);
  char text[128]{};
  if (info.kind == AnimationValueKind::Boolean) return value.x >= 0.5f ? "On" : "Off";
  if (info.kind == AnimationValueKind::Enumeration) {
    const int integer = static_cast<int>(std::round(value.x));
    if (const char* label = animationPropertyDiscreteValueLabel(property, integer)) return label;
    std::snprintf(text, sizeof(text), "%d", integer);
  } else if (info.kind == AnimationValueKind::Integer)
    std::snprintf(text, sizeof(text), "%d", static_cast<int>(std::round(value.x)));
  else if (info.components == 1) std::snprintf(text, sizeof(text), "%.4g", value.x);
  else if (info.components == 2) std::snprintf(text, sizeof(text), "[%.4g, %.4g]", value.x, value.y);
  else if (info.components == 3) std::snprintf(text, sizeof(text), "[%.3g, %.3g, %.3g]", value.x, value.y, value.z);
  else std::snprintf(text, sizeof(text), "[%.3g, %.3g, %.3g, %.3g]", value.x, value.y, value.z, value.w);
  return text;
}

void replaceTrack(RenderPass& destination, const RenderPass& source, const AnimationProperty property) {
  destination.animation.tracks.erase(std::remove_if(destination.animation.tracks.begin(),
    destination.animation.tracks.end(), [property](const PropertyAnimationTrack& track) {
      return track.property == property;
    }), destination.animation.tracks.end());
  if (const PropertyAnimationTrack* sourceTrack = findPropertyTrack(source, property))
    destination.animation.tracks.push_back(*sourceTrack);
}

} // namespace

void drawPassDifferenceAudit(bool& open, RenderStack& stack) {
  if (!open) return;
  const float uiScale = ImGui::GetFontSize() / 13.0f;
  ImGui::SetNextWindowSize(ImVec2(820.0f, 580.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(480.0f * uiScale, 300.0f * uiScale),
    ImVec2(FLT_MAX, FLT_MAX));
  if (!ImGui::Begin("Pass difference audit", &open)) { ImGui::End(); return; }
  keepCurrentWindowVisible();
  if (stack.passes().size() < 2) {
    ImGui::TextWrapped("Duplicate a render pass before auditing differences.");
    ImGui::End();
    return;
  }

  static std::size_t referenceIndex = 0;
  referenceIndex = std::min(referenceIndex, stack.passes().size() - 1);
  ImGui::TextDisabled("SELECTED PASS");
  ImGui::SameLine();
  ImGui::TextUnformatted(stack.selected().name.c_str());
  ImGui::TextDisabled("REFERENCE");
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180.0f);
  if (ImGui::BeginCombo("##audit-reference", stack.passes()[referenceIndex].name.c_str())) {
    for (std::size_t index = 0; index < stack.passes().size(); ++index)
      if (ImGui::Selectable(stack.passes()[index].name.c_str(), referenceIndex == index)) referenceIndex = index;
    ImGui::EndCombo();
  }
  static bool animatedOnly = false;
  ImGui::Checkbox("Animated differences only", &animatedOnly);
  ImGui::TextWrapped("Only authored disagreements are shown. Match reference copies the reference base value and its property track; if the reference is static, the selected track is removed.");
  ImGui::Separator();

  RenderPass& selected = stack.selected();
  const RenderPass& reference = stack.passes()[referenceIndex];
  const RenderPass selectedEffective = materializeRenderPass(stack, stack.selectedIndex(), 0.0f);
  const RenderPass referenceEffective = materializeRenderPass(stack, referenceIndex, 0.0f);
  int differenceCount = 0;
  std::string_view previousGroup;
  if (ImGui::BeginTable("pass-differences", 5,
      ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_RowBg | ImGuiTableFlags_Resizable |
      ImGuiTableFlags_ScrollY, ImVec2(0.0f, -1.0f))) {
    ImGui::TableSetupColumn("Property", ImGuiTableColumnFlags_WidthStretch, 2.0f);
    ImGui::TableSetupColumn("Reference", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Selected", ImGuiTableColumnFlags_WidthStretch, 1.0f);
    ImGui::TableSetupColumn("Animation", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    ImGui::TableSetupColumn("", ImGuiTableColumnFlags_WidthFixed, 118.0f);
    ImGui::TableHeadersRow();
    for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
      const AnimationProperty property = static_cast<AnimationProperty>(index);
      const AnimationPropertyInfo& info = animationPropertyInfo(property);
      const glm::vec4 selectedValue = animationPropertyValue(selectedEffective, property);
      const glm::vec4 referenceValue = animationPropertyValue(referenceEffective, property);
      const PropertyAnimationTrack* selectedTrack = findPropertyTrack(selected, property);
      const PropertyAnimationTrack* referenceTrack = findPropertyTrack(reference, property);
      const bool trackDifference = !tracksEqual(selectedTrack, referenceTrack);
      const bool valueDifference = !animationPropertyValuesEqual(property, selectedValue, referenceValue);
      if ((!valueDifference && !trackDifference) || (animatedOnly && !trackDifference)) continue;
      if (info.group != previousGroup) {
        ImGui::TableNextRow();
        ImGui::TableSetColumnIndex(0);
        ImGui::TextDisabled("%s", info.group.data());
        previousGroup = info.group;
      }
      ++differenceCount;
      ImGui::PushID(index);
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted(info.label.data());
      if (!animationPropertyIsPassLocal(property)) {
        ImGui::SameLine();
        ImGui::TextDisabled(findRenderPassOverride(selected, property) != nullptr ? "override" : "inherited");
      }
      if (info.behavior == AnimationBehavior::NotAnimatable) { ImGui::SameLine(); ImGui::TextDisabled("fixed"); }
      ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(valueText(property, referenceValue).c_str());
      ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(valueText(property, selectedValue).c_str());
      ImGui::TableSetColumnIndex(3);
      if (selectedTrack != nullptr && referenceTrack != nullptr)
        ImGui::Text("%zu / %zu keys", referenceTrack->keyframes.size(), selectedTrack->keyframes.size());
      else if (selectedTrack != nullptr) ImGui::Text("static / %zu keys", selectedTrack->keyframes.size());
      else if (referenceTrack != nullptr) ImGui::Text("%zu keys / static", referenceTrack->keyframes.size());
      else ImGui::TextDisabled("Static");
      ImGui::TableSetColumnIndex(4);
      if (ImGui::SmallButton("Match reference")) {
        if (animationPropertyIsPassLocal(property)) {
          setAnimationPropertyValue(selected, property, referenceValue);
        } else if (const PropertyOverride* referenceOverride = findRenderPassOverride(reference, property)) {
          setRenderPassOverride(selected, property, referenceOverride->value);
        } else {
          static_cast<void>(clearRenderPassOverride(selected, property));
        }
        replaceTrack(selected, reference, property);
      }
      ImGui::PopID();
    }
    const std::uint64_t selectedTextureHash = selectedEffective.importedTexture != nullptr
      ? selectedEffective.importedTexture->contentHash : 0;
    const std::uint64_t referenceTextureHash = referenceEffective.importedTexture != nullptr
      ? referenceEffective.importedTexture->contentHash : 0;
    if (!animatedOnly && selectedTextureHash != referenceTextureHash) {
      ++differenceCount;
      ImGui::PushID("imported-texture-resource");
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0); ImGui::TextUnformatted("Imported texture asset");
      ImGui::TableSetColumnIndex(1); ImGui::TextUnformatted(referenceEffective.importedTexture != nullptr
        ? referenceEffective.importedTexture->name.c_str() : "None");
      ImGui::TableSetColumnIndex(2); ImGui::TextUnformatted(selectedEffective.importedTexture != nullptr
        ? selectedEffective.importedTexture->name.c_str() : "None");
      ImGui::TableSetColumnIndex(3); ImGui::TextDisabled("Resource");
      ImGui::TableSetColumnIndex(4);
      if (ImGui::SmallButton("Match reference")) {
        selected.importedTextureOverride = reference.importedTextureOverride;
        selected.importedTexture = reference.importedTextureOverride ? reference.importedTexture : nullptr;
      }
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  if (differenceCount == 0) {
    ImGui::SetCursorPosY(105.0f);
    ImGui::TextDisabled("No authored differences under the current filter.");
  }
  ImGui::End();
}

} // namespace gfxlab::ui
