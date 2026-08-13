#pragma once

#include <array>
#include <cstddef>

namespace gfxlab::spectral {

inline constexpr std::size_t bandCount = 16;
using Samples = std::array<float, bandCount>;

extern const Samples wavelengthsNm;
extern const Samples daylight;
extern const Samples tungsten;
extern const Samples reflectanceA;
extern const Samples reflectanceB;
extern const Samples humanL;
extern const Samples humanM;
extern const Samples humanS;

[[nodiscard]] float integrate(const Samples& reflectance, const Samples& illuminant,
  const Samples& sensitivity);
[[nodiscard]] std::array<float, 3> humanResponse(const Samples& reflectance,
  const Samples& illuminant);

} // namespace gfxlab::spectral
