#pragma once

#include "app/Animation.hpp"
#include "document/Document.hpp"
#include "editor/Commands.hpp"
#include "editor/EditorState.hpp"

namespace gfxlab::ui {

void drawDocumentTimeline(AnimationTimeline& timeline, document::Document& document,
  const editor::EditorState& editorState, editor::CommandHistory& history);

} // namespace gfxlab::ui
