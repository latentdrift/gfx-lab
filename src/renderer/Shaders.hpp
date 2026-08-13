#pragma once

namespace gfxlab::shaders {

inline constexpr const char* sceneVertexShader = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
layout(location=3) in vec3 aBarycentric;
layout(location=4) in vec3 aColor;
layout(location=5) in vec4 aTangent;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat4 uLightSpace;
uniform float uQuantization;
uniform bool uClipEnabled;
uniform vec4 uClipPlane;
uniform vec3 uLightDirection;
uniform float uAmbient;
uniform bool uDepthCueEnabled;
uniform float uDepthCueStart;
uniform float uDepthCueEnd;
uniform bool uN64TextureGeneration;
uniform float uNormalInflation;
uniform int uUvMapping;
uniform bool uFieldEnabled;
uniform bool uFieldGeometryAffects;
uniform vec3 uFieldSourceA;
uniform vec3 uFieldSourceB;
uniform float uFieldWavelength;
uniform float uFieldPhaseOffset;
uniform float uFieldAmplitudeA;
uniform float uFieldAmplitudeB;
uniform float uFieldFalloff;
uniform float uFieldBandSharpness;
uniform int uFieldVisualization;
uniform float uFieldVertexDisplacement;
uniform bool uFieldSignedDisplacement;
uniform int uFieldProducerKind;
uniform int uFieldSdfAType;
uniform vec3 uFieldSdfAPosition;
uniform vec3 uFieldSdfAParameters;
uniform int uFieldSdfBType;
uniform vec3 uFieldSdfBPosition;
uniform vec3 uFieldSdfBParameters;
uniform int uFieldSdfOperation;
uniform float uFieldSdfSmoothness;
uniform float uFieldSdfPreviewRange;
uniform float uFieldIsoLevel;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vUvPerspective;
noperspective out vec2 vUvAffine;
out vec3 vBarycentric;
out vec3 vColor;
out float vVertexLighting;
out float vDepthCue;
out vec4 vTangent;
out vec4 vLightPosition;
out float vFieldSignal;
flat out int vFieldConsumerAffects;

float sdfPrimitive(vec3 p, int type, vec3 parameters) {
  if (type == 0) return length(p) - max(parameters.x, 0.001);
  if (type == 1) {
    vec3 q = abs(p) - max(parameters, vec3(0.001));
    return length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
  }
  vec2 q = vec2(length(p.xz) - max(parameters.x, 0.001), p.y);
  return length(q) - max(parameters.y, 0.001);
}

float sdfScene(vec3 p) {
  float a = sdfPrimitive(p - uFieldSdfAPosition, uFieldSdfAType, uFieldSdfAParameters);
  float b = sdfPrimitive(p - uFieldSdfBPosition, uFieldSdfBType, uFieldSdfBParameters);
  if (uFieldSdfOperation == 0) return min(a, b);
  if (uFieldSdfOperation == 1) return max(a, b);
  if (uFieldSdfOperation == 2) return max(a, -b);
  float k = max(uFieldSdfSmoothness, 0.0001);
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

float evaluateField(vec3 worldPosition) {
  if (uFieldProducerKind == 1) {
    float distanceToSurface = sdfScene(worldPosition) - uFieldIsoLevel;
    return clamp(1.0 - abs(distanceToSurface) / max(uFieldSdfPreviewRange, 0.001), 0.0, 1.0);
  }
  float distanceA = length(worldPosition - uFieldSourceA);
  float distanceB = length(worldPosition - uFieldSourceB);
  float wavelength = max(uFieldWavelength, 0.001);
  float waveNumber = 6.28318530718 / wavelength;
  float phaseA = waveNumber * distanceA;
  float phaseB = waveNumber * distanceB + uFieldPhaseOffset;
  float envelopeA = uFieldAmplitudeA * exp(-uFieldFalloff * distanceA);
  float envelopeB = uFieldAmplitudeB * exp(-uFieldFalloff * distanceB);
  float waveA = envelopeA * cos(phaseA);
  float waveB = envelopeB * cos(phaseB);
  float value;
  if (uFieldVisualization == 0) value = 0.5 + 0.5 * cos(phaseA);
  else if (uFieldVisualization == 1) value = 0.5 + 0.5 * cos(phaseB);
  else if (uFieldVisualization == 2) value = 0.5 + 0.5 * cos(phaseA - phaseB);
  else if (uFieldVisualization == 3) {
    float maximumAmplitude = max(uFieldAmplitudeA + uFieldAmplitudeB, 0.001);
    value = (waveA + waveB) * (waveA + waveB) / (maximumAmplitude * maximumAmplitude);
  } else if (uFieldVisualization == 4) {
    value = clamp(abs(distanceA - distanceB) / (4.0 * wavelength), 0.0, 1.0);
  } else {
    float contour = abs(fract(abs(distanceA - distanceB) / wavelength) - 0.5) * 2.0;
    value = 1.0 - contour;
  }
  return pow(clamp(value, 0.0, 1.0), max(uFieldBandSharpness, 0.01));
}

void main() {
  vec3 position = aPosition + aNormal * uNormalInflation;
  if (uQuantization > 0.0)
    position = round(position / uQuantization) * uQuantization;
  vec4 world = uModel * vec4(position, 1.0);
  vNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
  vFieldSignal = uFieldEnabled && uFieldGeometryAffects ? evaluateField(world.xyz) : 0.0;
  vFieldConsumerAffects = uFieldEnabled && uFieldGeometryAffects ? 1 : 0;
  float displacementSignal = uFieldSignedDisplacement ? vFieldSignal * 2.0 - 1.0 : vFieldSignal;
  world.xyz += vNormal * uFieldVertexDisplacement * displacementSignal;
  vWorldPosition = world.xyz;
  vec3 viewNormal = normalize(mat3(uView) * vNormal);
  vec2 mappedUv = uUvMapping == 1 ? position.xy + 0.5 :
    uUvMapping == 2 ? position.xz + 0.5 :
    uUvMapping == 3 ? position.yz + 0.5 : aUv;
  vec2 textureCoordinates = uN64TextureGeneration ? viewNormal.xy * 0.5 + 0.5 : mappedUv;
  vUvPerspective = textureCoordinates;
  vUvAffine = textureCoordinates;
  vBarycentric = aBarycentric;
  vColor = aColor;
  vTangent = vec4(normalize(mat3(uModel) * aTangent.xyz), aTangent.w);
  vLightPosition = uLightSpace * world;
  float vertexDiffuse = max(dot(vNormal, normalize(uLightDirection)), 0.0);
  vVertexLighting = uAmbient + (1.0 - uAmbient) * vertexDiffuse;
  float viewDepth = -(uView * world).z;
  vDepthCue = uDepthCueEnabled
    ? smoothstep(uDepthCueStart, max(uDepthCueEnd, uDepthCueStart + 0.001), viewDepth)
    : 0.0;
  gl_ClipDistance[0] = uClipEnabled ? dot(world, uClipPlane) : 1.0;
  gl_Position = uProjection * uView * world;
}
)GLSL";

inline constexpr const char* sceneFragmentShader = R"GLSL(
#version 410 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vUvPerspective;
noperspective in vec2 vUvAffine;
in vec3 vBarycentric;
in vec3 vColor;
in float vVertexLighting;
in float vDepthCue;
in vec4 vTangent;
in vec4 vLightPosition;
in float vFieldSignal;
flat in int vFieldConsumerAffects;

uniform sampler2D uTexture;
uniform sampler2D uIndexedTexture;
uniform sampler2D uClut;
uniform int uTextureColorMode;
uniform sampler2D uNormalMap;
uniform sampler2D uShadowMap;
uniform bool uAffineMapping;
uniform bool uSmoothShading;
uniform bool uWireframe;
uniform int uVisualization;
uniform int uLightingModel;
uniform float uAmbient;
uniform vec3 uLightDirection;
uniform float uShininess;
uniform int uTransparencyMode;
uniform float uAlphaCutoff;
uniform bool uPremultiplyAlpha;
uniform bool uNormalMapping;
uniform float uNormalStrength;
uniform bool uLinearLight;
uniform vec4 uObjectTint;
uniform bool uTextureSrgb;
uniform bool uHasIndexedTexture;
uniform bool uShadowsEnabled;
uniform float uShadowBias;
uniform bool uShadowPcf;
uniform bool uFogEnabled;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uCameraPosition;
uniform vec3 uFarColor;
uniform bool uN64Enabled;
uniform int uN64CycleType;
uniform ivec4 uN64Cycle0;
uniform ivec4 uN64Cycle1;
uniform vec4 uN64PrimitiveColor;
uniform vec4 uN64EnvironmentColor;
uniform int uN64TextureFormat;
uniform int uN64TextureFilter;
uniform int uN64MipmapMode;
uniform ivec2 uN64TileSize;
uniform bvec2 uN64Mirror;
uniform ivec2 uN64Shift;
uniform int uN64AlphaCompare;
uniform float uN64AlphaThreshold;
uniform sampler2D uN64DetailTexture;
uniform vec2 uUvOffset;
uniform vec2 uUvScale;
uniform float uUvRotation;
uniform vec2 uUvPivot;
uniform bool uFieldDiscardEnabled;
uniform float uFieldDiscardThreshold;
uniform float uFieldSurfaceColorInfluence;
uniform float uFieldEmissionInfluence;
uniform vec3 uFieldLowColor;
uniform vec3 uFieldHighColor;
out vec4 fragColor;

vec4 sampleSurfaceTexture(vec2 uv) {
  if (uTextureColorMode == 0) return texture(uTexture, uv);
  int index = int(round(texture(uIndexedTexture, uv).r * 255.0));
  if (uTextureColorMode == 2) index &= 15;
  return texelFetch(uClut, ivec2(index, 0), 0);
}

vec2 n64Address(vec2 uv) {
  uv *= exp2(vec2(uN64Shift));
  vec2 cell = floor(uv);
  vec2 local = fract(uv);
  if (uN64Mirror.x && (int(cell.x) & 1) != 0) local.x = 1.0 - local.x;
  if (uN64Mirror.y && (int(cell.y) & 1) != 0) local.y = 1.0 - local.y;
  return local;
}

vec4 n64FormatTexel(ivec2 coordinate, int level) {
  ivec2 levelSize = max(uN64TileSize >> level, ivec2(1));
  ivec2 wrapped = ivec2((coordinate.x % levelSize.x + levelSize.x) % levelSize.x,
    (coordinate.y % levelSize.y + levelSize.y) % levelSize.y);
  ivec2 directSize = textureSize(uTexture, level);
  ivec2 directCoordinate = min(wrapped * directSize / levelSize, directSize - 1);
  vec4 direct = texelFetch(uTexture, directCoordinate, level);
  ivec2 indexSize = textureSize(uIndexedTexture, 0);
  ivec2 indexCoordinate = min(wrapped * indexSize / levelSize, indexSize - 1);
  int paletteIndex = int(round(texelFetch(uIndexedTexture, indexCoordinate, 0).r * 255.0));
  if (uN64TextureFormat == 2) paletteIndex &= 15;
  if ((uN64TextureFormat == 2 || uN64TextureFormat == 3) && uHasIndexedTexture)
    return texelFetch(uClut, ivec2(paletteIndex, 0), 0);
  if (uN64TextureFormat == 0)
    return vec4(round(direct.rgb * 31.0) / 31.0, direct.a >= 0.5 ? 1.0 : 0.0);
  if (uN64TextureFormat == 1) return direct;
  float intensity = dot(direct.rgb, vec3(0.299, 0.587, 0.114));
  if (uN64TextureFormat == 4) return vec4(vec3(round(intensity * 7.0) / 7.0), direct.a >= 0.5 ? 1.0 : 0.0);
  if (uN64TextureFormat == 5) return vec4(vec3(round(intensity * 15.0) / 15.0), round(direct.a * 15.0) / 15.0);
  if (uN64TextureFormat == 6) return vec4(vec3(round(intensity * 255.0) / 255.0), round(direct.a * 255.0) / 255.0);
  if (uN64TextureFormat == 7) return vec4(vec3(round(intensity * 15.0) / 15.0), 1.0);
  return vec4(vec3(round(intensity * 255.0) / 255.0), 1.0);
}

vec4 n64FilterLevel(vec2 uv, int level) {
  ivec2 size = max(uN64TileSize >> level, ivec2(1));
  vec2 texelPosition = n64Address(uv) * vec2(size) - 0.5;
  ivec2 base = ivec2(floor(texelPosition));
  vec2 fraction = fract(texelPosition);
  if (uN64TextureFilter == 0)
    return n64FormatTexel(ivec2(floor(texelPosition + 0.5)), level);
  vec4 p00 = n64FormatTexel(base, level);
  vec4 p10 = n64FormatTexel(base + ivec2(1, 0), level);
  vec4 p01 = n64FormatTexel(base + ivec2(0, 1), level);
  vec4 p11 = n64FormatTexel(base + ivec2(1, 1), level);
  if (uN64TextureFilter == 2) return (p00 + p10 + p01 + p11) * 0.25;
  if (fraction.x + fraction.y <= 1.0)
    return p00 + fraction.x * (p10 - p00) + fraction.y * (p01 - p00);
  vec2 inverseFraction = 1.0 - fraction;
  return p11 + inverseFraction.x * (p01 - p11) + inverseFraction.y * (p10 - p11);
}

vec4 sampleN64Texture(vec2 uv, out float lodFraction) {
  vec2 footprintX = dFdx(uv * vec2(uN64TileSize));
  vec2 footprintY = dFdy(uv * vec2(uN64TileSize));
  float lod = max(0.0, log2(max(length(footprintX), length(footprintY))));
  int maximumLevel = int(floor(log2(float(max(1, min(uN64TileSize.x, uN64TileSize.y))))));
  lod = clamp(lod, 0.0, float(maximumLevel));
  lodFraction = fract(lod);
  if (uN64MipmapMode == 0) return n64FilterLevel(uv, 0);
  int lower = int(floor(lod));
  int upper = min(lower + 1, maximumLevel);
  vec4 a = n64FilterLevel(uv, uN64MipmapMode == 1 ? int(round(lod)) : lower);
  if (uN64MipmapMode == 1) return a;
  vec4 b = n64FilterLevel(uv, upper);
  if (uN64MipmapMode == 3) return clamp(a + (a - b) * lodFraction, 0.0, 1.0);
  if (uN64MipmapMode == 4) {
    vec4 detail = texture(uN64DetailTexture, uv * 4.0);
    return mix(a * detail * 2.0, a, clamp(lod, 0.0, 1.0));
  }
  return mix(a, b, lodFraction);
}

vec4 n64CombinerSource(int source, vec4 texel0, vec4 texel1, vec4 shade, vec4 combined, float lodFraction) {
  if (source == 1) return texel0;
  if (source == 2) return vec4(1.0);
  if (source == 3) return shade;
  if (source == 4) return uN64PrimitiveColor;
  if (source == 5) return uN64EnvironmentColor;
  if (source == 6) return texel1;
  if (source == 7) return combined;
  if (source == 8) return vec4(lodFraction);
  return vec4(0.0);
}

vec4 n64CombinerCycle(ivec4 operands, vec4 texel0, vec4 texel1, vec4 shade, vec4 combined, float lodFraction) {
  vec4 a = n64CombinerSource(operands.x, texel0, texel1, shade, combined, lodFraction);
  vec4 b = n64CombinerSource(operands.y, texel0, texel1, shade, combined, lodFraction);
  vec4 c = n64CombinerSource(operands.z, texel0, texel1, shade, combined, lodFraction);
  vec4 d = n64CombinerSource(operands.w, texel0, texel1, shade, combined, lodFraction);
  return clamp((a - b) * c + d, 0.0, 1.0);
}

float n64AlphaNoise(ivec2 pixel) {
  return fract(sin(dot(vec2(pixel), vec2(12.9898, 78.233))) * 43758.5453);
}

float shadowAmount() {
  vec3 projected = vLightPosition.xyz / vLightPosition.w * 0.5 + 0.5;
  if (projected.z <= 0.0 || projected.z >= 1.0 || any(lessThan(projected.xy, vec2(0.0))) || any(greaterThan(projected.xy, vec2(1.0))))
    return 0.0;
  if (!uShadowPcf)
    return projected.z - uShadowBias > texture(uShadowMap, projected.xy).r ? 1.0 : 0.0;
  vec2 texel = 1.0 / vec2(textureSize(uShadowMap, 0));
  float shadow = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      shadow += projected.z - uShadowBias > texture(uShadowMap, projected.xy + vec2(x, y) * texel).r ? 1.0 : 0.0;
  return shadow / 9.0;
}

void main() {
  if (vFieldConsumerAffects != 0 && uFieldDiscardEnabled && vFieldSignal < uFieldDiscardThreshold) discard;
  vec2 uv = (uAffineMapping ? vUvAffine : vUvPerspective);
  float uvCos = cos(uUvRotation);
  float uvSin = sin(uUvRotation);
  uv = mat2(uvCos, uvSin, -uvSin, uvCos) * ((uv - uUvPivot) * uUvScale) + uUvPivot + uUvOffset;
  vec3 normal = uSmoothShading
    ? normalize(vNormal)
    : normalize(cross(dFdx(vWorldPosition), dFdy(vWorldPosition)));
  if (!gl_FrontFacing) normal = -normal;
  vec3 tangent = normalize(vTangent.xyz - normal * dot(normal, vTangent.xyz));
  vec3 bitangent = normalize(cross(normal, tangent)) * vTangent.w;
  if (uNormalMapping) {
    vec3 tangentNormal = texture(uNormalMap, uv).xyz * 2.0 - 1.0;
    tangentNormal.xy *= uNormalStrength;
    tangentNormal = normalize(tangentNormal);
    normal = normalize(mat3(tangent, bitangent, normal) * tangentNormal);
  }
  float n64LodFraction = 0.0;
  vec4 texel = uN64Enabled ? sampleN64Texture(uv, n64LodFraction) : sampleSurfaceTexture(uv);
  vec3 objectTint = uLinearLight ? pow(uObjectTint.rgb, vec3(2.2)) : uObjectTint.rgb;
  float alpha = uVisualization == 0 ? texel.a * uObjectTint.a : 1.0;
  if (uTransparencyMode == 1 && alpha < uAlphaCutoff) discard;
  if (uTransparencyMode == 0) alpha = 1.0;
  vec3 albedo = uLinearLight && uTextureSrgb ? pow(texel.rgb, vec3(2.2)) : texel.rgb;
  albedo *= objectTint;
  if (uVisualization == 1) albedo = vec3(fract(uv), 0.0);
  if (uVisualization == 2) albedo = normal * 0.5 + 0.5;
  if (uVisualization == 3) albedo = vColor;
  if (uVisualization == 4) albedo = tangent * 0.5 + 0.5;
  if (uVisualization == 5) albedo = bitangent * 0.5 + 0.5;

  vec3 lightDirection = normalize(uLightDirection);
  float diffuse = max(dot(normal, lightDirection), 0.0);
  float fragmentLighting = uAmbient + (1.0 - uAmbient) * diffuse;
  vec3 color = albedo;
  if (uLightingModel == 1) color = albedo * vVertexLighting;
  if (uLightingModel >= 2) color = albedo * fragmentLighting;
  if (uLightingModel >= 3) {
    vec3 viewDirection = normalize(uCameraPosition - vWorldPosition);
    float specular = 0.0;
    if (uLightingModel == 3)
      specular = pow(max(dot(viewDirection, reflect(-lightDirection, normal)), 0.0), uShininess);
    if (uLightingModel == 4)
      specular = pow(max(dot(normal, normalize(lightDirection + viewDirection)), 0.0), uShininess);
    color += vec3(0.35) * specular;
  }
  if (uShadowsEnabled && uLightingModel != 0)
    color *= 1.0 - shadowAmount() * (1.0 - uAmbient);

  if (uN64Enabled) {
    texel *= uObjectTint;
    vec4 shade = vec4(vColor * (uLightingModel == 0 ? 1.0 : vVertexLighting), 1.0);
    vec4 texel1 = texture(uN64DetailTexture, uv * 4.0);
    vec4 combined = n64CombinerCycle(uN64Cycle0, texel, texel1, shade, vec4(0.0), n64LodFraction);
    if (uN64CycleType == 2)
      combined = n64CombinerCycle(uN64Cycle1, texel, texel1, shade, combined, n64LodFraction);
    color = combined.rgb;
    alpha = combined.a;
    if (uN64AlphaCompare == 1 && alpha < uN64AlphaThreshold) discard;
    if (uN64AlphaCompare == 2 && alpha < n64AlphaNoise(ivec2(gl_FragCoord.xy))) discard;
  }

  vec3 fieldMiddle = mix(vec3(0.10, 0.42, 0.88), uFieldHighColor, 0.28);
  vec3 fieldColor = vFieldSignal < 0.5
    ? mix(uFieldLowColor, fieldMiddle, vFieldSignal * 2.0)
    : mix(fieldMiddle, uFieldHighColor, vFieldSignal * 2.0 - 1.0);
  if (vFieldConsumerAffects != 0) {
    color = mix(color, fieldColor, uFieldSurfaceColorInfluence);
    color += fieldColor * vFieldSignal * uFieldEmissionInfluence;
  }

  if (uWireframe) {
    vec3 width = fwidth(vBarycentric);
    vec3 edge = smoothstep(vec3(0.0), width * 1.15, vBarycentric);
    float interior = min(edge.x, min(edge.y, edge.z));
    color = mix(vec3(0.035), color, interior);
  }
  if (uFogEnabled) {
    float distanceToCamera = length(uCameraPosition - vWorldPosition);
    float fogAmount = smoothstep(uFogStart, max(uFogEnd, uFogStart + 0.001), distanceToCamera);
    vec3 fogColor = vec3(0.105, 0.112, 0.12);
    if (uLinearLight) fogColor = pow(fogColor, vec3(2.2));
    color = mix(color, fogColor, fogAmount);
  }
  color = mix(color, uFarColor, vDepthCue);
  fragColor = vec4(uPremultiplyAlpha ? color * alpha : color, alpha);
}
)GLSL";

