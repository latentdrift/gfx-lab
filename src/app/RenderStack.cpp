#include "app/RenderStack.hpp"
#include "assets/ModelAsset.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <utility>

namespace gfxlab {

RenderStack::RenderStack() {
  global_.name = "Global base";
  RenderPass a;
  a.name = "Pass A";
  RenderPass b = a;
  b.name = "Pass B";
  passes_ = {std::move(a), std::move(b)};
}

bool animationPropertyIsPassLocal(const AnimationProperty property) {
  switch (property) {
  case AnimationProperty::PassEnabled:
  case AnimationProperty::PassOutput:
  case AnimationProperty::CompositeGain:
  case AnimationProperty::CompositeBias:
  case AnimationProperty::CompositeOpacity:
  case AnimationProperty::CompositeOperation:
  case AnimationProperty::CompositeSourceA:
  case AnimationProperty::CompositeSourceB:
  case AnimationProperty::CompositeSourceAPass:
  case AnimationProperty::CompositeSourceBPass:
  case AnimationProperty::CompositeFixedColor:
  case AnimationProperty::CompositeColorSpace:
  case AnimationProperty::CompositeRange:
  case AnimationProperty::CompositeMask:
  case AnimationProperty::CompositeMaskInverted: return true;
  default: return false;
  }
}

const PropertyOverride* findRenderPassOverride(const RenderPass& pass, const AnimationProperty property) {
  const auto found = std::find_if(pass.overrides.begin(), pass.overrides.end(),
    [property](const PropertyOverride& candidate) { return candidate.property == property; });
  return found == pass.overrides.end() ? nullptr : &*found;
}

void setRenderPassOverride(RenderPass& pass, const AnimationProperty property, const glm::vec4& value) {
  if (animationPropertyIsPassLocal(property) || property == AnimationProperty::Count) return;
  const auto found = std::find_if(pass.overrides.begin(), pass.overrides.end(),
    [property](const PropertyOverride& candidate) { return candidate.property == property; });
  if (found == pass.overrides.end()) pass.overrides.push_back({property, value});
  else found->value = value;
}

bool clearRenderPassOverride(RenderPass& pass, const AnimationProperty property) {
  const auto before = pass.overrides.size();
  pass.overrides.erase(std::remove_if(pass.overrides.begin(), pass.overrides.end(),
    [property](const PropertyOverride& candidate) { return candidate.property == property; }), pass.overrides.end());
  return pass.overrides.size() != before;
}

void replaceRenderPassOverrides(RenderPass& pass, const RenderPass& global, const RenderPass& materialized) {
  pass.overrides.clear();
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    if (animationPropertyIsPassLocal(property)) continue;
    const glm::vec4 globalValue = animationPropertyValue(global, property);
    const glm::vec4 materializedValue = animationPropertyValue(materialized, property);
    if (!animationPropertyValuesEqual(property, globalValue, materializedValue))
      pass.overrides.push_back({property, materializedValue});
  }
  pass.importedTextureOverride = materialized.importedTexture != global.importedTexture;
  if (pass.importedTextureOverride) pass.importedTexture = materialized.importedTexture;
  else pass.importedTexture.reset();
}

namespace {
void applyPassDefinition(RenderPass& materialized, const RenderPass& definition) {
  materialized.name = definition.name;
  materialized.enabled = definition.enabled;
  materialized.output = definition.output;
  materialized.composite = definition.composite;
  materialized.animation = definition.animation;
  materialized.overrides = definition.overrides;
  materialized.importedTextureOverride = definition.importedTextureOverride;
  for (const PropertyOverride& overrideValue : definition.overrides)
    setAnimationPropertyValue(materialized, overrideValue.property, overrideValue.value);
  if (definition.importedTextureOverride) materialized.importedTexture = definition.importedTexture;
}
} // namespace

RenderPass resolveRenderPass(const RenderStack& stack, const std::size_t passIndex) {
  const RenderPass& definition = stack.passes()[passIndex];
  RenderPass materialized = stack.global();
  applyPassDefinition(materialized, definition);
  return materialized;
}

RenderPass materializeRenderPass(const RenderStack& stack, const std::size_t passIndex,
    const float timeSeconds) {
  const RenderPass& definition = stack.passes()[passIndex];
  RenderPass materialized = evaluateRenderPass(stack.global(), timeSeconds);
  applyPassDefinition(materialized, definition);
  for (const PropertyAnimationTrack& track : definition.animation.tracks)
    if (!track.keyframes.empty())
      setAnimationPropertyValue(materialized, track.property, samplePropertyTrack(track, timeSeconds));
  return materialized;
}

