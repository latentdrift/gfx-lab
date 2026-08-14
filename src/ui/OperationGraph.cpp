#include "ui/OperationGraph.hpp"

#include "evaluation/Compiler.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>
#include <imnodes.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace gfxlab::ui {
namespace {

constexpr int outputNodeId = 1;
constexpr int outputInputPinId = 3'000'001;

struct InputPort {
  document::OperationId operation;
  editor::InputSocket socket = editor::InputSocket::Primary;
  std::string label;
  document::SignalRef signal;
  int pin = 0;
};

struct OutputPort {
  document::SignalRef signal;
  document::SignalDescriptor descriptor;
  int pin = 0;
};

struct LinkTarget {
  bool presentation = false;
  document::OperationId operation;
  editor::InputSocket socket = editor::InputSocket::Primary;
  document::SignalRef signal;
};

int nodeId(const document::OperationId operation) {
  static std::unordered_map<std::uint64_t, int> ids;
  static int next = 10;
  const auto [found, inserted] = ids.emplace(operation.value, next);
  if (inserted) ++next;
  return found->second;
}

int inputPin(const document::OperationId operation, const int index) {
  return 1'000'000 + nodeId(operation) * 16 + index;
}

int outputPin(const document::OperationId operation, const int index) {
  return 2'000'000 + nodeId(operation) * 16 + index;
}

ImU32 signalColor(const document::SignalDescriptor& signal) {
  switch (signal.metadata.semantic) {
    case document::SignalSemantic::Color: return IM_COL32(78, 190, 214, 255);
    case document::SignalSemantic::DeviceDepth:
    case document::SignalSemantic::LinearDepth: return IM_COL32(120, 154, 220, 255);
    case document::SignalSemantic::Normal: return IM_COL32(184, 127, 224, 255);
    case document::SignalSemantic::FieldStrength:
    case document::SignalSemantic::SignedDistance: return IM_COL32(98, 202, 138, 255);
    case document::SignalSemantic::Spectrum: return IM_COL32(232, 146, 72, 255);
    case document::SignalSemantic::Measurement: return IM_COL32(232, 202, 91, 255);
    case document::SignalSemantic::EdgeDirection: return IM_COL32(220, 117, 149, 255);
    default: return signal.shape == document::SignalShape::Scalar
      ? IM_COL32(225, 225, 225, 255) : IM_COL32(190, 190, 190, 255);
  }
}

void signalTooltip(const document::SignalDescriptor& signal) {
  if (!ImGui::IsItemHovered(ImGuiHoveredFlags_DelayNormal)) return;
  ImGui::BeginTooltip();
  ImGui::TextUnformatted(signal.name.c_str());
  ImGui::Separator();
  ImGui::TextUnformatted(document::signalDescriptorSummary(signal).c_str());
  ImGui::Text("Encoding: %s", document::signalEncodingLabel(signal.metadata.encoding));
  if (!signal.metadata.units.empty()) ImGui::Text("Units: %s", signal.metadata.units.c_str());
  if (signal.metadata.hasKnownRange)
    ImGui::Text("Meaningful range: %.3g .. %.3g", signal.metadata.knownRange.x,
      signal.metadata.knownRange.y);
  if (signal.metadata.extent.x > 0 && signal.metadata.extent.y > 0)
    ImGui::Text("Authored extent: %d x %d", signal.metadata.extent.x, signal.metadata.extent.y);
  ImGui::EndTooltip();
}

const char* executionClass(const document::Operation& operation) {
  return std::visit([](const auto& data) -> const char* {
    using Type = std::decay_t<decltype(data)>;
    if constexpr (std::is_same_v<Type, document::RenderOperation>) return "SCENE RENDER";
    if constexpr (std::is_same_v<Type, document::MeasureOperation>) return "GPU READBACK";
    if constexpr (std::is_same_v<Type, document::ConstantOperation>) return "VALUE";
    if constexpr (std::is_same_v<Type, document::EdgeOperation>) return "2 FULLSCREEN PASSES · 2 OUTPUTS";
    return "FULLSCREEN PASS";
  }, operation.data);
}

std::vector<InputPort> inputsFor(const document::Operation& operation) {
  std::vector<InputPort> result;
  const auto add = [&](const editor::InputSocket socket, const char* label,
      const document::SignalRef signal) {
    result.push_back({operation.id, socket, label, signal,
      inputPin(operation.id, static_cast<int>(result.size()))});
  };
  std::visit([&](const auto& data) {
    using Type = std::decay_t<decltype(data)>;
    if constexpr (std::is_same_v<Type, document::InterpretOperation>)
      add(editor::InputSocket::Primary, "Spectrum", data.spectrum);
    else if constexpr (std::is_same_v<Type, document::CompositeOperation>) {
      add(editor::InputSocket::A, "Input A", data.a);
      add(editor::InputSocket::B, "Input B", data.b);
      add(editor::InputSocket::Mask, "Mask", data.mask);
    } else if constexpr (std::is_same_v<Type, document::StereoOperation>) {
      add(editor::InputSocket::Left, "Left", data.left);
      add(editor::InputSocket::Right, "Right", data.right);
    } else if constexpr (std::is_same_v<Type, document::MeasureOperation>)
      add(editor::InputSocket::Primary, "Input", data.input);
    else if constexpr (std::is_same_v<Type, document::LuminanceOperation> ||
        std::is_same_v<Type, document::RemapOperation> ||
        std::is_same_v<Type, document::EdgeOperation> ||
        std::is_same_v<Type, document::BlurOperation> ||
        std::is_same_v<Type, document::ThresholdOperation> ||
        std::is_same_v<Type, document::GradientMapOperation>)
      add(editor::InputSocket::Primary, "Input", data.input);
    else if constexpr (std::is_same_v<Type, document::WarpOperation>) {
      add(editor::InputSocket::Image, "Image", data.image);
      add(editor::InputSocket::Displacement, "Displacement", data.displacement);
    }
  }, operation.data);
  return result;
}

bool accepts(const document::Document& document, const InputPort& input,
    const OutputPort& output) {
  const document::Operation* target = document::findOperation(document, input.operation);
  const document::Operation* producer = document::findOperation(document, output.signal.id.producer);
  if (target == nullptr || producer == nullptr) return false;
  const document::SignalDescriptor& descriptor = output.descriptor;
  if (std::holds_alternative<document::InterpretOperation>(target->data))
    return descriptor.shape == document::SignalShape::Spectrum16;
  if (std::holds_alternative<document::CompositeOperation>(target->data) &&
      input.socket == editor::InputSocket::Mask)
    return document::isScreenScalar(descriptor);
  if (std::holds_alternative<document::CompositeOperation>(target->data))
    return document::isScreenImage(descriptor) && descriptor.shape != document::SignalShape::Vector2;
  if (std::holds_alternative<document::StereoOperation>(target->data))
    return document::isColor(descriptor) &&
      std::holds_alternative<document::RenderOperation>(producer->data);
  if (std::holds_alternative<document::MeasureOperation>(target->data))
    return document::isScreenImage(descriptor);
  if (std::holds_alternative<document::LuminanceOperation>(target->data))
    return document::isColor(descriptor);
  if (std::holds_alternative<document::RemapOperation>(target->data))
    return document::isScreenScalar(descriptor);
  if (std::holds_alternative<document::EdgeOperation>(target->data))
    return document::isScreenImage(descriptor) && descriptor.shape != document::SignalShape::Spectrum16;
  if (const auto* blur = std::get_if<document::BlurOperation>(&target->data))
    return document::isScreenImage(descriptor) && descriptor.shape == blur->outputShape;
  if (std::holds_alternative<document::ThresholdOperation>(target->data) ||
      std::holds_alternative<document::GradientMapOperation>(target->data))
    return document::isScreenScalar(descriptor);
  if (std::holds_alternative<document::WarpOperation>(target->data)) {
    if (input.socket == editor::InputSocket::Image) return document::isColor(descriptor);
    return input.socket == editor::InputSocket::Displacement &&
      document::isScreenImage(descriptor) && descriptor.shape == document::SignalShape::Vector2;
  }
  return false;
}

document::SignalRef selectedSignal(const document::Document& document,
    const editor::EditorState& state) {
  if (state.selection.kind == editor::SelectionKind::Operation) {
    if (const document::Operation* operation = document::findOperation(
        document, state.selection.operation)) {
      if (state.viewer.viewed.id.producer == operation->id) return state.viewer.viewed;
      return document::primaryOutput(*operation);
    }
  }
  return document.presentation.input;
}

document::SignalRef spectrumFromSelection(const document::Document& document,
    const editor::EditorState& state) {
  const document::Operation* operation = state.selection.kind == editor::SelectionKind::Operation
    ? document::findOperation(document, state.selection.operation) : nullptr;
  if (operation != nullptr) {
    for (const document::SignalDescriptor& output : operation->outputs)
      if (output.shape == document::SignalShape::Spectrum16) return {output.id, 0};
  }
  for (const document::Operation& candidate : document.operations)
    for (const document::SignalDescriptor& output : candidate.outputs)
      if (output.shape == document::SignalShape::Spectrum16) return {output.id, 0};
  return {};
}

document::GraphNodePosition* graphPosition(document::Document& document,
    const document::OperationId operation) {
  const auto found = std::find_if(document.graphLayout.operations.begin(),
    document.graphLayout.operations.end(), [operation](const document::GraphNodePosition& position) {
      return position.operation == operation;
    });
  return found == document.graphLayout.operations.end() ? nullptr : &*found;
}

void placeGraph(document::Document& document, const bool force) {
  if (force) {
    document.graphLayout.operations.clear();
    document.graphLayout.outputPositionAuthored = false;
  }
  const evaluation::EvaluationPlan plan = evaluation::compileDocument(document);
  std::unordered_map<document::OperationId, int> levels;
  std::unordered_map<int, int> lanes;
  int maximumLevel = 0;
  for (const evaluation::EvaluationNode& evaluationNode : plan.nodes) {
    int level = 0;
    for (const document::SignalRef& input : evaluationNode.inputs) {
      if (input.frameOffset == 0)
        level = std::max(level, levels[input.id.producer] + 1);
    }
    levels[evaluationNode.operation] = level;
    maximumLevel = std::max(maximumLevel, level);
    document::GraphNodePosition* authored = graphPosition(document, evaluationNode.operation);
    if (authored == nullptr) {
      document.graphLayout.operations.push_back({evaluationNode.operation,
        glm::vec2(40.0f + level * 260.0f, 60.0f + lanes[level]++ * 210.0f)});
      authored = &document.graphLayout.operations.back();
    }
    ImNodes::SetNodeGridSpacePos(nodeId(evaluationNode.operation),
      ImVec2(authored->position.x, authored->position.y));
  }
  if (!document.graphLayout.outputPositionAuthored) {
    document.graphLayout.outputPosition = {40.0f + (maximumLevel + 1) * 260.0f, 60.0f};
    document.graphLayout.outputPositionAuthored = true;
  }
  ImNodes::SetNodeGridSpacePos(outputNodeId, ImVec2(document.graphLayout.outputPosition.x,
    document.graphLayout.outputPosition.y));
}

void captureGraphPositions(document::Document& document) {
  for (document::GraphNodePosition& position : document.graphLayout.operations) {
    if (document::findOperation(document, position.operation) == nullptr) continue;
    const ImVec2 current = ImNodes::GetNodeGridSpacePos(nodeId(position.operation));
    position.position = {current.x, current.y};
  }
  const ImVec2 output = ImNodes::GetNodeGridSpacePos(outputNodeId);
  document.graphLayout.outputPosition = {output.x, output.y};
  document.graphLayout.outputPositionAuthored = true;
}

} // namespace