inline constexpr const char* outputVertexShader = R"GLSL(
#version 410 core
out vec2 vUv;
void main() {
  const vec2 positions[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
  vec2 position = positions[gl_VertexID];
  vUv = position * 0.5 + 0.5;
  gl_Position = vec4(position, 0, 1);
}
)GLSL";

inline constexpr const char* outputFragmentShader = R"GLSL(
#version 410 core
in vec2 vUv;
uniform sampler2D uScene;
uniform sampler2D uDepth;
uniform int uBitsPerChannel;
uniform bool uDithering;
uniform bool uLinearLight;
uniform int uDepthVisualization;
uniform float uNearPlane;
uniform float uFarPlane;
uniform bool uOrthographic;
uniform sampler2D uShadowMap;
uniform bool uVisualizeShadowMap;
uniform sampler2D uOverdraw;
uniform sampler2D uField;
uniform bool uFieldOutput;
uniform vec3 uFieldLowColor;
uniform vec3 uFieldHighColor;
uniform bool uFieldSignedDistance;
uniform float uFieldSdfPreviewRange;
uniform bool uVisualizeOverdraw;
uniform float uOverdrawRange;
uniform bool uN64Enabled;
uniform int uN64ColorDither;
uniform bool uN64ViReconstruction;
uniform bool uN64ViDivot;
out vec4 fragColor;

