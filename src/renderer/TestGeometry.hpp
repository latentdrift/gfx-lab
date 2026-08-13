#pragma once

#include "renderer/Mesh.hpp"

#include <vector>

namespace gfxlab {

std::vector<Vertex> makePlane(float width, float depth, int xSegments, int zSegments);
std::vector<Vertex> makeQuad();
std::vector<Vertex> makeSphere(int longitudeSegments, int latitudeSegments);
std::vector<Vertex> makeTorus(int majorSegments = 16, int minorSegments = 8);

} // namespace gfxlab
