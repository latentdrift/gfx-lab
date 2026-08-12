#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>

namespace {

struct RendererState {
  struct Geometry { float vertexQuantization = 0.0f; bool clipping = false; float clipHeight = 0.0f; bool clipAbove = false; } geometry;
  struct Camera { float fieldOfView = 45.0f; float nearPlane = 0.05f; bool orthographic = false; float orthographicSize = 4.0f; } camera;
  struct Rasterization { bool affineMapping = false; int cullMode = 1; int samples = 1; bool polygonOffset = false; float polygonOffsetFactor = 1.0f; float polygonOffsetUnits = 1.0f; } rasterization;
  struct Surface { bool smoothShading = true; bool wireframe = false; int visualization = 0; bool normalMapping = false; float normalStrength = 1.0f; int transparency = 0; float alphaCutoff = 0.5f; bool reverseDrawOrder = false; } surface;
  struct Texture { bool nearestFiltering = false; bool repeat = true; bool mipmapping = false; bool trilinear = false; float anisotropy = 1.0f; } texture;
  struct Lighting { int model = 2; float ambient = 0.22f; float azimuth = 34.0f; float elevation = 52.0f; float shininess = 32.0f; bool shadows = false; int shadowResolution = 1024; float shadowBias = 0.002f; bool shadowPcf = true; bool visualizeShadowMap = false; } lighting;
  struct Depth { bool testing = true; bool writing = true; int precision = 24; int function = 0; int visualization = 0; } depth;
  struct Stencil { bool enabled = false; bool invert = false; int reference = 1; } stencil;
  struct Color { int bitsPerChannel = 8; bool dithering = false; bool linearLight = true; } color;
  struct Post { bool fog = false; float fogStart = 3.0f; float fogEnd = 7.0f; bool overdraw = false; float overdrawRange = 8.0f; } post;
  struct Output { int width = 640; int height = 480; bool nearestUpscaling = true; } output;
};

enum class Category { Geometry, Camera, Rasterization, Surface, Texture, Lighting, Depth, Stencil, Color, Post, Output };
enum class CompareMode { A, B, Split };
enum class TestScene { Torus, TexturePlane, DepthPrecision, Transparency, Lighting, StencilMask };

struct CameraOrbit {
  float yaw = 0.72f;
  float pitch = 0.36f;
  float distance = 5.2f;
  glm::vec3 target{0.0f};

  glm::vec3 eye() const {
    const float cp = std::cos(pitch);
    return target + distance * glm::vec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
  }

  glm::mat4 view() const { return glm::lookAt(eye(), target, glm::vec3(0, 1, 0)); }
};

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

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec3 barycentric;
  glm::vec3 color;
  glm::vec4 tangent;
};

struct MeshRange {
  GLint first = 0;
  GLsizei count = 0;
};

void appendTriangle(std::vector<Vertex>& vertices, Vertex a, Vertex b, Vertex c) {
  a.barycentric = {1, 0, 0};
  b.barycentric = {0, 1, 0};
  c.barycentric = {0, 0, 1};
  vertices.push_back(a);
  vertices.push_back(b);
  vertices.push_back(c);
}

std::vector<Vertex> makePlane(float width, float depth, int xSegments, int zSegments) {
  std::vector<Vertex> vertices;
  for (int z = 0; z < zSegments; ++z) {
    for (int x = 0; x < xSegments; ++x) {
      const float x0 = (static_cast<float>(x) / xSegments - 0.5f) * width;
      const float x1 = (static_cast<float>(x + 1) / xSegments - 0.5f) * width;
      const float z0 = (static_cast<float>(z) / zSegments - 0.5f) * depth;
      const float z1 = (static_cast<float>(z + 1) / zSegments - 0.5f) * depth;
      const float u0 = static_cast<float>(x) / xSegments * 8.0f;
      const float u1 = static_cast<float>(x + 1) / xSegments * 8.0f;
      const float v0 = static_cast<float>(z) / zSegments * 16.0f;
      const float v1 = static_cast<float>(z + 1) / zSegments * 16.0f;
      const glm::vec3 n(0, 1, 0), color(0.65f, 0.72f, 0.78f);
      const glm::vec4 tangent(1, 0, 0, -1);
      Vertex a{{x0, 0, z0}, n, {u0, v0}, {}, color, tangent};
      Vertex b{{x0, 0, z1}, n, {u0, v1}, {}, color, tangent};
      Vertex c{{x1, 0, z1}, n, {u1, v1}, {}, color, tangent};
      Vertex d{{x1, 0, z0}, n, {u1, v0}, {}, color, tangent};
      appendTriangle(vertices, a, b, c);
      appendTriangle(vertices, a, c, d);
    }
  }
  return vertices;
}

std::vector<Vertex> makeQuad() {
  std::vector<Vertex> vertices;
  const glm::vec3 n(0, 0, 1), color(0.5f, 0.8f, 0.7f);
  const glm::vec4 tangent(1, 0, 0, 1);
  Vertex a{{-1, -1, 0}, n, {0, 0}, {}, color, tangent};
  Vertex b{{ 1, -1, 0}, n, {4, 0}, {}, color, tangent};
  Vertex c{{ 1,  1, 0}, n, {4, 4}, {}, color, tangent};
  Vertex d{{-1,  1, 0}, n, {0, 4}, {}, color, tangent};
  appendTriangle(vertices, a, b, c);
  appendTriangle(vertices, a, c, d);
  return vertices;
}

std::vector<Vertex> makeSphere(int longitudeSegments, int latitudeSegments) {
  std::vector<Vertex> vertices;
  auto point = [=](int longitude, int latitude) {
    const float u = static_cast<float>(longitude) / longitudeSegments;
    const float v = static_cast<float>(latitude) / latitudeSegments;
    const float a = u * glm::two_pi<float>();
    const float b = (v - 0.5f) * glm::pi<float>();
    const glm::vec3 n(std::cos(b) * std::sin(a), std::sin(b), std::cos(b) * std::cos(a));
    const glm::vec3 tangent = glm::normalize(glm::vec3(std::cos(a), 0.0f, -std::sin(a)));
    return Vertex{n, n, {u * 4.0f, v * 2.0f}, {}, n * 0.5f + 0.5f, glm::vec4(tangent, 1.0f)};
  };
  for (int y = 0; y < latitudeSegments; ++y) {
    for (int x = 0; x < longitudeSegments; ++x) {
      Vertex a = point(x, y), b = point(x + 1, y), c = point(x + 1, y + 1), d = point(x, y + 1);
      appendTriangle(vertices, a, b, c);
      appendTriangle(vertices, a, c, d);
    }
  }
  return vertices;
}

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "graphics-lab: %s\n", message.c_str());
  std::exit(EXIT_FAILURE);
}

GLuint compileShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    fail("shader compilation failed:\n" + log);
  }
  return shader;
}

GLuint makeProgram(const char* vertexSource, const char* fragmentSource) {
  const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
  const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(length), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    fail("program link failed:\n" + log);
  }
  return program;
}

std::vector<Vertex> makeTorus() {
  constexpr int majorSegments = 16;
  constexpr int minorSegments = 8;
  constexpr float majorRadius = 1.15f;
  constexpr float minorRadius = 0.46f;
  std::vector<Vertex> vertices;
  vertices.reserve(majorSegments * minorSegments * 6);

  auto point = [](int majorIndex, int minorIndex) {
    const float u = static_cast<float>(majorIndex) / majorSegments;
    const float v = static_cast<float>(minorIndex) / minorSegments;
    const float a = u * glm::two_pi<float>();
    const float b = v * glm::two_pi<float>();
    const glm::vec3 normal(std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b));
    const glm::vec3 center(majorRadius * std::cos(a), 0.0f, majorRadius * std::sin(a));
    const glm::vec3 color = 0.5f + 0.5f * glm::vec3(std::cos(a), std::sin(b), std::sin(a));
    const glm::vec3 tangent(-std::sin(a), 0.0f, std::cos(a));
    return Vertex{center + minorRadius * normal, normal, glm::vec2(u * 4.0f, v * 2.0f), {0, 0, 0}, color,
      glm::vec4(tangent, -1.0f)};
  };

  for (int i = 0; i < majorSegments; ++i) {
    for (int j = 0; j < minorSegments; ++j) {
      std::array<Vertex, 4> q = {point(i, j), point(i + 1, j), point(i + 1, j + 1), point(i, j + 1)};
      // The torus parameterization's +u x +v direction points inward, so emit
      // each quad in the opposite order to keep outward faces counter-clockwise.
      const std::array<int, 6> order = {0, 2, 1, 0, 3, 2};
      for (int k = 0; k < 6; ++k) {
        Vertex vertex = q[order[k]];
        vertex.barycentric = glm::vec3(0.0f);
        vertex.barycentric[k % 3] = 1.0f;
        vertices.push_back(vertex);
      }
    }
  }
  return vertices;
}