RenderPass& RenderStack::selected() { return passes_[selected_]; }
const RenderPass& RenderStack::selected() const { return passes_[selected_]; }

void RenderStack::select(const std::size_t index) {
  if (!passes_.empty()) selected_ = std::min(index, passes_.size() - 1);
}

bool RenderStack::duplicateSelected() {
  if (passes_.size() >= maximumPasses) return false;
  RenderPass duplicate = selected();
  duplicate.name = "Pass " + std::to_string(nextPassNumber_++);
  passes_.insert(passes_.begin() + static_cast<std::ptrdiff_t>(selected_ + 1), std::move(duplicate));
  ++selected_;
  return true;
}

bool RenderStack::removeSelected() {
  if (passes_.size() <= 1) return false;
  passes_.erase(passes_.begin() + static_cast<std::ptrdiff_t>(selected_));
  if (selected_ >= passes_.size()) selected_ = passes_.size() - 1;
  return true;
}

bool RenderStack::moveSelected(const int direction) {
  if (direction == 0 || passes_.size() < 2) return false;
  const std::ptrdiff_t destination = static_cast<std::ptrdiff_t>(selected_) + (direction < 0 ? -1 : 1);
  if (destination < 0 || destination >= static_cast<std::ptrdiff_t>(passes_.size())) return false;
  std::swap(passes_[selected_], passes_[static_cast<std::size_t>(destination)]);
  selected_ = static_cast<std::size_t>(destination);
  return true;
}

namespace {
constexpr std::array<const char*, 18> labels = {
  "Absolute difference", "Signed A - B", "Positive A - B", "Positive B - A", "Multiply", "Screen",
  "Exclusion", "Minimum", "Maximum", "A x (1 - B)", "Centered sum", "Relative A / B",
  "Add", "Average (add + half)", "Subtract", "Reverse subtract", "Quarter-add B", "Signed color offset"
};
constexpr std::array<const char*, 18> ids = {
  "absolute_difference", "signed_a_minus_b", "positive_a_minus_b", "positive_b_minus_a", "multiply", "screen",
  "exclusion", "minimum", "maximum", "a_times_one_minus_b", "centered_sum", "relative_a_over_b",
  "add", "average", "subtract", "reverse_subtract", "quarter_add_b", "signed_color_offset"
};
constexpr std::array<const char*, 18> equations = {
  "|A - B|", "A - B", "max(A - B, 0)", "max(B - A, 0)", "A x B", "1 - (1 - A)(1 - B)",
  "A + B - 2AB", "min(A, B)", "max(A, B)", "A(1 - B)", "A + B - 1", "A / max(B, 1/255) - 1",
  "A + B", "(A + B) / 2", "A - B", "B - A", "A + B / 4", "A + B - 1/2"
};
constexpr std::array<const char*, 18> meanings = {
  "Black means agreement; RGB stores disagreement magnitude.",
  "Middle gray means agreement; direction says which input has more channel energy.",
  "Keeps only channel energy present in the accumulated image beyond this pass.",
  "Keeps only channel energy present in this pass beyond the accumulated image.",
  "Keeps color supported by both inputs and darkens disagreement.",
  "Combines bright contributions from either input.",
  "Dark at matching extrema and bright where the inputs oppose each other.",
  "Keeps the lower value from each channel.", "Keeps the higher value from each channel.",
  "Keeps the accumulated image where this pass is absent.",
  "With 0.5 bias, middle gray means the channel values sum to one.",
  "With 0.5 bias, middle gray means equality; a dark divisor clips aggressively.",
  "Adds channel energy; clamping drives overlaps toward white.",
  "Averages both inputs, matching half-add color-math hardware.",
  "Subtracts B from A before the selected range behavior.",
  "Subtracts A from B before the selected range behavior.",
  "Adds one quarter of B to A, matching the PS1 quarter-add equation.",
  "Treats B around middle gray as a signed offset: dark subtracts and bright adds."
};
std::size_t relationIndex(const RelationOperator operation) {
  return static_cast<std::size_t>(std::clamp(static_cast<int>(operation), 0,
    static_cast<int>(labels.size()) - 1));
}
} // namespace

