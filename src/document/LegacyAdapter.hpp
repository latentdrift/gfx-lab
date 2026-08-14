#pragma once

#include "app/StackDocument.hpp"
#include "document/Document.hpp"

namespace gfxlab::document {

// One-way v8 import boundary. Old stack documents are translated once into a
// typed Document; no legacy state survives in the editor or evaluator.
[[nodiscard]] Document migrateLegacyDocument(const StackDocument& legacy);

} // namespace gfxlab::document
