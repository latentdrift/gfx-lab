#include "ui/ContextInspector.hpp"

#include "app/Animation.hpp"
#include "app/PassEditing.hpp"
#include "app/RenderStack.hpp"
#include "ui/DisplayInspector.hpp"
#include "ui/AnimationControls.hpp"
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

void beginInheritanceContext(RenderStack& stack, const bool sceneDefaults, const float timeSeconds) {
  if (!sceneDefaults && rendersScene(stack.selected()))
    setPropertyInheritanceContext(&stack.selected(), evaluateRenderPass(stack.global(), timeSeconds));
  else
    clearPropertyInheritanceContext();
}

void drawOverview(RenderStack& stack, AnimationTimeline& timeline, const bool sceneDefaults,
    const float timeSeconds) {
  const RenderPass before = sceneDefaults ? evaluateRenderPass(stack.global(), timeSeconds)
    : materializeRenderPass(stack, stack.selectedIndex(), timeSeconds);
  RenderStack editingStack = stack;
  editingStack.selected() = before;
  beginInheritanceContext(stack, sceneDefaults, timeSeconds);
  drawPassInspector(editingStack, timeline, sceneDefaults);
  clearPropertyInheritanceContext();
  applyInspectorEdit(stack, sceneDefaults, before, editingStack.selected(), timeSeconds);
}

void drawRenderSettings(RenderStack& stack, AnimationTimeline& timeline, const bool sceneDefaults,
    const HardwareProfile profile, const ModelAsset* importedModel, const TestScene scene,
    const float timeSeconds, Category& activeCategory) {
  enum class Section { TransformCamera, Lighting, VisibilityRaster, EffectsSignals, Output };
  const auto sectionFor = [](const Category category) {
    switch (category) {
      case Category::Geometry:
      case Category::Camera: return Section::TransformCamera;
      case Category::Lighting: return Section::Lighting;
      case Category::Rasterization:
      case Category::Surface:
      case Category::Depth:
      case Category::Stencil: return Section::VisibilityRaster;
      case Category::Field:
      case Category::Spectral:
      case Category::Post: return Section::EffectsSignals;
      case Category::Color:
      case Category::Output: return Section::Output;
      case Category::Texture: return Section::TransformCamera;
    }
    return Section::TransformCamera;
  };
  Section section = sectionFor(activeCategory);
  constexpr std::array sectionLabels = {"Transform & Camera", "Lighting", "Visibility & Raster",
    "Effects & Signals", "Output"};
  int sectionIndex = static_cast<int>(section);
  ImGui::SetNextItemWidth(-1.0f);
  if (ImGui::Combo("##render-section", &sectionIndex, sectionLabels.data(), sectionLabels.size())) {
    section = static_cast<Section>(sectionIndex);
    constexpr std::array representatives = {Category::Geometry, Category::Lighting,
      Category::Rasterization, Category::Field, Category::Color};
    activeCategory = representatives[static_cast<std::size_t>(section)];
  }
  ImGui::Separator();
  const RenderPass before = sceneDefaults ? evaluateRenderPass(stack.global(), timeSeconds)
    : materializeRenderPass(stack, stack.selectedIndex(), timeSeconds);
  RenderPass edited = before;
  beginInheritanceContext(stack, sceneDefaults, timeSeconds);
  const auto drawCategory = [&](const Category category) {
    if (categoryAvailableForHardwareProfile(profile, category))
      drawInspector(category, edited, profile, timeline, importedModel, scene, false, sceneDefaults);
  };
  switch (section) {
    case Section::TransformCamera:
      drawCategory(Category::Geometry);
      ImGui::SeparatorText("CAMERA & PROJECTION");
      drawCategory(Category::Camera);
      break;
    case Section::Lighting:
      drawCategory(Category::Lighting);
      break;
    case Section::VisibilityRaster:
      drawCategory(Category::Rasterization);
      ImGui::SeparatorText("SURFACE & TRANSPARENCY");
      drawCategory(Category::Surface);
      if (categoryAvailableForHardwareProfile(profile, Category::Depth)) {
        ImGui::SeparatorText("DEPTH");
        drawCategory(Category::Depth);
      }
      if (categoryAvailableForHardwareProfile(profile, Category::Stencil)) {
        ImGui::SeparatorText("STENCIL");
        drawCategory(Category::Stencil);
      }
      break;
    case Section::EffectsSignals:
      drawCategory(Category::Field);
      if (categoryAvailableForHardwareProfile(profile, Category::Spectral)) {
        ImGui::SeparatorText("SPECTRAL");
        drawCategory(Category::Spectral);
      }
      ImGui::SeparatorText("POST EFFECTS");
      drawCategory(Category::Post);
      break;
    case Section::Output:
      drawCategory(Category::Color);
      ImGui::SeparatorText("RENDER TARGET");
      drawCategory(Category::Output);
      break;
  }
  clearPropertyInheritanceContext();
  applyInspectorEdit(stack, sceneDefaults, before, edited, timeSeconds);
}

