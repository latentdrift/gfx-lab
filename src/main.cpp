#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

struct RendererState {
  struct Geometry { float vertexQuantization = 0.0f; bool clipping = false; float clipHeight = 0.0f; bool clipAbove = false; } geometry;
  struct Camera { float fieldOfView = 45.0f; float nearPlane = 0.05f; bool orthographic = false; float orthographicSize = 4.0f; } camera;
  struct Rasterization { bool affineMapping = false; int cullMode = 1; int samples = 1; } rasterization;
  struct Surface { bool smoothShading = true; bool wireframe = false; int visualization = 0; int transparency = 0; float alphaCutoff = 0.5f; } surface;
  struct Texture { bool nearestFiltering = false; bool repeat = true; bool mipmapping = false; bool trilinear = false; float anisotropy = 1.0f; } texture;
  struct Lighting { int model = 2; float ambient = 0.22f; float azimuth = 34.0f; float elevation = 52.0f; float shininess = 32.0f; } lighting;
  struct Depth { bool testing = true; bool writing = true; int precision = 24; int function = 0; int visualization = 0; } depth;
  struct Color { int bitsPerChannel = 8; bool dithering = false; bool linearLight = true; } color;
  struct Post { bool fog = false; float fogStart = 3.0f; float fogEnd = 7.0f; } post;
  struct Output { int width = 640; int height = 480; bool nearestUpscaling = true; } output;
};

enum class Category { Geometry, Camera, Rasterization, Surface, Texture, Lighting, Depth, Color, Post, Output };
enum class CompareMode { A, B, Split };
enum class TestScene { Torus, TexturePlane, DepthPrecision, Transparency, Lighting };

struct CameraOrbit {
  float yaw = 0.72f;
  float pitch = 0.36f;
  float distance = 5.2f;
  glm::vec3 target{0.0f};

  glm::vec3 eye() const {
    const float cp = std::cos(pitch);
    return target + distance * glm::vec3(cp * std::sin(yaw), std::sin(pitch), cp * std::cos(yaw));
  }

  glm::mat4 view() const { return glm::lookAt(eye(), target, glm::vec3(0, 1, 0)); }
};

struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 uv;
  glm::vec3 barycentric;
  glm::vec3 color;
};

struct MeshRange {
  GLint first = 0;
  GLsizei count = 0;
};

void appendTriangle(std::vector<Vertex>& vertices, Vertex a, Vertex b, Vertex c) {
  a.barycentric = {1, 0, 0};
  b.barycentric = {0, 1, 0};
  c.barycentric = {0, 0, 1};
  vertices.push_back(a);
  vertices.push_back(b);
  vertices.push_back(c);
}

std::vector<Vertex> makePlane(float width, float depth, int xSegments, int zSegments) {
  std::vector<Vertex> vertices;
  for (int z = 0; z < zSegments; ++z) {
    for (int x = 0; x < xSegments; ++x) {
      const float x0 = (static_cast<float>(x) / xSegments - 0.5f) * width;
      const float x1 = (static_cast<float>(x + 1) / xSegments - 0.5f) * width;
      const float z0 = (static_cast<float>(z) / zSegments - 0.5f) * depth;
      const float z1 = (static_cast<float>(z + 1) / zSegments - 0.5f) * depth;
      const float u0 = static_cast<float>(x) / xSegments * 8.0f;
      const float u1 = static_cast<float>(x + 1) / xSegments * 8.0f;
      const float v0 = static_cast<float>(z) / zSegments * 16.0f;
      const float v1 = static_cast<float>(z + 1) / zSegments * 16.0f;
      const glm::vec3 n(0, 1, 0), color(0.65f, 0.72f, 0.78f);
      Vertex a{{x0, 0, z0}, n, {u0, v0}, {}, color};
      Vertex b{{x0, 0, z1}, n, {u0, v1}, {}, color};
      Vertex c{{x1, 0, z1}, n, {u1, v1}, {}, color};
      Vertex d{{x1, 0, z0}, n, {u1, v0}, {}, color};
      appendTriangle(vertices, a, b, c);
      appendTriangle(vertices, a, c, d);
    }
  }
  return vertices;
}

std::vector<Vertex> makeQuad() {
  std::vector<Vertex> vertices;
  const glm::vec3 n(0, 0, 1), color(0.5f, 0.8f, 0.7f);
  Vertex a{{-1, -1, 0}, n, {0, 0}, {}, color};
  Vertex b{{ 1, -1, 0}, n, {4, 0}, {}, color};
  Vertex c{{ 1,  1, 0}, n, {4, 4}, {}, color};
  Vertex d{{-1,  1, 0}, n, {0, 4}, {}, color};
  appendTriangle(vertices, a, b, c);
  appendTriangle(vertices, a, c, d);
  return vertices;
}

std::vector<Vertex> makeSphere(int longitudeSegments, int latitudeSegments) {
  std::vector<Vertex> vertices;
  auto point = [=](int longitude, int latitude) {
    const float u = static_cast<float>(longitude) / longitudeSegments;
    const float v = static_cast<float>(latitude) / latitudeSegments;
    const float a = u * glm::two_pi<float>();
    const float b = (v - 0.5f) * glm::pi<float>();
    const glm::vec3 n(std::cos(b) * std::sin(a), std::sin(b), std::cos(b) * std::cos(a));
    return Vertex{n, n, {u * 4.0f, v * 2.0f}, {}, n * 0.5f + 0.5f};
  };
  for (int y = 0; y < latitudeSegments; ++y) {
    for (int x = 0; x < longitudeSegments; ++x) {
      Vertex a = point(x, y), b = point(x + 1, y), c = point(x + 1, y + 1), d = point(x, y + 1);
      appendTriangle(vertices, a, b, c);
      appendTriangle(vertices, a, c, d);
    }
  }
  return vertices;
}

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "graphics-lab: %s\n", message.c_str());
  std::exit(EXIT_FAILURE);
}

