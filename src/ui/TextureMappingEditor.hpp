#pragma once

#include "app/State.hpp"

namespace gfxlab {
class RenderStack;
struct AnimationTimeline;
struct ModelAsset;
}

namespace gfxlab::ui {

void drawTextureMappingEditor(bool& open, RenderStack& stack, AnimationTimeline& timeline,
  const ModelAsset* importedModel, TestScene scene, bool globalScope, float timeSeconds,
  unsigned int texturePreview);

} // namespace gfxlab::ui
