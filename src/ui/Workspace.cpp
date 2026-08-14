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
constexpr const char* workspaceId = "Graphics Lab Workspace v12";

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
  ImGui::DockBuilderDockWindow("Signal Viewer", centerTop);
  ImGui::DockBuilderDockWindow("Scope", centerBottom);
  ImGui::DockBuilderDockWindow("Timeline", centerBottom);
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
  ImGui::MenuItem("Document", nullptr, &windows.document);
  ImGui::MenuItem("Signal Viewer", nullptr, &windows.viewport);
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

SceneWindowResult drawDocumentNavigator(bool& open, document::Document& document,
    editor::EditorState& editorState, editor::CommandHistory& history) {
  SceneWindowResult result;
  if (!open) return result;
  if (!ImGui::Begin("Document", &open)) {
    ImGui::End();
    return result;
  }
  keepCurrentWindowVisible();

  const TestScene scene = document.scene.testScene;
  const HardwareProfile profile = document.hardwareProfile;
  const ModelAsset* importedModel = document.scene.importedModel.get();

  ImGui::TextDisabled("SCENE");
  constexpr std::array<const char*, 10> sceneLabels = {"Torus", "Texture minification", "Depth precision",
    "Transparency", "Lighting comparison", "Stencil mask", "Field interference", "SDF iso-surface",
    "Spectral metamers", "Elemental chamber"};
  const char* currentLabel = scene == TestScene::ImportedModel && importedModel != nullptr
    ? importedModel->name.c_str() : sceneLabels[std::clamp(static_cast<int>(scene), 0, 9)];
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##scene", currentLabel)) {
    for (int option = 0; option < 5; ++option) {
      const TestScene candidate = static_cast<TestScene>(option);
      if (ImGui::Selectable(sceneLabels[option], scene == candidate)) {
        document::Document edited = document;
        edited.scene.testScene = candidate;
        static_cast<void>(history.execute(document,
          editor::ReplaceDocument{std::move(edited)}));
      }
    }
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[5], scene == TestScene::StencilMask))
      {
        document::Document edited = document;
        edited.scene.testScene = TestScene::StencilMask;
        static_cast<void>(history.execute(document,
          editor::ReplaceDocument{std::move(edited)}));
      }
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[6], scene == TestScene::FieldInterference))
      {
        document::Document edited = document;
        edited.scene.testScene = TestScene::FieldInterference;
        static_cast<void>(history.execute(document,
          editor::ReplaceDocument{std::move(edited)}));
      }
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[7], scene == TestScene::SdfIsoSurface))
      {
        document::Document edited = document;
        edited.scene.testScene = TestScene::SdfIsoSurface;
        static_cast<void>(history.execute(document,
          editor::ReplaceDocument{std::move(edited)}));
      }
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[8], scene == TestScene::SpectralMetamers))
      {
        document::Document edited = document;
        edited.scene.testScene = TestScene::SpectralMetamers;
        static_cast<void>(history.execute(document,
          editor::ReplaceDocument{std::move(edited)}));
      }
    if (profile == HardwareProfile::Unrestricted &&
        ImGui::Selectable(sceneLabels[9], scene == TestScene::ElementalChamber))
      {
        document::Document edited = document;
        edited.scene.testScene = TestScene::ElementalChamber;
        static_cast<void>(history.execute(document,
          editor::ReplaceDocument{std::move(edited)}));
      }
    if (importedModel != nullptr) {
      ImGui::PushID("imported-model-scene");
      if (ImGui::Selectable(importedModel->name.c_str(), scene == TestScene::ImportedModel))
        {
          document::Document edited = document;
          edited.scene.testScene = TestScene::ImportedModel;
          static_cast<void>(history.execute(document,
            editor::ReplaceDocument{std::move(edited)}));
        }
      ImGui::PopID();
    }
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
    if (scene != TestScene::ImportedModel && ImGui::Button("Use Model", ImVec2(-1, 0))) {
      document::Document edited = document;
      edited.scene.testScene = TestScene::ImportedModel;
      static_cast<void>(history.execute(document,
        editor::ReplaceDocument{std::move(edited)}));
    }
    if (ImGui::Button("Unload Model", ImVec2(-1, 0))) result.unloadModel = true;
  }
  ImGui::Separator();
  EditorSelection& selection = editorState.selection;
  ImGui::TextDisabled("DOCUMENT OBJECTS");
  if (ImGui::Selectable("Scene", selection.kind == EditorSelectionKind::Scene,
      0, ImVec2(0.0f, 24.0f))) selection = {EditorSelectionKind::Scene, {}};
  if (ImGui::Selectable("Render Defaults", selection.kind == EditorSelectionKind::RenderDefaults,
      0, ImVec2(0.0f, 24.0f))) selection = {EditorSelectionKind::RenderDefaults, {}};
  ImGui::TextDisabled("Inherited by every Render operation unless locally overridden.");
  ImGui::Separator();
  ImGui::TextDisabled("OPERATIONS — EXECUTION ORDER");
  for (std::size_t index = 0; index < document.operations.size(); ++index) {
    const document::Operation& operation = document.operations[index];
    ImGui::PushID(static_cast<int>(operation.id.value));
    const bool selected = selection.kind == EditorSelectionKind::Operation &&
      selection.operation == operation.id;
    if (ImGui::Selectable(operation.name.c_str(), selected, 0, ImVec2(0.0f, 22.0f))) {
      selection = {EditorSelectionKind::Operation, operation.id};
    }
    const float rowTop = ImGui::GetItemRectMin().y;
    ImGui::TextDisabled("%s", document::operationTypeLabel(operation));
    if (const auto* render = std::get_if<document::RenderOperation>(&operation.data)) {
      ImGui::SameLine();
      const document::ObjectId owner = document::operationObject(operation.id);
      const std::size_t tracks = static_cast<std::size_t>(std::count_if(
        document.automation.animation.begin(), document.automation.animation.end(),
        [owner](const document::AnimationTrack& track) { return track.target.owner == owner; }));
      ImGui::TextDisabled("· %zu changes · %zu tracks", render->overrides.size(), tracks);
    }
    ImGui::TextDisabled("Outputs:");
    ImGui::SameLine();
    for (std::size_t outputIndex = 0; outputIndex < operation.outputs.size(); ++outputIndex) {
      const document::SignalDescriptor& output = operation.outputs[outputIndex];
      if (outputIndex > 0) ImGui::SameLine();
      const document::SignalRef signal{output.id, 0};
      const bool viewed = editorState.viewer.viewed.id == output.id;
      const bool compared = editorState.viewer.comparison.has_value() &&
        editorState.viewer.comparison->id == output.id;
      if (viewed) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.42f, 0.48f, 1.0f));
      else if (compared) ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.48f, 0.34f, 0.14f, 1.0f));
      if (ImGui::SmallButton(document::signalKindLabel(output.kind))) {
        if (ImGui::GetIO().KeyShift) editorState.viewer.comparison = signal;
        else editorState.viewer.viewed = signal;
      }
      if (viewed) ImGui::PopStyleColor();
      else if (compared) ImGui::PopStyleColor();
      if (ImGui::BeginPopupContextItem("signal-actions")) {
        ImGui::TextDisabled("%s / %s", operation.name.c_str(), output.name.c_str());
        if (ImGui::MenuItem("View signal")) editorState.viewer.viewed = signal;
        if (ImGui::MenuItem("Use as comparison")) editorState.viewer.comparison = signal;
        if (compared && ImGui::MenuItem("Clear comparison")) editorState.viewer.comparison.reset();
        ImGui::EndPopup();
      }
      if (ImGui::IsItemHovered()) ImGui::SetTooltip(
        "Click: view %s / %s\nShift-click: use as comparison\nRight-click: signal actions",
        operation.name.c_str(), output.name.c_str());
    }
    const float right = ImGui::GetWindowPos().x + ImGui::GetWindowContentRegionMax().x;
    const ImVec2 restoreCursor = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(right - 28.0f, rowTop + 2.0f));
    bool enabled = operation.enabled;
    if (ImGui::Checkbox("##enabled", &enabled))
      static_cast<void>(history.execute(document,
        editor::SetOperationEnabled{operation.id, enabled}));
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Operation enabled");
    ImGui::SetCursorScreenPos(restoreCursor);
    if (index + 1 < document.operations.size()) ImGui::Separator();
    ImGui::PopID();
  }
  if (ImGui::Button("Add operation", ImVec2(-1, 0))) ImGui::OpenPopup("add-operation");
  if (ImGui::BeginPopup("add-operation")) {
    ImGui::TextDisabled("PRODUCE OR TRANSFORM A SIGNAL");
    const auto add = [&](document::Operation operation) {
      const document::OperationId id = operation.id;
      const bool imageOutput = !operation.outputs.empty() &&
        operation.outputs.front().kind != document::SignalKind::Scalar;
      if (history.execute(document, editor::AddOperation{std::move(operation),
          static_cast<std::size_t>(-1), imageOutput}).applied)
        selection = {EditorSelectionKind::Operation, id};
    };
    const document::SignalRef previous = selection.kind == EditorSelectionKind::Operation
      ? (document::findOperation(document, selection.operation) != nullptr
          ? document::primaryOutput(*document::findOperation(document, selection.operation))
          : document.presentation.input)
      : document.presentation.input;
    if (ImGui::Selectable("Render scene")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeRenderOperation(id, "Render " + std::to_string(id.value)));
    }
    if (ImGui::Selectable("Interpret spectrum")) {
      document::SignalRef spectrum;
      for (const document::Operation& candidate : document.operations)
        for (const document::SignalDescriptor& output : candidate.outputs)
          if (output.kind == document::SignalKind::Spectrum16) spectrum = {output.id, 0};
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeInterpretOperation(id, "Interpret " + std::to_string(id.value), spectrum));
    }
    if (ImGui::Selectable("Composite two signals")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeCompositeOperation(id, "Composite " + std::to_string(id.value),
        document.presentation.input, previous));
    }
    if (ImGui::Selectable("Analyze binocular views")) {
      std::array<document::SignalRef, 2> renders{};
      std::size_t count = 0;
      for (const document::Operation& candidate : document.operations)
        if (std::holds_alternative<document::RenderOperation>(candidate.data) && count < renders.size())
          renders[count++] = document::primaryOutput(candidate);
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeStereoOperation(id, "Stereo " + std::to_string(id.value),
        renders[0], renders[count > 1 ? 1 : 0]));
    }
    if (ImGui::Selectable("Measure a signal")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeMeasureOperation(id, "Measure " + std::to_string(id.value), previous));
    }
    ImGui::EndPopup();
  }
  if (ImGui::Button("Duplicate selected", ImVec2(-1, 0))) {
    const document::Operation* source = selection.kind == EditorSelectionKind::Operation
      ? document::findOperation(document, selection.operation) : nullptr;
    if (source != nullptr) {
      const document::OperationId id = document::nextOperationId(document);
      const auto found = std::find_if(document.operations.begin(), document.operations.end(),
        [&](const document::Operation& candidate) { return candidate.id == source->id; });
      const std::size_t index = static_cast<std::size_t>(std::distance(document.operations.begin(), found)) + 1;
      if (history.execute(document, editor::DuplicateOperation{source->id, id, index}).applied)
        selection = {EditorSelectionKind::Operation, id};
    }
  }
  const auto selectedIndex = [&]() -> std::optional<std::size_t> {
    if (selection.kind != EditorSelectionKind::Operation) return std::nullopt;
    const auto found = std::find_if(document.operations.begin(), document.operations.end(),
      [&](const document::Operation& operation) { return operation.id == selection.operation; });
    return found == document.operations.end() ? std::nullopt
      : std::optional{static_cast<std::size_t>(std::distance(document.operations.begin(), found))};
  };
  if (ImGui::Button("Up") && selectedIndex().has_value() && *selectedIndex() > 0)
    static_cast<void>(history.execute(document,
      editor::MoveOperation{selection.operation, *selectedIndex() - 1}));
  ImGui::SameLine();
  if (ImGui::Button("Down") && selectedIndex().has_value() && *selectedIndex() + 1 < document.operations.size())
    static_cast<void>(history.execute(document,
      editor::MoveOperation{selection.operation, *selectedIndex() + 1}));
  ImGui::SameLine();
  ImGui::BeginDisabled(document.operations.size() <= 1 || !selectedIndex().has_value());
  if (ImGui::Button("Delete") && history.execute(document,
      editor::RemoveOperation{selection.operation}).applied)
    selection = {EditorSelectionKind::RenderDefaults, {}};
  ImGui::EndDisabled();
  ImGui::Separator();
  ImGui::TextDisabled("PRESENTATION");
  if (ImGui::Selectable("Presentation", selection.kind == EditorSelectionKind::Presentation,
      0, ImVec2(0.0f, 26.0f))) selection = {EditorSelectionKind::Presentation, {}};
  const document::SignalDescriptor* finalSignal = document::findSignal(document,
    document.presentation.input.id);
  ImGui::TextDisabled("Input: %s", finalSignal != nullptr ? finalSignal->name.c_str() : "invalid signal");
  ImGui::End();
  return result;
}

