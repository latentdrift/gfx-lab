#include "ui/Workspace.hpp"

#include "assets/ModelAsset.hpp"
#include "ui/AnimationControls.hpp"
#include "ui/Inspector.hpp"

#include <imgui.h>
#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace gfxlab::ui {
namespace {

constexpr const char* workspaceId = "Graphics Lab Workspace v2";

void buildDefaultLayout(const ImGuiID dockspace) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

  ImGuiID center = dockspace;
  ImGuiID left = 0;
  ImGuiID right = 0;
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.19f, &left, &center);
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.25f, &right, &center);

  ImGuiID leftBottom = 0;
  ImGuiID leftTop = left;
  ImGui::DockBuilderSplitNode(leftTop, ImGuiDir_Down, 0.48f, &leftBottom, &leftTop);
  ImGui::DockBuilderDockWindow("Scene", leftTop);
  ImGui::DockBuilderDockWindow("Render Passes", leftTop);
  ImGui::DockBuilderDockWindow("Pipeline", leftBottom);
  ImGui::DockBuilderDockWindow("Viewport", center);
  ImGui::DockBuilderDockWindow("Pipeline Inspector", right);
  ImGui::DockBuilderDockWindow("Pass difference audit", right);
  ImGui::DockBuilderFinish(dockspace);
}

void windowMenu(WorkspaceWindows& windows) {
  ImGui::MenuItem("Scene", nullptr, &windows.scene);
  ImGui::MenuItem("Render Passes", nullptr, &windows.renderPasses);
  ImGui::MenuItem("Pipeline", nullptr, &windows.pipeline);
  ImGui::MenuItem("Viewport", nullptr, &windows.viewport);
  ImGui::MenuItem("Pipeline Inspector", nullptr, &windows.inspector);
  ImGui::MenuItem("Animation Timeline", nullptr, &windows.animation);
  ImGui::MenuItem("Pass Differences", nullptr, &windows.passDifferences);
  ImGui::Separator();
  if (ImGui::MenuItem("Restore Default Layout")) windows.resetLayout = true;
}

} // namespace

WorkspaceActions beginWorkspace(WorkspaceWindows& windows, const bool canUndo, const bool canRedo) {
  WorkspaceActions actions;
  if (ImGui::BeginMainMenuBar()) {
    if (ImGui::BeginMenu("File")) {
      if (ImGui::MenuItem("Import Model...")) actions.importModel = true;
      if (ImGui::MenuItem("Copy Stack JSON")) actions.copyJson = true;
      ImGui::Separator();
      if (ImGui::MenuItem("Quit")) actions.quit = true;
      ImGui::EndMenu();
    }
    if (ImGui::BeginMenu("Edit")) {
      if (ImGui::MenuItem("Undo", "Ctrl+Z", false, canUndo)) actions.undo = true;
      if (ImGui::MenuItem("Redo", "Ctrl+Shift+Z", false, canRedo)) actions.redo = true;
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
    ImGui::EndMainMenuBar();
  }

  const ImGuiID dockspace = ImGui::GetID(workspaceId);
  const bool layoutMissing = ImGui::DockBuilderGetNode(dockspace) == nullptr;
  ImGui::DockSpaceOverViewport(dockspace, ImGui::GetMainViewport(), ImGuiDockNodeFlags_PassthruCentralNode);
  if (windows.resetLayout || layoutMissing) {
    buildDefaultLayout(dockspace);
    windows.resetLayout = false;
  }
  return actions;
}

SceneWindowResult drawSceneWindow(bool& open, TestScene& scene, HardwareProfile& profile,
    const ModelAsset* importedModel) {
  SceneWindowResult result;
  if (!open) return result;
  if (!ImGui::Begin("Scene", &open)) {
    ImGui::End();
    return result;
  }

  ImGui::TextDisabled("HARDWARE TARGET");
  int profileIndex = static_cast<int>(profile);
  const char* profileLabels[] = {"Unrestricted", "PlayStation (PS1)", "Nintendo 64"};
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::Combo("##hardware-profile", &profileIndex, profileLabels, 3)) {
    profile = static_cast<HardwareProfile>(profileIndex);
    result.hardwareProfileChanged = true;
  }
  if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hardwareProfileDescription(profile));

  ImGui::Spacing();
  ImGui::TextDisabled("SCENE");
  constexpr std::array<const char*, 6> sceneLabels = {"Torus", "Texture minification", "Depth precision",
    "Transparency", "Lighting comparison", "Stencil mask"};
  const char* currentLabel = scene == TestScene::ImportedModel && importedModel != nullptr
    ? importedModel->name.c_str() : sceneLabels[std::min(static_cast<int>(scene), 5)];
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##scene", currentLabel)) {
    for (int option = 0; option < 5; ++option) {
      const TestScene candidate = static_cast<TestScene>(option);
      if (ImGui::Selectable(sceneLabels[option], scene == candidate)) scene = candidate;
    }
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[5], scene == TestScene::StencilMask))
      scene = TestScene::StencilMask;
    if (importedModel != nullptr &&
        ImGui::Selectable(importedModel->name.c_str(), scene == TestScene::ImportedModel))
      scene = TestScene::ImportedModel;
    ImGui::EndCombo();
  }
  if (ImGui::Button("Import Model...", ImVec2(-1, 0))) result.importModel = true;

  if (importedModel != nullptr) {
    ImGui::Separator();
    ImGui::TextDisabled("GLOBAL MODEL");
    ImGui::TextWrapped("%s", importedModel->name.c_str());
    ImGui::TextDisabled("%zu triangles   %zu meshes", importedModel->triangleCount,
      importedModel->sourceMeshCount);
    ImGui::TextDisabled("%zu materials   %zu textures", importedModel->materials.size(),
      importedModel->textures.size());
    ImGui::TextDisabled("UV0 %s   colors %s", importedModel->hasTextureCoordinates ? "yes" : "no",
      importedModel->hasVertexColors ? "yes" : "no");
    if (!importedModel->importWarnings.empty()) {
      ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.34f, 1.0f), "%zu texture warning%s",
        importedModel->importWarnings.size(), importedModel->importWarnings.size() == 1 ? "" : "s");
      if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        for (const std::string& warning : importedModel->importWarnings) ImGui::TextWrapped("%s", warning.c_str());
        ImGui::EndTooltip();
      }
    }
    if (scene != TestScene::ImportedModel && ImGui::Button("Use Model", ImVec2(-1, 0)))
      scene = TestScene::ImportedModel;
    if (ImGui::Button("Unload Model", ImVec2(-1, 0))) result.unloadModel = true;
  }
  ImGui::End();
  return result;
}

