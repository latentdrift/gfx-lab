#include "ui/DocumentInspector.hpp"

#include "app/HardwareProfile.hpp"
#include "app/RenderOperationState.hpp"
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

bool signalPicker(const char* label, document::Document& document,
    const document::OperationId consumer, document::SignalRef& selected,
    const std::optional<document::SignalShape> requiredShape = std::nullopt,
    const std::optional<document::SignalSemantic> requiredSemantic = std::nullopt) {
  bool changed = false;
  const document::SignalDescriptor* current = descriptor(document, selected);
  if (ImGui::BeginCombo(label, current != nullptr ? current->name.c_str() : "Select signal")) {
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
  return changed;
}

bool drawOperation(document::Document& document, document::Operation& operation,
    const SignalMeasurement* measurement) {
  bool changed = false;
  ImGui::TextDisabled("%s", document::operationTypeLabel(operation));
  std::array<char, 256> name{};
  std::snprintf(name.data(), name.size(), "%s", operation.name.c_str());
  if (ImGui::InputText("Name", name.data(), name.size())) {
    operation.name = name.data();
    changed = true;
  }
  if (auto* composite = std::get_if<document::CompositeOperation>(&operation.data)) {
    ImGui::SeparatorText("INPUTS");
    changed |= signalPicker("Source A", document, operation.id, composite->a);
    changed |= signalPicker("Source B", document, operation.id, composite->b);
    ImGui::SeparatorText("BLEND");
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
    changed |= signalPicker("Mask input", document, operation.id, composite->mask,
      document::SignalShape::Scalar);
    if (composite->mask) {
      ImGui::SameLine();
      if (ImGui::Button("Clear")) { composite->mask = {}; changed = true; }
      changed |= ImGui::Checkbox("Invert mask", &composite->invertMask);
    }
  } else if (auto* interpret = std::get_if<document::InterpretOperation>(&operation.data)) {
    changed |= signalPicker("Spectrum", document, operation.id, interpret->spectrum,
      document::SignalShape::Spectrum16);
    changed |= ImGui::DragFloat("Exposure", &interpret->exposureStops, 0.05f, -12.0f, 12.0f);
    changed |= ImGui::DragFloat("Gain", &interpret->gain, 0.01f, 0.0f, 16.0f);
    changed |= ImGui::DragFloat("Bias", &interpret->bias, 0.01f, -1.0f, 1.0f);
  } else if (auto* stereo = std::get_if<document::StereoOperation>(&operation.data)) {
    changed |= signalPicker("Left", document, operation.id, stereo->left,
      document::SignalShape::Vector4, document::SignalSemantic::Color);
    changed |= signalPicker("Right", document, operation.id, stereo->right,
      document::SignalShape::Vector4, document::SignalSemantic::Color);
    int mode = static_cast<int>(stereo->mode);
    constexpr const char* modes[] = {"Anaglyph", "Signed disparity", "Absolute disparity",
      "Correspondence", "Monocular occlusion"};
    if (ImGui::Combo("Output", &mode, modes, 5)) { stereo->mode = static_cast<StereoAnalysisMode>(mode); changed = true; }
    changed |= ImGui::DragFloat("Maximum disparity", &stereo->maximumDisparityPixels, 0.25f, 1.0f, 512.0f);
    changed |= ImGui::DragFloat("Occlusion tolerance", &stereo->occlusionTolerance,
      0.00005f, 0.00001f, 0.05f);
  } else if (auto* measure = std::get_if<document::MeasureOperation>(&operation.data)) {
    changed |= signalPicker("Input", document, operation.id, measure->input);
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
    changed |= ImGui::ColorEdit4("Value", &constant->value.x, ImGuiColorEditFlags_Float);
  } else if (auto* luminance = std::get_if<document::LuminanceOperation>(&operation.data)) {
    changed |= signalPicker("Input", document, operation.id, luminance->input,
      std::nullopt, document::SignalSemantic::Color);
    ImGui::TextWrapped("Converts linear RGB to one brightness value per pixel.");
  } else if (auto* remap = std::get_if<document::RemapOperation>(&operation.data)) {
    changed |= signalPicker("Input", document, operation.id, remap->input);
    changed |= ImGui::DragFloat2("Input range", &remap->inputLow, 0.005f, -100.0f, 100.0f);
    changed |= ImGui::DragFloat2("Output range", &remap->outputLow, 0.005f, -16.0f, 16.0f);
    changed |= ImGui::Checkbox("Clamp before output range", &remap->clamp);
  } else if (auto* edge = std::get_if<document::EdgeOperation>(&operation.data)) {
    changed |= signalPicker("Input", document, operation.id, edge->input);
    changed |= ImGui::DragFloat("Strength", &edge->strength, 0.01f, 0.0f, 32.0f);
    ImGui::TextWrapped("Sobel magnitude from scalar change or multi-channel disagreement.");
  } else if (auto* blur = std::get_if<document::BlurOperation>(&operation.data)) {
    changed |= signalPicker("Input", document, operation.id, blur->input,
      blur->outputShape);
    changed |= ImGui::SliderFloat("Radius", &blur->radiusPixels, 0.0f, 4.0f, "%.0f px");
  } else if (auto* threshold = std::get_if<document::ThresholdOperation>(&operation.data)) {
    changed |= signalPicker("Input", document, operation.id, threshold->input,
      document::SignalShape::Scalar);
    changed |= ImGui::SliderFloat("Threshold", &threshold->threshold, 0.0f, 1.0f);
    changed |= ImGui::SliderFloat("Softness", &threshold->softness, 0.0f, 0.5f);
  } else if (auto* gradient = std::get_if<document::GradientMapOperation>(&operation.data)) {
    changed |= signalPicker("Input", document, operation.id, gradient->input,
      document::SignalShape::Scalar);
    changed |= ImGui::ColorEdit4("Low color", &gradient->lowColor.x, ImGuiColorEditFlags_Float);
    changed |= ImGui::ColorEdit4("High color", &gradient->highColor.x, ImGuiColorEditFlags_Float);
  } else if (auto* warp = std::get_if<document::WarpOperation>(&operation.data)) {
    changed |= signalPicker("Image", document, operation.id, warp->image,
      document::SignalShape::Vector4, document::SignalSemantic::Color);
    changed |= signalPicker("Displacement", document, operation.id, warp->displacement,
      document::SignalShape::Vector2);
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
    ImGui::TextDisabled("%s", operation.has_value() ? "RENDER" : "RENDER DEFAULTS");
    ImGui::TextUnformatted(view.name.c_str());
    if (ImGui::CollapsingHeader("Essentials", ImGuiTreeNodeFlags_DefaultOpen)) {
      if (operation.has_value()) {
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
        ImGui::TextDisabled("LOCAL PROCEDURAL TIME");
        timeControl("Time Scale", render->time.scale, document::timeScaleProperty(), 0.01f);
        timeControl("Time Offset", render->time.offsetSeconds, document::timeOffsetProperty(), 0.01f);
        ImGui::TextDisabled("local = timeline × scale + offset; negative reverses, zero freezes");
      }
      drawInspector(Category::Geometry, view, edited.hardwareProfile, timeline,
        edited.scene.importedModel.get(), edited.scene.testScene);
      drawTextureMappingEditorContents(view, timeline, edited.scene.importedModel.get(),
        edited.scene.testScene, texturePreview);
    }
    if (operation.has_value() && ImGui::CollapsingHeader("Changes", ImGuiTreeNodeFlags_DefaultOpen)) {
      const auto* render = std::get_if<document::RenderOperation>(
        &document::findOperation(edited, *operation)->data);
      if (render == nullptr || (render->overrides.empty() && render->time.scale == 1.0f &&
          render->time.offsetSeconds == 0.0f)) ImGui::TextDisabled("No changes from Render Defaults.");
      else if (render != nullptr) {
        if (render->time.scale != 1.0f) ImGui::Text("Time Scale: %.3f", render->time.scale);
        if (render->time.offsetSeconds != 0.0f) ImGui::Text("Time Offset: %.3f s", render->time.offsetSeconds);
        for (const PropertyOverride& overrideValue : render->overrides) {
          const AnimationPropertyInfo& info = animationPropertyInfo(overrideValue.property);
          ImGui::TextUnformatted(info.label.data());
        }
      }
    }
    if (ImGui::CollapsingHeader("All Properties")) {
      constexpr std::array categories = {Category::Camera, Category::Lighting, Category::Rasterization,
        Category::Surface, Category::Texture, Category::Depth, Category::Stencil, Category::Field,
        Category::Spectral, Category::Post, Category::Color, Category::Output};
      for (const Category category : categories) {
        if (!categoryAvailableForHardwareProfile(edited.hardwareProfile, category)) continue;
        ImGui::PushID(static_cast<int>(category));
        drawInspector(category, view, edited.hardwareProfile, timeline,
          edited.scene.importedModel.get(), edited.scene.testScene);
        ImGui::PopID();
      }
    }
    if (renderViewChanged(before, view)) {
      commitRenderView(edited, operation, view, timeSeconds);
      changed = true;
    }
  } else if (editorState.selection.kind == editor::SelectionKind::Operation) {
    document::Operation* operation = document::findOperation(edited,
      editorState.selection.operation);
    if (operation != nullptr) changed = drawOperation(edited, *operation, measurement);
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
