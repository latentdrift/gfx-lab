#pragma once

namespace gfxlab::ui {

// Keeps a floating tool recoverable when ImGui restores coordinates from a monitor layout that no longer exists.
// Call after a successful Begin(). Docked windows are intentionally left to the dock builder.
void keepCurrentWindowVisible();

} // namespace gfxlab::ui
