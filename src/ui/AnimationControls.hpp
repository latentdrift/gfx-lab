#pragma once

#include "app/Animation.hpp"

namespace gfxlab {
struct RenderPass;
}

namespace gfxlab::ui {

void animationKeyControl(RenderPass& pass, AnimationProperty property, AnimationTimeline& timeline,
  bool valueChanged);
void animationKeyControlAt(RenderPass& pass, AnimationProperty property, AnimationTimeline& timeline,
  bool valueChanged, float anchorScreenY);

} // namespace gfxlab::ui
