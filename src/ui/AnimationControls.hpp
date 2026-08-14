#pragma once

#include "app/Animation.hpp"

namespace gfxlab {
struct RenderPass;
}

namespace gfxlab::ui {

// Supplies authored/inherited state while a selected Render operation is being drawn. The shared
// property affordance uses it to expose local override provenance and resume inheritance.
void setPropertyInheritanceContext(RenderPass* authoredPass, const RenderPass& inheritedPass);
void clearPropertyInheritanceContext();
void animationKeyControl(RenderPass& pass, AnimationProperty property, AnimationTimeline& timeline,
  bool valueChanged);
void animationKeyControlAt(RenderPass& pass, AnimationProperty property, AnimationTimeline& timeline,
  bool valueChanged, float anchorScreenY);

} // namespace gfxlab::ui
