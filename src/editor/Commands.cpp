#include "editor/Commands.hpp"

#include "evaluation/Compiler.hpp"

#include <algorithm>
#include <cmath>
#include <type_traits>

namespace gfxlab::editor {
namespace {

document::AnimationTrack* findTrack(document::Document& document,
    const document::PropertyAddress target) {
  const auto found = std::find_if(document.automation.animation.begin(),
    document.automation.animation.end(), [target](const document::AnimationTrack& track) {
      return track.target == target;
    });
  return found == document.automation.animation.end() ? nullptr : &*found;
}

std::string duplicateOperation(document::Document& document,
    const document::OperationId sourceId, const document::OperationId duplicateId,
    const std::size_t requestedIndex) {
  const document::Operation* source = document::findOperation(document, sourceId);
  if (source == nullptr) return "The source operation no longer exists.";
  if (!duplicateId || document::findOperation(document, duplicateId) != nullptr)
    return "The duplicate operation ID is invalid or already exists.";
  document::Operation duplicate = *source;
  duplicate.id = duplicateId;
  duplicate.name += " copy";
  for (document::SignalDescriptor& output : duplicate.outputs) {
    output.id = document::operationSignal(duplicate.id, output.key);
    output.producer = duplicate.id;
  }
  const auto remapSelf = [&](document::SignalRef& signal) {
    if (signal.id.producer == sourceId) signal.id.producer = duplicateId;
  };
  std::visit([&](auto& operationData) {
    using OperationType = std::decay_t<decltype(operationData)>;
    if constexpr (std::is_same_v<OperationType, document::InterpretOperation>)
      remapSelf(operationData.spectrum);
    else if constexpr (std::is_same_v<OperationType, document::CompositeOperation>) {
      remapSelf(operationData.a);
      remapSelf(operationData.b);
      remapSelf(operationData.mask);
    } else if constexpr (std::is_same_v<OperationType, document::StereoOperation>) {
      remapSelf(operationData.left);
      remapSelf(operationData.right);
    } else if constexpr (std::is_same_v<OperationType, document::MeasureOperation>)
      remapSelf(operationData.input);
    else if constexpr (std::is_same_v<OperationType, document::LuminanceOperation> ||
        std::is_same_v<OperationType, document::RemapOperation> ||
        std::is_same_v<OperationType, document::EdgeOperation> ||
        std::is_same_v<OperationType, document::BlurOperation> ||
        std::is_same_v<OperationType, document::ThresholdOperation> ||
        std::is_same_v<OperationType, document::GradientMapOperation>)
      remapSelf(operationData.input);
    else if constexpr (std::is_same_v<OperationType, document::WarpOperation>) {
      remapSelf(operationData.image);
      remapSelf(operationData.displacement);
    }
  }, duplicate.data);
  const std::size_t index = std::min(requestedIndex, document.operations.size());
  document.operations.insert(document.operations.begin() + static_cast<std::ptrdiff_t>(index),
    std::move(duplicate));
  const document::ObjectId sourceOwner = document::operationObject(sourceId);
  const document::ObjectId duplicateOwner = document::operationObject(duplicateId);
  const std::size_t trackCount = document.automation.animation.size();
  for (std::size_t track = 0; track < trackCount; ++track) {
    if (document.automation.animation[track].target.owner != sourceOwner) continue;
    document::AnimationTrack cloned = document.automation.animation[track];
    cloned.target.owner = duplicateOwner;
    document.automation.animation.push_back(std::move(cloned));
  }
  const std::size_t routeCount = document.automation.modulation.size();
  for (std::size_t route = 0; route < routeCount; ++route) {
    if (document.automation.modulation[route].target.owner != sourceOwner) continue;
    document::ModulationRoute cloned = document.automation.modulation[route];
    cloned.target.owner = duplicateOwner;
    remapSelf(cloned.source);
    document.automation.modulation.push_back(std::move(cloned));
  }
  document.nextOperationIdentity = std::max(document.nextOperationIdentity,
    duplicateId.value + 1);
  return {};
}

std::string mutate(document::Document& document, const Command& command) {
  return std::visit([&](const auto& data) -> std::string {
    using Type = std::decay_t<decltype(data)>;
    if constexpr (std::is_same_v<Type, AddOperation>) {
      if (!data.operation.id) return "An operation must have a stable non-zero ID.";
      if (document::findOperation(document, data.operation.id) != nullptr)
        return "An operation with this ID already exists.";
      const document::OperationId addedId = data.operation.id;
      const document::SignalRef addedOutput = document::primaryOutput(data.operation);
      const std::size_t index = std::min(data.index, document.operations.size());
      document.operations.insert(document.operations.begin() + static_cast<std::ptrdiff_t>(index),
        data.operation);
      document.nextOperationIdentity = std::max(document.nextOperationIdentity,
        addedId.value + 1);
      if (data.setAsFinal) document.presentation.input = addedOutput;
    } else if constexpr (std::is_same_v<Type, RemoveOperation>) {
      const auto found = std::find_if(document.operations.begin(), document.operations.end(),
        [&data](const document::Operation& operation) { return operation.id == data.operation; });
      if (found == document.operations.end()) return "The operation no longer exists.";
      document.operations.erase(found);
      const document::ObjectId owner = document::operationObject(data.operation);
      std::erase_if(document.automation.animation,
        [owner](const document::AnimationTrack& track) { return track.target.owner == owner; });
      std::erase_if(document.automation.modulation,
        [owner, operation = data.operation](const document::ModulationRoute& route) {
          return route.target.owner == owner || route.source.id.producer == operation;
        });
    } else if constexpr (std::is_same_v<Type, DuplicateOperation>) {
      if (const std::string error = duplicateOperation(document, data.source, data.duplicate,
          data.index); !error.empty()) return error;
    } else if constexpr (std::is_same_v<Type, DuplicateAndBlend>) {
      const document::Operation* source = document::findOperation(document, data.source);
      if (source == nullptr) return "The source operation no longer exists.";
      const document::SignalRef sourceOutput = document::primaryOutput(*source);
      const document::SignalDescriptor* descriptor = document::findSignal(document, sourceOutput.id);
      if (descriptor == nullptr || !document::isColor(*descriptor))
        return "Duplicate + Blend requires a Color output.";
      const std::string outputKey = descriptor->key;
      if (!data.composite || data.composite == data.duplicate ||
          document::findOperation(document, data.composite) != nullptr)
        return "The Composite operation ID is invalid or already exists.";
      const auto sourcePosition = std::find_if(document.operations.begin(), document.operations.end(),
        [&](const document::Operation& operation) { return operation.id == data.source; });
      const std::size_t duplicateIndex = static_cast<std::size_t>(
        std::distance(document.operations.begin(), sourcePosition)) + 1;
      if (const std::string error = duplicateOperation(document, data.source, data.duplicate,
          duplicateIndex); !error.empty()) return error;
      document::Operation composite = document::makeCompositeOperation(data.composite, "Composite",
        sourceOutput, document::SignalRef{document::operationSignal(data.duplicate, outputKey), 0});
      std::get<document::CompositeOperation>(composite.data).arithmetic.operation = data.operation;
      document.presentation.input = document::primaryOutput(composite);
      document.operations.insert(document.operations.begin() + static_cast<std::ptrdiff_t>(duplicateIndex + 1),
        std::move(composite));
      document.nextOperationIdentity = std::max(document.nextOperationIdentity,
        data.composite.value + 1);
    } else if constexpr (std::is_same_v<Type, MoveOperation>) {
      const auto found = std::find_if(document.operations.begin(), document.operations.end(),
        [&data](const document::Operation& operation) { return operation.id == data.operation; });
      if (found == document.operations.end()) return "The operation no longer exists.";
      const std::size_t from = static_cast<std::size_t>(std::distance(document.operations.begin(), found));
      const std::size_t to = std::min(data.index, document.operations.size() - 1);
      if (from != to) {
        document::Operation moved = std::move(document.operations[from]);
        document.operations.erase(document.operations.begin() + static_cast<std::ptrdiff_t>(from));
        document.operations.insert(document.operations.begin() + static_cast<std::ptrdiff_t>(to),
          std::move(moved));
      }
    } else if constexpr (std::is_same_v<Type, SetOperationEnabled>) {
      document::Operation* operation = document::findOperation(document, data.operation);
      if (operation == nullptr) return "The operation no longer exists.";
      operation->enabled = data.enabled;
    } else if constexpr (std::is_same_v<Type, ReplaceDocument>) {
      document = data.document;
    } else if constexpr (std::is_same_v<Type, ConnectSignal>) {
      document::Operation* operation = document::findOperation(document, data.operation);
      if (operation == nullptr) return "The target operation no longer exists.";
      bool connected = false;
      std::visit([&](auto& operationData) {
        using OperationType = std::decay_t<decltype(operationData)>;
        if constexpr (std::is_same_v<OperationType, document::InterpretOperation>) {
          if (data.socket == InputSocket::Primary) { operationData.spectrum = data.signal; connected = true; }
        } else if constexpr (std::is_same_v<OperationType, document::CompositeOperation>) {
          if (data.socket == InputSocket::A) { operationData.a = data.signal; connected = true; }
          if (data.socket == InputSocket::B) { operationData.b = data.signal; connected = true; }
          if (data.socket == InputSocket::Mask) { operationData.mask = data.signal; connected = true; }
        } else if constexpr (std::is_same_v<OperationType, document::StereoOperation>) {
          if (data.socket == InputSocket::Left) { operationData.left = data.signal; connected = true; }
          if (data.socket == InputSocket::Right) { operationData.right = data.signal; connected = true; }
        } else if constexpr (std::is_same_v<OperationType, document::MeasureOperation>) {
          if (data.socket == InputSocket::Primary) { operationData.input = data.signal; connected = true; }
        } else if constexpr (std::is_same_v<OperationType, document::LuminanceOperation> ||
            std::is_same_v<OperationType, document::RemapOperation> ||
            std::is_same_v<OperationType, document::EdgeOperation> ||
            std::is_same_v<OperationType, document::BlurOperation> ||
            std::is_same_v<OperationType, document::ThresholdOperation> ||
            std::is_same_v<OperationType, document::GradientMapOperation>) {
          if (data.socket == InputSocket::Primary) { operationData.input = data.signal; connected = true; }
        } else if constexpr (std::is_same_v<OperationType, document::WarpOperation>) {
          if (data.socket == InputSocket::Image) { operationData.image = data.signal; connected = true; }
          if (data.socket == InputSocket::Displacement) {
            operationData.displacement = data.signal; connected = true;
          }
        }
      }, operation->data);
      if (!connected) return "That input socket does not exist on the target operation.";
    } else if constexpr (std::is_same_v<Type, SetFinalSignal>) {
      document.presentation.input = data.signal;
    } else if constexpr (std::is_same_v<Type, SetRenderOverride>) {
      document::Operation* operation = document::findOperation(document, data.operation);
      if (operation == nullptr) return "The target operation no longer exists.";
      auto* render = std::get_if<document::RenderOperation>(&operation->data);
      if (render == nullptr) return "Only a Render operation owns renderer overrides.";
      const auto found = std::find_if(render->overrides.begin(), render->overrides.end(),
        [&data](const PropertyOverride& property) { return property.property == data.property; });
      if (!data.value.has_value()) {
        if (found != render->overrides.end()) render->overrides.erase(found);
      } else if (found == render->overrides.end()) {
        render->overrides.push_back({data.property, *data.value});
      } else {
        found->value = *data.value;
      }
    } else if constexpr (std::is_same_v<Type, SetKeyframe>) {
      document::AnimationTrack* track = findTrack(document, data.target);
      if (track == nullptr) {
        document.automation.animation.push_back({data.target, data.interpolation, {}});
        track = &document.automation.animation.back();
      }
      track->interpolation = data.interpolation;
      const auto key = std::find_if(track->keyframes.begin(), track->keyframes.end(),
        [&data](const PropertyKeyframe& frame) {
          return std::abs(frame.timeSeconds - data.timeSeconds) <= 1.0f / 120.0f;
        });
      if (key == track->keyframes.end()) track->keyframes.push_back({data.timeSeconds, data.value});
      else key->value = data.value;
      std::sort(track->keyframes.begin(), track->keyframes.end(),
        [](const PropertyKeyframe& a, const PropertyKeyframe& b) {
          return a.timeSeconds < b.timeSeconds;
        });
    } else if constexpr (std::is_same_v<Type, RemoveKeyframe>) {
      document::AnimationTrack* track = findTrack(document, data.target);
      if (track == nullptr) return "The animation track no longer exists.";
      const auto key = std::find_if(track->keyframes.begin(), track->keyframes.end(),
        [&data](const PropertyKeyframe& frame) {
          return std::abs(frame.timeSeconds - data.timeSeconds) <= data.toleranceSeconds;
        });
      if (key == track->keyframes.end()) return "No keyframe exists at that time.";
      track->keyframes.erase(key);
      if (track->keyframes.empty()) {
        const auto emptyTrack = std::find_if(document.automation.animation.begin(),
          document.automation.animation.end(), [&data](const document::AnimationTrack& candidate) {
            return candidate.target == data.target;
          });
        document.automation.animation.erase(emptyTrack);
      }
    } else if constexpr (std::is_same_v<Type, ConnectModulation>) {
      document.automation.modulation.push_back(data.route);
    } else if constexpr (std::is_same_v<Type, RemoveModulation>) {
      if (data.index >= document.automation.modulation.size()) return "The modulation route no longer exists.";
      document.automation.modulation.erase(document.automation.modulation.begin() +
        static_cast<std::ptrdiff_t>(data.index));
    }
    return {};
  }, command);
}

} // namespace

CommandResult applyCommand(document::Document& document, const Command& command) {
  const document::Document before = document;
  CommandResult result;
  result.error = mutate(document, command);
  if (!result.error.empty()) {
    document = before;
    return result;
  }
  const evaluation::EvaluationPlan plan = evaluation::compileDocument(document);
  result.diagnostics = plan.diagnostics;
  if (!plan.valid()) {
    document = before;
    result.error = "The edit would make the rendering dataflow invalid.";
    return result;
  }
  result.applied = true;
  return result;
}

CommandResult CommandHistory::execute(document::Document& document, const Command& command) {
  finishContinuous(document);
  const document::Document before = document;
  CommandResult result = applyCommand(document, command);
  if (result.applied) {
    pushBounded(undo_, before);
    redo_.clear();
  }
  return result;
}

CommandResult CommandHistory::executeContinuous(document::Document& document,
    const Command& command) {
  const document::Document before = document;
  CommandResult result = applyCommand(document, command);
  if (result.applied) {
    if (!continuousStart_.has_value()) continuousStart_ = before;
    redo_.clear();
  }
  return result;
}

void CommandHistory::finishContinuous(const document::Document&) {
  if (!continuousStart_.has_value()) return;
  pushBounded(undo_, *continuousStart_);
  continuousStart_.reset();
}

bool CommandHistory::undo(document::Document& document) {
  finishContinuous(document);
  if (undo_.empty()) return false;
  pushBounded(redo_, document);
  document = std::move(undo_.back());
  undo_.pop_back();
  return true;
}

bool CommandHistory::redo(document::Document& document) {
  finishContinuous(document);
  if (redo_.empty()) return false;
  pushBounded(undo_, document);
  document = std::move(redo_.back());
  redo_.pop_back();
  return true;
}

void CommandHistory::clear() {
  undo_.clear();
  redo_.clear();
  continuousStart_.reset();
}

void CommandHistory::pushBounded(std::vector<document::Document>& history,
    const document::Document& document) {
  if (history.size() == maximumEntries) history.erase(history.begin());
  history.push_back(document);
}

} // namespace gfxlab::editor