const char* sceneVertexShader = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
layout(location=3) in vec3 aBarycentric;
layout(location=4) in vec3 aColor;
layout(location=5) in vec4 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightSpace;
uniform float uQuantization;
uniform bool uClipEnabled;
uniform vec4 uClipPlane;
uniform vec3 uLightDirection;
uniform float uAmbient;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vUvPerspective;
noperspective out vec2 vUvAffine;
out vec3 vBarycentric;
out vec3 vColor;
out float vVertexLighting;
out vec4 vTangent;
out vec4 vLightPosition;

void main() {
  vec3 position = aPosition;
  if (uQuantization > 0.0)
    position = round(position / uQuantization) * uQuantization;
  vec4 world = uModel * vec4(position, 1.0);
  vWorldPosition = world.xyz;
  vNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
  vUvPerspective = aUv;
  vUvAffine = aUv;
  vBarycentric = aBarycentric;
  vColor = aColor;
  vTangent = vec4(normalize(mat3(uModel) * aTangent.xyz), aTangent.w);
  vLightPosition = uLightSpace * world;
  float vertexDiffuse = max(dot(vNormal, normalize(uLightDirection)), 0.0);
  vVertexLighting = uAmbient + (1.0 - uAmbient) * vertexDiffuse;
  gl_ClipDistance[0] = uClipEnabled ? dot(world, uClipPlane) : 1.0;
  gl_Position = uProjection * uView * world;
}
)GLSL";

const char* sceneFragmentShader = R"GLSL(
#version 410 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vUvPerspective;
noperspective in vec2 vUvAffine;
in vec3 vBarycentric;
in vec3 vColor;
in float vVertexLighting;
in vec4 vTangent;
in vec4 vLightPosition;

uniform sampler2D uTexture;
uniform sampler2D uNormalMap;
uniform sampler2D uShadowMap;
uniform bool uAffineMapping;
uniform bool uSmoothShading;
uniform bool uWireframe;
uniform int uVisualization;
uniform int uLightingModel;
uniform float uAmbient;
uniform vec3 uLightDirection;
uniform float uShininess;
uniform int uTransparencyMode;
uniform float uAlphaCutoff;
uniform bool uPremultiplyAlpha;
uniform bool uNormalMapping;
uniform float uNormalStrength;
uniform bool uLinearLight;
uniform vec3 uObjectTint;
uniform bool uShadowsEnabled;
uniform float uShadowBias;
uniform bool uShadowPcf;
uniform bool uFogEnabled;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uCameraPosition;
out vec4 fragColor;

float shadowAmount() {
  vec3 projected = vLightPosition.xyz / vLightPosition.w * 0.5 + 0.5;
  if (projected.z <= 0.0 || projected.z >= 1.0 || any(lessThan(projected.xy, vec2(0.0))) || any(greaterThan(projected.xy, vec2(1.0))))
    return 0.0;
  if (!uShadowPcf)
    return projected.z - uShadowBias > texture(uShadowMap, projected.xy).r ? 1.0 : 0.0;
  vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
  float shadow = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      shadow += projected.z - uShadowBias > texture(uShadowMap, projected.xy + vec2(x, y) * texel).r ? 1.0 : 0.0;
  return shadow / 9.0;
}

void main() {
  vec2 uv = uAffineMapping ? vUvAffine : vUvPerspective;
  vec3 normal = uSmoothShading
    ? normalize(vNormal)
    : normalize(cross(dFdx(vWorldPosition), dFdy(vWorldPosition)));
  if (!gl_FrontFacing) normal = -normal;
  vec3 tangent = normalize(vTangent.xyz - normal * dot(normal, vTangent.xyz));
  vec3 bitangent = normalize(cross(normal, tangent)) * vTangent.w;
  if (uNormalMapping) {
    vec3 tangentNormal = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= uNormalStrength;
    tangentNormal = normalize(tangentNormal);
    normal = normalize(mat3(tangent, bitangent, normal) * tangentNormal);
  }
  vec4 texel = texture(uTexture, uv);
  float alpha = uVisualization == 0 ? texel.a : 1.0;
  if (uTransparencyMode == 1 && alpha < uAlphaCutoff) discard;
  if (uTransparencyMode == 0) alpha = 1.0;
  vec3 albedo = uLinearLight ? pow(texel.rgb, vec3(2.2)) : texel.rgb;
  albedo *= uObjectTint;
  if (uVisualization == 1) albedo = vec3(fract(uv), 0.0);
  if (uVisualization == 2) albedo = normal * 0.5 + 0.5;
  if (uVisualization == 3) albedo = vColor;
  if (uVisualization == 4) albedo = tangent * 0.5 + 0.5;
  if (uVisualization == 5) albedo = bitangent * 0.5 + 0.5;

  vec3 lightDirection = normalize(uLightDirection);
  float diffuse = max(dot(normal, lightDirection), 0.0);
  float fragmentLighting = uAmbient + (1.0 - uAmbient) * diffuse;
  vec3 color = albedo;
  if (uLightingModel == 1) color = albedo * vVertexLighting;
  if (uLightingModel >= 2) color = albedo * fragmentLighting;
  if (uLightingModel >= 3) {
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    float specular = 0.0;
    if (uLightingModel == 3)
      specular = pow(max(dot(viewDirection, reflect(-lightDirection, normal)), 0.0), uShininess);
    if (uLightingModel == 4)
      specular = pow(max(dot(normal, normalize(lightDirection + viewDirection)), 0.0), uShininess);
    color += vec3(0.35) * specular;
  }
  if (uShadowsEnabled && uLightingModel != 0)
    color *= 1.0 - shadowAmount() * (1.0 - uAmbient);

  if (uWireframe) {
    vec3 width = fwidth(vBarycentric);
    vec3 edge = smoothstep(vec3(0.0), width * 1.15, vBarycentric);
    float interior = min(edge.x, min(edge.y, edge.z));
    color = mix(vec3(0.035), color, interior);
  }
  if (uFogEnabled) {
    float distanceToCamera = length(uCameraPosition - vWorldPosition);
    float fogAmount = smoothstep(uFogStart, max(uFogEnd, uFogStart + 0.001), distanceToCamera);
    vec3 fogColor = vec3(0.105, 0.112, 0.12);
    if (uLinearLight) fogColor = pow(fogColor, vec3(2.2));
    color = mix(color, fogColor, fogAmount);
  }
  fragColor = vec4(uPremultiplyAlpha ? color * alpha : color, alpha);
}
)GLSL";

const char* outputVertexShader = R"GLSL(
#version 410 core
out vec2 vUv;
void main() {
  const vec2 positions[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
  vec2 position = positions[gl_VertexID];
  vUv = position * 0.5 + 0.5;
  gl_Position = vec4(position, 0, 1);
}
)GLSL";

const char* outputFragmentShader = R"GLSL(
#version 410 core
in vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform int uBitsPerChannel;
uniform bool uDithering;
uniform bool uLinearLight;
uniform int uDepthVisualization;
uniform float uNearPlane;
uniform float uFarPlane;
uniform bool uOrthographic;
uniform sampler2D uShadowMap;
uniform bool uVisualizeShadowMap;
uniform sampler2D uOverdraw;
uniform bool uVisualizeOverdraw;
uniform float uOverdrawRange;
out vec4 fragColor;

float bayer4(ivec2 p) {
  const float m[16] = float[16](
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
  );
  return (m[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0 - 0.5;
}

void main() {
  vec3 color = texture(uScene, vUv).rgb;
  if (uVisualizeOverdraw) {
    float count = texture(uOverdraw, vUv).r;
    float t = clamp(count / max(uOverdrawRange, 1.0), 0.0, 1.0);
    color = clamp(vec3(1.5 * t, 1.5 - abs(4.0 * t - 2.0), 1.5 * (1.0 - t)), 0.0, 1.0);
    if (count < 0.5) color = vec3(0.02);
  } else if (uVisualizeShadowMap) {
    color = vec3(texture(uShadowMap, vUv).r);
  } else if (uDepthVisualization != 0) {
    float rawDepth = texture(uDepth, vUv).r;
    if (uDepthVisualization == 1) {
      color = vec3(rawDepth);
    } else {
      float linearDepth;
      if (uOrthographic) {
        linearDepth = mix(uNearPlane, uFarPlane, rawDepth);
      } else {
        float ndcDepth = rawDepth * 2.0 - 1.0;
        linearDepth = (2.0 * uNearPlane * uFarPlane) /
          (uFarPlane + uNearPlane - ndcDepth * (uFarPlane - uNearPlane));
      }
      color = vec3(clamp(linearDepth / 10.0, 0.0, 1.0));
    }
  } else if (uLinearLight) {
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
  }
  float levels = exp2(float(uBitsPerChannel)) - 1.0;
  if (uDithering) color += bayer4(ivec2(gl_FragCoord.xy)) / levels;
  color = round(clamp(color, 0.0, 1.0) * levels) / levels;
  fragColor = vec4(color, 1.0);
}
)GLSL";

const char* shadowVertexShader = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uLightSpace;
uniform float uQuantization;
void main() {
  vec3 position = aPosition;
  if (uQuantization > 0.0) position = round(position / uQuantization) * uQuantization;
  gl_Position = uLightSpace * uModel * vec4(position, 1.0);
}
)GLSL";

