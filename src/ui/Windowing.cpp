#include "ui/Windowing.hpp"

#include <imgui.h>

#include <algorithm>

namespace gfxlab::ui {

void keepCurrentWindowVisible() {
  if (ImGui::IsWindowDocked()) return;
  const ImVec2 position = ImGui::GetWindowPos();
  const ImVec2 size = ImGui::GetWindowSize();
  const float titleHeight = ImGui::GetFrameHeight();
  const ImGuiPlatformIO& platform = ImGui::GetPlatformIO();
  const ImGuiViewport* main = ImGui::GetMainViewport();
  const auto visibleOn = [&](const ImVec2 workPosition, const ImVec2 workSize) {
    const float overlapX = std::max(0.0f, std::min(position.x + size.x, workPosition.x +
      workSize.x) - std::max(position.x, workPosition.x));
    const float overlapY = std::max(0.0f, std::min(position.y + titleHeight, workPosition.y +
      workSize.y) - std::max(position.y, workPosition.y));
    if (overlapX >= std::min(80.0f, size.x * 0.5f) && overlapY >= titleHeight * 0.5f) {
      return true;
    }
    return false;
  };

  bool recoverable = false;
  if ((ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) == 0) {
    // Without native platform viewports, window positions are relative to the main ImGui viewport.
    // Comparing them with desktop-global monitor coordinates would introduce the same kind of
    // coordinate-space mismatch this recovery path is meant to repair.
    recoverable = visibleOn(main->WorkPos, main->WorkSize);
  } else {
    for (const ImGuiPlatformMonitor& monitor : platform.Monitors) {
      if (visibleOn(monitor.WorkPos, monitor.WorkSize)) {
        recoverable = true;
        break;
      }
    }
    if (platform.Monitors.empty()) recoverable = visibleOn(main->WorkPos, main->WorkSize);
  }
  if (recoverable) return;

  const ImVec2 recoveredSize(std::min(size.x, main->WorkSize.x * 0.9f),
    std::min(size.y, main->WorkSize.y * 0.9f));
  ImGui::SetWindowSize(recoveredSize);
  ImGui::SetWindowPos(ImVec2(main->WorkPos.x + (main->WorkSize.x - recoveredSize.x) * 0.5f,
    main->WorkPos.y + (main->WorkSize.y - recoveredSize.y) * 0.5f));
}

} // namespace gfxlab::ui