GLuint compileShader(GLenum type, const char* source) {
  const GLuint shader = glCreateShader(type);
  glShaderSource(shader, 1, &source, nullptr);
  glCompileShader(shader);
  GLint ok = GL_FALSE;
  glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
  if (!ok) {
    GLint length = 0;
    glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(length), '\0');
    glGetShaderInfoLog(shader, length, nullptr, log.data());
    fail("shader compilation failed:\n" + log);
  }
  return shader;
}

GLuint makeProgram(const char* vertexSource, const char* fragmentSource) {
  const GLuint vertex = compileShader(GL_VERTEX_SHADER, vertexSource);
  const GLuint fragment = compileShader(GL_FRAGMENT_SHADER, fragmentSource);
  const GLuint program = glCreateProgram();
  glAttachShader(program, vertex);
  glAttachShader(program, fragment);
  glLinkProgram(program);
  glDeleteShader(vertex);
  glDeleteShader(fragment);
  GLint ok = GL_FALSE;
  glGetProgramiv(program, GL_LINK_STATUS, &ok);
  if (!ok) {
    GLint length = 0;
    glGetProgramiv(program, GL_INFO_LOG_LENGTH, &length);
    std::string log(static_cast<size_t>(length), '\0');
    glGetProgramInfoLog(program, length, nullptr, log.data());
    fail("program link failed:\n" + log);
  }
  return program;
}

std::vector<Vertex> makeTorus() {
  constexpr int majorSegments = 16;
  constexpr int minorSegments = 8;
  constexpr float majorRadius = 1.15f;
  constexpr float minorRadius = 0.46f;
  std::vector<Vertex> vertices;
  vertices.reserve(majorSegments * minorSegments * 6);

  auto point = [](int majorIndex, int minorIndex) {
    const float u = static_cast<float>(majorIndex) / majorSegments;
    const float v = static_cast<float>(minorIndex) / minorSegments;
    const float a = u * glm::two_pi<float>();
    const float b = v * glm::two_pi<float>();
    const glm::vec3 normal(std::cos(a) * std::cos(b), std::sin(b), std::sin(a) * std::cos(b));
    const glm::vec3 center(majorRadius * std::cos(a), 0.0f, majorRadius * std::sin(a));
    const glm::vec3 color = 0.5f + 0.5f * glm::vec3(std::cos(a), std::sin(b), std::sin(a));
    return Vertex{center + minorRadius * normal, normal, glm::vec2(u * 4.0f, v * 2.0f), {0, 0, 0}, color};
  };

  for (int i = 0; i < majorSegments; ++i) {
    for (int j = 0; j < minorSegments; ++j) {
      std::array<Vertex, 4> q = {point(i, j), point(i + 1, j), point(i + 1, j + 1), point(i, j + 1)};
      // The torus parameterization's +u x +v direction points inward, so emit
      // each quad in the opposite order to keep outward faces counter-clockwise.
      const std::array<int, 6> order = {0, 2, 1, 0, 3, 2};
      for (int k = 0; k < 6; ++k) {
        Vertex vertex = q[order[k]];
        vertex.barycentric = glm::vec3(0.0f);
        vertex.barycentric[k % 3] = 1.0f;
        vertices.push_back(vertex);
      }
    }
  }
  return vertices;
}

const char* sceneVertexShader = R"GLSL(
#version 410 core
layout(location=0) in vec3 aPosition;
layout(location=1) in vec3 aNormal;
layout(location=2) in vec2 aUv;
layout(location=3) in vec3 aBarycentric;
layout(location=4) in vec3 aColor;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform float uQuantization;
uniform bool uClipEnabled;
uniform vec4 uClipPlane;
uniform vec3 uLightDirection;
uniform float uAmbient;

out vec3 vWorldPosition;
out vec3 vNormal;
out vec2 vUvPerspective;
noperspective out vec2 vUvAffine;
out vec3 vBarycentric;
out vec3 vColor;
out float vVertexLighting;

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
  float vertexDiffuse = max(dot(vNormal, normalize(uLightDirection)), 0.0);
  vVertexLighting = uAmbient + (1.0 - uAmbient) * vertexDiffuse;
  gl_ClipDistance[0] = uClipEnabled ? dot(world, uClipPlane) : 1.0;
  gl_Position = uProjection * uView * world;
}
)GLSL";

const char* sceneFragmentShader = R"GLSL(
#version 410 core
in vec3 vWorldPosition;
in vec3 vNormal;
in vec2 vUvPerspective;
noperspective in vec2 vUvAffine;
in vec3 vBarycentric;
in vec3 vColor;
in float vVertexLighting;

uniform sampler2D uTexture;
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
uniform bool uLinearLight;
uniform vec3 uObjectTint;
uniform bool uFogEnabled;
uniform float uFogStart;
uniform float uFogEnd;
uniform vec3 uCameraPosition;
out vec4 fragColor;

void main() {
  vec2 uv = uAffineMapping ? vUvAffine : vUvPerspective;
  vec3 normal = uSmoothShading
    ? normalize(vNormal)
    : normalize(cross(dFdx(vWorldPosition), dFdy(vWorldPosition)));
  if (!gl_FrontFacing) normal = -normal;
  vec4 texel = texture(uTexture, uv);
  float alpha = uVisualization == 0 ? texel.a : 1.0;
  if (uTransparencyMode == 1 && alpha < uAlphaCutoff) discard;
  if (uTransparencyMode == 0) alpha = 1.0;
  vec3 albedo = uLinearLight ? pow(texel.rgb, vec3(2.2)) : texel.rgb;
  albedo *= uObjectTint;
  if (uVisualization == 1) albedo = vec3(fract(uv), 0.0);
  if (uVisualization == 2) albedo = normal * 0.5 + 0.5;
  if (uVisualization == 3) albedo = vColor;

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
  fragColor = vec4(color, alpha);
}
)GLSL";

const char* outputVertexShader = R"GLSL(
#version 410 core
out vec2 vUv;
void main() {
  const vec2 positions[3] = vec2[3](vec2(-1,-1), vec2(3,-1), vec2(-1,3));
  vec2 position = positions[gl_VertexID];
  vUv = position * 0.5 + 0.5;
  gl_Position = vec4(position, 0, 1);
}
)GLSL";

