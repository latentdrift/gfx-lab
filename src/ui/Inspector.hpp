#pragma once

#include "app/State.hpp"
#include "app/HardwareProfile.hpp"

namespace gfxlab { struct RenderPass; struct AnimationTimeline; struct ModelAsset; }

namespace gfxlab::ui {

void setStyle();
void drawInspector(Category category, RenderPass& pass, HardwareProfile profile, AnimationTimeline& timeline,
  const ModelAsset* importedModel, TestScene scene, bool textureSamplingOnly = false);
const char* categoryName(Category category);

} // namespace gfxlab::ui