void drawRenderPassesWindow(bool& open, RenderStack& stack, AnimationTimeline& timeline,
    bool& globalScope) {
  if (!open) return;
  if (!ImGui::Begin("Render Passes", &open)) {
    ImGui::End();
    return;
  }
  ImGui::TextDisabled("BOTTOM TO TOP");
  for (std::size_t index = 0; index < stack.passes().size(); ++index) {
    RenderPass& pass = stack.passes()[index];
    ImGui::PushID(static_cast<int>(index));
    const bool changed = ImGui::Checkbox("##enabled", &pass.enabled);
    animationKeyControl(pass, AnimationProperty::PassEnabled, timeline, changed);
    ImGui::SameLine();
    const std::string label = pass.name + "  [" + std::to_string(pass.overrides.size()) + " local, " +
      std::to_string(pass.animation.tracks.size()) + " tracks]";
    if (ImGui::Selectable(label.c_str(), stack.selectedIndex() == index, 0, ImVec2(0, 24))) {
      stack.select(index);
      globalScope = false;
    }
    ImGui::PopID();
  }
  if (ImGui::Button("Duplicate", ImVec2(-1, 0))) {
    stack.duplicateSelected();
    globalScope = false;
  }
  if (ImGui::Button("Up")) stack.moveSelected(-1);
  ImGui::SameLine();
  if (ImGui::Button("Down")) stack.moveSelected(1);
  ImGui::SameLine();
  ImGui::BeginDisabled(stack.passes().size() <= 1);
  if (ImGui::Button("Delete")) stack.removeSelected();
  ImGui::EndDisabled();
  ImGui::End();
}

void drawPipelineWindow(bool& open, Category& category, const HardwareProfile profile) {
  if (!open) return;
  if (!ImGui::Begin("Pipeline", &open)) {
    ImGui::End();
    return;
  }
  constexpr std::array<Category, 11> categories = {Category::Geometry, Category::Camera, Category::Rasterization,
    Category::Surface, Category::Texture, Category::Lighting, Category::Depth, Category::Stencil, Category::Color,
    Category::Post, Category::Output};
  for (const Category candidate : categories) {
    if (!categoryAvailableForHardwareProfile(profile, candidate)) continue;
    if (ImGui::Selectable(categoryName(candidate), category == candidate, 0, ImVec2(0, 28))) category = candidate;
  }
  ImGui::End();
}

bool drawViewportWindow(bool& open, const ViewportImages& images, CompareMode& compare,
    const RenderStack& stack) {
  if (!open) return false;
  if (!ImGui::Begin("Viewport", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return false;
  }

  if (ImGui::RadioButton("Selected", compare == CompareMode::A)) compare = CompareMode::A;
  ImGui::SameLine();
  if (ImGui::RadioButton("Base", compare == CompareMode::B)) compare = CompareMode::B;
  ImGui::SameLine();
  if (ImGui::RadioButton("Split", compare == CompareMode::Split)) compare = CompareMode::Split;
  ImGui::SameLine();
  if (ImGui::RadioButton("Composite", compare == CompareMode::Relation)) compare = CompareMode::Relation;
  ImGui::SameLine();
  ImGui::TextDisabled("LMB orbit   MMB/RMB pan   Wheel zoom");
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
  if (compare == CompareMode::Split) {
    const float middle = origin.x + std::floor(size.x * 0.5f);
    draw->PushClipRect(origin, ImVec2(middle, end.y), true);
    draw->AddImage(static_cast<ImTextureID>(images.base), origin, end, ImVec2(0, 1), ImVec2(1, 0));
    draw->PopClipRect();
    draw->PushClipRect(ImVec2(middle + 1, origin.y), end, true);
    draw->AddImage(static_cast<ImTextureID>(images.selected), origin, end, ImVec2(0, 1), ImVec2(1, 0));
    draw->PopClipRect();
    draw->AddLine(ImVec2(middle, origin.y), ImVec2(middle, end.y), IM_COL32(225, 225, 225, 210));
    draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240, 240, 240, 220), "BASE PASS");
    draw->AddText(ImVec2(middle + 10, origin.y + 8), IM_COL32(240, 240, 240, 220),
      stack.selected().name.c_str());
  } else {
    const unsigned int texture = compare == CompareMode::A ? images.selected
      : compare == CompareMode::Relation ? images.composite : images.base;
    draw->AddImage(static_cast<ImTextureID>(texture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
    const char* label = compare == CompareMode::A ? stack.selected().name.c_str()
      : compare == CompareMode::B ? "BASE PASS" : "COMPOSITE STACK";
    draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240, 240, 240, 220), label);
  }
  ImGui::SetCursorScreenPos(origin);
  ImGui::InvisibleButton("viewport-input", size);
  const bool hovered = ImGui::IsItemHovered();
  ImGui::End();
  return hovered;
}

} // namespace gfxlab::ui
