#include "ui/DocumentTimeline.hpp"

#include "document/Properties.hpp"

#include <imgui.h>

#include <algorithm>
#include <optional>
#include <string>

namespace gfxlab::ui {
namespace {

struct KeySelection {
  document::PropertyAddress track;
  std::size_t key = 0;
};

std::optional<KeySelection> selectedKey;

std::string ownerName(const document::Document& document, const document::ObjectId owner) {
  if (owner == document::renderDefaultsObject) return "Render Defaults";
  const std::optional<document::OperationId> operation = document::operationFromObject(owner);
  const document::Operation* found = operation.has_value()
    ? document::findOperation(document, *operation) : nullptr;
  return found == nullptr ? "Unknown" : found->name;
}

document::AnimationTrack* findTrack(document::Document& document,
    const document::PropertyAddress target) {
  const auto found = std::find_if(document.automation.animation.begin(),
    document.automation.animation.end(), [&](const document::AnimationTrack& track) {
      return track.target == target;
    });
  return found == document.automation.animation.end() ? nullptr : &*found;
}

} // namespace

void drawDocumentTimeline(AnimationTimeline& timeline, document::Document& document,
    const editor::EditorState& editorState, editor::CommandHistory& history) {
  if (ImGui::Button(timeline.playing ? "Pause" : "Play")) timeline.playing = !timeline.playing;
  ImGui::SameLine();
  if (ImGui::Button("Start")) { timeline.playing = false; timeline.timeSeconds = 0.0f; }
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &timeline.loop);
  ImGui::SameLine();
  ImGui::Checkbox("Auto Key", &timeline.autoKey);
  ImGui::SetNextItemWidth(-1.0f);
  ImGui::SliderFloat("##playhead", &timeline.timeSeconds, 0.0f, timeline.durationSeconds,
    "%.3f s");

