#include "renderer/Renderer.hpp"
#include "renderer/Shaders.hpp"
#include "renderer/TestGeometry.hpp"

#include <GL/glew.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace gfxlab {

using namespace shaders;


[[noreturn]] void failRenderer(const std::string& message) {
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
    failRenderer("shader compilation failed:\n" + log);
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
    failRenderer("program link failed:\n" + log);
  }
  return program;
}



struct RenderTarget {
  GLuint sceneFbo = 0;
  GLuint sceneTexture = 0;
  GLuint depthTexture = 0;
  GLuint multisampleFbo = 0;
  GLuint multisampleColor = 0;
  GLuint multisampleDepth = 0;
  GLuint outputFbo = 0;
  GLuint outputTexture = 0;
  GLuint overdrawFbo = 0;
  GLuint overdrawTexture = 0;
  int width = 0;
  int height = 0;
  int depthPrecision = 0;
  int samples = 0;
  bool packedStencil = false;

  void resize(int newWidth, int newHeight, int newDepthPrecision, int newSamples, bool needsStencil) {
    if (width == newWidth && height == newHeight && depthPrecision == newDepthPrecision && samples == newSamples && packedStencil == needsStencil) return;
    width = newWidth;
    height = newHeight;
    depthPrecision = newDepthPrecision;
    samples = newSamples;
    packedStencil = needsStencil;
    if (!sceneFbo) {
      glGenFramebuffers(1, &sceneFbo);
      glGenTextures(1, &sceneTexture);
      glGenTextures(1, &depthTexture);
      glGenFramebuffers(1, &multisampleFbo);
      glGenRenderbuffers(1, &multisampleColor);
      glGenRenderbuffers(1, &multisampleDepth);
      glGenFramebuffers(1, &outputFbo);
      glGenTextures(1, &outputTexture);
      glGenFramebuffers(1, &overdrawFbo);
      glGenTextures(1, &overdrawTexture);
    }

    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, depthTexture);
    const GLint depthFormat = packedStencil ? GL_DEPTH24_STENCIL8 : depthPrecision == 16 ? GL_DEPTH_COMPONENT16 : GL_DEPTH_COMPONENT24;
    const GLenum depthType = packedStencil ? GL_UNSIGNED_INT_24_8 : depthPrecision == 16 ? GL_UNSIGNED_SHORT : GL_UNSIGNED_INT;
    const GLenum depthExternalFormat = packedStencil ? GL_DEPTH_STENCIL : GL_DEPTH_COMPONENT;
    glTexImage2D(GL_TEXTURE_2D, 0, depthFormat, width, height, 0, depthExternalFormat, depthType, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, sceneFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTexture, 0);
    // GL_DEPTH_STENCIL_ATTACHMENT aliases both attachment points. Detach each
    // point explicitly before switching between depth-only and packed formats;
    // otherwise the previous stencil half survives and makes the FBO incomplete.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, packedStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
      GL_TEXTURE_2D, depthTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      failRenderer("could not create resolved scene render target");

