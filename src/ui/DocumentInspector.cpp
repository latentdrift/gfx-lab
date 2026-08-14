#include "ui/DocumentInspector.hpp"

#include "app/HardwareProfile.hpp"
#include "app/RenderOperationState.hpp"
#include "ui/AnimationControls.hpp"
#include "ui/DisplayInspector.hpp"
#include "ui/Inspector.hpp"
#include "ui/TextureMappingEditor.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cstdio>
#include <optional>
#include <string>
#include <type_traits>

namespace gfxlab::ui {
namespace {

RenderPass renderView(const document::Document& document,
    const std::optional<document::OperationId> operation, const float timeSeconds) {
  RenderPass pass;
  pass.name = operation.has_value() && document::findOperation(document, *operation) != nullptr
    ? document::findOperation(document, *operation)->name : "Render Defaults";
  pass.renderer = document.renderDefaults.renderer;
  pass.textureSource = document.renderDefaults.texture.source;
  pass.importedTexture = document.renderDefaults.texture.imported;
  pass.importedTextureSrgb = document.renderDefaults.texture.srgb;
  const auto applyTracks = [&](const document::ObjectId owner) {
    for (const document::AnimationTrack& track : document.automation.animation) {
      if (track.target.owner != owner || track.keyframes.empty()) continue;
      const std::optional<AnimationProperty> property =
        document::animationProperty(track.target.property);
      if (!property.has_value()) continue;
      pass.animation.tracks.push_back({*property, track.interpolation, track.keyframes});
      setAnimationPropertyValue(pass, *property,
        samplePropertyTrack(pass.animation.tracks.back(), timeSeconds));
    }
  };
  applyTracks(document::renderDefaultsObject);
  if (!operation.has_value()) return pass;
  const document::Operation* authored = document::findOperation(document, *operation);
  const auto* render = authored == nullptr
    ? nullptr : std::get_if<document::RenderOperation>(&authored->data);
  if (render == nullptr) return pass;
  pass.id = static_cast<int>(authored->id.value);
  pass.enabled = authored->enabled;
  pass.perturbation = render->perturbation;
  pass.output = render->presentedOutput;
  pass.textureSource = render->texture.source;
  pass.importedTexture = render->texture.imported;
  pass.importedTextureSrgb = render->texture.srgb;
  pass.overrides = render->overrides;
  for (const PropertyOverride& overrideValue : render->overrides)
    setAnimationPropertyValue(pass, overrideValue.property, overrideValue.value);
  pass.animation.tracks.clear();
  applyTracks(document::operationObject(*operation));
  return pass;
}

bool tracksEqual(const PassAnimation& a, const PassAnimation& b) {
  if (a.tracks.size() != b.tracks.size()) return false;
  for (std::size_t index = 0; index < a.tracks.size(); ++index) {
    const PropertyAnimationTrack& left = a.tracks[index];
    const PropertyAnimationTrack& right = b.tracks[index];
    if (left.property != right.property || left.interpolation != right.interpolation ||
        left.keyframes.size() != right.keyframes.size()) return false;
    for (std::size_t key = 0; key < left.keyframes.size(); ++key)
      if (left.keyframes[key].timeSeconds != right.keyframes[key].timeSeconds ||
          left.keyframes[key].value != right.keyframes[key].value) return false;
  }
  return true;
}

bool displayEqual(const DisplayReconstructionState& a, const DisplayReconstructionState& b) {
  return a.enabled == b.enabled && a.signal == b.signal && a.chromaBleed == b.chromaBleed &&
    a.lumaChromaCrosstalk == b.lumaChromaCrosstalk &&
    a.scanlineStrength == b.scanlineStrength && a.phosphorMaskStrength == b.phosphorMaskStrength &&
    a.bloomStrength == b.bloomStrength && a.bloomRadiusPixels == b.bloomRadiusPixels &&
    a.observerExposureStops == b.observerExposureStops && a.darkAdaptation == b.darkAdaptation &&
    a.rodSensitivity == b.rodSensitivity && a.opponentGain == b.opponentGain &&
    a.receptorXorBits == b.receptorXorBits;
}

bool renderViewChanged(const RenderPass& before, const RenderPass& after) {
  if (before.name != after.name || before.enabled != after.enabled ||
      before.importedTexture != after.importedTexture ||
      before.importedTextureSrgb != after.importedTextureSrgb) return true;
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    if (!animationPropertyValuesEqual(property, animationPropertyValue(before, property),
        animationPropertyValue(after, property))) return true;
  }
  return !tracksEqual(before.animation, after.animation);
}

void replaceTracks(document::Document& document, const document::ObjectId owner,
    const PassAnimation& animation) {
  document.automation.animation.erase(std::remove_if(document.automation.animation.begin(),
    document.automation.animation.end(), [owner](const document::AnimationTrack& track) {
      return track.target.owner == owner;
    }), document.automation.animation.end());
  for (const PropertyAnimationTrack& track : animation.tracks)
    document.automation.animation.push_back({{owner, document::propertyId(track.property)},
      track.interpolation, track.keyframes});
}

void commitRenderView(document::Document& document,
    const std::optional<document::OperationId> operation, const RenderPass& edited,
    const float timeSeconds) {
  if (!operation.has_value()) {
    document.renderDefaults.renderer = edited.renderer;
    document.renderDefaults.texture = {edited.textureSource, edited.importedTexture,
      edited.importedTextureSrgb};
    replaceTracks(document, document::renderDefaultsObject, edited.animation);
    return;
  }
  document::Operation* authored = document::findOperation(document, *operation);
  auto* render = authored == nullptr ? nullptr
    : std::get_if<document::RenderOperation>(&authored->data);
  if (render == nullptr) return;
  authored->name = edited.name;
  authored->enabled = edited.enabled;
  render->perturbation = edited.perturbation;
  render->presentedOutput = edited.output;
  render->texture = {edited.textureSource, edited.importedTexture, edited.importedTextureSrgb};
  RenderPass inherited = renderView(document, std::nullopt, timeSeconds);
  render->overrides.clear();
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    if (animationPropertyIsPassLocal(property)) continue;
    const glm::vec4 value = animationPropertyValue(edited, property);
    if (!animationPropertyValuesEqual(property, value,
        animationPropertyValue(inherited, property)))
      render->overrides.push_back({property, value});
  }
  replaceTracks(document, document::operationObject(*operation), edited.animation);
}

