#pragma once

#include "app/HardwareProfile.hpp"
#include "assets/ModelAsset.hpp"
#include "document/Operations.hpp"
#include "document/Properties.hpp"

#include <memory>
#include <vector>

namespace gfxlab::document {

struct Scene {
  TestScene testScene = TestScene::Torus;
  std::shared_ptr<const ModelAsset> importedModel;
  CameraOrbit authoredCamera;
  bool cameraAuthored = false;
};

struct RenderDefaults {
  RendererState renderer;
  TextureBinding texture;
};

struct PropertyAddress {
  ObjectId owner;
  PropertyId property;
  auto operator<=>(const PropertyAddress&) const = default;
};

struct AnimationTrack {
  PropertyAddress target;
  KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
  std::vector<PropertyKeyframe> keyframes;
};

struct ModulationRoute {
  SignalRef source;
  PropertyAddress target;
  glm::vec2 inputRange{0.0f, 1.0f};
  glm::vec2 outputRange{0.0f, 1.0f};
  bool clamp = true;
  float smoothingSeconds = 0.15f;
};

struct Automation {
  std::vector<AnimationTrack> animation;
  std::vector<ModulationRoute> modulation;
};

struct Presentation {
  SignalRef input;
  DisplayReconstructionState reconstruction;
};

struct Document {
  Scene scene;
  HardwareProfile hardwareProfile = HardwareProfile::Unrestricted;
  RenderDefaults renderDefaults;
  std::vector<Operation> operations;
  Automation automation;
  Presentation presentation;
};

[[nodiscard]] const Operation* findOperation(const Document& document, OperationId id);
[[nodiscard]] Operation* findOperation(Document& document, OperationId id);
[[nodiscard]] const SignalDescriptor* findSignal(const Document& document, SignalId id);

} // namespace gfxlab::document
