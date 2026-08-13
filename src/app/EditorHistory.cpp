#include "app/EditorHistory.hpp"

#include <algorithm>

namespace gfxlab {

EditorSnapshot captureEditorSnapshot(const RenderStack& renderStack, const CameraOrbit& camera,
    const TestScene scene, const HardwareProfile hardwareProfile, const AnimationTimeline& timeline) {
  EditorSnapshot snapshot{renderStack, camera, scene, hardwareProfile, timeline};
  snapshot.timeline.timeSeconds = 0.0f;
  snapshot.timeline.playing = false;
  return snapshot;
}

void restoreEditorSnapshot(const EditorSnapshot& snapshot, RenderStack& renderStack, CameraOrbit& camera,
    TestScene& scene, HardwareProfile& hardwareProfile, AnimationTimeline& timeline) {
  const float currentTime = timeline.timeSeconds;
  renderStack = snapshot.renderStack;
  camera = snapshot.camera;
  scene = snapshot.scene;
  hardwareProfile = snapshot.hardwareProfile;
  timeline = snapshot.timeline;
  timeline.timeSeconds = std::clamp(currentTime, 0.0f, timeline.durationSeconds);
  timeline.playing = false;
}

EditorHistory::EditorHistory(const EditorSnapshot& initial)
  : committed_(initial), currentFingerprint_(fingerprint(initial)) {}

void EditorHistory::observe(const EditorSnapshot& current, const bool continuousInteraction) {
  const std::string observedFingerprint = fingerprint(current);
  if (observedFingerprint != currentFingerprint_) {
    if (!transactionStart_.has_value()) transactionStart_ = committed_;
    currentFingerprint_ = observedFingerprint;
  }
  if (transactionStart_.has_value() && !continuousInteraction) finishTransaction(current);
}

bool EditorHistory::undo(const EditorSnapshot& current, EditorSnapshot& restored) {
  if (transactionStart_.has_value()) finishTransaction(current);
  if (undo_.empty()) return false;
  pushBounded(redo_, current);
  restored = undo_.back();
  undo_.pop_back();
  committed_ = restored;
  currentFingerprint_ = fingerprint(restored);
  return true;
}

bool EditorHistory::redo(const EditorSnapshot& current, EditorSnapshot& restored) {
  if (transactionStart_.has_value()) finishTransaction(current);
  if (redo_.empty()) return false;
  pushBounded(undo_, current);
  restored = redo_.back();
  redo_.pop_back();
  committed_ = restored;
  currentFingerprint_ = fingerprint(restored);
  return true;
}

std::string EditorHistory::fingerprint(const EditorSnapshot& snapshot) {
  return renderStackConfigJson(snapshot.renderStack, snapshot.camera, snapshot.scene, snapshot.hardwareProfile,
    &snapshot.timeline);
}

void EditorHistory::finishTransaction(const EditorSnapshot& current) {
  if (!transactionStart_.has_value()) return;
  if (fingerprint(*transactionStart_) != fingerprint(current)) {
    pushBounded(undo_, *transactionStart_);
    redo_.clear();
  }
  committed_ = current;
  currentFingerprint_ = fingerprint(current);
  transactionStart_.reset();
}

void EditorHistory::pushBounded(std::vector<EditorSnapshot>& history, const EditorSnapshot& snapshot) {
  if (history.size() == maximumEntries) history.erase(history.begin());
  history.push_back(snapshot);
}

} // namespace gfxlab