float bayer4(ivec2 p) {
  const float m[16] = float[16](
     0,  8,  2, 10,
    12,  4, 14,  6,
     3, 11,  1,  9,
    15,  7, 13,  5
  );
  return (m[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 16.0 - 0.5;
}

float magic4(ivec2 p) {
  const float m[16] = float[16](
     0,  6,  1,  7,
     4,  2,  5,  3,
     1,  7,  0,  6,
     5,  3,  4,  2
  );
  return (m[(p.y & 3) * 4 + (p.x & 3)] + 0.5) / 8.0 - 0.5;
}

float noiseDither(ivec2 p) {
  return fract(sin(dot(vec2(p), vec2(12.9898, 78.233))) * 43758.5453) - 0.5;
}

vec3 median3(vec3 a, vec3 b, vec3 c) {
  return max(min(a, b), min(max(a, b), c));
}

void main() {
  vec3 color = texture(uScene, vUv).rgb;
  if (uFieldOutput) {
    float signal = texture(uField, vUv).r;
    if (texture(uDepth, vUv).r >= 0.999999) color = vec3(0.0);
    else {
      if (uFieldSignedDistance)
        signal = 0.5 + 0.5 * clamp(signal / max(uFieldSdfPreviewRange, 0.001), -1.0, 1.0);
      vec3 middle = mix(vec3(0.10, 0.42, 0.88), uFieldHighColor, 0.28);
      color = signal < 0.5 ? mix(uFieldLowColor, middle, signal * 2.0)
        : mix(middle, uFieldHighColor, signal * 2.0 - 1.0);
    }
  }
  if (!uFieldOutput && uN64Enabled && uN64ViReconstruction) {
    vec2 texel = 1.0 / vec2(textureSize(uScene, 0));
    vec3 horizontal = texture(uScene, vUv - vec2(texel.x, 0.0)).rgb + texture(uScene, vUv + vec2(texel.x, 0.0)).rgb;
    vec3 vertical = texture(uScene, vUv - vec2(0.0, texel.y)).rgb + texture(uScene, vUv + vec2(0.0, texel.y)).rgb;
    color = color * 0.5 + (horizontal + vertical) * 0.125;
    if (uN64ViDivot) {
      vec3 left = texture(uScene, vUv - vec2(texel.x, 0.0)).rgb;
      vec3 right = texture(uScene, vUv + vec2(texel.x, 0.0)).rgb;
      color = median3(left, color, right);
    }
  }
  if (uFieldOutput) {
    // A field output is scalar data. Preserve its literal 0..1 value instead of
    // applying display gamma or the pass's material color quantization.
  } else if (uVisualizeOverdraw) {
    float count = texture(uOverdraw, vUv).r;
    float t = clamp(count / max(uOverdrawRange, 1.0), 0.0, 1.0);
    color = clamp(vec3(1.5 * t, 1.5 - abs(4.0 * t - 2.0), 1.5 * (1.0 - t)), 0.0, 1.0);
    if (count < 0.5) color = vec3(0.02);
  } else if (uVisualizeShadowMap) {
    color = vec3(texture(uShadowMap, vUv).r);
  } else if (uDepthVisualization != 0) {
    float rawDepth = texture(uDepth, vUv).r;
    if (uDepthVisualization == 1) {
      color = vec3(rawDepth);
    } else {
      float linearDepth;
      if (uOrthographic) {
        linearDepth = mix(uNearPlane, uFarPlane, rawDepth);
      } else {
        float ndcDepth = rawDepth * 2.0 - 1.0;
        linearDepth = (2.0 * uNearPlane * uFarPlane) /
          (uFarPlane + uNearPlane - ndcDepth * (uFarPlane - uNearPlane));
      }
      color = vec3(clamp(linearDepth / 10.0, 0.0, 1.0));
    }
  } else if (uLinearLight) {
    color = pow(max(color, vec3(0.0)), vec3(1.0 / 2.2));
  }
  float levels = exp2(float(uBitsPerChannel)) - 1.0;
  if (uFieldOutput) {
    fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
    return;
  } else if (uN64Enabled && uN64ColorDither != 0 && uBitsPerChannel < 8) {
    float offset = uN64ColorDither == 1 ? magic4(ivec2(gl_FragCoord.xy))
      : uN64ColorDither == 2 ? bayer4(ivec2(gl_FragCoord.xy)) : noiseDither(ivec2(gl_FragCoord.xy));
    color += offset / levels;
  } else if (uDithering) {
    color += bayer4(ivec2(gl_FragCoord.xy)) / levels;
  }
  color = round(clamp(color, 0.0, 1.0) * levels) / levels;
  fragColor = vec4(color, 1.0);
}
)GLSL";

inline constexpr const char* relationFragmentShader = R"GLSL(
#version 410 core
in vec2 vUv;
uniform sampler2D uImageA;
uniform sampler2D uImageB;
uniform int uSourceAMode;
uniform int uSourceBMode;
uniform vec4 uFixedColor;
uniform int uBitDepth;
uniform float uHistoryDecay;
uniform vec2 uHistoryUvOffset;
uniform vec2 uHistoryUvScale;
uniform int uOperator;
uniform float uGain;
uniform float uBias;
uniform float uOpacity;
uniform int uColorSpace;
uniform int uRangeMode;
uniform sampler2D uMaskDepth;
uniform sampler2D uMaskField;
uniform int uMaskMode;
uniform bool uInvertMask;
uniform float uMaskNearPlane;
uniform bool uMaskOrthographic;
uniform bool uMaskFieldSignedDistance;
uniform float uMaskSdfPreviewRange;
out vec4 fragColor;

vec3 signedPow(vec3 value, float exponent) {
  return sign(value) * pow(abs(value), vec3(exponent));
}

float finiteChannel(float value) {
  if (isnan(value)) return 0.0;
  if (isinf(value)) return sign(value) * 65504.0;
  return clamp(value, -65504.0, 65504.0);
}

vec3 finiteColor(vec3 value) {
  return vec3(finiteChannel(value.r), finiteChannel(value.g), finiteChannel(value.b));
}

void main() {
  vec2 historyUv = (vUv - 0.5) * uHistoryUvScale + 0.5 + uHistoryUvOffset;
  vec3 storedA = uSourceAMode == 3 ? uFixedColor.rgb :
    texture(uImageA, uSourceAMode == 4 ? historyUv : vUv).rgb;
  vec3 storedB = uSourceBMode == 3 ? uFixedColor.rgb :
    texture(uImageB, uSourceBMode == 4 ? historyUv : vUv).rgb;
  if (uSourceAMode == 5) storedA = storedA.rrr;
  if (uSourceBMode == 5) storedB = storedB.rrr;
  storedA = finiteColor(storedA);
  storedB = finiteColor(storedB);
  if (uSourceAMode == 4) storedA *= uHistoryDecay;
  if (uSourceBMode == 4) storedB *= uHistoryDecay;
  vec3 a = uColorSpace == 1 ? signedPow(storedA, 2.2) : storedA;
  vec3 b = uColorSpace == 1 ? signedPow(storedB, 2.2) : storedB;
  vec3 relation;
  if (uOperator == 0) relation = abs(a - b);
  else if (uOperator == 1) relation = a - b;
  else if (uOperator == 2) relation = max(a - b, vec3(0.0));
  else if (uOperator == 3) relation = max(b - a, vec3(0.0));
  else if (uOperator == 4) relation = a * b;
  else if (uOperator == 5) relation = 1.0 - (1.0 - a) * (1.0 - b);
  else if (uOperator == 6) relation = a + b - 2.0 * a * b;
  else if (uOperator == 7) relation = min(a, b);
  else if (uOperator == 8) relation = max(a, b);
  else if (uOperator == 9) relation = a * (1.0 - b);
  else if (uOperator == 10) relation = a + b - 1.0;
  else if (uOperator == 11) relation = a / max(b, vec3(1.0 / 255.0)) - 1.0;
  else if (uOperator == 12) relation = a + b;
  else if (uOperator == 13) relation = (a + b) * 0.5;
  else if (uOperator == 14) relation = a - b;
  else if (uOperator == 15) relation = b - a;
  else if (uOperator == 16) relation = a + b * 0.25;
  else if (uOperator == 17) relation = a + b - 0.5;
  else {
    float levels = exp2(float(uBitDepth)) - 1.0;
    uvec3 qa = uvec3(round(clamp(a, 0.0, 1.0) * levels));
    uvec3 qb = uvec3(round(clamp(b, 0.0, 1.0) * levels));
    relation = vec3(qa ^ qb) / levels;
  }
  relation = relation * uGain + uBias;
  relation = finiteColor(relation);
  if (uRangeMode == 0) relation = clamp(relation, 0.0, 1.0);
  else if (uRangeMode == 2) relation = fract(relation);
  float mask = 1.0;
  if (uMaskMode == 1) {
    mask = dot(clamp(storedB, 0.0, 1.0), vec3(0.2126, 0.7152, 0.0722));
  } else if (uMaskMode == 2) {
    float rawDepth = texture(uMaskDepth, vUv).r;
    float linearDepth;
    if (uMaskOrthographic) linearDepth = mix(uMaskNearPlane, 100.0, rawDepth);
    else {
      float ndcDepth = rawDepth * 2.0 - 1.0;
      linearDepth = (2.0 * uMaskNearPlane * 100.0) /
        (100.0 + uMaskNearPlane - ndcDepth * (100.0 - uMaskNearPlane));
    }
    mask = clamp(linearDepth / 10.0, 0.0, 1.0);
  } else if (uMaskMode == 3) {
    vec3 change = abs(dFdx(storedB)) + abs(dFdy(storedB));
    mask = smoothstep(0.025, 0.20, max(change.r, max(change.g, change.b)));
  } else if (uMaskMode == 4) {
    float fieldValue = texture(uMaskField, vUv).r;
    mask = uMaskFieldSignedDistance
      ? 1.0 - smoothstep(0.0, max(uMaskSdfPreviewRange, 0.001), abs(fieldValue))
      : clamp(fieldValue, 0.0, 1.0);
    if (texture(uMaskDepth, vUv).r >= 0.999999) mask = 0.0;
  }
  if (uInvertMask) mask = 1.0 - mask;
  vec3 composed = finiteColor(mix(a, relation, uOpacity * mask));
  if (uColorSpace == 1) composed = signedPow(composed, 1.0 / 2.2);
  fragColor = vec4(composed, 1.0);
}
)GLSL";

