#include "ui/PassInspector.hpp"

#include "app/RenderStack.hpp"
#include "ui/AnimationControls.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>

namespace gfxlab::ui {
namespace {

void description(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

} // namespace

void drawPassInspector(RenderStack& stack, AnimationTimeline& timeline, const bool globalScope) {
  RenderPass& pass = stack.selected();
  ImGui::TextDisabled(globalScope ? "GLOBAL BASE" : "SELECTED RENDER PASS");
  if (!globalScope) {
    std::array<char, 64> name{};
    std::snprintf(name.data(), name.size(), "%s", pass.name.c_str());
    if (ImGui::InputText("Name", name.data(), name.size())) pass.name = name.data();

    const char* outputLabels[] = {"Color", "Linear depth", "Normals", "Vertex colors"};
    int output = static_cast<int>(pass.output);
    const bool outputChanged = ImGui::Combo("Output buffer", &output, outputLabels, 4);
    if (outputChanged) pass.output = static_cast<PassOutput>(output);
    animationKeyControl(pass, AnimationProperty::PassOutput, timeline, outputChanged);
  }

  ImGui::Spacing();
  ImGui::TextDisabled("GEOMETRY PERTURBATION");
  animationKeyControl(pass, AnimationProperty::ModelTranslation, timeline,
    ImGui::DragFloat3("Model translation", &pass.perturbation.modelTranslation.x, 0.005f, -4.0f, 4.0f, "%.3f"));
  animationKeyControl(pass, AnimationProperty::ModelScale, timeline,
    ImGui::DragFloat("Model scale", &pass.perturbation.modelScale, 0.001f, 0.5f, 1.5f, "%.4f"));
  animationKeyControl(pass, AnimationProperty::NormalInflation, timeline,
    ImGui::DragFloat("Normal inflation", &pass.perturbation.normalInflation, 0.001f, -0.5f, 0.5f, "%.4f"));
  description("Inflation moves each vertex along its mesh normal before projection. It changes actual silhouettes and internal geometry rather than enlarging the finished image.");

  ImGui::Spacing();
  ImGui::TextDisabled("VIEW PERTURBATION");
  animationKeyControl(pass, AnimationProperty::CameraYaw, timeline,
    ImGui::SliderAngle("Camera yaw offset", &pass.perturbation.cameraYaw, -5.0f, 5.0f, "%.2f deg"));
  animationKeyControl(pass, AnimationProperty::CameraPitch, timeline,
    ImGui::SliderAngle("Camera pitch offset", &pass.perturbation.cameraPitch, -5.0f, 5.0f, "%.2f deg"));
  animationKeyControl(pass, AnimationProperty::CameraDistance, timeline,
    ImGui::DragFloat("Camera distance offset", &pass.perturbation.cameraDistance, 0.005f, -2.0f, 2.0f, "%.3f unit"));
  animationKeyControl(pass, AnimationProperty::FieldOfViewOffset, timeline,
    ImGui::DragFloat("Field-of-view offset", &pass.perturbation.fieldOfView, 0.05f, -20.0f, 20.0f, "%.2f deg"));

  ImGui::Spacing();
  ImGui::TextDisabled("SAMPLING PERTURBATION");
  animationKeyControl(pass, AnimationProperty::UvOffset, timeline,
    ImGui::DragFloat2("UV offset", &pass.perturbation.uvOffset.x, 1.0f / 512.0f, -1.0f, 1.0f, "%.5f"));
  animationKeyControl(pass, AnimationProperty::UvScale, timeline,
    ImGui::DragFloat2("UV scale", &pass.perturbation.uvScale.x, 0.001f, 0.25f, 4.0f, "%.4f"));

  if (globalScope) return;

  std::size_t firstEnabled = stack.passes().size();
  for (std::size_t index = 0; index < stack.passes().size(); ++index) {
    if (stack.passes()[index].enabled) {
      firstEnabled = index;
      break;
    }
  }
  if (!pass.enabled) {
    ImGui::Spacing();
    ImGui::Separator();
    description("This pass is disabled, so its composite step is currently skipped. Its settings are retained.");
    return;
  }
  if (stack.selectedIndex() == firstEnabled) {
    ImGui::Spacing();
    ImGui::Separator();
    description("The first enabled pass seeds the accumulated image. Composite controls begin on later passes.");
    return;
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::TextDisabled("COMPOSITE OPERANDS");
  constexpr const char* sourceLabels[] = {
    "Accumulated result", "Current pass", "Render pass", "Fixed color", "Previous frame"};
  const auto sourceControl = [&](const char* label, CompositeSource& source, int& sourcePass,
      const AnimationProperty sourceProperty, const AnimationProperty passProperty) {
    int selectedSource = static_cast<int>(source);
    const bool sourceChanged = ImGui::Combo(label, &selectedSource, sourceLabels, 5);
    if (sourceChanged) source = static_cast<CompositeSource>(selectedSource);
    animationKeyControl(pass, sourceProperty, timeline, sourceChanged);
    if (source != CompositeSource::RenderPass) return;
    std::array<const char*, RenderStack::maximumPasses> passLabels{};
    for (std::size_t index = 0; index < stack.passes().size(); ++index)
      passLabels[index] = stack.passes()[index].name.c_str();
    sourcePass = std::clamp(sourcePass, 0, static_cast<int>(stack.passes().size()) - 1);
    ImGui::Indent();
    const bool passChanged = ImGui::Combo("Render pass", &sourcePass, passLabels.data(),
      static_cast<int>(stack.passes().size()));
    animationKeyControl(pass, passProperty, timeline, passChanged);
    ImGui::Unindent();
  };
  ImGui::PushID("source-a");
  sourceControl("Source A", pass.composite.sourceA, pass.composite.sourceAPass,
    AnimationProperty::CompositeSourceA, AnimationProperty::CompositeSourceAPass);
  ImGui::PopID();
  ImGui::PushID("source-b");
  sourceControl("Source B", pass.composite.sourceB, pass.composite.sourceBPass,
    AnimationProperty::CompositeSourceB, AnimationProperty::CompositeSourceBPass);
  ImGui::PopID();
  if (pass.composite.sourceA == CompositeSource::FixedColor ||
      pass.composite.sourceB == CompositeSource::FixedColor) {
    const bool colorChanged = ImGui::ColorEdit4("Fixed color", &pass.composite.fixedColor.x,
      ImGuiColorEditFlags_Float);
    animationKeyControl(pass, AnimationProperty::CompositeFixedColor, timeline, colorChanged);
  }
  if (pass.composite.sourceA == CompositeSource::PreviousFrame ||
      pass.composite.sourceB == CompositeSource::PreviousFrame) {
    animationKeyControl(pass, AnimationProperty::CompositeHistoryDecay, timeline,
      ImGui::SliderFloat("History decay", &pass.composite.historyDecay, 0.0f, 1.0f, "%.3f"));
    animationKeyControl(pass, AnimationProperty::CompositeHistoryUvOffset, timeline,
      ImGui::DragFloat2("History UV offset", &pass.composite.historyUvOffset.x, 1.0f / 1024.0f,
        -1.0f, 1.0f, "%.5f"));
    animationKeyControl(pass, AnimationProperty::CompositeHistoryUvScale, timeline,
      ImGui::DragFloat2("History UV scale", &pass.composite.historyUvScale.x, 0.001f,
        0.25f, 4.0f, "%.4f"));
    description("Previous frame reads the last completed composite. Decay is applied before color arithmetic.");
  }
  description("A and B are independent inputs. A render-pass source reads that pass's raw output, not its composited result.");

  ImGui::Spacing();
  ImGui::TextDisabled("COLOR ARITHMETIC");
  int operation = static_cast<int>(pass.composite.operation);
  constexpr int operationCount = static_cast<int>(RelationOperator::BitwiseXor) + 1;
  std::array<const char*, operationCount> operationLabels{};
  for (int index = 0; index < operationCount; ++index)
    operationLabels[static_cast<std::size_t>(index)] = relationOperatorLabel(static_cast<RelationOperator>(index));
  if (ImGui::Combo("Operation", &operation, operationLabels.data(), operationCount)) {
    pass.composite.operation = static_cast<RelationOperator>(operation);
    resetCompositeTransform(pass.composite);
  }
  animationKeyControl(pass, AnimationProperty::CompositeOperation, timeline, ImGui::IsItemEdited());
  if (pass.composite.operation == RelationOperator::BitwiseXor) {
    const bool bitsChanged = ImGui::SliderInt("Integer channel bits", &pass.composite.bitDepth, 1, 8);
    animationKeyControl(pass, AnimationProperty::CompositeBitDepth, timeline, bitsChanged);
  }
  animationKeyControl(pass, AnimationProperty::CompositeOpacity, timeline,
    ImGui::SliderFloat("Opacity", &pass.composite.opacity, 0.0f, 1.0f, "%.2f"));
  animationKeyControl(pass, AnimationProperty::CompositeGain, timeline,
    ImGui::SliderFloat("Gain", &pass.composite.gain, 0.1f, 16.0f, "%.2fx", ImGuiSliderFlags_Logarithmic));
  animationKeyControl(pass, AnimationProperty::CompositeBias, timeline,
    ImGui::SliderFloat("Bias", &pass.composite.bias, -1.0f, 1.0f, "%.3f"));
  ImGui::Text("Per RGB channel: %s", relationOperatorEquation(pass.composite.operation));
  description(relationOperatorMeaning(pass.composite.operation));

  const char* colorSpaces[] = {"Encoded RGB values", "Linear-light values"};
  int colorSpace = static_cast<int>(pass.composite.colorSpace);
  const bool colorSpaceChanged = ImGui::Combo("Arithmetic color space", &colorSpace, colorSpaces, 2);
  if (colorSpaceChanged)
    pass.composite.colorSpace = static_cast<CompositeColorSpace>(colorSpace);
  animationKeyControl(pass, AnimationProperty::CompositeColorSpace, timeline, colorSpaceChanged);
  const char* ranges[] = {"Clamp to 0..1", "Preserve signed / HDR", "Wrap with fract"};
  int range = static_cast<int>(pass.composite.range);
  const bool rangeChanged = ImGui::Combo("Range behavior", &range, ranges, 3);
  if (rangeChanged) pass.composite.range = static_cast<CompositeRange>(range);
  animationKeyControl(pass, AnimationProperty::CompositeRange, timeline, rangeChanged);
  const char* masks[] = {"None", "Pass luminance", "Pass depth (0..10 units)", "Pass image edges"};
  int mask = static_cast<int>(pass.composite.mask);
  const bool maskChanged = ImGui::Combo("Mask", &mask, masks, 4);
  if (maskChanged) pass.composite.mask = static_cast<CompositeMask>(mask);
  animationKeyControl(pass, AnimationProperty::CompositeMask, timeline, maskChanged);
  if (pass.composite.mask != CompositeMask::None) {
    ImGui::SameLine();
    animationKeyControl(pass, AnimationProperty::CompositeMaskInverted, timeline,
      ImGui::Checkbox("Invert", &pass.composite.invertMask));
  }
}

} // namespace gfxlab::ui
