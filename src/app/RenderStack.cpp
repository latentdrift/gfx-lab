#include "app/RenderStack.hpp"

#include <algorithm>
#include <utility>

namespace gfxlab {

RenderStack::RenderStack() {
  RenderPass a;
  a.name = "Pass A";
  RenderPass b = a;
  b.name = "Pass B";
  passes_ = {std::move(a), std::move(b)};
}

RenderPass& RenderStack::selected() { return passes_[selected_]; }
const RenderPass& RenderStack::selected() const { return passes_[selected_]; }

void RenderStack::select(const std::size_t index) {
  if (!passes_.empty()) selected_ = std::min(index, passes_.size() - 1);
}

bool RenderStack::duplicateSelected() {
  if (passes_.size() >= maximumPasses) return false;
  RenderPass duplicate = selected();
  duplicate.name = "Pass " + std::to_string(nextPassNumber_++);
  passes_.insert(passes_.begin() + static_cast<std::ptrdiff_t>(selected_ + 1), std::move(duplicate));
  ++selected_;
  return true;
}

bool RenderStack::removeSelected() {
  if (passes_.size() <= 1) return false;
  passes_.erase(passes_.begin() + static_cast<std::ptrdiff_t>(selected_));
  if (selected_ >= passes_.size()) selected_ = passes_.size() - 1;
  return true;
}

bool RenderStack::moveSelected(const int direction) {
  if (direction == 0 || passes_.size() < 2) return false;
  const std::ptrdiff_t destination = static_cast<std::ptrdiff_t>(selected_) + (direction < 0 ? -1 : 1);
  if (destination < 0 || destination >= static_cast<std::ptrdiff_t>(passes_.size())) return false;
  std::swap(passes_[selected_], passes_[static_cast<std::size_t>(destination)]);
  selected_ = static_cast<std::size_t>(destination);
  return true;
}

} // namespace gfxlab
