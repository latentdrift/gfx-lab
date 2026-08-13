#pragma once

#include "app/Animation.hpp"
#include "app/HardwareProfile.hpp"
#include "app/RenderStack.hpp"

#include <array>

namespace gfxlab {
struct ModelAsset;
}

namespace gfxlab::ui {

struct WorkspaceWindows {
  bool scene = true;
  bool renderPasses = true;
  bool pipeline = true;
  bool viewport = true;
  bool inspector = true;
  bool animation = false;
  bool passDifferences = false;
  bool resetLayout = false;
};

struct WorkspaceActions {
  bool undo = false;
  bool redo = false;
  bool importModel = false;
  bool copyJson = false;
  bool handbook = false;
  bool quit = false;
};

struct SceneWindowResult {
  bool hardwareProfileChanged = false;
  bool importModel = false;
  bool unloadModel = false;
};

struct ViewportImages {
  unsigned int selected = 0;
  unsigned int base = 0;
  unsigned int composite = 0;
};

WorkspaceActions beginWorkspace(WorkspaceWindows& windows, bool canUndo, bool canRedo);
SceneWindowResult drawSceneWindow(bool& open, TestScene& scene, HardwareProfile& profile,
  const ModelAsset* importedModel);
void drawRenderPassesWindow(bool& open, RenderStack& stack, AnimationTimeline& timeline,
  bool& globalScope);
void drawPipelineWindow(bool& open, Category& category, HardwareProfile profile);
bool drawViewportWindow(bool& open, const ViewportImages& images, CompareMode& compare,
  const RenderStack& stack);

} // namespace gfxlab::ui
