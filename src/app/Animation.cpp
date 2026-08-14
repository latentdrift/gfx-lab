#include "app/Animation.hpp"

#include "app/RenderOperationState.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace gfxlab {

void recordPropertyAnimationEdit(RenderPass& pass, const AnimationProperty property,
    AnimationTimeline& timeline, const bool valueChanged) {
  PropertyAnimationTrack* track = findPropertyTrack(pass, property);
  if (!valueChanged || (track == nullptr && !timeline.autoKey)) return;
  timeline.playing = false;
  const glm::vec4 value = animationPropertyValue(pass, property);
  setPropertyKeyframe(pass, property, timeline.timeSeconds, &value);
}

namespace {

using K = AnimationValueKind;
using B = AnimationBehavior;
#define CONT(ID, LABEL, GROUP, COMPONENTS, KIND, MINIMUM, MAXIMUM) \
  {ID, LABEL, GROUP, COMPONENTS, KIND, B::Continuous, MINIMUM, MAXIMUM}
#define STEP(ID, LABEL, GROUP, KIND, MINIMUM, MAXIMUM) \
  {ID, LABEL, GROUP, 1, KIND, B::Step, MINIMUM, MAXIMUM}
#define FIXED(ID, LABEL, GROUP, COMPONENTS, KIND, MINIMUM, MAXIMUM) \
  {ID, LABEL, GROUP, COMPONENTS, KIND, B::NotAnimatable, MINIMUM, MAXIMUM}
constexpr std::array<AnimationPropertyInfo, static_cast<std::size_t>(AnimationProperty::Count)> propertyInfo = {{
  STEP("pass_enabled", "Pass enabled", "Pass", K::Boolean, 0, 1),
  STEP("pass_output", "Output buffer", "Pass", K::Enumeration, 0, 4),
  STEP("texture_source", "Texture source", "Texture", K::Enumeration, 0, 3),
  STEP("texture_color_interpretation", "Texture color interpretation", "Texture", K::Boolean, 0, 1),
  CONT("model_translation", "Model translation", "Geometry perturbation", 3, K::Vec3, -4, 4),
  CONT("model_scale", "Model scale", "Geometry perturbation", 1, K::Float, 0.01f, 4),
  CONT("normal_inflation", "Normal inflation", "Geometry perturbation", 1, K::Float, -1, 1),
  CONT("uv_offset", "UV offset", "Sampling perturbation", 2, K::Vec2, -4, 4),
  CONT("uv_scale", "UV scale", "Sampling perturbation", 2, K::Vec2, 0.01f, 8),
  CONT("uv_rotation", "UV rotation", "Sampling perturbation", 1, K::Angle, -6.283f, 6.283f),
  CONT("uv_pivot", "UV transform pivot", "Sampling perturbation", 2, K::Vec2, -2, 2),
  STEP("uv_mapping", "Texture-coordinate source", "Sampling perturbation", K::Enumeration, 0, 3),
  CONT("camera_yaw", "Camera yaw offset", "View perturbation", 1, K::Angle, -6.283f, 6.283f),
  CONT("camera_pitch", "Camera pitch offset", "View perturbation", 1, K::Angle, -1.45f, 1.45f),
  CONT("camera_distance", "Camera distance offset", "View perturbation", 1, K::Float, -10, 10),
  CONT("camera_lateral", "Camera lateral (eye) offset", "View perturbation", 1, K::Float, -2, 2),
  CONT("stereo_convergence", "Stereo convergence distance", "View perturbation", 1, K::Float, 0.05f, 100),
  CONT("field_of_view_offset", "Field-of-view offset", "View perturbation", 1, K::Float, -100, 100),
  CONT("composite_gain", "Composite gain", "Composite", 1, K::Float, 0, 16),
  CONT("composite_bias", "Composite bias", "Composite", 1, K::Float, -1, 1),
  CONT("composite_opacity", "Composite opacity", "Composite", 1, K::Float, 0, 1),
  STEP("composite_operation", "Composite operation", "Composite", K::Enumeration, 0, 18),
  STEP("composite_source_a", "Composite source A", "Composite operands", K::Enumeration, 0, 6),
  STEP("composite_source_b", "Composite source B", "Composite operands", K::Enumeration, 0, 6),
  STEP("composite_source_a_pass", "Source A render-pass ID", "Composite operands", K::Integer, 1, 65535),
  STEP("composite_source_b_pass", "Source B render-pass ID", "Composite operands", K::Integer, 1, 65535),
  STEP("composite_interpretation_a", "Source A interpretation", "Composite operands", K::Enumeration, 0, 10),
  STEP("composite_interpretation_b", "Source B interpretation", "Composite operands", K::Enumeration, 0, 10),
  CONT("composite_fixed_color", "Composite fixed color", "Composite operands", 4, K::Color4, 0, 1),
  STEP("composite_bit_depth", "Composite bit depth", "Composite arithmetic", K::Integer, 1, 8),
  CONT("composite_history_decay", "Previous-frame decay", "Composite history", 1, K::Float, 0, 1),
  CONT("composite_history_uv_offset", "Previous-frame UV offset", "Composite history", 2, K::Vec2, -1, 1),
  CONT("composite_history_uv_scale", "Previous-frame UV scale", "Composite history", 2, K::Vec2, 0.25f, 4),
  STEP("composite_color_space", "Composite color space", "Composite", K::Enumeration, 0, 1),
  STEP("composite_range", "Composite range", "Composite", K::Enumeration, 0, 2),
  STEP("composite_mask", "Composite mask", "Composite", K::Enumeration, 0, 4),
  STEP("composite_mask_inverted", "Composite mask inverted", "Composite", K::Boolean, 0, 1),
  STEP("stereo_analysis_mode", "Stereo analysis output", "Stereo analysis", K::Enumeration, 0, 4),
  CONT("stereo_maximum_disparity", "Maximum displayed disparity", "Stereo analysis", 1, K::Float, 1, 512),
  CONT("stereo_occlusion_tolerance", "Depth agreement tolerance", "Stereo analysis", 1, K::Float, 0.00001f, 0.05f),
  CONT("vertex_quantization", "Vertex position precision", "Geometry", 1, K::Float, 0, 0.125f),
  STEP("clipping_enabled", "Clipping plane enabled", "Geometry", K::Boolean, 0, 1),
  CONT("clipping_height", "Clipping plane height", "Geometry", 1, K::Float, -10, 10),
  STEP("clipping_keep_above", "Clipping plane keep above", "Geometry", K::Boolean, 0, 1),
  STEP("projection_orthographic", "Orthographic projection", "Camera", K::Boolean, 0, 1),
  CONT("field_of_view", "Field of view", "Camera", 1, K::Float, 5, 150),
  CONT("orthographic_size", "Orthographic view height", "Camera", 1, K::Float, 0.1f, 20),
  CONT("near_plane", "Near clipping plane", "Camera", 1, K::Float, 0.001f, 10),
  STEP("affine_mapping", "Affine texture mapping", "Rasterization", K::Boolean, 0, 1),
  STEP("cull_mode", "Face culling", "Rasterization", K::Enumeration, 0, 2),
  FIXED("multisample_count", "Multisample count", "Rasterization", 1, K::Integer, 1, 8),
  STEP("polygon_offset_enabled", "Polygon offset fill", "Rasterization", K::Boolean, 0, 1),
  CONT("polygon_offset_factor", "Polygon offset slope factor", "Rasterization", 1, K::Float, -8, 8),
  CONT("polygon_offset_units", "Polygon offset constant units", "Rasterization", 1, K::Float, -16, 16),
  STEP("smooth_shading", "Smooth normal interpolation", "Surface", K::Boolean, 0, 1),
  STEP("wireframe_overlay", "Wireframe overlay", "Surface", K::Boolean, 0, 1),
  STEP("surface_visualization", "Surface visualization", "Surface", K::Enumeration, 0, 5),
  STEP("normal_mapping_enabled", "Normal mapping enabled", "Surface", K::Boolean, 0, 1),
  CONT("normal_strength", "Normal-map strength", "Surface", 1, K::Float, 0, 4),
  STEP("transparency_operation", "Transparency operation", "Surface", K::Enumeration, 0, 9),
  CONT("alpha_cutoff", "Alpha cutoff", "Surface", 1, K::Float, 0, 1),
  STEP("reverse_draw_order", "Reverse object draw order", "Surface", K::Boolean, 0, 1),
  STEP("nearest_filtering", "Nearest texture filtering", "Texture", K::Boolean, 0, 1),
  STEP("texture_repeat", "Texture repeat addressing", "Texture", K::Boolean, 0, 1),
  STEP("mipmapping", "Mipmapping enabled", "Texture", K::Boolean, 0, 1),
  STEP("trilinear_filtering", "Trilinear mip interpolation", "Texture", K::Boolean, 0, 1),
  CONT("anisotropy", "Anisotropy", "Texture", 1, K::Float, 1, 16),
  STEP("texture_color_storage", "Texture color storage", "Texture", K::Enumeration, 0, 2),
  STEP("lighting_model", "Lighting model", "Lighting", K::Enumeration, 0, 4),
  CONT("ambient", "Ambient term", "Lighting", 1, K::Float, 0, 1),
  CONT("light_azimuth", "Light azimuth", "Lighting", 1, K::Angle, -180, 180),
  CONT("light_elevation", "Light elevation", "Lighting", 1, K::Angle, -89, 89),
  CONT("shininess", "Specular exponent", "Lighting", 1, K::Float, 1, 256),
  STEP("shadows_enabled", "Shadow map enabled", "Lighting", K::Boolean, 0, 1),
  FIXED("shadow_resolution", "Shadow-map resolution", "Lighting", 1, K::Integer, 128, 4096),
  CONT("shadow_bias", "Shadow depth bias", "Lighting", 1, K::Float, 0, 0.02f),
  STEP("shadow_pcf", "Shadow PCF filtering", "Lighting", K::Boolean, 0, 1),
  STEP("shadow_map_visualization", "Shadow-map visualization", "Lighting", K::Boolean, 0, 1),
  STEP("depth_cue_enabled", "Vertex depth cue enabled", "Lighting", K::Boolean, 0, 1),
  CONT("depth_cue_start", "Depth cue start", "Lighting", 1, K::Float, 0, 100),
  CONT("depth_cue_end", "Depth cue end", "Lighting", 1, K::Float, 0, 100),
  CONT("far_color", "Depth cue far color", "Lighting", 3, K::Color3, 0, 1),
  STEP("field_enabled", "Field enabled", "Field", K::Boolean, 0, 1),
  STEP("field_producer_kind", "Field producer kind", "Field", K::Enumeration, 0, 2),
  CONT("field_source_a", "Field source A", "Field sources", 3, K::Vec3, -8, 8),
  CONT("field_source_b", "Field source B", "Field sources", 3, K::Vec3, -8, 8),
  CONT("field_wavelength", "Field wavelength", "Field waves", 1, K::Float, 0.05f, 8),
  CONT("field_phase_offset", "Relative phase", "Field waves", 1, K::Angle, -6.283f, 6.283f),
  CONT("field_amplitude_a", "Source A amplitude", "Field sources", 1, K::Float, 0, 4),
  CONT("field_amplitude_b", "Source B amplitude", "Field sources", 1, K::Float, 0, 4),
  CONT("field_falloff", "Field distance falloff", "Field waves", 1, K::Float, 0, 2),
  CONT("field_band_sharpness", "Field band sharpness", "Field display", 1, K::Float, 0.1f, 8),
  STEP("field_visualization", "Field visualization", "Field display", K::Enumeration, 0, 6),
  CONT("field_low_color", "Field low color", "Field display", 3, K::Color3, 0, 1),
  CONT("field_high_color", "Field high color", "Field display", 3, K::Color3, 0, 1),
  CONT("field_vertex_displacement", "Field vertex displacement", "Field consumers", 1, K::Float, -2, 2),
  STEP("field_signed_displacement", "Signed field displacement", "Field consumers", K::Boolean, 0, 1),
  STEP("field_discard_enabled", "Field discard enabled", "Field consumers", K::Boolean, 0, 1),
  CONT("field_discard_threshold", "Field discard threshold", "Field consumers", 1, K::Float, 0, 1),
  CONT("field_surface_color_influence", "Field surface color influence", "Field consumers", 1, K::Float, 0, 1),
  CONT("field_emission_influence", "Field emission influence", "Field consumers", 1, K::Float, 0, 8),
  STEP("sdf_a_type", "SDF producer A type", "SDF producers", K::Enumeration, 0, 4),
  CONT("sdf_a_position", "SDF producer A position", "SDF producers", 3, K::Vec3, -8, 8),
  CONT("sdf_a_parameters", "SDF producer A parameters", "SDF producers", 3, K::Vec3, 0.01f, 8),
  STEP("sdf_b_type", "SDF producer B type", "SDF producers", K::Enumeration, 0, 4),
  CONT("sdf_b_position", "SDF producer B position", "SDF producers", 3, K::Vec3, -8, 8),
  CONT("sdf_b_parameters", "SDF producer B parameters", "SDF producers", 3, K::Vec3, 0.01f, 8),
  STEP("sdf_operation", "SDF combination", "SDF combination", K::Enumeration, 0, 3),
  CONT("sdf_smoothness", "SDF smooth-union radius", "SDF combination", 1, K::Float, 0.001f, 4),
  CONT("sdf_preview_range", "SDF preview range", "SDF display", 1, K::Float, 0.01f, 10),
  STEP("iso_surface_enabled", "Iso-surface enabled", "Iso-surface", K::Boolean, 0, 1),
  CONT("iso_level", "Iso-surface level", "Iso-surface", 1, K::Float, -4, 4),
  CONT("iso_color", "Iso-surface color", "Iso-surface", 3, K::Color3, 0, 1),
  STEP("iso_maximum_steps", "Iso-surface maximum steps", "Iso-surface", K::Integer, 8, 512),
  CONT("iso_hit_epsilon", "Iso-surface hit epsilon", "Iso-surface", 1, K::Float, 0.0001f, 0.1f),
  CONT("iso_maximum_distance", "Iso-surface maximum distance", "Iso-surface", 1, K::Float, 1, 100),
  STEP("spectral_illuminant", "Spectral illuminant", "Spectral", K::Enumeration, 0, 2),
  STEP("spectral_observer", "Spectral observer", "Spectral", K::Enumeration, 0, 2),
  CONT("spectral_exposure", "Spectral exposure", "Spectral", 1, K::Float, -8, 8),
  STEP("depth_test_enabled", "Depth test enabled", "Depth", K::Boolean, 0, 1),
  STEP("depth_write_enabled", "Depth writes enabled", "Depth", K::Boolean, 0, 1),
  FIXED("depth_precision", "Depth-buffer precision", "Depth", 1, K::Integer, 16, 24),
  STEP("depth_comparison", "Depth comparison", "Depth", K::Enumeration, 0, 3),
  STEP("depth_visualization", "Depth visualization", "Depth", K::Enumeration, 0, 2),
  STEP("ordering_table_enabled", "Ordering-table submission", "Depth", K::Boolean, 0, 1),
  STEP("ordering_buckets", "Ordering-table buckets", "Depth", K::Integer, 4, 256),
  STEP("stencil_enabled", "Stencil mask enabled", "Stencil", K::Boolean, 0, 1),
  STEP("stencil_inverted", "Stencil comparison inverted", "Stencil", K::Boolean, 0, 1),
  STEP("stencil_reference", "Stencil reference", "Stencil", K::Integer, 0, 255),
  STEP("bits_per_channel", "Color bits per channel", "Color", K::Integer, 1, 8),
  STEP("dithering_enabled", "Ordered dithering", "Color", K::Boolean, 0, 1),
  STEP("linear_light", "Linear-light calculations", "Color", K::Boolean, 0, 1),
  STEP("fog_enabled", "Distance fog enabled", "Post", K::Boolean, 0, 1),
  CONT("fog_start", "Fog start", "Post", 1, K::Float, 0, 100),
  CONT("fog_end", "Fog end", "Post", 1, K::Float, 0, 100),
  STEP("overdraw_enabled", "Overdraw visualization", "Post", K::Boolean, 0, 1),
  CONT("overdraw_range", "Overdraw heat-map maximum", "Post", 1, K::Float, 1, 64),
  FIXED("internal_resolution", "Internal resolution", "Output", 2, K::Vec2, 1, 4096),
  STEP("nearest_upscaling", "Nearest viewport upscaling", "Output", K::Boolean, 0, 1),
  STEP("n64_cycle_type", "N64 RDP cycle type", "N64 surface", K::Enumeration, 1, 2),
  STEP("n64_cycle_0_a", "N64 cycle 0 source A", "N64 combiner", K::Enumeration, 0, 8),
  STEP("n64_cycle_0_b", "N64 cycle 0 source B", "N64 combiner", K::Enumeration, 0, 8),
  STEP("n64_cycle_0_c", "N64 cycle 0 source C", "N64 combiner", K::Enumeration, 0, 8),
  STEP("n64_cycle_0_d", "N64 cycle 0 source D", "N64 combiner", K::Enumeration, 0, 8),
  STEP("n64_cycle_1_a", "N64 cycle 1 source A", "N64 combiner", K::Enumeration, 0, 8),
  STEP("n64_cycle_1_b", "N64 cycle 1 source B", "N64 combiner", K::Enumeration, 0, 8),
  STEP("n64_cycle_1_c", "N64 cycle 1 source C", "N64 combiner", K::Enumeration, 0, 8),
  STEP("n64_cycle_1_d", "N64 cycle 1 source D", "N64 combiner", K::Enumeration, 0, 8),
  CONT("primitive_color", "N64 primitive color", "N64 surface", 4, K::Color4, 0, 1),
  CONT("environment_color", "N64 environment color", "N64 surface", 4, K::Color4, 0, 1),
  STEP("n64_texture_format", "N64 texture format", "N64 texture", K::Enumeration, 0, 8),
  STEP("n64_texture_filter", "N64 texture filter", "N64 texture", K::Enumeration, 0, 2),
  STEP("n64_mipmap_mode", "N64 mip/detail mode", "N64 texture", K::Enumeration, 0, 4),
  FIXED("n64_tile_size", "N64 tile size", "N64 texture", 2, K::Vec2, 1, 64),
  STEP("n64_mirror_s", "N64 mirror S", "N64 texture", K::Boolean, 0, 1),
  STEP("n64_mirror_t", "N64 mirror T", "N64 texture", K::Boolean, 0, 1),
  STEP("n64_shift_s", "N64 S coordinate shift", "N64 texture", K::Integer, -5, 5),
  STEP("n64_shift_t", "N64 T coordinate shift", "N64 texture", K::Integer, -5, 5),
  STEP("n64_texture_generation", "N64 texture-coordinate generation", "N64 surface", K::Boolean, 0, 1),
  STEP("n64_surface_mode", "N64 surface mode", "N64 surface", K::Enumeration, 0, 3),
  STEP("n64_z_compare", "N64 Z compare", "N64 depth", K::Boolean, 0, 1),
  STEP("n64_z_update", "N64 Z update", "N64 depth", K::Boolean, 0, 1),
  STEP("n64_alpha_compare", "N64 alpha compare", "N64 surface", K::Enumeration, 0, 2),
  CONT("alpha_threshold", "N64 alpha threshold", "N64 surface", 1, K::Float, 0, 1),
  STEP("n64_coverage_antialiasing", "N64 coverage antialiasing", "N64 rasterization", K::Boolean, 0, 1),
  STEP("n64_framebuffer_format", "N64 framebuffer format", "N64 output", K::Enumeration, 0, 1),
  STEP("n64_color_dither", "N64 color dither", "N64 output", K::Enumeration, 0, 3),
  STEP("n64_vi_reconstruction", "N64 VI reconstruction", "N64 output", K::Boolean, 0, 1),
  STEP("n64_vi_divot", "N64 VI divot filter", "N64 output", K::Boolean, 0, 1),
}};
#undef CONT
#undef STEP
#undef FIXED

std::size_t nearbyIndex(const std::vector<PropertyKeyframe>& keyframes, const float timeSeconds,
    const float toleranceSeconds) {
  std::size_t closest = std::numeric_limits<std::size_t>::max();
  float distance = toleranceSeconds;
  for (std::size_t index = 0; index < keyframes.size(); ++index) {
    const float candidate = std::abs(keyframes[index].timeSeconds - timeSeconds);
    if (candidate <= distance) {
      closest = index;
      distance = candidate;
    }
  }
  return closest;
}

} // namespace