const char* outputFragmentShader = R"GLSL(
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
  if (uDepthVisualization != 0) {
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

struct RenderTarget {
  GLuint sceneFbo = 0;
  GLuint sceneTexture = 0;
  GLuint depthTexture = 0;
  GLuint multisampleFbo = 0;
  GLuint multisampleColor = 0;
  GLuint multisampleDepth = 0;
  GLuint outputFbo = 0;
  GLuint outputTexture = 0;
  int width = 0;
  int height = 0;
  int depthPrecision = 0;
  int samples = 0;

  void resize(int newWidth, int newHeight, int newDepthPrecision, int newSamples) {
    if (width == newWidth && height == newHeight && depthPrecision == newDepthPrecision && samples == newSamples) return;
    width = newWidth;
    height = newHeight;
    depthPrecision = newDepthPrecision;
    samples = newSamples;
    if (!sceneFbo) {
      glGenFramebuffers(1, &sceneFbo);
      glGenTextures(1, &sceneTexture);
      glGenTextures(1, &depthTexture);
      glGenFramebuffers(1, &multisampleFbo);
      glGenRenderbuffers(1, &multisampleColor);
      glGenRenderbuffers(1, &multisampleDepth);
      glGenFramebuffers(1, &outputFbo);
      glGenTextures(1, &outputTexture);
    }

    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    const GLint depthFormat = depthPrecision == 16 ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT24;
    const GLenum depthType = depthPrecision == 16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    glTexImage2D(GL_TEXTURE_2D, 0, depthFormat, width, height, 0, GL_DEPTH_COMPONENT, depthType, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTexture, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      fail("could not create resolved scene render target");

    if (samples > 1) {
      glBindRenderbuffer(GL_RENDERBUFFER, multisampleColor);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
      glBindRenderbuffer(GL_RENDERBUFFER, multisampleDepth);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, depthFormat, width, height);
      glBindFramebuffer(GL_FRAMEBUFFER, multisampleFbo);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, multisampleColor);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, multisampleDepth);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        fail("could not create multisampled render target");
    }

    glBindTexture(GL_TEXTURE_2D, outputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      fail("could not create output render target");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  void destroy() {
    glDeleteFramebuffers(1, &sceneFbo);
    glDeleteTextures(1, &sceneTexture);
    glDeleteTextures(1, &depthTexture);
    glDeleteFramebuffers(1, &multisampleFbo);
    glDeleteRenderbuffers(1, &multisampleColor);
    glDeleteRenderbuffers(1, &multisampleDepth);
    glDeleteFramebuffers(1, &outputFbo);
    glDeleteTextures(1, &outputTexture);
  }
};

class Renderer {
public:
  Renderer() {
    sceneProgram_ = makeProgram(sceneVertexShader, sceneFragmentShader);
    outputProgram_ = makeProgram(outputVertexShader, outputFragmentShader);
    std::vector<Vertex> vertices;
    auto appendMesh = [&vertices](const std::vector<Vertex>& mesh) {
      const MeshRange range{static_cast<GLint>(vertices.size()), static_cast<GLsizei>(mesh.size())};
      vertices.insert(vertices.end(), mesh.begin(), mesh.end());
      return range;
    };
    torus_ = appendMesh(makeTorus());
    plane_ = appendMesh(makePlane(7.0f, 16.0f, 8, 24));
    quad_ = appendMesh(makeQuad());
    lowSphere_ = appendMesh(makeSphere(12, 6));
    smoothSphere_ = appendMesh(makeSphere(32, 16));
    vertexCount_ = static_cast<GLsizei>(vertices.size());
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(vertices.size() * sizeof(Vertex)), vertices.data(), GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, position)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, normal)));
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, uv)));
    glEnableVertexAttribArray(3);
    glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, barycentric)));
    glEnableVertexAttribArray(4);
    glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, color)));
    glGenVertexArrays(1, &fullscreenVao_);
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples_);
    if (GLEW_EXT_texture_filter_anisotropic)
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy_);
    makeCheckerTexture();
  }

  ~Renderer() {
    targetA_.destroy();
    targetB_.destroy();
    glDeleteTextures(1, &checkerTexture_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteVertexArrays(1, &fullscreenVao_);
    glDeleteProgram(sceneProgram_);
    glDeleteProgram(outputProgram_);
  }

  GLuint render(const RendererState& state, const CameraOrbit& camera, TestScene scene, bool referenceTarget) {
    RenderTarget& target = referenceTarget ? targetB_ : targetA_;
    const int samples = state.rasterization.samples == 1 ? 1 : std::min(state.rasterization.samples, maxSamples_);
    target.resize(state.output.width, state.output.height, state.depth.precision, samples);
    glBindFramebuffer(GL_FRAMEBUFFER, samples > 1 ? target.multisampleFbo : target.sceneFbo);
    glViewport(0, 0, target.width, target.height);
    glDepthMask(GL_TRUE);
    const GLenum depthFunctions[] = {GL_LESS, GL_LEQUAL, GL_GREATER, GL_ALWAYS};
    glDepthFunc(depthFunctions[std::clamp(state.depth.function, 0, 3)]);
    glClearDepth(state.depth.function == 2 ? 0.0 : 1.0);
    if (state.depth.testing) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (state.rasterization.cullMode == 0) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(state.rasterization.cullMode == 1 ? GL_BACK : GL_FRONT);
    }
    if (state.surface.transparency == 2) {
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    } else {
      glDisable(GL_BLEND);
    }
    glEnable(GL_CLIP_DISTANCE0);
    const float backgroundR = state.color.linearLight ? std::pow(0.105f, 2.2f) : 0.105f;
    const float backgroundG = state.color.linearLight ? std::pow(0.112f, 2.2f) : 0.112f;
    const float backgroundB = state.color.linearLight ? std::pow(0.120f, 2.2f) : 0.120f;
    glClearColor(backgroundR, backgroundG, backgroundB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    glDepthMask(state.depth.writing ? GL_TRUE : GL_FALSE);

    glUseProgram(sceneProgram_);
    const glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(-14.0f), glm::vec3(1, 0, 0));
    const glm::mat4 view = camera.view();
    const float aspect = static_cast<float>(target.width) / static_cast<float>(target.height);
    const float halfHeight = state.camera.orthographicSize * 0.5f;
    const glm::mat4 projection = state.camera.orthographic
      ? glm::ortho(-halfHeight * aspect, halfHeight * aspect, -halfHeight, halfHeight, state.camera.nearPlane, 100.0f)
      : glm::perspective(glm::radians(state.camera.fieldOfView), aspect, state.camera.nearPlane, 100.0f);
    matrix("uModel", model);
    matrix("uView", view);
    matrix("uProjection", projection);
    glUniform1f(location("uQuantization"), state.geometry.vertexQuantization);
    glUniform1i(location("uClipEnabled"), state.geometry.clipping);
    const glm::vec4 clipPlane(0.0f, state.geometry.clipAbove ? -1.0f : 1.0f, 0.0f,
      state.geometry.clipAbove ? state.geometry.clipHeight : -state.geometry.clipHeight);
    glUniform4fv(location("uClipPlane"), 1, glm::value_ptr(clipPlane));
    glUniform1i(location("uAffineMapping"), state.rasterization.affineMapping);
    glUniform1i(location("uSmoothShading"), state.surface.smoothShading);
    glUniform1i(location("uWireframe"), state.surface.wireframe);
    glUniform1i(location("uVisualization"), state.surface.visualization);
    glUniform1i(location("uTransparencyMode"), state.surface.transparency);
    glUniform1f(location("uAlphaCutoff"), state.surface.alphaCutoff);
    glUniform1i(location("uLightingModel"), state.lighting.model);
    glUniform1f(location("uAmbient"), state.lighting.ambient);
    glUniform1f(location("uShininess"), state.lighting.shininess);
    glUniform1i(location("uLinearLight"), state.color.linearLight);
    const float azimuth = glm::radians(state.lighting.azimuth);
    const float elevation = glm::radians(state.lighting.elevation);
    const glm::vec3 lightDirection(std::cos(elevation) * std::cos(azimuth), std::sin(elevation),
      std::cos(elevation) * std::sin(azimuth));
    glUniform3fv(location("uLightDirection"), 1, glm::value_ptr(lightDirection));
    glUniform1i(location("uFogEnabled"), state.post.fog);
    glUniform1f(location("uFogStart"), state.post.fogStart);
    glUniform1f(location("uFogEnd"), state.post.fogEnd);
    glUniform3fv(location("uCameraPosition"), 1, glm::value_ptr(camera.eye()));
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);
    GLint minificationFilter = state.texture.nearestFiltering ? GL_NEAREST : GL_LINEAR;
    if (state.texture.mipmapping) {
      if (state.texture.nearestFiltering) minificationFilter = GL_NEAREST_MIPMAP_NEAREST;
      else minificationFilter = state.texture.trilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
    }
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minificationFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, state.texture.nearestFiltering ? GL_NEAREST : GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    if (GLEW_EXT_texture_filter_anisotropic)
      glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT, std::clamp(state.texture.anisotropy, 1.0f, maxAnisotropy_));
    glUniform1i(location("uTexture"), 0);
    glBindVertexArray(vao_);
    auto drawMesh = [this](const MeshRange& mesh, const glm::mat4& modelMatrix, const glm::vec3& tint) {
      matrix("uModel", modelMatrix);
      glUniform3fv(location("uObjectTint"), 1, glm::value_ptr(tint));
      glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
    };
    const glm::mat4 identity(1.0f);
    switch (scene) {
      case TestScene::Torus:
        drawMesh(torus_, glm::rotate(identity, glm::radians(-14.0f), glm::vec3(1, 0, 0)), glm::vec3(1.0f));
        break;
      case TestScene::TexturePlane:
        drawMesh(plane_, glm::translate(identity, glm::vec3(0, -1.25f, -3.5f)), glm::vec3(1.0f));
        break;
      case TestScene::DepthPrecision: {
        const glm::mat4 horizontal = glm::rotate(identity, glm::radians(-90.0f), glm::vec3(1, 0, 0));
        drawMesh(quad_, glm::scale(horizontal, glm::vec3(2.2f)), glm::vec3(0.65f, 0.85f, 1.0f));
        drawMesh(quad_, glm::translate(horizontal, glm::vec3(0, 0, 0.00015f)) * glm::scale(identity, glm::vec3(1.65f)), glm::vec3(1.0f, 0.55f, 0.42f));
        drawMesh(quad_, glm::translate(horizontal, glm::vec3(0, 0, 0.00030f)) * glm::scale(identity, glm::vec3(1.05f)), glm::vec3(0.5f, 1.0f, 0.62f));
        break;
      }
      case TestScene::Transparency:
        drawMesh(quad_, glm::rotate(identity, glm::radians(28.0f), glm::vec3(0, 1, 0)), glm::vec3(0.42f, 0.8f, 1.0f));
        drawMesh(quad_, glm::rotate(identity, glm::radians(-35.0f), glm::vec3(0, 1, 0)), glm::vec3(1.0f, 0.48f, 0.35f));
        drawMesh(quad_, glm::rotate(identity, glm::radians(90.0f), glm::vec3(1, 0, 0)), glm::vec3(0.55f, 1.0f, 0.56f));
        break;
      case TestScene::Lighting:
        drawMesh(lowSphere_, glm::translate(identity, glm::vec3(-1.35f, 0, 0)), glm::vec3(0.9f, 0.55f, 0.38f));
        drawMesh(smoothSphere_, glm::translate(identity, glm::vec3(1.35f, 0, 0)), glm::vec3(0.45f, 0.68f, 1.0f));
        drawMesh(torus_, glm::translate(glm::scale(identity, glm::vec3(0.62f)), glm::vec3(0, 1.9f, 0)), glm::vec3(0.7f, 1.0f, 0.6f));
        break;
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glDisable(GL_CLIP_DISTANCE0);

    if (samples > 1) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, target.multisampleFbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.sceneFbo);
      glBlitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height,
        GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT, GL_NEAREST);
    }

    glBindFramebuffer(GL_FRAMEBUFFER, target.outputFbo);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glViewport(0, 0, target.width, target.height);
    glUseProgram(outputProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target.sceneTexture);
    glUniform1i(glGetUniformLocation(outputProgram_, "uScene"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, target.depthTexture);
    glUniform1i(glGetUniformLocation(outputProgram_, "uDepth"), 1);
    glUniform1i(glGetUniformLocation(outputProgram_, "uBitsPerChannel"), state.color.bitsPerChannel);
    glUniform1i(glGetUniformLocation(outputProgram_, "uDithering"), state.color.dithering);
    glUniform1i(glGetUniformLocation(outputProgram_, "uLinearLight"), state.color.linearLight);
    glUniform1i(glGetUniformLocation(outputProgram_, "uDepthVisualization"), state.depth.visualization);
    glUniform1f(glGetUniformLocation(outputProgram_, "uNearPlane"), state.camera.nearPlane);
    glUniform1f(glGetUniformLocation(outputProgram_, "uFarPlane"), 100.0f);
    glUniform1i(glGetUniformLocation(outputProgram_, "uOrthographic"), state.camera.orthographic);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, target.outputTexture);
    const GLint upscaleFilter = state.output.nearestUpscaling ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, upscaleFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, upscaleFilter);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return target.outputTexture;
  }

