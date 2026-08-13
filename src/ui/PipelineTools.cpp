#include "ui/PipelineTools.hpp"

#include "app/Animation.hpp"
#include "app/HardwareProfile.hpp"
#include "app/PassEditing.hpp"
#include "app/RenderStack.hpp"
#include "ui/Inspector.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>

namespace gfxlab::ui {

const char* pipelineToolWindowName(const Category category) {
  switch (category) {
    case Category::Geometry: return "Geometry##pipeline-tool";
    case Category::Camera: return "Camera##pipeline-tool";
    case Category::Rasterization: return "Rasterization##pipeline-tool";
    case Category::Surface: return "Surface##pipeline-tool";
    case Category::Texture: return "Texture##pipeline-tool";
    case Category::Lighting: return "Lighting##pipeline-tool";
    case Category::Field: return "Field##pipeline-tool";
    case Category::Spectral: return "Spectral##pipeline-tool";
    case Category::Depth: return "Depth##pipeline-tool";
    case Category::Stencil: return "Stencil##pipeline-tool";
    case Category::Color: return "Color##pipeline-tool";
    case Category::Post: return "Post##pipeline-tool";
    case Category::Output: return "Output##pipeline-tool";
  }
  return "Pipeline##pipeline-tool";
}

void drawPipelineTools(PipelineToolWindows& windows, RenderStack& stack, AnimationTimeline& timeline,
    const HardwareProfile profile, const ModelAsset* importedModel, const TestScene scene,
    const bool globalScope, const float timeSeconds, const Category focusCategory,
    const bool focusRequested) {
  for (std::size_t index = 0; index < pipelineCategories.size(); ++index) {
    const Category category = pipelineCategories[index];
    if (!categoryAvailableForHardwareProfile(profile, category)) continue;
    bool& open = windows.open[index];
    if (focusRequested && category == focusCategory) {
      open = true;
    }
    if (!open) continue;
    if (ImGui::Begin(pipelineToolWindowName(category), &open)) {
      keepCurrentWindowVisible();
      const bool selectedRenders = stack.selected().kind == StackOperationKind::Render ||
        stack.selected().kind == StackOperationKind::LegacyRenderComposite;
      ImGui::TextDisabled("%s", globalScope ? "SCENE DEFAULTS" : stack.selected().name.c_str());
      ImGui::Separator();
      if (!globalScope && !selectedRenders) {
        ImGui::TextWrapped("%s operations do not own renderer settings.",
          stackOperationKindLabel(stack.selected().kind));
        ImGui::TextDisabled("Select Scene defaults or a Render operation to edit this pipeline category.");
        ImGui::End();
        continue;
      }
      const RenderPass displayedBefore = globalScope
        ? evaluateRenderPass(stack.global(), timeSeconds)
        : materializeRenderPass(stack, stack.selectedIndex(), timeSeconds);
      RenderPass edited = displayedBefore;
      drawInspector(category, edited, profile, timeline, importedModel, scene);
      if (globalScope)
        applyEditedPass(stack.global(), displayedBefore, edited);
      else
        applyEditedLocalPass(stack, displayedBefore, edited, timeSeconds);
    }
    ImGui::End();
  }
  if (focusRequested && categoryAvailableForHardwareProfile(profile, focusCategory))
    ImGui::SetWindowFocus(pipelineToolWindowName(focusCategory));
}

} // namespace gfxlab::ui