void AnimationTimeline::advance(const float deltaSeconds) {
  if (!playing || durationSeconds <= 0.0f) return;
  timeSeconds += deltaSeconds * playbackRate;
  if (loop) {
    timeSeconds = std::fmod(timeSeconds, durationSeconds);
    if (timeSeconds < 0.0f) timeSeconds += durationSeconds;
  } else if (timeSeconds >= durationSeconds) {
    timeSeconds = durationSeconds;
    playing = false;
  } else if (timeSeconds <= 0.0f) {
    timeSeconds = 0.0f;
    playing = false;
  }
}

const AnimationPropertyInfo& animationPropertyInfo(const AnimationProperty property) {
  return propertyInfo[std::min(static_cast<std::size_t>(property), propertyInfo.size() - 1)];
}

bool animationPropertyIsAnimatable(const AnimationProperty property) {
  return property != AnimationProperty::Count &&
    animationPropertyInfo(property).behavior != AnimationBehavior::NotAnimatable;
}

bool animationPropertyValuesEqual(const AnimationProperty property, const glm::vec4& a, const glm::vec4& b) {
  const AnimationPropertyInfo& info = animationPropertyInfo(property);
  for (int component = 0; component < info.components; ++component)
    if (std::abs(a[component] - b[component]) > 0.000001f) return false;
  return true;
}

