#pragma once

#include "app/State.hpp"

namespace gfxlab {
struct AnimationTimeline;
struct ModelAsset;
struct RenderPass;
}

namespace gfxlab::ui {

void drawTextureMappingEditorContents(RenderPass& edited, AnimationTimeline& timeline,
  const ModelAsset* importedModel, TestScene scene, unsigned int texturePreview);

} // namespace gfxlab::ui
