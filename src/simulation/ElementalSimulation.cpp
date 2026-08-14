#include "simulation/ElementalSimulation.hpp"

#include <algorithm>
#include <cmath>

namespace gfxlab {
namespace {

constexpr float domainWidth = 12.0f;
constexpr float domainHeight = 8.0f;
constexpr float fixedStep = 1.0f / 60.0f;

float mixValue(const float a, const float b, const float t) { return a + (b - a) * t; }

} // namespace

ElementalSimulation::ElementalSimulation()
  : cells_(width * height), scratch_(width * height), pressureScratch_(width * height),
    matterPixels_(width * height), dynamicsPixels_(width * height) {
  reset();
}

int ElementalSimulation::index(const int x, const int y) const {
  return std::clamp(y, 0, height - 1) * width + std::clamp(x, 0, width - 1);
}

glm::vec2 ElementalSimulation::worldPosition(const int x, const int y) const {
  return {(static_cast<float>(x) + 0.5f) / width * domainWidth - domainWidth * 0.5f,
    (static_cast<float>(y) + 0.5f) / height * domainHeight - domainHeight * 0.5f};
}

void ElementalSimulation::reset() {
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      Cell& cell = cells_[index(x, y)];
      cell = {};
      const glm::vec2 world = worldPosition(x, y);
      const bool boundary = x < 2 || y < 2 || x >= width - 2 || y >= height - 2;
      const bool baffle = std::abs(world.x - 0.45f) < 0.16f &&
        (world.y < -0.75f || world.y > 0.55f) && std::abs(world.y) < 3.15f;
      const bool protectedPlinth = world.x > 2.35f && world.x < 3.25f &&
        world.y > 1.0f && world.y < 1.85f;
      cell.solid = boundary || baffle || protectedPlinth;
    }
  }
  accumulator_ = 0.0f;
  ++revision_;
  rebuildPixels();
}

ElementalSimulation::Cell ElementalSimulation::sample(const std::vector<Cell>& cells,
    const glm::vec2 position) const {
  const float x = std::clamp(position.x, 0.0f, static_cast<float>(width - 1));
  const float y = std::clamp(position.y, 0.0f, static_cast<float>(height - 1));
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const int x1 = std::min(x0 + 1, width - 1);
  const int y1 = std::min(y0 + 1, height - 1);
  const float tx = x - x0;
  const float ty = y - y0;
  const Cell& a = cells[index(x0, y0)];
  const Cell& b = cells[index(x1, y0)];
  const Cell& c = cells[index(x0, y1)];
  const Cell& d = cells[index(x1, y1)];
  Cell result;
  result.velocity = glm::mix(glm::mix(a.velocity, b.velocity, tx), glm::mix(c.velocity, d.velocity, tx), ty);
  result.pressure = mixValue(mixValue(a.pressure, b.pressure, tx), mixValue(c.pressure, d.pressure, tx), ty);
  result.temperature = mixValue(mixValue(a.temperature, b.temperature, tx),
    mixValue(c.temperature, d.temperature, tx), ty);
  result.fuel = mixValue(mixValue(a.fuel, b.fuel, tx), mixValue(c.fuel, d.fuel, tx), ty);
  result.smoke = mixValue(mixValue(a.smoke, b.smoke, tx), mixValue(c.smoke, d.smoke, tx), ty);
  result.moisture = mixValue(mixValue(a.moisture, b.moisture, tx), mixValue(c.moisture, d.moisture, tx), ty);
  return result;
}

void ElementalSimulation::update(const float deltaSeconds, const RendererState::Field& controls) {
  accumulator_ += std::clamp(deltaSeconds, 0.0f, 0.1f);
  int steps = 0;
  while (accumulator_ >= fixedStep && steps < 6) {
    step(fixedStep, controls);
    accumulator_ -= fixedStep;
    ++steps;
  }
  if (steps > 0) {
    ++revision_;
    rebuildPixels();
  }
}

