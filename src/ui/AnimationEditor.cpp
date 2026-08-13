#include "ui/AnimationEditor.hpp"

#include "app/Animation.hpp"
#include "app/RenderStack.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <limits>
#include <string>
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
int curveComponent = 0;
bool draggingCurveKey = false;
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
  const float effectiveLabelWidth = std::clamp(labelWidth, 70.0f, std::max(70.0f, rowWidth - 80.0f));
  const float trackWidth = std::max(40.0f, rowWidth - effectiveLabelWidth);
  ImGui::TextUnformatted(info.label.data());
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s — %s", info.group.data(), pass.name.c_str());
  ImGui::SameLine(effectiveLabelWidth);
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

void drawCurveEditor(AnimationTimeline& timeline, RenderStack& stack) {
  if (selection.pass != globalPassIndex && selection.pass >= stack.passes().size()) {
    ImGui::TextWrapped("Select a key diamond to inspect its property curve.");
    return;
  }
  RenderPass& pass = selection.pass == globalPassIndex ? stack.global() : stack.passes()[selection.pass];
  PropertyAnimationTrack* track = findPropertyTrack(pass, selection.property);
  if (track == nullptr || track->keyframes.empty()) {
    ImGui::TextWrapped("Select a key diamond to inspect its property curve.");
    return;
  }
  const AnimationPropertyInfo& info = animationPropertyInfo(track->property);
  curveComponent = std::clamp(curveComponent, 0, info.components - 1);
  ImGui::Text("%s / %s", pass.name.c_str(), info.label.data());
  if (info.components > 1) {
    constexpr std::array<const char*, 4> components = {"X / R", "Y / G", "Z / B", "W / A"};
    ImGui::SameLine();
    ImGui::SetNextItemWidth(78.0f);
    ImGui::Combo("##curve-component", &curveComponent, components.data(), info.components);
  }
  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  ImGui::InvisibleButton("curve-canvas", size);
  const ImVec2 end(origin.x + size.x, origin.y + size.y);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(origin, end, IM_COL32(24, 26, 29, 255));

  float minimum = track->keyframes.front().value[curveComponent];
  float maximum = minimum;
  for (const PropertyKeyframe& key : track->keyframes) {
    minimum = std::min(minimum, key.value[curveComponent]);
    maximum = std::max(maximum, key.value[curveComponent]);
  }
  const float padding = std::max((maximum - minimum) * 0.15f, 0.05f);
  minimum -= padding;
  maximum += padding;
  const auto point = [&](const float time, const float value) {
    const float x = origin.x + time / timeline.durationSeconds * size.x;
    const float normalizedValue = (value - minimum) / std::max(0.000001f, maximum - minimum);
    return ImVec2(x, end.y - normalizedValue * size.y);
  };
  for (int division = 1; division < 4; ++division) {
    const float y = origin.y + size.y * static_cast<float>(division) / 4.0f;
    draw->AddLine(ImVec2(origin.x, y), ImVec2(end.x, y), IM_COL32(52, 55, 60, 255));
  }
  ImVec2 previous = point(0.0f, samplePropertyTrack(*track, 0.0f)[curveComponent]);
  constexpr int sampleCount = 180;
  for (int sample = 1; sample <= sampleCount; ++sample) {
    const float time = timeline.durationSeconds * static_cast<float>(sample) / sampleCount;
    const ImVec2 current = point(time, samplePropertyTrack(*track, time)[curveComponent]);
    draw->AddLine(previous, current, IM_COL32(105, 182, 221, 255), 2.0f);
    previous = current;
  }
  const float playheadX = origin.x + timeline.timeSeconds / timeline.durationSeconds * size.x;
  draw->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, end.y), IM_COL32(235, 112, 83, 210));

  bool moveKey = false;
  float oldTime = 0.0f;
  float newTime = 0.0f;
  glm::vec4 newValue{};
  if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) draggingCurveKey = false;
  for (const PropertyKeyframe& key : track->keyframes) {
    const ImVec2 keyPoint = point(key.timeSeconds, key.value[curveComponent]);
    const bool isSelected = selected(selection.pass, track->property, key.timeSeconds);
    drawDiamond(draw, keyPoint, IM_COL32(245, 187, 72, 255), isSelected);
    const glm::vec2 delta(ImGui::GetIO().MousePos.x - keyPoint.x, ImGui::GetIO().MousePos.y - keyPoint.y);
    if (glm::dot(delta, delta) <= 81.0f && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
      selection.time = key.timeSeconds;
      timeline.timeSeconds = key.timeSeconds;
      timeline.playing = false;
      draggingCurveKey = true;
    }
    if (isSelected && draggingCurveKey && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      oldTime = key.timeSeconds;
      newTime = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / std::max(1.0f, size.x) *
        timeline.durationSeconds, 0.0f, timeline.durationSeconds);
      newValue = key.value;
      newValue[curveComponent] = minimum + (end.y - ImGui::GetIO().MousePos.y) /
        std::max(1.0f, size.y) * (maximum - minimum);
      moveKey = true;
    }
  }
  if (moveKey) {
    static_cast<void>(removePropertyKeyframe(pass, track->property, oldTime, 0.0001f));
    setPropertyKeyframe(pass, track->property, newTime, &newValue);
    selection.time = newTime;
    timeline.timeSeconds = newTime;
  } else if (ImGui::IsItemHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
    timeline.timeSeconds = std::clamp((ImGui::GetIO().MousePos.x - origin.x) /
      std::max(1.0f, size.x) * timeline.durationSeconds, 0.0f, timeline.durationSeconds);
    timeline.playing = false;
  }
  draw->AddText(ImVec2(origin.x + 5.0f, origin.y + 4.0f), IM_COL32(180, 183, 188, 255),
    std::to_string(maximum).c_str());
  draw->AddText(ImVec2(origin.x + 5.0f, end.y - 18.0f), IM_COL32(180, 183, 188, 255),
    std::to_string(minimum).c_str());
}

} // namespace

