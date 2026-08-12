#include "app/HardwareProfile.hpp"

#include "app/State.hpp"

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

} // namespace

const char* hardwareProfileName(HardwareProfile profile) {
  switch (profile) {
    case HardwareProfile::Unrestricted: return "Unrestricted";
    case HardwareProfile::PlayStation: return "PlayStation (PS1)";
  }
  return "Unknown";
}

const char* hardwareProfileId(HardwareProfile profile) {
  switch (profile) {
    case HardwareProfile::Unrestricted: return "unrestricted";
    case HardwareProfile::PlayStation: return "sony_playstation_ps1";
  }
  return "unknown";
}

const char* hardwareProfileDescription(HardwareProfile profile) {
  switch (profile) {
    case HardwareProfile::Unrestricted:
      return "All implemented renderer operations are independently available.";
    case HardwareProfile::PlayStation:
      return "Restricts the lab to PS1-representable operations and normalizes unavailable state.";
  }
  return "";
}

const ProfileCapabilities& hardwareProfileCapabilities(HardwareProfile profile) {
  return profile == HardwareProfile::PlayStation ? playStationCapabilities : unrestrictedCapabilities;
}

void normalizeForHardwareProfile(HardwareProfile profile, RendererState& state) {
  if (profile == HardwareProfile::Unrestricted) return;

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