// Produces a typed scalar resource from scene depth. Unlike the surface shader,
// this pass does not replace material color: it reconstructs the visible
// world-space position and writes the selected field signal to an R16F buffer.
inline constexpr const char* fieldFragmentShader = R"GLSL(
#version 410 core
in vec2 vUv;
uniform sampler2D uDepth;
uniform mat4 uInverseViewProjection;
uniform bool uEnabled;
uniform vec3 uSourceA;
uniform vec3 uSourceB;
uniform float uWavelength;
uniform float uPhaseOffset;
uniform float uAmplitudeA;
uniform float uAmplitudeB;
uniform float uFalloff;
uniform float uBandSharpness;
uniform int uVisualization;
uniform int uProducerKind;
uniform int uSdfAType;
uniform vec3 uSdfAPosition;
uniform vec3 uSdfAParameters;
uniform int uSdfBType;
uniform vec3 uSdfBPosition;
uniform vec3 uSdfBParameters;
uniform int uSdfOperation;
uniform float uSdfSmoothness;
out float fieldSignal;

float sdfPrimitive(vec3 p, int type, vec3 parameters) {
  if (type == 0) return length(p) - parameters.x;
  if (type == 1) {
    vec3 q = abs(p) - parameters;
    return length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
  }
  vec2 q = vec2(length(p.xz) - parameters.x, p.y);
  return length(q) - parameters.y;
}

