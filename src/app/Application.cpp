#include "app/Application.hpp"

#include <GL/glew.h>
#include <GLFW/glfw3.h>

#include <imgui.h>
#include <imgui_impl_glfw.h>
#include <imgui_impl_opengl3.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "app/State.hpp"
#include "app/HardwareProfile.hpp"
#include "handbook/Handbook.hpp"
#include "renderer/Renderer.hpp"
#include "ui/Inspector.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

using namespace gfxlab;
using namespace gfxlab::ui;

[[noreturn]] void fail(const std::string& message) {
  std::fprintf(stderr, "graphics-lab: %s\n", message.c_str());
  std::exit(EXIT_FAILURE);
}


void glfwError(int, const char* descriptionText) { std::fprintf(stderr, "GLFW: %s\n", descriptionText); }

bool isNintendo64Example(handbook::Example example) {
  return example >= handbook::Example::N64ThreePoint && example <= handbook::Example::N64VideoInterface;
}

constexpr std::array<const char*, 12> relationLabels = {
  "Absolute difference", "Signed A - B", "Positive A - B", "Positive B - A", "Multiply", "Screen",
  "Exclusion", "Minimum", "Maximum", "A x (1 - B)", "Centered sum", "Relative A / B"
};

constexpr std::array<const char*, 12> relationEquations = {
  "|A - B|", "A - B", "max(A - B, 0)", "max(B - A, 0)", "A x B", "1 - (1 - A)(1 - B)",
  "A + B - 2AB", "min(A, B)", "max(A, B)", "A(1 - B)", "A + B - 1", "A / max(B, 1/255) - 1"
};

constexpr std::array<const char*, 12> relationMeanings = {
  "Black means agreement; each RGB channel measures disagreement magnitude.",
  "Middle gray means agreement; excursions encode which rendering has the larger channel value.",
  "Shows only channel energy present in A beyond B.",
  "Shows only channel energy present in B beyond A.",
  "Keeps color supported by both renderings and darkens disagreement.",
  "Combines bright contributions from either rendering.",
  "Dark where equal at black or white; bright where the renderings oppose each other.",
  "Keeps only the lower channel value shared by the two renderings.",
  "Keeps the higher channel value supplied by either rendering.",
  "Shows A where B is absent; black where B fully occupies the channel.",
  "With 0.5 bias, middle gray means the two channel values sum to one.",
  "With 0.5 bias, middle gray means A equals B; ratios near black in B clip aggressively."
};

void resetRelationTransform(RelationOperator operation, float& gain, float& bias) {
  switch (operation) {
  case RelationOperator::AbsoluteDifference: gain = 4.0f; bias = 0.0f; break;
  case RelationOperator::SignedDifference: gain = 2.0f; bias = 0.5f; break;
  case RelationOperator::PositiveAMinusB:
  case RelationOperator::PositiveBMinusA: gain = 4.0f; bias = 0.0f; break;
  case RelationOperator::CenteredSum: gain = 1.0f; bias = 0.5f; break;
  case RelationOperator::RelativeDifference: gain = 0.5f; bias = 0.5f; break;
  default: gain = 1.0f; bias = 0.0f; break;
  }
}

} // namespace