const char* shadowFragmentShader = R"GLSL(
#version 410 core
void main() {}
)GLSL";

const char* overdrawVertexShader = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uQuantization;
void main() {
  vec3 position = aPosition;
  if (uQuantization > 0.0) position = round(position / uQuantization) * uQuantization;
  gl_Position = uProjection * uView * uModel * vec4(position, 1.0);
}
)GLSL";

const char* overdrawFragmentShader = R"GLSL(
#version 410 core
layout(location=0) out float fragmentCount;
void main() { fragmentCount = 1.0; }
)GLSL";

struct RenderTarget {
  GLuint sceneFbo = 0;
  GLuint sceneTexture = 0;
  GLuint depthTexture = 0;
  GLuint multisampleFbo = 0;
  GLuint multisampleColor = 0;
  GLuint multisampleDepth = 0;
  GLuint outputFbo = 0;
  GLuint outputTexture = 0;
  GLuint overdrawFbo = 0;
  GLuint overdrawTexture = 0;
  int width = 0;
  int height = 0;
  int depthPrecision = 0;
  int samples = 0;
  bool packedStencil = false;

  void resize(int newWidth, int newHeight, int newDepthPrecision, int newSamples, bool needsStencil) {
    if (width == newWidth && height == newHeight && depthPrecision == newDepthPrecision && samples == newSamples && packedStencil == needsStencil) return;
    width = newWidth;
    height = newHeight;
    depthPrecision = newDepthPrecision;
    samples = newSamples;
    packedStencil = needsStencil;
    if (!sceneFbo) {
      glGenFramebuffers(1, &sceneFbo);
      glGenTextures(1, &sceneTexture);
      glGenTextures(1, &depthTexture);
      glGenFramebuffers(1, &multisampleFbo);
      glGenRenderbuffers(1, &multisampleColor);
      glGenRenderbuffers(1, &multisampleDepth);
      glGenFramebuffers(1, &outputFbo);
      glGenTextures(1, &outputTexture);
      glGenFramebuffers(1, &overdrawFbo);
      glGenTextures(1, &overdrawTexture);
    }

    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    const GLint depthFormat = packedStencil ? GL_DEPTH24_STENCIL8 : depthPrecision == 16 ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT24;
    const GLenum depthType = packedStencil ? GL_UNSIGNED_INT_24_8 : depthPrecision == 16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    const GLenum depthExternalFormat = packedStencil ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT;
    glTexImage2D(GL_TEXTURE_2D, 0, depthFormat, width, height, 0, depthExternalFormat, depthType, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTexture, 0);
    // GL_DEPTH_STENCIL_ATTACHMENT aliases both attachment points. Detach each
    // point explicitly before switching between depth-only and packed formats;
    // otherwise the previous stencil half survives and makes the FBO incomplete.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, packedStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
      GL_TEXTURE_2D, depthTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      fail("could not create resolved scene render target");

    if (samples > 1) {
      glBindRenderbuffer(GL_RENDERBUFFER, multisampleColor);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
      glBindRenderbuffer(GL_RENDERBUFFER, multisampleDepth);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, depthFormat, width, height);
      glBindFramebuffer(GL_FRAMEBUFFER, multisampleFbo);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, multisampleColor);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, packedStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER, multisampleDepth);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fail("could not create multisampled render target");
    }

    glBindTexture(GL_TEXTURE_2D, outputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      fail("could not create output render target");

    glBindTexture(GL_TEXTURE_2D, overdrawTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, overdrawFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, overdrawTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      fail("could not create overdraw analysis target");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void destroy() {
    glDeleteFramebuffers(1, &sceneFbo);
    glDeleteTextures(1, &sceneTexture);
    glDeleteTextures(1, &depthTexture);
    glDeleteFramebuffers(1, &multisampleFbo);
    glDeleteRenderbuffers(1, &multisampleColor);
    glDeleteRenderbuffers(1, &multisampleDepth);
    glDeleteFramebuffers(1, &outputFbo);
    glDeleteTextures(1, &outputTexture);
    glDeleteFramebuffers(1, &overdrawFbo);
    glDeleteTextures(1, &overdrawTexture);
  }
};

class Renderer {
public:
  Renderer() {
    sceneProgram_ = makeProgram(sceneVertexShader, sceneFragmentShader);
    outputProgram_ = makeProgram(outputVertexShader, outputFragmentShader);
    shadowProgram_ = makeProgram(shadowVertexShader, shadowFragmentShader);
    overdrawProgram_ = makeProgram(overdrawVertexShader, overdrawFragmentShader);
    std::vector<Vertex> vertices;
    auto appendMesh = [&vertices](const std::vector<Vertex>& mesh) {
      const MeshRange range{static_cast<GLint>(vertices.size()), static_cast<GLsizei>(mesh.size())};
      vertices.insert(vertices.end(), mesh.begin(), mesh.end());
      return range;
    };
    torus_ = appendMesh(makeTorus());
    plane_ = appendMesh(makePlane(7.0f, 16.0f, 8, 24));
    quad_ = appendMesh(makeQuad());
    lowSphere_ = appendMesh(makeSphere(12, 6));
    smoothSphere_ = appendMesh(makeSphere(32, 16));
    vertexCount_ = static_cast<GLsizei>(vertices.size());
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, barycentric)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, tangent)));
    glGenVertexArrays(1, &fullscreenVao_);
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples_);
    if (GLEW_EXT_texture_filter_anisotropic)
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy_);
    if (std::getenv("GRAPHICS_LAB_VALIDATE_FRAMEBUFFERS")) {
      RenderTarget validationTarget;
      validationTarget.resize(64, 64, 16, 1, false);
      validationTarget.resize(64, 64, 24, 1, true);
      validationTarget.resize(64, 64, 24, 1, false);
      if (maxSamples_ >= 2) {
        validationTarget.resize(64, 64, 24, 2, true);
        validationTarget.resize(64, 64, 16, 2, false);
      }
      validationTarget.destroy();
    }
    makeCheckerTexture();
    makeNormalTexture();
    glGenFramebuffers(1, &shadowFbo_);
    glGenTextures(1, &shadowTexture_);
  }

  ~Renderer() {
    targetA_.destroy();
    targetB_.destroy();
    glDeleteTextures(1, &checkerTexture_);
    glDeleteTextures(1, &normalTexture_);
    glDeleteFramebuffers(1, &shadowFbo_);
    glDeleteTextures(1, &shadowTexture_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteVertexArrays(1, &fullscreenVao_);
    glDeleteProgram(sceneProgram_);
    glDeleteProgram(outputProgram_);
    glDeleteProgram(shadowProgram_);
    glDeleteProgram(overdrawProgram_);
  }

  GLuint render(const RendererState& state, const CameraOrbit& camera, TestScene scene, bool referenceTarget) {
    RenderTarget& target = referenceTarget ? targetB_ : targetA_;
    const float azimuth = glm::radians(state.lighting.azimuth);
    const float elevation = glm::radians(state.lighting.elevation);
    const glm::vec3 lightDirection(std::cos(elevation) * std::cos(azimuth), std::sin(elevation),
      std::cos(elevation) * std::sin(azimuth));
    const glm::vec3 lightUp = std::abs(lightDirection.y) > 0.98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 lightView = glm::lookAt(lightDirection * 9.0f, glm::vec3(0), lightUp);
    const glm::mat4 lightProjection = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 20.0f);
    const glm::mat4 lightSpace = lightProjection * lightView;
    const bool shadowsEnabled = state.lighting.shadows && scene == TestScene::Lighting;

    if (shadowsEnabled) {
      resizeShadowMap(state.lighting.shadowResolution);
      glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
      glViewport(0, 0, shadowResolution_, shadowResolution_);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);
      glDepthFunc(GL_LESS);
      glDisable(GL_BLEND);
      glEnable(GL_CULL_FACE);
      glCullFace(GL_FRONT);
      glClearDepth(1.0);
      glClear(GL_DEPTH_BUFFER_BIT);
      glUseProgram(shadowProgram_);
      glUniformMatrix4fv(glGetUniformLocation(shadowProgram_, "uLightSpace"), 1, GL_FALSE, glm::value_ptr(lightSpace));
      glUniform1f(glGetUniformLocation(shadowProgram_, "uQuantization"), state.geometry.vertexQuantization);
      glBindVertexArray(vao_);
      auto drawShadow = [this](const MeshRange& mesh, const glm::mat4& modelMatrix) {
        glUniformMatrix4fv(glGetUniformLocation(shadowProgram_, "uModel"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
        glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
      };
      const glm::mat4 identity(1.0f);
      drawShadow(lowSphere_, glm::translate(identity, glm::vec3(-1.35f, 0, 0)));
      drawShadow(smoothSphere_, glm::translate(identity, glm::vec3(1.35f, 0, 0)));
      drawShadow(torus_, glm::translate(glm::scale(identity, glm::vec3(0.62f)), glm::vec3(0, 1.9f, 0)));
      drawShadow(plane_, glm::translate(glm::scale(identity, glm::vec3(0.75f)), glm::vec3(0, -1.55f, -0.1f)));
    }

    const int samples = state.rasterization.samples == 1 ? 1 : std::min(state.rasterization.samples, maxSamples_);
    const bool needsStencil = scene == TestScene::StencilMask && state.stencil.enabled;
    target.resize(state.output.width, state.output.height, state.depth.precision, samples, needsStencil);
    glBindFramebuffer(GL_FRAMEBUFFER, samples > 1 ? target.multisampleFbo : target.sceneFbo);
    glViewport(0, 0, target.width, target.height);
    glDepthMask(GL_TRUE);
    const GLenum depthFunctions[] = {GL_LESS, GL_LEQUAL, GL_GREATER, GL_ALWAYS};
    glDepthFunc(depthFunctions[std::clamp(state.depth.function, 0, 3)]);
    glClearDepth(state.depth.function == 2 ? 0.0 : 1.0);
    glStencilMask(0xff);
    glClearStencil(0);
    glDisable(GL_STENCIL_TEST);
    if (state.depth.testing) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (state.rasterization.cullMode == 0) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(state.rasterization.cullMode == 1 ? GL_BACK : GL_FRONT);
    }
    if (state.rasterization.polygonOffset) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(state.rasterization.polygonOffsetFactor, state.rasterization.polygonOffsetUnits);
    } else {
      glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (state.surface.transparency >= 2) {
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      if (state.surface.transparency == 2) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      if (state.surface.transparency == 3) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      if (state.surface.transparency == 4) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      if (state.surface.transparency == 5) glBlendFunc(GL_DST_COLOR, GL_ZERO);
    } else {
      glDisable(GL_BLEND);
    }
    glEnable(GL_CLIP_DISTANCE0);
    const float backgroundR = state.color.linearLight ? std::pow(0.105f, 2.2f) : 0.105f;
    const float backgroundG = state.color.linearLight ? std::pow(0.112f, 2.2f) : 0.112f;
    const float backgroundB = state.color.linearLight ? std::pow(0.120f, 2.2f) : 0.120f;
    glClearColor(backgroundR, backgroundG, backgroundB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glDepthMask(state.depth.writing ? GL_TRUE : GL_FALSE);

    glUseProgram(sceneProgram_);
    const glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(-14.0f), glm::vec3(1, 0, 0));
    const glm::mat4 view = camera.view();
    const float aspect = static_cast<float>(target.width) / static_cast<float>(target.height);
    const float halfHeight = state.camera.orthographicSize * 0.5f;
    const glm::mat4 projection = state.camera.orthographic
      ? glm::ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, state.camera.nearPlane, 100.0f)
      : glm::perspective(glm::radians(state.camera.fieldOfView), aspect, state.camera.nearPlane, 100.0f);
    matrix("uModel", model);
    matrix("uView", view);
    matrix("uProjection", projection);
    matrix("uLightSpace", lightSpace);
    glUniform1f(location("uQuantization"), state.geometry.vertexQuantization);
    glUniform1i(location("uClipEnabled"), state.geometry.clipping);
    const glm::vec4 clipPlane(0.0f, state.geometry.clipAbove ? -1.0f : 1.0f, 0.0f,
      state.geometry.clipAbove ? state.geometry.clipHeight : -state.geometry.clipHeight);
    glUniform4fv(location("uClipPlane"), 1, glm::value_ptr(clipPlane));
    glUniform1i(location("uAffineMapping"), state.rasterization.affineMapping);
    glUniform1i(location("uSmoothShading"), state.surface.smoothShading);
    glUniform1i(location("uWireframe"), state.surface.wireframe);
    glUniform1i(location("uVisualization"), state.surface.visualization);
    glUniform1i(location("uTransparencyMode"), state.surface.transparency);
    glUniform1f(location("uAlphaCutoff"), state.surface.alphaCutoff);
    glUniform1i(location("uPremultiplyAlpha"), state.surface.transparency == 3);
    glUniform1i(location("uNormalMapping"), state.surface.normalMapping);
    glUniform1f(location("uNormalStrength"), state.surface.normalStrength);
    glUniform1i(location("uLightingModel"), state.lighting.model);
    glUniform1f(location("uAmbient"), state.lighting.ambient);
    glUniform1f(location("uShininess"), state.lighting.shininess);
    glUniform1i(location("uLinearLight"), state.color.linearLight);
    glUniform3fv(location("uLightDirection"), 1, glm::value_ptr(lightDirection));
    glUniform1i(location("uShadowsEnabled"), shadowsEnabled);
    glUniform1f(location("uShadowBias"), state.lighting.shadowBias);
    glUniform1i(location("uShadowPcf"), state.lighting.shadowPcf);
    glUniform1i(location("uFogEnabled"), state.post.fog);
    glUniform1f(location("uFogStart"), state.post.fogStart);
    glUniform1f(location("uFogEnd"), state.post.fogEnd);
    glUniform3fv(location("uCameraPosition"), 1, glm::value_ptr(camera.eye()));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);
    GLint minificationFilter = state.texture.nearestFiltering ? GL_NEAREST : GL_LINEAR;
    if (state.texture.mipmapping) {
      if (state.texture.nearestFiltering) minificationFilter = GL_NEAREST_MIPMAP_NEAREST;
      else minificationFilter = state.texture.trilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minificationFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, state.texture.nearestFiltering ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    if (GLEW_EXT_texture_filter_anisotropic)
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::clamp(state.texture.anisotropy, 1.0f, maxAnisotropy_));
    glUniform1i(location("uTexture"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, normalTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, state.texture.mipmapping ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glUniform1i(location("uNormalMap"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glUniform1i(location("uShadowMap"), 2);
    glBindVertexArray(vao_);
    auto drawMesh = [this](const MeshRange& mesh, const glm::mat4& modelMatrix, const glm::vec3& tint) {
      matrix("uModel", modelMatrix);
      glUniform3fv(location("uObjectTint"), 1, glm::value_ptr(tint));
      glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
    };
    const glm::mat4 identity(1.0f);
    switch (scene) {
      case TestScene::Torus:
        drawMesh(torus_, glm::rotate(identity, glm::radians(-14.0f), glm::vec3(1, 0, 0)), glm::vec3(1.0f));
        break;
      case TestScene::TexturePlane:
        drawMesh(plane_, glm::translate(identity, glm::vec3(0, -1.25f, -3.5f)), glm::vec3(1.0f));
        break;
      case TestScene::DepthPrecision: {
        const glm::mat4 horizontal = glm::rotate(identity, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        drawMesh(quad_, glm::scale(horizontal, glm::vec3(2.2f)), glm::vec3(0.65f, 0.85f, 1.0f));
        drawMesh(quad_, glm::translate(horizontal, glm::vec3(0, 0, 0.00015f)) * glm::scale(identity, glm::vec3(1.65f)), glm::vec3(1.0f, 0.55f, 0.42f));
        drawMesh(quad_, glm::translate(horizontal, glm::vec3(0, 0, 0.00030f)) * glm::scale(identity, glm::vec3(1.05f)), glm::vec3(0.5f, 1.0f, 0.62f));
        break;
      }
      case TestScene::Transparency: {
        const std::array<glm::mat4, 3> transforms = {
          glm::rotate(identity, glm::radians(28.0f), glm::vec3(0, 1, 0)),
          glm::rotate(identity, glm::radians(-35.0f), glm::vec3(0, 1, 0)),
          glm::rotate(identity, glm::radians(90.0f), glm::vec3(1, 0, 0))};
        const std::array<glm::vec3, 3> tints = {glm::vec3(0.42f, 0.8f, 1.0f), glm::vec3(1.0f, 0.48f, 0.35f), glm::vec3(0.55f, 1.0f, 0.56f)};
        for (int drawIndex = 0; drawIndex < 3; ++drawIndex) {
          const int objectIndex = state.surface.reverseDrawOrder ? 2 - drawIndex : drawIndex;
          drawMesh(quad_, transforms[objectIndex], tints[objectIndex]);
        }
        break;
      }
      case TestScene::Lighting:
        drawMesh(plane_, glm::translate(glm::scale(identity, glm::vec3(0.75f)), glm::vec3(0, -1.55f, -0.1f)), glm::vec3(0.45f));
        drawMesh(lowSphere_, glm::translate(identity, glm::vec3(-1.35f, 0, 0)), glm::vec3(0.9f, 0.55f, 0.38f));
        drawMesh(smoothSphere_, glm::translate(identity, glm::vec3(1.35f, 0, 0)), glm::vec3(0.45f, 0.68f, 1.0f));
        drawMesh(torus_, glm::translate(glm::scale(identity, glm::vec3(0.62f)), glm::vec3(0, 1.9f, 0)), glm::vec3(0.7f, 1.0f, 0.6f));
        break;
      case TestScene::StencilMask:
        if (state.stencil.enabled) {
          glEnable(GL_STENCIL_TEST);
          glStencilMask(0xff);
          glStencilFunc(GL_ALWAYS, state.stencil.reference, 0xff);
          glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
          glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
          glDepthMask(GL_FALSE);
          drawMesh(lowSphere_, glm::scale(identity, glm::vec3(1.35f)), glm::vec3(1.0f));
          glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
          glDepthMask(state.depth.writing ? GL_TRUE : GL_FALSE);
          glStencilMask(0x00);
          glStencilFunc(state.stencil.invert ? GL_NOTEQUAL : GL_EQUAL, state.stencil.reference, 0xff);
          glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        }
        drawMesh(quad_, glm::translate(glm::scale(identity, glm::vec3(2.0f)), glm::vec3(0, 0, -0.35f)),
          glm::vec3(0.45f, 0.8f, 1.0f));
        glStencilMask(0xff);
        glDisable(GL_STENCIL_TEST);
        break;
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_CLIP_DISTANCE0);

    if (samples > 1) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, target.multisampleFbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.sceneFbo);
      const GLbitfield resolveBuffers = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
        (needsStencil ? GL_STENCIL_BUFFER_BIT : 0);
      glBlitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height,
        resolveBuffers, GL_NEAREST);
    }

    if (state.post.overdraw) {
      glBindFramebuffer(GL_FRAMEBUFFER, target.overdrawFbo);
      glViewport(0, 0, target.width, target.height);
      glDisable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_ONE, GL_ONE);
      if (state.rasterization.cullMode == 0) glDisable(GL_CULL_FACE); else {
        glEnable(GL_CULL_FACE);
        glCullFace(state.rasterization.cullMode == 1 ? GL_BACK : GL_FRONT);
      }
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      glUseProgram(overdrawProgram_);
      glUniformMatrix4fv(glGetUniformLocation(overdrawProgram_, "uView"), 1, GL_FALSE, glm::value_ptr(view));
      glUniformMatrix4fv(glGetUniformLocation(overdrawProgram_, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));
      glUniform1f(glGetUniformLocation(overdrawProgram_, "uQuantization"), state.geometry.vertexQuantization);
      glBindVertexArray(vao_);
      auto countMesh = [this](const MeshRange& mesh, const glm::mat4& modelMatrix) {
        glUniformMatrix4fv(glGetUniformLocation(overdrawProgram_, "uModel"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
        glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
      };
      const glm::mat4 identity(1.0f);
      switch (scene) {
        case TestScene::Torus:
          countMesh(torus_, glm::rotate(identity, glm::radians(-14.0f), glm::vec3(1, 0, 0)));
          break;
        case TestScene::TexturePlane:
          countMesh(plane_, glm::translate(identity, glm::vec3(0, -1.25f, -3.5f)));
          break;
        case TestScene::DepthPrecision: {
          const glm::mat4 horizontal = glm::rotate(identity, glm::radians(-90.0f), glm::vec3(1, 0, 0));
          countMesh(quad_, glm::scale(horizontal, glm::vec3(2.2f)));
          countMesh(quad_, glm::translate(horizontal, glm::vec3(0, 0, 0.00015f)) * glm::scale(identity, glm::vec3(1.65f)));
          countMesh(quad_, glm::translate(horizontal, glm::vec3(0, 0, 0.00030f)) * glm::scale(identity, glm::vec3(1.05f)));
          break;
        }
        case TestScene::Transparency:
          countMesh(quad_, glm::rotate(identity, glm::radians(28.0f), glm::vec3(0, 1, 0)));
          countMesh(quad_, glm::rotate(identity, glm::radians(-35.0f), glm::vec3(0, 1, 0)));
          countMesh(quad_, glm::rotate(identity, glm::radians(90.0f), glm::vec3(1, 0, 0)));
          break;
        case TestScene::Lighting:
          countMesh(plane_, glm::translate(glm::scale(identity, glm::vec3(0.75f)), glm::vec3(0, -1.55f, -0.1f)));
          countMesh(lowSphere_, glm::translate(identity, glm::vec3(-1.35f, 0, 0)));
          countMesh(smoothSphere_, glm::translate(identity, glm::vec3(1.35f, 0, 0)));
          countMesh(torus_, glm::translate(glm::scale(identity, glm::vec3(0.62f)), glm::vec3(0, 1.9f, 0)));
          break;
        case TestScene::StencilMask:
          if (state.stencil.enabled) countMesh(lowSphere_, glm::scale(identity, glm::vec3(1.35f)));
          countMesh(quad_, glm::translate(glm::scale(identity, glm::vec3(2.0f)), glm::vec3(0, 0, -0.35f)));
          break;
      }
      glDisable(GL_BLEND);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target.outputFbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, target.width, target.height);
    glUseProgram(outputProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target.sceneTexture);
    glUniform1i(glGetUniformLocation(outputProgram_, "uScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, target.depthTexture);
    glUniform1i(glGetUniformLocation(outputProgram_, "uDepth"), 1);
    glUniform1i(glGetUniformLocation(outputProgram_, "uBitsPerChannel"), state.color.bitsPerChannel);
    glUniform1i(glGetUniformLocation(outputProgram_, "uDithering"), state.color.dithering);
    glUniform1i(glGetUniformLocation(outputProgram_, "uLinearLight"), state.color.linearLight);
    glUniform1i(glGetUniformLocation(outputProgram_, "uDepthVisualization"), state.depth.visualization);
    glUniform1f(glGetUniformLocation(outputProgram_, "uNearPlane"), state.camera.nearPlane);
    glUniform1f(glGetUniformLocation(outputProgram_, "uFarPlane"), 100.0f);
    glUniform1i(glGetUniformLocation(outputProgram_, "uOrthographic"), state.camera.orthographic);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glUniform1i(glGetUniformLocation(outputProgram_, "uShadowMap"), 2);
    glUniform1i(glGetUniformLocation(outputProgram_, "uVisualizeShadowMap"),
      shadowsEnabled && state.lighting.visualizeShadowMap);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, target.overdrawTexture);
    glUniform1i(glGetUniformLocation(outputProgram_, "uOverdraw"), 3);
    glUniform1i(glGetUniformLocation(outputProgram_, "uVisualizeOverdraw"), state.post.overdraw);
    glUniform1f(glGetUniformLocation(outputProgram_, "uOverdrawRange"), state.post.overdrawRange);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, target.outputTexture);
    const GLint upscaleFilter = state.output.nearestUpscaling ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, upscaleFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, upscaleFilter);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return target.outputTexture;
  }

private:
  GLuint sceneProgram_ = 0, outputProgram_ = 0, shadowProgram_ = 0, overdrawProgram_ = 0;
  GLuint vao_ = 0, vbo_ = 0, fullscreenVao_ = 0, checkerTexture_ = 0, normalTexture_ = 0;
  GLsizei vertexCount_ = 0;
  GLint maxSamples_ = 1;
  GLfloat maxAnisotropy_ = 1.0f;
  MeshRange torus_, plane_, quad_, lowSphere_, smoothSphere_;
  GLuint shadowFbo_ = 0, shadowTexture_ = 0;
  int shadowResolution_ = 0;
  RenderTarget targetA_, targetB_;

  GLint location(const char* name) const { return glGetUniformLocation(sceneProgram_, name); }
  void matrix(const char* name, const glm::mat4& value) { glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value)); }

  void makeCheckerTexture() {
    constexpr int size = 64;
    std::array<unsigned char, size * size * 4> pixels{};
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const bool checker = ((x / 8) + (y / 8)) % 2 == 0;
        const glm::u8vec3 color = checker ? glm::u8vec3(205, 188, 146) : glm::u8vec3(53, 68, 72);
        const int offset = (y * size + x) * 4;
        pixels[offset] = color.r; pixels[offset + 1] = color.g; pixels[offset + 2] = color.b;
        pixels[offset + 3] = checker ? 255 : 72;
      }
    }
    glGenTextures(1, &checkerTexture_);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  void makeNormalTexture() {
    constexpr int size = 64;
    std::array<unsigned char, size * size * 3> pixels{};
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const float u = static_cast<float>(x) / size * glm::two_pi<float>() * 4.0f;
        const float v = static_cast<float>(y) / size * glm::two_pi<float>() * 4.0f;
        glm::vec3 normal(0.42f * std::sin(u), 0.42f * std::cos(v), 1.0f);
        normal = glm::normalize(normal) * 0.5f + 0.5f;
        const int offset = (y * size + x) * 3;
        pixels[offset] = static_cast<unsigned char>(normal.x * 255.0f);
        pixels[offset + 1] = static_cast<unsigned char>(normal.y * 255.0f);
        pixels[offset + 2] = static_cast<unsigned char>(normal.z * 255.0f);
      }
    }
    glGenTextures(1, &normalTexture_);
    glBindTexture(GL_TEXTURE_2D, normalTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  void resizeShadowMap(int resolution) {
    resolution = std::clamp(resolution, 128, 4096);
    if (shadowResolution_ == resolution) return;
    shadowResolution_ = resolution;
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, resolution, resolution, 0,
      GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTexture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      fail("could not create shadow-map framebuffer");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
};

void setStyle() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.FrameRounding = 1.0f;
  style.PopupRounding = 1.0f;
  style.ScrollbarRounding = 1.0f;
  style.GrabRounding = 1.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.ItemSpacing = ImVec2(7, 6);
  style.FramePadding = ImVec2(7, 4);
  style.WindowPadding = ImVec2(10, 9);
  auto& c = style.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.11f, 0.12f, 1);
  c[ImGuiCol_ChildBg] = ImVec4(0.125f, 0.13f, 0.14f, 1);
  c[ImGuiCol_Border] = ImVec4(0.25f, 0.26f, 0.27f, 1);
  c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.18f, 1);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.21f, 0.22f, 1);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.24f, 0.25f, 1);
  c[ImGuiCol_Button] = ImVec4(0.16f, 0.17f, 0.18f, 1);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.21f, 0.22f, 0.23f, 1);
  c[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.29f, 0.30f, 1);
  c[ImGuiCol_Header] = ImVec4(0.19f, 0.25f, 0.27f, 1);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.29f, 0.31f, 1);
  c[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.32f, 0.34f, 1);
  c[ImGuiCol_CheckMark] = ImVec4(0.56f, 0.75f, 0.77f, 1);
  c[ImGuiCol_SliderGrab] = ImVec4(0.48f, 0.65f, 0.67f, 1);
  c[ImGuiCol_Text] = ImVec4(0.86f, 0.87f, 0.88f, 1);
  c[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.54f, 0.55f, 1);
}