const char* animationPropertyDiscreteValueLabel(const AnimationProperty property, const int value) {
  const auto label = [value](const auto& labels, const int first = 0) -> const char* {
    const int index = value - first;
    return index >= 0 && index < static_cast<int>(labels.size()) ? labels[static_cast<std::size_t>(index)] : nullptr;
  };
  switch (property) {
  case AnimationProperty::PassOutput: { constexpr std::array labels = {"Color", "Linear depth", "Normals", "Vertex colors", "Field signal preview"}; return label(labels); }
  case AnimationProperty::TextureSource: { constexpr std::array labels = {"Scene material", "Built-in checker", "Imported override", "White texel"}; return label(labels); }
  case AnimationProperty::UvMapping: { constexpr std::array labels = {"Mesh UV0", "Planar XY", "Planar XZ", "Planar YZ"}; return label(labels); }
  case AnimationProperty::CompositeOperation: return value >= 0 && value <= 19 ? relationOperatorLabel(static_cast<RelationOperator>(value)) : nullptr;
  case AnimationProperty::CompositeSourceA:
  case AnimationProperty::CompositeSourceB: { constexpr std::array labels = {"Accumulated result", "Current pass", "Render pass", "Fixed color", "Previous frame", "Render-pass field", "Render-pass spectrum"}; return label(labels); }
  case AnimationProperty::CompositeInterpretationA:
  case AnimationProperty::CompositeInterpretationB: { constexpr std::array labels = {"Raw RGB", "L cone", "M cone", "S cone", "Cone luminance", "Rod", "Red-green opponent", "Blue-yellow opponent", "Spectral human", "Spectral alternate", "Spectral rod"}; return label(labels); }
  case AnimationProperty::SpectralIlluminant: { constexpr std::array labels = {"Reference daylight", "Tungsten 2856 K", "Tri-band LED"}; return label(labels); }
  case AnimationProperty::SpectralObserver: { constexpr std::array labels = {"Reference human", "Shifted observer", "Rod monochrome"}; return label(labels); }
  case AnimationProperty::CompositeColorSpace: { constexpr std::array labels = {"Encoded RGB", "Linear light"}; return label(labels); }
  case AnimationProperty::CompositeRange: { constexpr std::array labels = {"Clamp 0..1", "Preserve signed/HDR", "Wrap fractional"}; return label(labels); }
  case AnimationProperty::CompositeMask: { constexpr std::array labels = {"None", "Pass luminance", "Pass depth", "Pass edges", "Pass field"}; return label(labels); }
  case AnimationProperty::CullMode: { constexpr std::array labels = {"None", "Back faces", "Front faces"}; return label(labels); }
  case AnimationProperty::SurfaceVisualization: { constexpr std::array labels = {"Texture", "UV coordinates", "Normals", "Vertex colors", "Tangents", "Bitangents"}; return label(labels); }
  case AnimationProperty::TransparencyOperation: { constexpr std::array labels = {"Opaque", "Alpha test", "Straight alpha", "Premultiplied alpha", "Additive", "Multiply", "PS1 average", "PS1 additive", "PS1 subtractive", "PS1 quarter-add"}; return label(labels); }
  case AnimationProperty::TextureColorStorage: { constexpr std::array labels = {"Direct color", "Indexed 8-bit", "Indexed 4-bit"}; return label(labels); }
  case AnimationProperty::LightingModel: { constexpr std::array labels = {"Unlit", "Gouraud Lambert", "Phong-shaded Lambert", "Phong reflection", "Blinn-Phong reflection"}; return label(labels); }
  case AnimationProperty::FieldVisualization: { constexpr std::array labels = {
    "Channel 0: source A / temperature", "Channel 1: source B / smoke", "Channel 2: phase / fuel",
    "Channel 3: interference / pressure", "Channel 4: distance / flow speed",
    "Channel 5: contours / moisture", "Channel 6: combustion rate"}; return label(labels); }
  case AnimationProperty::FieldProducerKind: { constexpr std::array labels = {
    "Wave interference", "Signed distance field", "Persistent elemental simulation"}; return label(labels); }
  case AnimationProperty::SdfAType:
  case AnimationProperty::SdfBType: { constexpr std::array labels = {"Sphere", "Box", "Torus",
    "4D Hypersphere Slice", "Pulsating Sphere"}; return label(labels); }
  case AnimationProperty::SdfOperation: { constexpr std::array labels = {"Union", "Intersection", "A subtract B", "Smooth union"}; return label(labels); }
  case AnimationProperty::DepthComparison: { constexpr std::array labels = {"Less", "Less or equal", "Greater", "Always"}; return label(labels); }
  case AnimationProperty::DepthVisualization: { constexpr std::array labels = {"Off", "Raw window depth", "Linear camera depth"}; return label(labels); }
  case AnimationProperty::N64CycleType: { constexpr std::array labels = {"1-cycle", "2-cycle"}; return label(labels, 1); }
  case AnimationProperty::N64Cycle0A: case AnimationProperty::N64Cycle0B:
  case AnimationProperty::N64Cycle0C: case AnimationProperty::N64Cycle0D:
  case AnimationProperty::N64Cycle1A: case AnimationProperty::N64Cycle1B:
  case AnimationProperty::N64Cycle1C: case AnimationProperty::N64Cycle1D: {
    constexpr std::array labels = {"ZERO", "TEXEL0", "ONE", "SHADE", "PRIMITIVE", "ENVIRONMENT", "TEXEL1", "COMBINED", "LOD_FRACTION"}; return label(labels);
  }
  case AnimationProperty::N64TextureFormat: { constexpr std::array labels = {"RGBA16", "RGBA32", "CI4", "CI8", "IA4", "IA8", "IA16", "I4", "I8"}; return label(labels); }
  case AnimationProperty::N64TextureFilter: { constexpr std::array labels = {"Point", "Three-point", "Box average"}; return label(labels); }
  case AnimationProperty::N64MipmapMode: { constexpr std::array labels = {"Disabled", "Nearest level", "Trilinear", "Sharpen", "Detail"}; return label(labels); }
  case AnimationProperty::N64SurfaceMode: { constexpr std::array labels = {"Opaque", "Translucent", "Decal", "Interpenetrating"}; return label(labels); }
  case AnimationProperty::N64AlphaCompare: { constexpr std::array labels = {"Off", "Threshold", "Dither"}; return label(labels); }
  case AnimationProperty::N64FramebufferFormat: { constexpr std::array labels = {"RGBA16", "RGBA32"}; return label(labels); }
  case AnimationProperty::N64ColorDither: { constexpr std::array labels = {"Disabled", "Magic-square 4x4", "Bayer 4x4", "Noise"}; return label(labels); }
  default: return nullptr;
  }
}

