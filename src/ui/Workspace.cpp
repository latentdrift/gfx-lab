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
#include <cstdio>
#include <string>

namespace gfxlab::ui {
namespace {

// Bump the workspace ID whenever the supplied dock topology gains a window.
// Dear ImGui otherwise restores the previous topology and leaves the unknown
// tool as a detached platform window, which is especially fragile across DPI
// scales and mixed-monitor coordinate spaces.
constexpr const char* workspaceId = "Graphics Lab Workspace v11";

void buildDefaultLayout(const ImGuiID dockspace, const WorkspaceLayout layout) {
  const ImGuiViewport* viewport = ImGui::GetMainViewport();
  ImGui::DockBuilderRemoveNode(dockspace);
  ImGui::DockBuilderAddNode(dockspace, ImGuiDockNodeFlags_DockSpace);
  ImGui::DockBuilderSetNodeSize(dockspace, viewport->WorkSize);

  ImGuiID center = dockspace;
  ImGuiID left = 0;
  ImGuiID right = 0;
  const float leftRatio = 0.20f;
  const float rightRatio = layout == WorkspaceLayout::Analyze ? 0.31f : 0.28f;
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, leftRatio, &left, &center);
  ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, rightRatio, &right, &center);

  ImGui::DockBuilderDockWindow("Document", left);
  ImGuiID centerBottom = 0;
  ImGuiID centerTop = center;
  const float timelineRatio = layout == WorkspaceLayout::Animate ? 0.46f : 0.30f;
  ImGui::DockBuilderSplitNode(centerTop, ImGuiDir_Down, timelineRatio, &centerBottom, &centerTop);
  ImGui::DockBuilderDockWindow("Viewport", centerTop);
  ImGui::DockBuilderDockWindow("Timeline", centerBottom);
  ImGui::DockBuilderDockWindow("Inspector", right);
  if (layout == WorkspaceLayout::Analyze) {
    ImGui::DockBuilderDockWindow("Signal Inspector", right);
    ImGui::DockBuilderDockWindow("Property Comparison", right);
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
      windows.passDifferences = true;
      windows.resetLayout = true;
    }
    ImGui::EndMenu();
  }
  ImGui::Separator();
  ImGui::MenuItem("Document", nullptr, &windows.document);
  ImGui::MenuItem("Viewport", nullptr, &windows.viewport);
  ImGui::MenuItem("Inspector", nullptr, &windows.inspector);
  ImGui::MenuItem("Timeline", nullptr, &windows.animation);
  ImGui::MenuItem("Property Comparison", nullptr, &windows.passDifferences);
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
      if (ImGui::MenuItem("Open Stack JSON...", "Ctrl+O")) actions.loadJson = true;
      if (ImGui::MenuItem("Save Stack JSON...", "Ctrl+S")) actions.saveJson = true;
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
    ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
    ImGui::AlignTextToFramePadding();
    ImGui::TextDisabled("Target");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    int profileIndex = static_cast<int>(profile);
    constexpr const char* profileLabels[] = {"Unrestricted", "PlayStation (PS1)", "Nintendo 64"};
    if (ImGui::Combo("##hardware-profile", &profileIndex, profileLabels, 3)) {
      profile = static_cast<HardwareProfile>(profileIndex);
      actions.hardwareProfileChanged = true;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hardwareProfileDescription(profile));
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

