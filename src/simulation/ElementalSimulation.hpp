#pragma once

#include "app/State.hpp"

#include <glm/glm.hpp>

#include <vector>

namespace gfxlab {

// A persistent, world-aligned 2.5D medium. The simulation owns physical state;
// render passes merely choose how to interpret one of its channels.
class ElementalSimulation {
public:
  static constexpr int width = 160;
  static constexpr int height = 96;

  ElementalSimulation();

  void reset();
  void update(float deltaSeconds, const RendererState::Field& controls);

  [[nodiscard]] const std::vector<glm::vec4>& matterPixels() const { return matterPixels_; }
  [[nodiscard]] const std::vector<glm::vec4>& dynamicsPixels() const { return dynamicsPixels_; }
  [[nodiscard]] float totalCombustion() const;
  [[nodiscard]] unsigned long long revision() const { return revision_; }

private:
  struct Cell {
    glm::vec2 velocity{0.0f};
    float pressure = 0.0f;
    float temperature = 0.0f;
    float fuel = 0.0f;
    float smoke = 0.0f;
    float moisture = 0.0f;
    float combustion = 0.0f;
    bool solid = false;
  };

  [[nodiscard]] int index(int x, int y) const;
  [[nodiscard]] Cell sample(const std::vector<Cell>& cells, glm::vec2 position) const;
  [[nodiscard]] glm::vec2 worldPosition(int x, int y) const;
  void step(float deltaSeconds, const RendererState::Field& controls);
  void rebuildPixels();

  std::vector<Cell> cells_;
  std::vector<Cell> scratch_;
  std::vector<float> pressureScratch_;
  std::vector<glm::vec4> matterPixels_;
  std::vector<glm::vec4> dynamicsPixels_;
  float accumulator_ = 0.0f;
  unsigned long long revision_ = 0;
};

} // namespace gfxlab
