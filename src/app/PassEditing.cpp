#include "app/PassEditing.hpp"

#include <array>
#include <cmath>
#include <utility>

namespace gfxlab {
namespace {

bool animationValueEqual(const glm::vec4& a, const glm::vec4& b, const int components) {
  for (int component = 0; component < components; ++component)
    if (std::abs(a[component] - b[component]) > 0.000001f) return false;
  return true;
}

} // namespace

void applyEditedPass(RenderPass& authored, const RenderPass& displayedBefore, RenderPass edited) {
  const RenderPass authoredBefore = authored;
  const PassAnimation editedAnimation = edited.animation;
  std::array<bool, static_cast<std::size_t>(AnimationProperty::Count)> propertyChanged{};
  std::array<glm::vec4, static_cast<std::size_t>(AnimationProperty::Count)> displayedValues{};
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    const std::size_t propertyIndex = static_cast<std::size_t>(index);
    displayedValues[propertyIndex] = animationPropertyValue(edited, property);
    propertyChanged[propertyIndex] = !animationValueEqual(animationPropertyValue(displayedBefore, property),
      displayedValues[propertyIndex], animationPropertyInfo(property).components);
    setAnimationPropertyValue(edited, property, animationPropertyValue(authoredBefore, property));
  }
  edited.animation = editedAnimation;
  authored = std::move(edited);
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    const std::size_t propertyIndex = static_cast<std::size_t>(index);
    if (propertyChanged[propertyIndex] && findPropertyTrack(authored, property) == nullptr)
      setAnimationPropertyValue(authored, property, displayedValues[propertyIndex]);
  }
}

void applyEditedLocalPass(RenderStack& stack, const RenderPass& displayedBefore, const RenderPass& edited,
    const float timeSeconds) {
  RenderPass& definition = stack.selected();
  const RenderPass definitionBefore = definition;
  definition.name = edited.name;
  definition.enabled = edited.enabled;
  definition.output = edited.output;
  definition.composite = edited.composite;
  definition.animation = edited.animation;
  const RenderPass evaluatedGlobal = evaluateRenderPass(stack.global(), timeSeconds);
  for (int index = 0; index < static_cast<int>(AnimationProperty::Count); ++index) {
    const AnimationProperty property = static_cast<AnimationProperty>(index);
    const glm::vec4 displayedValue = animationPropertyValue(displayedBefore, property);
    const glm::vec4 editedValue = animationPropertyValue(edited, property);
    if (animationPropertyValuesEqual(property, displayedValue, editedValue)) continue;
    if (findPropertyTrack(edited, property) != nullptr) continue;
    if (animationPropertyIsPassLocal(property)) {
      setAnimationPropertyValue(definition, property, editedValue);
      continue;
    }
    const glm::vec4 globalValue = animationPropertyValue(evaluatedGlobal, property);
    if (animationPropertyValuesEqual(property, globalValue, editedValue))
      static_cast<void>(clearRenderPassOverride(definition, property));
    else
      setRenderPassOverride(definition, property, editedValue);
  }
  if (edited.importedTexture != displayedBefore.importedTexture) {
    definition.importedTextureOverride = edited.importedTexture != stack.global().importedTexture;
    definition.importedTexture = definition.importedTextureOverride ? edited.importedTexture : nullptr;
  } else {
    definition.importedTextureOverride = definitionBefore.importedTextureOverride;
    definition.importedTexture = definitionBefore.importedTexture;
  }
}

} // namespace gfxlab