SceneWindowResult drawDocumentNavigator(bool& open, TestScene& scene, RenderStack& stack,
    AnimationTimeline& timeline, EditorSelection& selection, const HardwareProfile profile,
    const ModelAsset* importedModel) {
  SceneWindowResult result;
  if (!open) return result;
  if (!ImGui::Begin("Document", &open)) {
    ImGui::End();
    return result;
  }
  keepCurrentWindowVisible();

  ImGui::TextDisabled("SCENE");
  constexpr std::array<const char*, 9> sceneLabels = {"Torus", "Texture minification", "Depth precision",
    "Transparency", "Lighting comparison", "Stencil mask", "Field interference", "SDF iso-surface",
    "Spectral metamers"};
  const char* currentLabel = scene == TestScene::ImportedModel && importedModel != nullptr
    ? importedModel->name.c_str() : sceneLabels[std::clamp(static_cast<int>(scene), 0, 8)];
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##scene", currentLabel)) {
    for (int option = 0; option < 5; ++option) {
      const TestScene candidate = static_cast<TestScene>(option);
      if (ImGui::Selectable(sceneLabels[option], scene == candidate)) scene = candidate;
    }
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[5], scene == TestScene::StencilMask))
      scene = TestScene::StencilMask;
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[6], scene == TestScene::FieldInterference))
      scene = TestScene::FieldInterference;
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[7], scene == TestScene::SdfIsoSurface))
      scene = TestScene::SdfIsoSurface;
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[8], scene == TestScene::SpectralMetamers))
      scene = TestScene::SpectralMetamers;
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
  ImGui::Separator();
  if (selection.kind == EditorSelectionKind::Operation) {
    bool found = false;
    for (std::size_t index = 0; index < stack.passes().size(); ++index) {
      if (stack.passes()[index].id != selection.operationId) continue;
      stack.select(index);
      found = true;
      break;
    }
    if (!found) selection.operationId = stack.selected().id;
  }
  ImGui::TextDisabled("SCENE DEFAULTS");
  if (ImGui::Selectable("Scene defaults", selection.kind == EditorSelectionKind::SceneDefaults,
      0, ImVec2(0.0f, 26.0f))) selection.kind = EditorSelectionKind::SceneDefaults;
  ImGui::TextDisabled("Inherited by every Render operation unless locally overridden.");
  ImGui::Separator();
  ImGui::TextDisabled("STACK — TOP TO BOTTOM");
  for (std::size_t index = 0; index < stack.passes().size(); ++index) {
    RenderPass& pass = stack.passes()[index];
    ImGui::PushID(static_cast<int>(index));
    const bool selected = selection.kind == EditorSelectionKind::Operation && pass.id == selection.operationId;
    if (ImGui::Selectable(pass.name.c_str(), selected, 0, ImVec2(0.0f, 22.0f))) {
      stack.select(index);
      selection = {EditorSelectionKind::Operation, pass.id};
    }
    const float rowTop = ImGui::GetItemRectMin().y;
    const char* signalSummary = pass.kind == StackOperationKind::Render
      ? "Color · Depth · Field · Spectrum16"
      : pass.kind == StackOperationKind::Interpret ? "Spectrum16  ->  Color"
      : pass.kind == StackOperationKind::Composite ? "A + B  ->  Color"
      : pass.kind == StackOperationKind::StereoAnalysis ? "Left depth + Right depth  ->  analysis"
      : pass.kind == StackOperationKind::Measure ? "Named signal  ->  statistics"
      : "Color  ->  accumulator";
    ImGui::TextDisabled("%s", stackOperationKindLabel(pass.kind));
    if (pass.kind == StackOperationKind::Render || pass.kind == StackOperationKind::LegacyRenderComposite) {
      ImGui::SameLine();
      ImGui::TextDisabled("· %zu override%s · %zu track%s", pass.overrides.size(),
        pass.overrides.size() == 1 ? "" : "s", pass.animation.tracks.size(),
        pass.animation.tracks.size() == 1 ? "" : "s");
    }
    ImGui::TextDisabled("%s", signalSummary);
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(right - 48.0f, rowTop + 2.0f));
    const bool changed = ImGui::Checkbox("##enabled", &pass.enabled);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Operation enabled");
    animationKeyControl(pass, AnimationProperty::PassEnabled, timeline, changed);
    ImGui::SetCursorScreenPos(restoreCursor);
    ImGui::Dummy(ImVec2(0.0f, 0.0f));
    if (index + 1 < stack.passes().size()) ImGui::Separator();
    ImGui::PopID();
  }
  if (ImGui::Button("Add operation", ImVec2(-1, 0))) ImGui::OpenPopup("add-operation");
  if (ImGui::BeginPopup("add-operation")) {
    ImGui::TextDisabled("PRODUCE OR TRANSFORM A SIGNAL");
    if (ImGui::Selectable("Render scene")) {
      stack.addOperation(StackOperationKind::Render);
      selection = {EditorSelectionKind::Operation, stack.selected().id};
    }
    if (ImGui::Selectable("Interpret spectrum")) {
      stack.addOperation(StackOperationKind::Interpret);
      selection = {EditorSelectionKind::Operation, stack.selected().id};
    }
    if (ImGui::Selectable("Composite two signals")) {
      stack.addOperation(StackOperationKind::Composite);
      selection = {EditorSelectionKind::Operation, stack.selected().id};
    }
    if (ImGui::Selectable("Analyze binocular views")) {
      stack.addOperation(StackOperationKind::StereoAnalysis);
      selection = {EditorSelectionKind::Operation, stack.selected().id};
    }
    if (ImGui::Selectable("Measure a signal")) {
      stack.addOperation(StackOperationKind::Measure);
      selection = {EditorSelectionKind::Operation, stack.selected().id};
    }
    ImGui::EndPopup();
  }
  if (ImGui::Button("Duplicate selected", ImVec2(-1, 0))) {
    stack.duplicateSelected();
    selection = {EditorSelectionKind::Operation, stack.selected().id};
  }
  if (ImGui::Button("Up")) stack.moveSelected(-1);
  ImGui::SameLine();
  if (ImGui::Button("Down")) stack.moveSelected(1);
  ImGui::SameLine();
  ImGui::BeginDisabled(stack.passes().size() <= 1);
  if (ImGui::Button("Delete") && stack.removeSelected())
    selection = {EditorSelectionKind::Operation, stack.selected().id};
  ImGui::EndDisabled();
  ImGui::Separator();
  ImGui::TextDisabled("PRESENTATION");
  if (ImGui::Selectable("Final output", selection.kind == EditorSelectionKind::FinalOutput,
      0, ImVec2(0.0f, 26.0f))) selection.kind = EditorSelectionKind::FinalOutput;
  ImGui::TextDisabled("Stack result followed by display reconstruction.");
  ImGui::End();
  return result;
}

