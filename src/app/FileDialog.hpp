#pragma once

#include <optional>
#include <string>

namespace gfxlab {

struct FileDialogResult {
  std::optional<std::string> path;
  std::string error;
};

[[nodiscard]] FileDialogResult openModelFileDialog();
[[nodiscard]] FileDialogResult openTextureFileDialog();
[[nodiscard]] FileDialogResult saveViewportRecordingDialog();
[[nodiscard]] FileDialogResult openStackDocumentDialog();
[[nodiscard]] FileDialogResult saveStackDocumentDialog();

} // namespace gfxlab
