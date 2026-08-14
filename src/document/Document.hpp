#pragma once

#include "app/HardwareProfile.hpp"
#include "assets/ModelAsset.hpp"
#include "document/Operations.hpp"
#include "document/Properties.hpp"

#include <memory>
#include <optional>
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
  struct Timeline {
    float currentTimeSeconds = 0.0f;
    float durationSeconds = 4.0f;
    float playbackRate = 1.0f;
    bool loop = true;
    bool autoKey = false;
    bool showAllOperations = false;
    bool snapToFrames = true;
    int framesPerSecond = 24;
  } timeline;
  std::vector<AnimationTrack> animation;
  std::vector<ModulationRoute> modulation;
};

struct Presentation {
  SignalRef input;
  DisplayReconstructionState reconstruction;
};

struct Document {
  std::uint64_t nextOperationIdentity = 1;
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
[[nodiscard]] OperationId nextOperationId(const Document& document);
[[nodiscard]] std::optional<OperationId> operationFromObject(ObjectId object);
[[nodiscard]] Document makeDefaultDocument();

} // namespace gfxlab::document
