#pragma once

#include "app/Animation.hpp"
#include "document/Document.hpp"
#include "editor/Commands.hpp"
#include "editor/EditorState.hpp"
#include "renderer/TextureReadback.hpp"

namespace gfxlab::ui {

void drawDocumentInspector(bool& open, document::Document& document,
  editor::EditorState& editorState, editor::CommandHistory& history,
  AnimationTimeline& timeline, float timeSeconds, unsigned int texturePreview,
  const SignalMeasurement* measurement = nullptr);

} // namespace gfxlab::ui
