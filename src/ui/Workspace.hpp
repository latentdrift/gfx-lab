#pragma once

#include "app/Animation.hpp"
#include "app/HardwareProfile.hpp"
#include "app/RenderStack.hpp"
#include "app/State.hpp"
namespace gfxlab {
struct ModelAsset;
}

namespace gfxlab::ui {

enum class WorkspaceLayout { Edit, Animate, Analyze };

enum class EditorSelectionKind { SceneDefaults, Operation, FinalOutput };

struct EditorSelection {
  EditorSelectionKind kind = EditorSelectionKind::SceneDefaults;
  int operationId = 0;
};

struct WorkspaceWindows {
  bool document = true;
  bool viewport = true;
  bool inspector = true;
  bool animation = false;
  bool passDifferences = false;
  bool textureInspector = false;
  bool resetLayout = false;
  WorkspaceLayout requestedLayout = WorkspaceLayout::Edit;
};

struct WorkspaceActions {
  bool undo = false;
  bool redo = false;
  bool importModel = false;
  bool copyJson = false;
  bool saveJson = false;
  bool loadJson = false;
  bool toggleViewportRecording = false;
  bool handbook = false;
  bool resetFrameHistory = false;
  bool hardwareProfileChanged = false;
  bool quit = false;
};

struct SceneWindowResult {
  bool importModel = false;
  bool unloadModel = false;
};

struct ViewportImages {
  unsigned int selected = 0;
  unsigned int base = 0;
  unsigned int composite = 0;
  unsigned int leftEye = 0;
  unsigned int rightEye = 0;
};

struct ViewportWindowResult {
  bool hovered = false;
  bool acceptsCameraInput = false;
  bool gizmoUsing = false;
};

WorkspaceActions beginWorkspace(WorkspaceWindows& windows, bool canUndo, bool canRedo, float& uiScale,
  bool viewportRecording, double recordingDurationSeconds, HardwareProfile& profile);
SceneWindowResult drawDocumentNavigator(bool& open, TestScene& scene, RenderStack& stack,
  AnimationTimeline& timeline, EditorSelection& selection, HardwareProfile profile,
  const ModelAsset* importedModel);
ViewportWindowResult drawViewportWindow(bool& open, const ViewportImages& images, CompareMode& compare,
  const RenderStack& stack, RenderPass& displayedPass, const CameraOrbit& camera,
  AnimationTimeline& timeline, bool globalScope, bool canEditTransform = true);

} // namespace gfxlab::ui