  const float labelWidth = std::min(230.0f, ImGui::GetContentRegionAvail().x * 0.38f);
  const float rulerWidth = std::max(80.0f, ImGui::GetContentRegionAvail().x - labelWidth - 12.0f);
  for (const document::AnimationTrack& track : document.automation.animation) {
    if (!timeline.showAllPasses) {
      const document::ObjectId selectedOwner = editorState.selection.kind == editor::SelectionKind::Operation
        ? document::operationObject(editorState.selection.operation)
        : editorState.selection.kind == editor::SelectionKind::RenderDefaults
          ? document::renderDefaultsObject : document::ObjectId{};
      if (track.target.owner != selectedOwner) continue;
    }
    const document::PropertyDescriptor* property = document::propertyDescriptor(track.target.property);
    if (property == nullptr) continue;
    ImGui::PushID(static_cast<int>(track.target.owner.value ^ track.target.property.value));
    const std::string label = ownerName(document, track.target.owner) + " / " + property->label;
    ImGui::AlignTextToFramePadding();
    ImGui::Text("%.*s", 34, label.c_str());
    ImGui::SameLine(labelWidth + 8.0f);
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size(rulerWidth, ImGui::GetFrameHeight());
    ImGui::InvisibleButton("track", size);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y), IM_COL32(35, 38, 42, 255));
    const float playheadX = origin.x + size.x * std::clamp(timeline.timeSeconds /
      std::max(timeline.durationSeconds, 0.001f), 0.0f, 1.0f);
    draw->AddLine(ImVec2(playheadX, origin.y), ImVec2(playheadX, origin.y + size.y),
      IM_COL32(238, 187, 72, 210));
    for (std::size_t keyIndex = 0; keyIndex < track.keyframes.size(); ++keyIndex) {
      const float x = origin.x + size.x * std::clamp(track.keyframes[keyIndex].timeSeconds /
        std::max(timeline.durationSeconds, 0.001f), 0.0f, 1.0f);
      const ImVec2 center(x, origin.y + size.y * 0.5f);
      const ImVec2 points[] = {{center.x, center.y - 5.0f}, {center.x + 5.0f, center.y},
        {center.x, center.y + 5.0f}, {center.x - 5.0f, center.y}};
      const bool selected = selectedKey.has_value() && selectedKey->track == track.target &&
        selectedKey->key == keyIndex;
      draw->AddConvexPolyFilled(points, 4, selected ? IM_COL32(245, 187, 72, 255)
        : IM_COL32(112, 170, 202, 255));
      if (ImGui::IsMouseClicked(ImGuiMouseButton_Left) &&
          ImGui::IsMouseHoveringRect(ImVec2(center.x - 7.0f, center.y - 7.0f),
            ImVec2(center.x + 7.0f, center.y + 7.0f))) {
        selectedKey = KeySelection{track.target, keyIndex};
        timeline.timeSeconds = track.keyframes[keyIndex].timeSeconds;
      }
    }
    if (ImGui::IsItemClicked() && !selectedKey.has_value()) {
      const float normalized = std::clamp((ImGui::GetIO().MousePos.x - origin.x) / size.x, 0.0f, 1.0f);
      timeline.timeSeconds = normalized * timeline.durationSeconds;
    }
    ImGui::PopID();
  }

  ImGui::SeparatorText("Selected Key");
  if (!selectedKey.has_value()) {
    ImGui::TextDisabled("Select a key diamond to edit it.");
    return;
  }
  document::Document edited = document;
  document::AnimationTrack* track = findTrack(edited, selectedKey->track);
  if (track == nullptr || selectedKey->key >= track->keyframes.size()) {
    selectedKey.reset();
    return;
  }
  PropertyKeyframe& key = track->keyframes[selectedKey->key];
  const document::PropertyDescriptor* property = document::propertyDescriptor(track->target.property);
  bool changed = ImGui::DragFloat("Time", &key.timeSeconds, 1.0f /
    static_cast<float>(std::max(timeline.framesPerSecond, 1)), 0.0f, timeline.durationSeconds, "%.3f s");
  if (property != nullptr) {
    if (property->components == 1)
      changed |= ImGui::DragFloat("Value", &key.value.x, 0.01f, property->minimum, property->maximum);
    else if (property->components == 2)
      changed |= ImGui::DragFloat2("Value", &key.value.x, 0.01f, property->minimum, property->maximum);
    else if (property->components == 3)
      changed |= ImGui::DragFloat3("Value", &key.value.x, 0.01f, property->minimum, property->maximum);
    else
      changed |= ImGui::DragFloat4("Value", &key.value.x, 0.01f, property->minimum, property->maximum);
  }
  int interpolation = static_cast<int>(track->interpolation);
  constexpr const char* interpolations[] = {"Step", "Linear", "Smooth step"};
  if (ImGui::Combo("Interpolation", &interpolation, interpolations, 3)) {
    track->interpolation = static_cast<KeyframeInterpolation>(interpolation);
    changed = true;
  }
  if (ImGui::Button("Delete Key")) {
    track->keyframes.erase(track->keyframes.begin() + static_cast<std::ptrdiff_t>(selectedKey->key));
    if (track->keyframes.empty()) edited.automation.animation.erase(std::remove_if(
      edited.automation.animation.begin(), edited.automation.animation.end(),
      [&](const document::AnimationTrack& candidate) { return candidate.target == selectedKey->track; }),
      edited.automation.animation.end());
    selectedKey.reset();
    changed = true;
  }
  if (changed) {
    if (selectedKey.has_value()) {
      document::AnimationTrack* remaining = findTrack(edited, selectedKey->track);
      if (remaining != nullptr) std::sort(remaining->keyframes.begin(), remaining->keyframes.end(),
        [](const PropertyKeyframe& a, const PropertyKeyframe& b) {
          return a.timeSeconds < b.timeSeconds;
        });
    }
    const editor::ReplaceDocument replacement{std::move(edited)};
    if (ImGui::IsAnyItemActive()) static_cast<void>(history.executeContinuous(document, replacement));
    else static_cast<void>(history.execute(document, replacement));
  } else if (!ImGui::IsAnyItemActive()) history.finishContinuous(document);
}

} // namespace gfxlab::ui
