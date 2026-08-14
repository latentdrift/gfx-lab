#pragma once

#include "document/Document.hpp"

#include <optional>
#include <vector>

namespace gfxlab::editor {

enum class SelectionKind { Scene, RenderDefaults, Operation, Presentation };

struct ObjectSelection {
  SelectionKind kind = SelectionKind::Operation;
  document::OperationId operation;
};

enum class ViewerMode { Single, Split, AbsoluteDifference, Flicker };

struct SignalViewerState {
  document::SignalRef viewed;
  std::optional<document::SignalRef> comparison;
  ViewerMode mode = ViewerMode::Single;
  bool applyPresentation = true;
  int channel = -1;
  glm::vec2 range{0.0f, 1.0f};
};

enum class ScopeTool { Signal, Measurements, Automation, Differences };

struct EditorState {
  ObjectSelection selection;
  SignalViewerState viewer;
  std::vector<document::SignalRef> pinnedSignals;
  std::vector<document::PropertyAddress> pinnedProperties;
  ScopeTool scope = ScopeTool::Signal;
};

void synchronizeEditorState(EditorState& state, const document::Document& document);

} // namespace gfxlab::editor
