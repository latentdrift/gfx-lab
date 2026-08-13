#include "app/RenderStack.hpp"

#include <algorithm>
#include <array>
#include <iomanip>
#include <sstream>
#include <utility>

namespace gfxlab {

RenderStack::RenderStack() {
  RenderPass a;
  a.name = "Pass A";
  RenderPass b = a;
  b.name = "Pass B";
  passes_ = {std::move(a), std::move(b)};
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
constexpr std::array<const char*, 12> labels = {
  "Absolute difference", "Signed A - B", "Positive A - B", "Positive B - A", "Multiply", "Screen",
  "Exclusion", "Minimum", "Maximum", "A x (1 - B)", "Centered sum", "Relative A / B"
};
constexpr std::array<const char*, 12> ids = {
  "absolute_difference", "signed_a_minus_b", "positive_a_minus_b", "positive_b_minus_a", "multiply", "screen",
  "exclusion", "minimum", "maximum", "accumulator_times_one_minus_pass", "centered_sum", "relative_accumulator_over_pass"
};
constexpr std::array<const char*, 12> equations = {
  "|A - B|", "A - B", "max(A - B, 0)", "max(B - A, 0)", "A x B", "1 - (1 - A)(1 - B)",
  "A + B - 2AB", "min(A, B)", "max(A, B)", "A(1 - B)", "A + B - 1", "A / max(B, 1/255) - 1"
};
constexpr std::array<const char*, 12> meanings = {
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
  "With 0.5 bias, middle gray means equality; a dark divisor clips aggressively."
};
std::size_t relationIndex(const RelationOperator operation) {
  return static_cast<std::size_t>(std::clamp(static_cast<int>(operation), 0, 11));
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
    const HardwareProfile profile) {
  constexpr const char* outputIds[] = {"color", "linear_depth_0_to_10_units", "normals", "vertex_colors"};
  constexpr const char* colorSpaceIds[] = {"encoded_rgb", "linear_light"};
  constexpr const char* rangeIds[] = {"clamp_0_to_1", "preserve_signed_hdr", "wrap_fractional_part"};
  constexpr const char* maskIds[] = {"none", "pass_luminance", "pass_depth_0_to_10_units", "pass_image_edges"};
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
  json << "  \"schema\": \"graphics-lab.render-stack.v1\",\n";
  json << "  \"evaluation\": \"bottom_to_top_sequential_compositing\",\n";
  json << "  \"seed_rule\": \"the first enabled pass becomes the accumulator; every later enabled pass applies its composite step\",\n";
  json << "  \"test_scene\": \"" << testSceneName(scene) << "\",\n";
  json << "  \"passes\": [\n";
  for (std::size_t index = 0; index < stack.passes().size(); ++index) {
    const RenderPass& pass = stack.passes()[index];
    const PassPerturbation& p = pass.perturbation;
    const CompositeStep& c = pass.composite;
    json << "    {\n";
    json << "      \"name\": \"" << escape(pass.name) << "\", \"enabled\": " << pass.enabled << ",\n";
    json << "      \"output_buffer\": \"" << outputIds[static_cast<int>(pass.output)] << "\",\n";
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
         << "\", \"gain\": " << c.gain << ", \"bias\": " << c.bias << ", \"opacity\": " << c.opacity
         << ", \"arithmetic_color_space\": \"" << colorSpaceIds[static_cast<int>(c.colorSpace)]
         << "\", \"range_behavior\": \"" << rangeIds[static_cast<int>(c.range)]
         << "\", \"mask\": \"" << maskIds[static_cast<int>(c.mask)]
         << "\", \"invert_mask\": " << c.invertMask << "},\n";
    json << "      \"renderer\": " << nested(configJson(pass.renderer, camera, scene, profile), 6) << "\n";
    json << "    }" << (index + 1 < stack.passes().size() ? "," : "") << "\n";
  }
  json << "  ]\n";
  json << "}\n";
  return json.str();
}

} // namespace gfxlab
