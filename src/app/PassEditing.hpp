#pragma once

#include "app/RenderStack.hpp"

namespace gfxlab {

void applyEditedPass(RenderPass& authored, const RenderPass& displayedBefore, RenderPass edited);
void applyEditedLocalPass(RenderStack& stack, const RenderPass& displayedBefore, const RenderPass& edited,
  float timeSeconds = 0.0f);

} // namespace gfxlab
