#include "ui/AnimationEditor.hpp"

#include "app/Animation.hpp"
#include "app/RenderStack.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <limits>

namespace gfxlab::ui {

void drawAnimationEditor(AnimationTimeline& timeline, RenderStack& stack, bool& previewAtPlayhead) {
  RenderPass& pass = stack.selected();
  ImGui::BeginChild("Animation timeline", ImVec2(0.0f, 86.0f), true);
  ImGui::AlignTextToFramePadding();
  ImGui::TextDisabled("ANIMATION");
  ImGui::SameLine();
  if (ImGui::Button(timeline.playing ? "Pause" : "Play")) timeline.playing = !timeline.playing;
  ImGui::SameLine();
  if (ImGui::Button("Stop")) {
    timeline.playing = false;
    timeline.timeSeconds = 0.0f;
  }
  ImGui::SameLine();
  ImGui::Checkbox("Loop", &timeline.loop);
  ImGui::SameLine();
  ImGui::Checkbox("Preview playhead", &previewAtPlayhead);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(90.0f);
  ImGui::DragFloat("Duration", &timeline.durationSeconds, 0.1f, 0.1f, 120.0f, "%.1f s");
  timeline.durationSeconds = std::max(timeline.durationSeconds, 0.1f);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(80.0f);
  ImGui::DragFloat("Rate", &timeline.playbackRate, 0.05f, -4.0f, 4.0f, "%.2fx");

  ImGui::SetNextItemWidth(std::max(180.0f, ImGui::GetContentRegionAvail().x - 470.0f));
  if (ImGui::SliderFloat("Time", &timeline.timeSeconds, 0.0f, timeline.durationSeconds, "%.3f s"))
    timeline.playing = false;
  ImGui::SameLine();
  if (ImGui::Button("Set / replace key")) setPassKeyframe(pass, timeline.timeSeconds);
  ImGui::SameLine();
  const std::size_t nearby = keyframeIndexNear(pass, timeline.timeSeconds);
  ImGui::BeginDisabled(nearby == std::numeric_limits<std::size_t>::max());
  if (ImGui::Button("Delete key")) static_cast<void>(removePassKeyframe(pass, timeline.timeSeconds));
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::BeginDisabled(pass.animation.keyframes.empty());
  if (ImGui::Button("Load sampled values")) {
    const RenderStack sampled = evaluateRenderStack(stack, timeline.timeSeconds);
    applyAnimationValues(pass, captureAnimationValues(sampled.selected()));
    previewAtPlayhead = false;
    timeline.playing = false;
  }
  ImGui::EndDisabled();

  ImGui::SameLine();
  ImGui::Checkbox("Track", &pass.animation.enabled);
  ImGui::SameLine();
  ImGui::SetNextItemWidth(100.0f);
  const char* interpolationLabels[] = {"Step", "Linear", "Smooth"};
  int interpolation = static_cast<int>(pass.animation.interpolation);
  if (ImGui::Combo("##interpolation", &interpolation, interpolationLabels, 3))
    pass.animation.interpolation = static_cast<KeyframeInterpolation>(interpolation);
  ImGui::SameLine();
  ImGui::TextDisabled("%s: %zu keys", pass.name.c_str(), pass.animation.keyframes.size());
  for (std::size_t index = 0; index < pass.animation.keyframes.size(); ++index) {
    ImGui::SameLine();
    ImGui::PushID(static_cast<int>(index));
    char label[32];
    std::snprintf(label, sizeof(label), "%.2f", pass.animation.keyframes[index].timeSeconds);
    if (ImGui::SmallButton(label)) {
      timeline.timeSeconds = pass.animation.keyframes[index].timeSeconds;
      timeline.playing = false;
    }
    ImGui::PopID();
  }
  ImGui::EndChild();
}

} // namespace gfxlab::ui
