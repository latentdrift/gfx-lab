#pragma once

#include <glm/glm.hpp>

namespace gfxlab {

struct TextureDimensions {
  int width = 0;
  int height = 0;
};

struct SignalMeasurement {
  glm::vec3 meanChannels{0.0f};
  float meanMagnitude = 0.0f;
  float rmsMagnitude = 0.0f;
  float peakMagnitude = 0.0f;
  float coverage = 0.0f;
  int sampleCount = 0;
};

[[nodiscard]] TextureDimensions textureDimensions(unsigned int texture);
[[nodiscard]] glm::vec4 readTexturePixel(unsigned int texture, int x, int y);
[[nodiscard]] SignalMeasurement measureTextureSignal(unsigned int texture, float threshold,
  bool absoluteMagnitude);

} // namespace gfxlab
