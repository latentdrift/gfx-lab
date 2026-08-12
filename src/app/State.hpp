#pragma once

#include "handbook/Handbook.hpp"

#include <glm/glm.hpp>

#include <string>

namespace gfxlab {

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

  glm::vec3 eye() const;
  glm::mat4 view() const;
};

const char* testSceneName(TestScene scene);
void applyRecommendedSetup(TestScene scene, RendererState& state, CameraOrbit& camera);
void applyHandbookExample(handbook::Example example, bool alternative, RendererState& state,
  CameraOrbit& camera, TestScene& scene, Category& category);
std::string configJson(const RendererState& state, const CameraOrbit& camera, TestScene scene);

} // namespace gfxlab
