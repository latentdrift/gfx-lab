#pragma once

#include "app/State.hpp"

#include <cstdio>
#include <string>
#include <vector>

namespace gfxlab {

class ViewportRecorder {
public:
  static constexpr int width = 960;
  static constexpr int height = 720;
  static constexpr int framesPerSecond = 30;

  ViewportRecorder() = default;
  ~ViewportRecorder();

  ViewportRecorder(const ViewportRecorder&) = delete;
  ViewportRecorder& operator=(const ViewportRecorder&) = delete;

  bool start(const std::string& outputPath, double nowSeconds, std::string& error);
  bool stop(std::string& error);
  void capture(unsigned int selectedTexture, unsigned int baseTexture,
    unsigned int compositeTexture, CompareMode mode, double nowSeconds);

  [[nodiscard]] bool recording() const { return rawFile_ != nullptr; }
  [[nodiscard]] double durationSeconds() const {
    return static_cast<double>(frameCount_) / static_cast<double>(framesPerSecond);
  }
  [[nodiscard]] const std::string& outputPath() const { return outputPath_; }

private:
  void initializeCaptureTarget();
  void drawTexture(unsigned int texture, int sourceX0, int sourceX1, int destinationX0,
    int destinationX1);
  void captureFrame(unsigned int selectedTexture, unsigned int baseTexture,
    unsigned int compositeTexture, CompareMode mode);
  void discardRawFile();

  std::FILE* rawFile_{nullptr};
  std::string rawPath_;
  std::string outputPath_;
  double nextFrameTime_{0.0};
  std::size_t frameCount_{0};
  unsigned int readFramebuffer_{0};
  unsigned int captureFramebuffer_{0};
  unsigned int captureTexture_{0};
  std::vector<unsigned char> pixels_;
  std::vector<unsigned char> flippedPixels_;
};

} // namespace gfxlab
