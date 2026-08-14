#pragma once

#include "handbook/Handbook.hpp"
#include "app/HardwareProfile.hpp"

#include <glm/glm.hpp>

#include <string>

namespace gfxlab {

struct RendererState {
  struct CombinerCycle { int a = 1; int b = 0; int c = 3; int d = 0; };
  struct Geometry { float vertexQuantization = 0.0f; bool clipping = false; float clipHeight = 0.0f; bool clipAbove = false; } geometry;
  struct Camera { float fieldOfView = 45.0f; float nearPlane = 0.05f; bool orthographic = false; float orthographicSize = 4.0f; } camera;
  struct Rasterization { bool affineMapping = false; int cullMode = 1; int samples = 1; bool polygonOffset = false; float polygonOffsetFactor = 1.0f; float polygonOffsetUnits = 1.0f; } rasterization;
  struct Surface { bool smoothShading = true; bool wireframe = false; int visualization = 0; bool normalMapping = false; float normalStrength = 1.0f; int transparency = 0; float alphaCutoff = 0.5f; bool reverseDrawOrder = false; } surface;
  struct Texture { bool nearestFiltering = false; bool repeat = true; bool mipmapping = false; bool trilinear = false; float anisotropy = 1.0f; int colorMode = 0; } texture;
  struct Lighting { int model = 2; float ambient = 0.22f; float azimuth = 34.0f; float elevation = 52.0f; float shininess = 32.0f; bool shadows = false; int shadowResolution = 1024; float shadowBias = 0.002f; bool shadowPcf = true; bool visualizeShadowMap = false; bool depthCue = false; float depthCueStart = 3.0f; float depthCueEnd = 7.0f; glm::vec3 farColor{0.12f, 0.16f, 0.22f}; } lighting;
  struct Field {
    struct SdfProducer {
      int type = 0;
      glm::vec3 position{0.0f};
      glm::vec3 parameters{1.0f, 0.35f, 0.35f};
    };
    bool enabled = false;
    int producerKind = 0;
    glm::vec3 sourceA{-1.35f, 0.0f, 0.0f};
    glm::vec3 sourceB{1.35f, 0.0f, 0.0f};
    float wavelength = 0.72f;
    float phaseOffset = 0.0f;
    float amplitudeA = 1.0f;
    float amplitudeB = 1.0f;
    float falloff = 0.08f;
    float bandSharpness = 1.35f;
    int visualization = 3;
    glm::vec3 lowColor{0.005f, 0.01f, 0.025f};
    glm::vec3 highColor{1.0f, 0.78f, 0.42f};
    float vertexDisplacement = 0.0f;
    bool signedDisplacement = false;
    bool discardBelowEnabled = false;
    float discardThreshold = 0.5f;
    float surfaceColorInfluence = 0.0f;
    float emissionInfluence = 0.0f;
    SdfProducer sdfA{0, {-0.85f, 0.0f, 0.0f}, {1.15f, 0.35f, 0.35f}};
    SdfProducer sdfB{2, {0.85f, 0.0f, 0.0f}, {0.85f, 0.28f, 0.28f}};
    int sdfOperation = 3;
    float sdfSmoothness = 0.45f;
    float sdfPreviewRange = 1.5f;
    bool isoSurfaceEnabled = false;
    float isoLevel = 0.0f;
    int isoMaxSteps = 128;
    float isoEpsilon = 0.002f;
    float isoMaxDistance = 30.0f;
    glm::vec3 isoColor{0.72f, 0.82f, 1.0f};
  } field;
  struct Spectral { int illuminant = 0; int observer = 0; float exposure = 0.0f; } spectral;
  struct Depth { bool testing = true; bool writing = true; int precision = 24; int function = 0; int visualization = 0; bool orderingTable = false; int orderingBuckets = 32; } depth;
  struct Stencil { bool enabled = false; bool invert = false; int reference = 1; } stencil;
  struct Color { int bitsPerChannel = 8; bool dithering = false; bool linearLight = true; } color;
  struct Post { bool fog = false; float fogStart = 3.0f; float fogEnd = 7.0f; bool overdraw = false; float overdrawRange = 8.0f; } post;
  struct Output { int width = 640; int height = 480; bool nearestUpscaling = true; } output;
  struct N64 {
    bool enabled = false;
    int cycleType = 1;
    CombinerCycle cycle0{};
    CombinerCycle cycle1{7, 0, 2, 0};
    glm::vec4 primitiveColor{1.0f};
    glm::vec4 environmentColor{0.18f, 0.24f, 0.30f, 1.0f};
    int textureFormat = 0;
    int textureFilter = 1;
    int mipmapMode = 0;
    int tileWidth = 32;
    int tileHeight = 32;
    bool mirrorS = false;
    bool mirrorT = false;
    int shiftS = 0;
    int shiftT = 0;
    bool textureGeneration = false;
    int surfaceMode = 0;
    bool zCompare = true;
    bool zUpdate = true;
    int alphaCompare = 0;
    float alphaThreshold = 0.5f;
    bool coverageAntialiasing = true;
    int framebufferFormat = 0;
    int colorDither = 1;
    bool viReconstruction = true;
    bool viDivot = true;
  } n64;
};

enum class Category { Geometry, Camera, Rasterization, Surface, Texture, Lighting, Field, Spectral, Depth, Stencil, Color, Post, Output };
enum class CompareMode { A, B, Split, Relation, StereoPair };
enum class RelationOperator {
  AbsoluteDifference,
  SignedDifference,
  PositiveAMinusB,
  PositiveBMinusA,
  Multiply,
  Screen,
  Exclusion,
  Minimum,
  Maximum,
  ANotB,
  CenteredSum,
  RelativeDifference,
  Add,
  Average,
  HardwareSubtract,
  HardwareReverseSubtract,
  QuarterAdd,
  SignedColorOffset,
  BitwiseXor,
  Normal,
};
enum class TestScene { Torus, TexturePlane, DepthPrecision, Transparency, Lighting, StencilMask,
  FieldInterference, SdfIsoSurface, SpectralMetamers, ElementalChamber, ImportedModel };

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
std::string configJson(const RendererState& state, const CameraOrbit& camera, TestScene scene,
  HardwareProfile profile);
std::string relationConfigJson(const RendererState& a, const RendererState& b, const CameraOrbit& camera,
  TestScene scene, HardwareProfile profile, RelationOperator operation, float gain, float bias);

} // namespace gfxlab
