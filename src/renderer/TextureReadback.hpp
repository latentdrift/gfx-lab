#pragma once

#include <glm/glm.hpp>

namespace gfxlab {

struct TextureDimensions {
  int width = 0;
  int height = 0;
};

[[nodiscard]] TextureDimensions textureDimensions(unsigned int texture);
[[nodiscard]] glm::vec4 readTexturePixel(unsigned int texture, int x, int y);

} // namespace gfxlab