float sdfScene(vec3 p) {
  float a = sdfPrimitive(p - uSdfAPosition, uSdfAType, uSdfAParameters);
  float b = sdfPrimitive(p - uSdfBPosition, uSdfBType, uSdfBParameters);
  if (uSdfOperation == 0) return min(a, b);
  if (uSdfOperation == 1) return max(a, b);
  if (uSdfOperation == 2) return max(a, -b);
  float k = max(uSdfSmoothness, 0.0001);
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

void main() {
  float depth = texture(uDepth, vUv).r;
  if (!uEnabled || depth >= 0.999999) { fieldSignal = 0.0; return; }
  vec4 world = uInverseViewProjection * vec4(vUv * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
  vec3 position = world.xyz / max(abs(world.w), 0.000001);
  if (uProducerKind == 1) { fieldSignal = sdfScene(position); return; }
  float distanceA = length(position - uSourceA);
  float distanceB = length(position - uSourceB);
  float wavelength = max(uWavelength, 0.001);
  float waveNumber = 6.28318530718 / wavelength;
  float phaseA = waveNumber * distanceA;
  float phaseB = waveNumber * distanceB + uPhaseOffset;
  float envelopeA = uAmplitudeA * exp(-uFalloff * distanceA);
  float envelopeB = uAmplitudeB * exp(-uFalloff * distanceB);
  float waveA = envelopeA * cos(phaseA);
  float waveB = envelopeB * cos(phaseB);
  float value;
  if (uVisualization == 0) value = 0.5 + 0.5 * cos(phaseA);
  else if (uVisualization == 1) value = 0.5 + 0.5 * cos(phaseB);
  else if (uVisualization == 2) value = 0.5 + 0.5 * cos(phaseA - phaseB);
  else if (uVisualization == 3) {
    float maximumAmplitude = max(uAmplitudeA + uAmplitudeB, 0.001);
    value = (waveA + waveB) * (waveA + waveB) / (maximumAmplitude * maximumAmplitude);
  } else if (uVisualization == 4) {
    value = clamp(abs(distanceA - distanceB) / (4.0 * wavelength), 0.0, 1.0);
  } else {
    float contour = abs(fract(abs(distanceA - distanceB) / wavelength) - 0.5) * 2.0;
    value = 1.0 - contour;
  }
  fieldSignal = pow(clamp(value, 0.0, 1.0), max(uBandSharpness, 0.01));
}
)GLSL";

inline constexpr const char* sdfIsoSurfaceFragmentShader = R"GLSL(
#version 410 core
in vec2 vUv;
uniform mat4 uInverseViewProjection;
uniform mat4 uViewProjection;
uniform vec3 uCameraPosition;
uniform bool uOrthographic;
uniform int uSdfAType;
uniform vec3 uSdfAPosition;
uniform vec3 uSdfAParameters;
uniform int uSdfBType;
uniform vec3 uSdfBPosition;
uniform vec3 uSdfBParameters;
uniform int uSdfOperation;
uniform float uSdfSmoothness;
uniform float uIsoLevel;
uniform int uMaximumSteps;
uniform float uHitEpsilon;
uniform float uMaximumDistance;
uniform vec3 uSurfaceColor;
uniform vec3 uLightDirection;
uniform float uAmbient;
out vec4 fragColor;

float sdfPrimitive(vec3 p, int type, vec3 parameters) {
  if (type == 0) return length(p) - parameters.x;
  if (type == 1) {
    vec3 q = abs(p) - parameters;
    return length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
  }
  vec2 q = vec2(length(p.xz) - parameters.x, p.y);
  return length(q) - parameters.y;
}

float sceneDistance(vec3 p) {
  float a = sdfPrimitive(p - uSdfAPosition, uSdfAType, uSdfAParameters);
  float b = sdfPrimitive(p - uSdfBPosition, uSdfBType, uSdfBParameters);
  float distance;
  if (uSdfOperation == 0) distance = min(a, b);
  else if (uSdfOperation == 1) distance = max(a, b);
  else if (uSdfOperation == 2) distance = max(a, -b);
  else {
    float k = max(uSdfSmoothness, 0.0001);
    float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
    distance = mix(b, a, h) - k * h * (1.0 - h);
  }
  return distance - uIsoLevel;
}

vec3 normalAt(vec3 p) {
  float e = max(uHitEpsilon * 2.0, 0.0002);
  vec2 h = vec2(e, 0.0);
  return normalize(vec3(sceneDistance(p + h.xyy) - sceneDistance(p - h.xyy),
    sceneDistance(p + h.yxy) - sceneDistance(p - h.yxy),
    sceneDistance(p + h.yyx) - sceneDistance(p - h.yyx)));
}

void main() {
  vec2 ndc = vUv * 2.0 - 1.0;
  vec4 nearH = uInverseViewProjection * vec4(ndc, -1.0, 1.0);
  vec4 farH = uInverseViewProjection * vec4(ndc, 1.0, 1.0);
  vec3 nearPoint = nearH.xyz / nearH.w;
  vec3 farPoint = farH.xyz / farH.w;
  vec3 origin = uOrthographic ? nearPoint : uCameraPosition;
  vec3 direction = normalize(farPoint - nearPoint);
  float travel = 0.0;
  vec3 position = origin;
  bool hit = false;
  for (int step = 0; step < 512; ++step) {
    if (step >= uMaximumSteps || travel > uMaximumDistance) break;
    position = origin + direction * travel;
    float distance = sceneDistance(position);
    if (abs(distance) < uHitEpsilon) { hit = true; break; }
    travel += max(abs(distance), uHitEpsilon * 0.5);
  }
  if (!hit) discard;
  vec4 clip = uViewProjection * vec4(position, 1.0);
  float depth = clip.z / clip.w * 0.5 + 0.5;
  if (depth < 0.0 || depth > 1.0) discard;
  gl_FragDepth = depth;
  vec3 normal = normalAt(position);
  float diffuse = max(dot(normal, normalize(uLightDirection)), 0.0);
  float lighting = uAmbient + (1.0 - uAmbient) * diffuse;
  float rim = pow(1.0 - max(dot(normal, -direction), 0.0), 3.0);
  fragColor = vec4(uSurfaceColor * lighting + uSurfaceColor * rim * 0.35, 1.0);
}
)GLSL";