glm::vec4 animationPropertyValue(const RenderPass& pass, const AnimationProperty property) {
  switch (property) {
  case AnimationProperty::PassEnabled: return glm::vec4(pass.enabled ? 1.0f : 0.0f);
  case AnimationProperty::PassOutput: return glm::vec4(static_cast<float>(pass.output));
  case AnimationProperty::TextureSource: return glm::vec4(static_cast<float>(pass.textureSource));
  case AnimationProperty::TextureColorInterpretation: return glm::vec4(pass.importedTextureSrgb ? 1.0f : 0.0f);
  case AnimationProperty::ModelTranslation: return glm::vec4(pass.perturbation.modelTranslation, 0.0f);
  case AnimationProperty::ModelScale: return glm::vec4(pass.perturbation.modelScale);
  case AnimationProperty::NormalInflation: return glm::vec4(pass.perturbation.normalInflation);
  case AnimationProperty::UvOffset: return glm::vec4(pass.perturbation.uvOffset, 0.0f, 0.0f);
  case AnimationProperty::UvScale: return glm::vec4(pass.perturbation.uvScale, 0.0f, 0.0f);
  case AnimationProperty::UvRotation: return glm::vec4(pass.perturbation.uvRotation);
  case AnimationProperty::UvPivot: return glm::vec4(pass.perturbation.uvPivot, 0.0f, 0.0f);
  case AnimationProperty::UvMapping: return glm::vec4(static_cast<float>(pass.perturbation.uvMapping));
  case AnimationProperty::CameraYaw: return glm::vec4(pass.perturbation.cameraYaw);
  case AnimationProperty::CameraPitch: return glm::vec4(pass.perturbation.cameraPitch);
  case AnimationProperty::CameraDistance: return glm::vec4(pass.perturbation.cameraDistance);
  case AnimationProperty::CameraLateral: return glm::vec4(pass.perturbation.cameraLateral);
  case AnimationProperty::StereoConvergence: return glm::vec4(pass.perturbation.stereoConvergence);
  case AnimationProperty::FieldOfViewOffset: return glm::vec4(pass.perturbation.fieldOfView);
  case AnimationProperty::CompositeGain: return glm::vec4(pass.composite.gain);
  case AnimationProperty::CompositeBias: return glm::vec4(pass.composite.bias);
  case AnimationProperty::CompositeOpacity: return glm::vec4(pass.composite.opacity);
  case AnimationProperty::CompositeOperation: return glm::vec4(static_cast<float>(pass.composite.operation));
  case AnimationProperty::CompositeSourceA: return glm::vec4(static_cast<float>(pass.composite.sourceA));
  case AnimationProperty::CompositeSourceB: return glm::vec4(static_cast<float>(pass.composite.sourceB));
  case AnimationProperty::CompositeSourceAPass: return glm::vec4(pass.composite.sourceAPassId);
  case AnimationProperty::CompositeSourceBPass: return glm::vec4(pass.composite.sourceBPassId);
  case AnimationProperty::CompositeInterpretationA: return glm::vec4(static_cast<float>(pass.composite.interpretationA));
  case AnimationProperty::CompositeInterpretationB: return glm::vec4(static_cast<float>(pass.composite.interpretationB));
  case AnimationProperty::CompositeFixedColor: return pass.composite.fixedColor;
  case AnimationProperty::CompositeBitDepth: return glm::vec4(pass.composite.bitDepth);
  case AnimationProperty::CompositeHistoryDecay: return glm::vec4(pass.composite.historyDecay);
  case AnimationProperty::CompositeHistoryUvOffset: return glm::vec4(pass.composite.historyUvOffset, 0.0f, 0.0f);
  case AnimationProperty::CompositeHistoryUvScale: return glm::vec4(pass.composite.historyUvScale, 0.0f, 0.0f);
  case AnimationProperty::CompositeColorSpace: return glm::vec4(static_cast<float>(pass.composite.colorSpace));
  case AnimationProperty::CompositeRange: return glm::vec4(static_cast<float>(pass.composite.range));
  case AnimationProperty::CompositeMask: return glm::vec4(static_cast<float>(pass.composite.mask));
  case AnimationProperty::CompositeMaskInverted: return glm::vec4(pass.composite.invertMask ? 1.0f : 0.0f);
  case AnimationProperty::StereoAnalysisMode: return glm::vec4(static_cast<float>(pass.stereoAnalysis));
  case AnimationProperty::StereoMaximumDisparity: return glm::vec4(pass.stereoMaximumDisparityPixels);
  case AnimationProperty::StereoOcclusionTolerance: return glm::vec4(pass.stereoOcclusionTolerance);
  case AnimationProperty::VertexQuantization: return glm::vec4(pass.renderer.geometry.vertexQuantization);
  case AnimationProperty::ClippingEnabled: return glm::vec4(pass.renderer.geometry.clipping ? 1.0f : 0.0f);
  case AnimationProperty::ClippingHeight: return glm::vec4(pass.renderer.geometry.clipHeight);
  case AnimationProperty::ClippingKeepAbove: return glm::vec4(pass.renderer.geometry.clipAbove ? 1.0f : 0.0f);
  case AnimationProperty::ProjectionOrthographic: return glm::vec4(pass.renderer.camera.orthographic ? 1.0f : 0.0f);
  case AnimationProperty::FieldOfView: return glm::vec4(pass.renderer.camera.fieldOfView);
  case AnimationProperty::OrthographicSize: return glm::vec4(pass.renderer.camera.orthographicSize);
  case AnimationProperty::NearPlane: return glm::vec4(pass.renderer.camera.nearPlane);
  case AnimationProperty::AffineMapping: return glm::vec4(pass.renderer.rasterization.affineMapping ? 1.0f : 0.0f);
  case AnimationProperty::CullMode: return glm::vec4(pass.renderer.rasterization.cullMode);
  case AnimationProperty::MultisampleCount: return glm::vec4(pass.renderer.rasterization.samples);
  case AnimationProperty::PolygonOffsetEnabled: return glm::vec4(pass.renderer.rasterization.polygonOffset ? 1.0f : 0.0f);
  case AnimationProperty::PolygonOffsetFactor: return glm::vec4(pass.renderer.rasterization.polygonOffsetFactor);
  case AnimationProperty::PolygonOffsetUnits: return glm::vec4(pass.renderer.rasterization.polygonOffsetUnits);
  case AnimationProperty::SmoothShading: return glm::vec4(pass.renderer.surface.smoothShading ? 1.0f : 0.0f);
  case AnimationProperty::WireframeOverlay: return glm::vec4(pass.renderer.surface.wireframe ? 1.0f : 0.0f);
  case AnimationProperty::SurfaceVisualization: return glm::vec4(pass.renderer.surface.visualization);
  case AnimationProperty::NormalMappingEnabled: return glm::vec4(pass.renderer.surface.normalMapping ? 1.0f : 0.0f);
  case AnimationProperty::NormalStrength: return glm::vec4(pass.renderer.surface.normalStrength);
  case AnimationProperty::TransparencyOperation: return glm::vec4(pass.renderer.surface.transparency);
  case AnimationProperty::AlphaCutoff: return glm::vec4(pass.renderer.surface.alphaCutoff);
  case AnimationProperty::ReverseDrawOrder: return glm::vec4(pass.renderer.surface.reverseDrawOrder ? 1.0f : 0.0f);
  case AnimationProperty::NearestFiltering: return glm::vec4(pass.renderer.texture.nearestFiltering ? 1.0f : 0.0f);
  case AnimationProperty::TextureRepeat: return glm::vec4(pass.renderer.texture.repeat ? 1.0f : 0.0f);
  case AnimationProperty::Mipmapping: return glm::vec4(pass.renderer.texture.mipmapping ? 1.0f : 0.0f);
  case AnimationProperty::TrilinearFiltering: return glm::vec4(pass.renderer.texture.trilinear ? 1.0f : 0.0f);
  case AnimationProperty::Anisotropy: return glm::vec4(pass.renderer.texture.anisotropy);
  case AnimationProperty::TextureColorStorage: return glm::vec4(pass.renderer.texture.colorMode);
  case AnimationProperty::LightingModel: return glm::vec4(pass.renderer.lighting.model);
  case AnimationProperty::Ambient: return glm::vec4(pass.renderer.lighting.ambient);
  case AnimationProperty::LightAzimuth: return glm::vec4(pass.renderer.lighting.azimuth);
  case AnimationProperty::LightElevation: return glm::vec4(pass.renderer.lighting.elevation);
  case AnimationProperty::Shininess: return glm::vec4(pass.renderer.lighting.shininess);
  case AnimationProperty::ShadowsEnabled: return glm::vec4(pass.renderer.lighting.shadows ? 1.0f : 0.0f);
  case AnimationProperty::ShadowResolution: return glm::vec4(pass.renderer.lighting.shadowResolution);
  case AnimationProperty::ShadowBias: return glm::vec4(pass.renderer.lighting.shadowBias);
  case AnimationProperty::ShadowPcf: return glm::vec4(pass.renderer.lighting.shadowPcf ? 1.0f : 0.0f);
  case AnimationProperty::ShadowMapVisualization: return glm::vec4(pass.renderer.lighting.visualizeShadowMap ? 1.0f : 0.0f);
  case AnimationProperty::DepthCueEnabled: return glm::vec4(pass.renderer.lighting.depthCue ? 1.0f : 0.0f);
  case AnimationProperty::DepthCueStart: return glm::vec4(pass.renderer.lighting.depthCueStart);
  case AnimationProperty::DepthCueEnd: return glm::vec4(pass.renderer.lighting.depthCueEnd);
  case AnimationProperty::FarColor: return glm::vec4(pass.renderer.lighting.farColor, 1.0f);
  case AnimationProperty::FieldEnabled: return glm::vec4(pass.renderer.field.enabled ? 1.0f : 0.0f);
  case AnimationProperty::FieldProducerKind: return glm::vec4(pass.renderer.field.producerKind);
  case AnimationProperty::FieldSourceA: return glm::vec4(pass.renderer.field.sourceA, 0.0f);
  case AnimationProperty::FieldSourceB: return glm::vec4(pass.renderer.field.sourceB, 0.0f);
  case AnimationProperty::FieldWavelength: return glm::vec4(pass.renderer.field.wavelength);
  case AnimationProperty::FieldPhaseOffset: return glm::vec4(pass.renderer.field.phaseOffset);
  case AnimationProperty::FieldAmplitudeA: return glm::vec4(pass.renderer.field.amplitudeA);
  case AnimationProperty::FieldAmplitudeB: return glm::vec4(pass.renderer.field.amplitudeB);
  case AnimationProperty::FieldFalloff: return glm::vec4(pass.renderer.field.falloff);
  case AnimationProperty::FieldBandSharpness: return glm::vec4(pass.renderer.field.bandSharpness);
  case AnimationProperty::FieldVisualization: return glm::vec4(pass.renderer.field.visualization);
  case AnimationProperty::FieldLowColor: return glm::vec4(pass.renderer.field.lowColor, 1.0f);
  case AnimationProperty::FieldHighColor: return glm::vec4(pass.renderer.field.highColor, 1.0f);
  case AnimationProperty::FieldVertexDisplacement: return glm::vec4(pass.renderer.field.vertexDisplacement);
  case AnimationProperty::FieldSignedDisplacement: return glm::vec4(pass.renderer.field.signedDisplacement ? 1.0f : 0.0f);
  case AnimationProperty::FieldDiscardEnabled: return glm::vec4(pass.renderer.field.discardBelowEnabled ? 1.0f : 0.0f);
  case AnimationProperty::FieldDiscardThreshold: return glm::vec4(pass.renderer.field.discardThreshold);
  case AnimationProperty::FieldSurfaceColorInfluence: return glm::vec4(pass.renderer.field.surfaceColorInfluence);
  case AnimationProperty::FieldEmissionInfluence: return glm::vec4(pass.renderer.field.emissionInfluence);
  case AnimationProperty::SdfAType: return glm::vec4(pass.renderer.field.sdfA.type);
  case AnimationProperty::SdfAPosition: return glm::vec4(pass.renderer.field.sdfA.position, 0.0f);
  case AnimationProperty::SdfAParameters: return glm::vec4(pass.renderer.field.sdfA.parameters, 0.0f);
  case AnimationProperty::SdfBType: return glm::vec4(pass.renderer.field.sdfB.type);
  case AnimationProperty::SdfBPosition: return glm::vec4(pass.renderer.field.sdfB.position, 0.0f);
  case AnimationProperty::SdfBParameters: return glm::vec4(pass.renderer.field.sdfB.parameters, 0.0f);
  case AnimationProperty::SdfOperation: return glm::vec4(pass.renderer.field.sdfOperation);
  case AnimationProperty::SdfSmoothness: return glm::vec4(pass.renderer.field.sdfSmoothness);
  case AnimationProperty::SdfPreviewRange: return glm::vec4(pass.renderer.field.sdfPreviewRange);
  case AnimationProperty::IsoSurfaceEnabled: return glm::vec4(pass.renderer.field.isoSurfaceEnabled ? 1.0f : 0.0f);
  case AnimationProperty::IsoLevel: return glm::vec4(pass.renderer.field.isoLevel);
  case AnimationProperty::IsoColor: return glm::vec4(pass.renderer.field.isoColor, 1.0f);
  case AnimationProperty::IsoMaximumSteps: return glm::vec4(pass.renderer.field.isoMaxSteps);
  case AnimationProperty::IsoHitEpsilon: return glm::vec4(pass.renderer.field.isoEpsilon);
  case AnimationProperty::IsoMaximumDistance: return glm::vec4(pass.renderer.field.isoMaxDistance);
  case AnimationProperty::SpectralIlluminant: return glm::vec4(pass.renderer.spectral.illuminant);
  case AnimationProperty::SpectralObserver: return glm::vec4(pass.renderer.spectral.observer);
  case AnimationProperty::SpectralExposure: return glm::vec4(pass.renderer.spectral.exposure);
  case AnimationProperty::DepthTestEnabled: return glm::vec4(pass.renderer.depth.testing ? 1.0f : 0.0f);
  case AnimationProperty::DepthWriteEnabled: return glm::vec4(pass.renderer.depth.writing ? 1.0f : 0.0f);
  case AnimationProperty::DepthPrecision: return glm::vec4(pass.renderer.depth.precision);
  case AnimationProperty::DepthComparison: return glm::vec4(pass.renderer.depth.function);
  case AnimationProperty::DepthVisualization: return glm::vec4(pass.renderer.depth.visualization);
  case AnimationProperty::OrderingTableEnabled: return glm::vec4(pass.renderer.depth.orderingTable ? 1.0f : 0.0f);
  case AnimationProperty::OrderingBuckets: return glm::vec4(pass.renderer.depth.orderingBuckets);
  case AnimationProperty::StencilEnabled: return glm::vec4(pass.renderer.stencil.enabled ? 1.0f : 0.0f);
  case AnimationProperty::StencilInverted: return glm::vec4(pass.renderer.stencil.invert ? 1.0f : 0.0f);
  case AnimationProperty::StencilReference: return glm::vec4(pass.renderer.stencil.reference);
  case AnimationProperty::BitsPerChannel: return glm::vec4(pass.renderer.color.bitsPerChannel);
  case AnimationProperty::DitheringEnabled: return glm::vec4(pass.renderer.color.dithering ? 1.0f : 0.0f);
  case AnimationProperty::LinearLight: return glm::vec4(pass.renderer.color.linearLight ? 1.0f : 0.0f);
  case AnimationProperty::FogEnabled: return glm::vec4(pass.renderer.post.fog ? 1.0f : 0.0f);
  case AnimationProperty::FogStart: return glm::vec4(pass.renderer.post.fogStart);
  case AnimationProperty::FogEnd: return glm::vec4(pass.renderer.post.fogEnd);
  case AnimationProperty::OverdrawEnabled: return glm::vec4(pass.renderer.post.overdraw ? 1.0f : 0.0f);
  case AnimationProperty::OverdrawRange: return glm::vec4(pass.renderer.post.overdrawRange);
  case AnimationProperty::InternalResolution: return glm::vec4(pass.renderer.output.width, pass.renderer.output.height, 0, 0);
  case AnimationProperty::NearestUpscaling: return glm::vec4(pass.renderer.output.nearestUpscaling ? 1.0f : 0.0f);
  case AnimationProperty::N64CycleType: return glm::vec4(pass.renderer.n64.cycleType);
  case AnimationProperty::N64Cycle0A: return glm::vec4(pass.renderer.n64.cycle0.a);
  case AnimationProperty::N64Cycle0B: return glm::vec4(pass.renderer.n64.cycle0.b);
  case AnimationProperty::N64Cycle0C: return glm::vec4(pass.renderer.n64.cycle0.c);
  case AnimationProperty::N64Cycle0D: return glm::vec4(pass.renderer.n64.cycle0.d);
  case AnimationProperty::N64Cycle1A: return glm::vec4(pass.renderer.n64.cycle1.a);
  case AnimationProperty::N64Cycle1B: return glm::vec4(pass.renderer.n64.cycle1.b);
  case AnimationProperty::N64Cycle1C: return glm::vec4(pass.renderer.n64.cycle1.c);
  case AnimationProperty::N64Cycle1D: return glm::vec4(pass.renderer.n64.cycle1.d);
  case AnimationProperty::PrimitiveColor: return pass.renderer.n64.primitiveColor;
  case AnimationProperty::EnvironmentColor: return pass.renderer.n64.environmentColor;
  case AnimationProperty::N64TextureFormat: return glm::vec4(pass.renderer.n64.textureFormat);
  case AnimationProperty::N64TextureFilter: return glm::vec4(pass.renderer.n64.textureFilter);
  case AnimationProperty::N64MipmapMode: return glm::vec4(pass.renderer.n64.mipmapMode);
  case AnimationProperty::N64TileSize: return glm::vec4(pass.renderer.n64.tileWidth, pass.renderer.n64.tileHeight, 0, 0);
  case AnimationProperty::N64MirrorS: return glm::vec4(pass.renderer.n64.mirrorS ? 1.0f : 0.0f);
  case AnimationProperty::N64MirrorT: return glm::vec4(pass.renderer.n64.mirrorT ? 1.0f : 0.0f);
  case AnimationProperty::N64ShiftS: return glm::vec4(pass.renderer.n64.shiftS);
  case AnimationProperty::N64ShiftT: return glm::vec4(pass.renderer.n64.shiftT);
  case AnimationProperty::N64TextureGeneration: return glm::vec4(pass.renderer.n64.textureGeneration ? 1.0f : 0.0f);
  case AnimationProperty::N64SurfaceMode: return glm::vec4(pass.renderer.n64.surfaceMode);
  case AnimationProperty::N64ZCompare: return glm::vec4(pass.renderer.n64.zCompare ? 1.0f : 0.0f);
  case AnimationProperty::N64ZUpdate: return glm::vec4(pass.renderer.n64.zUpdate ? 1.0f : 0.0f);
  case AnimationProperty::N64AlphaCompare: return glm::vec4(pass.renderer.n64.alphaCompare);
  case AnimationProperty::AlphaThreshold: return glm::vec4(pass.renderer.n64.alphaThreshold);
  case AnimationProperty::N64CoverageAntialiasing: return glm::vec4(pass.renderer.n64.coverageAntialiasing ? 1.0f : 0.0f);
  case AnimationProperty::N64FramebufferFormat: return glm::vec4(pass.renderer.n64.framebufferFormat);
  case AnimationProperty::N64ColorDither: return glm::vec4(pass.renderer.n64.colorDither);
  case AnimationProperty::N64ViReconstruction: return glm::vec4(pass.renderer.n64.viReconstruction ? 1.0f : 0.0f);
  case AnimationProperty::N64ViDivot: return glm::vec4(pass.renderer.n64.viDivot ? 1.0f : 0.0f);
  case AnimationProperty::Count: break;
  }
  return glm::vec4(0.0f);
}

