#include "ui/PassInspector.hpp"

#include "app/RenderStack.hpp"
#include "ui/AnimationControls.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <initializer_list>
#include <string>

namespace gfxlab::ui {
namespace {

void description(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

void centeredText(const char* text) {
  const float offset = std::max(0.0f,
    (ImGui::GetContentRegionAvail().x - ImGui::CalcTextSize(text).x) * 0.5f);
  ImGui::SetCursorPosX(ImGui::GetCursorPosX() + offset);
  ImGui::TextDisabled("%s", text);
}

const char* compactSourceLabel(const CompositeSource source) {
  switch (source) {
    case CompositeSource::Accumulator: return "Accumulated result";
    case CompositeSource::CurrentPass: return "Current pass";
    case CompositeSource::RenderPass: return "Named render pass";
    case CompositeSource::FixedColor: return "Fixed RGBA color";
    case CompositeSource::PreviousFrame: return "Previous frame";
    case CompositeSource::RenderPassField: return "Named pass field";
  }
  return "Unknown source";
}

struct SourceNodeResult {
  bool sourceChanged{false};
  bool passChanged{false};
  bool interpretationChanged{false};
  float anchorScreenY{0.0f};
};

SourceNodeResult sourceNode(const char* id, const char* title, CompositeSource& source,
    int& sourcePassId, CompositeInterpretation& interpretation, RenderStack& stack) {
  SourceNodeResult result;
  ImGui::PushID(id);
  const float width = ImGui::GetContentRegionAvail().x;
  const std::string label = std::string(title) + "\n" + compactSourceLabel(source);
  if (ImGui::Button(label.c_str(), ImVec2(width, 48.0f))) ImGui::OpenPopup("source-palette");
  result.anchorScreenY = ImGui::GetItemRectMin().y;
  if (ImGui::BeginPopup("source-palette")) {
    ImGui::TextDisabled("%s SIGNAL", title);
    constexpr CompositeSource simpleSources[] = {
      CompositeSource::Accumulator, CompositeSource::CurrentPass,
      CompositeSource::FixedColor, CompositeSource::PreviousFrame};
    for (const CompositeSource candidate : simpleSources) {
      if (ImGui::Selectable(compactSourceLabel(candidate), source == candidate)) {
        source = candidate;
        result.sourceChanged = true;
      }
    }
    ImGui::SeparatorText("RAW RENDER PASS");
    for (const RenderPass& candidate : stack.passes()) {
      const bool selected = source == CompositeSource::RenderPass && sourcePassId == candidate.id;
      if (ImGui::Selectable(candidate.name.c_str(), selected)) {
        result.sourceChanged = source != CompositeSource::RenderPass;
        result.passChanged = sourcePassId != candidate.id;
        source = CompositeSource::RenderPass;
        sourcePassId = candidate.id;
      }
    }
    ImGui::SeparatorText("FIELD BUFFER");
    for (const RenderPass& candidate : stack.passes()) {
      const bool selected = source == CompositeSource::RenderPassField && sourcePassId == candidate.id;
      const std::string fieldLabel = candidate.name + " field";
      if (ImGui::Selectable(fieldLabel.c_str(), selected)) {
        result.sourceChanged = source != CompositeSource::RenderPassField;
        result.passChanged = sourcePassId != candidate.id;
        source = CompositeSource::RenderPassField;
        sourcePassId = candidate.id;
      }
    }
    ImGui::EndPopup();
  }
  constexpr const char* interpretationLabels[] = {"Raw RGB", "L cone response", "M cone response",
    "S cone response", "Cone luminance", "Rod response", "Red-green opponent (centered)",
    "Blue-yellow opponent (centered)"};
  int interpretationIndex = static_cast<int>(interpretation);
  ImGui::SetNextItemWidth(-1.0f);
  result.interpretationChanged = ImGui::Combo("##interpretation", &interpretationIndex,
    interpretationLabels, 8);
  if (result.interpretationChanged)
    interpretation = static_cast<CompositeInterpretation>(interpretationIndex);
  ImGui::PopID();
  return result;
}

struct OperationPaletteResult {
  bool changed{false};
  float anchorScreenY{0.0f};
};

OperationPaletteResult operationPalette(RelationOperator& operation) {
  OperationPaletteResult result;
  const std::string label = std::string(relationOperatorEquation(operation)) + "\n" +
    relationOperatorLabel(operation);
  if (ImGui::Button(label.c_str(), ImVec2(-1.0f, 52.0f))) ImGui::OpenPopup("operation-palette");
  result.anchorScreenY = ImGui::GetItemRectMin().y;
  if (!ImGui::BeginPopup("operation-palette")) return result;
  const auto group = [&](const char* heading, const std::initializer_list<RelationOperator> operators) {
    ImGui::SeparatorText(heading);
    for (const RelationOperator candidate : operators) {
      const std::string entry = std::string(relationOperatorEquation(candidate)) + "    " +
        relationOperatorLabel(candidate);
      if (ImGui::Selectable(entry.c_str(), operation == candidate)) {
        operation = candidate;
        result.changed = true;
      }
    }
  };
  group("COMPARE / DISAGREE", {RelationOperator::AbsoluteDifference, RelationOperator::SignedDifference,
    RelationOperator::PositiveAMinusB, RelationOperator::PositiveBMinusA,
    RelationOperator::Exclusion, RelationOperator::ANotB, RelationOperator::RelativeDifference});
  group("LIGHTEN / DARKEN", {RelationOperator::Multiply, RelationOperator::Screen,
    RelationOperator::Minimum, RelationOperator::Maximum, RelationOperator::CenteredSum});
  group("HARDWARE COLOR MATH", {RelationOperator::Add, RelationOperator::Average,
    RelationOperator::HardwareSubtract, RelationOperator::HardwareReverseSubtract,
    RelationOperator::QuarterAdd, RelationOperator::SignedColorOffset});
  group("INTEGER LOGIC", {RelationOperator::BitwiseXor});
  ImGui::EndPopup();
  return result;
}

} // namespace

void drawPassInspector(RenderStack& stack, AnimationTimeline& timeline, const bool globalScope) {
  RenderPass& pass = stack.selected();
  ImGui::TextDisabled(globalScope ? "GLOBAL BASE" : "SELECTED RENDER PASS");
  if (!globalScope) {
    std::array<char, 64> name{};
    std::snprintf(name.data(), name.size(), "%s", pass.name.c_str());
    if (ImGui::InputText("Name", name.data(), name.size())) pass.name = name.data();

    const char* outputLabels[] = {"Color", "Linear depth", "Normals", "Vertex colors", "Field signal preview"};
    int output = static_cast<int>(pass.output);
    const bool outputChanged = ImGui::Combo("Output buffer", &output, outputLabels, 5);
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
  animationKeyControl(pass, AnimationProperty::CameraLateral, timeline,
    ImGui::DragFloat("Camera lateral (eye) offset", &pass.perturbation.cameraLateral,
      0.001f, -1.0f, 1.0f, "%.3f unit"));
  animationKeyControl(pass, AnimationProperty::StereoConvergence, timeline,
    ImGui::DragFloat("Stereo convergence distance", &pass.perturbation.stereoConvergence,
      0.01f, 0.05f, 100.0f, "%.2f units"));
  description("Lateral offset translates an eye camera without rotating it. Perspective projection uses an off-axis frustum so points at the convergence distance have zero disparity.");
  animationKeyControl(pass, AnimationProperty::FieldOfViewOffset, timeline,
    ImGui::DragFloat("Field-of-view offset", &pass.perturbation.fieldOfView, 0.05f, -20.0f, 20.0f, "%.2f deg"));

  ImGui::Spacing();
  ImGui::TextDisabled("SAMPLING PERTURBATION");
  animationKeyControl(pass, AnimationProperty::UvOffset, timeline,
    ImGui::DragFloat2("UV offset", &pass.perturbation.uvOffset.x, 1.0f / 512.0f, -1.0f, 1.0f, "%.5f"));
  animationKeyControl(pass, AnimationProperty::UvScale, timeline,
    ImGui::DragFloat2("UV scale", &pass.perturbation.uvScale.x, 0.001f, 0.25f, 4.0f, "%.4f"));
  animationKeyControl(pass, AnimationProperty::UvRotation, timeline,
    ImGui::SliderAngle("UV rotation", &pass.perturbation.uvRotation, -180.0f, 180.0f, "%.1f deg"));
  animationKeyControl(pass, AnimationProperty::UvPivot, timeline,
    ImGui::DragFloat2("UV pivot", &pass.perturbation.uvPivot.x, 0.005f, -2.0f, 2.0f, "%.3f"));

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
  ImGui::TextDisabled("COMPOSITE SIGNAL FLOW");
  if (ImGui::BeginTable("composite-inputs", 2, ImGuiTableFlags_SizingStretchSame)) {
    ImGui::TableNextColumn();
    const SourceNodeResult sourceAChanged = sourceNode("source-a", "A", pass.composite.sourceA,
      pass.composite.sourceAPassId, pass.composite.interpretationA, stack);
    animationKeyControlAt(pass, AnimationProperty::CompositeSourceA, timeline,
      sourceAChanged.sourceChanged, sourceAChanged.anchorScreenY + 3.0f);
    animationKeyControlAt(pass, AnimationProperty::CompositeSourceAPass, timeline,
      sourceAChanged.passChanged, sourceAChanged.anchorScreenY + 25.0f);
    animationKeyControl(pass, AnimationProperty::CompositeInterpretationA, timeline,
      sourceAChanged.interpretationChanged);
    ImGui::TableNextColumn();
    const SourceNodeResult sourceBChanged = sourceNode("source-b", "B", pass.composite.sourceB,
      pass.composite.sourceBPassId, pass.composite.interpretationB, stack);
    animationKeyControlAt(pass, AnimationProperty::CompositeSourceB, timeline,
      sourceBChanged.sourceChanged, sourceBChanged.anchorScreenY + 3.0f);
    animationKeyControlAt(pass, AnimationProperty::CompositeSourceBPass, timeline,
      sourceBChanged.passChanged, sourceBChanged.anchorScreenY + 25.0f);
    animationKeyControl(pass, AnimationProperty::CompositeInterpretationB, timeline,
      sourceBChanged.interpretationChanged);
    ImGui::EndTable();
  }
  centeredText("A + B feed");
  const OperationPaletteResult operationChanged = operationPalette(pass.composite.operation);
  if (operationChanged.changed) resetCompositeTransform(pass.composite);
  animationKeyControlAt(pass, AnimationProperty::CompositeOperation, timeline,
    operationChanged.changed, operationChanged.anchorScreenY + 17.0f);
  centeredText("OUTPUT");
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
  description("A and B independently choose both a signal source and an interpretation. Receptor interpretations measure encoded RGB through an approximate observer before arithmetic. A named pass field remains a dedicated scalar field buffer rather than material color.");
  if (pass.composite.interpretationA != CompositeInterpretation::RawRgb ||
      pass.composite.interpretationB != CompositeInterpretation::RawRgb) {
    DisplayReconstructionState& observer = stack.display();
    ImGui::SeparatorText("RGB OBSERVER APPROXIMATION");
    ImGui::SliderFloat("Receptor exposure", &observer.observerExposureStops, -6.0f, 6.0f, "%+.1f stops");
    ImGui::SliderFloat("Rod sensitivity", &observer.rodSensitivity, 0.25f, 16.0f, "%.2fx",
      ImGuiSliderFlags_Logarithmic);
    ImGui::SliderFloat("Opponent gain", &observer.opponentGain, 0.25f, 16.0f, "%.2fx",
      ImGuiSliderFlags_Logarithmic);
    description("These parameters belong to the shared RGB observer model. Operand interpretation runs before composite color-space conversion and does not require final display reconstruction to be enabled.");
  }

  ImGui::Spacing();
  ImGui::TextDisabled("COLOR ARITHMETIC PARAMETERS");
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
  const char* masks[] = {"None", "Pass luminance", "Pass depth (0..10 units)", "Pass image edges", "Pass field signal"};
  int mask = static_cast<int>(pass.composite.mask);
  const bool maskChanged = ImGui::Combo("Mask", &mask, masks, 5);
  if (maskChanged) pass.composite.mask = static_cast<CompositeMask>(mask);
  animationKeyControl(pass, AnimationProperty::CompositeMask, timeline, maskChanged);
  if (pass.composite.mask != CompositeMask::None) {
    ImGui::SameLine();
    animationKeyControl(pass, AnimationProperty::CompositeMaskInverted, timeline,
      ImGui::Checkbox("Invert", &pass.composite.invertMask));
  }
  if (pass.composite.mask == CompositeMask::PassField)
    description("The mask reads B's named pass field when B is a named pass source; otherwise it reads the current pass field.");
}

} // namespace gfxlab::ui
