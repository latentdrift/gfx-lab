#include "evaluation/LegacyBridge.hpp"

#include "renderer/Renderer.hpp"

#include <algorithm>

namespace gfxlab::evaluation {

void publishLegacyResults(const document::Document& document, const RenderStack& legacyStack,
    const Renderer& renderer, SignalRegistry& registry, const std::uint64_t revision) {
  registry.clear();
  for (const document::Operation& operation : document.operations) {
    const auto pass = std::find_if(legacyStack.passes().begin(), legacyStack.passes().end(),
      [&operation](const RenderPass& candidate) {
        return static_cast<std::uint64_t>(std::max(candidate.id, 1)) == operation.id.value;
      });
    if (pass == legacyStack.passes().end()) continue;
    const std::size_t index = static_cast<std::size_t>(std::distance(legacyStack.passes().begin(), pass));
    for (const document::SignalDescriptor& descriptor : operation.outputs) {
      SignalResource resource;
      resource.descriptor = descriptor;
      resource.revision = revision;
      switch (descriptor.kind) {
        case document::SignalKind::Color:
        case document::SignalKind::Normal:
          resource.textures[0] = renderer.stackOperationResult(index);
          resource.textureCount = resource.textures[0] == 0 ? 0 : 1;
          break;
        case document::SignalKind::Depth:
          resource.textures[0] = renderer.stackOperationDepthResult(index);
          resource.textureCount = resource.textures[0] == 0 ? 0 : 1;
          break;
        case document::SignalKind::Field:
          resource.textures[0] = renderer.stackOperationFieldResult(index);
          resource.textureCount = resource.textures[0] == 0 ? 0 : 1;
          break;
        case document::SignalKind::Spectrum16:
          resource.textures = renderer.stackOperationSpectrumResult(index);
          resource.textureCount = resource.textures[0] == 0 ? 0 : 4;
          break;
        case document::SignalKind::Scalar:
        case document::SignalKind::Vector2: break;
      }
      registry.publish(std::move(resource));
    }
  }
}

} // namespace gfxlab::evaluation