void setAnimationPropertyValue(RenderPass& pass, const AnimationProperty property, const glm::vec4& value) {
  switch (property) {
  case AnimationProperty::PassEnabled: pass.enabled = value.x >= 0.5f; break;
  case AnimationProperty::PassOutput: pass.output = static_cast<PassOutput>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::TextureSource: pass.textureSource = static_cast<TextureSource>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::TextureColorInterpretation: pass.importedTextureSrgb = value.x >= 0.5f; break;
  case AnimationProperty::ModelTranslation: pass.perturbation.modelTranslation = glm::vec3(value); break;
  case AnimationProperty::ModelScale: pass.perturbation.modelScale = value.x; break;
  case AnimationProperty::NormalInflation: pass.perturbation.normalInflation = value.x; break;
  case AnimationProperty::UvOffset: pass.perturbation.uvOffset = glm::vec2(value); break;
  case AnimationProperty::UvScale: pass.perturbation.uvScale = glm::vec2(value); break;
  case AnimationProperty::UvRotation: pass.perturbation.uvRotation = value.x; break;
  case AnimationProperty::UvPivot: pass.perturbation.uvPivot = glm::vec2(value); break;
  case AnimationProperty::UvMapping: pass.perturbation.uvMapping = static_cast<UvMapping>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::CameraYaw: pass.perturbation.cameraYaw = value.x; break;
  case AnimationProperty::CameraPitch: pass.perturbation.cameraPitch = value.x; break;
  case AnimationProperty::CameraDistance: pass.perturbation.cameraDistance = value.x; break;
  case AnimationProperty::CameraLateral: pass.perturbation.cameraLateral = value.x; break;
  case AnimationProperty::StereoConvergence: pass.perturbation.stereoConvergence = value.x; break;
  case AnimationProperty::FieldOfViewOffset: pass.perturbation.fieldOfView = value.x; break;
  case AnimationProperty::CompositeGain: pass.composite.gain = value.x; break;
  case AnimationProperty::CompositeBias: pass.composite.bias = value.x; break;
  case AnimationProperty::CompositeOpacity: pass.composite.opacity = value.x; break;
  case AnimationProperty::CompositeOperation: pass.composite.operation = static_cast<RelationOperator>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::CompositeSourceA: pass.composite.sourceA = static_cast<CompositeSource>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::CompositeSourceB: pass.composite.sourceB = static_cast<CompositeSource>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::CompositeSourceAPass: pass.composite.sourceAPassId = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::CompositeSourceBPass: pass.composite.sourceBPassId = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::CompositeInterpretationA: pass.composite.interpretationA = static_cast<CompositeInterpretation>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::CompositeInterpretationB: pass.composite.interpretationB = static_cast<CompositeInterpretation>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::CompositeFixedColor: pass.composite.fixedColor = value; break;
  case AnimationProperty::CompositeBitDepth: pass.composite.bitDepth = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::CompositeHistoryDecay: pass.composite.historyDecay = value.x; break;
  case AnimationProperty::CompositeHistoryUvOffset: pass.composite.historyUvOffset = glm::vec2(value); break;
  case AnimationProperty::CompositeHistoryUvScale: pass.composite.historyUvScale = glm::vec2(value); break;
  case AnimationProperty::CompositeColorSpace: pass.composite.colorSpace = static_cast<CompositeColorSpace>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::CompositeRange: pass.composite.range = static_cast<CompositeRange>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::CompositeMask: pass.composite.mask = static_cast<CompositeMask>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::CompositeMaskInverted: pass.composite.invertMask = value.x >= 0.5f; break;
  case AnimationProperty::StereoAnalysisMode:
    pass.stereoAnalysis = static_cast<gfxlab::StereoAnalysisMode>(static_cast<int>(std::round(value.x))); break;
  case AnimationProperty::StereoMaximumDisparity: pass.stereoMaximumDisparityPixels = value.x; break;
  case AnimationProperty::StereoOcclusionTolerance: pass.stereoOcclusionTolerance = value.x; break;
  case AnimationProperty::VertexQuantization: pass.renderer.geometry.vertexQuantization = value.x; break;
  case AnimationProperty::ClippingEnabled: pass.renderer.geometry.clipping = value.x >= 0.5f; break;
  case AnimationProperty::ClippingHeight: pass.renderer.geometry.clipHeight = value.x; break;
  case AnimationProperty::ClippingKeepAbove: pass.renderer.geometry.clipAbove = value.x >= 0.5f; break;
  case AnimationProperty::ProjectionOrthographic: pass.renderer.camera.orthographic = value.x >= 0.5f; break;
  case AnimationProperty::FieldOfView: pass.renderer.camera.fieldOfView = value.x; break;
  case AnimationProperty::OrthographicSize: pass.renderer.camera.orthographicSize = value.x; break;
  case AnimationProperty::NearPlane: pass.renderer.camera.nearPlane = value.x; break;
  case AnimationProperty::AffineMapping: pass.renderer.rasterization.affineMapping = value.x >= 0.5f; break;
  case AnimationProperty::CullMode: pass.renderer.rasterization.cullMode = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::MultisampleCount: pass.renderer.rasterization.samples = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::PolygonOffsetEnabled: pass.renderer.rasterization.polygonOffset = value.x >= 0.5f; break;
  case AnimationProperty::PolygonOffsetFactor: pass.renderer.rasterization.polygonOffsetFactor = value.x; break;
  case AnimationProperty::PolygonOffsetUnits: pass.renderer.rasterization.polygonOffsetUnits = value.x; break;
  case AnimationProperty::SmoothShading: pass.renderer.surface.smoothShading = value.x >= 0.5f; break;
  case AnimationProperty::WireframeOverlay: pass.renderer.surface.wireframe = value.x >= 0.5f; break;
  case AnimationProperty::SurfaceVisualization: pass.renderer.surface.visualization = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::NormalMappingEnabled: pass.renderer.surface.normalMapping = value.x >= 0.5f; break;
  case AnimationProperty::NormalStrength: pass.renderer.surface.normalStrength = value.x; break;
  case AnimationProperty::TransparencyOperation: pass.renderer.surface.transparency = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::AlphaCutoff: pass.renderer.surface.alphaCutoff = value.x; break;
  case AnimationProperty::ReverseDrawOrder: pass.renderer.surface.reverseDrawOrder = value.x >= 0.5f; break;
  case AnimationProperty::NearestFiltering: pass.renderer.texture.nearestFiltering = value.x >= 0.5f; break;
  case AnimationProperty::TextureRepeat: pass.renderer.texture.repeat = value.x >= 0.5f; break;
  case AnimationProperty::Mipmapping: pass.renderer.texture.mipmapping = value.x >= 0.5f; break;
  case AnimationProperty::TrilinearFiltering: pass.renderer.texture.trilinear = value.x >= 0.5f; break;
  case AnimationProperty::Anisotropy: pass.renderer.texture.anisotropy = value.x; break;
  case AnimationProperty::TextureColorStorage: pass.renderer.texture.colorMode = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::LightingModel: pass.renderer.lighting.model = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::Ambient: pass.renderer.lighting.ambient = value.x; break;
  case AnimationProperty::LightAzimuth: pass.renderer.lighting.azimuth = value.x; break;
  case AnimationProperty::LightElevation: pass.renderer.lighting.elevation = value.x; break;
  case AnimationProperty::Shininess: pass.renderer.lighting.shininess = value.x; break;
  case AnimationProperty::ShadowsEnabled: pass.renderer.lighting.shadows = value.x >= 0.5f; break;
  case AnimationProperty::ShadowResolution: pass.renderer.lighting.shadowResolution = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::ShadowBias: pass.renderer.lighting.shadowBias = value.x; break;
  case AnimationProperty::ShadowPcf: pass.renderer.lighting.shadowPcf = value.x >= 0.5f; break;
  case AnimationProperty::ShadowMapVisualization: pass.renderer.lighting.visualizeShadowMap = value.x >= 0.5f; break;
  case AnimationProperty::DepthCueEnabled: pass.renderer.lighting.depthCue = value.x >= 0.5f; break;
  case AnimationProperty::DepthCueStart: pass.renderer.lighting.depthCueStart = value.x; break;
  case AnimationProperty::DepthCueEnd: pass.renderer.lighting.depthCueEnd = value.x; break;
  case AnimationProperty::FarColor: pass.renderer.lighting.farColor = glm::vec3(value); break;
  case AnimationProperty::FieldEnabled: pass.renderer.field.enabled = value.x >= 0.5f; break;
  case AnimationProperty::FieldProducerKind: pass.renderer.field.producerKind = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::FieldSourceA: pass.renderer.field.sourceA = glm::vec3(value); break;
  case AnimationProperty::FieldSourceB: pass.renderer.field.sourceB = glm::vec3(value); break;
  case AnimationProperty::FieldWavelength: pass.renderer.field.wavelength = value.x; break;
  case AnimationProperty::FieldPhaseOffset: pass.renderer.field.phaseOffset = value.x; break;
  case AnimationProperty::FieldAmplitudeA: pass.renderer.field.amplitudeA = value.x; break;
  case AnimationProperty::FieldAmplitudeB: pass.renderer.field.amplitudeB = value.x; break;
  case AnimationProperty::FieldFalloff: pass.renderer.field.falloff = value.x; break;
  case AnimationProperty::FieldBandSharpness: pass.renderer.field.bandSharpness = value.x; break;
  case AnimationProperty::FieldVisualization: pass.renderer.field.visualization = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::FieldLowColor: pass.renderer.field.lowColor = glm::vec3(value); break;
  case AnimationProperty::FieldHighColor: pass.renderer.field.highColor = glm::vec3(value); break;
  case AnimationProperty::FieldVertexDisplacement: pass.renderer.field.vertexDisplacement = value.x; break;
  case AnimationProperty::FieldSignedDisplacement: pass.renderer.field.signedDisplacement = value.x >= 0.5f; break;
  case AnimationProperty::FieldDiscardEnabled: pass.renderer.field.discardBelowEnabled = value.x >= 0.5f; break;
  case AnimationProperty::FieldDiscardThreshold: pass.renderer.field.discardThreshold = value.x; break;
  case AnimationProperty::FieldSurfaceColorInfluence: pass.renderer.field.surfaceColorInfluence = value.x; break;
  case AnimationProperty::FieldEmissionInfluence: pass.renderer.field.emissionInfluence = value.x; break;
  case AnimationProperty::SdfAType: pass.renderer.field.sdfA.type = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::SdfAPosition: pass.renderer.field.sdfA.position = glm::vec3(value); break;
  case AnimationProperty::SdfAParameters: pass.renderer.field.sdfA.parameters = glm::vec3(value); break;
  case AnimationProperty::SdfBType: pass.renderer.field.sdfB.type = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::SdfBPosition: pass.renderer.field.sdfB.position = glm::vec3(value); break;
  case AnimationProperty::SdfBParameters: pass.renderer.field.sdfB.parameters = glm::vec3(value); break;
  case AnimationProperty::SdfOperation: pass.renderer.field.sdfOperation = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::SdfSmoothness: pass.renderer.field.sdfSmoothness = value.x; break;
  case AnimationProperty::SdfPreviewRange: pass.renderer.field.sdfPreviewRange = value.x; break;
  case AnimationProperty::IsoSurfaceEnabled: pass.renderer.field.isoSurfaceEnabled = value.x >= 0.5f; break;
  case AnimationProperty::IsoLevel: pass.renderer.field.isoLevel = value.x; break;
  case AnimationProperty::IsoColor: pass.renderer.field.isoColor = glm::vec3(value); break;
  case AnimationProperty::IsoMaximumSteps: pass.renderer.field.isoMaxSteps = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::IsoHitEpsilon: pass.renderer.field.isoEpsilon = value.x; break;
  case AnimationProperty::IsoMaximumDistance: pass.renderer.field.isoMaxDistance = value.x; break;
  case AnimationProperty::SpectralIlluminant: pass.renderer.spectral.illuminant = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::SpectralObserver: pass.renderer.spectral.observer = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::SpectralExposure: pass.renderer.spectral.exposure = value.x; break;
  case AnimationProperty::DepthTestEnabled: pass.renderer.depth.testing = value.x >= 0.5f; break;
  case AnimationProperty::DepthWriteEnabled: pass.renderer.depth.writing = value.x >= 0.5f; break;
  case AnimationProperty::DepthPrecision: pass.renderer.depth.precision = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::DepthComparison: pass.renderer.depth.function = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::DepthVisualization: pass.renderer.depth.visualization = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::OrderingTableEnabled: pass.renderer.depth.orderingTable = value.x >= 0.5f; break;
  case AnimationProperty::OrderingBuckets: pass.renderer.depth.orderingBuckets = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::StencilEnabled: pass.renderer.stencil.enabled = value.x >= 0.5f; break;
  case AnimationProperty::StencilInverted: pass.renderer.stencil.invert = value.x >= 0.5f; break;
  case AnimationProperty::StencilReference: pass.renderer.stencil.reference = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::BitsPerChannel: pass.renderer.color.bitsPerChannel = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::DitheringEnabled: pass.renderer.color.dithering = value.x >= 0.5f; break;
  case AnimationProperty::LinearLight: pass.renderer.color.linearLight = value.x >= 0.5f; break;
  case AnimationProperty::FogEnabled: pass.renderer.post.fog = value.x >= 0.5f; break;
  case AnimationProperty::FogStart: pass.renderer.post.fogStart = value.x; break;
  case AnimationProperty::FogEnd: pass.renderer.post.fogEnd = value.x; break;
  case AnimationProperty::OverdrawEnabled: pass.renderer.post.overdraw = value.x >= 0.5f; break;
  case AnimationProperty::OverdrawRange: pass.renderer.post.overdrawRange = value.x; break;
  case AnimationProperty::InternalResolution:
    pass.renderer.output.width = static_cast<int>(std::round(value.x));
    pass.renderer.output.height = static_cast<int>(std::round(value.y)); break;
  case AnimationProperty::NearestUpscaling: pass.renderer.output.nearestUpscaling = value.x >= 0.5f; break;
  case AnimationProperty::N64CycleType: pass.renderer.n64.cycleType = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64Cycle0A: pass.renderer.n64.cycle0.a = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64Cycle0B: pass.renderer.n64.cycle0.b = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64Cycle0C: pass.renderer.n64.cycle0.c = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64Cycle0D: pass.renderer.n64.cycle0.d = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64Cycle1A: pass.renderer.n64.cycle1.a = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64Cycle1B: pass.renderer.n64.cycle1.b = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64Cycle1C: pass.renderer.n64.cycle1.c = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64Cycle1D: pass.renderer.n64.cycle1.d = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::PrimitiveColor: pass.renderer.n64.primitiveColor = value; break;
  case AnimationProperty::EnvironmentColor: pass.renderer.n64.environmentColor = value; break;
  case AnimationProperty::N64TextureFormat: pass.renderer.n64.textureFormat = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64TextureFilter: pass.renderer.n64.textureFilter = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64MipmapMode: pass.renderer.n64.mipmapMode = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64TileSize:
    pass.renderer.n64.tileWidth = static_cast<int>(std::round(value.x));
    pass.renderer.n64.tileHeight = static_cast<int>(std::round(value.y)); break;
  case AnimationProperty::N64MirrorS: pass.renderer.n64.mirrorS = value.x >= 0.5f; break;
  case AnimationProperty::N64MirrorT: pass.renderer.n64.mirrorT = value.x >= 0.5f; break;
  case AnimationProperty::N64ShiftS: pass.renderer.n64.shiftS = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64ShiftT: pass.renderer.n64.shiftT = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64TextureGeneration: pass.renderer.n64.textureGeneration = value.x >= 0.5f; break;
  case AnimationProperty::N64SurfaceMode: pass.renderer.n64.surfaceMode = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64ZCompare: pass.renderer.n64.zCompare = value.x >= 0.5f; break;
  case AnimationProperty::N64ZUpdate: pass.renderer.n64.zUpdate = value.x >= 0.5f; break;
  case AnimationProperty::N64AlphaCompare: pass.renderer.n64.alphaCompare = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::AlphaThreshold: pass.renderer.n64.alphaThreshold = value.x; break;
  case AnimationProperty::N64CoverageAntialiasing: pass.renderer.n64.coverageAntialiasing = value.x >= 0.5f; break;
  case AnimationProperty::N64FramebufferFormat: pass.renderer.n64.framebufferFormat = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64ColorDither: pass.renderer.n64.colorDither = static_cast<int>(std::round(value.x)); break;
  case AnimationProperty::N64ViReconstruction: pass.renderer.n64.viReconstruction = value.x >= 0.5f; break;
  case AnimationProperty::N64ViDivot: pass.renderer.n64.viDivot = value.x >= 0.5f; break;
  case AnimationProperty::Count: break;
  }
}