namespace gfxlab {

int runApplication() {
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
  HardwareProfile hardwareProfile = HardwareProfile::Unrestricted;
  RelationOperator relationOperator = RelationOperator::AbsoluteDifference;
  float relationGain = 4.0f;
  float relationBias = 0.0f;
  bool viewportHovered = false;
  double configCopiedAt = -10.0;
  handbook::Handbook graphicsHandbook;

  if (std::getenv("GRAPHICS_LAB_VALIDATE_HANDBOOK")) {
    constexpr std::array examples = {handbook::Example::VertexQuantization, handbook::Example::Projection,
      handbook::Example::AffineMapping, handbook::Example::TextureMinification, handbook::Example::NormalMapping,
      handbook::Example::LightingInterpolation, handbook::Example::DepthPrecision, handbook::Example::Transparency,
      handbook::Example::Stencil, handbook::Example::LinearLight, handbook::Example::ColorQuantization,
      handbook::Example::InternalResolution, handbook::Example::ShadowMapping, handbook::Example::Overdraw,
      handbook::Example::ClutTextures, handbook::Example::VertexDepthCue,
      handbook::Example::Ps1Semitransparency, handbook::Example::OrderingTable,
      handbook::Example::N64ThreePoint, handbook::Example::N64Combiner,
      handbook::Example::N64TextureFormats, handbook::Example::N64Mipmap,
      handbook::Example::N64Coverage, handbook::Example::N64VideoInterface};
    for (handbook::Example example : examples) {
      for (bool alternative : {false, true}) {
        applyHandbookExample(example, alternative, current, camera, scene, category);
        renderer.render(current, camera, scene, alternative);
      }
    }
    for (int operation = static_cast<int>(RelationOperator::AbsoluteDifference);
         operation <= static_cast<int>(RelationOperator::RelativeDifference); ++operation)
      renderer.renderRelation(static_cast<RelationOperator>(operation), 2.0f, 0.5f);
    const std::string relationConfig = relationConfigJson(current, reference, camera, scene,
      HardwareProfile::Unrestricted, RelationOperator::AbsoluteDifference, 4.0f, 0.0f);
    if (relationConfig.find("graphics-lab.render-algebra.v1") == std::string::npos ||
        relationConfig.find("\"renderer_a\"") == std::string::npos ||
        relationConfig.find("\"renderer_b\"") == std::string::npos)
      fail("render-algebra state missing from config export");
    applyRecommendedSetup(TestScene::Transparency, current, camera);
    scene = TestScene::Transparency;
    for (int transparencyMode = 6; transparencyMode <= 9; ++transparencyMode) {
      current.surface.transparency = transparencyMode;
      for (bool orderingTable : {false, true}) {
        current.depth.orderingTable = orderingTable;
        renderer.render(current, camera, scene, false);
      }
    }
    applyRecommendedSetup(TestScene::Torus, current, camera);
    scene = TestScene::Torus;
    current.lighting.depthCue = true;
    for (int textureColorMode = 0; textureColorMode <= 2; ++textureColorMode) {
      current.texture.colorMode = textureColorMode;
      renderer.render(current, camera, scene, false);
    }
    current = RendererState{};
    current.camera.orthographic = true;
    current.rasterization.samples = 8;
    current.surface.normalMapping = true;
    current.texture.mipmapping = true;
    current.lighting.model = 4;
    current.lighting.shadows = true;
    current.depth.testing = true;
    current.stencil.enabled = true;
    current.color.bitsPerChannel = 8;
    current.color.linearLight = true;
    current.post.fog = true;
    current.output.width = 1280;
    current.output.height = 960;
    normalizeForHardwareProfile(HardwareProfile::PlayStation, current);
    if (current.camera.orthographic || !current.rasterization.affineMapping || current.rasterization.samples != 1 ||
        current.surface.normalMapping || !current.texture.nearestFiltering || current.texture.mipmapping ||
        current.lighting.model > 1 || current.lighting.shadows || !current.depth.testing || !current.depth.writing ||
        current.stencil.enabled || current.color.bitsPerChannel != 5 || current.color.linearLight || current.post.fog ||
        current.output.width > 320 || current.output.height > 240 || !current.output.nearestUpscaling)
      fail("PlayStation hardware profile did not normalize renderer state");
    for (int sceneIndex = 0; sceneIndex < 5; ++sceneIndex)
      renderer.render(current, camera, static_cast<TestScene>(sceneIndex), false);
    if (configJson(current, camera, TestScene::Torus, HardwareProfile::PlayStation).find(
          "\"hardware_target\": \"sony_playstation_ps1\"") == std::string::npos)
      fail("hardware target missing from renderer config export");
    current = RendererState{};
    normalizeForHardwareProfile(HardwareProfile::Nintendo64, current);
    if (!current.n64.enabled || current.lighting.model > 1 || current.surface.normalMapping ||
        !current.depth.testing || current.stencil.enabled || current.color.linearLight ||
        current.rasterization.samples != 4)
      fail("Nintendo 64 hardware profile did not normalize renderer state");
    for (int textureFormat = 0; textureFormat <= 8; ++textureFormat) {
      current.n64.textureFormat = textureFormat;
      for (int textureFilter = 0; textureFilter <= 2; ++textureFilter) {
        current.n64.textureFilter = textureFilter;
        renderer.render(current, camera, TestScene::Torus, false);
      }
    }
    for (int mipmapMode = 0; mipmapMode <= 4; ++mipmapMode) {
      current.n64.mipmapMode = mipmapMode;
      current.n64.cycleType = mipmapMode >= 2 ? 2 : 1;
      renderer.render(current, camera, TestScene::TexturePlane, false);
    }
    current.n64.alphaCompare = 2;
    current.n64.textureGeneration = true;
    current.n64.viDivot = true;
    renderer.render(current, camera, TestScene::Torus, false);
    const std::string n64Config = configJson(current, camera, TestScene::Torus, HardwareProfile::Nintendo64);
    if (n64Config.find("\"hardware_target\": \"nintendo_64\"") == std::string::npos ||
        n64Config.find("\"n64_rdp\"") == std::string::npos ||
        n64Config.find("\"combiner_equation\": \"(a - b) * c + d\"") == std::string::npos)
      fail("Nintendo 64 state missing from renderer config export");
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
    ImGui::TextDisabled("Target");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(150.0f);
    int hardwareProfileIndex = static_cast<int>(hardwareProfile);
    const char* hardwareProfileLabels[] = {"Unrestricted", "PlayStation (PS1)", "Nintendo 64"};
    if (ImGui::Combo("##hardware-profile", &hardwareProfileIndex, hardwareProfileLabels, 3)) {
      hardwareProfile = static_cast<HardwareProfile>(hardwareProfileIndex);
      if (hardwareProfile == HardwareProfile::Unrestricted) {
        current.n64.enabled = false;
        reference.n64.enabled = false;
      }
      normalizeForHardwareProfile(hardwareProfile, current);
      normalizeForHardwareProfile(hardwareProfile, reference);
      if (!categoryAvailableForHardwareProfile(hardwareProfile, category)) category = Category::Geometry;
      if (hardwareProfile != HardwareProfile::Unrestricted && scene == TestScene::StencilMask) scene = TestScene::Torus;
    }
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", hardwareProfileDescription(hardwareProfile));
    ImGui::SameLine();
    ImGui::TextDisabled("Scene");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(180.0f);
    const char* sceneLabels[] = {"Torus", "Texture minification", "Depth precision", "Transparency", "Lighting comparison", "Stencil mask"};
    const int sceneCount = hardwareProfile == HardwareProfile::Unrestricted ? 6 : 5;
    int sceneIndex = static_cast<int>(scene);
    if (ImGui::Combo("##test-scene", &sceneIndex, sceneLabels, sceneCount)) scene = static_cast<TestScene>(sceneIndex);
    ImGui::SameLine();
    if (ImGui::Button("Reset neutral")) current = RendererState{};
    ImGui::SameLine();
    if (ImGui::Button("Reset scene setup")) applyRecommendedSetup(scene, current, camera);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Apply the recommended renderer state and camera framing for the selected scene.");
    ImGui::SameLine();
    if (ImGui::Button("Copy A to B")) reference = current;
    ImGui::SameLine();
    if (ImGui::Button("Swap A/B")) std::swap(current, reference);
    if (ImGui::IsItemHovered()) ImGui::SetTooltip("Exchange the complete A and B renderer states. Signed operators reverse direction.");
    ImGui::SameLine();
    const char* copyLabel = compare == CompareMode::Relation ? "Copy A/B JSON" : "Copy config JSON";
    if (ImGui::Button(copyLabel)) {
      const std::string exportedConfig = compare == CompareMode::Relation
        ? relationConfigJson(current, reference, camera, scene, hardwareProfile,
            relationOperator, relationGain, relationBias)
        : configJson(current, camera, scene, hardwareProfile);
      ImGui::SetClipboardText(exportedConfig.c_str());
      configCopiedAt = glfwGetTime();
    }
    if (glfwGetTime() - configCopiedAt < 2.0) {
      ImGui::SameLine();
      ImGui::TextDisabled("Copied");
    }
    ImGui::SameLine();
    if (ImGui::Button("Handbook")) graphicsHandbook.open();
    ImGui::TextDisabled("Compare:");
    ImGui::SameLine();
    if (ImGui::RadioButton("A", compare == CompareMode::A)) compare = CompareMode::A;
    ImGui::SameLine();
    if (ImGui::RadioButton("B", compare == CompareMode::B)) compare = CompareMode::B;
    ImGui::SameLine();
    if (ImGui::RadioButton("Split A/B", compare == CompareMode::Split)) compare = CompareMode::Split;
    ImGui::SameLine();
    if (ImGui::RadioButton("Render algebra", compare == CompareMode::Relation)) compare = CompareMode::Relation;
    if (compare == CompareMode::Relation) {
      ImGui::SameLine();
      ImGui::SetNextItemWidth(165.0f);
      int operatorIndex = static_cast<int>(relationOperator);
      if (ImGui::Combo("##relation-operator", &operatorIndex, relationLabels.data(), relationLabels.size())) {
        relationOperator = static_cast<RelationOperator>(operatorIndex);
        resetRelationTransform(relationOperator, relationGain, relationBias);
      }
    }
    ImGui::SameLine(ImGui::GetWindowWidth() - 260);
    ImGui::TextDisabled("LMB orbit   MMB/RMB pan   Wheel zoom");
    if (compare == CompareMode::Relation) {
      const int relationIndex = static_cast<int>(relationOperator);
      ImGui::TextDisabled("Per-channel: %s", relationEquations[relationIndex]);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(110.0f);
      ImGui::SliderFloat("Gain", &relationGain, 0.1f, 16.0f, "%.2fx", ImGuiSliderFlags_Logarithmic);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(110.0f);
      ImGui::SliderFloat("Bias", &relationBias, -1.0f, 1.0f, "%.2f");
      ImGui::SameLine();
      ImGui::TextDisabled("Then clamp to [0, 1]. %s", relationMeanings[relationIndex]);
    }
    ImGui::Separator();

    const float contentHeight = ImGui::GetContentRegionAvail().y;
    ImGui::BeginChild("Pipeline", ImVec2(145, contentHeight), true);
    ImGui::TextDisabled("PIPELINE");
    ImGui::Spacing();
    constexpr std::array<Category, 11> categories = {Category::Geometry, Category::Camera, Category::Rasterization,
      Category::Surface, Category::Texture, Category::Lighting, Category::Depth, Category::Stencil, Category::Color,
      Category::Post, Category::Output};
    for (Category candidate : categories) {
      if (!categoryAvailableForHardwareProfile(hardwareProfile, candidate)) continue;
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
    normalizeForHardwareProfile(hardwareProfile, current);
    normalizeForHardwareProfile(hardwareProfile, reference);
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
      const GLuint texture = compare == CompareMode::A ? textureA
        : compare == CompareMode::Relation ? renderer.renderRelation(relationOperator, relationGain, relationBias)
                                           : textureB;
      draw->AddImage(static_cast<ImTextureID>(texture), origin, end, ImVec2(0, 1), ImVec2(1, 0));
      const char* label = compare == CompareMode::A ? "A  CURRENT"
        : compare == CompareMode::B ? "B  REFERENCE" : relationLabels[static_cast<int>(relationOperator)];
      draw->AddText(ImVec2(origin.x + 9, origin.y + 8), IM_COL32(240,240,240,220), label);
    }
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("viewport-input", presentationSize);
    viewportHovered = ImGui::IsItemHovered();
    ImGui::EndChild();
    ImGui::SameLine(0, 5);

    ImGui::BeginChild("Inspector", ImVec2(inspectorWidth, contentHeight), true);
      drawInspector(category, current, hardwareProfile);
    ImGui::EndChild();
    ImGui::End();

    const handbook::Action handbookAction = graphicsHandbook.draw(hardwareProfile);
    if (handbookAction.type != handbook::ActionType::None && isNintendo64Example(handbookAction.example))
      hardwareProfile = HardwareProfile::Nintendo64;
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
    normalizeForHardwareProfile(hardwareProfile, current);
    normalizeForHardwareProfile(hardwareProfile, reference);

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

} // namespace gfxlab
