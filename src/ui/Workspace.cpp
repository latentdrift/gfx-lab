#include "ui/Workspace.hpp"

#include "ui/Inspector.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>
#include <imgui_internal.h>
#include <ImGuizmo.h>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace gfxlab::ui {
namespace {

// Bump the workspace ID whenever the supplied dock topology gains a window.
// Dear ImGui otherwise restores the previous topology and leaves the unknown
// tool as a detached platform window, which is especially fragile across DPI
// scales and mixed-monitor coordinate spaces.
constexpr const char* workspaceId = "Graphics Lab Workspace v13";

void buildDefaultLayout(const ImGuiID dockspace, const WorkspaceLayout layout) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

  ImGuiID center = dockspace;
  ImGuiID left = 0;
  ImGuiID right = 0;
  const float leftRatio = 0.32f;
  const float rightRatio = layout == WorkspaceLayout::Analyze ? 0.31f : 0.28f;
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, leftRatio, &left, &center);
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, rightRatio, &right, &center);

  ImGui::DockBuilderDockWindow("Operation Graph", left);
  ImGuiID centerTop = center;
  if (layout == WorkspaceLayout::Animate || layout == WorkspaceLayout::Analyze) {
    ImGuiID centerBottom = 0;
    const float bottomRatio = layout == WorkspaceLayout::Animate ? 0.46f : 0.30f;
    ImGui::DockBuilderSplitNode(centerTop, ImGuiDir_Down, bottomRatio, &centerBottom, &centerTop);
    if (layout == WorkspaceLayout::Animate) ImGui::DockBuilderDockWindow("Timeline", centerBottom);
    else ImGui::DockBuilderDockWindow("Scope", centerBottom);
  }
  ImGui::DockBuilderDockWindow("Canvas", centerTop);
  ImGui::DockBuilderDockWindow("Properties", right);
  if (layout == WorkspaceLayout::Analyze) {
    ImGui::DockBuilderDockWindow("Signal Inspector", right);
  }
  ImGui::DockBuilderFinish(dockspace);
}

void windowMenu(WorkspaceWindows& windows) {
  if (ImGui::BeginMenu("Workspace Layout")) {
    if (ImGui::MenuItem("Edit")) {
      windows.requestedLayout = WorkspaceLayout::Edit;
      windows.resetLayout = true;
    }
    if (ImGui::MenuItem("Animate")) {
      windows.requestedLayout = WorkspaceLayout::Animate;
      windows.animation = true;
      windows.resetLayout = true;
    }
    if (ImGui::MenuItem("Analyze")) {
      windows.requestedLayout = WorkspaceLayout::Analyze;
      windows.textureInspector = true;
      windows.resetLayout = true;
    }
    ImGui::EndMenu();
  }
  ImGui::Separator();
  ImGui::MenuItem("Operation Graph", nullptr, &windows.document);
  ImGui::MenuItem("Canvas", nullptr, &windows.viewport);
  ImGui::MenuItem("Properties", nullptr, &windows.inspector);
  ImGui::MenuItem("Scope", nullptr, &windows.scope);
  ImGui::MenuItem("Timeline", nullptr, &windows.animation);
  ImGui::MenuItem("Signal Inspector", nullptr, &windows.textureInspector);
  ImGui::Separator();
  if (ImGui::MenuItem("Restore Edit Layout")) {
    windows.requestedLayout = WorkspaceLayout::Edit;
    windows.resetLayout = true;
  }
}

} // namespace