PropertyAnimationTrack* findPropertyTrack(RenderPass& pass, const AnimationProperty property) {
  const auto track = std::find_if(pass.animation.tracks.begin(), pass.animation.tracks.end(),
    [property](const PropertyAnimationTrack& candidate) { return candidate.property == property; });
  return track == pass.animation.tracks.end() ? nullptr : &*track;
}

const PropertyAnimationTrack* findPropertyTrack(const RenderPass& pass, const AnimationProperty property) {
  const auto track = std::find_if(pass.animation.tracks.begin(), pass.animation.tracks.end(),
    [property](const PropertyAnimationTrack& candidate) { return candidate.property == property; });
  return track == pass.animation.tracks.end() ? nullptr : &*track;
}

glm::vec4 samplePropertyTrack(const PropertyAnimationTrack& track, const float timeSeconds) {
  if (track.keyframes.empty()) return glm::vec4(0.0f);
  if (track.keyframes.size() == 1 || timeSeconds <= track.keyframes.front().timeSeconds)
    return track.keyframes.front().value;
  if (timeSeconds >= track.keyframes.back().timeSeconds) return track.keyframes.back().value;
  const auto upper = std::upper_bound(track.keyframes.begin(), track.keyframes.end(), timeSeconds,
    [](const float time, const PropertyKeyframe& keyframe) { return time < keyframe.timeSeconds; });
  const PropertyKeyframe& b = *upper;
  const PropertyKeyframe& a = *(upper - 1);
  float amount = (timeSeconds - a.timeSeconds) / std::max(b.timeSeconds - a.timeSeconds, 0.00001f);
  if (animationPropertyInfo(track.property).behavior == AnimationBehavior::Step ||
      track.interpolation == KeyframeInterpolation::Step) amount = 0.0f;
  else if (track.interpolation == KeyframeInterpolation::SmoothStep)
    amount = amount * amount * (3.0f - 2.0f * amount);
  return glm::mix(a.value, b.value, amount);
}

