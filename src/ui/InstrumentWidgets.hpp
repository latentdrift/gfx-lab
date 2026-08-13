#pragma once

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

namespace gfxlab::ui {

struct DirectionFieldResult {
  bool azimuthChanged{false};
  bool elevationChanged{false};
};

DirectionFieldResult directionField(const char* id, float& azimuthDegrees,
  float& elevationDegrees, float height = 138.0f);

struct IntervalFieldResult {
  bool startChanged{false};
  bool endChanged{false};
};

IntervalFieldResult intervalField(const char* id, float& start, float& end,
  float minimum, float maximum, const char* clearLabel, const char* fullLabel);

struct UvCanvasResult {
  bool offsetChanged{false};
  bool scaleChanged{false};
  bool rotationChanged{false};
};

UvCanvasResult uvTransformCanvas(const char* id, glm::vec2& offset,
  glm::vec2& scale, float& rotationRadians, glm::vec2 pivot,
  unsigned int texture, float height = 230.0f);

struct FieldSourceCanvasResult {
  bool sourceAChanged{false};
  bool sourceBChanged{false};
};

FieldSourceCanvasResult fieldSourceCanvas(const char* id, glm::vec3& sourceA,
  glm::vec3& sourceB, float wavelength, float phaseOffset, float amplitudeA,
  float amplitudeB, float falloff, float height = 230.0f);

} // namespace gfxlab::ui
