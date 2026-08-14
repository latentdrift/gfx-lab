#include "renderer/Renderer.hpp"
#include "app/RenderOperationState.hpp"
#include "renderer/Shaders.hpp"
#include "renderer/TestGeometry.hpp"
#include "simulation/ElementalSimulation.hpp"
#include "assets/ModelAsset.hpp"

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
#include <iterator>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
  GLuint normalTexture = 0;
  GLuint depthTexture = 0;
  GLuint multisampleFbo = 0;
  GLuint multisampleColor = 0;
  GLuint multisampleNormal = 0;
  GLuint multisampleDepth = 0;
  GLuint outputFbo = 0;
  GLuint outputTexture = 0;
  GLuint fieldFbo = 0;
  GLuint fieldTexture = 0;
  GLuint overdrawFbo = 0;
  GLuint overdrawTexture = 0;
  GLuint spectralFbo = 0;
  std::array<GLuint, 4> spectralTextures{};
  int width = 0;
  int height = 0;
  int depthPrecision = 0;
  int samples = 0;
  bool packedStencil = false;
  glm::mat4 viewProjection{1.0f};
  glm::mat4 inverseViewProjection{1.0f};

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
      glGenTextures(1, &normalTexture);
      glGenTextures(1, &depthTexture);
      glGenFramebuffers(1, &multisampleFbo);
      glGenRenderbuffers(1, &multisampleColor);
      glGenRenderbuffers(1, &multisampleNormal);
      glGenRenderbuffers(1, &multisampleDepth);
      glGenFramebuffers(1, &outputFbo);
      glGenTextures(1, &outputTexture);
      glGenFramebuffers(1, &fieldFbo);
      glGenTextures(1, &fieldTexture);
      glGenFramebuffers(1, &overdrawFbo);
      glGenTextures(1, &overdrawTexture);
      glGenFramebuffers(1, &spectralFbo);
      glGenTextures(static_cast<GLsizei>(spectralTextures.size()), spectralTextures.data());
    }

    glBindTexture(GL_TEXTURE_2D, sceneTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, width, height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, normalTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
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
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D, normalTexture, 0);
    constexpr std::array<GLenum, 2> sceneDrawBuffers = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(static_cast<GLsizei>(sceneDrawBuffers.size()), sceneDrawBuffers.data());
    // GL_DEPTH_STENCIL_ATTACHMENT aliases both attachment points. Detach each
    // point explicitly before switching between depth-only and packed formats;
    // otherwise the previous stencil half survives and makes the FBO incomplete.
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, packedStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
      GL_TEXTURE_2D, depthTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      failRenderer("could not create resolved scene render target");

    glBindFramebuffer(GL_FRAMEBUFFER, spectralFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, sceneTexture, 0);
    for (std::size_t i = 0; i < spectralTextures.size(); ++i) {
      glBindTexture(GL_TEXTURE_2D, spectralTextures[i]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, width, height, 0, GL_RGBA, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1 + static_cast<GLenum>(i),
        GL_TEXTURE_2D, spectralTextures[i], 0);
    }
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_STENCIL_ATTACHMENT, GL_TEXTURE_2D, 0, 0);
    glFramebufferTexture2D(GL_FRAMEBUFFER, packedStencil ? GL_DEPTH_STENCIL_ATTACHMENT : GL_DEPTH_ATTACHMENT,
      GL_TEXTURE_2D, depthTexture, 0);
    constexpr std::array<GLenum, 5> spectralDrawBuffers = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
      GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4};
    glDrawBuffers(static_cast<GLsizei>(spectralDrawBuffers.size()), spectralDrawBuffers.data());
    const GLenum spectralStatus = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (spectralStatus != GL_FRAMEBUFFER_COMPLETE)
      failRenderer("could not create spectral render target (status " + std::to_string(spectralStatus) + ")");

    if (samples > 1) {
      glBindRenderbuffer(GL_RENDERBUFFER, multisampleColor);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA8, width, height);
      glBindRenderbuffer(GL_RENDERBUFFER, multisampleNormal);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, GL_RGBA16F, width, height);
      glBindRenderbuffer(GL_RENDERBUFFER, multisampleDepth);
      glRenderbufferStorageMultisample(GL_RENDERBUFFER, samples, depthFormat, width, height);
      glBindFramebuffer(GL_FRAMEBUFFER, multisampleFbo);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, multisampleColor);
      glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_RENDERBUFFER, multisampleNormal);
      glDrawBuffers(static_cast<GLsizei>(sceneDrawBuffers.size()), sceneDrawBuffers.data());
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

    glBindTexture(GL_TEXTURE_2D, fieldTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R16F, width, height, 0, GL_RED, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, fieldFbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, fieldTexture, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      failRenderer("could not create field-signal render target");

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
    glDeleteTextures(1, &normalTexture);
    glDeleteTextures(1, &depthTexture);
    glDeleteFramebuffers(1, &multisampleFbo);
    glDeleteRenderbuffers(1, &multisampleColor);
    glDeleteRenderbuffers(1, &multisampleNormal);
    glDeleteRenderbuffers(1, &multisampleDepth);
    glDeleteFramebuffers(1, &outputFbo);
    glDeleteTextures(1, &outputTexture);
    glDeleteFramebuffers(1, &fieldFbo);
    glDeleteTextures(1, &fieldTexture);
    glDeleteFramebuffers(1, &overdrawFbo);
    glDeleteTextures(1, &overdrawTexture);
    glDeleteFramebuffers(1, &spectralFbo);
    glDeleteTextures(static_cast<GLsizei>(spectralTextures.size()), spectralTextures.data());
  }
};

class Renderer::Impl {
public:
  Impl() {
    sceneProgram_ = makeProgram(sceneVertexShader, sceneFragmentShader);
    outputProgram_ = makeProgram(outputVertexShader, outputFragmentShader);
    relationProgram_ = makeProgram(outputVertexShader, relationFragmentShader);
    imageOperationProgram_ = makeProgram(outputVertexShader, imageOperationFragmentShader);
    stereoAnalysisProgram_ = makeProgram(outputVertexShader, stereoAnalysisFragmentShader);
    fieldProgram_ = makeProgram(outputVertexShader, fieldFragmentShader);
    sdfIsoProgram_ = makeProgram(outputVertexShader, sdfIsoSurfaceFragmentShader);
    spectralProgram_ = makeProgram(spectralVertexShader, spectralFragmentShader);
    copyProgram_ = makeProgram(outputVertexShader, copyFragmentShader);
    previewProgram_ = makeProgram(outputVertexShader, signalPreviewFragmentShader);
    displayProgram_ = makeProgram(outputVertexShader, displayReconstructionFragmentShader);
    shadowProgram_ = makeProgram(shadowVertexShader, shadowFragmentShader);
    overdrawProgram_ = makeProgram(overdrawVertexShader, overdrawFragmentShader);
    auto appendMesh = [this](const std::vector<Vertex>& mesh) {
      const MeshRange range{static_cast<GLint>(baseVertices_.size()), static_cast<GLsizei>(mesh.size())};
      baseVertices_.insert(baseVertices_.end(), mesh.begin(), mesh.end());
      return range;
    };
    torus_ = appendMesh(makeTorus());
    denseTorus_ = appendMesh(makeTorus(64, 32));
    plane_ = appendMesh(makePlane(7.0f, 16.0f, 8, 24));
    quad_ = appendMesh(makeQuad());
    lowSphere_ = appendMesh(makeSphere(12, 6));
    smoothSphere_ = appendMesh(makeSphere(32, 16));
    glGenVertexArrays(1, &vao_);
    glGenBuffers(1, &vbo_);
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    uploadGeometry(nullptr);
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
    glGenTextures(1, &simulationMatterTexture_);
    glGenTextures(1, &simulationDynamicsTexture_);
    const auto initializeSimulationTexture = [](const GLuint texture) {
      glBindTexture(GL_TEXTURE_2D, texture);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, ElementalSimulation::width,
        ElementalSimulation::height, 0, GL_RGBA, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    };
    initializeSimulationTexture(simulationMatterTexture_);
    initializeSimulationTexture(simulationDynamicsTexture_);
    ensureGraphTargets(2);
    glGenFramebuffers(1, &historyFbo_);
    glGenTextures(1, &historyTexture_);
    glBindTexture(GL_TEXTURE_2D, historyTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, relationWidth_, relationHeight_, 0, GL_RGBA,
      GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glBindFramebuffer(GL_FRAMEBUFFER, historyFbo_);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, historyTexture_, 0);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
      failRenderer("could not create frame-history target");
    resetFrameHistory();
    glGenFramebuffers(static_cast<GLsizei>(displayFbos_.size()), displayFbos_.data());
    glGenTextures(static_cast<GLsizei>(displayTextures_.size()), displayTextures_.data());
    for (std::size_t index = 0; index < displayTextures_.size(); ++index) {
      glBindTexture(GL_TEXTURE_2D, displayTextures_[index]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, relationWidth_, relationHeight_, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindFramebuffer(GL_FRAMEBUFFER, displayFbos_[index]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, displayTextures_[index], 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        failRenderer("could not create display-reconstruction target");
    }
    glGenFramebuffers(static_cast<GLsizei>(previewFbos_.size()), previewFbos_.data());
    glGenTextures(static_cast<GLsizei>(previewTextures_.size()), previewTextures_.data());
    for (std::size_t index = 0; index < previewTextures_.size(); ++index) {
      glBindTexture(GL_TEXTURE_2D, previewTextures_[index]);
      glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, relationWidth_, relationHeight_, 0, GL_RGBA,
        GL_UNSIGNED_BYTE, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindFramebuffer(GL_FRAMEBUFFER, previewFbos_[index]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        previewTextures_[index], 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        failRenderer("could not create signal-preview target");
    }
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
    constexpr std::array<unsigned char, 4> white = {255, 255, 255, 255};
    glGenTextures(1, &whiteTexture_);
    glBindTexture(GL_TEXTURE_2D, whiteTexture_);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white.data());
    glGenerateMipmap(GL_TEXTURE_2D);
    glGenFramebuffers(1, &shadowFbo_);
    glGenTextures(1, &shadowTexture_);
  }

  ~Impl() {
    clearImportedMaterialResources();
    for (const auto& entry : overrideTextures_) glDeleteTextures(1, &entry.second);
    for (RenderTarget& target : passTargets_) target.destroy();
    glDeleteTextures(1, &checkerTexture_);
    glDeleteTextures(1, &indexedTexture_);
    glDeleteTextures(1, &clutTexture_);
    glDeleteTextures(1, &normalTexture_);
    glDeleteTextures(1, &detailTexture_);
    glDeleteTextures(1, &whiteTexture_);
    glDeleteTextures(1, &simulationMatterTexture_);
    glDeleteTextures(1, &simulationDynamicsTexture_);
    glDeleteFramebuffers(1, &shadowFbo_);
    glDeleteTextures(1, &shadowTexture_);
    glDeleteBuffers(1, &vbo_);
    glDeleteVertexArrays(1, &vao_);
    glDeleteVertexArrays(1, &fullscreenVao_);
    glDeleteFramebuffers(static_cast<GLsizei>(relationFbos_.size()), relationFbos_.data());
    glDeleteTextures(static_cast<GLsizei>(relationTextures_.size()), relationTextures_.data());
    glDeleteFramebuffers(static_cast<GLsizei>(directionFbos_.size()), directionFbos_.data());
    glDeleteTextures(static_cast<GLsizei>(directionTextures_.size()), directionTextures_.data());
    glDeleteFramebuffers(1, &historyFbo_);
    glDeleteTextures(1, &historyTexture_);
    glDeleteFramebuffers(static_cast<GLsizei>(displayFbos_.size()), displayFbos_.data());
    glDeleteTextures(static_cast<GLsizei>(displayTextures_.size()), displayTextures_.data());
    glDeleteFramebuffers(static_cast<GLsizei>(previewFbos_.size()), previewFbos_.data());
    glDeleteTextures(static_cast<GLsizei>(previewTextures_.size()), previewTextures_.data());
    glDeleteProgram(sceneProgram_);
    glDeleteProgram(outputProgram_);
    glDeleteProgram(relationProgram_);
    glDeleteProgram(imageOperationProgram_);
    glDeleteProgram(stereoAnalysisProgram_);
    glDeleteProgram(fieldProgram_);
    glDeleteProgram(sdfIsoProgram_);
    glDeleteProgram(spectralProgram_);
    glDeleteProgram(copyProgram_);
    glDeleteProgram(previewProgram_);
    glDeleteProgram(displayProgram_);
    glDeleteProgram(shadowProgram_);
    glDeleteProgram(overdrawProgram_);
  }

  void ensureGraphTargets(const std::size_t operationCount) {
    const std::size_t requestedPassTargets = std::max<std::size_t>(operationCount, 2);
    if (passTargets_.size() < requestedPassTargets) passTargets_.resize(requestedPassTargets);
    const auto growTargets = [&](std::vector<GLuint>& framebuffers,
        std::vector<GLuint>& textures, std::vector<glm::ivec2>& extents,
        const std::size_t requested) {
      if (textures.size() >= requested) return;
      const std::size_t previous = textures.size();
      framebuffers.resize(requested, 0);
      textures.resize(requested, 0);
      extents.resize(requested, glm::ivec2(0));
      const GLsizei added = static_cast<GLsizei>(requested - previous);
      glGenFramebuffers(added, framebuffers.data() + previous);
      glGenTextures(added, textures.data() + previous);
    };
    growTargets(relationFbos_, relationTextures_, relationExtents_,
      std::max<std::size_t>(operationCount + 1, 3));
    growTargets(directionFbos_, directionTextures_, directionExtents_,
      std::max<std::size_t>(operationCount, 2));
  }

  void prepareGraphTarget(const bool direction, const std::size_t index,
      const int requestedWidth, const int requestedHeight) {
    std::vector<GLuint>& framebuffers = direction ? directionFbos_ : relationFbos_;
    std::vector<GLuint>& textures = direction ? directionTextures_ : relationTextures_;
    std::vector<glm::ivec2>& extents = direction ? directionExtents_ : relationExtents_;
    const int width = std::max(requestedWidth, 1);
    const int height = std::max(requestedHeight, 1);
    if (extents[index] != glm::ivec2(width, height)) {
      extents[index] = {width, height};
      glBindTexture(GL_TEXTURE_2D, textures[index]);
      glTexImage2D(GL_TEXTURE_2D, 0, direction ? GL_RG16F : GL_RGBA16F,
        width, height, 0, direction ? GL_RG : GL_RGBA, GL_FLOAT, nullptr);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[index]);
      glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
        textures[index], 0);
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        failRenderer(direction ? "could not create edge-direction target"
          : "could not create render-algebra target");
    } else {
      glBindFramebuffer(GL_FRAMEBUFFER, framebuffers[index]);
    }
  }

