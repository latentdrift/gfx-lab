#include "evaluation/Compiler.hpp"

#include <algorithm>
#include <deque>
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
  std::uint64_t maximumOperationId = 0;

  for (std::size_t index = 0; index < document.operations.size(); ++index) {
    const document::Operation& operation = document.operations[index];
    maximumOperationId = std::max(maximumOperationId, operation.id.value);
    if (!operationIds.insert(operation.id).second)
      result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error, "Duplicate operation ID."});
    std::unordered_set<std::string> portKeys;
    for (const document::SignalDescriptor& output : operation.outputs) {
      if (output.producer != operation.id)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Output producer does not match its owning operation."});
      if (!producerOrder.emplace(output.id, index).second)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error, "Duplicate signal ID."});
      if (output.key.empty() || output.id != document::operationSignal(operation.id, output.key))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Output does not use its owning operation and stable port key."});
      if (!portKeys.insert(output.key).second)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error, "Duplicate output port key."});
      descriptors[output.id] = &output;
    }
  }
  if (document.nextOperationIdentity <= maximumOperationId)
    result.diagnostics.push_back({{}, DiagnosticSeverity::Error,
      "The persistent operation allocator would reuse an existing identity."});

  std::vector<EvaluationNode> authoredNodes;
  authoredNodes.reserve(document.operations.size());
  std::vector<std::vector<std::size_t>> dependents(document.operations.size());
  std::vector<std::size_t> dependencyCount(document.operations.size(), 0);
  for (std::size_t index = 0; index < document.operations.size(); ++index) {
    const document::Operation& operation = document.operations[index];
    EvaluationNode node;
    node.operation = operation.id;
    node.inputs = operationInputs(operation);
    for (const document::SignalDescriptor& output : operation.outputs) node.outputs.push_back(output.id);

    for (const document::SignalRef& input : node.inputs) {
      const auto producer = producerOrder.find(input.id);
      if (producer == producerOrder.end()) {
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Input references a signal that is not declared by the document."});
        continue;
      }
      if (input.frameOffset == 0) {
        dependents[producer->second].push_back(index);
        ++dependencyCount[index];
      }
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
    authoredNodes.push_back(std::move(node));
  }

  std::deque<std::size_t> ready;
  for (std::size_t index = 0; index < dependencyCount.size(); ++index)
    if (dependencyCount[index] == 0) ready.push_back(index);
  while (!ready.empty()) {
    const std::size_t index = ready.front();
    ready.pop_front();
    result.nodes.push_back(std::move(authoredNodes[index]));
    for (const std::size_t dependent : dependents[index]) {
      if (--dependencyCount[dependent] == 0) ready.push_back(dependent);
    }
  }
  if (result.nodes.size() != document.operations.size()) {
    result.diagnostics.push_back({{}, DiagnosticSeverity::Error,
      "Same-frame operation connections contain a dependency cycle."});
    result.nodes.clear();
  }

  if (!result.finalSignal || descriptors.find(result.finalSignal.id) == descriptors.end())
    result.diagnostics.push_back({{}, DiagnosticSeverity::Error,
      "Presentation does not reference a valid final signal."});

  const auto validateTarget = [&](const document::PropertyAddress& target,
      const char* context) {
    const document::PropertyDescriptor* property = document::propertyDescriptor(target.property);
    if (property == nullptr) {
      result.diagnostics.push_back({{}, DiagnosticSeverity::Error,
        std::string(context) + " references an unknown property key."});
      return;
    }
    if (target.owner.kind == document::ObjectKind::RenderDefaults) {
      if (!property->availableOnRenderDefaults)
        result.diagnostics.push_back({{}, DiagnosticSeverity::Error,
          std::string(context) + " targets a property unavailable on Render Defaults."});
      return;
    }
    if (target.owner.kind == document::ObjectKind::Scene ||
        target.owner.kind == document::ObjectKind::Presentation) return;
    const std::optional<document::OperationId> operation = document::operationFromObject(target.owner);
    if (!operation.has_value() || operationIds.find(*operation) == operationIds.end())
      result.diagnostics.push_back({operation.value_or(document::OperationId{}),
        DiagnosticSeverity::Error, std::string(context) + " references a missing owner."});
    else if ((target.property == document::timeScaleProperty() ||
        target.property == document::timeOffsetProperty()) &&
        !std::holds_alternative<document::RenderOperation>(
          document::findOperation(document, *operation)->data))
      result.diagnostics.push_back({*operation, DiagnosticSeverity::Error,
        std::string(context) + " targets procedural time on a non-Render operation."});
  };
  for (const document::AnimationTrack& track : document.automation.animation)
    validateTarget(track.target, "Animation");
  for (const document::ModulationRoute& route : document.automation.modulation) {
    validateTarget(route.target, "Modulation");
    if (!route.source || descriptors.find(route.source.id) == descriptors.end())
      result.diagnostics.push_back({{}, DiagnosticSeverity::Error,
        "Modulation references a signal that is not declared by the document."});
    else if (descriptors.at(route.source.id)->kind != document::SignalKind::Scalar)
      result.diagnostics.push_back({route.source.id.producer, DiagnosticSeverity::Error,
        "Modulation requires a Scalar source signal."});
    else if (route.source.frameOffset > 0)
      result.diagnostics.push_back({route.source.id.producer, DiagnosticSeverity::Error,
        "Modulation cannot sample a future signal."});
  }
  return result;
}

} // namespace gfxlab::evaluation
