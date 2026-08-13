#include "ui/ContextInspector.hpp"

#include "app/Animation.hpp"
#include "app/PassEditing.hpp"
#include "app/RenderStack.hpp"
#include "ui/DisplayInspector.hpp"
#include "ui/Inspector.hpp"
#include "ui/PassInspector.hpp"
#include "ui/TextureMappingEditor.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>

#include <array>

namespace gfxlab::ui {
namespace {

bool rendersScene(const RenderPass& pass) {
  return pass.kind == StackOperationKind::Render ||
    pass.kind == StackOperationKind::LegacyRenderComposite;
}

void applyInspectorEdit(RenderStack& stack, const bool sceneDefaults, const RenderPass& before,
    const RenderPass& edited, const float timeSeconds) {
  if (sceneDefaults) applyEditedPass(stack.global(), before, edited);
  else applyEditedLocalPass(stack, before, edited, timeSeconds);
}

void drawOverview(RenderStack& stack, AnimationTimeline& timeline, const bool sceneDefaults,
    const float timeSeconds) {
  const RenderPass before = sceneDefaults ? evaluateRenderPass(stack.global(), timeSeconds)
    : materializeRenderPass(stack, stack.selectedIndex(), timeSeconds);
  RenderStack editingStack = stack;
  editingStack.selected() = before;
  drawPassInspector(editingStack, timeline, sceneDefaults);
  applyInspectorEdit(stack, sceneDefaults, before, editingStack.selected(), timeSeconds);
}

void drawRenderSettings(RenderStack& stack, AnimationTimeline& timeline, const bool sceneDefaults,
    const HardwareProfile profile, const ModelAsset* importedModel, const TestScene scene,
    const float timeSeconds, Category& activeCategory) {
  constexpr std::array categories = {Category::Geometry, Category::Camera, Category::Rasterization,
    Category::Surface, Category::Texture, Category::Lighting, Category::Field, Category::Spectral,
    Category::Depth, Category::Stencil, Category::Color, Category::Post, Category::Output};
  if (!categoryAvailableForHardwareProfile(profile, activeCategory)) activeCategory = Category::Geometry;
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::BeginCombo("##render-section", categoryName(activeCategory))) {
    for (const Category candidate : categories) {
      if (!categoryAvailableForHardwareProfile(profile, candidate)) continue;
      if (ImGui::Selectable(categoryName(candidate), candidate == activeCategory)) activeCategory = candidate;
    }
    ImGui::EndCombo();
  }
  ImGui::Separator();
  const RenderPass before = sceneDefaults ? evaluateRenderPass(stack.global(), timeSeconds)
    : materializeRenderPass(stack, stack.selectedIndex(), timeSeconds);
  RenderPass edited = before;
  drawInspector(activeCategory, edited, profile, timeline, importedModel, scene);
  applyInspectorEdit(stack, sceneDefaults, before, edited, timeSeconds);
}

void drawTextureSettings(RenderStack& stack, AnimationTimeline& timeline, const bool sceneDefaults,
    const ModelAsset* importedModel, const TestScene scene, const float timeSeconds,
    const unsigned int texturePreview) {
  const RenderPass before = sceneDefaults ? evaluateRenderPass(stack.global(), timeSeconds)
    : materializeRenderPass(stack, stack.selectedIndex(), timeSeconds);
  RenderPass edited = before;
  drawTextureMappingEditorContents(edited, timeline, importedModel, scene, texturePreview);
  applyInspectorEdit(stack, sceneDefaults, before, edited, timeSeconds);
}

} // namespace

void drawContextInspector(bool& open, RenderStack& stack, AnimationTimeline& timeline,
    const EditorSelection& selection, const HardwareProfile profile, const ModelAsset* importedModel,
    const TestScene scene, CameraOrbit& camera, const float timeSeconds,
    const unsigned int texturePreview, Category& activeCategory, const bool focusRenderSettings) {
  if (!open) return;
  if (!ImGui::Begin("Inspector", &open)) {
    ImGui::End();
    return;
  }
  keepCurrentWindowVisible();

  if (selection.kind == EditorSelectionKind::FinalOutput) {
    ImGui::TextDisabled("FINAL OUTPUT");
    ImGui::TextUnformatted("Stack result and presentation");
    ImGui::Separator();
    drawDisplayInspectorContents(stack);
    ImGui::End();
    return;
  }

  const bool sceneDefaults = selection.kind == EditorSelectionKind::SceneDefaults;
  const bool selectedRenders = sceneDefaults || rendersScene(stack.selected());
  ImGui::TextDisabled("%s", sceneDefaults ? "SCENE DEFAULTS" : stackOperationKindLabel(stack.selected().kind));
  ImGui::TextUnformatted(sceneDefaults ? "Inherited render state" : stack.selected().name.c_str());
  if (sceneDefaults) {
    if (ImGui::Button("Reset defaults")) stack.global().renderer = RendererState{};
  } else {
    ImGui::BeginDisabled(!selectedRenders);
    if (ImGui::Button("Clear overrides")) {
      stack.selected().overrides.clear();
      stack.selected().importedTextureOverride = false;
      stack.selected().importedTexture.reset();
    }
    ImGui::EndDisabled();
  }
  ImGui::SameLine();
  ImGui::BeginDisabled(!selectedRenders);
  if (ImGui::Button("Apply scene setup")) {
    if (sceneDefaults) applyRecommendedSetup(scene, stack.global().renderer, camera);
    else {
      RenderPass recommended = materializeRenderPass(stack, stack.selectedIndex(), 0.0f);
      applyRecommendedSetup(scene, recommended.renderer, camera);
      replaceRenderPassOverrides(stack.selected(), stack.global(), recommended);
    }
  }
  ImGui::EndDisabled();
  ImGui::Separator();

  if (ImGui::BeginTabBar("context-inspector-tabs")) {
    if (ImGui::BeginTabItem("Overview")) {
      drawOverview(stack, timeline, sceneDefaults, timeSeconds);
      ImGui::EndTabItem();
    }
    const ImGuiTabItemFlags renderTabFlags = focusRenderSettings ? ImGuiTabItemFlags_SetSelected : 0;
    if (selectedRenders && ImGui::BeginTabItem("Render Settings", nullptr, renderTabFlags)) {
      drawRenderSettings(stack, timeline, sceneDefaults, profile, importedModel, scene,
        timeSeconds, activeCategory);
      ImGui::EndTabItem();
    }
    if (selectedRenders && ImGui::BeginTabItem("Material & Texture")) {
      drawTextureSettings(stack, timeline, sceneDefaults, importedModel, scene, timeSeconds, texturePreview);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

} // namespace gfxlab::ui
