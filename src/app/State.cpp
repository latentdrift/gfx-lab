#include "app/State.hpp"

#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

namespace gfxlab {

glm::vec3 CameraOrbit::eye() const {
  const float cp = std::cos(pitch);
  return target + distance * glm::vec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
}

glm::mat4 CameraOrbit::view() const {
  return glm::lookAt(eye(), target, glm::vec3(0, 1, 0));
}

const char* testSceneName(TestScene scene) {
  switch (scene) {
    case TestScene::Torus: return "torus";
    case TestScene::TexturePlane: return "texture_minification";
    case TestScene::DepthPrecision: return "depth_precision";
    case TestScene::Transparency: return "transparency";
    case TestScene::Lighting: return "lighting_comparison";
    case TestScene::StencilMask: return "stencil_mask";
    case TestScene::FieldInterference: return "field_interference";
    case TestScene::ImportedModel: return "imported_model";
  }
  return "unknown";
}

void applyRecommendedSetup(TestScene scene, RendererState& state, CameraOrbit& camera) {
  state = RendererState{};
  camera = CameraOrbit{};

  switch (scene) {
    case TestScene::Torus:
      // Conventional neutral rendering: this is the general-purpose baseline.
      break;
    case TestScene::TexturePlane:
      state.texture.mipmapping = true;
      state.texture.trilinear = true;
      state.texture.anisotropy = 8.0f;
      camera.yaw = 0.18f;
      camera.pitch = 0.30f;
      camera.distance = 8.5f;
      camera.target = glm::vec3(0.0f, -1.25f, -3.5f);
      break;
    case TestScene::DepthPrecision:
      state.camera.nearPlane = 0.01f;
      state.depth.precision = 16;
      state.depth.testing = true;
      state.depth.writing = true;
      state.depth.function = 0;
      state.rasterization.polygonOffset = false;
      camera.yaw = 0.62f;
      camera.pitch = 0.58f;
      camera.distance = 4.6f;
      break;
    case TestScene::Transparency:
      state.rasterization.cullMode = 0;
      state.surface.transparency = 2;
      state.surface.reverseDrawOrder = false;
      state.depth.testing = true;
      state.depth.writing = false;
      state.lighting.model = 0;
      camera.yaw = 0.25f;
      camera.pitch = 0.18f;
      camera.distance = 5.0f;
      break;
    case TestScene::Lighting:
      state.texture.mipmapping = true;
      state.texture.trilinear = true;
      state.lighting.model = 4;
      state.lighting.shininess = 48.0f;
      state.lighting.shadows = true;
      state.lighting.shadowPcf = true;
      state.color.linearLight = true;
      camera.yaw = 0.66f;
      camera.pitch = 0.34f;
      camera.distance = 7.2f;
      camera.target = glm::vec3(0.0f, 0.0f, -0.4f);
      break;
    case TestScene::StencilMask:
      state.stencil.enabled = true;
      state.stencil.invert = false;
      state.stencil.reference = 1;
      state.rasterization.cullMode = 0;
      state.lighting.model = 0;
      camera.yaw = 0.0f;
      camera.pitch = 0.0f;
      camera.distance = 5.0f;
      break;
    case TestScene::FieldInterference:
      state.field.enabled = true;
      state.lighting.model = 0;
      state.texture.nearestFiltering = false;
      state.output.width = 640;
      state.output.height = 480;
      camera.yaw = 0.72f;
      camera.pitch = 0.56f;
      camera.distance = 7.4f;
      camera.target = glm::vec3(0.0f, -0.35f, 0.0f);
      break;
    case TestScene::ImportedModel:
      camera.distance = 5.2f;
      break;
  }
}

void applyHandbookExample(handbook::Example example, bool alternative, RendererState& state,
    CameraOrbit& camera, TestScene& scene, Category& category) {
  switch (example) {
    case handbook::Example::None:
      return;
    case handbook::Example::VertexQuantization:
      scene = TestScene::Torus; category = Category::Geometry;
      applyRecommendedSetup(scene, state, camera);
      state.geometry.vertexQuantization = alternative ? 1.0f / 8.0f : 0.0f;
      break;
    case handbook::Example::Projection:
      scene = TestScene::Torus; category = Category::Camera;
      applyRecommendedSetup(scene, state, camera);
      state.camera.orthographic = alternative;
      break;
    case handbook::Example::AffineMapping:
      scene = TestScene::Torus; category = Category::Rasterization;
      applyRecommendedSetup(scene, state, camera);
      state.rasterization.affineMapping = alternative;
      break;
    case handbook::Example::TextureMinification:
      scene = TestScene::TexturePlane; category = Category::Texture;
      applyRecommendedSetup(scene, state, camera);
      if (!alternative) {
        state.texture.mipmapping = false;
        state.texture.trilinear = false;
        state.texture.anisotropy = 1.0f;
      }
      break;
    case handbook::Example::NormalMapping:
      scene = TestScene::Torus; category = Category::Surface;
      applyRecommendedSetup(scene, state, camera);
      state.lighting.model = 4;
      state.surface.normalMapping = alternative;
      state.surface.normalStrength = alternative ? 1.5f : 1.0f;
      break;
    case handbook::Example::LightingInterpolation:
      scene = TestScene::Lighting; category = Category::Lighting;
      applyRecommendedSetup(scene, state, camera);
      state.lighting.shadows = false;
      state.lighting.model = alternative ? 4 : 1;
      break;
    case handbook::Example::DepthPrecision:
      scene = TestScene::DepthPrecision; category = Category::Depth;
      applyRecommendedSetup(scene, state, camera);
      state.camera.nearPlane = alternative ? 0.01f : 0.1f;
      state.depth.precision = alternative ? 16 : 24;
      break;
    case handbook::Example::Transparency:
      scene = TestScene::Transparency; category = Category::Surface;
      applyRecommendedSetup(scene, state, camera);
      if (!alternative) {
        state.surface.transparency = 0;
        state.depth.writing = true;
      }
      break;
    case handbook::Example::Stencil:
      scene = TestScene::StencilMask; category = Category::Stencil;
      applyRecommendedSetup(scene, state, camera);
      state.stencil.enabled = alternative;
      break;
    case handbook::Example::LinearLight:
      scene = TestScene::Lighting; category = Category::Color;
      applyRecommendedSetup(scene, state, camera);
      state.lighting.shadows = false;
      state.color.linearLight = alternative;
      break;
    case handbook::Example::ColorQuantization:
      scene = TestScene::Torus; category = Category::Color;
      applyRecommendedSetup(scene, state, camera);
      state.color.bitsPerChannel = alternative ? 5 : 8;
      state.color.dithering = alternative;
      break;
    case handbook::Example::InternalResolution:
      scene = TestScene::Torus; category = Category::Output;
      applyRecommendedSetup(scene, state, camera);
      state.output.width = alternative ? 160 : 640;
      state.output.height = alternative ? 120 : 480;
      state.output.nearestUpscaling = alternative;
      break;
    case handbook::Example::ShadowMapping:
      scene = TestScene::Lighting; category = Category::Lighting;
      applyRecommendedSetup(scene, state, camera);
      state.lighting.shadows = alternative;
      break;
    case handbook::Example::Overdraw:
      scene = TestScene::Transparency; category = Category::Post;
      applyRecommendedSetup(scene, state, camera);
      state.post.overdraw = alternative;
      break;
    case handbook::Example::ClutTextures:
      scene = TestScene::Torus; category = Category::Texture;
      applyRecommendedSetup(scene, state, camera);
      state.texture.nearestFiltering = true;
      state.texture.colorMode = alternative ? 2 : 0;
      break;
    case handbook::Example::VertexDepthCue:
      scene = TestScene::TexturePlane; category = Category::Lighting;
      applyRecommendedSetup(scene, state, camera);
      state.lighting.model = 1;
      state.lighting.depthCue = alternative;
      state.lighting.depthCueStart = 4.0f;
      state.lighting.depthCueEnd = 12.0f;
      state.lighting.farColor = glm::vec3(0.12f, 0.16f, 0.22f);
      break;
    case handbook::Example::Ps1Semitransparency:
      scene = TestScene::Transparency; category = Category::Surface;
      applyRecommendedSetup(scene, state, camera);
      state.surface.transparency = alternative ? 6 : 0;
      state.depth.writing = !alternative;
      state.color.linearLight = false;
      break;
    case handbook::Example::OrderingTable:
      scene = TestScene::Transparency; category = Category::Depth;
      applyRecommendedSetup(scene, state, camera);
      state.surface.transparency = 6;
      state.depth.writing = false;
      state.surface.reverseDrawOrder = !alternative;
      state.depth.orderingTable = alternative;
      state.depth.orderingBuckets = 32;
      state.color.linearLight = false;
      break;
    case handbook::Example::N64ThreePoint:
      scene = TestScene::Torus; category = Category::Texture;
      applyRecommendedSetup(scene, state, camera);
      state.n64.enabled = true;
      state.n64.textureFormat = 1;
      state.n64.textureFilter = alternative ? 1 : 0;
      state.color.linearLight = false;
      break;
    case handbook::Example::N64Combiner:
      scene = TestScene::Torus; category = Category::Surface;
      applyRecommendedSetup(scene, state, camera);
      state.n64.enabled = true;
      state.n64.cycleType = 1;
      state.n64.cycle0 = alternative ? RendererState::CombinerCycle{1, 0, 3, 0}
        : RendererState::CombinerCycle{1, 0, 2, 0};
      state.lighting.model = 1;
      state.color.linearLight = false;
      break;
    case handbook::Example::N64TextureFormats:
      scene = TestScene::Torus; category = Category::Texture;
      applyRecommendedSetup(scene, state, camera);
      state.n64.enabled = true;
      state.n64.textureFilter = 0;
      state.n64.textureFormat = alternative ? 2 : 1;
      state.color.linearLight = false;
      break;
    case handbook::Example::N64Mipmap:
      scene = TestScene::TexturePlane; category = Category::Texture;
      applyRecommendedSetup(scene, state, camera);
      state.n64.enabled = true;
      state.n64.textureFilter = 1;
      state.n64.mipmapMode = alternative ? 2 : 0;
      state.n64.cycleType = alternative ? 2 : 1;
      state.n64.tileWidth = 32;
      state.n64.tileHeight = 32;
      state.color.linearLight = false;
      break;
    case handbook::Example::N64Coverage:
      scene = TestScene::Torus; category = Category::Rasterization;
      applyRecommendedSetup(scene, state, camera);
      state.n64.enabled = true;
      state.n64.coverageAntialiasing = alternative;
      state.rasterization.samples = alternative ? 4 : 1;
      state.color.linearLight = false;
      break;
    case handbook::Example::N64VideoInterface:
      scene = TestScene::Torus; category = Category::Output;
      applyRecommendedSetup(scene, state, camera);
      state.n64.enabled = true;
      state.n64.viReconstruction = alternative;
      state.n64.viDivot = alternative;
      state.output.width = 320;
      state.output.height = 240;
      state.color.linearLight = false;
      break;
  }
}

std::string configJson(const RendererState& state, const CameraOrbit& camera, TestScene scene,
    HardwareProfile profile) {
  const char* cullModes[] = {"none", "back", "front"};
  const char* visualizations[] = {"texture", "uv_coordinates", "normals", "vertex_colors", "tangents", "bitangents"};
  const char* transparencyModes[] = {"opaque", "alpha_test", "straight_alpha", "premultiplied_alpha", "additive", "multiply",
    "ps1_average_background_half_plus_foreground_half", "ps1_additive_background_plus_foreground",
    "ps1_subtractive_background_minus_foreground", "ps1_quarter_add_background_plus_foreground_quarter"};
  const char* textureColorModes[] = {"direct_color", "indexed_8bit_clut_256", "indexed_4bit_clut_16"};
  const char* lightingModels[] = {"unlit", "gouraud_lambert", "phong_shaded_lambert", "phong_reflection", "blinn_phong_reflection"};
  const char* depthFunctions[] = {"less", "less_or_equal", "greater", "always"};
  const char* depthViews[] = {"off", "raw_window_space", "linear_camera_space_0_to_10_units"};
  const char* minificationFilter = state.texture.nearestFiltering ? "nearest" : "bilinear";
  if (state.texture.mipmapping && state.texture.nearestFiltering) minificationFilter = "nearest_mipmap_nearest";
  if (state.texture.mipmapping && !state.texture.nearestFiltering)
    minificationFilter = state.texture.trilinear ? "linear_mipmap_linear" : "linear_mipmap_nearest";
  auto boolean = [](bool value) { return value ? "true" : "false"; };
  std::ostringstream json;
  json << std::boolalpha << std::fixed << std::setprecision(5);
  json << "{\n";
  json << "  \"schema\": \"graphics-lab.renderer-state.v1\",\n";
  json << "  \"hardware_target\": \"" << hardwareProfileId(profile) << "\",\n";
  json << "  \"test_scene\": \"" << testSceneName(scene) << "\",\n";
  json << "  \"view\": {\n";
  json << "    \"orbit_yaw_radians\": " << camera.yaw << ",\n";
  json << "    \"orbit_pitch_radians\": " << camera.pitch << ",\n";
  json << "    \"distance_units\": " << camera.distance << ",\n";
  json << "    \"target\": [" << camera.target.x << ", " << camera.target.y << ", " << camera.target.z << "]\n";
  json << "  },\n";
  json << "  \"geometry\": {\n";
  json << "    \"vertex_position_quantization_step_units\": " << state.geometry.vertexQuantization << ",\n";
  json << "    \"world_space_clipping_plane\": {\"enabled\": " << boolean(state.geometry.clipping)
       << ", \"axis\": \"y\", \"height_units\": " << state.geometry.clipHeight
       << ", \"keep\": \"" << (state.geometry.clipAbove ? "below" : "above") << "\"}\n";
  json << "  },\n";
  json << "  \"camera\": {\n";
  json << "    \"projection\": \"" << (state.camera.orthographic ? "orthographic" : "perspective") << "\",\n";
  json << "    \"vertical_field_of_view_degrees\": " << state.camera.fieldOfView << ",\n";
  json << "    \"orthographic_view_height_units\": " << state.camera.orthographicSize << ",\n";
  json << "    \"near_plane_units\": " << state.camera.nearPlane << ",\n";
  json << "    \"far_plane_units\": 100.00000\n";
  json << "  },\n";
  json << "  \"rasterization\": {\n";
  json << "    \"texture_coordinate_interpolation\": \"" << (state.rasterization.affineMapping ? "affine" : "perspective_correct") << "\",\n";
  json << "    \"face_culling\": \"" << cullModes[std::clamp(state.rasterization.cullMode, 0, 2)] << "\",\n";
  json << "    \"multisample_count\": " << state.rasterization.samples << ",\n";
  json << "    \"polygon_offset\": {\"enabled\": " << boolean(state.rasterization.polygonOffset)
       << ", \"factor\": " << state.rasterization.polygonOffsetFactor << ", \"units\": " << state.rasterization.polygonOffsetUnits << "}\n";
  json << "  },\n";
  json << "  \"surface\": {\n";
  json << "    \"visualization\": \"" << visualizations[std::clamp(state.surface.visualization, 0, 5)] << "\",\n";
  json << "    \"normal_interpolation\": \"" << (state.surface.smoothShading ? "smooth" : "flat") << "\",\n";
  json << "    \"wireframe_overlay\": " << boolean(state.surface.wireframe) << ",\n";
  json << "    \"normal_mapping\": {\"enabled\": " << boolean(state.surface.normalMapping)
       << ", \"space\": \"tangent\", \"strength\": " << state.surface.normalStrength << "},\n";
  json << "    \"transparency\": {\"operation\": \"" << transparencyModes[std::clamp(state.surface.transparency, 0, 9)]
       << "\", \"alpha_cutoff\": " << state.surface.alphaCutoff
       << ", \"reverse_object_draw_order\": " << boolean(state.surface.reverseDrawOrder) << "}\n";
  json << "  },\n";
  json << "  \"texture\": {\n";
  json << "    \"magnification_filter\": \"" << (state.texture.nearestFiltering ? "nearest" : "bilinear") << "\",\n";
  json << "    \"minification_filter\": \"" << minificationFilter << "\",\n";
  json << "    \"address_mode\": \"" << (state.texture.repeat ? "repeat" : "clamp_to_edge") << "\",\n";
  json << "    \"mipmapping\": " << boolean(state.texture.mipmapping) << ",\n";
  json << "    \"mip_level_interpolation\": \"" << (state.texture.trilinear ? "linear" : "nearest") << "\",\n";
  json << "    \"anisotropy\": " << state.texture.anisotropy << ",\n";
  json << "    \"color_storage\": \"" << textureColorModes[std::clamp(state.texture.colorMode, 0, 2)] << "\"\n";
  json << "  },\n";
  json << "  \"lighting\": {\n";
  json << "    \"model\": \"" << lightingModels[std::clamp(state.lighting.model, 0, 4)] << "\",\n";
  json << "    \"ambient_term\": " << state.lighting.ambient << ",\n";
  json << "    \"direction_degrees\": {\"azimuth\": " << state.lighting.azimuth << ", \"elevation\": " << state.lighting.elevation << "},\n";
  json << "    \"specular_exponent\": " << state.lighting.shininess << ",\n";
  json << "    \"shadow_map\": {\"enabled\": " << boolean(state.lighting.shadows)
       << ", \"resolution\": " << state.lighting.shadowResolution << ", \"depth_bias\": " << state.lighting.shadowBias
       << ", \"filter\": \"" << (state.lighting.shadowPcf ? "pcf_3x3" : "nearest")
       << "\", \"visualize_light_depth\": " << boolean(state.lighting.visualizeShadowMap) << "},\n";
  json << "    \"vertex_depth_cue\": {\"enabled\": " << boolean(state.lighting.depthCue)
       << ", \"start_units\": " << state.lighting.depthCueStart << ", \"end_units\": " << state.lighting.depthCueEnd
       << ", \"far_color_rgb\": [" << state.lighting.farColor.r << ", " << state.lighting.farColor.g << ", " << state.lighting.farColor.b << "]}\n";
  json << "  },\n";
  constexpr const char* fieldViews[] = {"source_a_phase", "source_b_phase", "phase_difference",
    "interference_intensity", "absolute_distance_difference", "distance_difference_contours"};
  json << "  \"field\": {\n";
  json << "    \"enabled\": " << boolean(state.field.enabled) << ",\n";
  json << "    \"source_a_position_units\": [" << state.field.sourceA.x << ", " << state.field.sourceA.y << ", " << state.field.sourceA.z << "],\n";
  json << "    \"source_b_position_units\": [" << state.field.sourceB.x << ", " << state.field.sourceB.y << ", " << state.field.sourceB.z << "],\n";
  json << "    \"wavelength_units\": " << state.field.wavelength << ",\n";
  json << "    \"relative_phase_radians\": " << state.field.phaseOffset << ",\n";
  json << "    \"amplitudes\": [" << state.field.amplitudeA << ", " << state.field.amplitudeB << "],\n";
  json << "    \"distance_falloff\": " << state.field.falloff << ",\n";
  json << "    \"band_sharpness\": " << state.field.bandSharpness << ",\n";
  json << "    \"visualization\": \"" << fieldViews[std::clamp(state.field.visualization, 0, 5)] << "\",\n";
  json << "    \"low_color_rgb\": [" << state.field.lowColor.r << ", " << state.field.lowColor.g << ", " << state.field.lowColor.b << "],\n";
  json << "    \"high_color_rgb\": [" << state.field.highColor.r << ", " << state.field.highColor.g << ", " << state.field.highColor.b << "]\n";
  json << "  },\n";
  json << "  \"depth\": {\n";
  json << "    \"test_enabled\": " << boolean(state.depth.testing) << ",\n";
  json << "    \"write_enabled\": " << boolean(state.depth.writing) << ",\n";
  json << "    \"comparison\": \"" << depthFunctions[std::clamp(state.depth.function, 0, 3)] << "\",\n";
  json << "    \"buffer_precision_bits\": " << state.depth.precision << ",\n";
  json << "    \"visualization\": \"" << depthViews[std::clamp(state.depth.visualization, 0, 2)] << "\",\n";
  json << "    \"visibility_submission\": {\"method\": \"" << (state.depth.orderingTable ? "ordering_table" : "depth_buffer")
       << "\", \"granularity\": \"object\", \"depth_buckets\": " << state.depth.orderingBuckets << "}\n";
  json << "  },\n";
  json << "  \"stencil\": {\n";
  json << "    \"two_pass_mask_enabled\": " << boolean(state.stencil.enabled) << ",\n";
  json << "    \"reference\": " << state.stencil.reference << ",\n";
  json << "    \"comparison\": \"" << (state.stencil.invert ? "not_equal" : "equal") << "\",\n";
  json << "    \"write_pass\": {\"comparison\": \"always\", \"pass_operation\": \"replace\"}\n";
  json << "  },\n";
  json << "  \"color\": {\n";
  json << "    \"bits_per_channel\": " << state.color.bitsPerChannel << ",\n";
  json << "    \"ordered_dithering\": " << boolean(state.color.dithering) << ",\n";
  json << "    \"dithering_matrix\": \"bayer_4x4\",\n";
  json << "    \"lighting_color_space\": \"" << (state.color.linearLight ? "linear_light" : "encoded_rgb") << "\"\n";
  json << "  },\n";
  json << "  \"post\": {\n";
  json << "    \"linear_distance_fog\": {\"enabled\": " << boolean(state.post.fog)
       << ", \"start_units\": " << state.post.fogStart << ", \"end_units\": " << state.post.fogEnd << "},\n";
  json << "    \"overdraw_visualization\": {\"enabled\": " << boolean(state.post.overdraw)
       << ", \"heat_map_maximum_fragments\": " << state.post.overdrawRange << "}\n";
  json << "  },\n";
  json << "  \"output\": {\n";
  json << "    \"internal_resolution\": [" << state.output.width << ", " << state.output.height << "],\n";
  json << "    \"viewport_upscaling_filter\": \"" << (state.output.nearestUpscaling ? "nearest" : "bilinear") << "\",\n";
  json << "    \"presentation_aspect_ratio\": \"4:3\"\n";
  json << "  }";
  if (state.n64.enabled) {
    const char* combinerSources[] = {"zero", "texel0", "one", "shade", "primitive", "environment", "texel1", "combined", "lod_fraction"};
    const char* textureFormats[] = {"rgba16", "rgba32", "ci4_rgba16_tlut", "ci8_rgba16_tlut", "ia4", "ia8", "ia16", "i4", "i8"};
    const char* textureFilters[] = {"point", "n64_three_point", "box_average"};
    const char* mipmapModes[] = {"disabled", "nearest_level", "trilinear", "sharpen", "detail"};
    const char* surfaceModes[] = {"opaque", "translucent", "decal", "interpenetrating"};
    const char* alphaModes[] = {"off", "threshold", "dither"};
    const char* framebufferFormats[] = {"rgba5551_with_coverage", "rgba8888"};
    const char* ditherModes[] = {"disabled", "magic_square_4x4", "bayer_4x4", "noise"};
    constexpr int bitsPerTexel[] = {16, 32, 4, 8, 4, 8, 16, 4, 8};
    const int format = std::clamp(state.n64.textureFormat, 0, 8);
    const int tmemBytes = state.n64.tileWidth * state.n64.tileHeight * bitsPerTexel[format] / 8 +
      (format == 2 ? 128 : format == 3 ? 2048 : 0);
    auto cycle = [&](const RendererState::CombinerCycle& value) {
      std::ostringstream result;
      result << "{\"a\": \"" << combinerSources[std::clamp(value.a, 0, 8)]
             << "\", \"b\": \"" << combinerSources[std::clamp(value.b, 0, 8)]
             << "\", \"c\": \"" << combinerSources[std::clamp(value.c, 0, 8)]
             << "\", \"d\": \"" << combinerSources[std::clamp(value.d, 0, 8)] << "\"}";
      return result.str();
    };
    json << ",\n  \"n64_rdp\": {\n";
    json << "    \"cycle_type\": " << state.n64.cycleType << ",\n";
    json << "    \"combiner_equation\": \"(a - b) * c + d\",\n";
    json << "    \"combiner_cycle_0\": " << cycle(state.n64.cycle0) << ",\n";
    json << "    \"combiner_cycle_1\": " << cycle(state.n64.cycle1) << ",\n";
    json << "    \"primitive_color_rgba\": [" << state.n64.primitiveColor.r << ", " << state.n64.primitiveColor.g << ", " << state.n64.primitiveColor.b << ", " << state.n64.primitiveColor.a << "],\n";
    json << "    \"environment_color_rgba\": [" << state.n64.environmentColor.r << ", " << state.n64.environmentColor.g << ", " << state.n64.environmentColor.b << ", " << state.n64.environmentColor.a << "],\n";
    json << "    \"texture\": {\"format\": \"" << textureFormats[format] << "\", \"filter\": \""
         << textureFilters[std::clamp(state.n64.textureFilter, 0, 2)] << "\", \"mipmap_mode\": \""
         << mipmapModes[std::clamp(state.n64.mipmapMode, 0, 4)] << "\", \"tile_size\": ["
         << state.n64.tileWidth << ", " << state.n64.tileHeight << "], \"tmem_bytes\": " << tmemBytes
         << ", \"mirror_st\": [" << boolean(state.n64.mirrorS) << ", " << boolean(state.n64.mirrorT)
         << "], \"shift_st\": [" << state.n64.shiftS << ", " << state.n64.shiftT << "]},\n";
    json << "    \"surface_mode\": \"" << surfaceModes[std::clamp(state.n64.surfaceMode, 0, 3)] << "\",\n";
    json << "    \"z_compare\": " << boolean(state.n64.zCompare) << ", \"z_update\": " << boolean(state.n64.zUpdate) << ",\n";
    json << "    \"alpha_compare\": {\"mode\": \"" << alphaModes[std::clamp(state.n64.alphaCompare, 0, 2)]
         << "\", \"threshold\": " << state.n64.alphaThreshold << "},\n";
    json << "    \"texture_coordinate_generation\": " << boolean(state.n64.textureGeneration) << ",\n";
    json << "    \"coverage_antialiasing\": " << boolean(state.n64.coverageAntialiasing) << ",\n";
    json << "    \"framebuffer_format\": \"" << framebufferFormats[std::clamp(state.n64.framebufferFormat, 0, 1)] << "\",\n";
    json << "    \"color_dither\": \"" << ditherModes[std::clamp(state.n64.colorDither, 0, 3)] << "\",\n";
    json << "    \"vi_reconstruction_filter\": " << boolean(state.n64.viReconstruction)
         << ", \"vi_divot_filter\": " << boolean(state.n64.viDivot) << "\n";
    json << "  }";
  }
  json << "\n";
  json << "}\n";
  return json.str();
}

std::string relationConfigJson(const RendererState& a, const RendererState& b, const CameraOrbit& camera,
    TestScene scene, HardwareProfile profile, RelationOperator operation, float gain, float bias) {
  constexpr const char* operationIds[] = {
    "absolute_difference", "signed_a_minus_b", "positive_a_minus_b", "positive_b_minus_a", "multiply", "screen",
    "exclusion", "minimum", "maximum", "a_times_one_minus_b", "centered_sum", "relative_a_over_b",
    "add", "average", "subtract", "reverse_subtract", "quarter_add_b", "signed_color_offset", "bitwise_xor"
  };
  constexpr const char* equations[] = {
    "abs(a - b)", "a - b", "max(a - b, 0)", "max(b - a, 0)", "a * b", "1 - (1 - a) * (1 - b)",
    "a + b - 2 * a * b", "min(a, b)", "max(a, b)", "a * (1 - b)", "a + b - 1",
    "a / max(b, 1/255) - 1", "a + b", "(a + b) / 2", "a - b", "b - a", "a + b / 4", "a + b - 1/2",
    "quantize(a) XOR quantize(b)"
  };
  const int index = std::clamp(static_cast<int>(operation), 0, 18);
  const auto nested = [](std::string value) {
    if (!value.empty() && value.back() == '\n') value.pop_back();
    std::string result;
    result.reserve(value.size() + 128);
    for (const char character : value) {
      result.push_back(character);
      if (character == '\n') result += "  ";
    }
    return result;
  };
  std::ostringstream json;
  json << std::fixed << std::setprecision(5);
  json << "{\n";
  json << "  \"schema\": \"graphics-lab.render-algebra.v1\",\n";
  json << "  \"test_scene\": \"" << testSceneName(scene) << "\",\n";
  json << "  \"relation\": {\"operation\": \"" << operationIds[index] << "\", \"equation_per_rgb_channel\": \""
       << equations[index] << "\", \"gain\": " << gain << ", \"bias\": " << bias
       << ", \"output_clamp\": [0.00000, 1.00000]},\n";
  json << "  \"renderer_a\": " << nested(configJson(a, camera, scene, profile)) << ",\n";
  json << "  \"renderer_b\": " << nested(configJson(b, camera, scene, profile)) << "\n";
  json << "}\n";
  return json.str();
}

} // namespace gfxlab
