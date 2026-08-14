#include "editor/EditorState.hpp"

#include <algorithm>

namespace gfxlab::editor {

void synchronizeEditorState(EditorState& state, const document::Document& document) {
  if (state.selection.kind == SelectionKind::Operation &&
      document::findOperation(document, state.selection.operation) == nullptr) {
    state.selection = {SelectionKind::RenderDefaults, {}};
  }
  if (!state.viewer.viewed || document::findSignal(document, state.viewer.viewed.id) == nullptr)
    state.viewer.viewed = document.presentation.input;
  if (state.viewer.comparison.has_value() &&
      document::findSignal(document, state.viewer.comparison->id) == nullptr)
    state.viewer.comparison.reset();
  if (!state.viewer.comparison.has_value() && state.viewer.mode != ViewerMode::Single)
    state.viewer.mode = ViewerMode::Single;
  state.pinnedSignals.erase(std::remove_if(state.pinnedSignals.begin(), state.pinnedSignals.end(),
    [&document](const document::SignalRef signal) { return document::findSignal(document, signal.id) == nullptr; }),
    state.pinnedSignals.end());
}

} // namespace gfxlab::editor
