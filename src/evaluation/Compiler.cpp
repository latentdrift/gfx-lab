#include "evaluation/Compiler.hpp"

#include <algorithm>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>

namespace gfxlab::evaluation {
namespace {

void addInput(std::vector<document::SignalRef>& inputs, const document::SignalRef input) {
  if (input) inputs.push_back(input);
}

std::vector<document::SignalRef> operationInputs(const document::Operation& operation) {
  std::vector<document::SignalRef> result;
  std::visit([&](const auto& data) {
    using Type = std::decay_t<decltype(data)>;
    if constexpr (std::is_same_v<Type, document::InterpretOperation>) addInput(result, data.spectrum);
    if constexpr (std::is_same_v<Type, document::CompositeOperation>) {
      addInput(result, data.a);
      addInput(result, data.b);
    }
    if constexpr (std::is_same_v<Type, document::StereoOperation>) {
      addInput(result, data.left);
      addInput(result, data.right);
    }
    if constexpr (std::is_same_v<Type, document::MeasureOperation>) addInput(result, data.input);
  }, operation.data);
  return result;
}

} // namespace

bool EvaluationPlan::valid() const {
  return std::none_of(diagnostics.begin(), diagnostics.end(), [](const OperationDiagnostic& diagnostic) {
    return diagnostic.severity == DiagnosticSeverity::Error;
  });
}

EvaluationPlan compileDocument(const document::Document& document) {
  EvaluationPlan result;
  result.finalSignal = document.presentation.input;
  std::unordered_map<document::SignalId, std::size_t> producerOrder;
  std::unordered_map<document::SignalId, const document::SignalDescriptor*> descriptors;
  std::unordered_set<document::OperationId> operationIds;

  for (std::size_t index = 0; index < document.operations.size(); ++index) {
    const document::Operation& operation = document.operations[index];
    if (!operationIds.insert(operation.id).second)
      result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error, "Duplicate operation ID."});
    for (const document::SignalDescriptor& output : operation.outputs) {
      if (output.producer != operation.id)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Output producer does not match its owning operation."});
      if (!producerOrder.emplace(output.id, index).second)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error, "Duplicate signal ID."});
      descriptors[output.id] = &output;
    }
  }

  for (std::size_t index = 0; index < document.operations.size(); ++index) {
    const document::Operation& operation = document.operations[index];
    EvaluationNode node;
    node.operation = operation.id;
    node.inputs = operationInputs(operation);
    for (const document::SignalDescriptor& output : operation.outputs) node.outputs.push_back(output.id);

    for (const document::SignalRef input : node.inputs) {
      const auto producer = producerOrder.find(input.id);
      if (producer == producerOrder.end()) {
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Input references a signal that is not declared by the document."});
        continue;
      }
      if (input.frameOffset == 0 && producer->second >= index)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Same-frame input must reference an earlier operation."});
      if (input.frameOffset > 0)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "A signal cannot reference a future frame."});
    }

    if (const auto* interpret = std::get_if<document::InterpretOperation>(&operation.data)) {
      const auto descriptor = descriptors.find(interpret->spectrum.id);
      if (descriptor != descriptors.end() && descriptor->second->kind != document::SignalKind::Spectrum16)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Interpret requires a Spectrum16 input."});
    }
    if (const auto* measure = std::get_if<document::MeasureOperation>(&operation.data)) {
      const auto descriptor = descriptors.find(measure->input.id);
      if (descriptor != descriptors.end() && descriptor->second->kind == document::SignalKind::Scalar)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Warning,
          "Measuring an existing scalar is redundant."});
    }
    result.nodes.push_back(std::move(node));
  }

  if (!result.finalSignal || descriptors.find(result.finalSignal.id) == descriptors.end())
    result.diagnostics.push_back({{}, DiagnosticSeverity::Error,
      "Presentation does not reference a valid final signal."});
  return result;
}

} // namespace gfxlab::evaluation