private:
  GLuint sceneProgram_ = 0, outputProgram_ = 0;
  GLuint vao_ = 0, vbo_ = 0, fullscreenVao_ = 0, checkerTexture_ = 0;
  GLsizei vertexCount_ = 0;
  GLint maxSamples_ = 1;
  GLfloat maxAnisotropy_ = 1.0f;
  MeshRange torus_, plane_, quad_, lowSphere_, smoothSphere_;
  RenderTarget targetA_, targetB_;

  GLint location(const char* name) const { return glGetUniformLocation(sceneProgram_, name); }
  void matrix(const char* name, const glm::mat4& value) { glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value)); }

  void makeCheckerTexture() {
    constexpr int size = 64;
    std::array<unsigned char, size * size * 4> pixels{};
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const bool checker = ((x / 8) + (y / 8)) % 2 == 0;
        const glm::u8vec3 color = checker ? glm::u8vec3(205, 188, 146) : glm::u8vec3(53, 68, 72);
        const int offset = (y * size + x) * 4;
        pixels[offset] = color.r; pixels[offset + 1] = color.g; pixels[offset + 2] = color.b;
        pixels[offset + 3] = checker ? 255 : 72;
      }
    }
    glGenTextures(1, &checkerTexture_);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
  }
};

void setStyle() {
  ImGuiStyle& style = ImGui::GetStyle();
  style.WindowRounding = 0.0f;
  style.ChildRounding = 0.0f;
  style.FrameRounding = 1.0f;
  style.PopupRounding = 1.0f;
  style.ScrollbarRounding = 1.0f;
  style.GrabRounding = 1.0f;
  style.WindowBorderSize = 0.0f;
  style.ChildBorderSize = 1.0f;
  style.FrameBorderSize = 1.0f;
  style.ItemSpacing = ImVec2(7, 6);
  style.FramePadding = ImVec2(7, 4);
  style.WindowPadding = ImVec2(10, 9);
  auto& c = style.Colors;
  c[ImGuiCol_WindowBg] = ImVec4(0.105f, 0.11f, 0.12f, 1);
  c[ImGuiCol_ChildBg] = ImVec4(0.125f, 0.13f, 0.14f, 1);
  c[ImGuiCol_Border] = ImVec4(0.25f, 0.26f, 0.27f, 1);
  c[ImGuiCol_FrameBg] = ImVec4(0.16f, 0.17f, 0.18f, 1);
  c[ImGuiCol_FrameBgHovered] = ImVec4(0.20f, 0.21f, 0.22f, 1);
  c[ImGuiCol_FrameBgActive] = ImVec4(0.23f, 0.24f, 0.25f, 1);
  c[ImGuiCol_Button] = ImVec4(0.16f, 0.17f, 0.18f, 1);
  c[ImGuiCol_ButtonHovered] = ImVec4(0.21f, 0.22f, 0.23f, 1);
  c[ImGuiCol_ButtonActive] = ImVec4(0.26f, 0.29f, 0.30f, 1);
  c[ImGuiCol_Header] = ImVec4(0.19f, 0.25f, 0.27f, 1);
  c[ImGuiCol_HeaderHovered] = ImVec4(0.22f, 0.29f, 0.31f, 1);
  c[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.32f, 0.34f, 1);
  c[ImGuiCol_CheckMark] = ImVec4(0.56f, 0.75f, 0.77f, 1);
  c[ImGuiCol_SliderGrab] = ImVec4(0.48f, 0.65f, 0.67f, 1);
  c[ImGuiCol_Text] = ImVec4(0.86f, 0.87f, 0.88f, 1);
  c[ImGuiCol_TextDisabled] = ImVec4(0.52f, 0.54f, 0.55f, 1);
}

