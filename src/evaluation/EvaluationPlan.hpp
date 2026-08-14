#pragma once

#include "document/Document.hpp"

#include <string>
#include <vector>

namespace gfxlab::evaluation {

enum class DiagnosticSeverity { Information, Warning, Error };

struct OperationDiagnostic {
  document::OperationId operation;
  DiagnosticSeverity severity = DiagnosticSeverity::Error;
  std::string message;
};

struct EvaluationNode {
  document::OperationId operation;
  std::vector<document::SignalRef> inputs;
  std::vector<document::SignalId> outputs;
};

struct EvaluationPlan {
  std::vector<EvaluationNode> nodes;
  std::vector<OperationDiagnostic> diagnostics;
  document::SignalRef finalSignal;

  [[nodiscard]] bool valid() const;
};

} // namespace gfxlab::evaluation
