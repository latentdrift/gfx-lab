#pragma once

#include "document/Document.hpp"
#include "editor/Commands.hpp"
#include "editor/EditorState.hpp"
#include "ui/Workspace.hpp"

namespace gfxlab::ui {

SceneWindowResult drawOperationGraph(bool& open, document::Document& document,
  editor::EditorState& editorState, editor::CommandHistory& history);

} // namespace gfxlab::ui