inline constexpr const char* copyFragmentShader = R"GLSL(
#version 410 core
in vec2 vUv;
uniform sampler2D uImage;
out vec4 fragColor;
void main() { fragColor = texture(uImage, vUv); }
)GLSL";

inline constexpr const char* displayReconstructionFragmentShader = R"GLSL(
#version 410 core
in vec2 vUv;
uniform sampler2D uImage;
uniform int uSignal;
uniform float uChromaBleed;
uniform float uCrosstalk;
uniform float uScanlineStrength;
uniform float uMaskStrength;
uniform float uBloomStrength;
uniform float uBloomRadius;
out vec4 fragColor;

vec3 rgbToYiq(vec3 rgb) {
  return vec3(dot(rgb, vec3(0.299, 0.587, 0.114)),
    dot(rgb, vec3(0.596, -0.274, -0.322)),
    dot(rgb, vec3(0.211, -0.523, 0.312)));
}

vec3 yiqToRgb(vec3 yiq) {
  return vec3(yiq.x + 0.956 * yiq.y + 0.621 * yiq.z,
    yiq.x - 0.272 * yiq.y - 0.647 * yiq.z,
    yiq.x - 1.106 * yiq.y + 1.703 * yiq.z);
}

void main() {
  vec2 texel = 1.0 / vec2(textureSize(uImage, 0));
  vec3 center = texture(uImage, vUv).rgb;
  vec3 color = center;
  if (uSignal == 1) {
    vec3 centerYiq = rgbToYiq(center);
    vec3 neighborhood = rgbToYiq(texture(uImage, vUv - vec2(2.0 * texel.x, 0.0)).rgb) * 0.12;
    neighborhood += rgbToYiq(texture(uImage, vUv - vec2(texel.x, 0.0)).rgb) * 0.23;
    neighborhood += centerYiq * 0.30;
    neighborhood += rgbToYiq(texture(uImage, vUv + vec2(texel.x, 0.0)).rgb) * 0.23;
    neighborhood += rgbToYiq(texture(uImage, vUv + vec2(2.0 * texel.x, 0.0)).rgb) * 0.12;
    vec3 decoded = vec3(centerYiq.x, mix(centerYiq.yz, neighborhood.yz, uChromaBleed));
    float carrier = sin(gl_FragCoord.x * 1.57079632679);
    decoded.x += carrier * neighborhood.y * uCrosstalk * 0.22;
    decoded.yz += vec2(carrier, -carrier) * (centerYiq.x - neighborhood.x) * uCrosstalk * 0.08;
    color = yiqToRgb(decoded);
  }

  vec2 bloomStep = texel * uBloomRadius;
  vec3 bloom = texture(uImage, vUv + vec2(bloomStep.x, 0.0)).rgb;
  bloom += texture(uImage, vUv - vec2(bloomStep.x, 0.0)).rgb;
  bloom += texture(uImage, vUv + vec2(0.0, bloomStep.y)).rgb;
  bloom += texture(uImage, vUv - vec2(0.0, bloomStep.y)).rgb;
  bloom *= 0.25;
  color += max(bloom - vec3(0.35), vec3(0.0)) * uBloomStrength;

  float darkRow = mod(floor(gl_FragCoord.y), 2.0);
  color *= 1.0 - darkRow * uScanlineStrength;
  int triad = int(mod(floor(gl_FragCoord.x), 3.0));
  vec3 grille = triad == 0 ? vec3(1.15, 0.86, 0.86) :
    triad == 1 ? vec3(0.86, 1.15, 0.86) : vec3(0.86, 0.86, 1.15);
  color *= mix(vec3(1.0), grille, uMaskStrength);
  fragColor = vec4(clamp(color, 0.0, 1.0), 1.0);
}
)GLSL";