ViewportWindowResult drawViewportWindow(bool& open, const ViewportImages& images,
    editor::SignalViewerState& viewer, document::Document& document,
    editor::EditorState& editorState, editor::CommandHistory& history) {
  ViewportWindowResult result;
  if (!open) return result;
  if (!ImGui::Begin("Signal Viewer", &open, ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoScrollWithMouse)) {
    ImGui::End();
    return result;
  }
  keepCurrentWindowVisible();

  enum class Tool { Orbit, Translate, Scale };
  static Tool tool = Tool::Orbit;

  if (ImGui::RadioButton("Signal", viewer.mode == editor::ViewerMode::Single))
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
  ImGui::TextDisabled("%s", images.viewedLabel);
  if (hasComparison) {
    ImGui::SameLine();
    ImGui::TextDisabled("vs %s", images.comparisonLabel);
  }
  ImGui::SameLine();
  ImGui::SeparatorEx(ImGuiSeparatorFlags_Vertical);
  ImGui::SameLine();
  if (ImGui::RadioButton("Orbit", tool == Tool::Orbit)) tool = Tool::Orbit;
  ImGui::SameLine();
  document::Operation* selectedOperation = editorState.selection.kind == editor::SelectionKind::Operation
    ? document::findOperation(document, editorState.selection.operation) : nullptr;
  document::RenderOperation* selectedRender = selectedOperation == nullptr ? nullptr
    : std::get_if<document::RenderOperation>(&selectedOperation->data);
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
    draw->AddText(ImVec2(pairOrigin.x + 9, pairOrigin.y + 8), IM_COL32(240, 240, 240, 220), "COMPARISON");
    draw->AddText(ImVec2(middle + 10, pairOrigin.y + 8), IM_COL32(240, 240, 240, 220), "VIEWED SIGNAL");
  } else {
    const unsigned int texture = viewer.mode == editor::ViewerMode::AbsoluteDifference ? images.difference
      : viewer.mode == editor::ViewerMode::Flicker && std::fmod(ImGui::GetTime(), 0.6) < 0.3
        ? images.comparison : images.viewed;
    draw->AddImage(static_cast<ImTextureID>(texture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
    const char* label = viewer.mode == editor::ViewerMode::AbsoluteDifference ? "ABSOLUTE DIFFERENCE"
      : viewer.mode == editor::ViewerMode::Flicker ? "FLICKER COMPARISON" : "VIEWED SIGNAL";
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
