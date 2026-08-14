#pragma once

#include "evaluation/EvaluationPlan.hpp"

#include <vector>

namespace gfxlab::evaluation {

[[nodiscard]] EvaluationPlan compileDocument(const document::Document& document);
[[nodiscard]] EvaluationPlan restrictEvaluationPlan(const EvaluationPlan& plan,
  const std::vector<document::SignalRef>& requiredSignals);

} // namespace gfxlab::evaluation
