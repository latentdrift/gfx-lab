#include "ui/Workspace.hpp"

#include "assets/ModelAsset.hpp"
#include "ui/AnimationControls.hpp"
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
#include <string>

namespace gfxlab::ui {
namespace {

constexpr const char* workspaceId = "Graphics Lab Workspace v6";

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
  ImGui::DockBuilderDockWindow("Render Passes", leftBottom);
  ImGui::DockBuilderDockWindow("Viewport", center);
  ImGuiID rightBottom = 0;
  ImGuiID rightTop = right;
  ImGui::DockBuilderSplitNode(rightTop, ImGuiDir_Down, 0.34f, &rightBottom, &rightTop);
  for (const Category category : pipelineCategories)
    ImGui::DockBuilderDockWindow(pipelineToolWindowName(category), rightTop);
  ImGui::DockBuilderDockWindow("Display Reconstruction", rightTop);
  ImGui::DockBuilderDockWindow("Texture Mapping", rightTop);
  ImGui::DockBuilderDockWindow("Pass difference audit", rightTop);
  ImGui::DockBuilderDockWindow("Pass Properties", rightBottom);
  ImGui::DockBuilderFinish(dockspace);
}

void windowMenu(WorkspaceWindows& windows) {
  ImGui::MenuItem("Scene", nullptr, &windows.scene);
  ImGui::MenuItem("Render Passes", nullptr, &windows.renderPasses);
  ImGui::MenuItem("Viewport", nullptr, &windows.viewport);
  if (ImGui::BeginMenu("Pipeline Tools")) {
    if (ImGui::MenuItem("Show All")) windows.pipelineTools.open.fill(true);
    if (ImGui::MenuItem("Hide All")) windows.pipelineTools.open.fill(false);
    ImGui::Separator();
    for (std::size_t index = 0; index < pipelineCategories.size(); ++index)
      ImGui::MenuItem(categoryName(pipelineCategories[index]), nullptr, &windows.pipelineTools.open[index]);
    ImGui::EndMenu();
  }
  ImGui::MenuItem("Pass Properties", nullptr, &windows.passProperties);
  ImGui::MenuItem("Animation Timeline", nullptr, &windows.animation);
  ImGui::MenuItem("Pass Differences", nullptr, &windows.passDifferences);
  ImGui::MenuItem("Texture Inspector", nullptr, &windows.textureInspector);
  ImGui::MenuItem("Texture Mapping", nullptr, &windows.textureMapping);
  ImGui::MenuItem("Display Reconstruction", nullptr, &windows.displayReconstruction);
  ImGui::Separator();
  if (ImGui::MenuItem("Restore Default Layout")) windows.resetLayout = true;
}

} // namespace

