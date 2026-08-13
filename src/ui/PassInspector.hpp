#pragma once

namespace gfxlab { class RenderStack; struct AnimationTimeline; }

namespace gfxlab::ui {

void drawPassInspector(RenderStack& stack, AnimationTimeline& timeline, bool globalScope = false);

} // namespace gfxlab::ui
