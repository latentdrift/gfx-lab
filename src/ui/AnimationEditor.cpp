#include "ui/AnimationEditor.hpp"

#include "app/Animation.hpp"
#include "app/RenderStack.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string_view>
#include <vector>

namespace gfxlab::ui {
namespace {

struct KeySelection {
  std::size_t pass = std::numeric_limits<std::size_t>::max();
  AnimationProperty property = AnimationProperty::ModelTranslation;
  float time = 0.0f;
};

KeySelection selection;
int propertyToAdd = 0;
constexpr std::size_t globalPassIndex = std::numeric_limits<std::size_t>::max() - 1;

std::vector<AnimationProperty> animatableProperties(const bool globalScope) {
  std::vector<AnimationProperty> properties;
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    if (animationPropertyIsAnimatable(property) && (!globalScope || !animationPropertyIsPassLocal(property)))
      properties.push_back(property);
  }
  return properties;
}

void drawDiamond(ImDrawList* draw, const ImVec2 center, const ImU32 color, const bool selected) {
  const float radius = selected ? 6.0f : 5.0f;
  const ImVec2 points[] = {{center.x, center.y - radius}, {center.x + radius, center.y},
    {center.x, center.y + radius}, {center.x - radius, center.y}};
  draw->AddConvexPolyFilled(points, 4, color);
  if (selected) draw->AddPolyline(points, 4, IM_COL32(255, 255, 255, 245), ImDrawFlags_Closed, 1.0f);
}

bool selected(const std::size_t passIndex, const AnimationProperty property, const float time) {
  return selection.pass == passIndex && selection.property == property && std::abs(selection.time - time) < 0.0001f;
}

void drawTrackRow(AnimationTimeline& timeline, RenderPass& pass, const std::size_t passIndex,
    PropertyAnimationTrack& track, const float labelWidth) {
  const AnimationPropertyInfo& info = animationPropertyInfo(track.property);
  const float rowWidth = ImGui::GetContentRegionAvail().x;
  const float trackWidth = std::max(80.0f, rowWidth - labelWidth);
  ImGui::TextUnformatted(info.label.data());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s — %s", info.group.data(), pass.name.c_str());
  ImGui::SameLine(labelWidth);
  ImGui::InvisibleButton("##track", ImVec2(trackWidth, 19.0f));
  const ImVec2 areaMinimum = ImGui::GetItemRectMin();
  const ImVec2 areaMaximum = ImGui::GetItemRectMax();
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const float centerY = (areaMinimum.y + areaMaximum.y) * 0.5f;
  draw->AddLine(ImVec2(areaMinimum.x, centerY), ImVec2(areaMaximum.x, centerY), IM_COL32(70, 74, 79, 255));
  if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    const float normalized = std::clamp((ImGui::GetIO().MousePos.x - areaMinimum.x) / trackWidth, 0.0f, 1.0f);
    timeline.timeSeconds = normalized * timeline.durationSeconds;
    timeline.playing = false;
  }
  for (const PropertyKeyframe& key : track.keyframes) {
    const float normalized = timeline.durationSeconds > 0.0f ? key.timeSeconds / timeline.durationSeconds : 0.0f;
    const ImVec2 center(areaMinimum.x + std::clamp(normalized, 0.0f, 1.0f) * trackWidth, centerY);
    const bool isSelected = selected(passIndex, track.property, key.timeSeconds);
    const ImU32 color = std::abs(key.timeSeconds - timeline.timeSeconds) <= 0.0001f
      ? IM_COL32(245, 187, 72, 255) : IM_COL32(127, 183, 213, 255);
    drawDiamond(draw, center, color, isSelected);
    const glm::vec2 delta(ImGui::GetIO().MousePos.x - center.x, ImGui::GetIO().MousePos.y - center.y);
    if (glm::dot(delta, delta) <= 64.0f && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      selection = {passIndex, track.property, key.timeSeconds};
      timeline.timeSeconds = key.timeSeconds;
      timeline.playing = false;
    }
  }
  const float playheadX = areaMinimum.x + (timeline.timeSeconds / timeline.durationSeconds) * trackWidth;
  draw->AddLine(ImVec2(playheadX, areaMinimum.y), ImVec2(playheadX, areaMaximum.y),
    IM_COL32(235, 112, 83, 210), 1.0f);
  ImGui::PushID(static_cast<int>(passIndex));
  ImGui::PushID(static_cast<int>(track.property));
  if (ImGui::BeginPopupContextItem("track-menu")) {
    if (ImGui::MenuItem("Key at playhead")) {
      const glm::vec4 value = samplePropertyTrack(track, timeline.timeSeconds);
      setPropertyKeyframe(pass, track.property, timeline.timeSeconds, &value);
    }
    if (ImGui::MenuItem("Remove track")) track.keyframes.clear();
    ImGui::EndPopup();
  }
  ImGui::PopID();
  ImGui::PopID();
}

