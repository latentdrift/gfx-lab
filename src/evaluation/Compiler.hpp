#pragma once

#include "evaluation/EvaluationPlan.hpp"

namespace gfxlab::evaluation {

[[nodiscard]] EvaluationPlan compileDocument(const document::Document& document);

} // namespace gfxlab::evaluation
