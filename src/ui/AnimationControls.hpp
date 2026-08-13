#pragma once

#include "app/Animation.hpp"

namespace gfxlab {
struct RenderPass;
}

namespace gfxlab::ui {

void animationKeyControl(RenderPass& pass, AnimationProperty property, AnimationTimeline& timeline,
  bool valueChanged);

} // namespace gfxlab::ui
