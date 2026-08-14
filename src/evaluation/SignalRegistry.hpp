#pragma once

#include "document/Signals.hpp"

#include <array>
#include <optional>
#include <unordered_map>

namespace gfxlab::evaluation {

struct SignalResource {
  document::SignalDescriptor descriptor;
  std::array<unsigned int, 4> textures{};
  int textureCount = 0;
  std::optional<float> scalar;
  std::uint64_t revision = 0;
};

class SignalRegistry {
public:
  void clear();
  void publish(SignalResource resource);
  [[nodiscard]] const SignalResource* find(document::SignalId id) const;
  [[nodiscard]] unsigned int displayTexture(document::SignalId id) const;
  [[nodiscard]] const std::unordered_map<document::SignalId, SignalResource>& resources() const {
    return resources_;
  }

private:
  std::unordered_map<document::SignalId, SignalResource> resources_;
};

} // namespace gfxlab::evaluation
