#pragma once

#include "app/Animation.hpp"
#include "app/RenderStack.hpp"
#include "assets/ModelAsset.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace gfxlab {

struct EditorSnapshot {
  RenderStack renderStack;
  CameraOrbit camera;
  TestScene scene = TestScene::Torus;
  HardwareProfile hardwareProfile = HardwareProfile::Unrestricted;
  AnimationTimeline timeline;
  std::shared_ptr<const ModelAsset> importedModel;
};

[[nodiscard]] EditorSnapshot captureEditorSnapshot(const RenderStack& renderStack, const CameraOrbit& camera,
  TestScene scene, HardwareProfile hardwareProfile, const AnimationTimeline& timeline,
  std::shared_ptr<const ModelAsset> importedModel = nullptr);
void restoreEditorSnapshot(const EditorSnapshot& snapshot, RenderStack& renderStack, CameraOrbit& camera,
  TestScene& scene, HardwareProfile& hardwareProfile, AnimationTimeline& timeline,
  std::shared_ptr<const ModelAsset>* importedModel = nullptr);

class EditorHistory {
public:
  static constexpr std::size_t maximumEntries = 256;

  explicit EditorHistory(const EditorSnapshot& initial);

  void observe(const EditorSnapshot& current, bool continuousInteraction);
  [[nodiscard]] bool undo(const EditorSnapshot& current, EditorSnapshot& restored);
  [[nodiscard]] bool redo(const EditorSnapshot& current, EditorSnapshot& restored);
  [[nodiscard]] bool canUndo() const { return transactionStart_.has_value() || !undo_.empty(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }

private:
  [[nodiscard]] static std::string fingerprint(const EditorSnapshot& snapshot);
  void finishTransaction(const EditorSnapshot& current);
  static void pushBounded(std::vector<EditorSnapshot>& history, const EditorSnapshot& snapshot);

  EditorSnapshot committed_;
  std::string currentFingerprint_;
  std::optional<EditorSnapshot> transactionStart_;
  std::vector<EditorSnapshot> undo_;
  std::vector<EditorSnapshot> redo_;
};

} // namespace gfxlab
