cmake_minimum_required(VERSION 3.20)
project(graphics_lab VERSION 0.1.0 LANGUAGES CXX)

option(GRAPHICS_LAB_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)
find_package(glfw3 REQUIRED)
find_package(glm REQUIRED)
find_package(imgui REQUIRED)

add_executable(graphics-lab
  src/main.cpp
  src/app/State.cpp
  src/handbook/Handbook.cpp
)
target_include_directories(graphics-lab PRIVATE src)
target_compile_features(graphics-lab PRIVATE cxx_std_20)
target_link_libraries(graphics-lab PRIVATE
  OpenGL::GL
  GLEW::GLEW
  glfw
  glm::glm
  imgui::imgui
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(graphics-lab PRIVATE -Wall -Wextra -Wpedantic)
  if(GRAPHICS_LAB_WARNINGS_AS_ERRORS)
    target_compile_options(graphics-lab PRIVATE -Werror)
  endif()
endif()

install(TARGETS graphics-lab RUNTIME DESTINATION bin)