const document::SignalDescriptor* descriptor(const document::Document& document,
    const document::SignalRef signal) {
  return document::findSignal(document, signal.id);
}

void drawSignalDescriptor(const document::SignalDescriptor& signal,
    editor::EditorState& editorState) {
  ImGui::PushID(signal.key.c_str());
  ImGui::TextUnformatted(signal.name.c_str());
  ImGui::TextDisabled("%s", document::signalDescriptorSummary(signal).c_str());
  ImGui::TextDisabled("Encoding: %s", document::signalEncodingLabel(signal.metadata.encoding));
  if (!signal.metadata.units.empty()) {
    ImGui::SameLine();
    ImGui::TextDisabled("· %s", signal.metadata.units.c_str());
  }
  if (signal.metadata.hasKnownRange)
    ImGui::TextDisabled("Range: %.3g .. %.3g", signal.metadata.knownRange.x,
      signal.metadata.knownRange.y);
  if (ImGui::SmallButton("View")) editorState.viewer.viewed = {signal.id, 0};
  ImGui::PopID();
}

std::string overrideValueText(const PropertyOverride& overrideValue) {
  const AnimationPropertyInfo& info = animationPropertyInfo(overrideValue.property);
  std::array<char, 128> text{};
  if (info.components == 1) std::snprintf(text.data(), text.size(), "%.3g", overrideValue.value.x);
  else if (info.components == 2) std::snprintf(text.data(), text.size(), "%.3g, %.3g",
    overrideValue.value.x, overrideValue.value.y);
  else if (info.components == 3) std::snprintf(text.data(), text.size(), "%.3g, %.3g, %.3g",
    overrideValue.value.x, overrideValue.value.y, overrideValue.value.z);
  else std::snprintf(text.data(), text.size(), "%.3g, %.3g, %.3g, %.3g",
    overrideValue.value.x, overrideValue.value.y, overrideValue.value.z, overrideValue.value.w);
  return text.data();
}

