#pragma once

#include "app/HardwareProfile.hpp"
#include "app/State.hpp"
#include "ui/Workspace.hpp"

namespace gfxlab {
class RenderStack;
struct AnimationTimeline;
struct CameraOrbit;
struct ModelAsset;
struct SignalMeasurement;
}

namespace gfxlab::ui {

void drawContextInspector(bool& open, RenderStack& stack, AnimationTimeline& timeline,
  const EditorSelection& selection, HardwareProfile profile, const ModelAsset* importedModel,
  TestScene scene, CameraOrbit& camera, float timeSeconds, unsigned int texturePreview,
  Category& activeCategory, bool focusRenderSettings, const SignalMeasurement* measurement,
  float smoothedControl, float mappedOutput, bool modulationApplied);

} // namespace gfxlab::ui