inline constexpr const char* shadowVertexShader = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
uniform mat4 uModel;
uniform mat4 uLightSpace;
uniform float uQuantization;
uniform bool uFieldEnabled;
uniform vec3 uFieldSourceA;
uniform vec3 uFieldSourceB;
uniform float uFieldWavelength;
uniform float uFieldPhaseOffset;
uniform float uFieldAmplitudeA;
uniform float uFieldAmplitudeB;
uniform float uFieldFalloff;
uniform float uFieldBandSharpness;
uniform int uFieldVisualization;
uniform float uFieldVertexDisplacement;
uniform bool uFieldSignedDisplacement;
uniform int uFieldProducerKind;
uniform int uFieldSdfAType;
uniform vec3 uFieldSdfAPosition;
uniform vec3 uFieldSdfAParameters;
uniform int uFieldSdfBType;
uniform vec3 uFieldSdfBPosition;
uniform vec3 uFieldSdfBParameters;
uniform int uFieldSdfOperation;
uniform float uFieldSdfSmoothness;
uniform float uFieldSdfPreviewRange;
uniform float uFieldIsoLevel;
out float vShadowFieldSignal;

float sdfPrimitive(vec3 p, int type, vec3 parameters) {
  if (type == 0) return length(p) - max(parameters.x, 0.001);
  if (type == 1) {
    vec3 q = abs(p) - max(parameters, vec3(0.001));
    return length(max(q, vec3(0.0))) + min(max(q.x, max(q.y, q.z)), 0.0);
  }
  vec2 q = vec2(length(p.xz) - max(parameters.x, 0.001), p.y);
  return length(q) - max(parameters.y, 0.001);
}

