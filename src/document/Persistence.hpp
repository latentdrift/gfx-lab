#pragma once

#include "document/Document.hpp"

#include <optional>
#include <string>

namespace gfxlab::document {

struct DocumentLoadResult {
  std::optional<Document> document;
  std::string error;
  [[nodiscard]] explicit operator bool() const { return document.has_value(); }
};

[[nodiscard]] std::string documentJson(const Document& document);
[[nodiscard]] bool saveDocumentFile(const std::string& path, const Document& document,
  std::string& error);
[[nodiscard]] DocumentLoadResult loadDocumentFile(const std::string& path);

} // namespace gfxlab::document