bool radioPair(const char* first, const char* second, bool& secondSelected) {
  bool changed = false;
  if (ImGui::RadioButton(first, !secondSelected)) { secondSelected = false; changed = true; }
  ImGui::SameLine();
  if (ImGui::RadioButton(second, secondSelected)) { secondSelected = true; changed = true; }
  return changed;
}

void description(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::PushTextWrapPos();
  ImGui::TextUnformatted(text);
  ImGui::PopTextWrapPos();
  ImGui::PopStyleColor();
  ImGui::Spacing();
}

void inspector(Category category, RendererState& state) {
  switch (category) {
    case Category::Geometry: {
      ImGui::TextUnformatted("GEOMETRY"); ImGui::Separator();
      ImGui::TextUnformatted("Vertex position precision");
      const char* labels[] = {"Full precision", "1/64 unit", "1/32 unit", "1/16 unit", "1/8 unit"};
      const float values[] = {0.0f, 1.0f/64.0f, 1.0f/32.0f, 1.0f/16.0f, 1.0f/8.0f};
      int selected = 0;
      for (int i = 1; i < 5; ++i) if (std::abs(state.geometry.vertexQuantization - values[i]) < 0.0001f) selected = i;
      if (ImGui::Combo("##precision", &selected, labels, 5)) state.geometry.vertexQuantization = values[selected];
      description("Rounds model-space vertex positions to a fixed grid before projection.");
      ImGui::Checkbox("World-space clipping plane", &state.geometry.clipping);
      ImGui::SliderFloat("Plane height", &state.geometry.clipHeight, -1.5f, 1.5f, "y = %.2f");
      radioPair("Keep above", "Keep below", state.geometry.clipAbove);
      description("The GPU clips primitives against a horizontal world-space plane before rasterization.");
      break;
    }
    case Category::Camera: {
      ImGui::TextUnformatted("CAMERA"); ImGui::Separator();
      ImGui::TextUnformatted("Projection");
      radioPair("Perspective", "Orthographic", state.camera.orthographic);
      description("Perspective divides by depth; orthographic projection preserves apparent size with distance.");
      if (state.camera.orthographic)
        ImGui::SliderFloat("View height", &state.camera.orthographicSize, 1.0f, 10.0f, "%.2f units");
      else
        ImGui::SliderFloat("Field of view", &state.camera.fieldOfView, 20.0f, 100.0f, "%.0f deg");
      ImGui::SliderFloat("Near clipping plane", &state.camera.nearPlane, 0.01f, 2.0f, "%.3f unit", ImGuiSliderFlags_Logarithmic);
      description("Geometry closer than this camera-space distance is clipped. It also strongly affects depth precision.");
      break;
    }
    case Category::Rasterization: {
      ImGui::TextUnformatted("RASTERIZATION"); ImGui::Separator();
      ImGui::TextUnformatted("Texture coordinate interpolation");
      radioPair("Perspective-correct", "Affine", state.rasterization.affineMapping);
      description("Affine interpolation does not compensate texture coordinates for perspective depth.");
      ImGui::TextUnformatted("Face culling");
      const char* cullLabels[] = {"None", "Back faces", "Front faces"};
      ImGui::Combo("##culling", &state.rasterization.cullMode, cullLabels, 3);
      description("Discards triangles according to their screen-space winding direction.");
      ImGui::TextUnformatted("Multisample anti-aliasing");
      const char* sampleLabels[] = {"Off (1 sample)", "2 samples", "4 samples", "8 samples"};
      const int sampleValues[] = {1, 2, 4, 8};
      int sampleIndex = state.rasterization.samples == 1 ? 0 : state.rasterization.samples == 2 ? 1 : state.rasterization.samples == 4 ? 2 : 3;
      if (ImGui::Combo("##samples", &sampleIndex, sampleLabels, 4)) state.rasterization.samples = sampleValues[sampleIndex];
      description("Stores multiple coverage and depth samples per pixel, then resolves them to one color.");
      break;
    }
    case Category::Surface: {
      ImGui::TextUnformatted("SURFACE"); ImGui::Separator();
      ImGui::TextUnformatted("Surface visualization");
      const char* visualizationLabels[] = {"Texture", "UV coordinates", "Normals", "Vertex colors"};
      ImGui::Combo("##visualization", &state.surface.visualization, visualizationLabels, 4);
      description("Selects the mesh attribute used as the surface's base color.");
      ImGui::TextUnformatted("Shading interpolation");
      bool flat = !state.surface.smoothShading;
      if (radioPair("Smooth", "Flat", flat)) state.surface.smoothShading = !flat;
      description("Smooth shading interpolates vertex normals; flat shading uses one face normal per triangle.");
      ImGui::Checkbox("Wireframe overlay", &state.surface.wireframe);
      description("Draws triangle boundaries over the shaded surface.");
      ImGui::TextUnformatted("Transparency operation");
      const char* transparencyLabels[] = {"Opaque", "Alpha test (discard)", "Alpha blending"};
      ImGui::Combo("##transparency", &state.surface.transparency, transparencyLabels, 3);
      if (state.surface.transparency == 1)
        ImGui::SliderFloat("Alpha cutoff", &state.surface.alphaCutoff, 0.0f, 1.0f, "%.2f");
      description("Discard makes a binary coverage decision; blending combines source and framebuffer colors.");
      break;
    }
    case Category::Texture:
      ImGui::TextUnformatted("TEXTURE"); ImGui::Separator();
      ImGui::TextUnformatted("Texture filtering");
      radioPair("Bilinear", "Nearest", state.texture.nearestFiltering);
      description("Selects how samples between adjacent texels are reconstructed.");
      ImGui::TextUnformatted("Texture address mode");
      radioPair("Clamp to edge", "Repeat", state.texture.repeat);
      description("Defines how texture coordinates outside the normalized 0-1 range are sampled.");
      ImGui::Checkbox("Mipmapping", &state.texture.mipmapping);
      description("Selects prefiltered, lower-resolution texture levels during minification.");
      ImGui::BeginDisabled(!state.texture.mipmapping || state.texture.nearestFiltering);
      ImGui::Checkbox("Trilinear mip interpolation", &state.texture.trilinear);
      ImGui::EndDisabled();
      description("Interpolates between the two nearest mip levels as well as between texels.");
      ImGui::SliderFloat("Anisotropy", &state.texture.anisotropy, 1.0f, 16.0f, "%.0f x");
      description("Uses additional samples to preserve detail when texture footprints are elongated by perspective.");
      break;
    case Category::Lighting: {
      ImGui::TextUnformatted("LIGHTING"); ImGui::Separator();
      ImGui::TextUnformatted("Lighting model");
      const char* lightingLabels[] = {"Unlit", "Gouraud / per-vertex Lambert", "Phong shading / per-fragment Lambert",
        "Phong reflection", "Blinn-Phong reflection"};
      ImGui::Combo("##lighting-model", &state.lighting.model, lightingLabels, 5);
      description("Gouraud interpolates computed vertex lighting; Phong shading interpolates normals and lights each fragment.");
      ImGui::SliderFloat("Ambient term", &state.lighting.ambient, 0.0f, 1.0f, "%.2f");
      if (state.lighting.model >= 3)
        ImGui::SliderFloat("Specular exponent", &state.lighting.shininess, 2.0f, 128.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Light azimuth", &state.lighting.azimuth, -180.0f, 180.0f, "%.0f deg");
      ImGui::SliderFloat("Light elevation", &state.lighting.elevation, -90.0f, 90.0f, "%.0f deg");
      description("Azimuth rotates around the vertical axis; elevation moves above or below the horizon.");
      break;
    }
    case Category::Depth: {
      ImGui::TextUnformatted("DEPTH"); ImGui::Separator();
      ImGui::Checkbox("Depth testing", &state.depth.testing);
      description("Compares each fragment's depth against the stored depth value before drawing it.");
      ImGui::Checkbox("Depth writes", &state.depth.writing);
      description("Stores passing fragment depths in the depth buffer. Disabling the depth test also prevents writes.");
      ImGui::TextUnformatted("Depth comparison function");
      const char* functionLabels[] = {"Less", "Less or equal", "Greater", "Always"};
      ImGui::Combo("##depth-function", &state.depth.function, functionLabels, 4);
      description("Determines which comparison between incoming and stored depth values passes.");
      ImGui::TextUnformatted("Depth buffer precision");
      const char* depthLabels[] = {"16-bit fixed point", "24-bit fixed point"};
      int selected = state.depth.precision == 16 ? 0 : 1;
      if (ImGui::Combo("##depth-precision", &selected, depthLabels, 2)) state.depth.precision = selected == 0 ? 16 : 24;
      description("Sets the actual storage precision of the framebuffer's depth attachment.");
      ImGui::TextUnformatted("Depth visualization");
      const char* viewLabels[] = {"Off", "Raw window-space depth", "Linear camera depth (0-10 units)"};
      ImGui::Combo("##depth-view", &state.depth.visualization, viewLabels, 3);
      description("Raw perspective depth is nonlinear; linearization reconstructs camera-space distance.");
      break;
    }
    case Category::Color: {
      ImGui::TextUnformatted("COLOR"); ImGui::Separator();
      ImGui::TextUnformatted("Output color depth");
      const char* labels[] = {"24-bit (8:8:8)", "15-bit (5:5:5)", "12-bit (4:4:4)"};
      int selected = state.color.bitsPerChannel == 8 ? 0 : state.color.bitsPerChannel == 5 ? 1 : 2;
      if (ImGui::Combo("##depth", &selected, labels, 3)) state.color.bitsPerChannel = selected == 0 ? 8 : selected == 1 ? 5 : 4;
      description("Quantizes each output color channel to a fixed number of levels.");
      ImGui::Checkbox("Ordered dithering (4 x 4 Bayer)", &state.color.dithering);
      description("Offsets pixels with a fixed threshold matrix before color quantization.");
      ImGui::TextUnformatted("Lighting color space");
      radioPair("Encoded RGB (incorrect)", "Linear light", state.color.linearLight);
      description("Linear-light mode decodes texture values before lighting and encodes the final image for display.");
      break;
    }
    case Category::Post:
      ImGui::TextUnformatted("POST"); ImGui::Separator();
      ImGui::Checkbox("Linear distance fog", &state.post.fog);
      description("Blends shaded fragments toward the background according to camera distance.");
      ImGui::SliderFloat("Fog start", &state.post.fogStart, 0.0f, 12.0f, "%.2f units");
      ImGui::SliderFloat("Fog end", &state.post.fogEnd, 0.0f, 12.0f, "%.2f units");
      description("Start is fully clear; end is fully fogged.");
      break;
    case Category::Output: {
      ImGui::TextUnformatted("OUTPUT"); ImGui::Separator();
      ImGui::TextUnformatted("Internal render resolution");
      const char* labels[] = {"1280 x 960", "640 x 480", "320 x 240", "256 x 192", "160 x 120"};
      const int widths[] = {1280, 640, 320, 256, 160};
      const int heights[] = {960, 480, 240, 192, 120};
      int selected = 1;
      for (int i = 0; i < 5; ++i) if (state.output.width == widths[i] && state.output.height == heights[i]) selected = i;
      if (ImGui::Combo("##resolution", &selected, labels, 5)) { state.output.width = widths[selected]; state.output.height = heights[selected]; }
      description("Scene and output passes render at this exact pixel resolution.");
      ImGui::TextUnformatted("Viewport upscaling");
      radioPair("Bilinear", "Nearest", state.output.nearestUpscaling);
      description("Filters the completed internal-resolution framebuffer when enlarging it to the viewport.");
      break;
    }
  }
}

const char* categoryName(Category category) {
  switch (category) {
    case Category::Geometry: return "Geometry";
    case Category::Camera: return "Camera";
    case Category::Rasterization: return "Rasterization";
    case Category::Surface: return "Surface";
    case Category::Texture: return "Texture";
    case Category::Lighting: return "Lighting";
    case Category::Depth: return "Depth";
    case Category::Color: return "Color";
    case Category::Post: return "Post";
    case Category::Output: return "Output";
  }
  return "";
}

void glfwError(int, const char* descriptionText) { std::fprintf(stderr, "GLFW: %s\n", descriptionText); }

} // namespace

