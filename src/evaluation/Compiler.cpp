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
      addInput(result, data.mask);
    }
    if constexpr (std::is_same_v<Type, document::StereoOperation>) {
      addInput(result, data.left);
      addInput(result, data.right);
    }
    if constexpr (std::is_same_v<Type, document::MeasureOperation>) addInput(result, data.input);
    if constexpr (std::is_same_v<Type, document::LuminanceOperation> ||
        std::is_same_v<Type, document::RemapOperation> ||
        std::is_same_v<Type, document::EdgeOperation> ||
        std::is_same_v<Type, document::BlurOperation> ||
        std::is_same_v<Type, document::ThresholdOperation> ||
        std::is_same_v<Type, document::GradientMapOperation>) addInput(result, data.input);
    if constexpr (std::is_same_v<Type, document::WarpOperation>) {
      addInput(result, data.image);
      addInput(result, data.displacement);
    }
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
      if (descriptor != descriptors.end() && descriptor->second->shape != document::SignalShape::Spectrum16)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Interpret requires a Spectrum16 input."});
    }
    const auto descriptorOf = [&](const document::SignalRef signal)
        -> const document::SignalDescriptor* {
      const auto found = descriptors.find(signal.id);
      return found == descriptors.end() ? nullptr : found->second;
    };
    const auto requireInput = [&](const document::SignalRef signal, const char* label) {
      if (!signal) result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
        std::string(label) + " is not connected."});
    };
    if (const auto* composite = std::get_if<document::CompositeOperation>(&operation.data)) {
      requireInput(composite->a, "Composite Input A");
      requireInput(composite->b, "Composite Input B");
      if (composite->mask) {
        const auto* mask = descriptorOf(composite->mask);
        if (mask != nullptr && !document::isScreenScalar(*mask))
          result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
            "Composite Mask requires a screen-space Scalar."});
        else if (mask != nullptr && mask->metadata.semantic != document::SignalSemantic::MaskCoverage)
          result.diagnostics.push_back({operation.id, DiagnosticSeverity::Warning,
            "Composite interprets this Scalar as 0..1 mask coverage."});
      }
    }
    if (const auto* luminance = std::get_if<document::LuminanceOperation>(&operation.data)) {
      requireInput(luminance->input, "Luminance input");
      if (const auto* input = descriptorOf(luminance->input); input != nullptr && !document::isColor(*input))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Luminance requires a Color input."});
    }
    if (const auto* remap = std::get_if<document::RemapOperation>(&operation.data)) {
      requireInput(remap->input, "Remap input");
      const auto* input = descriptorOf(remap->input);
      if (input != nullptr && !document::isScreenScalar(*input))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Remap requires a Scalar image, Depth, or Field input."});
      else if (input != nullptr && (!input->metadata.units.empty() ||
          input->metadata.semantic == document::SignalSemantic::SignedDistance))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Warning,
          "Remap intentionally produces a unitless Scalar and does not preserve the input semantic."});
    }
    if (const auto* edge = std::get_if<document::EdgeOperation>(&operation.data)) {
      requireInput(edge->input, "Edge input");
      const auto* input = descriptorOf(edge->input);
      if (input != nullptr && (!document::isScreenImage(*input) ||
          input->shape == document::SignalShape::Spectrum16))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Edge requires a screen-space image input."});
    }
    if (const auto* blur = std::get_if<document::BlurOperation>(&operation.data)) {
      requireInput(blur->input, "Blur input");
      const auto* input = descriptorOf(blur->input);
      if (input != nullptr && (!document::isScreenImage(*input) || input->shape != blur->outputShape))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Blur requires matching screen-image storage shape."});
      else if (input != nullptr && input->metadata.semantic != blur->outputSemantic)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Warning,
          "Blur performs numeric filtering and strips the input semantic."});
    }
    if (const auto* threshold = std::get_if<document::ThresholdOperation>(&operation.data)) {
      requireInput(threshold->input, "Threshold input");
      if (const auto* input = descriptorOf(threshold->input);
          input != nullptr && !document::isScreenScalar(*input))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Threshold requires a Scalar image input."});
    }
    if (const auto* gradient = std::get_if<document::GradientMapOperation>(&operation.data)) {
      requireInput(gradient->input, "Gradient Map input");
      if (const auto* input = descriptorOf(gradient->input);
          input != nullptr && !document::isScreenScalar(*input))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Gradient Map requires a Scalar image input."});
    }
    if (const auto* warp = std::get_if<document::WarpOperation>(&operation.data)) {
      requireInput(warp->image, "Warp image");
      requireInput(warp->displacement, "Warp displacement");
      const auto* image = descriptorOf(warp->image);
      const auto* displacement = descriptorOf(warp->displacement);
      if (image != nullptr && !document::isColor(*image))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Warp requires a Color image."});
      if (displacement != nullptr && (!document::isScreenImage(*displacement) ||
          displacement->shape != document::SignalShape::Vector2))
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Error,
          "Warp displacement requires a screen-space Vector2."});
      else if (displacement != nullptr &&
          displacement->metadata.semantic != document::SignalSemantic::EdgeDirection)
        result.diagnostics.push_back({operation.id, DiagnosticSeverity::Warning,
          "Warp interprets this Vector2 as a signed direction packed into 0..1."});
    }
    if (const auto* measure = std::get_if<document::MeasureOperation>(&operation.data)) {
      const auto descriptor = descriptors.find(measure->input.id);
      if (descriptor != descriptors.end() &&
          descriptor->second->metadata.domain == document::SignalDomain::Document)
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
    else if (descriptors.at(route.source.id)->metadata.domain != document::SignalDomain::Document ||
        descriptors.at(route.source.id)->shape != document::SignalShape::Scalar)
      result.diagnostics.push_back({route.source.id.producer, DiagnosticSeverity::Error,
        "Modulation requires a Scalar source signal."});
    else if (route.source.frameOffset > 0)
      result.diagnostics.push_back({route.source.id.producer, DiagnosticSeverity::Error,
        "Modulation cannot sample a future signal."});
  }
  return result;
}