void drawTextureSettings(RenderStack& stack, AnimationTimeline& timeline, const bool sceneDefaults,
    const HardwareProfile profile, const ModelAsset* importedModel, const TestScene scene, const float timeSeconds,
    const unsigned int texturePreview) {
  const RenderPass before = sceneDefaults ? evaluateRenderPass(stack.global(), timeSeconds)
    : materializeRenderPass(stack, stack.selectedIndex(), timeSeconds);
  RenderPass edited = before;
  beginInheritanceContext(stack, sceneDefaults, timeSeconds);
  if (!sceneDefaults) {
    const bool assetOverride = stack.selected().importedTextureOverride;
    ImGui::TextDisabled("IMPORTED IMAGE RESOURCE");
    ImGui::SameLine();
    if (assetOverride) {
      ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.31f, 0.24f, 0.14f, 1.0f));
      if (ImGui::SmallButton("override##image-resource")) {
        stack.selected().importedTextureOverride = false;
        stack.selected().importedTexture.reset();
        edited.importedTexture = stack.global().importedTexture;
      }
      ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) ImGui::SetTooltip("Click to use the image resource from Scene Defaults.");
    } else {
      ImGui::TextDisabled("inherited");
    }
  }
  drawTextureMappingEditorContents(edited, timeline, importedModel, scene, texturePreview);
  ImGui::SeparatorText("SAMPLING & STORAGE");
  drawInspector(Category::Texture, edited, profile, timeline, importedModel, scene, true, sceneDefaults);
  clearPropertyInheritanceContext();
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

  if (selection.kind == EditorSelectionKind::Scene) {
    ImGui::TextDisabled("SCENE");
    ImGui::TextUnformatted("World, assets, and document camera");
    ImGui::Separator();
    ImGui::TextWrapped("Scene selection is independent from the signal shown in the viewer. Scene and asset editing will move into this object as the typed document migration continues.");
    ImGui::End();
    return;
  }

  if (selection.kind == EditorSelectionKind::Presentation) {
    ImGui::TextDisabled("FINAL OUTPUT");
    ImGui::TextUnformatted("Stack result and presentation");
    ImGui::Separator();
    drawDisplayInspectorContents(stack);
    ImGui::End();
    return;
  }

  const bool sceneDefaults = selection.kind == EditorSelectionKind::RenderDefaults;
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
    const bool focusTexture = focusRenderSettings && activeCategory == Category::Texture;
    const ImGuiTabItemFlags renderTabFlags = focusRenderSettings && !focusTexture
      ? ImGuiTabItemFlags_SetSelected : 0;
    if (selectedRenders && ImGui::BeginTabItem("Render Settings", nullptr, renderTabFlags)) {
      drawRenderSettings(stack, timeline, sceneDefaults, profile, importedModel, scene,
        timeSeconds, activeCategory);
      ImGui::EndTabItem();
    }
    const ImGuiTabItemFlags textureTabFlags = focusTexture ? ImGuiTabItemFlags_SetSelected : 0;
    if (selectedRenders && ImGui::BeginTabItem("Material & Texture", nullptr, textureTabFlags)) {
      drawTextureSettings(stack, timeline, sceneDefaults, profile, importedModel, scene, timeSeconds,
        texturePreview);
      ImGui::EndTabItem();
    }
    ImGui::EndTabBar();
  }
  ImGui::End();
}

} // namespace gfxlab::ui
