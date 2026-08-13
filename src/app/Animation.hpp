#pragma once

#include <glm/glm.hpp>

#include <cstddef>
#include <string_view>
#include <vector>

namespace gfxlab {

struct RenderPass;
class RenderStack;

enum class KeyframeInterpolation { Step, Linear, SmoothStep };
enum class AnimationValueKind { Float, Vec2, Vec3, Color3, Color4, Angle, Boolean, Integer, Enumeration };
enum class AnimationBehavior { Continuous, Step, NotAnimatable };

enum class AnimationProperty {
  PassEnabled,
  PassOutput,
  TextureSource,
  TextureColorInterpretation,
  ModelTranslation,
  ModelScale,
  NormalInflation,
  UvOffset,
  UvScale,
  UvRotation,
  UvPivot,
  UvMapping,
  CameraYaw,
  CameraPitch,
  CameraDistance,
  CameraLateral,
  StereoConvergence,
  FieldOfViewOffset,
  CompositeGain,
  CompositeBias,
  CompositeOpacity,
  CompositeOperation,
  CompositeSourceA,
  CompositeSourceB,
  CompositeSourceAPass,
  CompositeSourceBPass,
  CompositeFixedColor,
  CompositeBitDepth,
  CompositeHistoryDecay,
  CompositeHistoryUvOffset,
  CompositeHistoryUvScale,
  CompositeColorSpace,
  CompositeRange,
  CompositeMask,
  CompositeMaskInverted,
  VertexQuantization,
  ClippingEnabled,
  ClippingHeight,
  ClippingKeepAbove,
  ProjectionOrthographic,
  FieldOfView,
  OrthographicSize,
  NearPlane,
  AffineMapping,
  CullMode,
  MultisampleCount,
  PolygonOffsetEnabled,
  PolygonOffsetFactor,
  PolygonOffsetUnits,
  SmoothShading,
  WireframeOverlay,
  SurfaceVisualization,
  NormalMappingEnabled,
  NormalStrength,
  TransparencyOperation,
  AlphaCutoff,
  ReverseDrawOrder,
  NearestFiltering,
  TextureRepeat,
  Mipmapping,
  TrilinearFiltering,
  Anisotropy,
  TextureColorStorage,
  LightingModel,
  Ambient,
  LightAzimuth,
  LightElevation,
  Shininess,
  ShadowsEnabled,
  ShadowResolution,
  ShadowBias,
  ShadowPcf,
  ShadowMapVisualization,
  DepthCueEnabled,
  DepthCueStart,
  DepthCueEnd,
  FarColor,
  FieldEnabled,
  FieldProducerKind,
  FieldSourceA,
  FieldSourceB,
  FieldWavelength,
  FieldPhaseOffset,
  FieldAmplitudeA,
  FieldAmplitudeB,
  FieldFalloff,
  FieldBandSharpness,
  FieldVisualization,
  FieldLowColor,
  FieldHighColor,
  FieldVertexDisplacement,
  FieldSignedDisplacement,
  FieldDiscardEnabled,
  FieldDiscardThreshold,
  FieldSurfaceColorInfluence,
  FieldEmissionInfluence,
  SdfAType,
  SdfAPosition,
  SdfAParameters,
  SdfBType,
  SdfBPosition,
  SdfBParameters,
  SdfOperation,
  SdfSmoothness,
  SdfPreviewRange,
  IsoSurfaceEnabled,
  IsoLevel,
  IsoColor,
  IsoMaximumSteps,
  IsoHitEpsilon,
  IsoMaximumDistance,
  DepthTestEnabled,
  DepthWriteEnabled,
  DepthPrecision,
  DepthComparison,
  DepthVisualization,
  OrderingTableEnabled,
  OrderingBuckets,
  StencilEnabled,
  StencilInverted,
  StencilReference,
  BitsPerChannel,
  DitheringEnabled,
  LinearLight,
  FogEnabled,
  FogStart,
  FogEnd,
  OverdrawEnabled,
  OverdrawRange,
  InternalResolution,
  NearestUpscaling,
  N64CycleType,
  N64Cycle0A,
  N64Cycle0B,
  N64Cycle0C,
  N64Cycle0D,
  N64Cycle1A,
  N64Cycle1B,
  N64Cycle1C,
  N64Cycle1D,
  PrimitiveColor,
  EnvironmentColor,
  N64TextureFormat,
  N64TextureFilter,
  N64MipmapMode,
  N64TileSize,
  N64MirrorS,
  N64MirrorT,
  N64ShiftS,
  N64ShiftT,
  N64TextureGeneration,
  N64SurfaceMode,
  N64ZCompare,
  N64ZUpdate,
  N64AlphaCompare,
  AlphaThreshold,
  N64CoverageAntialiasing,
  N64FramebufferFormat,
  N64ColorDither,
  N64ViReconstruction,
  N64ViDivot,
  Count,
};

struct AnimationPropertyInfo {
  std::string_view id;
  std::string_view label;
  std::string_view group;
  int components = 1;
  AnimationValueKind kind = AnimationValueKind::Float;
  AnimationBehavior behavior = AnimationBehavior::Continuous;
  float minimum = 0.0f;
  float maximum = 1.0f;
};

struct PropertyKeyframe {
  float timeSeconds = 0.0f;
  glm::vec4 value{0.0f};
};

struct PropertyAnimationTrack {
  AnimationProperty property = AnimationProperty::ModelTranslation;
  KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
  std::vector<PropertyKeyframe> keyframes;
};

struct PassAnimation {
  bool enabled = true;
  std::vector<PropertyAnimationTrack> tracks;
};

struct AnimationTimeline {
  float timeSeconds = 0.0f;
  float durationSeconds = 4.0f;
  float playbackRate = 1.0f;
  bool playing = false;
  bool loop = true;
  bool autoKey = false;
  bool showAllPasses = false;
  bool snapToFrames = true;
  int framesPerSecond = 24;

