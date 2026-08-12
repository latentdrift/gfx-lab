#pragma once

#include "app/State.hpp"
#include "app/HardwareProfile.hpp"

namespace gfxlab::ui {

void setStyle();
void drawInspector(Category category, RendererState& state, HardwareProfile profile);
const char* categoryName(Category category);

} // namespace gfxlab::ui
