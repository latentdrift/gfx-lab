#include "app/Spectral.hpp"

namespace gfxlab::spectral {

const Samples wavelengthsNm = {400, 420, 440, 460, 480, 500, 520, 540,
  560, 580, 600, 620, 640, 660, 680, 700};
const Samples daylight = {1.040000f, 1.039887f, 1.039138f, 1.037145f, 1.033353f, 1.027288f,
  1.018574f, 1.006950f, 0.992283f, 0.974574f, 0.953955f, 0.930687f, 0.905145f,
  0.877805f, 0.849220f, 0.820000f};
const Samples tungsten = {0.074256f, 0.105985f, 0.144881f, 0.190845f, 0.243469f, 0.302084f,
  0.365819f, 0.433667f, 0.504545f, 0.577351f, 0.651010f, 0.724507f, 0.796918f,
  0.867422f, 0.935312f, 1.000000f};
const Samples humanL = {0.001204f, 0.005564f, 0.021110f, 0.065729f, 0.167973f, 0.352322f,
  0.606531f, 0.856997f, 0.993846f, 0.945959f, 0.738991f, 0.473827f, 0.249352f,
  0.107701f, 0.038180f, 0.011109f};
const Samples humanM = {0.003362f, 0.016038f, 0.059587f, 0.172422f, 0.388558f, 0.681941f,
  0.932102f, 0.992218f, 0.822578f, 0.531096f, 0.267052f, 0.104579f, 0.031895f,
  0.007576f, 0.001401f, 0.000202f};
const Samples humanS = {0.324652f, 0.706648f, 0.986207f, 0.882497f, 0.506336f, 0.186270f,
  0.043937f, 0.006645f, 0.000644f, 0.000040f, 0.000002f, 0, 0, 0, 0, 0};
const Samples reflectanceA = {0.545263f, 0.585386f, 0.584232f, 0.569249f, 0.278715f, 0.196356f,
  0.610745f, 0.832594f, 0.636627f, 0.501370f, 0.367205f, 0.163679f, 0.364608f,
  0.753793f, 0.632001f, 0.287699f};
const Samples reflectanceB = {0.375459f, 0.387827f, 0.440040f, 0.499116f, 0.822001f, 0.921461f,
  0.507073f, 0.268122f, 0.431738f, 0.522901f, 0.606008f, 0.757043f, 0.507881f,
  0.079945f, 0.176668f, 0.512301f};

float integrate(const Samples& reflectance, const Samples& illuminant, const Samples& sensitivity) {
  float value = 0.0f;
  for (std::size_t i = 0; i < bandCount; ++i) value += reflectance[i] * illuminant[i] * sensitivity[i];
  return value;
}

std::array<float, 3> humanResponse(const Samples& reflectance, const Samples& illuminant) {
  return {integrate(reflectance, illuminant, humanL), integrate(reflectance, illuminant, humanM),
    integrate(reflectance, illuminant, humanS)};
}

} // namespace gfxlab::spectral
