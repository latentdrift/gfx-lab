#pragma once

#include "app/Animation.hpp"
#include "document/Identifiers.hpp"

#include <optional>
#include <span>
#include <string>

namespace gfxlab::document {

enum class PropertyType { Float, Integer, Boolean, Enumeration, Vector2, Vector3, Color3, Color4 };
enum class PropertyOwnerKind { Scene, RenderDefaults, RenderOperation, CompositeOperation, Presentation };
enum class PropertyUnits { None, Unit, Degrees, Radians, Pixels, Seconds, Stops, Ratio };

struct PropertyDescriptor {
  PropertyId id;
  AnimationProperty legacyProperty = AnimationProperty::VertexQuantization;
  std::string stableName;
  std::string label;
  std::string group;
  PropertyType type = PropertyType::Float;
  PropertyUnits units = PropertyUnits::None;
  int components = 1;
  float minimum = 0.0f;
  float maximum = 1.0f;
  AnimationBehavior animation = AnimationBehavior::Continuous;
  bool availableOnRenderDefaults = true;
  bool availableOnRenderOperation = true;
};

[[nodiscard]] PropertyId propertyId(AnimationProperty property);
[[nodiscard]] std::optional<AnimationProperty> legacyAnimationProperty(PropertyId property);
[[nodiscard]] const PropertyDescriptor* propertyDescriptor(PropertyId property);
[[nodiscard]] std::span<const PropertyDescriptor> propertyDescriptors();

} // namespace gfxlab::document
