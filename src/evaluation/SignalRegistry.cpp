#include "evaluation/SignalRegistry.hpp"

namespace gfxlab::evaluation {

void SignalRegistry::clear() { resources_.clear(); }

void SignalRegistry::publish(SignalResource resource) {
  resources_.insert_or_assign(resource.descriptor.id, std::move(resource));
}

const SignalResource* SignalRegistry::find(const document::SignalId id) const {
  const auto found = resources_.find(id);
  return found == resources_.end() ? nullptr : &found->second;
}

unsigned int SignalRegistry::displayTexture(const document::SignalId id) const {
  const SignalResource* resource = find(id);
  return resource == nullptr || resource->textureCount == 0 ? 0 : resource->textures[0];
}

} // namespace gfxlab::evaluation
