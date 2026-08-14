#include "ui/ContextInspector.hpp"

#include "app/Animation.hpp"
#include "app/PassEditing.hpp"
#include "app/RenderStack.hpp"
#include "ui/DisplayInspector.hpp"
#include "ui/Inspector.hpp"
#include "ui/PassInspector.hpp"
#include "ui/TextureMappingEditor.hpp"
#include "ui/Windowing.hpp"
#include "renderer/TextureReadback.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

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
    const unsigned int texturePreview, Category& activeCategory, const bool focusRenderSettings,
    const SignalMeasurement* measurement, const float smoothedControl, const float mappedOutput,
    const bool modulationApplied) {
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

  if (!sceneDefaults && stack.selected().kind == StackOperationKind::Measure) {
    drawOverview(stack, timeline, false, timeSeconds);
    ImGui::SeparatorText("LIVE CONTROL");
    if (measurement == nullptr || measurement->sampleCount == 0) {
      ImGui::TextDisabled("Waiting for a readable upstream signal.");
    } else {
      const RenderPass& controller = stack.selected();
      const float rawControl = measurementMetricValue(*measurement, controller.measurementMetric);
      ImGui::TextUnformatted(measurementMetricLabel(controller.measurementMetric));
      ImGui::SameLine();
      ImGui::Text("%.5f", rawControl);
      const float inputSpan = controller.measurementInputMaximum - controller.measurementInputMinimum;
      const float inputPosition = std::abs(inputSpan) <= 0.000001f ? 0.0f : std::clamp(
        (rawControl - controller.measurementInputMinimum) / inputSpan, 0.0f, 1.0f);
      ImGui::ProgressBar(inputPosition, ImVec2(-1.0f, 0.0f));
      if (controller.measurementModulationEnabled) {
        const RenderPass* target = nullptr;
        for (const RenderPass& candidate : stack.passes())
          if (candidate.id == controller.measurementTargetPassId) target = &candidate;
        if (modulationApplied && target != nullptr) {
          ImGui::TextDisabled("smoothed input %.5f", smoothedControl);
          ImGui::Text("%s  ->  %s", target->name.c_str(),
            animationPropertyInfo(controller.measurementTargetProperty).label.data());
          ImGui::SameLine();
          ImGui::TextColored(ImVec4(0.58f, 0.84f, 0.88f, 1.0f), "%.5f", mappedOutput);
        } else {
          ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.34f, 1.0f),
            "Modulation is enabled but has no valid sampled destination yet.");
        }
      } else {
        ImGui::TextDisabled("Enable Drive a property above to use this value.");
      }
      if (ImGui::TreeNode("Diagnostic statistics")) {
        ImGui::Text("Mean RGB       %+.4f   %+.4f   %+.4f", measurement->meanChannels.r,
          measurement->meanChannels.g, measurement->meanChannels.b);
        ImGui::Text("Mean magnitude %.5f", measurement->meanMagnitude);
        ImGui::Text("RMS energy     %.5f", measurement->rmsMagnitude);
        ImGui::Text("Peak magnitude %.5f", measurement->peakMagnitude);
        ImGui::Text("Coverage       %.2f%%", measurement->coverage * 100.0f);
        ImGui::TextDisabled("%d spatial samples (64 x 64)", measurement->sampleCount);
        ImGui::TreePop();
      }
    }
    ImGui::TextWrapped("Measure leaves the image unchanged. Its selected scalar can drive one continuous property on another operation.");
    ImGui::End();
    return;
  }

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