SceneWindowResult drawOperationGraph(bool& open, document::Document& document,
    editor::EditorState& editorState, editor::CommandHistory& history) {
  SceneWindowResult result;
  if (!open) return result;
  if (!ImGui::Begin("Operation Graph", &open)) {
    ImGui::End();
    return result;
  }
  keepCurrentWindowVisible();
  static std::string message;
  const auto execute = [&](const editor::Command& command) {
    const editor::CommandResult commandResult = history.execute(document, command);
    message = commandResult.error;
    return commandResult.applied;
  };

  if (ImGui::Button("+ Add")) ImGui::OpenPopup("add-operation-graph");
  if (ImGui::BeginPopup("add-operation-graph")) {
    ImGui::TextDisabled("ADD OPERATION");
    const document::SignalRef selected = selectedSignal(document, editorState);
    const auto add = [&](document::Operation operation, const bool final) {
      const document::OperationId id = operation.id;
      const bool followingFinal = editorState.viewer.viewed == document.presentation.input;
      if (execute(editor::AddOperation{std::move(operation), static_cast<std::size_t>(-1), final})) {
        editorState.selection = {editor::SelectionKind::Operation, id};
        if (final && followingFinal) editorState.viewer.viewed = document.presentation.input;
      }
    };
    if (ImGui::MenuItem("Render")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeRenderOperation(id, "Render"), true);
    }
    if (ImGui::MenuItem("Composite")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeCompositeOperation(id, "Composite", document.presentation.input, selected), true);
    }
    if (ImGui::MenuItem("Interpret")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeInterpretOperation(id, "Interpret", spectrumFromSelection(document, editorState)), true);
    }
    if (ImGui::MenuItem("Measure")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeMeasureOperation(id, "Measure", selected), false);
    }
    const document::SignalDescriptor* selectedDescriptor = document::findSignal(document, selected.id);
    const bool selectedColor = selectedDescriptor != nullptr && document::isColor(*selectedDescriptor);
    const bool selectedScalar = selectedDescriptor != nullptr && document::isScreenScalar(*selectedDescriptor);
    const bool selectedImage = selectedDescriptor != nullptr && document::isScreenImage(*selectedDescriptor);
    ImGui::Separator();
    ImGui::TextDisabled("WORKING SIGNALS");
    ImGui::BeginDisabled(!selectedColor);
    if (ImGui::MenuItem("Luminance")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeLuminanceOperation(id, "Luminance", selected), true);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!selectedScalar);
    if (ImGui::MenuItem("Remap")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeRemapOperation(id, "Remap", selected), true);
    }
    if (ImGui::MenuItem("Remap to Mask")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeRemapOperation(id, "Mask Remap", selected,
        document::SignalSemantic::MaskCoverage), true);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!selectedImage || selectedDescriptor->shape == document::SignalShape::Spectrum16);
    if (ImGui::MenuItem("Edge")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeEdgeOperation(id, "Edge", selected), true);
    }
    ImGui::EndDisabled();
    const bool blurCompatible = selectedImage && selectedDescriptor->shape != document::SignalShape::Spectrum16;
    ImGui::BeginDisabled(!blurCompatible);
    if (ImGui::MenuItem("Blur")) {
      const document::OperationId id = document::nextOperationId(document);
      const document::SignalSemantic safeSemantic = selectedDescriptor->metadata.semantic ==
          document::SignalSemantic::Color || selectedDescriptor->metadata.semantic ==
          document::SignalSemantic::Luminance || selectedDescriptor->metadata.semantic ==
          document::SignalSemantic::MaskCoverage || selectedDescriptor->metadata.semantic ==
          document::SignalSemantic::EdgeStrength
        ? selectedDescriptor->metadata.semantic : document::SignalSemantic::Generic;
      add(document::makeBlurOperation(id, "Blur", selected, selectedDescriptor->shape,
        safeSemantic), true);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!selectedScalar);
    if (ImGui::MenuItem("Threshold")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeThresholdOperation(id, "Threshold", selected), true);
    }
    if (ImGui::MenuItem("Gradient Map")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeGradientMapOperation(id, "Gradient Map", selected), true);
    }
    ImGui::EndDisabled();
    document::SignalRef warpImage;
    const document::SignalDescriptor* finalDescriptor = document::findSignal(document,
      document.presentation.input.id);
    if (finalDescriptor != nullptr && document::isColor(*finalDescriptor))
      warpImage = document.presentation.input;
    if (!warpImage) {
      for (const document::Operation& candidate : document.operations) {
        const auto found = std::find_if(candidate.outputs.begin(), candidate.outputs.end(),
          [](const document::SignalDescriptor& output) { return document::isColor(output); });
        if (found != candidate.outputs.end()) { warpImage = {found->id, 0}; break; }
      }
    }
    const bool vectorSelected = selectedDescriptor != nullptr &&
      selectedDescriptor->shape == document::SignalShape::Vector2 &&
      selectedDescriptor->metadata.domain == document::SignalDomain::Screen2D;
    ImGui::BeginDisabled(!vectorSelected || !warpImage);
    if (ImGui::MenuItem("Warp")) {
      const document::OperationId id = document::nextOperationId(document);
      add(document::makeWarpOperation(id, "Warp", warpImage, selected), true);
    }
    ImGui::EndDisabled();
    ImGui::EndPopup();
  }

  ImGui::SameLine();
  const document::Operation* selectedOperation = editorState.selection.kind == editor::SelectionKind::Operation
    ? document::findOperation(document, editorState.selection.operation) : nullptr;
  ImGui::BeginDisabled(selectedOperation == nullptr);
  if (ImGui::Button("Duplicate")) {
    const document::OperationId id = document::nextOperationId(document);
    if (execute(editor::DuplicateOperation{selectedOperation->id, id, static_cast<std::size_t>(-1)}))
      editorState.selection = {editor::SelectionKind::Operation, id};
  }
  ImGui::SameLine();
  const document::OperationId compareSource = selectedOperation == nullptr
    ? document::OperationId{} : selectedOperation->id;
  const auto duplicateAndCompare = [&](const RelationOperator relationship) {
    const document::OperationId duplicate = document::nextOperationId(document);
    const document::OperationId comparison{duplicate.value + 1};
    if (execute(editor::DuplicateAndCompare{compareSource, duplicate, comparison,
        relationship})) {
      editorState.selection = {editor::SelectionKind::Operation, duplicate};
      editorState.viewer.viewed = document.presentation.input;
      const document::Operation* source = document::findOperation(document, compareSource);
      if (source != nullptr) editorState.viewer.comparison = document::primaryOutput(*source);
      return true;
    }
    return false;
  };
  if (ImGui::Button("Duplicate + Compare"))
    static_cast<void>(duplicateAndCompare(RelationOperator::AbsoluteDifference));
  ImGui::SameLine(0.0f, 2.0f);
  if (ImGui::SmallButton("v##compare-mode")) ImGui::OpenPopup("duplicate-compare-mode");
  if (ImGui::BeginPopup("duplicate-compare-mode")) {
    ImGui::TextDisabled("RELATIONSHIP");
    for (int index = 0; index <= static_cast<int>(RelationOperator::Normal); ++index) {
      const RelationOperator relationship = static_cast<RelationOperator>(index);
      if (ImGui::MenuItem(relationOperatorLabel(relationship)))
        static_cast<void>(duplicateAndCompare(relationship));
    }
    ImGui::EndPopup();
  }
  ImGui::SameLine();
  if (ImGui::Button("Delete")) {
    const document::OperationId deleted = selectedOperation->id;
    if (execute(editor::RemoveOperation{deleted})) editorState.selection = {};
  }
  ImGui::EndDisabled();
  ImGui::SameLine();
  if (ImGui::Button("View Output")) {
    editorState.viewer.viewed = document.presentation.input;
    editorState.viewer.mode = editor::ViewerMode::Single;
  }
  ImGui::SameLine();
  const bool arrange = ImGui::Button("Arrange");
  ImGui::SameLine();
  if (ImGui::Button("Scene")) ImGui::OpenPopup("graph-scene");
  if (ImGui::BeginPopup("graph-scene")) {
    if (ImGui::MenuItem("Import model...")) result.importModel = true;
    if (document.scene.importedModel != nullptr && ImGui::MenuItem("Unload model")) result.unloadModel = true;
    ImGui::Separator();
    constexpr std::array<const char*, 10> labels = {"Torus", "Texture plane", "Depth precision",
      "Transparency", "Lighting", "Stencil mask", "Field interference", "SDF iso-surface",
      "Spectral metamers", "Elemental chamber"};
    for (int index = 0; index < static_cast<int>(labels.size()); ++index) {
      if (document.hardwareProfile != HardwareProfile::Unrestricted && index >= 5) continue;
      if (!ImGui::MenuItem(labels[index], nullptr, static_cast<int>(document.scene.testScene) == index)) continue;
      document::Document edited = document;
      edited.scene.testScene = static_cast<TestScene>(index);
      execute(editor::ReplaceDocument{std::move(edited)});
    }
    ImGui::EndPopup();
  }
  if (!message.empty()) {
    ImGui::SameLine();
    ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "%s", message.c_str());
  }
  ImGui::Separator();

  std::unordered_map<int, InputPort> inputPins;
  std::unordered_map<int, OutputPort> outputPins;
  std::unordered_map<int, document::OperationId> operationNodes;
  std::unordered_map<int, LinkTarget> links;
  std::optional<editor::Command> deferredSignalCommand;
  std::optional<editor::ObjectSelection> deferredSelection;
  document::SignalRef deferredViewedSignal;
  ImNodes::GetIO().LinkDetachWithModifierClick.Modifier = &ImGui::GetIO().KeyCtrl;
  ImNodes::BeginNodeEditor();
  placeGraph(document, arrange);
  for (document::Operation& operation : document.operations) {
    const int id = nodeId(operation.id);
    operationNodes[id] = operation.id;
    ImNodes::BeginNode(id);
    ImNodes::BeginNodeTitleBar();
    ImGui::TextUnformatted(operation.name.c_str());
    ImGui::SameLine();
    ImGui::TextDisabled("  %s", document::operationTypeLabel(operation));
    ImNodes::EndNodeTitleBar();
    ImNodes::BeginStaticAttribute(10'000 + id);
    bool enabled = operation.enabled;
    if (ImGui::Checkbox("Enabled", &enabled))
      execute(editor::SetOperationEnabled{operation.id, enabled});
    ImGui::SameLine();
    ImGui::TextDisabled("%s", executionClass(operation));
    ImNodes::EndStaticAttribute();
    for (const InputPort& input : inputsFor(operation)) {
      inputPins[input.pin] = input;
      const document::SignalDescriptor* descriptor = document::findSignal(document, input.signal.id);
      const document::SignalDescriptor fallback;
      ImNodes::PushColorStyle(ImNodesCol_Pin, signalColor(descriptor == nullptr ? fallback : *descriptor));
      ImNodes::PushAttributeFlag(ImNodesAttributeFlags_EnableLinkDetachWithDragClick);
      ImNodes::BeginInputAttribute(input.pin);
      ImGui::TextUnformatted(input.label.c_str());
      if (descriptor != nullptr) signalTooltip(*descriptor);
      ImNodes::EndInputAttribute();
      ImNodes::PopAttributeFlag();
      ImNodes::PopColorStyle();
    }
    for (std::size_t index = 0; index < operation.outputs.size(); ++index) {
      const document::SignalDescriptor& output = operation.outputs[index];
      const int pin = outputPin(operation.id, static_cast<int>(index));
      outputPins[pin] = {{output.id, 0}, output, pin};
      ImNodes::PushColorStyle(ImNodesCol_Pin, signalColor(output));
      ImNodes::BeginOutputAttribute(pin);
      ImGui::PushID(pin);
      const bool viewed = editorState.viewer.viewed.id == output.id;
      if (viewed) ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.35f, 0.9f, 1.0f, 1.0f));
      if (ImGui::Selectable(output.name.c_str(), viewed, 0, ImVec2(120.0f, 0.0f))) {
        const document::SignalRef signal{output.id, 0};
        if (ImGui::GetIO().KeyShift) editorState.viewer.comparison = signal;
        else editorState.viewer.viewed = signal;
      }
      signalTooltip(output);
      if (ImGui::BeginPopupContextItem("signal-actions")) {
        const document::SignalRef signal{output.id, 0};
        ImGui::TextDisabled("%s", document::signalDescriptorSummary(output).c_str());
        if (ImGui::MenuItem("View Signal")) deferredViewedSignal = signal;
        ImGui::BeginDisabled(output.metadata.domain == document::SignalDomain::Document);
        if (ImGui::MenuItem("Set as Final Output")) {
          deferredSignalCommand = editor::SetFinalSignal{signal};
          deferredViewedSignal = signal;
          deferredSelection = editor::ObjectSelection{editor::SelectionKind::Presentation, {}};
        }
        ImGui::EndDisabled();
        const bool continuesFinal = document.presentation.input.id == output.id;
        const auto insertionPoint = std::find_if(document.operations.begin(), document.operations.end(),
          [&](const document::Operation& candidate) { return candidate.id == operation.id; });
        const std::size_t insertionIndex = insertionPoint == document.operations.end()
          ? static_cast<std::size_t>(-1) : static_cast<std::size_t>(
              std::distance(document.operations.begin(), insertionPoint)) + 1;
        const auto deferOperation = [&](document::Operation inserted) {
          const document::OperationId insertedId = inserted.id;
          deferredViewedSignal = document::primaryOutput(inserted);
          deferredSelection = editor::ObjectSelection{editor::SelectionKind::Operation, insertedId};
          deferredSignalCommand = editor::AddOperation{std::move(inserted), insertionIndex,
            continuesFinal};
        };
        ImGui::Separator();
        ImGui::TextDisabled("INSERT FROM THIS SIGNAL");
        ImGui::BeginDisabled(!document::isColor(output));
        if (ImGui::MenuItem("Extract Luminance")) {
          const document::OperationId id = document::nextOperationId(document);
          deferOperation(document::makeLuminanceOperation(id, "Luminance", signal));
        }
        ImGui::EndDisabled();
        const bool scalar = document::isScreenScalar(output);
        ImGui::BeginDisabled(!scalar);
        if (ImGui::MenuItem("Remap Scalar")) {
          const document::OperationId id = document::nextOperationId(document);
          deferOperation(document::makeRemapOperation(id, "Remap", signal));
        }
        if (ImGui::MenuItem("Remap to Mask")) {
          const document::OperationId id = document::nextOperationId(document);
          deferOperation(document::makeRemapOperation(id, "Mask Remap", signal,
            document::SignalSemantic::MaskCoverage));
        }
        if (ImGui::MenuItem("Threshold to Mask")) {
          const document::OperationId id = document::nextOperationId(document);
          deferOperation(document::makeThresholdOperation(id, "Threshold Mask", signal));
        }
        ImGui::EndDisabled();
        const bool edgeCompatible = document::isScreenImage(output) &&
          output.shape != document::SignalShape::Spectrum16;
        ImGui::BeginDisabled(!edgeCompatible);
        if (ImGui::MenuItem("Find Edges")) {
          const document::OperationId id = document::nextOperationId(document);
          deferOperation(document::makeEdgeOperation(id, "Edge", signal));
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
      }
      if (viewed) ImGui::PopStyleColor();
      ImGui::PopID();
      ImNodes::EndOutputAttribute();
      ImNodes::PopColorStyle();
    }
    ImNodes::EndNode();
  }

  ImNodes::BeginNode(outputNodeId);
  ImNodes::BeginNodeTitleBar();
  ImGui::TextUnformatted("Output");
  ImNodes::EndNodeTitleBar();
  ImNodes::BeginInputAttribute(outputInputPinId);
  const document::SignalDescriptor* finalDescriptor = document::findSignal(document, document.presentation.input.id);
  ImGui::Text("Input  %s", finalDescriptor == nullptr ? "Disconnected" : finalDescriptor->name.c_str());
  ImNodes::EndInputAttribute();
  ImNodes::BeginStaticAttribute(10'001);
  ImGui::TextDisabled("DISPLAY / EXPORT");
  ImNodes::EndStaticAttribute();
  ImNodes::EndNode();

  int link = 4'000'000;
  for (const auto& [pin, input] : inputPins) {
    if (!input.signal) continue;
    const document::Operation* producer = document::findOperation(document, input.signal.id.producer);
    if (producer == nullptr) continue;
    const auto output = std::find_if(producer->outputs.begin(), producer->outputs.end(),
      [&](const document::SignalDescriptor& descriptor) { return descriptor.id == input.signal.id; });
    if (output == producer->outputs.end()) continue;
    const int index = static_cast<int>(std::distance(producer->outputs.begin(), output));
    ImNodes::PushColorStyle(ImNodesCol_Link, signalColor(*output));
    links.emplace(link, LinkTarget{false, input.operation, input.socket, input.signal});
    ImNodes::Link(link++, outputPin(producer->id, index), pin);
    ImNodes::PopColorStyle();
  }
  if (finalDescriptor != nullptr) {
    const document::Operation* producer = document::findOperation(document, finalDescriptor->producer);
    if (producer != nullptr) {
      const auto output = std::find_if(producer->outputs.begin(), producer->outputs.end(),
        [&](const document::SignalDescriptor& descriptor) { return descriptor.id == finalDescriptor->id; });
      if (output != producer->outputs.end()) {
        const int index = static_cast<int>(std::distance(producer->outputs.begin(), output));
        ImNodes::PushColorStyle(ImNodesCol_Link, signalColor(*output));
        links.emplace(link, LinkTarget{true, {}, editor::InputSocket::Primary,
          document.presentation.input});
        ImNodes::Link(link++, outputPin(producer->id, index), outputInputPinId);
        ImNodes::PopColorStyle();
      }
    }
  }
  ImNodes::MiniMap(0.16f, ImNodesMiniMapLocation_BottomRight);
  ImNodes::EndNodeEditor();
  captureGraphPositions(document);

  const auto disconnect = [&](const int linkId) {
    const auto found = links.find(linkId);
    if (found == links.end()) return;
    if (found->second.presentation) {
      message = "Output must remain connected. Connect it to another signal instead.";
      return;
    }
    execute(editor::DisconnectSignal{found->second.operation, found->second.socket,
      found->second.signal});
  };
  int destroyedLink = 0;
  if (ImNodes::IsLinkDestroyed(&destroyedLink)) disconnect(destroyedLink);
  if (ImNodes::IsEditorHovered() && ImGui::IsKeyPressed(ImGuiKey_Delete, false)) {
    const int selectedLinkCount = ImNodes::NumSelectedLinks();
    if (selectedLinkCount > 0) {
      std::vector<int> selectedLinks(static_cast<std::size_t>(selectedLinkCount));
      ImNodes::GetSelectedLinks(selectedLinks.data());
      for (const int selectedLink : selectedLinks) disconnect(selectedLink);
      ImNodes::ClearLinkSelection();
    }
  }

  int firstPin = 0;
  int secondPin = 0;
  if (ImNodes::IsLinkCreated(&firstPin, &secondPin)) {
    const auto output = outputPins.contains(firstPin) ? outputPins.find(firstPin) : outputPins.find(secondPin);
    const int destinationPin = outputPins.contains(firstPin) ? secondPin : firstPin;
    if (output == outputPins.end()) message = "Connections must run from an output to an input.";
    else if (destinationPin == outputInputPinId) {
      if (output->second.descriptor.metadata.domain == document::SignalDomain::Document)
        message = "Output requires an image or field, not a Scalar.";
      else if (execute(editor::SetFinalSignal{output->second.signal}))
        editorState.viewer.viewed = document.presentation.input;
    } else if (const auto input = inputPins.find(destinationPin); input == inputPins.end()) {
      message = "That pin is not a connectable input.";
    } else if (!accepts(document, input->second, output->second)) {
      message = "Those signal types are not compatible.";
    } else if (execute(editor::ConnectSignal{input->second.operation, input->second.socket,
        output->second.signal})) {
      editorState.selection = {editor::SelectionKind::Operation, input->second.operation};
    }
  }

  const int selectedCount = ImNodes::NumSelectedNodes();
  if (selectedCount > 0) {
    std::vector<int> selectedNodes(static_cast<std::size_t>(selectedCount));
    ImNodes::GetSelectedNodes(selectedNodes.data());
    const int selected = selectedNodes.back();
    if (selected == outputNodeId) editorState.selection = {editor::SelectionKind::Presentation, {}};
    else if (const auto found = operationNodes.find(selected); found != operationNodes.end())
      editorState.selection = {editor::SelectionKind::Operation, found->second};
  }

  if (deferredSignalCommand.has_value() && execute(*deferredSignalCommand)) {
    if (deferredSelection.has_value()) editorState.selection = *deferredSelection;
    if (deferredViewedSignal) editorState.viewer.viewed = deferredViewedSignal;
  } else if (deferredViewedSignal) {
    editorState.viewer.viewed = deferredViewedSignal;
  }

  ImGui::End();
  return result;
}

} // namespace gfxlab::ui