void drawSelectedKeyEditor(AnimationTimeline& timeline, RenderStack& stack) {
  ImGui::TextDisabled("SELECTED KEY");
  if (selection.pass != globalPassIndex && selection.pass >= stack.passes().size()) {
    ImGui::TextWrapped("Click a diamond to inspect and modify its exact time and value.");
    return;
  }
  RenderPass& pass = selection.pass == globalPassIndex ? stack.global() : stack.passes()[selection.pass];
  PropertyAnimationTrack* track = findPropertyTrack(pass, selection.property);
  if (track == nullptr) {
    selection.pass = std::numeric_limits<std::size_t>::max();
    return;
  }
  const std::size_t keyIndex = propertyKeyframeIndexNear(pass, selection.property, selection.time, 0.0001f);
  if (keyIndex == std::numeric_limits<std::size_t>::max()) {
    selection.pass = std::numeric_limits<std::size_t>::max();
    return;
  }
  const AnimationPropertyInfo& info = animationPropertyInfo(selection.property);
  ImGui::TextWrapped("%s / %s", pass.name.c_str(), info.label.data());
  float newTime = track->keyframes[keyIndex].timeSeconds;
  glm::vec4 value = track->keyframes[keyIndex].value;
  if (ImGui::DragFloat("Time", &newTime, 0.01f, 0.0f, timeline.durationSeconds, "%.3f s")) {
    const float oldTime = selection.time;
    static_cast<void>(removePropertyKeyframe(pass, selection.property, oldTime, 0.0001f));
    setPropertyKeyframe(pass, selection.property, newTime, &value);
    selection.time = newTime;
    timeline.timeSeconds = newTime;
    track = findPropertyTrack(pass, selection.property);
  }
  bool valueChanged = false;
  if (info.kind == AnimationValueKind::Boolean) {
    bool enabled = value.x >= 0.5f;
    if (ImGui::Checkbox("Value", &enabled)) { value.x = enabled ? 1.0f : 0.0f; valueChanged = true; }
  } else if (info.kind == AnimationValueKind::Enumeration) {
    int integer = static_cast<int>(std::round(value.x));
    const char* currentLabel = animationPropertyDiscreteValueLabel(selection.property, integer);
    if (currentLabel != nullptr && ImGui::BeginCombo("Value", currentLabel)) {
      for (int option = static_cast<int>(info.minimum); option <= static_cast<int>(info.maximum); ++option) {
        const char* optionLabel = animationPropertyDiscreteValueLabel(selection.property, option);
        if (optionLabel != nullptr && ImGui::Selectable(optionLabel, integer == option)) {
          integer = option; value.x = static_cast<float>(integer); valueChanged = true;
        }
      }
      ImGui::EndCombo();
    } else if (currentLabel == nullptr && ImGui::DragInt("Value", &integer, 1.0f,
        static_cast<int>(info.minimum), static_cast<int>(info.maximum))) {
      value.x = static_cast<float>(integer); valueChanged = true;
    }
  } else if (info.kind == AnimationValueKind::Integer) {
    int integer = static_cast<int>(std::round(value.x));
    if (ImGui::DragInt("Value", &integer, 1.0f, static_cast<int>(info.minimum),
        static_cast<int>(info.maximum))) { value.x = static_cast<float>(integer); valueChanged = true; }
  } else if (info.components == 1) valueChanged = ImGui::DragFloat("Value", &value.x, 0.01f,
      info.minimum, info.maximum);
  else if (info.components == 2) valueChanged = ImGui::DragFloat2("Value", &value.x, 0.01f);
  else if (info.kind == AnimationValueKind::Color3)
    valueChanged = ImGui::ColorEdit3("Value", &value.x);
  else if (info.kind == AnimationValueKind::Color4)
    valueChanged = ImGui::ColorEdit4("Value", &value.x);
  else if (info.components == 3) valueChanged = ImGui::DragFloat3("Value", &value.x, 0.01f);
  else valueChanged = ImGui::DragFloat4("Value", &value.x, 0.01f);
  if (valueChanged) setPropertyKeyframe(pass, selection.property, selection.time, &value);

  if (info.behavior == AnimationBehavior::Step) {
    ImGui::TextDisabled("Interpolation: Step (discrete property)");
  } else {
    const char* interpolationLabels[] = {"Step", "Linear", "Smooth step"};
    int interpolation = static_cast<int>(track->interpolation);
    if (ImGui::Combo("Interpolation", &interpolation, interpolationLabels, 3))
      track->interpolation = static_cast<KeyframeInterpolation>(interpolation);
  }
  if (ImGui::Button("Delete selected key", ImVec2(-1.0f, 0.0f))) {
    static_cast<void>(removePropertyKeyframe(pass, selection.property, selection.time, 0.0001f));
    selection.pass = std::numeric_limits<std::size_t>::max();
  }
}

} // namespace

