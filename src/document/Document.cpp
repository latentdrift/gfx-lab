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

OperationId nextOperationId(const Document& document) {
  return {document.nextOperationIdentity};
}

std::optional<OperationId> operationFromObject(const ObjectId object) {
  if (object.kind != ObjectKind::Operation || object.value == 0) return std::nullopt;
  return OperationId{object.value};
}

Document makeDefaultDocument() {
  Document result;
  Operation a = makeRenderOperation({1}, "Base Render");
  Operation b = makeRenderOperation({2}, "Variant");
  Operation composite = makeCompositeOperation({3}, "Composite",
    primaryOutput(a), primaryOutput(b));
  result.operations.push_back(std::move(a));
  result.operations.push_back(std::move(b));
  result.operations.push_back(std::move(composite));
  result.presentation.input = primaryOutput(result.operations.back());
  result.nextOperationIdentity = 4;
  result.scene.cameraAuthored = true;
  return result;
}

} // namespace gfxlab::document
