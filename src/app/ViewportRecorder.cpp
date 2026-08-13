#include "app/ViewportRecorder.hpp"

#include <GL/glew.h>

#include <algorithm>
#include <cerrno>
#include <cstring>

#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#ifndef GRAPHICS_LAB_FFMPEG_EXECUTABLE
#define GRAPHICS_LAB_FFMPEG_EXECUTABLE "ffmpeg"
#endif

namespace gfxlab {

ViewportRecorder::~ViewportRecorder() {
  discardRawFile();
  if (captureTexture_ != 0) glDeleteTextures(1, &captureTexture_);
  if (captureFramebuffer_ != 0) glDeleteFramebuffers(1, &captureFramebuffer_);
  if (readFramebuffer_ != 0) glDeleteFramebuffers(1, &readFramebuffer_);
}

bool ViewportRecorder::start(const std::string& outputPath, const double nowSeconds,
    std::string& error) {
  if (recording()) return true;
  std::string pattern = "/tmp/graphics-lab-recording-XXXXXX";
  std::vector<char> writablePattern(pattern.begin(), pattern.end());
  writablePattern.push_back('\0');
  const int descriptor = mkstemp(writablePattern.data());
  if (descriptor < 0) {
    error = std::string("Could not create the temporary recording: ") + std::strerror(errno);
    return false;
  }
  rawFile_ = fdopen(descriptor, "wb");
  if (rawFile_ == nullptr) {
    error = std::string("Could not open the temporary recording: ") + std::strerror(errno);
    close(descriptor);
    unlink(writablePattern.data());
    return false;
  }
  rawPath_ = writablePattern.data();
  outputPath_ = outputPath;
  nextFrameTime_ = nowSeconds;
  frameCount_ = 0;
  return true;
}

void ViewportRecorder::initializeCaptureTarget() {
  if (captureFramebuffer_ != 0) return;
  glGenFramebuffers(1, &readFramebuffer_);
  glGenFramebuffers(1, &captureFramebuffer_);
  glGenTextures(1, &captureTexture_);
  glBindTexture(GL_TEXTURE_2D, captureTexture_);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, captureFramebuffer_);
  glFramebufferTexture2D(GL_DRAW_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
    captureTexture_, 0);
  pixels_.resize(static_cast<std::size_t>(width * height * 4));
  flippedPixels_.resize(pixels_.size());
}

void ViewportRecorder::drawTexture(const unsigned int texture, const int sourceX0,
    const int sourceX1, const int destinationX0, const int destinationX1) {
  if (texture == 0) return;
  glBindTexture(GL_TEXTURE_2D, texture);
  GLint textureWidth = 0;
  GLint textureHeight = 0;
  GLint filter = GL_LINEAR;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &textureWidth);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &textureHeight);
  glGetTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, &filter);
  if (textureWidth <= 0 || textureHeight <= 0) return;
  glBindFramebuffer(GL_READ_FRAMEBUFFER, readFramebuffer_);
  glFramebufferTexture2D(GL_READ_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, captureFramebuffer_);
  const int inputX0 = sourceX0 * textureWidth / width;
  const int inputX1 = sourceX1 * textureWidth / width;
  glBlitFramebuffer(inputX0, 0, inputX1, textureHeight,
    destinationX0, 0, destinationX1, height, GL_COLOR_BUFFER_BIT,
    filter == GL_NEAREST ? GL_NEAREST : GL_LINEAR);
}

void ViewportRecorder::captureFrame(const unsigned int selectedTexture,
    const unsigned int baseTexture, const unsigned int compositeTexture, const CompareMode mode) {
  initializeCaptureTarget();
  GLint previousReadFramebuffer = 0;
  GLint previousDrawFramebuffer = 0;
  GLint previousPackAlignment = 0;
  glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
  glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previousDrawFramebuffer);
  glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, captureFramebuffer_);
  glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
  glClear(GL_COLOR_BUFFER_BIT);
  if (mode == CompareMode::Split) {
    drawTexture(baseTexture, 0, width / 2, 0, width / 2);
    drawTexture(selectedTexture, width / 2, width, width / 2, width);
  } else {
    const unsigned int texture = mode == CompareMode::A ? selectedTexture
      : mode == CompareMode::B ? baseTexture : compositeTexture;
    drawTexture(texture, 0, width, 0, width);
  }
  glBindFramebuffer(GL_READ_FRAMEBUFFER, captureFramebuffer_);
  glReadBuffer(GL_COLOR_ATTACHMENT0);
  glPixelStorei(GL_PACK_ALIGNMENT, 1);
  glReadPixels(0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels_.data());
  constexpr std::size_t rowBytes = static_cast<std::size_t>(width * 4);
  for (int row = 0; row < height; ++row) {
    const unsigned char* source = pixels_.data() + static_cast<std::size_t>(height - row - 1) * rowBytes;
    unsigned char* destination = flippedPixels_.data() + static_cast<std::size_t>(row) * rowBytes;
    std::copy_n(source, rowBytes, destination);
  }
  std::fwrite(flippedPixels_.data(), 1, flippedPixels_.size(), rawFile_);
  ++frameCount_;
  glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
  glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(previousReadFramebuffer));
  glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(previousDrawFramebuffer));
}

void ViewportRecorder::capture(const unsigned int selectedTexture, const unsigned int baseTexture,
    const unsigned int compositeTexture, const CompareMode mode, const double nowSeconds) {
  if (!recording()) return;
  constexpr double frameInterval = 1.0 / static_cast<double>(framesPerSecond);
  int catchUpFrames = 0;
  while (nowSeconds >= nextFrameTime_ && catchUpFrames < 4) {
    captureFrame(selectedTexture, baseTexture, compositeTexture, mode);
    nextFrameTime_ += frameInterval;
    ++catchUpFrames;
  }
  if (catchUpFrames == 4 && nowSeconds >= nextFrameTime_)
    nextFrameTime_ = nowSeconds + frameInterval;
}

bool ViewportRecorder::stop(std::string& error) {
  if (!recording()) return true;
  std::fclose(rawFile_);
  rawFile_ = nullptr;
  if (frameCount_ == 0) {
    error = "No viewport frames were captured.";
    discardRawFile();
    return false;
  }
  const std::string size = std::to_string(width) + "x" + std::to_string(height);
  const std::string rate = std::to_string(framesPerSecond);
  const pid_t child = fork();
  if (child == 0) {
    execl(GRAPHICS_LAB_FFMPEG_EXECUTABLE, GRAPHICS_LAB_FFMPEG_EXECUTABLE,
      "-hide_banner", "-loglevel", "error", "-y", "-f", "rawvideo",
      "-pixel_format", "rgba", "-video_size", size.c_str(), "-framerate", rate.c_str(),
      "-i", rawPath_.c_str(), "-an", "-c:v", "libx264", "-preset", "medium",
      "-crf", "18", "-pix_fmt", "yuv420p", "-movflags", "+faststart",
      outputPath_.c_str(), static_cast<char*>(nullptr));
    _exit(127);
  }
  int status = 0;
  const bool launched = child >= 0;
  if (launched) waitpid(child, &status, 0);
  unlink(rawPath_.c_str());
  rawPath_.clear();
  if (!launched || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
    error = "FFmpeg could not encode the viewport recording.";
    return false;
  }
  return true;
}

void ViewportRecorder::discardRawFile() {
  if (rawFile_ != nullptr) {
    std::fclose(rawFile_);
    rawFile_ = nullptr;
  }
  if (!rawPath_.empty()) {
    unlink(rawPath_.c_str());
    rawPath_.clear();
  }
}

} // namespace gfxlab
