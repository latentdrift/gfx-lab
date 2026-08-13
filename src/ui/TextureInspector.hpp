#pragma once

#include "ui/Workspace.hpp"

#include <string_view>

namespace gfxlab::ui {

void drawTextureInspector(bool& open, const ViewportImages& images, std::string_view selectedPassName);

} // namespace gfxlab::ui