void setPropertyKeyframe(RenderPass& pass, const AnimationProperty property, const float timeSeconds,
    const glm::vec4* explicitValue) {
  if (!animationPropertyIsAnimatable(property)) return;
  PropertyAnimationTrack* track = findPropertyTrack(pass, property);
  if (track == nullptr) {
    const KeyframeInterpolation interpolation = animationPropertyInfo(property).behavior == AnimationBehavior::Step
      ? KeyframeInterpolation::Step : KeyframeInterpolation::Linear;
    pass.animation.tracks.push_back({property, interpolation, {}});
    track = &pass.animation.tracks.back();
  }
  const glm::vec4 value = explicitValue == nullptr ? animationPropertyValue(pass, property) : *explicitValue;
  const std::size_t nearby = nearbyIndex(track->keyframes, timeSeconds, 1.0f / 120.0f);
  if (nearby == std::numeric_limits<std::size_t>::max()) track->keyframes.push_back({timeSeconds, value});
  else track->keyframes[nearby] = {timeSeconds, value};
  std::sort(track->keyframes.begin(), track->keyframes.end(),
    [](const PropertyKeyframe& a, const PropertyKeyframe& b) { return a.timeSeconds < b.timeSeconds; });
}

bool removePropertyKeyframe(RenderPass& pass, const AnimationProperty property, const float timeSeconds,
    const float toleranceSeconds) {
  PropertyAnimationTrack* track = findPropertyTrack(pass, property);
  if (track == nullptr) return false;
  const std::size_t nearby = nearbyIndex(track->keyframes, timeSeconds, toleranceSeconds);
  if (nearby == std::numeric_limits<std::size_t>::max()) return false;
  track->keyframes.erase(track->keyframes.begin() + static_cast<std::ptrdiff_t>(nearby));
  if (track->keyframes.empty()) {
    pass.animation.tracks.erase(std::remove_if(pass.animation.tracks.begin(), pass.animation.tracks.end(),
      [property](const PropertyAnimationTrack& candidate) { return candidate.property == property; }),
      pass.animation.tracks.end());
  }
  return true;
}

std::size_t propertyKeyframeIndexNear(const RenderPass& pass, const AnimationProperty property,
    const float timeSeconds, const float toleranceSeconds) {
  const PropertyAnimationTrack* track = findPropertyTrack(pass, property);
  return track == nullptr ? std::numeric_limits<std::size_t>::max()
    : nearbyIndex(track->keyframes, timeSeconds, toleranceSeconds);
}

bool propertyHasKeyAt(const RenderPass& pass, const AnimationProperty property, const float timeSeconds,
    const float toleranceSeconds) {
  return propertyKeyframeIndexNear(pass, property, timeSeconds, toleranceSeconds) !=
    std::numeric_limits<std::size_t>::max();
}

RenderPass evaluateRenderPass(const RenderPass& source, const float timeSeconds) {
  RenderPass evaluated = source;
  if (!source.animation.enabled) return evaluated;
  for (const PropertyAnimationTrack& track : source.animation.tracks) {
    if (!track.keyframes.empty()) setAnimationPropertyValue(evaluated, track.property, samplePropertyTrack(track, timeSeconds));
  }
  return evaluated;
}

} // namespace gfxlab