int main() {
  glfwSetErrorCallback(glfwError);
  if (!glfwInit()) fail("GLFW initialization failed");
  glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
  glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 1);
  glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
  glfwWindowHint(GLFW_SAMPLES, 0);
  GLFWwindow* window = glfwCreateWindow(1440, 900, "Graphics Lab", nullptr, nullptr);
  if (!window) fail("window creation failed");
  glfwMakeContextCurrent(window);
  glfwSwapInterval(1);
  glewExperimental = GL_TRUE;
  if (glewInit() != GLEW_OK) fail("OpenGL function loading failed");
  glGetError();

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  setStyle();
  ImGui_ImplGlfw_InitForOpenGL(window, true);
  ImGui_ImplOpenGL3_Init("#version 410 core");

  Renderer renderer;
  RendererState current;
  RendererState reference = current;
  CameraOrbit camera;
  Category category = Category::Geometry;
  TestScene scene = TestScene::Torus;
  CompareMode compare = CompareMode::A;
  bool viewportHovered = false;

  while (!glfwWindowShouldClose(window)) {
    glfwPollEvents();
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    ImGuiIO& io = ImGui::GetIO();
    if (viewportHovered) {
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        camera.yaw -= io.MouseDelta.x * 0.008f;
        camera.pitch = std::clamp(camera.pitch + io.MouseDelta.y * 0.008f, -1.45f, 1.45f);
      }
      if (ImGui::IsMouseDragging(ImGuiMouseButton_Middle) || ImGui::IsMouseDragging(ImGuiMouseButton_Right)) {
        const glm::mat4 inverseView = glm::inverse(camera.view());
        const glm::vec3 right = glm::vec3(inverseView[0]);
        const glm::vec3 up = glm::vec3(inverseView[1]);
        camera.target += (-right * io.MouseDelta.x + up * io.MouseDelta.y) * camera.distance * 0.0015f;
      }
      if (io.MouseWheel != 0.0f) camera.distance = std::clamp(camera.distance * std::pow(0.88f, io.MouseWheel), 1.4f, 14.0f);
    }

    int framebufferWidth, framebufferHeight;
    glfwGetFramebufferSize(window, &framebufferWidth, &framebufferHeight);
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(io.DisplaySize);
    ImGui::Begin("Graphics Lab", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);

    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("GRAPHICS LAB");
    ImGui::SameLine(150);
    ImGui::SetNextItemWidth(180.0f);
    const char* sceneLabels[] = {"Torus", "Texture minification", "Depth precision", "Transparency", "Lighting comparison"};
    int sceneIndex = static_cast<int>(scene);
    if (ImGui::Combo("##test-scene", &sceneIndex, sceneLabels, 5)) scene = static_cast<TestScene>(sceneIndex);
    ImGui::SameLine();
    if (ImGui::Button("Reset neutral")) current = RendererState{};
    ImGui::SameLine();
    if (ImGui::Button("Copy A to B")) reference = current;
    ImGui::SameLine();
    ImGui::TextDisabled("Compare:");
    ImGui::SameLine();
    if (ImGui::RadioButton("A", compare == CompareMode::A)) compare = CompareMode::A;
    ImGui::SameLine();
    if (ImGui::RadioButton("B", compare == CompareMode::B)) compare = CompareMode::B;
    ImGui::SameLine();
    if (ImGui::RadioButton("Split A/B", compare == CompareMode::Split)) compare = CompareMode::Split;
    ImGui::SameLine(ImGui::GetWindowWidth() - 260);
    ImGui::TextDisabled("LMB orbit   MMB/RMB pan   Wheel zoom");
    ImGui::Separator();

    const float contentHeight = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("Pipeline", ImVec2(145, contentHeight), true);
    ImGui::TextDisabled("PIPELINE");
    ImGui::Spacing();
    constexpr std::array<Category, 10> categories = {Category::Geometry, Category::Camera, Category::Rasterization,
      Category::Surface, Category::Texture, Category::Lighting, Category::Depth, Category::Color, Category::Post,
      Category::Output};
    for (Category candidate : categories) {
      if (ImGui::Selectable(categoryName(candidate), category == candidate, 0, ImVec2(0, 28))) category = candidate;
    }
    ImGui::EndChild();
    ImGui::SameLine(0, 5);

    const float inspectorWidth = 310;
    const float viewportWidth = std::max(100.0f, ImGui::GetContentRegionAvail().x - inspectorWidth - 5);
    ImGui::BeginChild("Viewport", ImVec2(viewportWidth, contentHeight), true, ImGuiWindowFlags_NoScrollbar);
    const ImVec2 available = ImGui::GetContentRegionAvail();
    const ImVec2 paneOrigin = ImGui::GetCursorScreenPos();
    constexpr float cameraWidth = 960.0f;
    constexpr float cameraHeight = 720.0f;
    const float presentationScale = std::min(available.x / cameraWidth, available.y / cameraHeight);
    const ImVec2 presentationSize(cameraWidth * presentationScale, cameraHeight * presentationScale);
    const ImVec2 origin(
      paneOrigin.x + std::floor((available.x - presentationSize.x) * 0.5f),
      paneOrigin.y + std::floor((available.y - presentationSize.y) * 0.5f));
    const ImVec2 end(origin.x + presentationSize.x, origin.y + presentationSize.y);
    const GLuint textureA = renderer.render(current, camera, scene, false);
    const GLuint textureB = (compare == CompareMode::A) ? 0 : renderer.render(reference, camera, scene, true);
    ImDrawList* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(origin, end, IM_COL32(27, 29, 31, 255));
    if (compare == CompareMode::Split) {
      const float middle = origin.x + std::floor(presentationSize.x * 0.5f);
      draw->PushClipRect(origin, ImVec2(middle, end.y), true);
      draw->AddImage(static_cast<ImTextureID>(textureA), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      draw->PopClipRect();
      draw->PushClipRect(ImVec2(middle + 1, origin.y), end, true);
      draw->AddImage(static_cast<ImTextureID>(textureB), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      draw->PopClipRect();
      draw->AddLine(ImVec2(middle, origin.y), ImVec2(middle, end.y), IM_COL32(225, 225, 225, 210));
      draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240,240,240,220), "A  CURRENT");
      draw->AddText(ImVec2(middle + 10, origin.y + 8), IM_COL32(240,240,240,220), "B  REFERENCE");
    } else {
      const GLuint texture = compare == CompareMode::A ? textureA : textureB;
      draw->AddImage(static_cast<ImTextureID>(texture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240,240,240,220), compare == CompareMode::A ? "A  CURRENT" : "B  REFERENCE");
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("viewport-input", presentationSize);
    viewportHovered = ImGui::IsItemHovered();
    ImGui::EndChild();
    ImGui::SameLine(0, 5);

    ImGui::BeginChild("Inspector", ImVec2(inspectorWidth, contentHeight), true);
    inspector(category, current);
    ImGui::EndChild();
    ImGui::End();

    ImGui::Render();
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, framebufferWidth, framebufferHeight);
    glClearColor(0.08f, 0.085f, 0.09f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    glfwSwapBuffers(window);
  }

  ImGui_ImplOpenGL3_Shutdown();
  ImGui_ImplGlfw_Shutdown();
  ImGui::DestroyContext();
  glfwDestroyWindow(window);
  glfwTerminate();
  return 0;
}