bool signalPicker(const char* label, document::Document& document,
    editor::EditorState& editorState, const document::OperationId consumer,
    document::SignalRef& selected,
    const std::optional<document::SignalShape> requiredShape = std::nullopt,
    const std::optional<document::SignalSemantic> requiredSemantic = std::nullopt) {
  bool changed = false;
  const document::SignalDescriptor* current = descriptor(document, selected);
  const document::Operation* currentProducer = current == nullptr ? nullptr
    : document::findOperation(document, current->producer);
  const std::string currentLabel = current == nullptr ? "Select signal"
    : (currentProducer != nullptr ? currentProducer->name + " / " : std::string{}) + current->name;
  if (ImGui::BeginCombo(label, currentLabel.c_str())) {
    for (const document::Operation& operation : document.operations) {
      if (operation.id == consumer) continue;
      for (const document::SignalDescriptor& output : operation.outputs) {
        if (requiredShape.has_value() && output.shape != *requiredShape) continue;
        if (requiredSemantic.has_value() && output.metadata.semantic != *requiredSemantic) continue;
        if (requiredShape == document::SignalShape::Scalar &&
            output.metadata.domain != document::SignalDomain::Screen2D) continue;
        const std::string name = operation.name + " / " + output.name;
        const std::string widgetLabel = name + "##signal_" +
          std::to_string(operation.id.value) + "_" + output.key;
        if (ImGui::Selectable(widgetLabel.c_str(), selected.id == output.id)) {
          selected = {output.id, 0};
          changed = true;
        }
      }
    }
    ImGui::EndCombo();
  }
  if (current != nullptr) {
    ImGui::PushID(label);
    ImGui::SameLine();
    if (ImGui::SmallButton("View")) editorState.viewer.viewed = selected;
    ImGui::SameLine();
    if (ImGui::SmallButton("Go"))
      editorState.selection = {editor::SelectionKind::Operation, current->producer};
    ImGui::PopID();
  }
  return changed;
}