  void setImportedModel(const ModelAsset& asset) {
    clearImportedMaterialResources();
    uploadGeometry(&asset.vertices);
    importedTextureIds_.reserve(asset.textures.size());
    for (const TextureAsset& texture : asset.textures) importedTextureIds_.push_back(uploadTexture(texture));
    importedMaterials_.reserve(asset.materials.size());
    for (const MaterialAsset& material : asset.materials) {
      const GLuint texture = material.baseColorTexture >= 0 &&
          static_cast<std::size_t>(material.baseColorTexture) < importedTextureIds_.size()
        ? importedTextureIds_[static_cast<std::size_t>(material.baseColorTexture)] : 0;
      importedMaterials_.push_back({material.baseColor, texture});
    }
    importedSubmeshes_.reserve(asset.submeshes.size());
    for (const SubmeshAsset& submesh : asset.submeshes)
      importedSubmeshes_.push_back({{imported_.first + static_cast<int>(submesh.firstVertex),
        static_cast<int>(submesh.vertexCount)}, submesh.materialIndex});
  }
  void clearImportedModel() {
    clearImportedMaterialResources();
    uploadGeometry(nullptr);
  }

  void updateElementalSimulation(const float deltaSeconds, const RendererState& state,
      const TestScene scene) {
    if (scene != TestScene::ElementalChamber) return;
    elementalSimulation_.update(deltaSeconds, state.field);
    if (uploadedSimulationRevision_ == elementalSimulation_.revision()) return;
    glBindTexture(GL_TEXTURE_2D, simulationMatterTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ElementalSimulation::width,
      ElementalSimulation::height, GL_RGBA, GL_FLOAT, elementalSimulation_.matterPixels().data());
    glBindTexture(GL_TEXTURE_2D, simulationDynamicsTexture_);
    glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ElementalSimulation::width,
      ElementalSimulation::height, GL_RGBA, GL_FLOAT, elementalSimulation_.dynamicsPixels().data());
    uploadedSimulationRevision_ = elementalSimulation_.revision();
  }

  void resetElementalSimulation() {
    elementalSimulation_.reset();
    uploadedSimulationRevision_ = 0;
  }

  GLuint render(const RendererState& state, const CameraOrbit& camera, TestScene scene, const std::size_t targetIndex,
      const PassPerturbation& perturbation = {}, const PassOutput output = PassOutput::Color,
      const TextureSource textureSource = TextureSource::SceneMaterial,
      const TextureAsset* importedTexture = nullptr, const bool importedTextureSrgb = true,
      const float localTimeSeconds = 0.0f) {
    ensureGraphTargets(targetIndex + 1);
    RenderTarget& target = passTargets_[targetIndex];
    const glm::mat4 passTransform = glm::translate(glm::mat4(1.0f), perturbation.modelTranslation) *
      glm::scale(glm::mat4(1.0f), glm::vec3(std::max(0.01f, perturbation.modelScale)));
    const float azimuth = glm::radians(state.lighting.azimuth);
    const float elevation = glm::radians(state.lighting.elevation);
    const glm::vec3 lightDirection(std::cos(elevation) * std::cos(azimuth), std::sin(elevation),
      std::cos(elevation) * std::sin(azimuth));
    const glm::vec3 lightUp = std::abs(lightDirection.y) > 0.98f ? glm::vec3(0, 0, 1) : glm::vec3(0, 1, 0);
    const glm::mat4 lightView = glm::lookAt(lightDirection * 9.0f, glm::vec3(0), lightUp);
    const glm::mat4 lightProjection = glm::ortho(-5.0f, 5.0f, -5.0f, 5.0f, 0.1f, 20.0f);
    const glm::mat4 lightSpace = lightProjection * lightView;
    const bool shadowsEnabled = state.lighting.shadows &&
      (scene == TestScene::Lighting || scene == TestScene::ImportedModel);

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
      glUniform1f(glGetUniformLocation(shadowProgram_, "uTimeSeconds"), localTimeSeconds);
      glUniformMatrix4fv(glGetUniformLocation(shadowProgram_, "uLightSpace"), 1, GL_FALSE, glm::value_ptr(lightSpace));
      glUniform1f(glGetUniformLocation(shadowProgram_, "uQuantization"), state.geometry.vertexQuantization);
      glUniform1i(glGetUniformLocation(shadowProgram_, "uFieldEnabled"), state.field.enabled);
      glUniform3fv(glGetUniformLocation(shadowProgram_, "uFieldSourceA"), 1, glm::value_ptr(state.field.sourceA));
      glUniform3fv(glGetUniformLocation(shadowProgram_, "uFieldSourceB"), 1, glm::value_ptr(state.field.sourceB));
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldWavelength"), state.field.wavelength);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldPhaseOffset"), state.field.phaseOffset);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldAmplitudeA"), state.field.amplitudeA);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldAmplitudeB"), state.field.amplitudeB);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldFalloff"), state.field.falloff);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldBandSharpness"), state.field.bandSharpness);
      glUniform1i(glGetUniformLocation(shadowProgram_, "uFieldVisualization"), state.field.visualization);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldVertexDisplacement"), state.field.vertexDisplacement);
      glUniform1i(glGetUniformLocation(shadowProgram_, "uFieldSignedDisplacement"), state.field.signedDisplacement);
      glUniform1i(glGetUniformLocation(shadowProgram_, "uFieldProducerKind"), state.field.producerKind);
      glUniform1i(glGetUniformLocation(shadowProgram_, "uFieldSdfAType"), state.field.sdfA.type);
      glUniform3fv(glGetUniformLocation(shadowProgram_, "uFieldSdfAPosition"), 1,
        glm::value_ptr(state.field.sdfA.position));
      glUniform3fv(glGetUniformLocation(shadowProgram_, "uFieldSdfAParameters"), 1,
        glm::value_ptr(state.field.sdfA.parameters));
      glUniform1i(glGetUniformLocation(shadowProgram_, "uFieldSdfBType"), state.field.sdfB.type);
      glUniform3fv(glGetUniformLocation(shadowProgram_, "uFieldSdfBPosition"), 1,
        glm::value_ptr(state.field.sdfB.position));
      glUniform3fv(glGetUniformLocation(shadowProgram_, "uFieldSdfBParameters"), 1,
        glm::value_ptr(state.field.sdfB.parameters));
      glUniform1i(glGetUniformLocation(shadowProgram_, "uFieldSdfOperation"), state.field.sdfOperation);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldSdfSmoothness"), state.field.sdfSmoothness);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldSdfPreviewRange"), state.field.sdfPreviewRange);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldIsoLevel"), state.field.isoLevel);
      glUniform1i(glGetUniformLocation(shadowProgram_, "uFieldDiscardEnabled"), state.field.discardBelowEnabled);
      glUniform1f(glGetUniformLocation(shadowProgram_, "uFieldDiscardThreshold"), state.field.discardThreshold);
      glBindVertexArray(vao_);
      auto drawShadow = [this, &passTransform](const MeshRange& mesh, const glm::mat4& modelMatrix) {
        const glm::mat4 transformed = passTransform * modelMatrix;
        glUniformMatrix4fv(glGetUniformLocation(shadowProgram_, "uModel"), 1, GL_FALSE,
          glm::value_ptr(transformed));
        glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
      };
      const glm::mat4 identity(1.0f);
      if (scene == TestScene::ImportedModel) {
        drawShadow(imported_, identity);
      } else {
        drawShadow(lowSphere_, glm::translate(identity, glm::vec3(-1.35f, 0, 0)));
        drawShadow(smoothSphere_, glm::translate(identity, glm::vec3(1.35f, 0, 0)));
        drawShadow(torus_, glm::translate(glm::scale(identity, glm::vec3(0.62f)), glm::vec3(0, 1.9f, 0)));
        drawShadow(plane_, glm::translate(glm::scale(identity, glm::vec3(0.75f)), glm::vec3(0, -1.55f, -0.1f)));
      }
    }

    const int samples = state.rasterization.samples == 1 ? 1 : std::min(state.rasterization.samples, maxSamples_);
    const bool needsStencil = scene == TestScene::StencilMask && state.stencil.enabled;
    target.resize(state.output.width, state.output.height, state.depth.precision, samples, needsStencil);
    if (scene != TestScene::SpectralMetamers) {
      glBindFramebuffer(GL_FRAMEBUFFER, target.spectralFbo);
      constexpr std::array<float, 4> zero = {0, 0, 0, 0};
      for (int attachment = 1; attachment <= 4; ++attachment)
        glClearBufferfv(GL_COLOR, attachment, zero.data());
    }
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
    const std::array<float, 4> background = {backgroundR, backgroundG, backgroundB, 1.0f};
    constexpr std::array<float, 4> emptyNormal = {0.5f, 0.5f, 1.0f, 0.0f};
    glClearBufferfv(GL_COLOR, 0, background.data());
    glClearBufferfv(GL_COLOR, 1, emptyNormal.data());
    glClear(GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);
    glDepthMask(state.depth.writing && !orderingTableActive ? GL_TRUE : GL_FALSE);

    glUseProgram(sceneProgram_);
    glUniform1f(location("uTimeSeconds"), localTimeSeconds);
    const glm::mat4 model = glm::rotate(glm::mat4(1.0f), glm::radians(-14.0f), glm::vec3(1, 0, 0));
    const float aspect = static_cast<float>(target.width) / static_cast<float>(target.height);
    const PassCameraMatrices passCamera = buildPassCamera(camera, state, perturbation, aspect);
    const glm::mat4& view = passCamera.view;
    const glm::mat4& projection = passCamera.projection;
    target.viewProjection = projection * view;
    target.inverseViewProjection = glm::inverse(target.viewProjection);
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
    const int passVisualization = output == PassOutput::Normals ? 2
      : output == PassOutput::VertexColor ? 3 : state.surface.visualization;
    glUniform1i(location("uVisualization"), passVisualization);
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
    glUniform1i(location("uUvMapping"), static_cast<int>(perturbation.uvMapping));
    glUniform1f(location("uNormalInflation"), perturbation.normalInflation);
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
    glUniform3fv(location("uCameraPosition"), 1, glm::value_ptr(passCamera.eye));
    glUniform2fv(location("uUvOffset"), 1, glm::value_ptr(perturbation.uvOffset));
    glUniform2fv(location("uUvScale"), 1, glm::value_ptr(perturbation.uvScale));
    glUniform1f(location("uUvRotation"), perturbation.uvRotation);
    glUniform2fv(location("uUvPivot"), 1, glm::value_ptr(perturbation.uvPivot));
    glUniform1i(location("uFieldEnabled"), state.field.enabled);
    glUniform3fv(location("uFieldSourceA"), 1, glm::value_ptr(state.field.sourceA));
    glUniform3fv(location("uFieldSourceB"), 1, glm::value_ptr(state.field.sourceB));
    glUniform1f(location("uFieldWavelength"), state.field.wavelength);
    glUniform1f(location("uFieldPhaseOffset"), state.field.phaseOffset);
    glUniform1f(location("uFieldAmplitudeA"), state.field.amplitudeA);
    glUniform1f(location("uFieldAmplitudeB"), state.field.amplitudeB);
    glUniform1f(location("uFieldFalloff"), state.field.falloff);
    glUniform1f(location("uFieldBandSharpness"), state.field.bandSharpness);
    glUniform1i(location("uFieldVisualization"), state.field.visualization);
    glUniform1f(location("uFieldVertexDisplacement"), state.field.vertexDisplacement);
    glUniform1i(location("uFieldSignedDisplacement"), state.field.signedDisplacement);
    glUniform1i(location("uFieldProducerKind"), state.field.producerKind);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, simulationMatterTexture_);
    glUniform1i(location("uSimulationMatter"), 9);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, simulationDynamicsTexture_);
    glUniform1i(location("uSimulationDynamics"), 10);
    glUniform1i(location("uSimulationChannel"), state.field.visualization);
    glUniform1i(location("uFieldSdfAType"), state.field.sdfA.type);
    glUniform3fv(location("uFieldSdfAPosition"), 1, glm::value_ptr(state.field.sdfA.position));
    glUniform3fv(location("uFieldSdfAParameters"), 1, glm::value_ptr(state.field.sdfA.parameters));
    glUniform1i(location("uFieldSdfBType"), state.field.sdfB.type);
    glUniform3fv(location("uFieldSdfBPosition"), 1, glm::value_ptr(state.field.sdfB.position));
    glUniform3fv(location("uFieldSdfBParameters"), 1, glm::value_ptr(state.field.sdfB.parameters));
    glUniform1i(location("uFieldSdfOperation"), state.field.sdfOperation);
    glUniform1f(location("uFieldSdfSmoothness"), state.field.sdfSmoothness);
    glUniform1f(location("uFieldSdfPreviewRange"), state.field.sdfPreviewRange);
    glUniform1f(location("uFieldIsoLevel"), state.field.isoLevel);
    glUniform1i(location("uFieldDiscardEnabled"), state.field.discardBelowEnabled);
    glUniform1f(location("uFieldDiscardThreshold"), state.field.discardThreshold);
    glUniform1f(location("uFieldSurfaceColorInfluence"), state.field.surfaceColorInfluence);
    glUniform1f(location("uFieldEmissionInfluence"), state.field.emissionInfluence);
    glUniform3fv(location("uFieldLowColor"), 1, glm::value_ptr(state.field.lowColor));
    glUniform3fv(location("uFieldHighColor"), 1, glm::value_ptr(state.field.highColor));
    GLint minificationFilter = state.texture.nearestFiltering ? GL_NEAREST : GL_LINEAR;
    if (state.texture.mipmapping) {
      if (state.texture.nearestFiltering) minificationFilter = GL_NEAREST_MIPMAP_NEAREST;
      else minificationFilter = state.texture.trilinear ? GL_LINEAR_MIPMAP_LINEAR : GL_LINEAR_MIPMAP_NEAREST;
    }
    glUniform1i(location("uTexture"), 0);
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, indexedTexture_);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
    glUniform1i(location("uIndexedTexture"), 4);
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, clutTexture_);
    glUniform1i(location("uClut"), 5);
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
    auto bindSurfaceTexture = [this, &state, minificationFilter](const GLuint texture, const bool srgb,
        const bool indexedAvailable) {
      glActiveTexture(GL_TEXTURE0);
      glBindTexture(GL_TEXTURE_2D, texture);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minificationFilter);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
        state.texture.nearestFiltering ? GL_NEAREST : GL_LINEAR);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
      glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, state.texture.repeat ? GL_REPEAT : GL_CLAMP_TO_EDGE);
      if (GLEW_EXT_texture_filter_anisotropic)
        glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAX_ANISOTROPY_EXT,
          std::clamp(state.texture.anisotropy, 1.0f, maxAnisotropy_));
      glUniform1i(location("uTextureColorMode"), indexedAvailable ? state.texture.colorMode : 0);
      glUniform1i(location("uHasIndexedTexture"), indexedAvailable);
      glUniform1i(location("uTextureSrgb"), srgb);
    };
    if (textureSource == TextureSource::ImportedOverride && importedTexture != nullptr)
      bindSurfaceTexture(overrideTexture(importedTexture), importedTextureSrgb, false);
    else if (textureSource == TextureSource::White)
      bindSurfaceTexture(whiteTexture_, false, false);
    else
      bindSurfaceTexture(checkerTexture_, true, true);
    glBindVertexArray(vao_);
    glUniform1i(location("uFieldGeometryAffects"), true);
    auto drawMesh = [this, &passTransform](const MeshRange& mesh, const glm::mat4& modelMatrix,
        const glm::vec3& tint, const bool fieldGeometryAffects = true) {
      matrix("uModel", passTransform * modelMatrix);
      const glm::vec4 tintWithAlpha(tint, 1.0f);
      glUniform4fv(location("uObjectTint"), 1, glm::value_ptr(tintWithAlpha));
      glUniform1i(location("uFieldGeometryAffects"), fieldGeometryAffects);
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
            const glm::vec4 viewCenter = view * passTransform * transforms[objectIndex] * glm::vec4(0, 0, 0, 1);
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
      case TestScene::FieldInterference: {
        drawMesh(plane_, glm::translate(glm::scale(identity, glm::vec3(0.92f, 1.0f, 0.48f)),
          glm::vec3(0.0f, -1.15f, 0.0f)), glm::vec3(1.0f));
        drawMesh(torus_, glm::translate(identity, glm::vec3(-1.35f, -0.05f, 0.0f)) *
          glm::scale(identity, glm::vec3(0.58f)), glm::vec3(1.0f));
        drawMesh(denseTorus_, glm::translate(identity, glm::vec3(1.35f, -0.05f, 0.0f)) *
          glm::scale(identity, glm::vec3(0.58f)), glm::vec3(1.0f));
        bindSurfaceTexture(whiteTexture_, false, false);
        const auto drawSource = [this](const glm::vec3& position, const glm::vec3& tint) {
          matrix("uModel", glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), glm::vec3(0.11f)));
          glUniform4fv(location("uObjectTint"), 1, glm::value_ptr(glm::vec4(tint, 1.0f)));
          glUniform1i(location("uFieldGeometryAffects"), false);
          glDrawArrays(GL_TRIANGLES, smoothSphere_.first, smoothSphere_.count);
        };
        drawSource(state.field.sourceA, glm::vec3(0.18f, 0.82f, 1.0f));
        drawSource(state.field.sourceB, glm::vec3(1.0f, 0.28f, 0.68f));
        break;
      }
      case TestScene::SdfIsoSurface:
        drawMesh(plane_, glm::translate(glm::scale(identity, glm::vec3(0.80f, 1.0f, 0.42f)),
          glm::vec3(0.0f, -1.55f, 0.0f)), glm::vec3(0.18f, 0.22f, 0.28f));
        break;
      case TestScene::SpectralMetamers:
        break;
      case TestScene::ElementalChamber: {
        drawMesh(plane_, glm::scale(identity, glm::vec3(1.70f, 1.0f, 0.50f)), glm::vec3(1.0f));
        bindSurfaceTexture(whiteTexture_, false, false);
        const auto drawMarker = [this](const glm::vec3& position, const glm::vec3& tint, const float scale) {
          matrix("uModel", glm::translate(glm::mat4(1.0f), position) *
            glm::scale(glm::mat4(1.0f), glm::vec3(scale)));
          glUniform4fv(location("uObjectTint"), 1, glm::value_ptr(glm::vec4(tint, 1.0f)));
          glUniform1i(location("uFieldGeometryAffects"), false);
          glDrawArrays(GL_TRIANGLES, smoothSphere_.first, smoothSphere_.count);
        };
        drawMarker({state.field.sourceA.x, 0.16f, state.field.sourceA.z},
          {1.0f, 0.22f, 0.04f}, 0.14f);
        drawMarker({2.8f, 0.16f, 1.42f}, {0.35f, 0.75f, 1.0f}, 0.18f);
        break;
      }
      case TestScene::ImportedModel:
        for (const ImportedSubmeshGpu& submesh : importedSubmeshes_) {
          const ImportedMaterialGpu* material = submesh.materialIndex < importedMaterials_.size()
            ? &importedMaterials_[submesh.materialIndex] : nullptr;
          const glm::vec4 baseColor = material != nullptr ? material->baseColor : glm::vec4(1.0f);
          GLuint surfaceTexture = whiteTexture_;
          bool surfaceSrgb = false;
          bool indexedAvailable = false;
          if (textureSource == TextureSource::BuiltInChecker) {
            surfaceTexture = checkerTexture_;
            surfaceSrgb = true;
            indexedAvailable = true;
          } else if (textureSource == TextureSource::ImportedOverride && importedTexture != nullptr) {
            surfaceTexture = overrideTexture(importedTexture);
            surfaceSrgb = importedTextureSrgb;
          } else if (textureSource == TextureSource::SceneMaterial && material != nullptr &&
              material->baseColorTexture != 0) {
            surfaceTexture = material->baseColorTexture;
            surfaceSrgb = true;
          }
          bindSurfaceTexture(surfaceTexture, surfaceSrgb, indexedAvailable);
          matrix("uModel", passTransform);
          glUniform4fv(location("uObjectTint"), 1, glm::value_ptr(baseColor));
          glDrawArrays(GL_TRIANGLES, submesh.range.first, submesh.range.count);
        }
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
      glReadBuffer(GL_COLOR_ATTACHMENT0);
      glDrawBuffer(GL_COLOR_ATTACHMENT0);
      glBlitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
      glReadBuffer(GL_COLOR_ATTACHMENT1);
      glDrawBuffer(GL_COLOR_ATTACHMENT1);
      glBlitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height,
        GL_COLOR_BUFFER_BIT, GL_NEAREST);
      const GLbitfield resolveDepth = GL_DEPTH_BUFFER_BIT | (needsStencil ? GL_STENCIL_BUFFER_BIT : 0);
      glBlitFramebuffer(0, 0, target.width, target.height, 0, 0, target.width, target.height,
        resolveDepth, GL_NEAREST);
      constexpr std::array<GLenum, 2> sceneDrawBuffers = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
      glBindFramebuffer(GL_DRAW_FRAMEBUFFER, target.sceneFbo);
      glDrawBuffers(static_cast<GLsizei>(sceneDrawBuffers.size()), sceneDrawBuffers.data());
    }

    if (scene == TestScene::SpectralMetamers) {
      glBindFramebuffer(GL_FRAMEBUFFER, target.spectralFbo);
      constexpr std::array<GLenum, 5> buffers = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1,
        GL_COLOR_ATTACHMENT2, GL_COLOR_ATTACHMENT3, GL_COLOR_ATTACHMENT4};
      glDrawBuffers(static_cast<GLsizei>(buffers.size()), buffers.data());
      glViewport(0, 0, target.width, target.height);
      if (state.depth.testing) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
      glDepthMask(state.depth.writing ? GL_TRUE : GL_FALSE);
      glDepthFunc(depthFunctions[std::clamp(state.depth.function, 0, 3)]);
      glEnable(GL_CULL_FACE);
      glCullFace(GL_BACK);
      glDisable(GL_BLEND);
      // A single glClear(GL_COLOR_BUFFER_BIT) would copy the presentation
      // background's RGBA tuple into every MRT attachment. In the spectral
      // attachments that tuple would become four invented wavelength samples
      // (including an intensity of 1.0 from alpha), so observers would see
      // color in empty space. Presentation and radiance have different units
      // and must be cleared independently.
      constexpr std::array<float, 4> presentationBackground = {0.018f, 0.021f, 0.027f, 1.0f};
      constexpr std::array<float, 4> zeroRadiance = {0.0f, 0.0f, 0.0f, 0.0f};
      glClearBufferfv(GL_COLOR, 0, presentationBackground.data());
      for (int spectralAttachment = 1; spectralAttachment <= 4; ++spectralAttachment)
        glClearBufferfv(GL_COLOR, spectralAttachment, zeroRadiance.data());
      const float clearDepth = state.depth.function == 2 ? 0.0f : 1.0f;
      glClearBufferfv(GL_DEPTH, 0, &clearDepth);
      if (std::getenv("GRAPHICS_LAB_VALIDATE_HANDBOOK")) {
        for (int spectralAttachment = 1; spectralAttachment <= 4; ++spectralAttachment) {
          std::array<float, 4> clearSample{};
          glReadBuffer(GL_COLOR_ATTACHMENT0 + spectralAttachment);
          glReadPixels(0, 0, 1, 1, GL_RGBA, GL_FLOAT, clearSample.data());
          if (std::any_of(clearSample.begin(), clearSample.end(),
              [](const float value) { return value != 0.0f; }))
            failRenderer("spectral background clear produced nonzero radiance");
        }
        glReadBuffer(GL_COLOR_ATTACHMENT0);
      }
      glUseProgram(spectralProgram_);
      glUniformMatrix4fv(glGetUniformLocation(spectralProgram_, "uView"), 1, GL_FALSE, glm::value_ptr(view));
      glUniformMatrix4fv(glGetUniformLocation(spectralProgram_, "uProjection"), 1, GL_FALSE,
        glm::value_ptr(projection));
      glUniform3fv(glGetUniformLocation(spectralProgram_, "uLightDirection"), 1,
        glm::value_ptr(lightDirection));
      glUniform1f(glGetUniformLocation(spectralProgram_, "uAmbient"), state.lighting.ambient);
      glUniform1i(glGetUniformLocation(spectralProgram_, "uIlluminant"), state.spectral.illuminant);
      glUniform1i(glGetUniformLocation(spectralProgram_, "uObserver"), state.spectral.observer);
      glUniform1f(glGetUniformLocation(spectralProgram_, "uExposure"), state.spectral.exposure);
      glBindVertexArray(vao_);
      const auto drawSpectral = [this, &passTransform](const MeshRange& mesh, const glm::mat4& transform,
          const int material) {
        const glm::mat4 modelMatrix = passTransform * transform;
        glUniformMatrix4fv(glGetUniformLocation(spectralProgram_, "uModel"), 1, GL_FALSE,
          glm::value_ptr(modelMatrix));
        glUniform1i(glGetUniformLocation(spectralProgram_, "uMaterial"), material);
        glDrawArrays(GL_TRIANGLES, mesh.first, mesh.count);
      };
      drawSpectral(smoothSphere_, glm::translate(identity, glm::vec3(-1.25f, 0.0f, 0.0f)), 1);
      drawSpectral(smoothSphere_, glm::translate(identity, glm::vec3(1.25f, 0.0f, 0.0f)), 2);
      const glm::mat4 floor = glm::translate(identity, glm::vec3(0.0f, -1.18f, -0.6f)) *
        glm::rotate(identity, glm::radians(-90.0f), glm::vec3(1, 0, 0)) *
        glm::scale(identity, glm::vec3(3.5f, 2.4f, 1.0f));
      drawSpectral(quad_, floor, 0);
    }

    if (state.field.enabled && state.field.producerKind == 1 && state.field.isoSurfaceEnabled) {
      glBindFramebuffer(GL_FRAMEBUFFER, target.sceneFbo);
      glViewport(0, 0, target.width, target.height);
      if (state.depth.testing) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
      glDepthMask(state.depth.writing ? GL_TRUE : GL_FALSE);
      glDepthFunc(depthFunctions[std::clamp(state.depth.function, 0, 3)]);
      glDisable(GL_CULL_FACE);
      glDisable(GL_BLEND);
      glUseProgram(sdfIsoProgram_);
      glUniform1f(glGetUniformLocation(sdfIsoProgram_, "uTimeSeconds"), localTimeSeconds);
      const glm::mat4 viewProjection = projection * view;
      const glm::mat4 inverseViewProjection = glm::inverse(viewProjection);
      glUniformMatrix4fv(glGetUniformLocation(sdfIsoProgram_, "uInverseViewProjection"), 1, GL_FALSE,
        glm::value_ptr(inverseViewProjection));
      glUniformMatrix4fv(glGetUniformLocation(sdfIsoProgram_, "uViewProjection"), 1, GL_FALSE,
        glm::value_ptr(viewProjection));
      glUniform3fv(glGetUniformLocation(sdfIsoProgram_, "uCameraPosition"), 1,
        glm::value_ptr(passCamera.eye));
      glUniform1i(glGetUniformLocation(sdfIsoProgram_, "uOrthographic"), state.camera.orthographic);
      glUniform1i(glGetUniformLocation(sdfIsoProgram_, "uSdfAType"), state.field.sdfA.type);
      glUniform3fv(glGetUniformLocation(sdfIsoProgram_, "uSdfAPosition"), 1,
        glm::value_ptr(state.field.sdfA.position));
      glUniform3fv(glGetUniformLocation(sdfIsoProgram_, "uSdfAParameters"), 1,
        glm::value_ptr(state.field.sdfA.parameters));
      glUniform1i(glGetUniformLocation(sdfIsoProgram_, "uSdfBType"), state.field.sdfB.type);
      glUniform3fv(glGetUniformLocation(sdfIsoProgram_, "uSdfBPosition"), 1,
        glm::value_ptr(state.field.sdfB.position));
      glUniform3fv(glGetUniformLocation(sdfIsoProgram_, "uSdfBParameters"), 1,
        glm::value_ptr(state.field.sdfB.parameters));
      glUniform1i(glGetUniformLocation(sdfIsoProgram_, "uSdfOperation"), state.field.sdfOperation);
      glUniform1f(glGetUniformLocation(sdfIsoProgram_, "uSdfSmoothness"), state.field.sdfSmoothness);
      glUniform1f(glGetUniformLocation(sdfIsoProgram_, "uIsoLevel"), state.field.isoLevel);
      glUniform1i(glGetUniformLocation(sdfIsoProgram_, "uMaximumSteps"),
        std::clamp(state.field.isoMaxSteps, 8, 512));
      glUniform1f(glGetUniformLocation(sdfIsoProgram_, "uHitEpsilon"),
        std::max(state.field.isoEpsilon, 0.0001f));
      glUniform1f(glGetUniformLocation(sdfIsoProgram_, "uMaximumDistance"),
        std::max(state.field.isoMaxDistance, 1.0f));
      glUniform3fv(glGetUniformLocation(sdfIsoProgram_, "uSurfaceColor"), 1,
        glm::value_ptr(state.field.isoColor));
      glUniform3fv(glGetUniformLocation(sdfIsoProgram_, "uLightDirection"), 1,
        glm::value_ptr(lightDirection));
      glUniform1f(glGetUniformLocation(sdfIsoProgram_, "uAmbient"), state.lighting.ambient);
      glBindVertexArray(fullscreenVao_);
      glDrawArrays(GL_TRIANGLES, 0, 3);
    }

    // Material rendering and field generation are deliberately separate. The
    // field pass reconstructs visible world positions from depth and writes a
    // scalar resource that later passes may preview, composite, or use as a mask.
    glBindFramebuffer(GL_FRAMEBUFFER, target.fieldFbo);
    glViewport(0, 0, target.width, target.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glUseProgram(fieldProgram_);
    glUniform1f(glGetUniformLocation(fieldProgram_, "uTimeSeconds"), localTimeSeconds);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, target.depthTexture);
    glUniform1i(glGetUniformLocation(fieldProgram_, "uDepth"), 0);
    const glm::mat4 inverseViewProjection = glm::inverse(projection * view);
    glUniformMatrix4fv(glGetUniformLocation(fieldProgram_, "uInverseViewProjection"), 1, GL_FALSE,
      glm::value_ptr(inverseViewProjection));
    glUniform1i(glGetUniformLocation(fieldProgram_, "uEnabled"), state.field.enabled);
    glUniform3fv(glGetUniformLocation(fieldProgram_, "uSourceA"), 1, glm::value_ptr(state.field.sourceA));
    glUniform3fv(glGetUniformLocation(fieldProgram_, "uSourceB"), 1, glm::value_ptr(state.field.sourceB));
    glUniform1f(glGetUniformLocation(fieldProgram_, "uWavelength"), state.field.wavelength);
    glUniform1f(glGetUniformLocation(fieldProgram_, "uPhaseOffset"), state.field.phaseOffset);
    glUniform1f(glGetUniformLocation(fieldProgram_, "uAmplitudeA"), state.field.amplitudeA);
    glUniform1f(glGetUniformLocation(fieldProgram_, "uAmplitudeB"), state.field.amplitudeB);
    glUniform1f(glGetUniformLocation(fieldProgram_, "uFalloff"), state.field.falloff);
    glUniform1f(glGetUniformLocation(fieldProgram_, "uBandSharpness"), state.field.bandSharpness);
    glUniform1i(glGetUniformLocation(fieldProgram_, "uVisualization"), state.field.visualization);
    glUniform1i(glGetUniformLocation(fieldProgram_, "uProducerKind"), state.field.producerKind);
    glActiveTexture(GL_TEXTURE9);
    glBindTexture(GL_TEXTURE_2D, simulationMatterTexture_);
    glUniform1i(glGetUniformLocation(fieldProgram_, "uSimulationMatter"), 9);
    glActiveTexture(GL_TEXTURE10);
    glBindTexture(GL_TEXTURE_2D, simulationDynamicsTexture_);
    glUniform1i(glGetUniformLocation(fieldProgram_, "uSimulationDynamics"), 10);
    glUniform1i(glGetUniformLocation(fieldProgram_, "uSimulationChannel"), state.field.visualization);
    glUniform1i(glGetUniformLocation(fieldProgram_, "uSdfAType"), state.field.sdfA.type);
    glUniform3fv(glGetUniformLocation(fieldProgram_, "uSdfAPosition"), 1,
      glm::value_ptr(state.field.sdfA.position));
    glUniform3fv(glGetUniformLocation(fieldProgram_, "uSdfAParameters"), 1,
      glm::value_ptr(state.field.sdfA.parameters));
    glUniform1i(glGetUniformLocation(fieldProgram_, "uSdfBType"), state.field.sdfB.type);
    glUniform3fv(glGetUniformLocation(fieldProgram_, "uSdfBPosition"), 1,
      glm::value_ptr(state.field.sdfB.position));
    glUniform3fv(glGetUniformLocation(fieldProgram_, "uSdfBParameters"), 1,
      glm::value_ptr(state.field.sdfB.parameters));
    glUniform1i(glGetUniformLocation(fieldProgram_, "uSdfOperation"), state.field.sdfOperation);
    glUniform1f(glGetUniformLocation(fieldProgram_, "uSdfSmoothness"), state.field.sdfSmoothness);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);

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
      auto countMesh = [this, &passTransform](const MeshRange& mesh, const glm::mat4& modelMatrix) {
        const glm::mat4 transformed = passTransform * modelMatrix;
        glUniformMatrix4fv(glGetUniformLocation(overdrawProgram_, "uModel"), 1, GL_FALSE,
          glm::value_ptr(transformed));
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
        case TestScene::FieldInterference:
          countMesh(plane_, glm::translate(glm::scale(identity, glm::vec3(0.92f, 1.0f, 0.48f)),
            glm::vec3(0.0f, -1.15f, 0.0f)));
          countMesh(torus_, glm::translate(identity, glm::vec3(-1.35f, -0.05f, 0.0f)) *
            glm::scale(identity, glm::vec3(0.58f)));
          countMesh(denseTorus_, glm::translate(identity, glm::vec3(1.35f, -0.05f, 0.0f)) *
            glm::scale(identity, glm::vec3(0.58f)));
          break;
        case TestScene::SdfIsoSurface:
          countMesh(plane_, glm::translate(glm::scale(identity, glm::vec3(0.80f, 1.0f, 0.42f)),
            glm::vec3(0.0f, -1.55f, 0.0f)));
          break;
        case TestScene::SpectralMetamers:
          countMesh(smoothSphere_, glm::translate(identity, glm::vec3(-1.25f, 0.0f, 0.0f)));
          countMesh(smoothSphere_, glm::translate(identity, glm::vec3(1.25f, 0.0f, 0.0f)));
          break;
        case TestScene::ElementalChamber:
          countMesh(plane_, glm::scale(identity, glm::vec3(1.70f, 1.0f, 0.50f)));
          break;
        case TestScene::ImportedModel:
          countMesh(imported_, identity);
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
    glUniform1i(glGetUniformLocation(outputProgram_, "uDepthVisualization"),
      output == PassOutput::Depth ? 2 : state.depth.visualization);
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
    glActiveTexture(GL_TEXTURE4);
    glBindTexture(GL_TEXTURE_2D, target.fieldTexture);
    glUniform1i(glGetUniformLocation(outputProgram_, "uField"), 4);
    glUniform1i(glGetUniformLocation(outputProgram_, "uFieldOutput"), output == PassOutput::FieldSignal);
    glUniform3fv(glGetUniformLocation(outputProgram_, "uFieldLowColor"), 1,
      glm::value_ptr(state.field.lowColor));
    glUniform3fv(glGetUniformLocation(outputProgram_, "uFieldHighColor"), 1,
      glm::value_ptr(state.field.highColor));
    glUniform1i(glGetUniformLocation(outputProgram_, "uFieldSignedDistance"),
      state.field.producerKind == 1);
    glUniform1f(glGetUniformLocation(outputProgram_, "uFieldSdfPreviewRange"),
      state.field.sdfPreviewRange);
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

  GLuint compositeTextures(const GLuint imageA, const GLuint imageB, const GLuint explicitMask,
      const GLuint maskDepth,
      const GLuint maskField, const std::array<GLuint, 4>& spectrumA,
      const std::array<GLuint, 4>& spectrumB,
      const RendererState& maskState, const CompositeStep& step,
      const std::size_t outputIndex, const int width, const int height) {
    const std::size_t pingPong = outputIndex % relationFbos_.size();
    prepareGraphTarget(false, pingPong, width, height);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(relationProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, imageA);
    glUniform1i(glGetUniformLocation(relationProgram_, "uImageA"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, imageB);
    glUniform1i(glGetUniformLocation(relationProgram_, "uImageB"), 1);
    glActiveTexture(GL_TEXTURE12);
    glBindTexture(GL_TEXTURE_2D, explicitMask);
    glUniform1i(glGetUniformLocation(relationProgram_, "uMaskImage"), 12);
    glUniform1i(glGetUniformLocation(relationProgram_, "uUseExplicitMask"), explicitMask != 0);
    for (int group = 0; group < 4; ++group) {
      glActiveTexture(GL_TEXTURE4 + group);
      glBindTexture(GL_TEXTURE_2D, spectrumA[static_cast<std::size_t>(group)]);
      const std::string uniformName = "uSpectrumA" + std::to_string(group);
      glUniform1i(glGetUniformLocation(relationProgram_, uniformName.c_str()), 4 + group);
      glActiveTexture(GL_TEXTURE8 + group);
      glBindTexture(GL_TEXTURE_2D, spectrumB[static_cast<std::size_t>(group)]);
      const std::string uniformNameB = "uSpectrumB" + std::to_string(group);
      glUniform1i(glGetUniformLocation(relationProgram_, uniformNameB.c_str()), 8 + group);
    }
    glUniform1i(glGetUniformLocation(relationProgram_, "uSourceAMode"), static_cast<int>(step.sourceA));
    glUniform1i(glGetUniformLocation(relationProgram_, "uSourceBMode"), static_cast<int>(step.sourceB));
    glUniform1i(glGetUniformLocation(relationProgram_, "uInterpretationA"),
      static_cast<int>(step.interpretationA));
    glUniform1i(glGetUniformLocation(relationProgram_, "uInterpretationB"),
      static_cast<int>(step.interpretationB));
    glUniform1f(glGetUniformLocation(relationProgram_, "uObserverExposureStops"),
      step.observerExposureStops);
    glUniform1f(glGetUniformLocation(relationProgram_, "uRodSensitivity"), step.rodSensitivity);
    glUniform1f(glGetUniformLocation(relationProgram_, "uOpponentGain"), step.opponentGain);
    glUniform4fv(glGetUniformLocation(relationProgram_, "uFixedColor"), 1, glm::value_ptr(step.fixedColor));
    glUniform1i(glGetUniformLocation(relationProgram_, "uBitDepth"), std::clamp(step.bitDepth, 1, 8));
    glUniform1f(glGetUniformLocation(relationProgram_, "uHistoryDecay"), step.historyDecay);
    glUniform2fv(glGetUniformLocation(relationProgram_, "uHistoryUvOffset"), 1,
      glm::value_ptr(step.historyUvOffset));
    glUniform2fv(glGetUniformLocation(relationProgram_, "uHistoryUvScale"), 1,
      glm::value_ptr(step.historyUvScale));
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, maskDepth);
    glUniform1i(glGetUniformLocation(relationProgram_, "uMaskDepth"), 2);
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, maskField);
    glUniform1i(glGetUniformLocation(relationProgram_, "uMaskField"), 3);
    glUniform1i(glGetUniformLocation(relationProgram_, "uOperator"), static_cast<int>(step.operation));
    glUniform1f(glGetUniformLocation(relationProgram_, "uGain"), step.gain);
    glUniform1f(glGetUniformLocation(relationProgram_, "uBias"), step.bias);
    glUniform1f(glGetUniformLocation(relationProgram_, "uOpacity"), step.opacity);
    glUniform1i(glGetUniformLocation(relationProgram_, "uColorSpace"), static_cast<int>(step.colorSpace));
    glUniform1i(glGetUniformLocation(relationProgram_, "uRangeMode"), static_cast<int>(step.range));
    glUniform1i(glGetUniformLocation(relationProgram_, "uMaskMode"), static_cast<int>(step.mask));
    glUniform1i(glGetUniformLocation(relationProgram_, "uInvertMask"), step.invertMask);
    glUniform1f(glGetUniformLocation(relationProgram_, "uMaskNearPlane"), maskState.camera.nearPlane);
    glUniform1i(glGetUniformLocation(relationProgram_, "uMaskOrthographic"), maskState.camera.orthographic);
    glUniform1i(glGetUniformLocation(relationProgram_, "uMaskFieldSignedDistance"),
      maskState.field.producerKind == 1);
    glUniform1f(glGetUniformLocation(relationProgram_, "uMaskSdfPreviewRange"),
      maskState.field.sdfPreviewRange);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return relationTextures_[pingPong];
  }

  GLuint renderRelation(const RelationOperator operation, const float gain, const float bias) {
    CompositeStep step;
    step.operation = operation;
    step.gain = gain;
    step.bias = bias;
    return compositeTextures(passTargets_[0].outputTexture, passTargets_[1].outputTexture, 0,
      passTargets_[1].depthTexture, passTargets_[1].fieldTexture, {}, {}, RendererState{}, step,
      0, passTargets_[0].width, passTargets_[0].height);
  }

  GLuint compareSignals(const GLuint a, const GLuint b, const RelationOperator operation,
      const float gain, const float bias) {
    if (a == 0 || b == 0) return 0;
    CompositeStep step;
    step.sourceA = CompositeSource::RenderPass;
    step.sourceB = CompositeSource::RenderPass;
    step.operation = operation;
    step.gain = gain;
    step.bias = bias;
    step.colorSpace = CompositeColorSpace::LinearLight;
    return compositeTextures(a, b, 0, 0, 0, {}, {}, RendererState{}, step,
      relationTextures_.size() - 1, relationWidth_, relationHeight_);
  }

  GLuint processImage(const GLuint input, const int mode, const bool scalarInput,
      const glm::vec4 parameters, const glm::vec4 lowColor, const glm::vec4 highColor,
      const std::size_t outputIndex, const bool directionTarget = false,
      const GLuint vectorInput = 0, const int width = relationWidth_,
      const int height = relationHeight_) {
    if (input == 0) return 0;
    const std::size_t target = outputIndex % (directionTarget ? directionFbos_.size() : relationFbos_.size());
    prepareGraphTarget(directionTarget, target, width, height);
    glViewport(0, 0, width, height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(imageOperationProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, input);
    glUniform1i(glGetUniformLocation(imageOperationProgram_, "uInput"), 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, vectorInput);
    glUniform1i(glGetUniformLocation(imageOperationProgram_, "uVectorInput"), 1);
    glUniform1i(glGetUniformLocation(imageOperationProgram_, "uMode"), mode);
    glUniform1i(glGetUniformLocation(imageOperationProgram_, "uScalarInput"), scalarInput);
    glUniform4fv(glGetUniformLocation(imageOperationProgram_, "uParameters"), 1,
      glm::value_ptr(parameters));
    glUniform4fv(glGetUniformLocation(imageOperationProgram_, "uLowColor"), 1,
      glm::value_ptr(lowColor));
    glUniform4fv(glGetUniformLocation(imageOperationProgram_, "uHighColor"), 1,
      glm::value_ptr(highColor));
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return directionTarget ? directionTextures_[target] : relationTextures_[target];
  }

  GLuint analyzeStereo(const RenderTarget& left, const RenderTarget& right, const RenderPass& operation,
      const std::size_t outputIndex) {
    prepareGraphTarget(false, outputIndex, left.width, left.height);
    glViewport(0, 0, left.width, left.height);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(stereoAnalysisProgram_);
    const GLuint textures[] = {left.outputTexture, right.outputTexture, left.depthTexture, right.depthTexture};
    const char* names[] = {"uLeftColor", "uRightColor", "uLeftDepth", "uRightDepth"};
    for (int unit = 0; unit < 4; ++unit) {
      glActiveTexture(GL_TEXTURE0 + unit);
      glBindTexture(GL_TEXTURE_2D, textures[unit]);
      glUniform1i(glGetUniformLocation(stereoAnalysisProgram_, names[unit]), unit);
    }
    glUniformMatrix4fv(glGetUniformLocation(stereoAnalysisProgram_, "uLeftInverseViewProjection"),
      1, GL_FALSE, glm::value_ptr(left.inverseViewProjection));
    glUniformMatrix4fv(glGetUniformLocation(stereoAnalysisProgram_, "uRightInverseViewProjection"),
      1, GL_FALSE, glm::value_ptr(right.inverseViewProjection));
    glUniformMatrix4fv(glGetUniformLocation(stereoAnalysisProgram_, "uLeftViewProjection"),
      1, GL_FALSE, glm::value_ptr(left.viewProjection));
    glUniformMatrix4fv(glGetUniformLocation(stereoAnalysisProgram_, "uRightViewProjection"),
      1, GL_FALSE, glm::value_ptr(right.viewProjection));
    glUniform1i(glGetUniformLocation(stereoAnalysisProgram_, "uMode"),
      static_cast<int>(operation.stereoAnalysis));
    glUniform1f(glGetUniformLocation(stereoAnalysisProgram_, "uMaximumDisparityPixels"),
      operation.stereoMaximumDisparityPixels);
    glUniform1f(glGetUniformLocation(stereoAnalysisProgram_, "uOcclusionTolerance"),
      operation.stereoOcclusionTolerance);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return relationTextures_[outputIndex];
  }

  GLuint evaluate(const document::Document& document, const evaluation::EvaluationPlan& plan,
      evaluation::SignalRegistry& signals, const std::uint64_t revision,
      const float timeSeconds) {
    signals.clear();
    std::unordered_map<document::SignalId, std::size_t> lastUse;
    for (std::size_t index = 0; index < plan.nodes.size(); ++index) {
      for (const document::SignalId& output : plan.nodes[index].outputs)
        lastUse.insert_or_assign(output, index);
      for (const document::SignalRef& input : plan.nodes[index].inputs)
        if (input.frameOffset >= 0) lastUse[input.id] = index;
    }
    const std::unordered_set<document::SignalId> retained(plan.retainedSignals.begin(),
      plan.retainedSignals.end());
    if (plan.retainAllSignals) {
      for (auto& [signal, end] : lastUse) end = plan.nodes.size();
    } else {
      for (const document::SignalId& signal : retained) {
        const auto found = lastUse.find(signal);
        if (found != lastUse.end()) found->second = plan.nodes.size();
      }
    }
    for (const evaluation::EvaluationNode& node : plan.nodes) {
      const document::Operation* operation = document::findOperation(document, node.operation);
      const auto* measure = operation == nullptr ? nullptr
        : std::get_if<document::MeasureOperation>(&operation->data);
      if (measure == nullptr || measure->input.frameOffset < 0) continue;
      std::size_t aliasLastUse = lastUse[measure->input.id];
      for (const document::SignalId& output : node.outputs)
        if (const auto found = lastUse.find(output); found != lastUse.end())
          aliasLastUse = std::max(aliasLastUse, found->second);
      lastUse[measure->input.id] = aliasLastUse;
    }
    std::vector<std::vector<document::SignalId>> transientSignalsEndingAt(plan.nodes.size());
    if (!plan.retainAllSignals) {
      for (const auto& [signal, end] : lastUse)
        if (end < plan.nodes.size()) transientSignalsEndingAt[end].push_back(signal);
    }

    std::unordered_map<document::OperationId, std::size_t> renderSlots;
    std::unordered_map<document::OperationId, std::size_t> relationSlots;
    std::unordered_map<document::OperationId, std::size_t> directionSlots;
    std::vector<std::size_t> renderSlotRelease;
    std::vector<std::size_t> relationSlotRelease;
    std::vector<std::size_t> directionSlotRelease;
    const auto allocateSlot = [](std::vector<std::size_t>& releases,
        const std::size_t nodeIndex, const std::size_t release) {
      const auto available = std::find_if(releases.begin(), releases.end(),
        [nodeIndex](const std::size_t occupiedThrough) { return occupiedThrough < nodeIndex; });
      if (available == releases.end()) {
        releases.push_back(release);
        return releases.size() - 1;
      }
      *available = release;
      return static_cast<std::size_t>(std::distance(releases.begin(), available));
    };
    for (std::size_t index = 0; index < plan.nodes.size(); ++index) {
      const evaluation::EvaluationNode& node = plan.nodes[index];
      const document::Operation* operation = document::findOperation(document, node.operation);
      if (operation == nullptr) continue;
      std::size_t release = index;
      for (const document::SignalId& output : node.outputs)
        if (const auto found = lastUse.find(output); found != lastUse.end())
          release = std::max(release, found->second);
      if (std::holds_alternative<document::RenderOperation>(operation->data)) {
        renderSlots.emplace(operation->id, allocateSlot(renderSlotRelease, index, release));
      } else if (!std::holds_alternative<document::ConstantOperation>(operation->data) &&
          !std::holds_alternative<document::MeasureOperation>(operation->data)) {
        relationSlots.emplace(operation->id, allocateSlot(relationSlotRelease, index, release));
      }
      if (std::holds_alternative<document::EdgeOperation>(operation->data))
        directionSlots.emplace(operation->id, allocateSlot(directionSlotRelease, index, release));
    }
    ensureGraphTargets(std::max({renderSlotRelease.size(), relationSlotRelease.size(),
      directionSlotRelease.size()}));
    std::unordered_map<document::SignalId, std::size_t> targetBySignal;

    const auto applyTracks = [&](RenderPass& pass, const document::ObjectId owner) {
      for (const document::AnimationTrack& track : document.automation.animation) {
        if (track.target.owner != owner || track.keyframes.empty()) continue;
        const std::optional<AnimationProperty> property =
          document::animationProperty(track.target.property);
        if (!property.has_value()) continue;
        const PropertyAnimationTrack legacyTrack{*property, track.interpolation, track.keyframes};
        setAnimationPropertyValue(pass, *property, samplePropertyTrack(legacyTrack, timeSeconds));
      }
    };
    const auto renderState = [&](const document::Operation& operation,
        const document::RenderOperation& data) {
      RenderPass pass;
      pass.id = static_cast<int>(operation.id.value);
      pass.name = operation.name;
      pass.enabled = operation.enabled;
      pass.renderer = document.renderDefaults.renderer;
      pass.textureSource = document.renderDefaults.texture.source;
      pass.importedTexture = document.renderDefaults.texture.imported;
      pass.importedTextureSrgb = document.renderDefaults.texture.srgb;
      applyTracks(pass, document::renderDefaultsObject);
      pass.perturbation = data.perturbation;
      pass.output = data.presentedOutput;
      pass.textureSource = data.texture.source;
      pass.importedTexture = data.texture.imported;
      pass.importedTextureSrgb = data.texture.srgb;
      for (const PropertyOverride& overrideValue : data.overrides)
        setAnimationPropertyValue(pass, overrideValue.property, overrideValue.value);
      applyTracks(pass, document::operationObject(operation.id));
      return pass;
    };
    const auto signalResource = [&](const document::SignalRef signal)
        -> const evaluation::SignalResource* {
      return signal.frameOffset < 0 ? nullptr : signals.find(signal.id);
    };
    const auto signalTexture = [&](const document::SignalRef signal) {
      if (signal.frameOffset < 0) return historyTexture_;
      const evaluation::SignalResource* resource = signalResource(signal);
      return resource == nullptr || resource->textureCount == 0 ? 0U : resource->textures[0];
    };
    const auto signalSpectrum = [&](const document::SignalRef signal) {
      const evaluation::SignalResource* resource = signalResource(signal);
      return resource == nullptr || resource->textureCount != 4
        ? std::array<GLuint, 4>{} : resource->textures;
    };
    const auto signalDescriptor = [&](const document::SignalRef signal) {
      return document::findSignal(document, signal.id);
    };
    const auto signalExtent = [&](const document::SignalRef signal) {
      const evaluation::SignalResource* resource = signalResource(signal);
      return resource == nullptr || resource->extent.x <= 0 || resource->extent.y <= 0
        ? glm::ivec2(relationWidth_, relationHeight_)
        : glm::ivec2(resource->extent);
    };
    const auto isScalarImage = [&](const document::SignalRef signal) {
      const document::SignalDescriptor* descriptor = signalDescriptor(signal);
      return descriptor != nullptr && document::isScreenScalar(*descriptor);
    };
    const auto signalTarget = [&](const document::SignalRef signal) -> const RenderTarget* {
      const auto found = targetBySignal.find(signal.id);
      return found == targetBySignal.end() ? nullptr : &passTargets_[found->second];
    };
    const auto constantValue = [&](const document::SignalRef signal) -> std::optional<glm::vec4> {
      const document::SignalDescriptor* descriptor = document::findSignal(document, signal.id);
      const document::Operation* producer = descriptor == nullptr
        ? nullptr : document::findOperation(document, descriptor->producer);
      if (producer == nullptr) return std::nullopt;
      const auto* constant = std::get_if<document::ConstantOperation>(&producer->data);
      return constant == nullptr ? std::nullopt : std::optional{constant->value};
    };
    const auto publish = [&](const document::Operation& operation,
        const std::size_t renderTargetIndex, const GLuint primaryTexture, const GLuint auxiliaryTexture,
        const glm::ivec2 extent) {
      const bool rendered = std::holds_alternative<document::RenderOperation>(operation.data);
      for (const document::SignalDescriptor& descriptor : operation.outputs) {
        evaluation::SignalResource resource;
        resource.descriptor = descriptor;
        resource.revision = revision;
        if (descriptor.metadata.domain == document::SignalDomain::Screen2D) {
          resource.extent = {extent.x, extent.y, 1};
          resource.descriptor.metadata.extent = resource.extent;
        }
        if (rendered && descriptor.metadata.semantic == document::SignalSemantic::Normal) {
            resource.textures[0] = passTargets_[renderTargetIndex].normalTexture;
            resource.textureCount = resource.textures[0] == 0 ? 0 : 1;
        } else if (rendered && descriptor.metadata.semantic == document::SignalSemantic::DeviceDepth) {
            resource.textures[0] = passTargets_[renderTargetIndex].depthTexture;
            resource.textureCount = resource.textures[0] == 0 ? 0 : 1;
        } else if (rendered &&
            (descriptor.metadata.semantic == document::SignalSemantic::FieldStrength ||
             descriptor.metadata.semantic == document::SignalSemantic::SignedDistance)) {
            resource.textures[0] = passTargets_[renderTargetIndex].fieldTexture;
            resource.textureCount = resource.textures[0] == 0 ? 0 : 1;
        } else if (rendered && descriptor.shape == document::SignalShape::Spectrum16) {
            resource.textures = passTargets_[renderTargetIndex].spectralTextures;
            resource.textureCount = resource.textures[0] == 0 ? 0 : 4;
        } else if (descriptor.metadata.semantic == document::SignalSemantic::EdgeDirection) {
            resource.textures[0] = auxiliaryTexture;
            resource.textureCount = auxiliaryTexture == 0 ? 0 : 1;
        } else {
            resource.textures[0] = primaryTexture;
            resource.textureCount = primaryTexture == 0 ? 0 : 1;
        }
        signals.publish(std::move(resource));
        if (rendered) targetBySignal[descriptor.id] = renderTargetIndex;
      }
    };
    const auto evaluatedOperation = [&](const document::Operation& source) {
      document::Operation result = source;
      RenderPass carrier;
      carrier.enabled = source.enabled;
      if (const auto* data = std::get_if<document::CompositeOperation>(&source.data)) {
        carrier.composite.interpretationA = data->interpretationA;
        carrier.composite.interpretationB = data->interpretationB;
        carrier.composite.observerExposureStops = data->observer.exposureStops;
        carrier.composite.rodSensitivity = data->observer.rodSensitivity;
        carrier.composite.opponentGain = data->observer.opponentGain;
        carrier.composite.operation = data->arithmetic.operation;
        carrier.composite.gain = data->arithmetic.gain;
        carrier.composite.bias = data->arithmetic.bias;
        carrier.composite.opacity = data->arithmetic.opacity;
        carrier.composite.bitDepth = data->arithmetic.bitDepth;
        carrier.composite.colorSpace = data->arithmetic.colorSpace;
        carrier.composite.range = data->arithmetic.range;
        carrier.composite.invertMask = data->invertMask;
        if (data->feedback.has_value()) {
          carrier.composite.historyDecay = data->feedback->decay;
          carrier.composite.historyUvOffset = data->feedback->uvOffset;
          carrier.composite.historyUvScale = data->feedback->uvScale;
        }
      } else if (const auto* data = std::get_if<document::InterpretOperation>(&source.data)) {
        carrier.composite.interpretationA = data->observer;
        carrier.composite.gain = data->gain;
        carrier.composite.bias = data->bias;
      } else if (const auto* data = std::get_if<document::StereoOperation>(&source.data)) {
        carrier.stereoAnalysis = data->mode;
        carrier.stereoMaximumDisparityPixels = data->maximumDisparityPixels;
        carrier.stereoOcclusionTolerance = data->occlusionTolerance;
      }
      applyTracks(carrier, document::operationObject(source.id));
      result.enabled = carrier.enabled;
      if (auto* data = std::get_if<document::CompositeOperation>(&result.data)) {
        data->interpretationA = carrier.composite.interpretationA;
        data->interpretationB = carrier.composite.interpretationB;
        data->observer = {carrier.composite.observerExposureStops,
          carrier.composite.rodSensitivity, carrier.composite.opponentGain};
        data->arithmetic = {carrier.composite.operation, carrier.composite.gain,
          carrier.composite.bias, carrier.composite.opacity, carrier.composite.bitDepth,
          carrier.composite.colorSpace, carrier.composite.range};
        data->invertMask = carrier.composite.invertMask;
        if (data->feedback.has_value()) data->feedback = document::FeedbackSettings{
          carrier.composite.historyDecay, carrier.composite.historyUvOffset,
          carrier.composite.historyUvScale};
      } else if (auto* data = std::get_if<document::InterpretOperation>(&result.data)) {
        data->observer = carrier.composite.interpretationA;
        data->gain = carrier.composite.gain;
        data->bias = carrier.composite.bias;
      } else if (auto* data = std::get_if<document::StereoOperation>(&result.data)) {
        data->mode = carrier.stereoAnalysis;
        data->maximumDisparityPixels = carrier.stereoMaximumDisparityPixels;
        data->occlusionTolerance = carrier.stereoOcclusionTolerance;
      }
      return result;
    };

    for (std::size_t nodeIndex = 0; nodeIndex < plan.nodes.size(); ++nodeIndex) {
      const document::Operation* authored = document::findOperation(document,
        plan.nodes[nodeIndex].operation);
      if (authored == nullptr) continue;
      const document::Operation evaluated = evaluatedOperation(*authored);
      const document::Operation* operation = &evaluated;
      const auto releaseTransientSignals = [&] {
        for (const document::SignalId& signal : transientSignalsEndingAt[nodeIndex])
          signals.erase(signal);
      };
      if (!operation->enabled) {
        releaseTransientSignals();
        continue;
      }
      const std::size_t renderSlot = renderSlots.contains(operation->id)
        ? renderSlots.at(operation->id) : 0;
      const std::size_t relationSlot = relationSlots.contains(operation->id)
        ? relationSlots.at(operation->id) : 0;
      const std::size_t directionSlot = directionSlots.contains(operation->id)
        ? directionSlots.at(operation->id) : 0;
      GLuint output = 0;
      GLuint auxiliaryOutput = 0;
      glm::ivec2 outputExtent(relationWidth_, relationHeight_);
      if (const auto* data = std::get_if<document::RenderOperation>(&operation->data)) {
        RenderPass pass = renderState(*operation, *data);
        document::TimeTransform time = data->time;
        for (const document::AnimationTrack& track : document.automation.animation) {
          if (track.target.owner != document::operationObject(operation->id) || track.keyframes.empty()) continue;
          const PropertyAnimationTrack sampled{AnimationProperty::Ambient,
            track.interpolation, track.keyframes};
          if (track.target.property == document::timeScaleProperty())
            time.scale = samplePropertyTrack(sampled, timeSeconds).x;
          else if (track.target.property == document::timeOffsetProperty())
            time.offsetSeconds = samplePropertyTrack(sampled, timeSeconds).x;
        }
        normalizeForHardwareProfile(document.hardwareProfile, pass.renderer);
        output = render(pass.renderer, document.scene.authoredCamera, document.scene.testScene,
          renderSlot, pass.perturbation, pass.output, pass.textureSource,
          pass.importedTexture.get(), pass.importedTextureSrgb, time.apply(timeSeconds));
        outputExtent = {passTargets_[renderSlot].width, passTargets_[renderSlot].height};
      } else if (std::holds_alternative<document::ConstantOperation>(operation->data)) {
        for (const document::SignalDescriptor& descriptor : operation->outputs) {
          evaluation::SignalResource resource;
          resource.descriptor = descriptor;
          resource.revision = revision;
          signals.publish(std::move(resource));
        }
        releaseTransientSignals();
        continue;
      } else if (const auto* data = std::get_if<document::InterpretOperation>(&operation->data)) {
        CompositeStep step;
        step.sourceA = CompositeSource::RenderPassSpectrum;
        step.sourceB = CompositeSource::RenderPassSpectrum;
        step.interpretationA = data->observer;
        step.interpretationB = data->observer;
        step.observerExposureStops = data->exposureStops;
        step.operation = RelationOperator::Maximum;
        step.gain = data->gain;
        step.bias = data->bias;
        step.opacity = 1.0f;
        const auto spectrum = signalSpectrum(data->spectrum);
        outputExtent = signalExtent(data->spectrum);
        output = compositeTextures(signalTexture(data->spectrum), signalTexture(data->spectrum),
          0, 0, 0, spectrum, spectrum, RendererState{}, step, relationSlot,
          outputExtent.x, outputExtent.y);
      } else if (const auto* data = std::get_if<document::CompositeOperation>(&operation->data)) {
        CompositeStep step;
        step.sourceA = data->a.frameOffset < 0 ? CompositeSource::PreviousFrame : CompositeSource::RenderPass;
        step.sourceB = data->b.frameOffset < 0 ? CompositeSource::PreviousFrame : CompositeSource::RenderPass;
        if (data->a.frameOffset >= 0 && isScalarImage(data->a)) step.sourceA = CompositeSource::RenderPassField;
        if (data->b.frameOffset >= 0 && isScalarImage(data->b)) step.sourceB = CompositeSource::RenderPassField;
        if (signalDescriptor(data->a) != nullptr &&
            signalDescriptor(data->a)->shape == document::SignalShape::Spectrum16)
          step.sourceA = CompositeSource::RenderPassSpectrum;
        if (signalDescriptor(data->b) != nullptr &&
            signalDescriptor(data->b)->shape == document::SignalShape::Spectrum16)
          step.sourceB = CompositeSource::RenderPassSpectrum;
        const std::optional<glm::vec4> constantA = constantValue(data->a);
        const std::optional<glm::vec4> constantB = constantValue(data->b);
        if (constantA.has_value()) { step.sourceA = CompositeSource::FixedColor; step.fixedColor = *constantA; }
        if (constantB.has_value()) { step.sourceB = CompositeSource::FixedColor; step.fixedColor = *constantB; }
        step.interpretationA = data->interpretationA;
        step.interpretationB = data->interpretationB;
        step.observerExposureStops = data->observer.exposureStops;
        step.rodSensitivity = data->observer.rodSensitivity;
        step.opponentGain = data->observer.opponentGain;
        step.operation = data->arithmetic.operation;
        step.gain = data->arithmetic.gain;
        step.bias = data->arithmetic.bias;
        step.opacity = data->arithmetic.opacity;
        step.bitDepth = data->arithmetic.bitDepth;
        step.colorSpace = data->arithmetic.colorSpace;
        step.range = data->arithmetic.range;
        step.mask = CompositeMask::None;
        step.invertMask = data->invertMask;
        if (data->feedback.has_value()) {
          step.historyDecay = data->feedback->decay;
          step.historyUvOffset = data->feedback->uvOffset;
          step.historyUvScale = data->feedback->uvScale;
        }
        outputExtent = signalExtent(data->a);
        output = compositeTextures(signalTexture(data->a), signalTexture(data->b),
          signalTexture(data->mask), 0, 0,
          signalSpectrum(data->a), signalSpectrum(data->b), document.renderDefaults.renderer,
          step, relationSlot, outputExtent.x, outputExtent.y);
      } else if (const auto* data = std::get_if<document::StereoOperation>(&operation->data)) {
        const RenderTarget* left = signalTarget(data->left);
        const RenderTarget* right = signalTarget(data->right);
        if (left != nullptr && right != nullptr) {
          RenderPass settings;
          settings.stereoAnalysis = data->mode;
          settings.stereoMaximumDisparityPixels = data->maximumDisparityPixels;
          settings.stereoOcclusionTolerance = data->occlusionTolerance;
          output = analyzeStereo(*left, *right, settings, relationSlot);
          outputExtent = {left->width, left->height};
        }
      } else if (const auto* data = std::get_if<document::MeasureOperation>(&operation->data)) {
        output = signalTexture(data->input);
        outputExtent = signalExtent(data->input);
      } else if (const auto* data = std::get_if<document::LuminanceOperation>(&operation->data)) {
        outputExtent = signalExtent(data->input);
        output = processImage(signalTexture(data->input), 0, false, {}, {}, {}, relationSlot,
          false, 0, outputExtent.x, outputExtent.y);
      } else if (const auto* data = std::get_if<document::RemapOperation>(&operation->data)) {
        outputExtent = signalExtent(data->input);
        output = processImage(signalTexture(data->input), 1, true,
          {data->inputLow, data->inputHigh, 0.0f, data->clamp ? 1.0f : 0.0f},
          glm::vec4(data->outputLow), glm::vec4(data->outputHigh), relationSlot,
          false, 0, outputExtent.x, outputExtent.y);
      } else if (const auto* data = std::get_if<document::EdgeOperation>(&operation->data)) {
        outputExtent = signalExtent(data->input);
        output = processImage(signalTexture(data->input), 2, isScalarImage(data->input),
          glm::vec4(data->strength, 0.0f, 0.0f, 0.0f), {}, {}, relationSlot,
          false, 0, outputExtent.x, outputExtent.y);
        auxiliaryOutput = processImage(signalTexture(data->input), 6, isScalarImage(data->input),
          {}, {}, {}, directionSlot, true, 0, outputExtent.x, outputExtent.y);
      } else if (const auto* data = std::get_if<document::BlurOperation>(&operation->data)) {
        outputExtent = signalExtent(data->input);
        output = processImage(signalTexture(data->input), 3,
          data->outputShape == document::SignalShape::Scalar,
          glm::vec4(data->radiusPixels, 0.0f, 0.0f, 0.0f), {}, {}, relationSlot,
          false, 0, outputExtent.x, outputExtent.y);
      } else if (const auto* data = std::get_if<document::ThresholdOperation>(&operation->data)) {
        outputExtent = signalExtent(data->input);
        output = processImage(signalTexture(data->input), 4, true,
          {data->threshold, data->softness, 0.0f, 0.0f}, {}, {}, relationSlot,
          false, 0, outputExtent.x, outputExtent.y);
      } else if (const auto* data = std::get_if<document::GradientMapOperation>(&operation->data)) {
        outputExtent = signalExtent(data->input);
        output = processImage(signalTexture(data->input), 5, true, {},
          data->lowColor, data->highColor, relationSlot, false, 0,
          outputExtent.x, outputExtent.y);
      } else if (const auto* data = std::get_if<document::WarpOperation>(&operation->data)) {
        outputExtent = signalExtent(data->image);
        output = processImage(signalTexture(data->image), 7, false,
          glm::vec4(data->strengthPixels, 0.0f, 0.0f, 0.0f), {}, {}, relationSlot, false,
          signalTexture(data->displacement), outputExtent.x, outputExtent.y);
      }
      publish(*operation, renderSlot, output, auxiliaryOutput, outputExtent);
      releaseTransientSignals();
    }
    const GLuint finalTexture = signals.displayTexture(document.presentation.input.id);
    if (finalTexture != 0) copyToFrameHistory(finalTexture);
    return finalTexture;
  }

  void resetFrameHistory() {
    glBindFramebuffer(GL_FRAMEBUFFER, historyFbo_);
    glViewport(0, 0, relationWidth_, relationHeight_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  GLuint texturePreview(const TextureAsset* texture) { return overrideTexture(texture); }

  GLuint previewSignal(const evaluation::SignalResource& resource, const RendererState& state,
      const std::size_t targetIndex) {
    if (resource.textureCount == 0) return 0;
    const document::SignalDescriptor& descriptor = resource.descriptor;
    int mode = 0;
    if (descriptor.metadata.semantic == document::SignalSemantic::DeviceDepth) mode = 1;
    else if (descriptor.metadata.encoding == document::SignalEncoding::Signed &&
        descriptor.shape == document::SignalShape::Scalar) mode = 2;
    else if (descriptor.shape == document::SignalShape::Vector2) mode = 3;
    else if (descriptor.shape == document::SignalShape::Spectrum16) mode = 4;
    else if (descriptor.metadata.semantic == document::SignalSemantic::Normal) mode = 5;
    if (mode == 0) return resource.textures[0];
    const std::size_t index = std::min(targetIndex, previewFbos_.size() - 1);
    glBindFramebuffer(GL_FRAMEBUFFER, previewFbos_[index]);
    glViewport(0, 0, relationWidth_, relationHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(previewProgram_);
    for (int texture = 0; texture < 4; ++texture) {
      glActiveTexture(GL_TEXTURE0 + texture);
      glBindTexture(GL_TEXTURE_2D, resource.textures[static_cast<std::size_t>(texture)]);
      const std::string name = "uImage" + std::to_string(texture);
      glUniform1i(glGetUniformLocation(previewProgram_, name.c_str()), texture);
    }
    glUniform1i(glGetUniformLocation(previewProgram_, "uMode"), mode);
    glUniform1f(glGetUniformLocation(previewProgram_, "uNearPlane"), state.camera.nearPlane);
    glUniform1f(glGetUniformLocation(previewProgram_, "uFarPlane"), 100.0f);
    glUniform1i(glGetUniformLocation(previewProgram_, "uOrthographic"), state.camera.orthographic);
    const glm::vec2 range = descriptor.metadata.hasKnownRange
      ? descriptor.metadata.knownRange : glm::vec2(-1.0f, 1.0f);
    glUniform2fv(glGetUniformLocation(previewProgram_, "uRange"), 1, glm::value_ptr(range));
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return previewTextures_[index];
  }

  GLuint reconstructDisplay(const GLuint sourceTexture, const DisplayReconstructionState& state,
      const std::size_t targetIndex) {
    if (!state.enabled || sourceTexture == 0) return sourceTexture;
    const std::size_t index = std::min(targetIndex, displayFbos_.size() - 1);
    glBindFramebuffer(GL_FRAMEBUFFER, displayFbos_[index]);
    glViewport(0, 0, relationWidth_, relationHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(displayProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glUniform1i(glGetUniformLocation(displayProgram_, "uImage"), 0);
    glUniform1i(glGetUniformLocation(displayProgram_, "uSignal"), static_cast<int>(state.signal));
    glUniform1f(glGetUniformLocation(displayProgram_, "uChromaBleed"), state.chromaBleed);
    glUniform1f(glGetUniformLocation(displayProgram_, "uCrosstalk"), state.lumaChromaCrosstalk);
    glUniform1f(glGetUniformLocation(displayProgram_, "uScanlineStrength"), state.scanlineStrength);
    glUniform1f(glGetUniformLocation(displayProgram_, "uMaskStrength"), state.phosphorMaskStrength);
    glUniform1f(glGetUniformLocation(displayProgram_, "uBloomStrength"), state.bloomStrength);
    glUniform1f(glGetUniformLocation(displayProgram_, "uBloomRadius"), state.bloomRadiusPixels);
    glUniform1f(glGetUniformLocation(displayProgram_, "uObserverExposureStops"), state.observerExposureStops);
    glUniform1f(glGetUniformLocation(displayProgram_, "uDarkAdaptation"), state.darkAdaptation);
    glUniform1f(glGetUniformLocation(displayProgram_, "uRodSensitivity"), state.rodSensitivity);
    glUniform1f(glGetUniformLocation(displayProgram_, "uOpponentGain"), state.opponentGain);
    glUniform1i(glGetUniformLocation(displayProgram_, "uReceptorXorBits"), state.receptorXorBits);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return displayTextures_[index];
  }

private:
  struct ImportedMaterialGpu {
    glm::vec4 baseColor{1.0f};
    GLuint baseColorTexture = 0;
  };

  struct ImportedSubmeshGpu {
    MeshRange range;
    std::size_t materialIndex = 0;
  };

  GLuint uploadTexture(const TextureAsset& texture) {
    GLuint id = 0;
    glGenTextures(1, &id);
    glBindTexture(GL_TEXTURE_2D, id);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, texture.width, texture.height, 0, GL_RGBA,
      GL_UNSIGNED_BYTE, texture.rgba8.data());
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
    glGenerateMipmap(GL_TEXTURE_2D);
    return id;
  }

  GLuint overrideTexture(const TextureAsset* texture) {
    if (texture == nullptr) return 0;
    const auto existing = overrideTextures_.find(texture->contentHash);
    if (existing != overrideTextures_.end()) return existing->second;
    const GLuint uploaded = uploadTexture(*texture);
    overrideTextures_.emplace(texture->contentHash, uploaded);
    return uploaded;
  }

  void clearImportedMaterialResources() {
    if (!importedTextureIds_.empty())
      glDeleteTextures(static_cast<GLsizei>(importedTextureIds_.size()), importedTextureIds_.data());
    importedTextureIds_.clear();
    importedMaterials_.clear();
    importedSubmeshes_.clear();
  }

  void uploadGeometry(const std::vector<Vertex>* importedVertices) {
    std::vector<Vertex> combined = baseVertices_;
    imported_ = {static_cast<int>(combined.size()), 0};
    if (importedVertices != nullptr) {
      imported_.count = static_cast<int>(importedVertices->size());
      combined.insert(combined.end(), importedVertices->begin(), importedVertices->end());
    }
    glBindVertexArray(vao_);
    glBindBuffer(GL_ARRAY_BUFFER, vbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(combined.size() * sizeof(Vertex)), combined.data(),
      GL_STATIC_DRAW);
  }

  void copyToFrameHistory(const GLuint sourceTexture) {
    glBindFramebuffer(GL_FRAMEBUFFER, historyFbo_);
    glViewport(0, 0, relationWidth_, relationHeight_);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_BLEND);
    glUseProgram(copyProgram_);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, sourceTexture);
    glUniform1i(glGetUniformLocation(copyProgram_, "uImage"), 0);
    glBindVertexArray(fullscreenVao_);
    glDrawArrays(GL_TRIANGLES, 0, 3);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }

  GLuint sceneProgram_ = 0, outputProgram_ = 0, relationProgram_ = 0,
    imageOperationProgram_ = 0, stereoAnalysisProgram_ = 0, fieldProgram_ = 0,
    sdfIsoProgram_ = 0, spectralProgram_ = 0,
    copyProgram_ = 0, previewProgram_ = 0, displayProgram_ = 0,
    shadowProgram_ = 0, overdrawProgram_ = 0;
  GLuint vao_ = 0, vbo_ = 0, fullscreenVao_ = 0, checkerTexture_ = 0, indexedTexture_ = 0,
    clutTexture_ = 0, normalTexture_ = 0, detailTexture_ = 0, whiteTexture_ = 0;
  ElementalSimulation elementalSimulation_;
  GLuint simulationMatterTexture_ = 0, simulationDynamicsTexture_ = 0;
  unsigned long long uploadedSimulationRevision_ = 0;
  GLint maxSamples_ = 1;
  GLfloat maxAnisotropy_ = 1.0f;
  std::vector<Vertex> baseVertices_;
  MeshRange torus_, denseTorus_, plane_, quad_, lowSphere_, smoothSphere_, imported_;
  std::vector<GLuint> importedTextureIds_;
  std::vector<ImportedMaterialGpu> importedMaterials_;
  std::vector<ImportedSubmeshGpu> importedSubmeshes_;
  std::unordered_map<std::uint64_t, GLuint> overrideTextures_;
  GLuint shadowFbo_ = 0, shadowTexture_ = 0;
  int shadowResolution_ = 0;
  std::vector<RenderTarget> passTargets_;
  std::vector<GLuint> relationFbos_;
  std::vector<GLuint> relationTextures_;
  std::vector<glm::ivec2> relationExtents_;
  std::vector<GLuint> directionFbos_;
  std::vector<GLuint> directionTextures_;
  std::vector<glm::ivec2> directionExtents_;
  GLuint historyFbo_ = 0, historyTexture_ = 0;
  std::array<GLuint, 3> displayFbos_{};
  std::array<GLuint, 3> displayTextures_{};
  std::array<GLuint, 3> previewFbos_{};
  std::array<GLuint, 3> previewTextures_{};
  static constexpr int relationWidth_ = 960;
  static constexpr int relationHeight_ = 720;

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

unsigned int Renderer::renderRelation(RelationOperator operation, float gain, float bias) {
  return impl_->renderRelation(operation, gain, bias);
}
unsigned int Renderer::compareSignals(const unsigned int a, const unsigned int b,
    const RelationOperator operation, const float gain, const float bias) {
  return impl_->compareSignals(a, b, operation, gain, bias);
}

unsigned int Renderer::renderPass(const RenderPass& pass, const CameraOrbit& camera, const TestScene scene,
    const std::size_t targetIndex) {
  return impl_->render(pass.renderer, camera, scene, targetIndex, pass.perturbation, pass.output,
    pass.textureSource, pass.importedTexture.get(), pass.importedTextureSrgb);
}

unsigned int Renderer::evaluate(const document::Document& document,
    const evaluation::EvaluationPlan& plan, evaluation::SignalRegistry& signals,
    const std::uint64_t revision, const float timeSeconds) {
  return impl_->evaluate(document, plan, signals, revision, timeSeconds);
}
unsigned int Renderer::previewSignal(const evaluation::SignalResource& resource,
    const RendererState& state, const std::size_t targetIndex) {
  return impl_->previewSignal(resource, state, targetIndex);
}
unsigned int Renderer::texturePreview(const TextureAsset* texture) { return impl_->texturePreview(texture); }
unsigned int Renderer::reconstructDisplay(const unsigned int sourceTexture,
    const DisplayReconstructionState& state, const std::size_t targetIndex) {
  return impl_->reconstructDisplay(sourceTexture, state, targetIndex);
}
void Renderer::updateElementalSimulation(const float deltaSeconds, const RendererState& state,
    const TestScene scene) { impl_->updateElementalSimulation(deltaSeconds, state, scene); }
void Renderer::resetElementalSimulation() { impl_->resetElementalSimulation(); }
void Renderer::resetFrameHistory() { impl_->resetFrameHistory(); }

void Renderer::setImportedModel(const ModelAsset& asset) { impl_->setImportedModel(asset); }
void Renderer::clearImportedModel() { impl_->clearImportedModel(); }

} // namespace gfxlab