EvaluationPlan restrictEvaluationPlan(const EvaluationPlan& plan,
    const std::vector<document::SignalRef>& requiredSignals) {
  if (!plan.valid()) return plan;
  std::unordered_map<document::SignalId, std::size_t> producerBySignal;
  for (std::size_t index = 0; index < plan.nodes.size(); ++index)
    for (const document::SignalId& output : plan.nodes[index].outputs)
      producerBySignal.emplace(output, index);

  std::vector<bool> required(plan.nodes.size(), false);
  std::vector<std::size_t> pending;
  const auto requireSignal = [&](const document::SignalRef signal) {
    if (!signal || signal.frameOffset < 0) return;
    const auto producer = producerBySignal.find(signal.id);
    if (producer != producerBySignal.end()) pending.push_back(producer->second);
  };
  for (const document::SignalRef& signal : requiredSignals) requireSignal(signal);
  while (!pending.empty()) {
    const std::size_t index = pending.back();
    pending.pop_back();
    if (required[index]) continue;
    required[index] = true;
    for (const document::SignalRef& input : plan.nodes[index].inputs) requireSignal(input);
  }

  EvaluationPlan result;
  result.finalSignal = plan.finalSignal;
  result.diagnostics = plan.diagnostics;
  result.retainAllSignals = false;
  for (const document::SignalRef& signal : requiredSignals)
    if (signal && signal.frameOffset >= 0 &&
        std::find(result.retainedSignals.begin(), result.retainedSignals.end(), signal.id) ==
          result.retainedSignals.end())
      result.retainedSignals.push_back(signal.id);
  result.nodes.reserve(plan.nodes.size());
  for (std::size_t index = 0; index < plan.nodes.size(); ++index)
    if (required[index]) result.nodes.push_back(plan.nodes[index]);
  return result;
}

} // namespace gfxlab::evaluation