bool drawOperation(document::Document& document, document::Operation& operation,
    editor::EditorState& editorState, const SignalMeasurement* measurement) {
  bool changed = false;
  if (auto* composite = std::get_if<document::CompositeOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUTS");
    changed |= signalPicker("Source A", document, editorState, operation.id, composite->a);
    changed |= signalPicker("Source B", document, editorState, operation.id, composite->b);
    ImGui::SeparatorText("RELATIONSHIP");
    int blend = static_cast<int>(composite->arithmetic.operation);
    if (ImGui::BeginCombo("Mode", relationOperatorLabel(composite->arithmetic.operation))) {
      for (int index = 0; index <= static_cast<int>(RelationOperator::Normal); ++index) {
        if (!ImGui::Selectable(relationOperatorLabel(static_cast<RelationOperator>(index)),
            blend == index)) continue;
        composite->arithmetic.operation = static_cast<RelationOperator>(index);
        changed = true;
      }
      ImGui::EndCombo();
    }
    changed |= ImGui::SliderFloat("Opacity", &composite->arithmetic.opacity, 0.0f, 1.0f);
    changed |= ImGui::DragFloat("Gain", &composite->arithmetic.gain, 0.01f, 0.0f, 16.0f);
    changed |= ImGui::DragFloat("Bias", &composite->arithmetic.bias, 0.01f, -1.0f, 1.0f);
    ImGui::SeparatorText("MASK");
    changed |= signalPicker("Mask input", document, editorState, operation.id, composite->mask,
      document::SignalShape::Scalar);
    if (composite->mask) {
      ImGui::SameLine();
      if (ImGui::Button("Clear")) { composite->mask = {}; changed = true; }
      changed |= ImGui::Checkbox("Invert mask", &composite->invertMask);
    }
  } else if (auto* interpret = std::get_if<document::InterpretOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUT");
    changed |= signalPicker("Spectrum", document, editorState, operation.id, interpret->spectrum,
      document::SignalShape::Spectrum16);
    ImGui::SeparatorText("INTERPRETATION");
    changed |= ImGui::DragFloat("Exposure", &interpret->exposureStops, 0.05f, -12.0f, 12.0f);
    changed |= ImGui::DragFloat("Gain", &interpret->gain, 0.01f, 0.0f, 16.0f);
    changed |= ImGui::DragFloat("Bias", &interpret->bias, 0.01f, -1.0f, 1.0f);
  } else if (auto* stereo = std::get_if<document::StereoOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUTS");
    changed |= signalPicker("Left", document, editorState, operation.id, stereo->left,
      document::SignalShape::Vector4, document::SignalSemantic::Color);
    changed |= signalPicker("Right", document, editorState, operation.id, stereo->right,
      document::SignalShape::Vector4, document::SignalSemantic::Color);
    ImGui::SeparatorText("ANALYSIS");
    int mode = static_cast<int>(stereo->mode);
    constexpr const char* modes[] = {"Anaglyph", "Signed disparity", "Absolute disparity",
      "Correspondence", "Monocular occlusion"};
    if (ImGui::Combo("Output", &mode, modes, 5)) { stereo->mode = static_cast<StereoAnalysisMode>(mode); changed = true; }
    changed |= ImGui::DragFloat("Maximum disparity", &stereo->maximumDisparityPixels, 0.25f, 1.0f, 512.0f);
    changed |= ImGui::DragFloat("Occlusion tolerance", &stereo->occlusionTolerance,
      0.00005f, 0.00001f, 0.05f);
  } else if (auto* measure = std::get_if<document::MeasureOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUT");
    changed |= signalPicker("Input", document, editorState, operation.id, measure->input);
    ImGui::SeparatorText("MEASUREMENT");
    int metric = static_cast<int>(measure->metric);
    constexpr const char* metrics[] = {"Mean magnitude", "RMS", "Peak", "Coverage",
      "Mean red", "Mean green", "Mean blue"};
    if (ImGui::Combo("Metric", &metric, metrics, 7)) { measure->metric = static_cast<MeasurementMetric>(metric); changed = true; }
    changed |= ImGui::DragFloat("Threshold", &measure->threshold, 0.001f, 0.0f, 16.0f);
    changed |= ImGui::Checkbox("Absolute magnitude", &measure->absoluteMagnitude);
    if (measurement != nullptr && measurement->sampleCount > 0)
      ImGui::Text("Current value: %.5f", measurementMetricValue(*measurement, measure->metric));
    ImGui::SeparatorText("DRIVE A PROPERTY");
    const document::SignalRef measurementSignal = document::primaryOutput(operation);
    auto route = std::find_if(document.automation.modulation.begin(),
      document.automation.modulation.end(), [&](const document::ModulationRoute& candidate) {
        return candidate.source == measurementSignal;
      });
    bool driven = route != document.automation.modulation.end();
    if (ImGui::Checkbox("Enabled", &driven)) {
      if (!driven) {
        document.automation.modulation.erase(route);
      } else {
        document::Operation* target = nullptr;
        for (document::Operation& candidate : document.operations)
          if (candidate.id != operation.id &&
              std::holds_alternative<document::RenderOperation>(candidate.data)) {
            target = &candidate;
            break;
          }
        if (target != nullptr) document.automation.modulation.push_back({measurementSignal,
          {document::operationObject(target->id), document::propertyId(AnimationProperty::Ambient)}});
      }
      changed = true;
      route = std::find_if(document.automation.modulation.begin(),
        document.automation.modulation.end(), [&](const document::ModulationRoute& candidate) {
          return candidate.source == measurementSignal;
        });
    }
    if (route != document.automation.modulation.end()) {
      const std::optional<document::OperationId> targetId = document::operationFromObject(route->target.owner);
      document::Operation* target = targetId.has_value() ? document::findOperation(document, *targetId) : nullptr;
      if (ImGui::BeginCombo("Destination", target != nullptr ? target->name.c_str() : "Select operation")) {
        for (document::Operation& candidate : document.operations) {
          if (candidate.id == operation.id || std::holds_alternative<document::MeasureOperation>(candidate.data))
            continue;
          const std::string candidateLabel = candidate.name + "##operation_" +
            std::to_string(candidate.id.value);
          if (ImGui::Selectable(candidateLabel.c_str(), target != nullptr && candidate.id == target->id)) {
            route->target.owner = document::operationObject(candidate.id);
            target = &candidate;
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
      const document::PropertyDescriptor* selectedProperty = document::propertyDescriptor(route->target.property);
      if (ImGui::BeginCombo("Property", selectedProperty != nullptr
          ? selectedProperty->label.c_str() : "Select property")) {
        for (const document::PropertyDescriptor& property : document::propertyDescriptors()) {
          if (property.animation != AnimationBehavior::Continuous || property.components != 1) continue;
          bool compatible = target != nullptr && std::holds_alternative<document::RenderOperation>(target->data);
          if (target != nullptr && std::holds_alternative<document::CompositeOperation>(target->data))
            compatible = property.rendererProperty == AnimationProperty::CompositeGain ||
              property.rendererProperty == AnimationProperty::CompositeBias ||
              property.rendererProperty == AnimationProperty::CompositeOpacity ||
              property.rendererProperty == AnimationProperty::CompositeHistoryDecay;
          if (target != nullptr && std::holds_alternative<document::StereoOperation>(target->data))
            compatible = property.rendererProperty == AnimationProperty::StereoMaximumDisparity ||
              property.rendererProperty == AnimationProperty::StereoOcclusionTolerance;
          if (!compatible) continue;
          if (ImGui::Selectable(property.label.c_str(), route->target.property == property.id)) {
            route->target.property = property.id;
            route->outputRange = {property.minimum, property.maximum};
            changed = true;
          }
        }
        ImGui::EndCombo();
      }
      changed |= ImGui::DragFloat2("Input range", &route->inputRange.x, 0.005f, -16.0f, 16.0f);
      changed |= ImGui::DragFloat2("Output range", &route->outputRange.x, 0.005f, -100.0f, 100.0f);
      changed |= ImGui::Checkbox("Clamp", &route->clamp);
      changed |= ImGui::DragFloat("Smoothing", &route->smoothingSeconds, 0.01f, 0.0f, 5.0f, "%.2f s");
    }
  } else if (auto* constant = std::get_if<document::ConstantOperation>(&operation.data)) {
    ImGui::SeparatorText("VALUE");
    changed |= ImGui::ColorEdit4("Value", &constant->value.x, ImGuiColorEditFlags_Float);
  } else if (auto* luminance = std::get_if<document::LuminanceOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUT");
    changed |= signalPicker("Input", document, editorState, operation.id, luminance->input,
      std::nullopt, document::SignalSemantic::Color);
    ImGui::TextWrapped("Converts linear RGB to one brightness value per pixel.");
  } else if (auto* remap = std::get_if<document::RemapOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUT");
    changed |= signalPicker("Input", document, editorState, operation.id, remap->input);
    ImGui::SeparatorText("RANGE");
    changed |= ImGui::DragFloat2("Input range", &remap->inputLow, 0.005f, -100.0f, 100.0f);
    changed |= ImGui::DragFloat2("Output range", &remap->outputLow, 0.005f, -16.0f, 16.0f);
    changed |= ImGui::Checkbox("Clamp before output range", &remap->clamp);
    ImGui::SeparatorText("MEANING");
    int outputMeaning = remap->outputSemantic == document::SignalSemantic::MaskCoverage ? 1 : 0;
    constexpr const char* outputMeanings[] = {"Generic scalar", "Mask coverage"};
    if (ImGui::Combo("Output meaning", &outputMeaning, outputMeanings, 2)) {
      remap->outputSemantic = outputMeaning == 1 ? document::SignalSemantic::MaskCoverage
        : document::SignalSemantic::Generic;
      changed = true;
    }
  } else if (auto* edge = std::get_if<document::EdgeOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUT");
    changed |= signalPicker("Input", document, editorState, operation.id, edge->input);
    ImGui::SeparatorText("CONTROLS");
    changed |= ImGui::DragFloat("Strength", &edge->strength, 0.01f, 0.0f, 32.0f);
    ImGui::TextWrapped("Sobel magnitude from scalar change or multi-channel disagreement.");
  } else if (auto* blur = std::get_if<document::BlurOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUT");
    changed |= signalPicker("Input", document, editorState, operation.id, blur->input,
      blur->outputShape);
    ImGui::SeparatorText("CONTROLS");
    changed |= ImGui::SliderFloat("Radius", &blur->radiusPixels, 0.0f, 4.0f, "%.0f px");
  } else if (auto* threshold = std::get_if<document::ThresholdOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUT");
    changed |= signalPicker("Input", document, editorState, operation.id, threshold->input,
      document::SignalShape::Scalar);
    ImGui::SeparatorText("CONTROLS");
    changed |= ImGui::SliderFloat("Threshold", &threshold->threshold, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Softness", &threshold->softness, 0.0f, 0.5f);
  } else if (auto* gradient = std::get_if<document::GradientMapOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUT");
    changed |= signalPicker("Input", document, editorState, operation.id, gradient->input,
      document::SignalShape::Scalar);
    ImGui::SeparatorText("COLORS");
    changed |= ImGui::ColorEdit4("Low color", &gradient->lowColor.x, ImGuiColorEditFlags_Float);
    changed |= ImGui::ColorEdit4("High color", &gradient->highColor.x, ImGuiColorEditFlags_Float);
  } else if (auto* warp = std::get_if<document::WarpOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUTS");
    changed |= signalPicker("Image", document, editorState, operation.id, warp->image,
      document::SignalShape::Vector4, document::SignalSemantic::Color);
    changed |= signalPicker("Displacement", document, editorState, operation.id, warp->displacement,
      document::SignalShape::Vector2);
    ImGui::SeparatorText("CONTROLS");
    changed |= ImGui::DragFloat("Strength", &warp->strengthPixels, 0.1f, -512.0f, 512.0f, "%.1f px");
    ImGui::TextWrapped("Samples the image along the screen-space Vector2 direction.");
  }
  return changed;
}

} // namespace

void drawDocumentInspector(bool& open, document::Document& document,
    editor::EditorState& editorState, editor::CommandHistory& history,
    AnimationTimeline& timeline, const float timeSeconds, const unsigned int texturePreview,
    const SignalMeasurement* measurement) {
  if (!open) return;
  if (!ImGui::Begin("Properties", &open)) { ImGui::End(); return; }
  keepCurrentWindowVisible();
  document::Document edited = document;
  bool changed = false;
  std::optional<AnimationProperty> resetOverride;
  bool operationActionApplied = false;
  if (editorState.selection.kind == editor::SelectionKind::Operation) {
    document::Operation* selected = document::findOperation(edited, editorState.selection.operation);
    const document::Operation* authored = document::findOperation(document,
      editorState.selection.operation);
    if (selected != nullptr && authored != nullptr) {
      ImGui::TextDisabled("%s · ID %llu", document::operationTypeLabel(*selected),
        static_cast<unsigned long long>(selected->id.value));
      std::array<char, 256> name{};
      std::snprintf(name.data(), name.size(), "%s", selected->name.c_str());
      ImGui::SetNextItemWidth(-1.0f);
      if (ImGui::InputText("##operation-name", name.data(), name.size())) {
        selected->name = name.data();
        changed = true;
      }
      const document::SignalRef primary = document::primaryOutput(*authored);
      const document::SignalDescriptor* primaryDescriptor = document::findSignal(document, primary.id);
      if (ImGui::Button("View Output")) editorState.viewer.viewed = primary;
      ImGui::SameLine();
      if (ImGui::Button("Duplicate")) {
        const document::OperationId duplicate = document::nextOperationId(document);
        if (history.execute(document, editor::DuplicateOperation{authored->id, duplicate,
            static_cast<std::size_t>(-1)}).applied) {
          editorState.selection = {editor::SelectionKind::Operation, duplicate};
          operationActionApplied = true;
        }
      }
      ImGui::SameLine();
      ImGui::BeginDisabled(primaryDescriptor == nullptr || !document::isColor(*primaryDescriptor));
      if (ImGui::Button("Compare")) {
        const document::OperationId duplicate = document::nextOperationId(document);
        const document::OperationId comparison{duplicate.value + 1};
        if (history.execute(document, editor::DuplicateAndCompare{authored->id, duplicate,
            comparison}).applied) {
          editorState.selection = {editor::SelectionKind::Operation, duplicate};
          editorState.viewer.viewed = document.presentation.input;
          editorState.viewer.comparison = primary;
          operationActionApplied = true;
        }
      }
      ImGui::EndDisabled();
      ImGui::SameLine();
      if (ImGui::Button(selected->enabled ? "Bypass" : "Enable")) {
        operationActionApplied = history.execute(document,
          editor::SetOperationEnabled{authored->id, !authored->enabled}).applied;
      }
      ImGui::SameLine();
      if (ImGui::Button("...##operation-actions")) ImGui::OpenPopup("operation-actions");
      if (ImGui::BeginPopup("operation-actions")) {
        if (ImGui::MenuItem("Set Primary Output as Final")) {
          operationActionApplied = history.execute(document, editor::SetFinalSignal{primary}).applied;
          if (operationActionApplied) editorState.viewer.viewed = document.presentation.input;
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Delete Operation")) {
          operationActionApplied = history.execute(document,
            editor::RemoveOperation{authored->id}).applied;
          if (operationActionApplied) editorState.selection = {};
        }
        ImGui::EndPopup();
      }
      ImGui::Separator();
    }
  }
  if (operationActionApplied) { ImGui::End(); return; }
  if (editorState.selection.kind == editor::SelectionKind::Scene) {
    ImGui::TextDisabled("SCENE");
    ImGui::TextUnformatted("World, assets, and document camera");
    ImGui::TextWrapped("Scene selection and the signal shown on the canvas are independent.");
  } else if (editorState.selection.kind == editor::SelectionKind::Presentation) {
    ImGui::TextDisabled("FINAL OUTPUT");
    const DisplayReconstructionState before = edited.presentation.reconstruction;
    drawDisplayInspectorContents(edited.presentation.reconstruction);
    changed = !displayEqual(before, edited.presentation.reconstruction);
  } else if (editorState.selection.kind == editor::SelectionKind::RenderDefaults ||
      (editorState.selection.kind == editor::SelectionKind::Operation &&
       document::findOperation(edited, editorState.selection.operation) != nullptr &&
       std::holds_alternative<document::RenderOperation>(
         document::findOperation(edited, editorState.selection.operation)->data))) {
    const std::optional<document::OperationId> operation =
      editorState.selection.kind == editor::SelectionKind::Operation
      ? std::optional{editorState.selection.operation} : std::nullopt;
    const RenderPass before = renderView(edited, operation, timeSeconds);
    RenderPass view = before;
    if (!operation.has_value()) {
      ImGui::TextDisabled("RENDER DEFAULTS");
      ImGui::TextUnformatted("Shared source state for every Render operation");
    }
    if (ImGui::CollapsingHeader("Essentials", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (!operation.has_value()) {
        drawInspector(Category::Camera, view, edited.hardwareProfile, timeline,
          edited.scene.importedModel.get(), edited.scene.testScene);
      } else {
        auto* render = std::get_if<document::RenderOperation>(
          &document::findOperation(edited, *operation)->data);
        const document::ObjectId owner = document::operationObject(*operation);
        const auto timeControl = [&](const char* label, float& value,
            const document::PropertyId property, const float speed) {
          const document::PropertyDescriptor* descriptor = document::propertyDescriptor(property);
          const bool valueChanged = ImGui::DragFloat(label, &value, speed,
            descriptor != nullptr ? descriptor->minimum : -60.0f,
            descriptor != nullptr ? descriptor->maximum : 60.0f, "%.3f");
          ImGui::SameLine();
          const bool keyRequested = ImGui::SmallButton((std::string("◇##") + label).c_str());
          if (valueChanged || keyRequested) changed = true;
          if ((valueChanged && timeline.autoKey) || keyRequested)
            static_cast<void>(editor::applyCommand(edited, editor::SetKeyframe{
              {owner, property}, timeline.timeSeconds, glm::vec4(value)}));
        };

        ImGui::TextDisabled("OBJECT VARIATION");
        animationKeyControl(view, AnimationProperty::ModelTranslation, timeline,
          ImGui::DragFloat3("Position", &view.perturbation.modelTranslation.x,
            0.01f, -10.0f, 10.0f, "%.3f"));
        animationKeyControl(view, AnimationProperty::ModelScale, timeline,
          ImGui::DragFloat("Scale", &view.perturbation.modelScale, 0.01f, 0.01f, 8.0f, "%.3f"));
        animationKeyControl(view, AnimationProperty::NormalInflation, timeline,
          ImGui::DragFloat("Normal inflation", &view.perturbation.normalInflation,
            0.005f, -2.0f, 2.0f, "%.3f"));

        ImGui::Separator();
        ImGui::TextDisabled("CAMERA VARIATION");
        animationKeyControl(view, AnimationProperty::CameraYaw, timeline,
          ImGui::SliderAngle("Yaw offset", &view.perturbation.cameraYaw, -45.0f, 45.0f));
        animationKeyControl(view, AnimationProperty::CameraPitch, timeline,
          ImGui::SliderAngle("Pitch offset", &view.perturbation.cameraPitch, -45.0f, 45.0f));
        animationKeyControl(view, AnimationProperty::CameraLateral, timeline,
          ImGui::DragFloat("Lateral offset", &view.perturbation.cameraLateral,
            0.005f, -2.0f, 2.0f, "%.3f"));

        ImGui::Separator();
        ImGui::TextDisabled("IMAGE COORDINATES");
        animationKeyControl(view, AnimationProperty::UvScale, timeline,
          ImGui::DragFloat2("UV scale", &view.perturbation.uvScale.x,
            0.005f, -16.0f, 16.0f, "%.3f"));
        animationKeyControl(view, AnimationProperty::UvOffset, timeline,
          ImGui::DragFloat2("UV offset", &view.perturbation.uvOffset.x,
            1.0f / 512.0f, -4.0f, 4.0f, "%.4f"));

        ImGui::Separator();
        ImGui::TextDisabled("LOCAL PROCEDURAL TIME");
        timeControl("Time scale", render->time.scale, document::timeScaleProperty(), 0.01f);
        timeControl("Time offset", render->time.offsetSeconds, document::timeOffsetProperty(), 0.01f);
        ImGui::TextDisabled("timeline × scale + offset; negative reverses, zero freezes");
      }
    }
    if (operation.has_value() && ImGui::CollapsingHeader("Changes from Render Defaults",
        ImGuiTreeNodeFlags_DefaultOpen)) {
      auto* render = std::get_if<document::RenderOperation>(
        &document::findOperation(edited, *operation)->data);
      const bool hasTimeChanges = render != nullptr &&
        (render->time.scale != 1.0f || render->time.offsetSeconds != 0.0f);
      if (render == nullptr || (render->overrides.empty() && !hasTimeChanges &&
          view.animation.tracks.empty())) {
        ImGui::TextDisabled("No local changes. This Render follows Render Defaults.");
      } else if (render != nullptr) {
        if (render->time.scale != 1.0f) {
          ImGui::Text("Time scale  %.3f", render->time.scale);
          ImGui::SameLine();
          if (ImGui::SmallButton("Reset##time-scale")) {
            render->time.scale = 1.0f;
            changed = true;
          }
        }
        if (render->time.offsetSeconds != 0.0f) {
          ImGui::Text("Time offset  %.3f s", render->time.offsetSeconds);
          ImGui::SameLine();
          if (ImGui::SmallButton("Reset##time-offset")) {
            render->time.offsetSeconds = 0.0f;
            changed = true;
          }
        }
        for (const PropertyOverride& overrideValue : render->overrides) {
          ImGui::PushID(static_cast<int>(overrideValue.property));
          const AnimationPropertyInfo& info = animationPropertyInfo(overrideValue.property);
          ImGui::TextUnformatted(info.label.data());
          ImGui::SameLine();
          ImGui::TextDisabled("%s", overrideValueText(overrideValue).c_str());
          ImGui::SameLine();
          if (ImGui::SmallButton("Reset")) resetOverride = overrideValue.property;
          ImGui::PopID();
        }
        if (!view.animation.tracks.empty()) {
          ImGui::Separator();
          ImGui::TextDisabled("ANIMATED HERE");
          for (const PropertyAnimationTrack& track : view.animation.tracks)
            ImGui::BulletText("%s", animationPropertyInfo(track.property).label.data());
        }
      }
    }
    if (ImGui::CollapsingHeader("All Properties")) {
      static ImGuiTextFilter propertyGroupFilter;
      propertyGroupFilter.Draw("Filter groups");
      constexpr std::array categories = {Category::Geometry, Category::Camera, Category::Lighting,
        Category::Rasterization, Category::Surface, Category::Texture, Category::Depth,
        Category::Stencil, Category::Field, Category::Spectral, Category::Post,
        Category::Color, Category::Output};
      for (const Category category : categories) {
        if (!categoryAvailableForHardwareProfile(edited.hardwareProfile, category) ||
            !propertyGroupFilter.PassFilter(categoryName(category))) continue;
        ImGui::PushID(static_cast<int>(category));
        if (ImGui::CollapsingHeader(categoryName(category)))
          drawInspector(category, view, edited.hardwareProfile, timeline,
            edited.scene.importedModel.get(), edited.scene.testScene);
        ImGui::PopID();
      }
      if (propertyGroupFilter.PassFilter("Texture Mapping") &&
          ImGui::CollapsingHeader("Texture Mapping")) {
        drawTextureMappingEditorContents(view, timeline, edited.scene.importedModel.get(),
          edited.scene.testScene, texturePreview);
      }
    }
    if (renderViewChanged(before, view)) {
      commitRenderView(edited, operation, view, timeSeconds);
      changed = true;
    }
  } else if (editorState.selection.kind == editor::SelectionKind::Operation) {
    document::Operation* operation = document::findOperation(edited,
      editorState.selection.operation);
    if (operation != nullptr) changed |= drawOperation(edited, *operation, editorState, measurement);
  }
  if (resetOverride.has_value()) {
    static_cast<void>(history.execute(document, editor::SetRenderOverride{
      editorState.selection.operation, *resetOverride, std::nullopt}));
    ImGui::End();
    return;
  }
  if (editorState.selection.kind == editor::SelectionKind::Operation) {
    document::Operation* selected = document::findOperation(edited, editorState.selection.operation);
    if (selected != nullptr) {
      if (changed) document::synchronizeOperationSignalMetadata(*selected);
      ImGui::SeparatorText("OUTPUT SIGNALS");
      for (const document::SignalDescriptor& output : selected->outputs)
        drawSignalDescriptor(output, editorState);
    }
  }
  if (changed) {
    const editor::ReplaceDocument replacement{std::move(edited)};
    if (ImGui::IsAnyItemActive())
      static_cast<void>(history.executeContinuous(document, replacement));
    else
      static_cast<void>(history.execute(document, replacement));
  } else if (!ImGui::IsAnyItemActive()) {
    history.finishContinuous(document);
  }
  ImGui::End();
}

} // namespace gfxlab::ui
