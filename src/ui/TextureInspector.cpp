#include "ui/TextureInspector.hpp"

#include "renderer/TextureReadback.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace gfxlab::ui {

void drawTextureInspector(bool& open, const ViewportImages& images, const std::string_view selectedPassName) {
  if (!open) return;
  const float uiScale = ImGui::GetFontSize() / 13.0f;
  ImGui::SetNextWindowSize(ImVec2(720.0f, 520.0f), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(320.0f * uiScale, 240.0f * uiScale),
    ImVec2(FLT_MAX, FLT_MAX));
  if (!ImGui::Begin("Texture Inspector", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return;
  }
  keepCurrentWindowVisible();

  enum class Source { Selected, Base, Composite };
  static Source source = Source::Selected;
  static float zoom = 1.0f;
  static ImVec2 center(0.5f, 0.5f);
  constexpr std::array<const char*, 3> sourceLabels = {"Selected pass", "Base pass", "Composite"};
  int sourceIndex = static_cast<int>(source);
  ImGui::SetNextItemWidth(150.0f);
  if (ImGui::Combo("##texture-source", &sourceIndex, sourceLabels.data(), sourceLabels.size()))
    source = static_cast<Source>(sourceIndex);
  ImGui::SameLine();
  if (ImGui::Button("Fit")) {
    zoom = 1.0f;
    center = ImVec2(0.5f, 0.5f);
  }

  const unsigned int texture = source == Source::Selected ? images.selected
    : source == Source::Base ? images.base : images.composite;
  const TextureDimensions dimensions = textureDimensions(texture);
  const std::string label = source == Source::Selected ? std::string(selectedPassName)
    : source == Source::Base ? "Base pass" : "Composite stack";
  ImGui::TextDisabled("%s   %d x %d texels   Wheel zoom   MMB/RMB pan", label.c_str(),
    dimensions.width, dimensions.height);
  ImGui::Separator();

  const ImVec2 origin = ImGui::GetCursorScreenPos();
  const ImVec2 size = ImGui::GetContentRegionAvail();
  const ImVec2 end(origin.x + size.x, origin.y + size.y);
  ImGui::InvisibleButton("texture-inspection-input", size);
  const bool hovered = ImGui::IsItemHovered();
  const float previousSpan = 1.0f / zoom;
  if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
    zoom = std::clamp(zoom * std::pow(1.25f, ImGui::GetIO().MouseWheel), 1.0f, 128.0f);
    const float nextSpan = 1.0f / zoom;
    const ImVec2 mouse = ImGui::GetMousePos();
    const float normalizedX = std::clamp((mouse.x - origin.x) / std::max(1.0f, size.x), 0.0f, 1.0f);
    const float normalizedY = std::clamp((mouse.y - origin.y) / std::max(1.0f, size.y), 0.0f, 1.0f);
    center.x += (normalizedX - 0.5f) * (previousSpan - nextSpan);
    center.y += (normalizedY - 0.5f) * (previousSpan - nextSpan);
  }
  const float span = 1.0f / zoom;
  if (hovered && (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) ||
      ImGui::IsMouseDragging(ImGuiMouseButton_Right))) {
    center.x -= ImGui::GetIO().MouseDelta.x / std::max(1.0f, size.x) * span;
    center.y -= ImGui::GetIO().MouseDelta.y / std::max(1.0f, size.y) * span;
  }
  center.x = std::clamp(center.x, span * 0.5f, 1.0f - span * 0.5f);
  center.y = std::clamp(center.y, span * 0.5f, 1.0f - span * 0.5f);
  const ImVec2 uv0(center.x - span * 0.5f, 1.0f - (center.y - span * 0.5f));
  const ImVec2 uv1(center.x + span * 0.5f, 1.0f - (center.y + span * 0.5f));
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(origin, end, IM_COL32(19, 20, 22, 255));
  draw->AddImage(static_cast<ImTextureID>(texture), origin, end, uv0, uv1);

  if (hovered && dimensions.width > 0 && dimensions.height > 0) {
    const ImVec2 mouse = ImGui::GetMousePos();
    const float viewX = std::clamp((mouse.x - origin.x) / std::max(1.0f, size.x), 0.0f, 0.999999f);
    const float viewY = std::clamp((mouse.y - origin.y) / std::max(1.0f, size.y), 0.0f, 0.999999f);
    const float textureU = center.x - span * 0.5f + viewX * span;
    const float textureFromTop = center.y - span * 0.5f + viewY * span;
    const int x = std::clamp(static_cast<int>(textureU * dimensions.width), 0, dimensions.width - 1);
    const int yFromTop = std::clamp(static_cast<int>(textureFromTop * dimensions.height), 0,
      dimensions.height - 1);
    const glm::vec4 rgba = readTexturePixel(texture, x, dimensions.height - 1 - yFromTop);
    ImGui::BeginTooltip();
    ImGui::Text("Texel: %d, %d", x, yFromTop);
    ImGui::Text("RGBA: %.5f  %.5f  %.5f  %.5f", rgba.r, rgba.g, rgba.b, rgba.a);
    ImGui::EndTooltip();
  }
  ImGui::End();
}

} // namespace gfxlab::ui