WorkspaceActions beginWorkspace(WorkspaceWindows& windows, const bool canUndo, const bool canRedo,
    float& uiScale, const bool viewportRecording, const double recordingDurationSeconds,
    HardwareProfile& profile) {
  WorkspaceActions actions;
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Import Model...")) actions.importModel = true;
      if (ImGui::MenuItem("Open Document...", "Ctrl+O")) actions.loadJson = true;
      if (ImGui::MenuItem("Save Document...", "Ctrl+S")) actions.saveJson = true;
      if (ImGui::MenuItem("Copy Document JSON")) actions.copyJson = true;
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) actions.quit = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) actions.undo = true;
      if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, canRedo)) actions.redo = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("View")) {
      if (ImGui::BeginMenu("UI Scale")) {
        constexpr std::array<float, 8> scales = {0.75f, 0.9f, 1.0f, 1.1f, 1.25f, 1.5f, 1.75f, 2.0f};
        constexpr std::array<const char*, 8> labels = {
          "75%", "90%", "100%", "110%", "125%", "150%", "175%", "200%"};
        for (std::size_t index = 0; index < scales.size(); ++index)
          if (ImGui::MenuItem(labels[index], nullptr, std::abs(uiScale - scales[index]) < 0.001f))
            uiScale = scales[index];
        ImGui::Separator();
        ImGui::TextDisabled("Ctrl+- / Ctrl+= / Ctrl+0");
        ImGui::EndMenu();
      }
      ImGui::Separator();
      if (ImGui::MenuItem("Reset Persistent State")) actions.resetFrameHistory = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Window")) {
      windowMenu(windows);
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Help")) {
      if (ImGui::MenuItem("Graphics Handbook")) actions.handbook = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Advanced")) {
      int profileIndex = static_cast<int>(profile);
      constexpr const char* profileLabels[] = {"Unrestricted", "PlayStation (PS1)", "Nintendo 64"};
      ImGui::SetNextItemWidth(180.0f);
      if (ImGui::Combo("Hardware target", &profileIndex, profileLabels, 3)) {
        profile = static_cast<HardwareProfile>(profileIndex);
        actions.hardwareProfileChanged = true;
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hardwareProfileDescription(profile));
      ImGui::EndMenu();
    }
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    if (viewportRecording) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.12f, 0.10f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.62f, 0.16f, 0.13f, 1.0f));
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.72f, 0.20f, 0.16f, 1.0f));
      std::array<char, 64> label{};
      std::snprintf(label.data(), label.size(), "Stop Recording  %02d:%02d",
        static_cast<int>(recordingDurationSeconds) / 60,
        static_cast<int>(recordingDurationSeconds) % 60);
      if (ImGui::Button(label.data())) actions.toggleViewportRecording = true;
      ImGui::PopStyleColor(3);
    } else if (ImGui::Button("Record Viewport")) {
      actions.toggleViewportRecording = true;
    }
    ImGui::EndMainMenuBar();
  }

  const ImGuiID dockspace = ImGui::GetID(workspaceId);
  const bool layoutMissing = ImGui::DockBuilderGetNode(dockspace) == nullptr;
  ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  if (windows.resetLayout || layoutMissing) {
    buildDefaultLayout(dockspace, windows.requestedLayout);
    windows.resetLayout = false;
  }
  return actions;
}

