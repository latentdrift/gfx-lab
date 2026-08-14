#include "document/Properties.hpp"

#include "app/RenderStack.hpp"

#include <array>
#include <cstdint>
#include <string_view>
#include <vector>

namespace gfxlab::document {
namespace {

std::uint64_t stableHash(const std::string_view value) {
  std::uint64_t hash = 1469598103934665603ULL;
  for (const unsigned char character : value) {
    hash ^= character;
    hash *= 1099511628211ULL;
  }
  return hash == 0 ? 1 : hash;
}

PropertyType propertyType(const AnimationPropertyInfo& info) {
  switch (info.kind) {
    case AnimationValueKind::Boolean: return PropertyType::Boolean;
    case AnimationValueKind::Integer: return PropertyType::Integer;
    case AnimationValueKind::Enumeration: return PropertyType::Enumeration;
    case AnimationValueKind::Angle: return PropertyType::Float;
    case AnimationValueKind::Color3: return PropertyType::Color3;
    case AnimationValueKind::Color4: return PropertyType::Color4;
    case AnimationValueKind::Vec2: return PropertyType::Vector2;
    case AnimationValueKind::Vec3: return PropertyType::Vector3;
    case AnimationValueKind::Float: return PropertyType::Float;
  }
  return PropertyType::Float;
}

PropertyUnits propertyUnits(const AnimationPropertyInfo& info) {
  const std::string_view id = info.id;
  if (id.find("degrees") != std::string_view::npos || id.find("azimuth") != std::string_view::npos ||
      id.find("elevation") != std::string_view::npos || id.find("field_of_view") != std::string_view::npos)
    return PropertyUnits::Degrees;
  if (id.find("rotation") != std::string_view::npos || id.find("phase") != std::string_view::npos ||
      id.find("yaw") != std::string_view::npos || id.find("pitch") != std::string_view::npos)
    return PropertyUnits::Radians;
  if (id.find("pixel") != std::string_view::npos || id.find("resolution") != std::string_view::npos)
    return PropertyUnits::Pixels;
  if (id.find("seconds") != std::string_view::npos || id.find("duration") != std::string_view::npos)
    return PropertyUnits::Seconds;
  if (id.find("exposure") != std::string_view::npos) return PropertyUnits::Stops;
  if (id.find("position") != std::string_view::npos || id.find("distance") != std::string_view::npos ||
      id.find("height") != std::string_view::npos || id.find("plane") != std::string_view::npos ||
      id.find("wavelength") != std::string_view::npos || id.find("translation") != std::string_view::npos)
    return PropertyUnits::Unit;
  if (id.find("scale") != std::string_view::npos || id.find("gain") != std::string_view::npos ||
      id.find("strength") != std::string_view::npos || id.find("opacity") != std::string_view::npos)
    return PropertyUnits::Ratio;
  return PropertyUnits::None;
}

const std::vector<PropertyDescriptor>& registry() {
  static const std::vector<PropertyDescriptor> descriptors = [] {
    std::vector<PropertyDescriptor> result;
    result.reserve(static_cast<std::size_t>(AnimationProperty::Count));
    for (std::size_t index = 0; index < static_cast<std::size_t>(AnimationProperty::Count); ++index) {
      const AnimationProperty property = static_cast<AnimationProperty>(index);
      const AnimationPropertyInfo& info = animationPropertyInfo(property);
      result.push_back({PropertyId{stableHash(info.id)}, property, std::string(info.id),
        std::string(info.label), std::string(info.group), propertyType(info), propertyUnits(info),
        info.components, info.minimum, info.maximum, info.behavior, true,
        animationPropertyIsPassLocal(property)});
    }
    return result;
  }();
  return descriptors;
}

} // namespace

PropertyId propertyId(const AnimationProperty property) {
  return PropertyId{stableHash(animationPropertyInfo(property).id)};
}

std::optional<AnimationProperty> legacyAnimationProperty(const PropertyId property) {
  const PropertyDescriptor* descriptor = propertyDescriptor(property);
  return descriptor == nullptr ? std::nullopt : std::optional{descriptor->legacyProperty};
}

const PropertyDescriptor* propertyDescriptor(const PropertyId property) {
  for (const PropertyDescriptor& descriptor : registry())
    if (descriptor.id == property) return &descriptor;
  return nullptr;
}

std::span<const PropertyDescriptor> propertyDescriptors() { return registry(); }

} // namespace gfxlab::document