const char* relationOperatorLabel(const RelationOperator operation) { return labels[relationIndex(operation)]; }
const char* relationOperatorId(const RelationOperator operation) { return ids[relationIndex(operation)]; }
const char* relationOperatorEquation(const RelationOperator operation) { return equations[relationIndex(operation)]; }
const char* relationOperatorMeaning(const RelationOperator operation) { return meanings[relationIndex(operation)]; }

void resetCompositeTransform(CompositeStep& step) {
  switch (step.operation) {
  case RelationOperator::AbsoluteDifference: step.gain = 4.0f; step.bias = 0.0f; break;
  case RelationOperator::SignedDifference: step.gain = 2.0f; step.bias = 0.5f; break;
  case RelationOperator::PositiveAMinusB:
  case RelationOperator::PositiveBMinusA: step.gain = 4.0f; step.bias = 0.0f; break;
  case RelationOperator::CenteredSum: step.gain = 1.0f; step.bias = 0.5f; break;
  case RelationOperator::RelativeDifference: step.gain = 0.5f; step.bias = 0.5f; break;
  default: step.gain = 1.0f; step.bias = 0.0f; break;
  }
}

std::string renderStackConfigJson(const RenderStack& stack, const CameraOrbit& camera, const TestScene scene,
    const HardwareProfile profile, const AnimationTimeline* timeline, const ModelAsset* importedModel) {
  constexpr const char* outputIds[] = {"color", "linear_depth_0_to_10_units", "normals", "vertex_colors"};
  constexpr const char* colorSpaceIds[] = {"encoded_rgb", "linear_light"};
  constexpr const char* rangeIds[] = {"clamp_0_to_1", "preserve_signed_hdr", "wrap_fractional_part"};
  constexpr const char* maskIds[] = {"none", "pass_luminance", "pass_depth_0_to_10_units", "pass_image_edges"};
  constexpr const char* sourceIds[] = {"accumulated_result", "current_pass", "render_pass", "fixed_color"};
  constexpr const char* interpolationIds[] = {"step", "linear", "smooth_step"};
  constexpr const char* valueKindIds[] = {"float", "vec2", "vec3", "color3", "color4", "angle",
    "boolean", "integer", "enumeration"};
  constexpr const char* behaviorIds[] = {"continuous", "step", "not_animatable"};
  constexpr const char* textureSourceIds[] = {"scene_material", "built_in_checker", "imported_override", "white"};
  const auto escape = [](const std::string& value) {
    std::string result;
    for (const char character : value) {
      if (character == '"' || character == '\\') result.push_back('\\');
      result.push_back(character);
    }
    return result;
  };
  const auto nested = [](std::string value, const int spaces) {
    if (!value.empty() && value.back() == '\n') value.pop_back();
    std::string result;
    const std::string indentation(static_cast<std::size_t>(spaces), ' ');
    for (const char character : value) {
      result.push_back(character);
      if (character == '\n') result += indentation;
    }
    return result;
  };

  std::ostringstream json;
  json << std::boolalpha << std::fixed << std::setprecision(5);
  json << "{\n";
  json << "  \"schema\": \"graphics-lab.render-stack.v3\",\n";
  json << "  \"evaluation\": \"bottom_to_top_sequential_compositing\",\n";
  json << "  \"property_precedence\": \"global base, global track, local override, local track\",\n";
  json << "  \"seed_rule\": \"the first enabled pass becomes the accumulator; every later enabled pass applies its composite step\",\n";
  json << "  \"test_scene\": \"" << testSceneName(scene) << "\",\n";
  if (importedModel != nullptr) {
    json << "  \"imported_model\": {\"name\": \"" << escape(importedModel->name)
         << "\", \"source_path\": \"" << escape(importedModel->sourcePath)
         << "\", \"content_hash_fnv1a64\": \"" << std::hex << importedModel->contentHash << std::dec
         << "\", \"source_meshes\": " << importedModel->sourceMeshCount
         << ", \"triangles\": " << importedModel->triangleCount
         << ", \"materials\": " << importedModel->materials.size()
         << ", \"base_color_textures\": " << importedModel->textures.size()
         << ", \"normalization_scale\": " << importedModel->normalizationScale
         << ", \"has_uv0\": " << importedModel->hasTextureCoordinates
         << ", \"has_vertex_color0\": " << importedModel->hasVertexColors
         << ", \"has_tangents\": " << importedModel->hasTangents << "},\n";
  }
  if (timeline != nullptr) {
    json << "  \"animation_timeline\": {\"duration_seconds\": " << timeline->durationSeconds
         << ", \"playback_rate\": " << timeline->playbackRate << ", \"loop\": " << timeline->loop
         << ", \"auto_key\": " << timeline->autoKey << ", \"show_all_passes\": "
         << timeline->showAllPasses << ", \"current_time_seconds\": " << timeline->timeSeconds << "},\n";
  }
  json << "  \"global_base\": {\n";
  json << "    \"properties\": {";
  bool firstGlobalProperty = true;
  for (int propertyIndex = 0; propertyIndex < static_cast<int>(AnimationProperty::Count); ++propertyIndex) {
    const AnimationProperty property = static_cast<AnimationProperty>(propertyIndex);
    if (animationPropertyIsPassLocal(property)) continue;
    const AnimationPropertyInfo& info = animationPropertyInfo(property);
    const glm::vec4 value = animationPropertyValue(stack.global(), property);
    if (!firstGlobalProperty) json << ",";
    json << "\n      \"" << info.id << "\": [";
    for (int component = 0; component < info.components; ++component) {
      if (component != 0) json << ", ";
      json << value[component];
    }
    json << "]";
    firstGlobalProperty = false;
  }
  if (!firstGlobalProperty) json << "\n    ";
  json << "},\n";
  json << "    \"property_tracks\": [";
  for (std::size_t trackIndex = 0; trackIndex < stack.global().animation.tracks.size(); ++trackIndex) {
    const PropertyAnimationTrack& track = stack.global().animation.tracks[trackIndex];
    const AnimationPropertyInfo& info = animationPropertyInfo(track.property);
    if (trackIndex != 0) json << ",";
    json << "\n      {\"property\": \"" << info.id << "\", \"interpolation\": \""
         << interpolationIds[static_cast<int>(track.interpolation)] << "\", \"keys\": [";
    for (std::size_t keyIndex = 0; keyIndex < track.keyframes.size(); ++keyIndex) {
      const PropertyKeyframe& key = track.keyframes[keyIndex];
      if (keyIndex != 0) json << ", ";
      json << "{\"time_seconds\": " << key.timeSeconds << ", \"value\": [";
      for (int component = 0; component < info.components; ++component) {
        if (component != 0) json << ", ";
        json << key.value[component];
      }
      json << "]}";
    }
    json << "]}";
  }
  if (!stack.global().animation.tracks.empty()) json << "\n    ";
  json << "],\n";
  json << "    \"renderer\": " << nested(configJson(stack.global().renderer, camera, scene, profile), 4) << "\n";
  json << "  },\n";
  json << "  \"passes\": [\n";
  for (std::size_t index = 0; index < stack.passes().size(); ++index) {
    const RenderPass& pass = stack.passes()[index];
    const RenderPass effective = materializeRenderPass(stack, index, 0.0f);
    const PassPerturbation& p = effective.perturbation;
    const CompositeStep& c = pass.composite;
    json << "    {\n";
    json << "      \"name\": \"" << escape(pass.name) << "\", \"enabled\": " << pass.enabled << ",\n";
    json << "      \"output_buffer\": \"" << outputIds[static_cast<int>(pass.output)] << "\",\n";
    json << "      \"texture_source\": \"" << textureSourceIds[static_cast<int>(effective.textureSource)] << "\"";
    if (effective.importedTexture != nullptr) {
      json << ", \"imported_texture\": {\"name\": \"" << escape(effective.importedTexture->name)
           << "\", \"source_path\": \"" << escape(effective.importedTexture->sourcePath)
           << "\", \"content_hash_fnv1a64\": \"" << std::hex << effective.importedTexture->contentHash << std::dec
           << "\", \"dimensions\": [" << effective.importedTexture->width << ", " << effective.importedTexture->height
           << "], \"alpha_present\": " << effective.importedTexture->hasAlpha
           << ", \"color_interpretation\": \"" << (effective.importedTextureSrgb ? "srgb" : "linear_data") << "\"}";
    }
    json << ",\n";
    json << "      \"overrides\": [";
    for (std::size_t overrideIndex = 0; overrideIndex < pass.overrides.size(); ++overrideIndex) {
      const PropertyOverride& overrideValue = pass.overrides[overrideIndex];
      const AnimationPropertyInfo& info = animationPropertyInfo(overrideValue.property);
      if (overrideIndex != 0) json << ", ";
      json << "{\"property\": \"" << info.id << "\", \"value\": [";
      for (int component = 0; component < info.components; ++component) {
        if (component != 0) json << ", ";
        json << overrideValue.value[component];
      }
      json << "]}";
    }
    json << "],\n";
    json << "      \"perturbation\": {\n";
    json << "        \"model_translation_units\": [" << p.modelTranslation.x << ", " << p.modelTranslation.y
         << ", " << p.modelTranslation.z << "], \"model_scale\": " << p.modelScale
         << ", \"normal_inflation_units\": " << p.normalInflation << ",\n";
    json << "        \"uv_offset\": [" << p.uvOffset.x << ", " << p.uvOffset.y << "], \"uv_scale\": ["
         << p.uvScale.x << ", " << p.uvScale.y << "],\n";
    json << "        \"camera_yaw_offset_radians\": " << p.cameraYaw
         << ", \"camera_pitch_offset_radians\": " << p.cameraPitch
         << ", \"camera_distance_offset_units\": " << p.cameraDistance
         << ", \"field_of_view_offset_degrees\": " << p.fieldOfView << "\n";
    json << "      },\n";
    json << "      \"composite_into_previous\": {\"operation\": \"" << relationOperatorId(c.operation)
         << "\", \"equation_per_rgb_channel\": \"" << relationOperatorEquation(c.operation)
         << "\", \"source_a\": {\"type\": \"" << sourceIds[static_cast<int>(c.sourceA)]
         << "\", \"pass_index\": " << c.sourceAPass << "}, \"source_b\": {\"type\": \""
         << sourceIds[static_cast<int>(c.sourceB)] << "\", \"pass_index\": " << c.sourceBPass
         << "}, \"fixed_color_rgba\": [" << c.fixedColor.r << ", " << c.fixedColor.g << ", "
         << c.fixedColor.b << ", " << c.fixedColor.a << "]"
         << ", \"gain\": " << c.gain << ", \"bias\": " << c.bias << ", \"opacity\": " << c.opacity
         << ", \"arithmetic_color_space\": \"" << colorSpaceIds[static_cast<int>(c.colorSpace)]
         << "\", \"range_behavior\": \"" << rangeIds[static_cast<int>(c.range)]
         << "\", \"mask\": \"" << maskIds[static_cast<int>(c.mask)]
         << "\", \"invert_mask\": " << c.invertMask << "},\n";
    json << "      \"animation\": {\"enabled\": " << pass.animation.enabled << ", \"property_tracks\": [";
    for (std::size_t trackIndex = 0; trackIndex < pass.animation.tracks.size(); ++trackIndex) {
      const PropertyAnimationTrack& track = pass.animation.tracks[trackIndex];
      const AnimationPropertyInfo& info = animationPropertyInfo(track.property);
      if (trackIndex != 0) json << ",";
      json << "\n        {\"property\": \"" << info.id << "\", \"components\": " << info.components
           << ", \"value_kind\": \"" << valueKindIds[static_cast<int>(info.kind)]
           << "\", \"animation_behavior\": \"" << behaviorIds[static_cast<int>(info.behavior)]
           << "\", \"interpolation\": \"" << interpolationIds[static_cast<int>(track.interpolation)]
           << "\", \"keys\": [";
      for (std::size_t keyIndex = 0; keyIndex < track.keyframes.size(); ++keyIndex) {
        const PropertyKeyframe& key = track.keyframes[keyIndex];
        if (keyIndex != 0) json << ", ";
        json << "{\"time_seconds\": " << key.timeSeconds << ", \"value\": [";
        for (int component = 0; component < info.components; ++component) {
          if (component != 0) json << ", ";
          json << key.value[component];
        }
        json << "]}";
      }
      json << "]}";
    }
    if (!pass.animation.tracks.empty()) json << "\n      ";
    json << "]},\n";
    json << "      \"effective_renderer_at_time_zero\": " << nested(configJson(effective.renderer, camera, scene, profile), 6) << "\n";
    json << "    }" << (index + 1 < stack.passes().size() ? "," : "") << "\n";
  }
  json << "  ]\n";
  json << "}\n";
  return json.str();
}

} // namespace gfxlab
