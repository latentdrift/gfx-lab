#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "app/State.hpp"
#include "handbook/Handbook.hpp"
#include "renderer/Renderer.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace gfxlab;

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "graphics-lab: %s\n", message.c_str());
  std::exit(EXIT_FAILURE);
}

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
      ImGui::Checkbox("Polygon offset fill", &state.rasterization.polygonOffset);
      ImGui::BeginDisabled(!state.rasterization.polygonOffset);
      ImGui::SliderFloat("Slope factor", &state.rasterization.polygonOffsetFactor, -4.0f, 4.0f, "%.2f");
      ImGui::SliderFloat("Constant units", &state.rasterization.polygonOffsetUnits, -8.0f, 8.0f, "%.2f");
      ImGui::EndDisabled();
      description("Offsets generated depth values by a slope-dependent term plus a minimum-depth-step term.");
      break;
    }
    case Category::Surface: {
      ImGui::TextUnformatted("SURFACE"); ImGui::Separator();
      ImGui::TextUnformatted("Surface visualization");
      const char* visualizationLabels[] = {"Texture", "UV coordinates", "Normals", "Vertex colors", "Tangents", "Bitangents"};
      ImGui::Combo("##visualization", &state.surface.visualization, visualizationLabels, 6);
      description("Selects the mesh attribute used as the surface's base color.");
      ImGui::TextUnformatted("Shading interpolation");
      bool flat = !state.surface.smoothShading;
      if (radioPair("Smooth", "Flat", flat)) state.surface.smoothShading = !flat;
      description("Smooth shading interpolates vertex normals; flat shading uses one face normal per triangle.");
      ImGui::Checkbox("Wireframe overlay", &state.surface.wireframe);
      description("Draws triangle boundaries over the shaded surface.");
      ImGui::Checkbox("Tangent-space normal mapping", &state.surface.normalMapping);
      ImGui::BeginDisabled(!state.surface.normalMapping);
      ImGui::SliderFloat("Normal-map strength", &state.surface.normalStrength, 0.0f, 2.0f, "%.2f");
      ImGui::EndDisabled();
      description("Transforms sampled tangent-space normals into world space with the tangent-bitangent-normal basis.");
      ImGui::TextUnformatted("Transparency operation");
      const char* transparencyLabels[] = {"Opaque", "Alpha test (discard)", "Straight alpha blend",
        "Premultiplied alpha blend", "Additive blend", "Multiply blend"};
      ImGui::Combo("##transparency", &state.surface.transparency, transparencyLabels, 6);
      if (state.surface.transparency == 1)
        ImGui::SliderFloat("Alpha cutoff", &state.surface.alphaCutoff, 0.0f, 1.0f, "%.2f");
      description("Each blend mode configures explicit source and destination factors in the framebuffer blend equation.");
      ImGui::Checkbox("Reverse object draw order", &state.surface.reverseDrawOrder);
      description("Transparent surfaces generally require back-to-front submission because blending is order-dependent.");
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
      ImGui::Checkbox("Directional shadow map", &state.lighting.shadows);
      ImGui::BeginDisabled(!state.lighting.shadows);
      const char* shadowResolutionLabels[] = {"256 x 256", "512 x 512", "1024 x 1024", "2048 x 2048"};
      const int shadowResolutions[] = {256, 512, 1024, 2048};
      int shadowResolutionIndex = state.lighting.shadowResolution == 256 ? 0 : state.lighting.shadowResolution == 512 ? 1 :
        state.lighting.shadowResolution == 2048 ? 3 : 2;
      if (ImGui::Combo("Shadow-map resolution", &shadowResolutionIndex, shadowResolutionLabels, 4))
        state.lighting.shadowResolution = shadowResolutions[shadowResolutionIndex];
      ImGui::SliderFloat("Depth comparison bias", &state.lighting.shadowBias, 0.0f, 0.02f, "%.5f", ImGuiSliderFlags_Logarithmic);
      ImGui::Checkbox("3 x 3 percentage-closer filtering", &state.lighting.shadowPcf);
      ImGui::Checkbox("Visualize light-space depth", &state.lighting.visualizeShadowMap);
      ImGui::EndDisabled();
      description("Renders scene depth from the light, then compares each camera fragment against that depth map.");
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
    case Category::Stencil:
      ImGui::TextUnformatted("STENCIL"); ImGui::Separator();
      ImGui::Checkbox("Two-pass stencil mask", &state.stencil.enabled);
      description("First pass writes a projected sphere silhouette while color and depth writes are disabled.");
      ImGui::SliderInt("Reference value", &state.stencil.reference, 0, 255);
      radioPair("Equal", "Not equal", state.stencil.invert);
      description("Second pass compares each stored 8-bit stencil value against the reference before shading.");
      ImGui::TextDisabled("Pass 1: Always / Replace");
      ImGui::TextDisabled("Pass 2: Equal or Not equal / Keep");
      ImGui::TextDisabled("Attachment: DEPTH24_STENCIL8");
      description("Select the Stencil mask scene to isolate this operation.");
      break;
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
      ImGui::Checkbox("Overdraw visualization", &state.post.overdraw);
      ImGui::BeginDisabled(!state.post.overdraw);
      ImGui::SliderFloat("Heat-map maximum", &state.post.overdrawRange, 1.0f, 32.0f, "%.0f fragments");
      ImGui::EndDisabled();
      description("An additive floating-point pass counts rasterized fragments with depth testing disabled.");
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
    case Category::Stencil: return "Stencil";
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
  double configCopiedAt = -10.0;
  handbook::Handbook graphicsHandbook;

  if (std::getenv("GRAPHICS_LAB_VALIDATE_HANDBOOK")) {
    constexpr std::array examples = {handbook::Example::VertexQuantization, handbook::Example::Projection,
      handbook::Example::AffineMapping, handbook::Example::TextureMinification, handbook::Example::NormalMapping,
      handbook::Example::LightingInterpolation, handbook::Example::DepthPrecision, handbook::Example::Transparency,
      handbook::Example::Stencil, handbook::Example::LinearLight, handbook::Example::ColorQuantization,
      handbook::Example::InternalResolution, handbook::Example::ShadowMapping, handbook::Example::Overdraw};
    for (handbook::Example example : examples) {
      for (bool alternative : {false, true}) {
        applyHandbookExample(example, alternative, current, camera, scene, category);
        renderer.render(current, camera, scene, alternative);
      }
    }
    current = RendererState{};
    reference = current;
    camera = CameraOrbit{};
    scene = TestScene::Torus;
    category = Category::Geometry;
  }

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
    const char* sceneLabels[] = {"Torus", "Texture minification", "Depth precision", "Transparency", "Lighting comparison", "Stencil mask"};
    int sceneIndex = static_cast<int>(scene);
    if (ImGui::Combo("##test-scene", &sceneIndex, sceneLabels, 6)) scene = static_cast<TestScene>(sceneIndex);
    ImGui::SameLine();
    if (ImGui::Button("Reset neutral")) current = RendererState{};
    ImGui::SameLine();
    if (ImGui::Button("Reset scene setup")) applyRecommendedSetup(scene, current, camera);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Apply the recommended renderer state and camera framing for the selected scene.");
    ImGui::SameLine();
    if (ImGui::Button("Copy A to B")) reference = current;
    ImGui::SameLine();
    if (ImGui::Button("Copy config JSON")) {
      const std::string exportedConfig = configJson(current, camera, scene);
      ImGui::SetClipboardText(exportedConfig.c_str());
      configCopiedAt = glfwGetTime();
    }
    if (glfwGetTime() - configCopiedAt < 2.0) {
      ImGui::SameLine();
      ImGui::TextDisabled("Copied");
    }
    ImGui::SameLine();
    if (ImGui::Button("Handbook")) graphicsHandbook.open();
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
    constexpr std::array<Category, 11> categories = {Category::Geometry, Category::Camera, Category::Rasterization,
      Category::Surface, Category::Texture, Category::Lighting, Category::Depth, Category::Stencil, Category::Color,
      Category::Post, Category::Output};
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

    const handbook::Action handbookAction = graphicsHandbook.draw();
    if (handbookAction.type == handbook::ActionType::ApplyToA) {
      applyHandbookExample(handbookAction.example, false, current, camera, scene, category);
    } else if (handbookAction.type == handbook::ActionType::ApplyToB) {
      RendererState exampleState;
      applyHandbookExample(handbookAction.example, true, exampleState, camera, scene, category);
      reference = exampleState;
    } else if (handbookAction.type == handbook::ActionType::LoadComparison) {
      applyHandbookExample(handbookAction.example, false, current, camera, scene, category);
      RendererState comparisonState;
      CameraOrbit comparisonCamera = camera;
      TestScene comparisonScene = scene;
      Category comparisonCategory = category;
      applyHandbookExample(handbookAction.example, true, comparisonState, comparisonCamera, comparisonScene, comparisonCategory);
      reference = comparisonState;
      camera = comparisonCamera;
      scene = comparisonScene;
      category = comparisonCategory;
      compare = CompareMode::Split;
    }

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
