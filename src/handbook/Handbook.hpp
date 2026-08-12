#pragma once

#include <array>

namespace gfxlab { enum class HardwareProfile; }

namespace handbook {

enum class Example {
  None,
  VertexQuantization,
  Projection,
  AffineMapping,
  TextureMinification,
  NormalMapping,
  LightingInterpolation,
  DepthPrecision,
  Transparency,
  Stencil,
  LinearLight,
  ColorQuantization,
  InternalResolution,
  ShadowMapping,
  Overdraw,
  ClutTextures,
  VertexDepthCue,
  Ps1Semitransparency,
  OrderingTable,
  N64ThreePoint,
  N64Combiner,
  N64TextureFormats,
  N64Mipmap,
  N64Coverage,
  N64VideoInterface
};

enum class ActionType { None, ApplyToA, ApplyToB, LoadComparison };

struct Action {
  ActionType type = ActionType::None;
  Example example = Example::None;
};

class Handbook {
public:
  void open();
  bool isOpen() const;
  Action draw(gfxlab::HardwareProfile profile);

private:
  bool open_ = false;
  bool focusRequested_ = false;
  int chapter_ = 0;
  int article_ = 0;
  std::array<char, 96> search_{};
};

} // namespace handbook