    if (samples > 1) {
      glBindRenderbuffer(GL_RENDERBUFFER, multisampleColor);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
      glBindRenderbuffer(GL_RENDERBUFFER, multisampleDepth);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, depthFormat, width, height);
      glBindFramebuffer(GL_FRAMEBUFFER, multisampleFbo);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, multisampleColor);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_RENDERBUFFER, 0);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, packedStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
        GL_RENDERBUFFER, multisampleDepth);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        failRenderer("could not create multisampled render target");
    }

    glBindTexture(GL_TEXTURE_2D, outputTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, outputFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, outputTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      failRenderer("could not create output render target");

    glBindTexture(GL_TEXTURE_2D, overdrawTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindFramebuffer(GL_FRAMEBUFFER, overdrawFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, overdrawTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      failRenderer("could not create overdraw analysis target");
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
    glDeleteFramebuffers(1, &overdrawFbo);
    glDeleteTextures(1, &overdrawTexture);
  }
};

class Renderer::Impl {
public:
  Impl() {
    sceneProgram_ = makeProgram(sceneVertexShader, sceneFragmentShader);
    outputProgram_ = makeProgram(outputVertexShader, outputFragmentShader);
    differenceProgram_ = makeProgram(outputVertexShader, differenceFragmentShader);
    shadowProgram_ = makeProgram(shadowVertexShader, shadowFragmentShader);
    overdrawProgram_ = makeProgram(overdrawVertexShader, overdrawFragmentShader);
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
    glEnableVertexAttribArray(5);
    glVertexAttribPointer(5, 4, GL_FLOAT, GL_FALSE, sizeof(Vertex), reinterpret_cast<void*>(offsetof(Vertex, tangent)));
    glGenVertexArrays(1, &fullscreenVao_);
    glGenFramebuffers(1, &differenceFbo_);
    glGenTextures(1, &differenceTexture_);
    glBindTexture(GL_TEXTURE_2D, differenceTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, differenceWidth_, differenceHeight_, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, differenceFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, differenceTexture_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      failRenderer("could not create difference render target");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples_);
    if (GLEW_EXT_texture_filter_anisotropic)
      glGetFloatv(GL_MAX_TEXTURE_MAX_ANISOTROPY_EXT, &maxAnisotropy_);
    if (std::getenv("GRAPHICS_LAB_VALIDATE_FRAMEBUFFERS")) {
      RenderTarget validationTarget;
      validationTarget.resize(64, 64, 16, 1, false);
      validationTarget.resize(64, 64, 24, 1, true);
      validationTarget.resize(64, 64, 24, 1, false);
      if (maxSamples_ >= 2) {
        validationTarget.resize(64, 64, 24, 2, true);
        validationTarget.resize(64, 64, 16, 2, false);
      }
      validationTarget.destroy();
    }
    makeCheckerTexture();
    makeNormalTexture();
    makeDetailTexture();
    glGenFramebuffers(1, &shadowFbo_);
    glGenTextures(1, &shadowTexture_);
  }

  ~Impl() {
    targetA_.destroy();
    targetB_.destroy();
    glDeleteTextures(1, &checkerTexture_);
    glDeleteTextures(1, &indexedTexture_);
    glDeleteTextures(1, &clutTexture_);
    glDeleteTextures(1, &normalTexture_);
    glDeleteTextures(1, &detailTexture_);
    glDeleteFramebuffers(1, &shadowFbo_);
    glDeleteTextures(1, &shadowTexture_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteVertexArrays(1, &fullscreenVao_);
    glDeleteFramebuffers(1, &differenceFbo_);
    glDeleteTextures(1, &differenceTexture_);
    glDeleteProgram(sceneProgram_);
    glDeleteProgram(outputProgram_);
    glDeleteProgram(differenceProgram_);
    glDeleteProgram(shadowProgram_);
    glDeleteProgram(overdrawProgram_);
  }

  GLuint render(const RendererState& state, const CameraOrbit& camera, TestScene scene, bool referenceTarget) {
    RenderTarget& target = referenceTarget ? targetB_ : targetA_;
    const float azimuth = glm::radians(state.lighting.azimuth);
    const float elevation = glm::radians(state.lighting.elevation);
    const glm::vec3 lightDirection(std::cos(elevation) * std::cos(azimuth), std::sin(elevation),
      std::cos(elevation) * std::sin(azimuth));
    const glm::vec3 lightUp = std::abs(lightDirection.y) > 0.98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 lightView = glm::lookAt(lightDirection * 9.0f, glm::vec3(0), lightUp);
    const glm::mat4 lightProjection = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 20.0f);
    const glm::mat4 lightSpace = lightProjection * lightView;
    const bool shadowsEnabled = state.lighting.shadows && scene == TestScene::Lighting;

    if (shadowsEnabled) {
      resizeShadowMap(state.lighting.shadowResolution);
      glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
      glViewport(0, 0, shadowResolution_, shadowResolution_);
      glEnable(GL_DEPTH_TEST);
      glDepthMask(GL_TRUE);
      glDepthFunc(GL_LESS);
      glDisable(GL_BLEND);
      glEnable(GL_CULL_FACE);
      glCullFace(GL_FRONT);
      glClearDepth(1.0);
      glClear(GL_DEPTH_BUFFER_BIT);
      glUseProgram(shadowProgram_);
      glUniformMatrix4fv(glGetUniformLocation(shadowProgram_, "uLightSpace"), 1, GL_FALSE, glm::value_ptr(lightSpace));
      glUniform1f(glGetUniformLocation(shadowProgram_, "uQuantization"), state.geometry.vertexQuantization);
      glBindVertexArray(vao_);
      auto drawShadow = [this](const MeshRange& mesh, const glm::mat4& modelMatrix) {
        glUniformMatrix4fv(glGetUniformLocation(shadowProgram_, "uModel"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
        glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
      };
      const glm::mat4 identity(1.0f);
      drawShadow(lowSphere_, glm::translate(identity, glm::vec3(-1.35f, 0, 0)));
      drawShadow(smoothSphere_, glm::translate(identity, glm::vec3(1.35f, 0, 0)));
      drawShadow(torus_, glm::translate(glm::scale(identity, glm::vec3(0.62f)), glm::vec3(0, 1.9f, 0)));
      drawShadow(plane_, glm::translate(glm::scale(identity, glm::vec3(0.75f)), glm::vec3(0, -1.55f, -0.1f)));
    }

    const int samples = state.rasterization.samples == 1 ? 1 : std::min(state.rasterization.samples, maxSamples_);
    const bool needsStencil = scene == TestScene::StencilMask && state.stencil.enabled;
    target.resize(state.output.width, state.output.height, state.depth.precision, samples, needsStencil);
    glBindFramebuffer(GL_FRAMEBUFFER, samples > 1 ? target.multisampleFbo : target.sceneFbo);
    glViewport(0, 0, target.width, target.height);
    glDepthMask(GL_TRUE);
    const GLenum depthFunctions[] = {GL_LESS, GL_LEQUAL, GL_GREATER, GL_ALWAYS};
    glDepthFunc(depthFunctions[std::clamp(state.depth.function, 0, 3)]);
    glClearDepth(state.depth.function == 2 ? 0.0 : 1.0);
    glStencilMask(0xff);
    glClearStencil(0);
    glDisable(GL_STENCIL_TEST);
    const bool orderingTableActive = state.depth.orderingTable && scene == TestScene::Transparency;
    if (state.depth.testing && !orderingTableActive) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
    if (state.rasterization.cullMode == 0) {
      glDisable(GL_CULL_FACE);
    } else {
      glEnable(GL_CULL_FACE);
      glCullFace(state.rasterization.cullMode == 1 ? GL_BACK : GL_FRONT);
    }
    if (state.rasterization.polygonOffset) {
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(state.rasterization.polygonOffsetFactor, state.rasterization.polygonOffsetUnits);
    } else {
      glDisable(GL_POLYGON_OFFSET_FILL);
    }
    if (state.surface.transparency >= 2) {
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      if (state.surface.transparency == 2) glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
      if (state.surface.transparency == 3) glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
      if (state.surface.transparency == 4) glBlendFunc(GL_SRC_ALPHA, GL_ONE);
      if (state.surface.transparency == 5) glBlendFunc(GL_DST_COLOR, GL_ZERO);
      if (state.surface.transparency == 6) {
        glBlendColor(0.0f, 0.0f, 0.0f, 0.5f);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_CONSTANT_ALPHA);
      }
      if (state.surface.transparency == 7) glBlendFunc(GL_ONE, GL_ONE);
      if (state.surface.transparency == 8) {
        glBlendEquation(GL_FUNC_REVERSE_SUBTRACT);
        glBlendFunc(GL_ONE, GL_ONE);
      }
      if (state.surface.transparency == 9) {
        glBlendColor(0.0f, 0.0f, 0.0f, 0.25f);
        glBlendFunc(GL_CONSTANT_ALPHA, GL_ONE);
      }
    } else {
      glDisable(GL_BLEND);
    }
    glEnable(GL_CLIP_DISTANCE0);
    const float backgroundR = state.color.linearLight ? std::pow(0.105f, 2.2f) : 0.105f;
    const float backgroundG = state.color.linearLight ? std::pow(0.112f, 2.2f) : 0.112f;
    const float backgroundB = state.color.linearLight ? std::pow(0.120f, 2.2f) : 0.120f;
    glClearColor(backgroundR, backgroundG, backgroundB, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glDepthMask(state.depth.writing && !orderingTableActive ? GL_TRUE : GL_FALSE);

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
    matrix("uLightSpace", lightSpace);
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
    glUniform1i(location("uPremultiplyAlpha"), state.surface.transparency == 3);
    glUniform1i(location("uNormalMapping"), state.surface.normalMapping);
    glUniform1f(location("uNormalStrength"), state.surface.normalStrength);
    glUniform1i(location("uLightingModel"), state.lighting.model);
    glUniform1f(location("uAmbient"), state.lighting.ambient);
    glUniform1i(location("uDepthCueEnabled"), state.lighting.depthCue);
    glUniform1f(location("uDepthCueStart"), state.lighting.depthCueStart);
    glUniform1f(location("uDepthCueEnd"), state.lighting.depthCueEnd);
    glUniform3fv(location("uFarColor"), 1, glm::value_ptr(state.lighting.farColor));
    glUniform1i(location("uN64TextureGeneration"), state.n64.enabled && state.n64.textureGeneration);
    glUniform1f(location("uShininess"), state.lighting.shininess);
    glUniform1i(location("uLinearLight"), state.color.linearLight);
    glUniform3fv(location("uLightDirection"), 1, glm::value_ptr(lightDirection));
    glUniform1i(location("uShadowsEnabled"), shadowsEnabled);
    glUniform1f(location("uShadowBias"), state.lighting.shadowBias);
    glUniform1i(location("uShadowPcf"), state.lighting.shadowPcf);
    glUniform1i(location("uFogEnabled"), state.post.fog);
    glUniform1f(location("uFogStart"), state.post.fogStart);
    glUniform1f(location("uFogEnd"), state.post.fogEnd);
    glUniform1i(location("uN64Enabled"), state.n64.enabled);
    glUniform1i(location("uN64CycleType"), state.n64.cycleType);
    glUniform4i(location("uN64Cycle0"), state.n64.cycle0.a, state.n64.cycle0.b, state.n64.cycle0.c, state.n64.cycle0.d);
    glUniform4i(location("uN64Cycle1"), state.n64.cycle1.a, state.n64.cycle1.b, state.n64.cycle1.c, state.n64.cycle1.d);
    glUniform4fv(location("uN64PrimitiveColor"), 1, glm::value_ptr(state.n64.primitiveColor));
    glUniform4fv(location("uN64EnvironmentColor"), 1, glm::value_ptr(state.n64.environmentColor));
    glUniform1i(location("uN64TextureFormat"), state.n64.textureFormat);
    glUniform1i(location("uN64TextureFilter"), state.n64.textureFilter);
    glUniform1i(location("uN64MipmapMode"), state.n64.mipmapMode);
    glUniform2i(location("uN64TileSize"), state.n64.tileWidth, state.n64.tileHeight);
    glUniform2i(location("uN64Mirror"), state.n64.mirrorS, state.n64.mirrorT);
    glUniform2i(location("uN64Shift"), state.n64.shiftS, state.n64.shiftT);
    glUniform1i(location("uN64AlphaCompare"), state.n64.alphaCompare);
    glUniform1f(location("uN64AlphaThreshold"), state.n64.alphaThreshold);
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
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, indexedTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glUniform1i(location("uIndexedTexture"), 4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, clutTexture_);
    glUniform1i(location("uClut"), 5);
    glUniform1i(location("uTextureColorMode"), state.texture.colorMode);
    glActiveTexture(GL_TEXTURE6);
    glBindTexture(GL_TEXTURE_2D, detailTexture_);
    glUniform1i(location("uN64DetailTexture"), 6);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, normalTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, state.texture.mipmapping ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR);
    glUniform1i(location("uNormalMap"), 1);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glUniform1i(location("uShadowMap"), 2);
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
      case TestScene::Transparency: {
        const std::array<glm::mat4, 3> transforms = {
          glm::translate(identity, glm::vec3(-0.18f, 0.0f, -0.65f)) * glm::rotate(identity, glm::radians(28.0f), glm::vec3(0, 1, 0)),
          glm::translate(identity, glm::vec3(0.18f, 0.0f, 0.10f)) * glm::rotate(identity, glm::radians(-35.0f), glm::vec3(0, 1, 0)),
          glm::translate(identity, glm::vec3(0.0f, -0.10f, 0.75f)) * glm::rotate(identity, glm::radians(90.0f), glm::vec3(1, 0, 0))};
        const std::array<glm::vec3, 3> tints = {glm::vec3(0.42f, 0.8f, 1.0f), glm::vec3(1.0f, 0.48f, 0.35f), glm::vec3(0.55f, 1.0f, 0.56f)};
        std::array<int, 3> drawOrder = {0, 1, 2};
        if (orderingTableActive) {
          const int bucketCount = std::clamp(state.depth.orderingBuckets, 4, 256);
          auto bucket = [&](int objectIndex) {
            const glm::vec4 viewCenter = view * transforms[objectIndex] * glm::vec4(0, 0, 0, 1);
            const float normalizedDepth = glm::clamp(-viewCenter.z / 16.0f, 0.0f, 1.0f);
            return static_cast<int>(normalizedDepth * static_cast<float>(bucketCount - 1));
          };
          std::stable_sort(drawOrder.begin(), drawOrder.end(), [&](int a, int b) { return bucket(a) > bucket(b); });
        }
        if (state.surface.reverseDrawOrder) std::reverse(drawOrder.begin(), drawOrder.end());
        for (int drawIndex = 0; drawIndex < 3; ++drawIndex) {
          const int objectIndex = drawOrder[drawIndex];
          drawMesh(quad_, transforms[objectIndex], tints[objectIndex]);
        }
        break;
      }
      case TestScene::Lighting:
        drawMesh(plane_, glm::translate(glm::scale(identity, glm::vec3(0.75f)), glm::vec3(0, -1.55f, -0.1f)), glm::vec3(0.45f));
        drawMesh(lowSphere_, glm::translate(identity, glm::vec3(-1.35f, 0, 0)), glm::vec3(0.9f, 0.55f, 0.38f));
        drawMesh(smoothSphere_, glm::translate(identity, glm::vec3(1.35f, 0, 0)), glm::vec3(0.45f, 0.68f, 1.0f));
        drawMesh(torus_, glm::translate(glm::scale(identity, glm::vec3(0.62f)), glm::vec3(0, 1.9f, 0)), glm::vec3(0.7f, 1.0f, 0.6f));
        break;
      case TestScene::StencilMask:
        if (state.stencil.enabled) {
          glEnable(GL_STENCIL_TEST);
          glStencilMask(0xff);
          glStencilFunc(GL_ALWAYS, state.stencil.reference, 0xff);
          glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
          glColorMask(GL_FALSE, GL_FALSE, GL_FALSE, GL_FALSE);
          glDepthMask(GL_FALSE);
          drawMesh(lowSphere_, glm::scale(identity, glm::vec3(1.35f)), glm::vec3(1.0f));
          glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
          glDepthMask(state.depth.writing ? GL_TRUE : GL_FALSE);
          glStencilMask(0x00);
          glStencilFunc(state.stencil.invert ? GL_NOTEQUAL : GL_EQUAL, state.stencil.reference, 0xff);
          glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
        }
        drawMesh(quad_, glm::translate(glm::scale(identity, glm::vec3(2.0f)), glm::vec3(0, 0, -0.35f)),
          glm::vec3(0.45f, 0.8f, 1.0f));
        glStencilMask(0xff);
        glDisable(GL_STENCIL_TEST);
        break;
    }
    glDepthMask(GL_TRUE);
    glDisable(GL_BLEND);
    glBlendEquation(GL_FUNC_ADD);
    glDisable(GL_POLYGON_OFFSET_FILL);
    glDisable(GL_CLIP_DISTANCE0);

    if (samples > 1) {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, target.multisampleFbo);
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.sceneFbo);
      const GLbitfield resolveBuffers = GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT |
        (needsStencil ? GL_STENCIL_BUFFER_BIT : 0);
      glBlitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height,
        resolveBuffers, GL_NEAREST);
    }

    if (state.post.overdraw) {
      glBindFramebuffer(GL_FRAMEBUFFER, target.overdrawFbo);
      glViewport(0, 0, target.width, target.height);
      glDisable(GL_DEPTH_TEST);
      glEnable(GL_BLEND);
      glBlendEquation(GL_FUNC_ADD);
      glBlendFunc(GL_ONE, GL_ONE);
      if (state.rasterization.cullMode == 0) glDisable(GL_CULL_FACE); else {
        glEnable(GL_CULL_FACE);
        glCullFace(state.rasterization.cullMode == 1 ? GL_BACK : GL_FRONT);
      }
      glClearColor(0, 0, 0, 0);
      glClear(GL_COLOR_BUFFER_BIT);
      glUseProgram(overdrawProgram_);
      glUniformMatrix4fv(glGetUniformLocation(overdrawProgram_, "uView"), 1, GL_FALSE, glm::value_ptr(view));
      glUniformMatrix4fv(glGetUniformLocation(overdrawProgram_, "uProjection"), 1, GL_FALSE, glm::value_ptr(projection));
      glUniform1f(glGetUniformLocation(overdrawProgram_, "uQuantization"), state.geometry.vertexQuantization);
      glBindVertexArray(vao_);
      auto countMesh = [this](const MeshRange& mesh, const glm::mat4& modelMatrix) {
        glUniformMatrix4fv(glGetUniformLocation(overdrawProgram_, "uModel"), 1, GL_FALSE, glm::value_ptr(modelMatrix));
        glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
      };
      const glm::mat4 identity(1.0f);
      switch (scene) {
        case TestScene::Torus:
          countMesh(torus_, glm::rotate(identity, glm::radians(-14.0f), glm::vec3(1, 0, 0)));
          break;
        case TestScene::TexturePlane:
          countMesh(plane_, glm::translate(identity, glm::vec3(0, -1.25f, -3.5f)));
          break;
        case TestScene::DepthPrecision: {
          const glm::mat4 horizontal = glm::rotate(identity, glm::radians(-90.0f), glm::vec3(1, 0, 0));
          countMesh(quad_, glm::scale(horizontal, glm::vec3(2.2f)));
          countMesh(quad_, glm::translate(horizontal, glm::vec3(0, 0, 0.00015f)) * glm::scale(identity, glm::vec3(1.65f)));
          countMesh(quad_, glm::translate(horizontal, glm::vec3(0, 0, 0.00030f)) * glm::scale(identity, glm::vec3(1.05f)));
          break;
        }
        case TestScene::Transparency:
          countMesh(quad_, glm::rotate(identity, glm::radians(28.0f), glm::vec3(0, 1, 0)));
          countMesh(quad_, glm::rotate(identity, glm::radians(-35.0f), glm::vec3(0, 1, 0)));
          countMesh(quad_, glm::rotate(identity, glm::radians(90.0f), glm::vec3(1, 0, 0)));
          break;
        case TestScene::Lighting:
          countMesh(plane_, glm::translate(glm::scale(identity, glm::vec3(0.75f)), glm::vec3(0, -1.55f, -0.1f)));
          countMesh(lowSphere_, glm::translate(identity, glm::vec3(-1.35f, 0, 0)));
          countMesh(smoothSphere_, glm::translate(identity, glm::vec3(1.35f, 0, 0)));
          countMesh(torus_, glm::translate(glm::scale(identity, glm::vec3(0.62f)), glm::vec3(0, 1.9f, 0)));
          break;
        case TestScene::StencilMask:
          if (state.stencil.enabled) countMesh(lowSphere_, glm::scale(identity, glm::vec3(1.35f)));
          countMesh(quad_, glm::translate(glm::scale(identity, glm::vec3(2.0f)), glm::vec3(0, 0, -0.35f)));
          break;
      }
      glDisable(GL_BLEND);
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
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glUniform1i(glGetUniformLocation(outputProgram_, "uShadowMap"), 2);
    glUniform1i(glGetUniformLocation(outputProgram_, "uVisualizeShadowMap"),
      shadowsEnabled && state.lighting.visualizeShadowMap);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, target.overdrawTexture);
    glUniform1i(glGetUniformLocation(outputProgram_, "uOverdraw"), 3);
    glUniform1i(glGetUniformLocation(outputProgram_, "uVisualizeOverdraw"), state.post.overdraw);
    glUniform1f(glGetUniformLocation(outputProgram_, "uOverdrawRange"), state.post.overdrawRange);
    glUniform1i(glGetUniformLocation(outputProgram_, "uN64Enabled"), state.n64.enabled);
    glUniform1i(glGetUniformLocation(outputProgram_, "uN64ColorDither"), state.n64.colorDither);
    glUniform1i(glGetUniformLocation(outputProgram_, "uN64ViReconstruction"), state.n64.viReconstruction);
    glUniform1i(glGetUniformLocation(outputProgram_, "uN64ViDivot"), state.n64.viDivot);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindTexture(GL_TEXTURE_2D, target.outputTexture);
    const GLint upscaleFilter = state.output.nearestUpscaling ? GL_NEAREST : GL_LINEAR;
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, upscaleFilter);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, upscaleFilter);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return target.outputTexture;
  }

  GLuint renderDifference(float exposure) {
    glBindFramebuffer(GL_FRAMEBUFFER, differenceFbo_);
    glViewport(0, 0, differenceWidth_, differenceHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(differenceProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, targetA_.outputTexture);
    glUniform1i(glGetUniformLocation(differenceProgram_, "uImageA"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, targetB_.outputTexture);
    glUniform1i(glGetUniformLocation(differenceProgram_, "uImageB"), 1);
    glUniform1f(glGetUniformLocation(differenceProgram_, "uExposure"), exposure);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return differenceTexture_;
  }

private:
  GLuint sceneProgram_ = 0, outputProgram_ = 0, differenceProgram_ = 0, shadowProgram_ = 0, overdrawProgram_ = 0;
  GLuint vao_ = 0, vbo_ = 0, fullscreenVao_ = 0, checkerTexture_ = 0, indexedTexture_ = 0,
    clutTexture_ = 0, normalTexture_ = 0, detailTexture_ = 0;
  GLsizei vertexCount_ = 0;
  GLint maxSamples_ = 1;
  GLfloat maxAnisotropy_ = 1.0f;
  MeshRange torus_, plane_, quad_, lowSphere_, smoothSphere_;
  GLuint shadowFbo_ = 0, shadowTexture_ = 0;
  int shadowResolution_ = 0;
  RenderTarget targetA_, targetB_;
  GLuint differenceFbo_ = 0, differenceTexture_ = 0;
  static constexpr int differenceWidth_ = 960;
  static constexpr int differenceHeight_ = 720;

  GLint location(const char* name) const { return glGetUniformLocation(sceneProgram_, name); }
  void matrix(const char* name, const glm::mat4& value) { glUniformMatrix4fv(location(name), 1, GL_FALSE, glm::value_ptr(value)); }

  void makeCheckerTexture() {
    constexpr int size = 64;
    std::array<unsigned char, size * size * 4> pixels{};
    std::array<unsigned char, size * size> indices{};
    std::array<unsigned char, 256 * 4> palette{};
    constexpr std::array<glm::u8vec3, 16> baseColors = {
      glm::u8vec3(28, 34, 39), glm::u8vec3(205, 188, 146), glm::u8vec3(53, 68, 72), glm::u8vec3(165, 76, 65),
      glm::u8vec3(71, 120, 145), glm::u8vec3(98, 145, 88), glm::u8vec3(157, 112, 61), glm::u8vec3(128, 89, 142),
      glm::u8vec3(214, 215, 203), glm::u8vec3(99, 105, 107), glm::u8vec3(225, 141, 121), glm::u8vec3(116, 169, 190),
      glm::u8vec3(151, 190, 125), glm::u8vec3(215, 176, 101), glm::u8vec3(180, 141, 190), glm::u8vec3(66, 49, 46)};
    for (int index = 0; index < 256; ++index) {
      const glm::ivec3 base(baseColors[index & 15]);
      const int variation = ((index >> 4) - 7) * 3;
      const glm::u8vec3 color(glm::clamp(base + variation, glm::ivec3(0), glm::ivec3(255)));
      palette[index * 4] = color.r;
      palette[index * 4 + 1] = color.g;
      palette[index * 4 + 2] = color.b;
      palette[index * 4 + 3] = (index & 1) ? 255 : 96;
    }
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const int low = ((x / 8) + (y / 8) * 3) & 15;
        const int high = ((x & 7) + (y & 7)) & 15;
        const unsigned char paletteIndex = static_cast<unsigned char>((high << 4) | low);
        indices[y * size + x] = paletteIndex;
        const int offset = (y * size + x) * 4;
        pixels[offset] = palette[paletteIndex * 4];
        pixels[offset + 1] = palette[paletteIndex * 4 + 1];
        pixels[offset + 2] = palette[paletteIndex * 4 + 2];
        pixels[offset + 3] = palette[paletteIndex * 4 + 3];
      }
    }
    glGenTextures(1, &checkerTexture_);
    glBindTexture(GL_TEXTURE_2D, checkerTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);

    glGenTextures(1, &indexedTexture_);
    glBindTexture(GL_TEXTURE_2D, indexedTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R8, size, size, 0, GL_RED, GL_UNSIGNED_BYTE, indices.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);

    glGenTextures(1, &clutTexture_);
    glBindTexture(GL_TEXTURE_2D, clutTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 256, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, palette.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  }

  void makeNormalTexture() {
    constexpr int size = 64;
    std::array<unsigned char, size * size * 3> pixels{};
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const float u = static_cast<float>(x) / size * glm::two_pi<float>() * 4.0f;
        const float v = static_cast<float>(y) / size * glm::two_pi<float>() * 4.0f;
        glm::vec3 normal(0.42f * std::sin(u), 0.42f * std::cos(v), 1.0f);
        normal = glm::normalize(normal) * 0.5f + 0.5f;
        const int offset = (y * size + x) * 3;
        pixels[offset] = static_cast<unsigned char>(normal.x * 255.0f);
        pixels[offset + 1] = static_cast<unsigned char>(normal.y * 255.0f);
        pixels[offset + 2] = static_cast<unsigned char>(normal.z * 255.0f);
      }
    }
    glGenTextures(1, &normalTexture_);
    glBindTexture(GL_TEXTURE_2D, normalTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB8, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  void makeDetailTexture() {
    constexpr int size = 64;
    std::array<unsigned char, size * size * 4> pixels{};
    for (int y = 0; y < size; ++y) {
      for (int x = 0; x < size; ++x) {
        const int checker = ((x / 2) + (y / 2)) & 1;
        const unsigned char intensity = static_cast<unsigned char>(checker ? 176 : 80);
        const int offset = (y * size + x) * 4;
        pixels[offset] = intensity;
        pixels[offset + 1] = intensity;
        pixels[offset + 2] = intensity;
        pixels[offset + 3] = 255;
      }
    }
    glGenTextures(1, &detailTexture_);
    glBindTexture(GL_TEXTURE_2D, detailTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, size, size, 0, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
  }

  void resizeShadowMap(int resolution) {
    resolution = std::clamp(resolution, 128, 4096);
    if (shadowResolution_ == resolution) return;
    shadowResolution_ = resolution;
    glBindTexture(GL_TEXTURE_2D, shadowTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, resolution, resolution, 0,
      GL_DEPTH_COMPONENT, GL_UNSIGNED_INT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    const float border[] = {1, 1, 1, 1};
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, border);
    glBindFramebuffer(GL_FRAMEBUFFER, shadowFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, shadowTexture_, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      failRenderer("could not create shadow-map framebuffer");
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
};

Renderer::Renderer() : impl_(std::make_unique<Impl>()) {}
Renderer::~Renderer() = default;

unsigned int Renderer::render(const RendererState& state, const CameraOrbit& camera, TestScene scene,
    bool referenceTarget) {
  return impl_->render(state, camera, scene, referenceTarget);
}

unsigned int Renderer::renderDifference(float exposure) { return impl_->renderDifference(exposure); }

} // namespace gfxlab
