#pragma once

#include "app/State.hpp"

namespace gfxlab::ui {

void setStyle();
void drawInspector(Category category, RendererState& state);
const char* categoryName(Category category);

} // namespace gfxlab::ui
