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

void main() {
  vec3 position = aPosition;
  if (uQuantization > 0.0)
    position = round(position / uQuantization) * uQuantization;
  vec4 world = uModel * vec4(position, 1.0);
  vWorldPosition = world.xyz;
  vNormal = normalize(mat3(transpose(inverse(uModel))) * aNormal);
  vUvPerspective = aUv;
  vUvAffine = aUv;
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
uniform vec3 uObjectTint;
uniform bool uShadowsEnabled;
uniform float uShadowBias;
uniform bool uShadowPcf;
uniform bool uFogEnabled;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uCameraPosition;
uniform vec3 uFarColor;
out vec4 fragColor;

vec4 sampleSurfaceTexture(vec2 uv) {
  if (uTextureColorMode == 0) return texture(uTexture, uv);
  int index = int(round(texture(uIndexedTexture, uv).r * 255.0));
  if (uTextureColorMode == 2) index &= 15;
  return texelFetch(uClut, ivec2(index, 0), 0);
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
  vec2 uv = uAffineMapping ? vUvAffine : vUvPerspective;
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
  vec4 texel = sampleSurfaceTexture(uv);
  float alpha = uVisualization == 0 ? texel.a : 1.0;
  if (uTransparencyMode == 1 && alpha < uAlphaCutoff) discard;
  if (uTransparencyMode == 0) alpha = 1.0;
  vec3 albedo = uLinearLight ? pow(texel.rgb, vec3(2.2)) : texel.rgb;
  albedo *= uObjectTint;
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
uniform bool uVisualizeOverdraw;
uniform float uOverdrawRange;
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

void main() {
  vec3 color = texture(uScene, vUv).rgb;
  if (uVisualizeOverdraw) {
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
  if (uDithering) color += bayer4(ivec2(gl_FragCoord.xy)) / levels;
  color = round(clamp(color, 0.0, 1.0) * levels) / levels;
  fragColor = vec4(color, 1.0);
}
)GLSL";

inline constexpr const char* differenceFragmentShader = R"GLSL(
#version 410 core
in vec2 vUv;
uniform sampler2D uImageA;
uniform sampler2D uImageB;
uniform float uExposure;
out vec4 fragColor;

void main() {
  vec3 difference = abs(texture(uImageA, vUv).rgb - texture(uImageB, vUv).rgb);
  fragColor = vec4(clamp(difference * uExposure, 0.0, 1.0), 1.0);
}
)GLSL";

inline constexpr const char* shadowVertexShader = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPosition;
uniform mat4 uModel;
uniform mat4 uLightSpace;
uniform float uQuantization;
void main() {
  vec3 position = aPosition;
  if (uQuantization > 0.0) position = round(position / uQuantization) * uQuantization;
  gl_Position = uLightSpace * uModel * vec4(position, 1.0);
}
)GLSL";

inline constexpr const char* shadowFragmentShader = R"GLSL(
#version 410 core
void main() {}
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
