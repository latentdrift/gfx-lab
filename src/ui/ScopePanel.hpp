#pragma once

#include "document/Document.hpp"
#include "editor/EditorState.hpp"
#include "evaluation/EvaluationPlan.hpp"
#include "evaluation/SignalRegistry.hpp"

namespace gfxlab::ui {

void drawScopePanel(bool& open, const document::Document& document,
  const evaluation::EvaluationPlan& plan, const evaluation::SignalRegistry& signals,
  editor::EditorState& editorState);

} // namespace gfxlab::ui
