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
  }
}

std::string configJson(const RendererState& state, const CameraOrbit& camera, TestScene scene) {
  const char* cullModes[] = {"none", "back", "front"};
  const char* visualizations[] = {"texture", "uv_coordinates", "normals", "vertex_colors", "tangents", "bitangents"};
  const char* transparencyModes[] = {"opaque", "alpha_test", "straight_alpha", "premultiplied_alpha", "additive", "multiply"};
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
  json << "    \"transparency\": {\"operation\": \"" << transparencyModes[std::clamp(state.surface.transparency, 0, 5)]
       << "\", \"alpha_cutoff\": " << state.surface.alphaCutoff
       << ", \"reverse_object_draw_order\": " << boolean(state.surface.reverseDrawOrder) << "}\n";
  json << "  },\n";
  json << "  \"texture\": {\n";
  json << "    \"magnification_filter\": \"" << (state.texture.nearestFiltering ? "nearest" : "bilinear") << "\",\n";
  json << "    \"minification_filter\": \"" << minificationFilter << "\",\n";
  json << "    \"address_mode\": \"" << (state.texture.repeat ? "repeat" : "clamp_to_edge") << "\",\n";
  json << "    \"mipmapping\": " << boolean(state.texture.mipmapping) << ",\n";
  json << "    \"mip_level_interpolation\": \"" << (state.texture.trilinear ? "linear" : "nearest") << "\",\n";
  json << "    \"anisotropy\": " << state.texture.anisotropy << "\n";
  json << "  },\n";
  json << "  \"lighting\": {\n";
  json << "    \"model\": \"" << lightingModels[std::clamp(state.lighting.model, 0, 4)] << "\",\n";
  json << "    \"ambient_term\": " << state.lighting.ambient << ",\n";
  json << "    \"direction_degrees\": {\"azimuth\": " << state.lighting.azimuth << ", \"elevation\": " << state.lighting.elevation << "},\n";
  json << "    \"specular_exponent\": " << state.lighting.shininess << ",\n";
  json << "    \"shadow_map\": {\"enabled\": " << boolean(state.lighting.shadows)
       << ", \"resolution\": " << state.lighting.shadowResolution << ", \"depth_bias\": " << state.lighting.shadowBias
       << ", \"filter\": \"" << (state.lighting.shadowPcf ? "pcf_3x3" : "nearest")
       << "\", \"visualize_light_depth\": " << boolean(state.lighting.visualizeShadowMap) << "}\n";
  json << "  },\n";
  json << "  \"depth\": {\n";
  json << "    \"test_enabled\": " << boolean(state.depth.testing) << ",\n";
  json << "    \"write_enabled\": " << boolean(state.depth.writing) << ",\n";
  json << "    \"comparison\": \"" << depthFunctions[std::clamp(state.depth.function, 0, 3)] << "\",\n";
  json << "    \"buffer_precision_bits\": " << state.depth.precision << ",\n";
  json << "    \"visualization\": \"" << depthViews[std::clamp(state.depth.visualization, 0, 2)] << "\"\n";
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
  json << "  }\n";
  json << "}\n";
  return json.str();
}

} // namespace gfxlab
