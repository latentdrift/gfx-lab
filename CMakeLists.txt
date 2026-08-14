cmake_minimum_required(VERSION 3.20)
project(graphics_lab VERSION 0.1.0 LANGUAGES CXX)

option(GRAPHICS_LAB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(GRAPHICS_LAB_IMGUIZMO_SOURCE "$ENV{GRAPHICS_LAB_IMGUIZMO_SOURCE}" CACHE PATH
  "Path to the pinned ImGuizmo source tree")

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(imgui REQUIRED)
find_package(assimp REQUIRED)
find_package(nfd REQUIRED)
find_package(ZLIB REQUIRED)
find_package(nlohmann_json REQUIRED)
find_program(GRAPHICS_LAB_FFMPEG_EXECUTABLE ffmpeg REQUIRED)

add_executable(graphics-lab
  src/main.cpp
  src/assets/ModelAsset.cpp
  src/app/Animation.cpp
  src/app/Application.cpp
  src/app/EditorHistory.cpp
  src/app/FileDialog.cpp
  src/app/HardwareProfile.cpp
  src/app/PassEditing.cpp
  src/app/RenderStack.cpp
  src/app/Spectral.cpp
  src/app/State.cpp
  src/app/StackDocument.cpp
  src/app/Validation.cpp
  src/app/ViewportRecorder.cpp
  src/document/Document.cpp
  src/document/LegacyAdapter.cpp
  src/document/Operations.cpp
  src/document/Properties.cpp
  src/document/Signals.cpp
  src/evaluation/Compiler.cpp
  src/evaluation/LegacyBridge.cpp
  src/evaluation/SignalRegistry.cpp
  src/editor/EditorState.cpp
  src/editor/Commands.cpp
  src/handbook/Handbook.cpp
  src/renderer/Renderer.cpp
  src/renderer/TestGeometry.cpp
  src/renderer/TextureReadback.cpp
  src/simulation/ElementalSimulation.cpp
  src/ui/AnimationEditor.cpp
  src/ui/AnimationControls.cpp
  src/ui/ContextInspector.cpp
  src/ui/DisplayInspector.cpp
  src/ui/Inspector.cpp
  src/ui/InstrumentWidgets.cpp
  src/ui/PassInspector.cpp
  src/ui/PassDifferenceAudit.cpp
  src/ui/ScopePanel.cpp
  src/ui/TextureInspector.cpp
  src/ui/TextureMappingEditor.cpp
  src/ui/Windowing.cpp
  src/ui/Workspace.cpp
)
if(NOT EXISTS "${GRAPHICS_LAB_IMGUIZMO_SOURCE}/src/ImGuizmo.cpp")
  message(FATAL_ERROR "GRAPHICS_LAB_IMGUIZMO_SOURCE must point to an ImGuizmo checkout")
endif()
target_sources(graphics-lab PRIVATE "${GRAPHICS_LAB_IMGUIZMO_SOURCE}/src/ImGuizmo.cpp")
target_include_directories(graphics-lab PRIVATE src)
target_include_directories(graphics-lab SYSTEM PRIVATE "${GRAPHICS_LAB_IMGUIZMO_SOURCE}/src")
target_compile_features(graphics-lab PRIVATE cxx_std_20)
target_compile_definitions(graphics-lab PRIVATE
  GRAPHICS_LAB_FFMPEG_EXECUTABLE="${GRAPHICS_LAB_FFMPEG_EXECUTABLE}")
target_link_libraries(graphics-lab PRIVATE
  OpenGL::GL
  GLEW::glew
  glfw
  glm::glm
  imgui::imgui
  assimp::assimp
  nfd::nfd
  ZLIB::ZLIB
  nlohmann_json::nlohmann_json
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(graphics-lab PRIVATE -Wall -Wextra -Wpedantic)
  if(GRAPHICS_LAB_WARNINGS_AS_ERRORS)
    target_compile_options(graphics-lab PRIVATE -Werror)
  endif()
endif()

install(TARGETS graphics-lab RUNTIME DESTINATION bin)
install(DIRECTORY examples DESTINATION share/graphics-lab)