WorkspaceActions beginWorkspace(WorkspaceWindows& windows, const bool canUndo, const bool canRedo,
    float& uiScale) {
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
      if (ImGui::MenuItem("Reset Frame History")) actions.resetFrameHistory = true;
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
  keepCurrentWindowVisible();

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
  keepCurrentWindowVisible();
  ImGui::TextDisabled("BOTTOM TO TOP");
  for (std::size_t index = 0; index < stack.passes().size(); ++index) {
    RenderPass& pass = stack.passes()[index];
    ImGui::PushID(static_cast<int>(index));
    if (ImGui::Selectable(pass.name.c_str(), stack.selectedIndex() == index, 0, ImVec2(0.0f, 22.0f))) {
      stack.select(index);
      globalScope = false;
    }
    ImGui::TextDisabled("%zu local override%s   %zu track%s", pass.overrides.size(),
      pass.overrides.size() == 1 ? "" : "s", pass.animation.tracks.size(),
      pass.animation.tracks.size() == 1 ? "" : "s");
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    ImGui::SameLine();
    ImGui::SetCursorScreenPos(ImVec2(right - 48.0f, ImGui::GetItemRectMin().y - 2.0f));
    const bool changed = ImGui::Checkbox("##enabled", &pass.enabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Pass enabled");
    animationKeyControl(pass, AnimationProperty::PassEnabled, timeline, changed);
    if (index + 1 < stack.passes().size()) ImGui::Separator();
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

ViewportWindowResult drawViewportWindow(bool& open, const ViewportImages& images, CompareMode& compare,
    const RenderStack& stack, RenderPass& displayedPass, const CameraOrbit& camera,
    AnimationTimeline& timeline, const bool globalScope) {
  ViewportWindowResult result;
  if (!open) return result;
  if (!ImGui::Begin("Viewport", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return result;
  }
  keepCurrentWindowVisible();

  enum class Tool { Orbit, Translate, Scale };
  static Tool tool = Tool::Orbit;

  if (ImGui::RadioButton("Selected", compare == CompareMode::A)) compare = CompareMode::A;
  ImGui::SameLine();
  if (ImGui::RadioButton("Base", compare == CompareMode::B)) compare = CompareMode::B;
  ImGui::SameLine();
  if (ImGui::RadioButton("Split", compare == CompareMode::Split)) compare = CompareMode::Split;
  ImGui::SameLine();
  if (ImGui::RadioButton("Composite", compare == CompareMode::Relation)) compare = CompareMode::Relation;
  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();
  if (ImGui::RadioButton("Orbit", tool == Tool::Orbit)) tool = Tool::Orbit;
  ImGui::SameLine();
  ImGui::BeginDisabled(compare != CompareMode::A);
  if (ImGui::RadioButton("Translate", tool == Tool::Translate)) tool = Tool::Translate;
  ImGui::SameLine();
  if (ImGui::RadioButton("Scale", tool == Tool::Scale)) tool = Tool::Scale;
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%s transform", globalScope ? "Global base" : "Selected pass");
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
  // ImGuizmo deliberately refuses activation while a normal ImGui item is hovered. Keep the viewport
  // interaction surface item-free and perform its hit test directly, otherwise a full-size InvisibleButton
  // makes a visible gizmo impossible to grab.
  result.hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin, end);

  const bool gizmoVisible = compare == CompareMode::A && tool != Tool::Orbit;
  result.acceptsCameraInput = result.hovered && !gizmoVisible;
  if (gizmoVisible) {
    CameraOrbit passCamera = camera;
    passCamera.yaw += displayedPass.perturbation.cameraYaw;
    passCamera.pitch = std::clamp(passCamera.pitch + displayedPass.perturbation.cameraPitch, -1.45f, 1.45f);
    passCamera.distance = std::clamp(passCamera.distance + displayedPass.perturbation.cameraDistance, 1.4f, 14.0f);
    const glm::mat4 view = passCamera.view();
    const RendererState& state = displayedPass.renderer;
    constexpr float aspect = cameraWidth / cameraHeight;
    const float halfHeight = state.camera.orthographicSize * 0.5f;
    const glm::mat4 projection = state.camera.orthographic
      ? glm::ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight,
          state.camera.nearPlane, 100.0f)
      : glm::perspective(glm::radians(std::clamp(state.camera.fieldOfView +
          displayedPass.perturbation.fieldOfView, 5.0f, 150.0f)), aspect,
          state.camera.nearPlane, 100.0f);
    glm::mat4 transform = glm::translate(glm::mat4(1.0f), displayedPass.perturbation.modelTranslation) *
      glm::scale(glm::mat4(1.0f), glm::vec3(std::max(0.01f, displayedPass.perturbation.modelScale)));
    const glm::vec3 previousTranslation = displayedPass.perturbation.modelTranslation;
    const float previousScale = displayedPass.perturbation.modelScale;

    ImGuizmo::SetOrthographic(state.camera.orthographic);
    ImGuizmo::SetDrawlist(draw);
    ImGuizmo::SetRect(origin.x, origin.y, size.x, size.y);
    const ImGuizmo::OPERATION operation = tool == Tool::Translate ? ImGuizmo::TRANSLATE : ImGuizmo::SCALEU;
    ImGuizmo::Manipulate(glm::value_ptr(view), glm::value_ptr(projection), operation, ImGuizmo::WORLD,
      glm::value_ptr(transform));
    result.gizmoUsing = ImGuizmo::IsUsing();
    if (result.gizmoUsing) {
      displayedPass.perturbation.modelTranslation = glm::vec3(transform[3]);
      const glm::vec3 scale(glm::length(glm::vec3(transform[0])), glm::length(glm::vec3(transform[1])),
        glm::length(glm::vec3(transform[2])));
      displayedPass.perturbation.modelScale = std::clamp((scale.x + scale.y + scale.z) / 3.0f, 0.01f, 8.0f);
      const bool translationChanged = glm::length(displayedPass.perturbation.modelTranslation -
        previousTranslation) > 0.000001f;
      const bool scaleChanged = std::abs(displayedPass.perturbation.modelScale - previousScale) > 0.000001f;
      recordPropertyAnimationEdit(displayedPass, AnimationProperty::ModelTranslation, timeline,
        translationChanged);
      recordPropertyAnimationEdit(displayedPass, AnimationProperty::ModelScale, timeline, scaleChanged);
    }
  }
  ImGui::End();
  return result;
}

} // namespace gfxlab::ui