void ElementalSimulation::step(const float dt, const RendererState::Field& controls) {
  const float cellsPerWorldX = width / domainWidth;
  const float cellsPerWorldY = height / domainHeight;

  // Semi-Lagrangian advection: persistent quantities follow the velocity field.
  scratch_ = cells_;
  for (int y = 1; y < height - 1; ++y) {
    for (int x = 1; x < width - 1; ++x) {
      const int i = index(x, y);
      if (cells_[i].solid) continue;
      const glm::vec2 velocity = cells_[i].velocity;
      const glm::vec2 previous(static_cast<float>(x) - velocity.x * dt * cellsPerWorldX,
        static_cast<float>(y) - velocity.y * dt * cellsPerWorldY);
      const Cell carried = sample(cells_, previous);
      scratch_[i].velocity = carried.velocity * 0.996f;
      scratch_[i].temperature = carried.temperature * 0.9985f;
      scratch_[i].fuel = carried.fuel * 0.9995f;
      scratch_[i].smoke = carried.smoke * 0.997f;
      scratch_[i].moisture = carried.moisture * 0.998f;
      scratch_[i].combustion = 0.0f;
    }
  }
  cells_.swap(scratch_);

  // The existing field controls become a real producer when producerKind == 2:
  // source A = injector position, wavelength = radius, amplitudes = heat/fuel,
  // phase = wind direction, falloff = wind strength.
  const glm::vec2 source(controls.sourceA.x, controls.sourceA.z);
  const float radius = std::max(controls.wavelength, 0.08f);
  const glm::vec2 wind(std::cos(controls.phaseOffset), std::sin(controls.phaseOffset));
  for (int y = 1; y < height - 1; ++y) {
    for (int x = 1; x < width - 1; ++x) {
      Cell& cell = cells_[index(x, y)];
      if (cell.solid) continue;
      const float distance = glm::distance(worldPosition(x, y), source);
      const float influence = std::clamp(1.0f - distance / radius, 0.0f, 1.0f);
      const float shaped = influence * influence * (3.0f - 2.0f * influence);
      cell.temperature += controls.amplitudeA * shaped * dt * 1.25f;
      cell.fuel += controls.amplitudeB * shaped * dt * 0.72f;
      cell.velocity += wind * controls.falloff * shaped * dt * 2.4f;
    }
  }

  // Reaction and buoyancy. Moisture absorbs heat; sufficiently hot fuel burns
  // into heat, smoke, pressure, and an explicitly exposed combustion channel.
  for (Cell& cell : cells_) {
    if (cell.solid) { cell.velocity = {}; continue; }
    const float cooling = std::min(cell.moisture, cell.temperature * dt * 0.7f);
    cell.temperature = std::max(0.0f, cell.temperature - cooling);
    cell.moisture = std::max(0.0f, cell.moisture - cooling * 0.18f);
    const float ignition = std::max(cell.temperature - 0.32f, 0.0f);
    const float burned = std::min(cell.fuel, ignition * dt * 1.65f);
    cell.fuel -= burned;
    cell.temperature = std::min(cell.temperature + burned * 1.8f, 4.0f);
    cell.smoke = std::min(cell.smoke + burned * 1.15f, 3.0f);
    cell.pressure += burned * 1.3f;
    cell.combustion = burned / dt;
    cell.velocity.y += (cell.temperature * 0.34f - cell.smoke * 0.045f) * dt;
  }

  // Diffuse scalar quantities just enough to keep a stable, legible medium.
  scratch_ = cells_;
  for (int y = 1; y < height - 1; ++y) {
    for (int x = 1; x < width - 1; ++x) {
      const int i = index(x, y);
      if (cells_[i].solid) continue;
      const Cell& l = cells_[index(x - 1, y)];
      const Cell& r = cells_[index(x + 1, y)];
      const Cell& b = cells_[index(x, y - 1)];
      const Cell& t = cells_[index(x, y + 1)];
      constexpr float diffusion = 0.035f;
      scratch_[i].temperature += diffusion * (l.temperature + r.temperature + b.temperature + t.temperature -
        4.0f * cells_[i].temperature);
      scratch_[i].smoke += diffusion * 0.55f * (l.smoke + r.smoke + b.smoke + t.smoke -
        4.0f * cells_[i].smoke);
    }
  }
  cells_.swap(scratch_);

  // Incompressibility projection: solve a small pressure system and remove its
  // gradient from velocity. Obstacles participate in the boundary condition.
  std::fill(pressureScratch_.begin(), pressureScratch_.end(), 0.0f);
  std::vector<float> divergence(width * height, 0.0f);
  for (int y = 1; y < height - 1; ++y) {
    for (int x = 1; x < width - 1; ++x) {
      const int i = index(x, y);
      if (cells_[i].solid) continue;
      divergence[i] = 0.5f * ((cells_[index(x + 1, y)].velocity.x - cells_[index(x - 1, y)].velocity.x) +
        (cells_[index(x, y + 1)].velocity.y - cells_[index(x, y - 1)].velocity.y));
    }
  }
  std::vector<float> nextPressure(width * height, 0.0f);
  for (int iteration = 0; iteration < 14; ++iteration) {
    for (int y = 1; y < height - 1; ++y) {
      for (int x = 1; x < width - 1; ++x) {
        const int i = index(x, y);
        if (cells_[i].solid) continue;
        nextPressure[i] = (pressureScratch_[index(x - 1, y)] + pressureScratch_[index(x + 1, y)] +
          pressureScratch_[index(x, y - 1)] + pressureScratch_[index(x, y + 1)] - divergence[i]) * 0.25f;
      }
    }
    pressureScratch_.swap(nextPressure);
  }
  for (int y = 1; y < height - 1; ++y) {
    for (int x = 1; x < width - 1; ++x) {
      const int i = index(x, y);
      if (cells_[i].solid) continue;
      cells_[i].pressure = pressureScratch_[i] + cells_[i].pressure * 0.88f;
      cells_[i].velocity -= 0.5f * glm::vec2(
        pressureScratch_[index(x + 1, y)] - pressureScratch_[index(x - 1, y)],
        pressureScratch_[index(x, y + 1)] - pressureScratch_[index(x, y - 1)]);
      if (cells_[index(x - 1, y)].solid || cells_[index(x + 1, y)].solid) cells_[i].velocity.x = 0.0f;
      if (cells_[index(x, y - 1)].solid || cells_[index(x, y + 1)].solid) cells_[i].velocity.y = 0.0f;
    }
  }
}

void ElementalSimulation::rebuildPixels() {
  for (std::size_t i = 0; i < cells_.size(); ++i) {
    const Cell& cell = cells_[i];
    matterPixels_[i] = cell.solid ? glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
      : glm::vec4(cell.temperature, cell.smoke, cell.fuel, cell.moisture);
    dynamicsPixels_[i] = cell.solid ? glm::vec4(0.0f, 0.0f, 0.0f, -1.0f)
      : glm::vec4(cell.velocity, cell.pressure, cell.combustion);
  }
}

float ElementalSimulation::totalCombustion() const {
  float total = 0.0f;
  for (const Cell& cell : cells_) total += cell.combustion;
  return total;
}

} // namespace gfxlab
