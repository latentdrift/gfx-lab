#pragma once

#include "app/Animation.hpp"
#include "app/HardwareProfile.hpp"
#include "app/RenderOperationState.hpp"
#include "app/State.hpp"
#include "document/Document.hpp"
#include "editor/EditorState.hpp"
#include "editor/Commands.hpp"
namespace gfxlab::ui {

enum class WorkspaceLayout { Edit, Animate, Analyze };

using EditorSelection = editor::ObjectSelection;
using EditorSelectionKind = editor::SelectionKind;

struct WorkspaceWindows {
  bool document = true;
  bool viewport = true;
  bool inspector = true;
  bool scope = false;
  bool animation = false;
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
  unsigned int viewed = 0;
  unsigned int comparison = 0;
  unsigned int difference = 0;
  unsigned int final = 0;
  const char* viewedLabel = "Viewed signal";
  const char* comparisonLabel = "Comparison signal";
};

struct ViewportWindowResult {
  bool hovered = false;
  bool acceptsCameraInput = false;
  bool gizmoUsing = false;
};

WorkspaceActions beginWorkspace(WorkspaceWindows& windows, bool canUndo, bool canRedo, float& uiScale,
  bool viewportRecording, double recordingDurationSeconds, HardwareProfile& profile);
ViewportWindowResult drawViewportWindow(bool& open, const ViewportImages& images,
  editor::SignalViewerState& viewer, document::Document& document,
  editor::EditorState& editorState, editor::CommandHistory& history);

} // namespace gfxlab::ui
