#pragma once

namespace gfxlab {
class RenderStack;
struct AnimationTimeline;
}

namespace gfxlab::ui {

void drawAnimationEditor(AnimationTimeline& timeline, RenderStack& stack, bool& previewAtPlayhead,
  bool globalScope);

} // namespace gfxlab::ui