void drawAnimationEditor(AnimationTimeline& timeline, RenderStack& stack, bool& previewAtPlayhead,
    const bool globalScope) {
  ImGui::BeginChild("Animation timeline", ImVec2(0.0f, 238.0f), true);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled(globalScope ? "GLOBAL ANIMATION" : "LOCAL PASS ANIMATION");
  ImGui::SameLine();
  if (ImGui::Button(timeline.playing ? "Pause" : "Play")) timeline.playing = !timeline.playing;
  ImGui::SameLine();
  if (ImGui::Button("Stop")) { timeline.playing = false; timeline.timeSeconds = 0.0f; }
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &timeline.loop);
  ImGui::SameLine();
  ImGui::Checkbox("Preview", &previewAtPlayhead);
  ImGui::SameLine();
  ImGui::Checkbox("Auto Key", &timeline.autoKey);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("When an animatable inspector control changes, write or replace its key at the playhead.");
  ImGui::SameLine();
  ImGui::Checkbox("All passes", &timeline.showAllPasses);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(85.0f);
  ImGui::DragFloat("Duration", &timeline.durationSeconds, 0.1f, 0.1f, 120.0f, "%.1f s");
  timeline.durationSeconds = std::max(timeline.durationSeconds, 0.1f);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(75.0f);
  ImGui::DragFloat("Rate", &timeline.playbackRate, 0.05f, -4.0f, 4.0f, "%.2fx");

  ImGui::SetNextItemWidth(std::max(160.0f, ImGui::GetContentRegionAvail().x - 390.0f));
  if (ImGui::SliderFloat("Time", &timeline.timeSeconds, 0.0f, timeline.durationSeconds, "%.3f s"))
    timeline.playing = false;
  ImGui::SameLine();
  ImGui::SetNextItemWidth(180.0f);
  const std::vector<AnimationProperty> properties = animatableProperties(globalScope);
  propertyToAdd = std::clamp(propertyToAdd, 0, static_cast<int>(properties.size()) - 1);
  const AnimationProperty chosenProperty = properties[static_cast<std::size_t>(propertyToAdd)];
  if (ImGui::BeginCombo("##property-to-key", animationPropertyInfo(chosenProperty).label.data())) {
    std::string_view previousGroup;
    for (std::size_t index = 0; index < properties.size(); ++index) {
      const AnimationProperty property = properties[index];
      const AnimationPropertyInfo& info = animationPropertyInfo(property);
      if (info.group != previousGroup) {
        ImGui::SeparatorText(info.group.data());
        previousGroup = info.group;
      }
      if (ImGui::Selectable(info.label.data(), propertyToAdd == static_cast<int>(index)))
        propertyToAdd = static_cast<int>(index);
    }
    ImGui::EndCombo();
  }
  ImGui::SameLine();
  if (ImGui::Button("Key property")) {
    RenderPass& pass = globalScope ? stack.global() : stack.selected();
    const AnimationProperty property = properties[static_cast<std::size_t>(propertyToAdd)];
    const RenderPass evaluated = globalScope ? evaluateRenderPass(pass, timeline.timeSeconds)
      : materializeRenderPass(stack, stack.selectedIndex(), timeline.timeSeconds);
    const glm::vec4 value = animationPropertyValue(evaluated, property);
    setPropertyKeyframe(pass, property, timeline.timeSeconds, &value);
    selection = {globalScope ? globalPassIndex : stack.selectedIndex(), property, timeline.timeSeconds};
    previewAtPlayhead = true;
  }

  const float editorWidth = 290.0f;
  ImGui::BeginChild("Dope sheet tracks", ImVec2(std::max(200.0f, ImGui::GetContentRegionAvail().x - editorWidth - 7.0f), 146.0f), true);
  bool anyTracks = false;
  if (globalScope || timeline.showAllPasses) {
    RenderPass& pass = stack.global();
    ImGui::TextDisabled("%s", globalScope ? "> Global base" : "  Global base");
    for (std::size_t trackIndex = 0; trackIndex < pass.animation.tracks.size();) {
      PropertyAnimationTrack& track = pass.animation.tracks[trackIndex];
      if (track.keyframes.empty()) {
        pass.animation.tracks.erase(pass.animation.tracks.begin() + static_cast<std::ptrdiff_t>(trackIndex));
        continue;
      }
      ImGui::PushID("global");
      ImGui::PushID(static_cast<int>(track.property));
      drawTrackRow(timeline, pass, globalPassIndex, track, 190.0f);
      ImGui::PopID();
      ImGui::PopID();
      anyTracks = true;
      ++trackIndex;
    }
  }
  const std::size_t firstPass = timeline.showAllPasses ? 0 : stack.selectedIndex();
  const std::size_t endPass = globalScope && !timeline.showAllPasses ? 0
    : timeline.showAllPasses ? stack.passes().size() : stack.selectedIndex() + 1;
  for (std::size_t passIndex = firstPass; passIndex < endPass; ++passIndex) {
    RenderPass& pass = stack.passes()[passIndex];
    if (timeline.showAllPasses) {
      ImGui::TextDisabled("%s%s", passIndex == stack.selectedIndex() ? "> " : "  ", pass.name.c_str());
      if (ImGui::IsItemClicked()) stack.select(passIndex);
    }
    for (std::size_t trackIndex = 0; trackIndex < pass.animation.tracks.size();) {
      PropertyAnimationTrack& track = pass.animation.tracks[trackIndex];
      if (track.keyframes.empty()) {
        pass.animation.tracks.erase(pass.animation.tracks.begin() + static_cast<std::ptrdiff_t>(trackIndex));
        continue;
      }
      ImGui::PushID(static_cast<int>(passIndex));
      ImGui::PushID(static_cast<int>(track.property));
      drawTrackRow(timeline, pass, passIndex, track, 190.0f);
      ImGui::PopID();
      ImGui::PopID();
      anyTracks = true;
      ++trackIndex;
    }
  }
  if (!anyTracks) ImGui::TextWrapped("No animated properties. Choose a real parameter above and press Key property, or use a diamond beside an inspector control.");
  ImGui::EndChild();
  ImGui::SameLine();
  ImGui::BeginChild("Selected key editor", ImVec2(0.0f, 146.0f), true);
  drawSelectedKeyEditor(timeline, stack);
  ImGui::EndChild();
  ImGui::EndChild();
}

} // namespace gfxlab::ui
