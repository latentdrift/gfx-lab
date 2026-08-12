#pragma once

namespace gfxlab {

struct RendererState;
enum class Category;

enum class HardwareProfile { Unrestricted, PlayStation, Nintendo64 };

struct ProfileCapabilities {
  bool projectionModes = true;
  bool multisampling = true;
  bool polygonOffset = true;
  bool surfaceDiagnostics = true;
  bool smoothAndFlatNormals = true;
  bool wireframeOverlay = true;
  bool normalMapping = true;
  bool generalBlendModes = true;
  bool textureFiltering = true;
  bool mipmapping = true;
  bool anisotropy = true;
  bool perFragmentLighting = true;
  bool shadowMapping = true;
  bool depthBuffer = true;
  bool stencilBuffer = true;
  bool configurableColorDepth = true;
  bool linearLight = true;
  bool fragmentFog = true;
  bool overdrawAnalysis = true;
};

const char* hardwareProfileName(HardwareProfile profile);
const char* hardwareProfileId(HardwareProfile profile);
const char* hardwareProfileDescription(HardwareProfile profile);
const ProfileCapabilities& hardwareProfileCapabilities(HardwareProfile profile);
void normalizeForHardwareProfile(HardwareProfile profile, RendererState& state);
bool categoryAvailableForHardwareProfile(HardwareProfile profile, Category category);

} // namespace gfxlab
