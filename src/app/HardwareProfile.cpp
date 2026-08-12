#include "app/HardwareProfile.hpp"

#include "app/State.hpp"

#include <algorithm>

namespace gfxlab {
namespace {

constexpr ProfileCapabilities unrestrictedCapabilities{};

constexpr ProfileCapabilities playStationCapabilities{
  .projectionModes = false,
  .multisampling = false,
  .polygonOffset = false,
  .surfaceDiagnostics = false,
  .smoothAndFlatNormals = false,
  .wireframeOverlay = false,
  .normalMapping = false,
  .generalBlendModes = false,
  .textureFiltering = false,
  .mipmapping = false,
  .anisotropy = false,
  .perFragmentLighting = false,
  .shadowMapping = false,
  .depthBuffer = false,
  .stencilBuffer = false,
  .configurableColorDepth = false,
  .linearLight = false,
  .fragmentFog = false,
  .overdrawAnalysis = false,
};

constexpr ProfileCapabilities nintendo64Capabilities{
  .projectionModes = true,
  .multisampling = false,
  .polygonOffset = false,
  .surfaceDiagnostics = false,
  .smoothAndFlatNormals = false,
  .wireframeOverlay = false,
  .normalMapping = false,
  .generalBlendModes = false,
  .textureFiltering = false,
  .mipmapping = false,
  .anisotropy = false,
  .perFragmentLighting = false,
  .shadowMapping = false,
  .depthBuffer = true,
  .stencilBuffer = false,
  .configurableColorDepth = false,
  .linearLight = false,
  .fragmentFog = false,
  .overdrawAnalysis = false,
};

} // namespace

const char* hardwareProfileName(HardwareProfile profile) {
  switch (profile) {
    case HardwareProfile::Unrestricted: return "Unrestricted";
    case HardwareProfile::PlayStation: return "PlayStation (PS1)";
    case HardwareProfile::Nintendo64: return "Nintendo 64";
  }
  return "Unknown";
}

const char* hardwareProfileId(HardwareProfile profile) {
  switch (profile) {
    case HardwareProfile::Unrestricted: return "unrestricted";
    case HardwareProfile::PlayStation: return "sony_playstation_ps1";
    case HardwareProfile::Nintendo64: return "nintendo_64";
  }
  return "unknown";
}

const char* hardwareProfileDescription(HardwareProfile profile) {
  switch (profile) {
    case HardwareProfile::Unrestricted:
      return "All implemented renderer operations are independently available.";
    case HardwareProfile::PlayStation:
      return "Restricts the lab to PS1-representable operations and normalizes unavailable state.";
    case HardwareProfile::Nintendo64:
      return "Restricts the lab to the standard RSP/RDP pipeline and exposes N64-specific pipeline state.";
  }
  return "";
}

const ProfileCapabilities& hardwareProfileCapabilities(HardwareProfile profile) {
  if (profile == HardwareProfile::PlayStation) return playStationCapabilities;
  if (profile == HardwareProfile::Nintendo64) return nintendo64Capabilities;
  return unrestrictedCapabilities;
}

void normalizeForHardwareProfile(HardwareProfile profile, RendererState& state) {
  state.n64.enabled = profile == HardwareProfile::Nintendo64;
  if (profile == HardwareProfile::Unrestricted) return;

  if (profile == HardwareProfile::Nintendo64) {
    if (state.n64.mipmapMode >= 2) state.n64.cycleType = 2;
    state.n64.tileWidth = std::clamp(state.n64.tileWidth, 16, 64);
    state.n64.tileHeight = std::clamp(state.n64.tileHeight, 16, 64);
    state.rasterization.samples = state.n64.coverageAntialiasing ? 4 : 1;
    state.rasterization.polygonOffset = state.n64.surfaceMode == 2;
    state.rasterization.polygonOffsetFactor = -1.0f;
    state.rasterization.polygonOffsetUnits = -1.0f;
    state.surface.visualization = 0;
    state.surface.smoothShading = true;
    state.surface.wireframe = false;
    state.surface.normalMapping = false;
    state.surface.normalStrength = 1.0f;
    state.surface.transparency = state.n64.surfaceMode == 1 ? 2 : 0;
    state.texture.nearestFiltering = state.n64.textureFilter == 0;
    state.texture.mipmapping = state.n64.mipmapMode != 0;
    state.texture.trilinear = state.n64.mipmapMode >= 2;
    state.texture.anisotropy = 1.0f;
    if (state.lighting.model > 1) state.lighting.model = 1;
    state.lighting.shadows = false;
    state.lighting.visualizeShadowMap = false;
    state.depth.testing = state.n64.zCompare;
    state.depth.writing = state.n64.zUpdate;
    state.depth.function = state.n64.surfaceMode == 2 ? 1 : 0;
    state.depth.visualization = 0;
    state.depth.orderingTable = false;
    state.stencil.enabled = false;
    state.color.bitsPerChannel = state.n64.framebufferFormat == 0 ? 5 : 8;
    state.color.dithering = state.n64.colorDither != 0;
    state.color.linearLight = false;
    state.post.fog = false;
    state.post.overdraw = false;
    if (state.output.width > 640 || state.output.height > 480) {
      state.output.width = 320;
      state.output.height = 240;
    }
    state.output.nearestUpscaling = true;
    return;
  }

  state.camera.orthographic = false;
  state.rasterization.affineMapping = true;
  state.rasterization.samples = 1;
  state.rasterization.polygonOffset = false;

  state.surface.visualization = 0;
  state.surface.smoothShading = true;
  state.surface.wireframe = false;
  state.surface.normalMapping = false;
  state.surface.normalStrength = 1.0f;
  if (state.surface.transparency >= 2 && state.surface.transparency <= 5) {
    constexpr int ps1Equivalents[] = {6, 6, 7, 8};
    state.surface.transparency = ps1Equivalents[state.surface.transparency - 2];
  }

  state.texture.nearestFiltering = true;
  state.texture.mipmapping = false;
  state.texture.trilinear = false;
  state.texture.anisotropy = 1.0f;

  if (state.lighting.model > 1) state.lighting.model = 1;
  state.lighting.shadows = false;
  state.lighting.visualizeShadowMap = false;

  // The lab's ordering table is object-granularity and only meaningful for the
  // transparency scene. Keep conventional opaque visibility stable elsewhere
  // until a true per-polygon ordering path exists.
  state.depth.testing = true;
  state.depth.writing = true;
  state.depth.function = 0;
  state.depth.visualization = 0;

  state.stencil.enabled = false;
  state.color.bitsPerChannel = 5;
  state.color.linearLight = false;
  state.post.fog = false;
  state.post.overdraw = false;

  if (state.output.width > 320 || state.output.height > 240) {
    state.output.width = 320;
    state.output.height = 240;
  }
  state.output.nearestUpscaling = true;
}

bool categoryAvailableForHardwareProfile(HardwareProfile profile, Category category) {
  if (profile == HardwareProfile::Unrestricted) return true;
  return category != Category::Stencil && category != Category::Post;
}

} // namespace gfxlab
