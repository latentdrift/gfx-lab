#include "ui/InstrumentWidgets.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>

namespace gfxlab::ui {
namespace {

ImU32 color(const ImGuiCol index, const float alpha = 1.0f) {
  ImVec4 value = ImGui::GetStyleColorVec4(index);
  value.w *= alpha;
  return ImGui::ColorConvertFloat4ToU32(value);
}

float clamp01(const float value) {
  return std::clamp(value, 0.0f, 1.0f);
}

} // namespace

DirectionFieldResult directionField(const char* id, float& azimuthDegrees,
    float& elevationDegrees, const float height) {
  DirectionFieldResult result;
  const float width = std::max(120.0f, ImGui::GetContentRegionAvail().x);
  const ImVec2 topLeft = ImGui::GetCursorScreenPos();
  const ImVec2 size(width, height);
  ImGui::InvisibleButton(id, size, ImGuiButtonFlags_MouseButtonLeft);
  const bool active = ImGui::IsItemActive();
  const bool hovered = ImGui::IsItemHovered();

  if (active && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const ImVec2 mouse = ImGui::GetIO().MousePos;
    const float x = clamp01((mouse.x - topLeft.x) / size.x);
    const float y = clamp01((mouse.y - topLeft.y) / size.y);
    const float nextAzimuth = -180.0f + x * 360.0f;
    const float nextElevation = 90.0f - y * 180.0f;
    result.azimuthChanged = std::abs(nextAzimuth - azimuthDegrees) > 0.0001f;
    result.elevationChanged = std::abs(nextElevation - elevationDegrees) > 0.0001f;
    azimuthDegrees = nextAzimuth;
    elevationDegrees = nextElevation;
  }

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
  draw->AddRectFilled(topLeft, bottomRight, color(ImGuiCol_FrameBg));
  draw->AddRect(topLeft, bottomRight,
    color(hovered || active ? ImGuiCol_HeaderHovered : ImGuiCol_Border));
  for (int column = 1; column < 4; ++column) {
    const float x = topLeft.x + size.x * static_cast<float>(column) / 4.0f;
    draw->AddLine(ImVec2(x, topLeft.y), ImVec2(x, bottomRight.y), color(ImGuiCol_Border, 0.55f));
  }
  for (int row = 1; row < 4; ++row) {
    const float y = topLeft.y + size.y * static_cast<float>(row) / 4.0f;
    draw->AddLine(ImVec2(topLeft.x, y), ImVec2(bottomRight.x, y), color(ImGuiCol_Border, 0.55f));
  }
  const float px = topLeft.x + (azimuthDegrees + 180.0f) / 360.0f * size.x;
  const float py = topLeft.y + (90.0f - elevationDegrees) / 180.0f * size.y;
  draw->AddLine(ImVec2(px, topLeft.y), ImVec2(px, bottomRight.y), color(ImGuiCol_CheckMark, 0.45f));
  draw->AddLine(ImVec2(topLeft.x, py), ImVec2(bottomRight.x, py), color(ImGuiCol_CheckMark, 0.45f));
  draw->AddCircleFilled(ImVec2(px, py), 6.0f, color(ImGuiCol_CheckMark));
  draw->AddCircle(ImVec2(px, py), 9.0f, IM_COL32(230, 235, 235, 190), 20, 1.0f);
  draw->AddText(ImVec2(topLeft.x + 6.0f, topLeft.y + 5.0f), color(ImGuiCol_TextDisabled), "+90 elevation");
  draw->AddText(ImVec2(topLeft.x + 6.0f, bottomRight.y - ImGui::GetTextLineHeight() - 5.0f),
    color(ImGuiCol_TextDisabled), "-90 elevation");
  const char* azimuthLabel = "azimuth -180                                    +180";
  const ImVec2 labelSize = ImGui::CalcTextSize(azimuthLabel);
  draw->AddText(ImVec2(topLeft.x + std::max(5.0f, (size.x - labelSize.x) * 0.5f),
    bottomRight.y - ImGui::GetTextLineHeight() - 5.0f), color(ImGuiCol_TextDisabled), azimuthLabel);
  if (hovered) ImGui::SetTooltip("Drag to aim the directional light");
  return result;
}

IntervalFieldResult intervalField(const char* id, float& start, float& end,
    const float minimum, const float maximum, const char* clearLabel, const char* fullLabel) {
  IntervalFieldResult result;
  const float width = std::max(120.0f, ImGui::GetContentRegionAvail().x);
  const float height = 54.0f;
  const ImVec2 topLeft = ImGui::GetCursorScreenPos();
  const ImGuiID itemId = ImGui::GetID(id);
  ImGui::InvisibleButton(id, ImVec2(width, height), ImGuiButtonFlags_MouseButtonLeft);
  const float range = std::max(0.0001f, maximum - minimum);
  auto toX = [&](const float value) {
    return topLeft.x + clamp01((value - minimum) / range) * width;
  };
  if (ImGui::IsItemActivated()) {
    const float mouseX = ImGui::GetIO().MousePos.x;
    ImGui::GetStateStorage()->SetInt(itemId,
      std::abs(mouseX - toX(start)) <= std::abs(mouseX - toX(end)) ? 0 : 1);
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
    const float normalized = clamp01((ImGui::GetIO().MousePos.x - topLeft.x) / width);
    const float value = minimum + normalized * range;
    if (ImGui::GetStateStorage()->GetInt(itemId, 0) == 0) {
      const float next = std::min(value, end);
      result.startChanged = std::abs(next - start) > 0.0001f;
      start = next;
    } else {
      const float next = std::max(value, start);
      result.endChanged = std::abs(next - end) > 0.0001f;
      end = next;
    }
  }

  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 bottomRight(topLeft.x + width, topLeft.y + height);
  draw->AddRectFilled(topLeft, bottomRight, color(ImGuiCol_FrameBg));
  const float startX = toX(start);
  const float endX = toX(end);
  draw->AddRectFilled(ImVec2(startX, topLeft.y), ImVec2(endX, bottomRight.y),
    color(ImGuiCol_Header, 0.95f));
  draw->AddRect(topLeft, bottomRight, color(ImGuiCol_Border));
  draw->AddLine(ImVec2(startX, topLeft.y), ImVec2(startX, bottomRight.y), color(ImGuiCol_CheckMark), 2.0f);
  draw->AddLine(ImVec2(endX, topLeft.y), ImVec2(endX, bottomRight.y), color(ImGuiCol_CheckMark), 2.0f);
  draw->AddTriangleFilled(ImVec2(startX, topLeft.y), ImVec2(startX - 5.0f, topLeft.y + 7.0f),
    ImVec2(startX + 5.0f, topLeft.y + 7.0f), color(ImGuiCol_CheckMark));
  draw->AddTriangleFilled(ImVec2(endX, topLeft.y), ImVec2(endX - 5.0f, topLeft.y + 7.0f),
    ImVec2(endX + 5.0f, topLeft.y + 7.0f), color(ImGuiCol_CheckMark));
  draw->AddText(ImVec2(topLeft.x + 6.0f, bottomRight.y - ImGui::GetTextLineHeight() - 5.0f),
    color(ImGuiCol_TextDisabled), clearLabel);
  const ImVec2 fullSize = ImGui::CalcTextSize(fullLabel);
  draw->AddText(ImVec2(bottomRight.x - fullSize.x - 6.0f,
    bottomRight.y - ImGui::GetTextLineHeight() - 5.0f), color(ImGuiCol_TextDisabled), fullLabel);
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("Drag either boundary");
  return result;
}

UvCanvasResult uvTransformCanvas(const char* id, glm::vec2& offset,
    glm::vec2& scale, float& rotationRadians, const glm::vec2 pivot,
    const unsigned int texture, const float height) {
  UvCanvasResult result;
  const float width = std::max(150.0f, ImGui::GetContentRegionAvail().x);
  const ImVec2 topLeft = ImGui::GetCursorScreenPos();
  const ImVec2 size(width, height);
  ImGui::InvisibleButton(id, size,
    ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
  const bool hovered = ImGui::IsItemHovered();
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
    const ImVec2 delta = ImGui::GetIO().MouseDelta;
    offset.x += delta.x / size.x * 3.0f;
    offset.y -= delta.y / size.y * 3.0f;
    result.offsetChanged = delta.x != 0.0f || delta.y != 0.0f;
  }
  if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
    const float delta = ImGui::GetIO().MouseDelta.x * 0.01f;
    rotationRadians += delta;
    result.rotationChanged = delta != 0.0f;
  }
  if (hovered && ImGui::GetIO().MouseWheel != 0.0f) {
    const float factor = std::pow(1.1f, ImGui::GetIO().MouseWheel);
    scale *= factor;
    result.scaleChanged = true;
  }