bool radioPair(const char* first, const char* second, bool& secondSelected) {
  bool changed = false;
  if (ImGui::RadioButton(first, !secondSelected)) { secondSelected = false; changed = true; }
  ImGui::SameLine();
  if (ImGui::RadioButton(second, secondSelected)) { secondSelected = true; changed = true; }
  return changed;
}

void description(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::PushTextWrapPos();
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::Spacing();
}

void inspector(Category category, RendererState& state) {
  switch (category) {
    case Category::Geometry: {
      ImGui::TextUnformatted("GEOMETRY"); ImGui::Separator();
      ImGui::TextUnformatted("Vertex position precision");
      const char* labels[] = {"Full precision", "1/64 unit", "1/32 unit", "1/16 unit", "1/8 unit"};
      const float values[] = {0.0f, 1.0f/64.0f, 1.0f/32.0f, 1.0f/16.0f, 1.0f/8.0f};
      int selected = 0;
      for (int i = 1; i < 5; ++i) if (std::abs(state.geometry.vertexQuantization - values[i]) < 0.0001f) selected = i;
      if (ImGui::Combo("##precision", &selected, labels, 5)) state.geometry.vertexQuantization = values[selected];
      description("Rounds model-space vertex positions to a fixed grid before projection.");
      ImGui::Checkbox("World-space clipping plane", &state.geometry.clipping);
      ImGui::SliderFloat("Plane height", &state.geometry.clipHeight, -1.5f, 1.5f, "y = %.2f");
      radioPair("Keep above", "Keep below", state.geometry.clipAbove);
      description("The GPU clips primitives against a horizontal world-space plane before rasterization.");
      break;
    }
    case Category::Camera: {
      ImGui::TextUnformatted("CAMERA"); ImGui::Separator();
      ImGui::TextUnformatted("Projection");
      radioPair("Perspective", "Orthographic", state.camera.orthographic);
      description("Perspective divides by depth; orthographic projection preserves apparent size with distance.");
      if (state.camera.orthographic)
        ImGui::SliderFloat("View height", &state.camera.orthographicSize, 1.0f, 10.0f, "%.2f units");
      else
        ImGui::SliderFloat("Field of view", &state.camera.fieldOfView, 20.0f, 100.0f, "%.0f deg");
      ImGui::SliderFloat("Near clipping plane", &state.camera.nearPlane, 0.01f, 2.0f, "%.3f unit", ImGuiSliderFlags_Logarithmic);
      description("Geometry closer than this camera-space distance is clipped. It also strongly affects depth precision.");
      break;
    }
    case Category::Rasterization: {
      ImGui::TextUnformatted("RASTERIZATION"); ImGui::Separator();
      ImGui::TextUnformatted("Texture coordinate interpolation");
      radioPair("Perspective-correct", "Affine", state.rasterization.affineMapping);
      description("Affine interpolation does not compensate texture coordinates for perspective depth.");
      ImGui::TextUnformatted("Face culling");
      const char* cullLabels[] = {"None", "Back faces", "Front faces"};
      ImGui::Combo("##culling", &state.rasterization.cullMode, cullLabels, 3);
      description("Discards triangles according to their screen-space winding direction.");
      ImGui::TextUnformatted("Multisample anti-aliasing");
      const char* sampleLabels[] = {"Off (1 sample)", "2 samples", "4 samples", "8 samples"};
      const int sampleValues[] = {1, 2, 4, 8};
      int sampleIndex = state.rasterization.samples == 1 ? 0 : state.rasterization.samples == 2 ? 1 : state.rasterization.samples == 4 ? 2 : 3;
      if (ImGui::Combo("##samples", &sampleIndex, sampleLabels, 4)) state.rasterization.samples = sampleValues[sampleIndex];
      description("Stores multiple coverage and depth samples per pixel, then resolves them to one color.");
      ImGui::Checkbox("Polygon offset fill", &state.rasterization.polygonOffset);
      ImGui::BeginDisabled(!state.rasterization.polygonOffset);
      ImGui::SliderFloat("Slope factor", &state.rasterization.polygonOffsetFactor, -4.0f, 4.0f, "%.2f");
      ImGui::SliderFloat("Constant units", &state.rasterization.polygonOffsetUnits, -8.0f, 8.0f, "%.2f");
      ImGui::EndDisabled();
      description("Offsets generated depth values by a slope-dependent term plus a minimum-depth-step term.");
      break;
    }
    case Category::Surface: {
      ImGui::TextUnformatted("SURFACE"); ImGui::Separator();
      ImGui::TextUnformatted("Surface visualization");
      const char* visualizationLabels[] = {"Texture", "UV coordinates", "Normals", "Vertex colors", "Tangents", "Bitangents"};
      ImGui::Combo("##visualization", &state.surface.visualization, visualizationLabels, 6);
      description("Selects the mesh attribute used as the surface's base color.");
      ImGui::TextUnformatted("Shading interpolation");
      bool flat = !state.surface.smoothShading;
      if (radioPair("Smooth", "Flat", flat)) state.surface.smoothShading = !flat;
      description("Smooth shading interpolates vertex normals; flat shading uses one face normal per triangle.");
      ImGui::Checkbox("Wireframe overlay", &state.surface.wireframe);
      description("Draws triangle boundaries over the shaded surface.");
      ImGui::Checkbox("Tangent-space normal mapping", &state.surface.normalMapping);
      ImGui::BeginDisabled(!state.surface.normalMapping);
      ImGui::SliderFloat("Normal-map strength", &state.surface.normalStrength, 0.0f, 2.0f, "%.2f");
      ImGui::EndDisabled();
      description("Transforms sampled tangent-space normals into world space with the tangent-bitangent-normal basis.");
      ImGui::TextUnformatted("Transparency operation");
      const char* transparencyLabels[] = {"Opaque", "Alpha test (discard)", "Straight alpha blend",
        "Premultiplied alpha blend", "Additive blend", "Multiply blend"};
      ImGui::Combo("##transparency", &state.surface.transparency, transparencyLabels, 6);
      if (state.surface.transparency == 1)
        ImGui::SliderFloat("Alpha cutoff", &state.surface.alphaCutoff, 0.0f, 1.0f, "%.2f");
      description("Each blend mode configures explicit source and destination factors in the framebuffer blend equation.");
      ImGui::Checkbox("Reverse object draw order", &state.surface.reverseDrawOrder);
      description("Transparent surfaces generally require back-to-front submission because blending is order-dependent.");
      break;
    }
    case Category::Texture:
      ImGui::TextUnformatted("TEXTURE"); ImGui::Separator();
      ImGui::TextUnformatted("Texture filtering");
      radioPair("Bilinear", "Nearest", state.texture.nearestFiltering);
      description("Selects how samples between adjacent texels are reconstructed.");
      ImGui::TextUnformatted("Texture address mode");
      radioPair("Clamp to edge", "Repeat", state.texture.repeat);
      description("Defines how texture coordinates outside the normalized 0-1 range are sampled.");
      ImGui::Checkbox("Mipmapping", &state.texture.mipmapping);
      description("Selects prefiltered, lower-resolution texture levels during minification.");
      ImGui::BeginDisabled(!state.texture.mipmapping || state.texture.nearestFiltering);
      ImGui::Checkbox("Trilinear mip interpolation", &state.texture.trilinear);
      ImGui::EndDisabled();
      description("Interpolates between the two nearest mip levels as well as between texels.");
      ImGui::SliderFloat("Anisotropy", &state.texture.anisotropy, 1.0f, 16.0f, "%.0f x");
      description("Uses additional samples to preserve detail when texture footprints are elongated by perspective.");
      break;
    case Category::Lighting: {
      ImGui::TextUnformatted("LIGHTING"); ImGui::Separator();
      ImGui::TextUnformatted("Lighting model");
      const char* lightingLabels[] = {"Unlit", "Gouraud / per-vertex Lambert", "Phong shading / per-fragment Lambert",
        "Phong reflection", "Blinn-Phong reflection"};
      ImGui::Combo("##lighting-model", &state.lighting.model, lightingLabels, 5);
      description("Gouraud interpolates computed vertex lighting; Phong shading interpolates normals and lights each fragment.");
      ImGui::SliderFloat("Ambient term", &state.lighting.ambient, 0.0f, 1.0f, "%.2f");
      if (state.lighting.model >= 3)
        ImGui::SliderFloat("Specular exponent", &state.lighting.shininess, 2.0f, 128.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Light azimuth", &state.lighting.azimuth, -180.0f, 180.0f, "%.0f deg");
      ImGui::SliderFloat("Light elevation", &state.lighting.elevation, -90.0f, 90.0f, "%.0f deg");
      description("Azimuth rotates around the vertical axis; elevation moves above or below the horizon.");
      ImGui::Checkbox("Directional shadow map", &state.lighting.shadows);
      ImGui::BeginDisabled(!state.lighting.shadows);
      const char* shadowResolutionLabels[] = {"256 x 256", "512 x 512", "1024 x 1024", "2048 x 2048"};
      const int shadowResolutions[] = {256, 512, 1024, 2048};
      int shadowResolutionIndex = state.lighting.shadowResolution == 256 ? 0 : state.lighting.shadowResolution == 512 ? 1 :
        state.lighting.shadowResolution == 2048 ? 3 : 2;
      if (ImGui::Combo("Shadow-map resolution", &shadowResolutionIndex, shadowResolutionLabels, 4))
        state.lighting.shadowResolution = shadowResolutions[shadowResolutionIndex];
      ImGui::SliderFloat("Depth comparison bias", &state.lighting.shadowBias, 0.0f, 0.02f, "%.5f", ImGuiSliderFlags_Logarithmic);
      ImGui::Checkbox("3 x 3 percentage-closer filtering", &state.lighting.shadowPcf);
      ImGui::Checkbox("Visualize light-space depth", &state.lighting.visualizeShadowMap);
      ImGui::EndDisabled();
      description("Renders scene depth from the light, then compares each camera fragment against that depth map.");
      break;
    }
    case Category::Depth: {
      ImGui::TextUnformatted("DEPTH"); ImGui::Separator();
      ImGui::Checkbox("Depth testing", &state.depth.testing);
      description("Compares each fragment's depth against the stored depth value before drawing it.");
      ImGui::Checkbox("Depth writes", &state.depth.writing);
      description("Stores passing fragment depths in the depth buffer. Disabling the depth test also prevents writes.");
      ImGui::TextUnformatted("Depth comparison function");
      const char* functionLabels[] = {"Less", "Less or equal", "Greater", "Always"};
      ImGui::Combo("##depth-function", &state.depth.function, functionLabels, 4);
      description("Determines which comparison between incoming and stored depth values passes.");
      ImGui::TextUnformatted("Depth buffer precision");
      const char* depthLabels[] = {"16-bit fixed point", "24-bit fixed point"};
      int selected = state.depth.precision == 16 ? 0 : 1;
      if (ImGui::Combo("##depth-precision", &selected, depthLabels, 2)) state.depth.precision = selected == 0 ? 16 : 24;
      description("Sets the actual storage precision of the framebuffer's depth attachment.");
      ImGui::TextUnformatted("Depth visualization");
      const char* viewLabels[] = {"Off", "Raw window-space depth", "Linear camera depth (0-10 units)"};
      ImGui::Combo("##depth-view", &state.depth.visualization, viewLabels, 3);
      description("Raw perspective depth is nonlinear; linearization reconstructs camera-space distance.");
      break;
    }
    case Category::Stencil:
      ImGui::TextUnformatted("STENCIL"); ImGui::Separator();
      ImGui::Checkbox("Two-pass stencil mask", &state.stencil.enabled);
      description("First pass writes a projected sphere silhouette while color and depth writes are disabled.");
      ImGui::SliderInt("Reference value", &state.stencil.reference, 0, 255);
      radioPair("Equal", "Not equal", state.stencil.invert);
      description("Second pass compares each stored 8-bit stencil value against the reference before shading.");
      ImGui::TextDisabled("Pass 1: Always / Replace");
      ImGui::TextDisabled("Pass 2: Equal or Not equal / Keep");
      ImGui::TextDisabled("Attachment: DEPTH24_STENCIL8");
      description("Select the Stencil mask scene to isolate this operation.");
      break;
    case Category::Color: {
      ImGui::TextUnformatted("COLOR"); ImGui::Separator();
      ImGui::TextUnformatted("Output color depth");
      const char* labels[] = {"24-bit (8:8:8)", "15-bit (5:5:5)", "12-bit (4:4:4)"};
      int selected = state.color.bitsPerChannel == 8 ? 0 : state.color.bitsPerChannel == 5 ? 1 : 2;
      if (ImGui::Combo("##depth", &selected, labels, 3)) state.color.bitsPerChannel = selected == 0 ? 8 : selected == 1 ? 5 : 4;
      description("Quantizes each output color channel to a fixed number of levels.");
      ImGui::Checkbox("Ordered dithering (4 x 4 Bayer)", &state.color.dithering);
      description("Offsets pixels with a fixed threshold matrix before color quantization.");
      ImGui::TextUnformatted("Lighting color space");
      radioPair("Encoded RGB (incorrect)", "Linear light", state.color.linearLight);
      description("Linear-light mode decodes texture values before lighting and encodes the final image for display.");
      break;
    }
    case Category::Post:
      ImGui::TextUnformatted("POST"); ImGui::Separator();
      ImGui::Checkbox("Linear distance fog", &state.post.fog);
      description("Blends shaded fragments toward the background according to camera distance.");
      ImGui::SliderFloat("Fog start", &state.post.fogStart, 0.0f, 12.0f, "%.2f units");
      ImGui::SliderFloat("Fog end", &state.post.fogEnd, 0.0f, 12.0f, "%.2f units");
      description("Start is fully clear; end is fully fogged.");
      ImGui::Checkbox("Overdraw visualization", &state.post.overdraw);
      ImGui::BeginDisabled(!state.post.overdraw);
      ImGui::SliderFloat("Heat-map maximum", &state.post.overdrawRange, 1.0f, 32.0f, "%.0f fragments");
      ImGui::EndDisabled();
      description("An additive floating-point pass counts rasterized fragments with depth testing disabled.");
      break;
    case Category::Output: {
      ImGui::TextUnformatted("OUTPUT"); ImGui::Separator();
      ImGui::TextUnformatted("Internal render resolution");
      const char* labels[] = {"1280 x 960", "640 x 480", "320 x 240", "256 x 192", "160 x 120"};
      const int widths[] = {1280, 640, 320, 256, 160};
      const int heights[] = {960, 480, 240, 192, 120};
      int selected = 1;
      for (int i = 0; i < 5; ++i) if (state.output.width == widths[i] && state.output.height == heights[i]) selected = i;
      if (ImGui::Combo("##resolution", &selected, labels, 5)) { state.output.width = widths[selected]; state.output.height = heights[selected]; }
      description("Scene and output passes render at this exact pixel resolution.");
      ImGui::TextUnformatted("Viewport upscaling");
      radioPair("Bilinear", "Nearest", state.output.nearestUpscaling);
      description("Filters the completed internal-resolution framebuffer when enlarging it to the viewport.");
      break;
    }
  }
}

const char* categoryName(Category category) {
  switch (category) {
    case Category::Geometry: return "Geometry";
    case Category::Camera: return "Camera";
    case Category::Rasterization: return "Rasterization";
    case Category::Surface: return "Surface";
    case Category::Texture: return "Texture";
    case Category::Lighting: return "Lighting";
    case Category::Depth: return "Depth";
    case Category::Stencil: return "Stencil";
    case Category::Color: return "Color";
    case Category::Post: return "Post";
    case Category::Output: return "Output";
  }
  return "";
}

void glfwError(int, const char* descriptionText) { std::fprintf(stderr, "GLFW: %s\n", descriptionText); }

} // namespace

