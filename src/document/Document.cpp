#include "document/Document.hpp"

#include <algorithm>

namespace gfxlab::document {

const Operation* findOperation(const Document& document, const OperationId id) {
  const auto found = std::find_if(document.operations.begin(), document.operations.end(),
    [id](const Operation& operation) { return operation.id == id; });
  return found == document.operations.end() ? nullptr : &*found;
}

Operation* findOperation(Document& document, const OperationId id) {
  const auto found = std::find_if(document.operations.begin(), document.operations.end(),
    [id](const Operation& operation) { return operation.id == id; });
  return found == document.operations.end() ? nullptr : &*found;
}

const SignalDescriptor* findSignal(const Document& document, const SignalId id) {
  for (const Operation& operation : document.operations) {
    const auto found = std::find_if(operation.outputs.begin(), operation.outputs.end(),
      [id](const SignalDescriptor& signal) { return signal.id == id; });
    if (found != operation.outputs.end()) return &*found;
  }
  return nullptr;
}

} // namespace gfxlab::document
