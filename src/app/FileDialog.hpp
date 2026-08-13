#pragma once

#include <optional>
#include <string>

namespace gfxlab {

struct FileDialogResult {
  std::optional<std::string> path;
  std::string error;
};

[[nodiscard]] FileDialogResult openModelFileDialog();

} // namespace gfxlab