int main() {
  glfwSetErrorCallback(glfwError);
  if (!glfwInit()) fail("GLFW initialization failed");
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 0);
  GLFWwindow* window = glfwCreateWindow(1440, 900, "Graphics Lab", nullptr, nullptr);
  if (!window) fail("window creation failed");
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) fail("OpenGL function loading failed");
  glGetError();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  setStyle();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 410 core");

  Renderer renderer;
  RendererState current;
  RendererState reference = current;
  CameraOrbit camera;
  Category category = Category::Geometry;
  TestScene scene = TestScene::Torus;
  CompareMode compare = CompareMode::A;
  bool viewportHovered = false;
  double configCopiedAt = -10.0;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    if (viewportHovered) {
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        camera.yaw -= io.MouseDelta.x * 0.008f;
        camera.pitch = std::clamp(camera.pitch + io.MouseDelta.y * 0.008f, -1.45f, 1.45f);
      }
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        const glm::mat4 inverseView = glm::inverse(camera.view());
        const glm::vec3 right = glm::vec3(inverseView[0]);
        const glm::vec3 up = glm::vec3(inverseView[1]);
        camera.target += (-right * io.MouseDelta.x + up * io.MouseDelta.y) * camera.distance * 0.0015f;
      }
      if (io.MouseWheel != 0.0f) camera.distance = std::clamp(camera.distance * std::pow(0.88f, io.MouseWheel), 1.4f, 14.0f);
    }

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Graphics Lab", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("GRAPHICS LAB");
    ImGui::SameLine(150);
    ImGui::SetNextItemWidth(180.0f);
    const char* sceneLabels[] = {"Torus", "Texture minification", "Depth precision", "Transparency", "Lighting comparison", "Stencil mask"};
    int sceneIndex = static_cast<int>(scene);
    if (ImGui::Combo("##test-scene", &sceneIndex, sceneLabels, 6)) scene = static_cast<TestScene>(sceneIndex);
    ImGui::SameLine();
    if (ImGui::Button("Reset neutral")) current = RendererState{};
    ImGui::SameLine();
    if (ImGui::Button("Reset scene setup")) applyRecommendedSetup(scene, current, camera);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Apply the recommended renderer state and camera framing for the selected scene.");
    ImGui::SameLine();
    if (ImGui::Button("Copy A to B")) reference = current;
    ImGui::SameLine();
    if (ImGui::Button("Copy config JSON")) {
      const std::string exportedConfig = configJson(current, camera, scene);
      ImGui::SetClipboardText(exportedConfig.c_str());
      configCopiedAt = glfwGetTime();
    }
    if (glfwGetTime() - configCopiedAt < 2.0) {
      ImGui::SameLine();
      ImGui::TextDisabled("Copied");
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Compare:");
    ImGui::SameLine();
    if (ImGui::RadioButton("A", compare == CompareMode::A)) compare = CompareMode::A;
    ImGui::SameLine();
    if (ImGui::RadioButton("B", compare == CompareMode::B)) compare = CompareMode::B;
    ImGui::SameLine();
    if (ImGui::RadioButton("Split A/B", compare == CompareMode::Split)) compare = CompareMode::Split;
    ImGui::SameLine(ImGui::GetWindowWidth() - 260);
    ImGui::TextDisabled("LMB orbit   MMB/RMB pan   Wheel zoom");
    ImGui::Separator();

    const float contentHeight = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("Pipeline", ImVec2(145, contentHeight), true);
    ImGui::TextDisabled("PIPELINE");
    ImGui::Spacing();
    constexpr std::array<Category, 11> categories = {Category::Geometry, Category::Camera, Category::Rasterization,
      Category::Surface, Category::Texture, Category::Lighting, Category::Depth, Category::Stencil, Category::Color,
      Category::Post, Category::Output};
    for (Category candidate : categories) {
      if (ImGui::Selectable(categoryName(candidate), category == candidate, 0, ImVec2(0, 28))) category = candidate;
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 5);

    const float inspectorWidth = 310;
    const float viewportWidth = std::max(100.0f, ImGui::GetContentRegionAvail().x - inspectorWidth - 5);
    ImGui::BeginChild("Viewport", ImVec2(viewportWidth, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 paneOrigin = ImGui::GetCursorScreenPos();
    constexpr float cameraWidth = 960.0f;
    constexpr float cameraHeight = 720.0f;
    const float presentationScale = std::min(available.x / cameraWidth, available.y / cameraHeight);
    const ImVec2 presentationSize(cameraWidth * presentationScale, cameraHeight * presentationScale);
    const ImVec2 origin(
      paneOrigin.x + std::floor((available.x - presentationSize.x) * 0.5f),
      paneOrigin.y + std::floor((available.y - presentationSize.y) * 0.5f));
    const ImVec2 end(origin.x + presentationSize.x, origin.y + presentationSize.y);
    const GLuint textureA = renderer.render(current, camera, scene, false);
    const GLuint textureB = (compare == CompareMode::A) ? 0 : renderer.render(reference, camera, scene, true);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, IM_COL32(27, 29, 31, 255));
    if (compare == CompareMode::Split) {
      const float middle = origin.x + std::floor(presentationSize.x * 0.5f);
      draw->PushClipRect(origin, ImVec2(middle, end.y), true);
      draw->AddImage(static_cast<ImTextureID>(textureA), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      draw->PopClipRect();
      draw->PushClipRect(ImVec2(middle + 1, origin.y), end, true);
      draw->AddImage(static_cast<ImTextureID>(textureB), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      draw->PopClipRect();
      draw->AddLine(ImVec2(middle, origin.y), ImVec2(middle, end.y), IM_COL32(225, 225, 225, 210));
      draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240,240,240,220), "A  CURRENT");
      draw->AddText(ImVec2(middle + 10, origin.y + 8), IM_COL32(240,240,240,220), "B  REFERENCE");
    } else {
      const GLuint texture = compare == CompareMode::A ? textureA : textureB;
      draw->AddImage(static_cast<ImTextureID>(texture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240,240,240,220), compare == CompareMode::A ? "A  CURRENT" : "B  REFERENCE");
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("viewport-input", presentationSize);
    viewportHovered = ImGui::IsItemHovered();
    ImGui::EndChild();
    ImGui::SameLine(0, 5);

    ImGui::BeginChild("Inspector", ImVec2(inspectorWidth, contentHeight), true);
    inspector(category, current);
    ImGui::EndChild();
    ImGui::End();

    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.08f, 0.085f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
