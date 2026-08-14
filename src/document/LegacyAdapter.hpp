#pragma once

#include "app/StackDocument.hpp"
#include "document/Document.hpp"

namespace gfxlab::document {

// v8 remains a supported import format while the editor migrates to the typed
// document model. The adapter resolves accumulator/current-pass vocabulary into
// explicit signal references and extracts animation/modulation from pass data.
[[nodiscard]] Document migrateLegacyDocument(const StackDocument& legacy);

} // namespace gfxlab::document