ViewportWindowResult drawViewportWindow(bool& open, const ViewportImages& images, CompareMode& compare,
    const RenderStack& stack, RenderPass& displayedPass, const CameraOrbit& camera,
    AnimationTimeline& timeline, const bool globalScope, const bool canEditTransform) {
  ViewportWindowResult result;
  if (!open) return result;
  if (!ImGui::Begin("Viewport", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return result;
  }
  keepCurrentWindowVisible();

  enum class Tool { Orbit, Translate, Scale };
  static Tool tool = Tool::Orbit;

  if (ImGui::RadioButton("Selected result", compare == CompareMode::A)) compare = CompareMode::A;
  ImGui::SameLine();
  if (ImGui::RadioButton("First render", compare == CompareMode::B)) compare = CompareMode::B;
  ImGui::SameLine();
  if (ImGui::RadioButton("Split", compare == CompareMode::Split)) compare = CompareMode::Split;
  ImGui::SameLine();
  if (ImGui::RadioButton("Final output", compare == CompareMode::Relation)) compare = CompareMode::Relation;
  ImGui::SameLine();
  const bool hasStereoPair = images.leftEye != 0 && images.rightEye != 0;
  ImGui::BeginDisabled(!hasStereoPair);
  if (ImGui::RadioButton("Stereo pair", compare == CompareMode::StereoPair)) compare = CompareMode::StereoPair;
  ImGui::EndDisabled();
  if (!hasStereoPair && compare == CompareMode::StereoPair) compare = CompareMode::A;
  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();
  if (ImGui::RadioButton("Orbit", tool == Tool::Orbit)) tool = Tool::Orbit;
  ImGui::SameLine();
  const bool selectedRenders = displayedPass.kind == StackOperationKind::Render ||
    displayedPass.kind == StackOperationKind::LegacyRenderComposite;
  ImGui::BeginDisabled(!canEditTransform || compare != CompareMode::A || (!globalScope && !selectedRenders));
  if (ImGui::RadioButton("Translate", tool == Tool::Translate)) tool = Tool::Translate;
  ImGui::SameLine();
  if (ImGui::RadioButton("Scale", tool == Tool::Scale)) tool = Tool::Scale;
  ImGui::EndDisabled();
  ImGui::SameLine();
  ImGui::TextDisabled("%s", !canEditTransform ? "Final output has no transform" : globalScope
    ? "Scene-default transform" : selectedRenders ? "Selected Render transform"
    : "Selected operation has no geometry");
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
  if (compare == CompareMode::Split || compare == CompareMode::StereoPair) {
    const float eyeHeight = compare == CompareMode::StereoPair ? std::floor(size.x * 0.375f) : size.y;
    const float pairTop = origin.y + std::floor((size.y - eyeHeight) * 0.5f);
    const ImVec2 pairOrigin(origin.x, pairTop);
    const ImVec2 pairEnd(end.x, pairTop + eyeHeight);
    const float middle = pairOrigin.x + std::floor(size.x * 0.5f);
    const unsigned int leftTexture = compare == CompareMode::StereoPair ? images.leftEye : images.base;
    const unsigned int rightTexture = compare == CompareMode::StereoPair ? images.rightEye : images.selected;
    draw->PushClipRect(pairOrigin, ImVec2(middle, pairEnd.y), true);
    draw->AddImage(static_cast<ImTextureID>(leftTexture), pairOrigin, ImVec2(middle, pairEnd.y),
      ImVec2(0, 1), ImVec2(1, 0));
    draw->PopClipRect();
    draw->PushClipRect(ImVec2(middle + 1, pairOrigin.y), pairEnd, true);
    draw->AddImage(static_cast<ImTextureID>(rightTexture), ImVec2(middle + 1, pairOrigin.y), pairEnd,
      ImVec2(0, 1), ImVec2(1, 0));
    draw->PopClipRect();
    draw->AddLine(ImVec2(middle, pairOrigin.y), ImVec2(middle, pairEnd.y), IM_COL32(225, 225, 225, 210));
    draw->AddText(ImVec2(pairOrigin.x + 9, pairOrigin.y + 8), IM_COL32(240, 240, 240, 220),
      compare == CompareMode::StereoPair ? "LEFT EYE" : "FIRST RENDER");
    draw->AddText(ImVec2(middle + 10, pairOrigin.y + 8), IM_COL32(240, 240, 240, 220),
      compare == CompareMode::StereoPair ? "RIGHT EYE" : stack.selected().name.c_str());
  } else {
    const unsigned int texture = compare == CompareMode::A ? images.selected
      : compare == CompareMode::Relation ? images.composite : images.base;
    draw->AddImage(static_cast<ImTextureID>(texture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
    const char* label = compare == CompareMode::A ? stack.selected().name.c_str()
      : compare == CompareMode::B ? "FIRST RENDER" : "FINAL OUTPUT";
    draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240, 240, 240, 220), label);
  }
  // ImGuizmo deliberately refuses activation while a normal ImGui item is hovered. Keep the viewport
  // interaction surface item-free and perform its hit test directly, otherwise a full-size InvisibleButton
  // makes a visible gizmo impossible to grab.
  result.hovered = ImGui::IsWindowHovered() && ImGui::IsMouseHoveringRect(origin, end);

  const bool gizmoVisible = canEditTransform && compare == CompareMode::A && tool != Tool::Orbit;
  result.acceptsCameraInput = result.hovered && !gizmoVisible;
  if (gizmoVisible) {
    const RendererState& state = displayedPass.renderer;
    constexpr float aspect = cameraWidth / cameraHeight;
    const PassCameraMatrices passCamera = buildPassCamera(camera, state, displayedPass.perturbation, aspect);
    const glm::mat4& view = passCamera.view;
    const glm::mat4& projection = passCamera.projection;
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
