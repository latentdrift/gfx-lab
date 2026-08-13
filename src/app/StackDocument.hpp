#pragma once

#include "app/Animation.hpp"
#include "app/HardwareProfile.hpp"
#include "app/RenderStack.hpp"
#include "assets/ModelAsset.hpp"

#include <memory>
#include <optional>
#include <string>

namespace gfxlab {

struct StackDocument {
  RenderStack renderStack;
  CameraOrbit camera;
  TestScene scene = TestScene::Torus;
  HardwareProfile hardwareProfile = HardwareProfile::Unrestricted;
  AnimationTimeline timeline;
  std::shared_ptr<const ModelAsset> importedModel;
};

struct StackDocumentLoadResult {
  std::optional<StackDocument> document;
  std::string error;
  [[nodiscard]] explicit operator bool() const { return document.has_value(); }
};

[[nodiscard]] StackDocumentLoadResult loadStackDocumentFile(const std::string& path);
[[nodiscard]] bool saveStackDocumentFile(const std::string& path, const std::string& json,
  std::string& error);

} // namespace gfxlab
