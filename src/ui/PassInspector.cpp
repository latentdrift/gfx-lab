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

void drawPassInspector(RenderStack& stack, AnimationTimeline& timeline) {
  RenderPass& pass = stack.selected();
  ImGui::TextDisabled("SELECTED RENDER PASS");
  std::array<char, 64> name{};
  std::snprintf(name.data(), name.size(), "%s", pass.name.c_str());
  if (ImGui::InputText("Name", name.data(), name.size())) pass.name = name.data();

  const char* outputLabels[] = {"Color", "Linear depth", "Normals", "Vertex colors"};
  int output = static_cast<int>(pass.output);
  if (ImGui::Combo("Output buffer", &output, outputLabels, 4)) pass.output = static_cast<PassOutput>(output);

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
  ImGui::TextDisabled("COMPOSITE INTO PREVIOUS RESULT");
  int operation = static_cast<int>(pass.composite.operation);
  constexpr int operationCount = static_cast<int>(RelationOperator::RelativeDifference) + 1;
  std::array<const char*, operationCount> operationLabels{};
  for (int index = 0; index < operationCount; ++index)
    operationLabels[static_cast<std::size_t>(index)] = relationOperatorLabel(static_cast<RelationOperator>(index));
  if (ImGui::Combo("Operation", &operation, operationLabels.data(), operationCount)) {
    pass.composite.operation = static_cast<RelationOperator>(operation);
    resetCompositeTransform(pass.composite);
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
  if (ImGui::Combo("Arithmetic color space", &colorSpace, colorSpaces, 2))
    pass.composite.colorSpace = static_cast<CompositeColorSpace>(colorSpace);
  const char* ranges[] = {"Clamp to 0..1", "Preserve signed / HDR", "Wrap with fract"};
  int range = static_cast<int>(pass.composite.range);
  if (ImGui::Combo("Range behavior", &range, ranges, 3)) pass.composite.range = static_cast<CompositeRange>(range);
  const char* masks[] = {"None", "Pass luminance", "Pass depth (0..10 units)", "Pass image edges"};
  int mask = static_cast<int>(pass.composite.mask);
  if (ImGui::Combo("Mask", &mask, masks, 4)) pass.composite.mask = static_cast<CompositeMask>(mask);
  if (pass.composite.mask != CompositeMask::None) {
    ImGui::SameLine();
    ImGui::Checkbox("Invert", &pass.composite.invertMask);
  }
}

} // namespace gfxlab::ui