ViewportWindowResult drawViewportWindow(bool& open, const ViewportImages& images,
    editor::SignalViewerState& viewer, document::Document& document,
    editor::EditorState& editorState, editor::CommandHistory& history) {
  ViewportWindowResult result;
  if (!open) return result;
  if (!ImGui::Begin("Canvas", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return result;
  }
  keepCurrentWindowVisible();

  enum class Tool { Orbit, Translate, Scale };
  static Tool tool = Tool::Orbit;

  document::Operation* selectedOperation = editorState.selection.kind == editor::SelectionKind::Operation
    ? document::findOperation(document, editorState.selection.operation) : nullptr;
  document::RenderOperation* selectedRender = selectedOperation == nullptr ? nullptr
    : std::get_if<document::RenderOperation>(&selectedOperation->data);

  const bool viewingFinal = viewer.viewed == document.presentation.input;
  if (ImGui::Button(viewingFinal ? "Final ✓" : "Final")) {
    viewer.viewed = document.presentation.input;
    viewer.mode = editor::ViewerMode::Single;
  }
  ImGui::SameLine();
  if (ImGui::RadioButton("Single", viewer.mode == editor::ViewerMode::Single))
    viewer.mode = editor::ViewerMode::Single;
  ImGui::SameLine();
  const bool hasComparison = viewer.comparison.has_value() && images.comparison != 0;
  ImGui::BeginDisabled(!hasComparison);
  if (ImGui::RadioButton("Split", viewer.mode == editor::ViewerMode::Split))
    viewer.mode = editor::ViewerMode::Split;
  ImGui::SameLine();
  if (ImGui::RadioButton("Difference", viewer.mode == editor::ViewerMode::AbsoluteDifference))
    viewer.mode = editor::ViewerMode::AbsoluteDifference;
  ImGui::SameLine();
  if (ImGui::RadioButton("Flicker", viewer.mode == editor::ViewerMode::Flicker))
    viewer.mode = editor::ViewerMode::Flicker;
  ImGui::EndDisabled();
  if (!hasComparison && viewer.mode != editor::ViewerMode::Single) viewer.mode = editor::ViewerMode::Single;
  ImGui::SameLine();
  ImGui::Checkbox("Presentation", &viewer.applyPresentation);
  ImGui::SameLine();
  ImGui::TextDisabled("Viewing: %s", images.viewedLabel);
  if (hasComparison) {
    ImGui::SameLine();
    ImGui::TextDisabled("Compare: %s", images.comparisonLabel);
  }
  if (selectedOperation != nullptr) {
    ImGui::SameLine();
    ImGui::TextDisabled("Selected: %s", selectedOperation->name.c_str());
  }
  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();
  if (ImGui::RadioButton("Orbit", tool == Tool::Orbit)) tool = Tool::Orbit;
  ImGui::SameLine();
  ImGui::BeginDisabled(selectedRender == nullptr || viewer.mode != editor::ViewerMode::Single);
  if (ImGui::RadioButton("Translate", tool == Tool::Translate)) tool = Tool::Translate;
  ImGui::SameLine();
  if (ImGui::RadioButton("Scale", tool == Tool::Scale)) tool = Tool::Scale;
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", selectedRender != nullptr ? "Selected Render transform"
    : "Select a Render to transform it");
  ImGui::Separator();

  const ImVec2 available = ImGui::GetContentRegionAvail();
  const ImVec2 paneOrigin = ImGui::GetCursorScreenPos();
  constexpr float cameraWidth = 960.0f;
  constexpr float cameraHeight = 720.0f;
  const float scale = std::max(0.01f, std::min(available.x / cameraWidth, available.y / cameraHeight));
  const ImVec2 size(cameraWidth * scale, cameraHeight * scale);
  const ImVec2 origin(paneOrigin.x + std::floor((available.x - size.x) * 0.5f),
    paneOrigin.y + std::floor((available.y - size.y) * 0.5f));
  const ImVec2 end(origin.x + size.x, origin.y + size.y);
  ImDrawList* draw = ImGui::GetWindowDrawList();
  draw->AddRectFilled(origin, end, IM_COL32(27, 29, 31, 255));
  if (viewer.mode == editor::ViewerMode::Split) {
    const float eyeHeight = size.y;
    const float pairTop = origin.y;
    const ImVec2 pairOrigin(origin.x, pairTop);
    const ImVec2 pairEnd(end.x, pairTop + eyeHeight);
    const float middle = pairOrigin.x + std::floor(size.x * 0.5f);
    const unsigned int leftTexture = images.comparison;
    const unsigned int rightTexture = images.viewed;
    draw->PushClipRect(pairOrigin, ImVec2(middle, pairEnd.y), true);
    draw->AddImage(static_cast<ImTextureID>(leftTexture), pairOrigin, ImVec2(middle, pairEnd.y),
      ImVec2(0, 1), ImVec2(1, 0));
    draw->PopClipRect();
    draw->PushClipRect(ImVec2(middle + 1, pairOrigin.y), pairEnd, true);
    draw->AddImage(static_cast<ImTextureID>(rightTexture), ImVec2(middle + 1, pairOrigin.y), pairEnd,
      ImVec2(0, 1), ImVec2(1, 0));
    draw->PopClipRect();
    draw->AddLine(ImVec2(middle, pairOrigin.y), ImVec2(middle, pairEnd.y), IM_COL32(225, 225, 225, 210));
    draw->AddText(ImVec2(pairOrigin.x + 9, pairOrigin.y + 8), IM_COL32(240, 240, 240, 220), "COMPARE");
    draw->AddText(ImVec2(middle + 10, pairOrigin.y + 8), IM_COL32(240, 240, 240, 220), "VIEWING");
  } else {
    const unsigned int texture = viewer.mode == editor::ViewerMode::AbsoluteDifference ? images.difference
      : viewer.mode == editor::ViewerMode::Flicker && std::fmod(ImGui::GetTime(), 0.6) < 0.3
        ? images.comparison : images.viewed;
    draw->AddImage(static_cast<ImTextureID>(texture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
    const char* label = viewer.mode == editor::ViewerMode::AbsoluteDifference ? "ABSOLUTE DIFFERENCE"
      : viewer.mode == editor::ViewerMode::Flicker ? "FLICKER COMPARISON" : "VIEWING";
    draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240, 240, 240, 220), label);
  }
  // ImGuizmo deliberately refuses activation while a normal ImGui item is hovered. Keep the viewport
  // interaction surface item-free and perform its hit test directly, otherwise a full-size InvisibleButton
  // makes a visible gizmo impossible to grab.
  result.hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin, end);

  const bool gizmoVisible = selectedRender != nullptr && viewer.mode == editor::ViewerMode::Single &&
    tool != Tool::Orbit;
  result.acceptsCameraInput = result.hovered && !gizmoVisible;
  if (gizmoVisible) {
    const RendererState& state = document.renderDefaults.renderer;
    constexpr float aspect = cameraWidth / cameraHeight;
    const PassCameraMatrices passCamera = buildPassCamera(document.scene.authoredCamera, state,
      selectedRender->perturbation, aspect);
    const glm::mat4& view = passCamera.view;
    const glm::mat4& projection = passCamera.projection;
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), selectedRender->perturbation.modelTranslation) *
      glm::scale(glm::mat4(1.0f), glm::vec3(std::max(0.01f, selectedRender->perturbation.modelScale)));

    ImGuizmo::SetOrthographic(state.camera.orthographic);
    ImGuizmo::SetDrawlist(draw);
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    const ImGuizmo::OPERATION operation = tool == Tool::Translate ? ImGuizmo::TRANSLATE : ImGuizmo::SCALEU;
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, ImGuizmo::WORLD,
      glm::value_ptr(transform));
    result.gizmoUsing = ImGuizmo::IsUsing();
    if (result.gizmoUsing) {
      document::Document edited = document;
      document::Operation* editedOperation = document::findOperation(edited, selectedOperation->id);
      auto* editedRender = editedOperation == nullptr ? nullptr
        : std::get_if<document::RenderOperation>(&editedOperation->data);
      if (editedRender == nullptr) {
        ImGui::End();
        return result;
      }
      editedRender->perturbation.modelTranslation = glm::vec3(transform[3]);
      const glm::vec3 scale(glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])),
        glm::length(glm::vec3(transform[2])));
      editedRender->perturbation.modelScale = std::clamp((scale.x + scale.y + scale.z) / 3.0f,
        0.01f, 8.0f);
      static_cast<void>(history.executeContinuous(document,
        editor::ReplaceDocument{std::move(edited)}));
    }
  } else if (!ImGui::IsAnyItemActive()) {
    history.finishContinuous(document);
  }
  ImGui::End();
  return result;
}

} // namespace gfxlab::ui