void drawAnimationEditor(AnimationTimeline& timeline, RenderStack& stack, bool& previewAtPlayhead,
    const bool globalScope) {
  static bool curveView = false;
  ImGui::BeginChild("Animation editor contents", ImVec2(0.0f, 0.0f), false);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled(globalScope ? "GLOBAL ANIMATION" : "LOCAL PASS ANIMATION");
  ImGui::SameLine();
  if (ImGui::Button(timeline.playing ? "Pause" : "Play")) timeline.playing = !timeline.playing;
  ImGui::SameLine();
  if (ImGui::Button("Stop")) { timeline.playing = false; timeline.timeSeconds = 0.0f; }
  ImGui::SameLine();
  if (ImGui::Button(curveView ? "Dope sheet" : "Curve view")) curveView = !curveView;

  ImGui::Checkbox("Loop", &timeline.loop);
  ImGui::SameLine();
  ImGui::Checkbox("Preview", &previewAtPlayhead);
  ImGui::SameLine();
  ImGui::Checkbox("Auto Key", &timeline.autoKey);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("When an animatable inspector control changes, write or replace its key at the playhead.");
  ImGui::SameLine();
  ImGui::Checkbox("All passes", &timeline.showAllPasses);

  ImGui::SetNextItemWidth(100.0f);
  ImGui::DragFloat("Duration", &timeline.durationSeconds, 0.1f, 0.1f, 120.0f, "%.1f s");
  timeline.durationSeconds = std::max(timeline.durationSeconds, 0.1f);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.0f);
  ImGui::DragFloat("Rate", &timeline.playbackRate, 0.05f, -4.0f, 4.0f, "%.2fx");

  ImGui::Text("Time   %.3f s", timeline.timeSeconds);
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::SliderFloat("##timeline-time", &timeline.timeSeconds, 0.0f, timeline.durationSeconds, "%.3f s"))
    timeline.playing = false;

  const std::vector<AnimationProperty> properties = animatableProperties(globalScope);
  propertyToAdd = std::clamp(propertyToAdd, 0, static_cast<int>(properties.size()) - 1);
  const AnimationProperty chosenProperty = properties[static_cast<std::size_t>(propertyToAdd)];
  const float actionWidth = ImGui::CalcTextSize("Key property").x + ImGui::GetStyle().FramePadding.x * 2.0f;
  ImGui::SetNextItemWidth(std::max(120.0f, ImGui::GetContentRegionAvail().x - actionWidth -
    ImGui::GetStyle().ItemSpacing.x));
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

  const ImVec2 editorAvailable = ImGui::GetContentRegionAvail();
  const float uiScale = ImGui::GetFontSize() / 13.0f;
  const bool horizontalPanes = editorAvailable.x >= 640.0f * uiScale;
  const float editorWidth = horizontalPanes ? std::clamp(editorAvailable.x * 0.34f,
    250.0f * uiScale, 360.0f * uiScale)
    : editorAvailable.x;
  const float tracksWidth = horizontalPanes
    ? std::max(200.0f * uiScale, editorAvailable.x - editorWidth - ImGui::GetStyle().ItemSpacing.x)
    : editorAvailable.x;
  const float tracksHeight = horizontalPanes ? editorAvailable.y
    : std::max(100.0f * uiScale, editorAvailable.y * 0.58f);
  ImGui::BeginChild("Animation tracks", ImVec2(tracksWidth, tracksHeight), true);
  if (curveView) {
    drawCurveEditor(timeline, stack);
    ImGui::EndChild();
    if (horizontalPanes) ImGui::SameLine();
    ImGui::BeginChild("Selected key editor", ImVec2(0.0f, horizontalPanes ? editorAvailable.y : 0.0f), true);
    drawSelectedKeyEditor(timeline, stack);
    ImGui::EndChild();
    ImGui::EndChild();
    return;
  }
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
  if (horizontalPanes) ImGui::SameLine();
  ImGui::BeginChild("Selected key editor", ImVec2(0.0f, horizontalPanes ? editorAvailable.y : 0.0f), true);
  drawSelectedKeyEditor(timeline, stack);
  ImGui::EndChild();
  ImGui::EndChild();
}

} // namespace gfxlab::ui
