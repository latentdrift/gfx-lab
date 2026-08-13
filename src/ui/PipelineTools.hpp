#pragma once

#include "app/State.hpp"

#include <array>
#include <cstddef>

namespace gfxlab {
class RenderStack;
struct AnimationTimeline;
struct ModelAsset;
enum class HardwareProfile;
}

namespace gfxlab::ui {

inline constexpr std::size_t pipelineToolCount = 13;
inline constexpr std::array<Category, pipelineToolCount> pipelineCategories = {Category::Geometry,
  Category::Camera, Category::Rasterization, Category::Surface, Category::Texture, Category::Lighting,
  Category::Field, Category::Spectral, Category::Depth, Category::Stencil, Category::Color, Category::Post, Category::Output};

struct PipelineToolWindows {
  std::array<bool, pipelineToolCount> open = {true, true, true, true, true, true, true, true, true, true, true, true, true};
};

[[nodiscard]] const char* pipelineToolWindowName(Category category);
void drawPipelineTools(PipelineToolWindows& windows, RenderStack& stack, AnimationTimeline& timeline,
  HardwareProfile profile, const ModelAsset* importedModel, TestScene scene, bool globalScope,
  float timeSeconds, Category focusCategory, bool focusRequested);

} // namespace gfxlab::ui
