#include "renderer/TextureReadback.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <vector>

namespace gfxlab {

TextureDimensions textureDimensions(const unsigned int texture) {
  if (texture == 0) return {};
  GLint previous = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
  glBindTexture(GL_TEXTURE_2D, texture);
  TextureDimensions dimensions;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &dimensions.width);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &dimensions.height);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
  return dimensions;
}

glm::vec4 readTexturePixel(const unsigned int texture, const int x, const int y) {
  const TextureDimensions dimensions = textureDimensions(texture);
  if (dimensions.width <= 0 || dimensions.height <= 0) return {};
  std::vector<glm::vec4> pixels(static_cast<std::size_t>(dimensions.width * dimensions.height));
  GLint previous = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
  glBindTexture(GL_TEXTURE_2D, texture);
  glGetTexImage(GL_TEXTURE_2D, 0, GL_RGBA, GL_FLOAT, pixels.data());
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previous));
  const int clampedX = std::clamp(x, 0, dimensions.width - 1);
  const int clampedY = std::clamp(y, 0, dimensions.height - 1);
  return pixels[static_cast<std::size_t>(clampedY * dimensions.width + clampedX)];
}

} // namespace gfxlab