  auto toScreen = [&](const glm::vec2 uv) {
    return ImVec2(topLeft.x + (uv.x + 1.0f) / 3.0f * size.x,
      topLeft.y + (2.0f - uv.y) / 3.0f * size.y);
  };
  auto transform = [&](glm::vec2 uv) {
    uv = (uv - pivot) * scale;
    const float cosine = std::cos(rotationRadians);
    const float sine = std::sin(rotationRadians);
    uv = glm::vec2(cosine * uv.x - sine * uv.y, sine * uv.x + cosine * uv.y);
    return uv + pivot + offset;
  };
  ImDrawList* draw = ImGui::GetWindowDrawList();
  const ImVec2 bottomRight(topLeft.x + size.x, topLeft.y + size.y);
  draw->AddRectFilled(topLeft, bottomRight, color(ImGuiCol_FrameBg));
  for (int value = -1; value <= 2; ++value) {
    const ImVec2 vertical = toScreen(glm::vec2(static_cast<float>(value), 0.0f));
    const ImVec2 horizontal = toScreen(glm::vec2(0.0f, static_cast<float>(value)));
    draw->AddLine(ImVec2(vertical.x, topLeft.y), ImVec2(vertical.x, bottomRight.y), color(ImGuiCol_Border, 0.65f));
    draw->AddLine(ImVec2(topLeft.x, horizontal.y), ImVec2(bottomRight.x, horizontal.y), color(ImGuiCol_Border, 0.65f));
  }
  const ImVec2 corners[] = {
    toScreen(transform(glm::vec2(0, 0))), toScreen(transform(glm::vec2(1, 0))),
    toScreen(transform(glm::vec2(1, 1))), toScreen(transform(glm::vec2(0, 1)))};
  if (texture != 0) {
    draw->AddImageQuad(static_cast<ImTextureID>(texture), corners[0], corners[1], corners[2], corners[3],
      ImVec2(0, 1), ImVec2(1, 1), ImVec2(1, 0), ImVec2(0, 0), IM_COL32_WHITE);
  } else {
    draw->AddQuadFilled(corners[0], corners[1], corners[2], corners[3], color(ImGuiCol_Header, 0.8f));
  }
  draw->AddQuad(corners[0], corners[1], corners[2], corners[3], color(ImGuiCol_CheckMark), 2.0f);
  const ImVec2 transformedPivot = toScreen(transform(pivot));
  draw->AddCircleFilled(transformedPivot, 5.0f, color(ImGuiCol_CheckMark));
  draw->AddRect(topLeft, bottomRight, color(hovered ? ImGuiCol_HeaderHovered : ImGuiCol_Border));
  draw->AddText(ImVec2(topLeft.x + 6.0f, topLeft.y + 5.0f), color(ImGuiCol_TextDisabled),
    "drag: move   right-drag: rotate   wheel: scale");
  if (hovered) ImGui::SetMouseCursor(ImGuiMouseCursor_Hand);
  return result;
}

} // namespace gfxlab::ui
