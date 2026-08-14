#include "renderer/TextureReadback.hpp"

#include "app/RenderStack.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <cmath>
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

SignalMeasurement measureTextureSignal(const unsigned int texture, const float threshold,
    const bool absoluteMagnitude) {
  const TextureDimensions source = textureDimensions(texture);
  if (source.width <= 0 || source.height <= 0) return {};

  GLint sourceInternalFormat = 0;
  GLint previousSourceTexture = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousSourceTexture);
  glBindTexture(GL_TEXTURE_2D, texture);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_INTERNAL_FORMAT, &sourceInternalFormat);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousSourceTexture));
  const bool scalarTexture = sourceInternalFormat == GL_R8 || sourceInternalFormat == GL_R16 ||
    sourceInternalFormat == GL_R16F || sourceInternalFormat == GL_R32F;

  constexpr int sampleWidth = 64;
  constexpr int sampleHeight = 64;
  GLint previousReadFramebuffer = 0;
  GLint previousDrawFramebuffer = 0;
  GLint previousTexture = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &previousTexture);

  GLuint sourceFramebuffer = 0;
  GLuint sampleFramebuffer = 0;
  GLuint sampleTexture = 0;
  glGenFramebuffers(1, &sourceFramebuffer);
  glGenFramebuffers(1, &sampleFramebuffer);
  glGenTextures(1, &sampleTexture);
  glBindTexture(GL_TEXTURE_2D, sampleTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, sampleWidth, sampleHeight, 0, GL_RGBA, GL_FLOAT, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);

  glBindFramebuffer(GL_READ_FRAMEBUFFER, sourceFramebuffer);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, sampleFramebuffer);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sampleTexture, 0);
  glDrawBuffer(GL_COLOR_ATTACHMENT0);

  SignalMeasurement result;
  if (glCheckFramebufferStatus(GL_READ_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE &&
      glCheckFramebufferStatus(GL_DRAW_FRAMEBUFFER) == GL_FRAMEBUFFER_COMPLETE) {
    glBlitFramebuffer(0, 0, source.width, source.height, 0, 0, sampleWidth, sampleHeight,
      GL_COLOR_BUFFER_BIT, GL_NEAREST);
    std::vector<glm::vec4> samples(static_cast<std::size_t>(sampleWidth * sampleHeight));
    glBindFramebuffer(GL_READ_FRAMEBUFFER, sampleFramebuffer);
    glReadPixels(0, 0, sampleWidth, sampleHeight, GL_RGBA, GL_FLOAT, samples.data());
    float squaredMagnitudeSum = 0.0f;
    int activeSamples = 0;
    for (const glm::vec4& sample : samples) {
      const glm::vec3 channels(sample);
      result.meanChannels += channels;
      const glm::vec3 measured = absoluteMagnitude ? glm::abs(channels) : glm::max(channels, glm::vec3(0.0f));
      const float magnitude = scalarTexture ? measured.r :
        std::sqrt(glm::dot(measured, measured) / 3.0f);
      result.meanMagnitude += magnitude;
      squaredMagnitudeSum += magnitude * magnitude;
      result.peakMagnitude = std::max(result.peakMagnitude, magnitude);
      if (magnitude >= threshold) ++activeSamples;
    }
    result.sampleCount = static_cast<int>(samples.size());
    const float inverseCount = 1.0f / static_cast<float>(result.sampleCount);
    result.meanChannels *= inverseCount;
    result.meanMagnitude *= inverseCount;
    result.rmsMagnitude = std::sqrt(squaredMagnitudeSum * inverseCount);
    result.coverage = static_cast<float>(activeSamples) * inverseCount;
  }

  glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(previousTexture));
  glDeleteTextures(1, &sampleTexture);
  glDeleteFramebuffers(1, &sampleFramebuffer);
  glDeleteFramebuffers(1, &sourceFramebuffer);
  return result;
}

float measurementMetricValue(const SignalMeasurement& measurement, const MeasurementMetric metric) {
  switch (metric) {
    case MeasurementMetric::MeanMagnitude: return measurement.meanMagnitude;
    case MeasurementMetric::RmsMagnitude: return measurement.rmsMagnitude;
    case MeasurementMetric::PeakMagnitude: return measurement.peakMagnitude;
    case MeasurementMetric::Coverage: return measurement.coverage;
    case MeasurementMetric::MeanRed: return measurement.meanChannels.r;
    case MeasurementMetric::MeanGreen: return measurement.meanChannels.g;
    case MeasurementMetric::MeanBlue: return measurement.meanChannels.b;
  }
  return 0.0f;
}

} // namespace gfxlab