  void advance(float deltaSeconds);
};

void recordPropertyAnimationEdit(RenderPass& pass, AnimationProperty property,
  AnimationTimeline& timeline, bool valueChanged);

[[nodiscard]] const AnimationPropertyInfo& animationPropertyInfo(AnimationProperty property);
[[nodiscard]] bool animationPropertyIsAnimatable(AnimationProperty property);
[[nodiscard]] bool animationPropertyValuesEqual(AnimationProperty property, const glm::vec4& a,
  const glm::vec4& b);
[[nodiscard]] const char* animationPropertyDiscreteValueLabel(AnimationProperty property, int value);
[[nodiscard]] glm::vec4 animationPropertyValue(const RenderPass& pass, AnimationProperty property);
void setAnimationPropertyValue(RenderPass& pass, AnimationProperty property, const glm::vec4& value);
[[nodiscard]] PropertyAnimationTrack* findPropertyTrack(RenderPass& pass, AnimationProperty property);
[[nodiscard]] const PropertyAnimationTrack* findPropertyTrack(const RenderPass& pass, AnimationProperty property);
[[nodiscard]] glm::vec4 samplePropertyTrack(const PropertyAnimationTrack& track, float timeSeconds);
void setPropertyKeyframe(RenderPass& pass, AnimationProperty property, float timeSeconds,
  const glm::vec4* explicitValue = nullptr);
[[nodiscard]] bool removePropertyKeyframe(RenderPass& pass, AnimationProperty property, float timeSeconds,
  float toleranceSeconds = 1.0f / 120.0f);
[[nodiscard]] std::size_t propertyKeyframeIndexNear(const RenderPass& pass, AnimationProperty property,
  float timeSeconds, float toleranceSeconds = 1.0f / 120.0f);
[[nodiscard]] bool propertyHasKeyAt(const RenderPass& pass, AnimationProperty property, float timeSeconds,
  float toleranceSeconds = 1.0f / 120.0f);
[[nodiscard]] RenderPass evaluateRenderPass(const RenderPass& source, float timeSeconds);
[[nodiscard]] RenderStack evaluateRenderStack(const RenderStack& source, float timeSeconds);

} // namespace gfxlab