float sdfScene(vec3 p) {
  float a = sdfPrimitive(p - uFieldSdfAPosition, uFieldSdfAType, uFieldSdfAParameters);
  float b = sdfPrimitive(p - uFieldSdfBPosition, uFieldSdfBType, uFieldSdfBParameters);
  if (uFieldSdfOperation == 0) return min(a, b);
  if (uFieldSdfOperation == 1) return max(a, b);
  if (uFieldSdfOperation == 2) return max(a, -b);
  float k = max(uFieldSdfSmoothness, 0.0001);
  float h = clamp(0.5 + 0.5 * (b - a) / k, 0.0, 1.0);
  return mix(b, a, h) - k * h * (1.0 - h);
}

float evaluateField(vec3 worldPosition) {
  if (uFieldProducerKind == 1) {
    float distanceToSurface = sdfScene(worldPosition) - uFieldIsoLevel;
    return clamp(1.0 - abs(distanceToSurface) / max(uFieldSdfPreviewRange, 0.001), 0.0, 1.0);
  }
  float distanceA = length(worldPosition - uFieldSourceA);
  float distanceB = length(worldPosition - uFieldSourceB);
  float wavelength = max(uFieldWavelength, 0.001);
  float waveNumber = 6.28318530718 / wavelength;
  float phaseA = waveNumber * distanceA;
  float phaseB = waveNumber * distanceB + uFieldPhaseOffset;
  float envelopeA = uFieldAmplitudeA * exp(-uFieldFalloff * distanceA);
  float envelopeB = uFieldAmplitudeB * exp(-uFieldFalloff * distanceB);
  float waveA = envelopeA * cos(phaseA);
  float waveB = envelopeB * cos(phaseB);
  float value;
  if (uFieldVisualization == 0) value = 0.5 + 0.5 * cos(phaseA);
  else if (uFieldVisualization == 1) value = 0.5 + 0.5 * cos(phaseB);
  else if (uFieldVisualization == 2) value = 0.5 + 0.5 * cos(phaseA - phaseB);
  else if (uFieldVisualization == 3) {
    float maximumAmplitude = max(uFieldAmplitudeA + uFieldAmplitudeB, 0.001);
    value = (waveA + waveB) * (waveA + waveB) / (maximumAmplitude * maximumAmplitude);
  } else if (uFieldVisualization == 4)
    value = clamp(abs(distanceA - distanceB) / (4.0 * wavelength), 0.0, 1.0);
  else {
    float contour = abs(fract(abs(distanceA - distanceB) / wavelength) - 0.5) * 2.0;
    value = 1.0 - contour;
  }
  return pow(clamp(value, 0.0, 1.0), max(uFieldBandSharpness, 0.01));
}

void main() {
  vec3 position = aPosition;
  if (uQuantization > 0.0) position = round(position / uQuantization) * uQuantization;
  vec4 world = uModel * vec4(position, 1.0);
  vec3 worldNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
  vShadowFieldSignal = uFieldEnabled ? evaluateField(world.xyz) : 0.0;
  float displacementSignal = uFieldSignedDisplacement ? vShadowFieldSignal * 2.0 - 1.0 : vShadowFieldSignal;
  world.xyz += worldNormal * uFieldVertexDisplacement * displacementSignal;
  gl_Position = uLightSpace * world;
}
)GLSL";

inline constexpr const char* shadowFragmentShader = R"GLSL(
#version 410 core
in float vShadowFieldSignal;
uniform bool uFieldEnabled;
uniform bool uFieldDiscardEnabled;
uniform float uFieldDiscardThreshold;
void main() {
  if (uFieldEnabled && uFieldDiscardEnabled && vShadowFieldSignal < uFieldDiscardThreshold) discard;
}
)GLSL";

inline constexpr const char* overdrawVertexShader = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uQuantization;
void main() {
  vec3 position = aPosition;
  if (uQuantization > 0.0) position = round(position / uQuantization) * uQuantization;
  gl_Position = uProjection * uView * uModel * vec4(position, 1.0);
}
)GLSL";

inline constexpr const char* overdrawFragmentShader = R"GLSL(
#version 410 core
layout(location=0) out float fragmentCount;
void main() { fragmentCount = 1.0; }
)GLSL";

} // namespace gfxlab::shaders
