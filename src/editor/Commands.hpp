#pragma once

#include "document/Document.hpp"
#include "evaluation/EvaluationPlan.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <variant>
#include <vector>

namespace gfxlab::editor {

struct AddOperation {
  document::Operation operation;
  std::size_t index = static_cast<std::size_t>(-1);
  bool setAsFinal = false;
};
struct RemoveOperation { document::OperationId operation; };
struct DuplicateOperation {
  document::OperationId source;
  document::OperationId duplicate;
  std::size_t index = static_cast<std::size_t>(-1);
};
struct DuplicateAndBlend {
  document::OperationId source;
  document::OperationId duplicate;
  document::OperationId composite;
  RelationOperator operation = RelationOperator::Normal;
};
struct MoveOperation { document::OperationId operation; std::size_t index = 0; };
struct SetOperationEnabled { document::OperationId operation; bool enabled = true; };
struct ReplaceDocument { document::Document document; };

enum class InputSocket { Primary, A, B, Left, Right };
struct ConnectSignal {
  document::OperationId operation;
  InputSocket socket = InputSocket::Primary;
  document::SignalRef signal;
};

struct SetFinalSignal { document::SignalRef signal; };
struct SetRenderOverride {
  document::OperationId operation;
  AnimationProperty property = AnimationProperty::VertexQuantization;
  std::optional<glm::vec4> value;
};
struct SetKeyframe {
  document::PropertyAddress target;
  float timeSeconds = 0.0f;
  glm::vec4 value{0.0f};
  KeyframeInterpolation interpolation = KeyframeInterpolation::Linear;
};
struct RemoveKeyframe {
  document::PropertyAddress target;
  float timeSeconds = 0.0f;
  float toleranceSeconds = 1.0f / 120.0f;
};
struct ConnectModulation { document::ModulationRoute route; };
struct RemoveModulation { std::size_t index = 0; };

using Command = std::variant<AddOperation, RemoveOperation, DuplicateOperation, DuplicateAndBlend,
  MoveOperation, SetOperationEnabled, ReplaceDocument, ConnectSignal,
  SetFinalSignal, SetRenderOverride, SetKeyframe, RemoveKeyframe, ConnectModulation,
  RemoveModulation>;

struct CommandResult {
  bool applied = false;
  std::string error;
  std::vector<evaluation::OperationDiagnostic> diagnostics;
};

[[nodiscard]] CommandResult applyCommand(document::Document& document, const Command& command);

class CommandHistory {
public:
  static constexpr std::size_t maximumEntries = 256;

  [[nodiscard]] CommandResult execute(document::Document& document, const Command& command);
  [[nodiscard]] CommandResult executeContinuous(document::Document& document, const Command& command);
  void finishContinuous(const document::Document& document);
  [[nodiscard]] bool undo(document::Document& document);
  [[nodiscard]] bool redo(document::Document& document);
  [[nodiscard]] bool canUndo() const { return !undo_.empty(); }
  [[nodiscard]] bool canRedo() const { return !redo_.empty(); }
  void clear();

private:
  static void pushBounded(std::vector<document::Document>& history,
    const document::Document& document);
  std::vector<document::Document> undo_;
  std::vector<document::Document> redo_;
  std::optional<document::Document> continuousStart_;
};

} // namespace gfxlab::editor
