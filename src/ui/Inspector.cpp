#include "ui/Inspector.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>

namespace gfxlab::ui {

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

void fixedProfileValue(const char* parameter, const char* value) {
  ImGui::TextUnformatted(parameter);
  ImGui::SameLine();
  ImGui::TextDisabled("%s (fixed by profile)", value);
  ImGui::Spacing();
}

void drawInspector(Category category, RendererState& state, HardwareProfile profile) {
  const ProfileCapabilities& capabilities = hardwareProfileCapabilities(profile);
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
      if (capabilities.projectionModes) {
        ImGui::TextUnformatted("Projection");
        radioPair("Perspective", "Orthographic", state.camera.orthographic);
        description("Perspective divides by depth; orthographic projection preserves apparent size with distance.");
      } else {
        fixedProfileValue("Projection", "Perspective");
      }
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
      if (profile == HardwareProfile::PlayStation) {
        fixedProfileValue("Texture interpolation", "Affine");
      } else {
        ImGui::TextUnformatted("Texture coordinate interpolation");
        radioPair("Perspective-correct", "Affine", state.rasterization.affineMapping);
        description("Affine interpolation does not compensate texture coordinates for perspective depth.");
      }
      ImGui::TextUnformatted("Face culling");
      const char* cullLabels[] = {"None", "Back faces", "Front faces"};
      ImGui::Combo("##culling", &state.rasterization.cullMode, cullLabels, 3);
      description("Discards triangles according to their screen-space winding direction.");
      if (capabilities.multisampling) {
        ImGui::TextUnformatted("Multisample anti-aliasing");
        const char* sampleLabels[] = {"Off (1 sample)", "2 samples", "4 samples", "8 samples"};
        const int sampleValues[] = {1, 2, 4, 8};
        int sampleIndex = state.rasterization.samples == 1 ? 0 : state.rasterization.samples == 2 ? 1 : state.rasterization.samples == 4 ? 2 : 3;
        if (ImGui::Combo("##samples", &sampleIndex, sampleLabels, 4)) state.rasterization.samples = sampleValues[sampleIndex];
        description("Stores multiple coverage and depth samples per pixel, then resolves them to one color.");
      }
      if (capabilities.polygonOffset) {
        ImGui::Checkbox("Polygon offset fill", &state.rasterization.polygonOffset);
        ImGui::BeginDisabled(!state.rasterization.polygonOffset);
        ImGui::SliderFloat("Slope factor", &state.rasterization.polygonOffsetFactor, -4.0f, 4.0f, "%.2f");
        ImGui::SliderFloat("Constant units", &state.rasterization.polygonOffsetUnits, -8.0f, 8.0f, "%.2f");
        ImGui::EndDisabled();
        description("Offsets generated depth values by a slope-dependent term plus a minimum-depth-step term.");
      }
      break;
    }
    case Category::Surface: {
      ImGui::TextUnformatted("SURFACE"); ImGui::Separator();
      if (capabilities.surfaceDiagnostics) {
        ImGui::TextUnformatted("Surface visualization");
        const char* visualizationLabels[] = {"Texture", "UV coordinates", "Normals", "Vertex colors", "Tangents", "Bitangents"};
        ImGui::Combo("##visualization", &state.surface.visualization, visualizationLabels, 6);
        description("Selects the mesh attribute used as the surface's base color.");
      }
      if (capabilities.smoothAndFlatNormals) {
        ImGui::TextUnformatted("Shading interpolation");
        bool flat = !state.surface.smoothShading;
        if (radioPair("Smooth", "Flat", flat)) state.surface.smoothShading = !flat;
        description("Smooth shading interpolates vertex normals; flat shading uses one face normal per triangle.");
      }
      if (capabilities.wireframeOverlay) {
        ImGui::Checkbox("Wireframe overlay", &state.surface.wireframe);
        description("Draws triangle boundaries over the shaded surface.");
      }
      if (capabilities.normalMapping) {
        ImGui::Checkbox("Tangent-space normal mapping", &state.surface.normalMapping);
        ImGui::BeginDisabled(!state.surface.normalMapping);
        ImGui::SliderFloat("Normal-map strength", &state.surface.normalStrength, 0.0f, 2.0f, "%.2f");
        ImGui::EndDisabled();
        description("Transforms sampled tangent-space normals into world space with the tangent-bitangent-normal basis.");
      }
      ImGui::TextUnformatted("Transparency operation");
      if (capabilities.generalBlendModes) {
        const char* transparencyLabels[] = {"Opaque", "Alpha test (discard)", "Straight alpha blend",
          "Premultiplied alpha blend", "Additive blend", "Multiply blend", "PS1 average (B/2 + F/2)",
          "PS1 additive (B + F)", "PS1 subtractive (B - F)", "PS1 quarter-add (B + F/4)"};
        ImGui::Combo("##transparency", &state.surface.transparency, transparencyLabels, 10);
      } else {
        const char* labels[] = {"Opaque", "Texture cutout", "Average (B/2 + F/2)", "Additive (B + F)",
          "Subtractive (B - F)", "Quarter-add (B + F/4)"};
        constexpr int values[] = {0, 1, 6, 7, 8, 9};
        int selected = 0;
        for (int index = 0; index < 6; ++index) if (state.surface.transparency == values[index]) selected = index;
        if (ImGui::Combo("##transparency", &selected, labels, 6)) state.surface.transparency = values[selected];
      }
      if (state.surface.transparency == 1)
        ImGui::SliderFloat("Alpha cutoff", &state.surface.alphaCutoff, 0.0f, 1.0f, "%.2f");
      description("Each blend mode configures explicit source and destination factors in the framebuffer blend equation.");
      ImGui::Checkbox("Reverse object draw order", &state.surface.reverseDrawOrder);
      description("Transparent surfaces generally require back-to-front submission because blending is order-dependent.");
      break;
    }
    case Category::Texture: {
      ImGui::TextUnformatted("TEXTURE"); ImGui::Separator();
      if (capabilities.textureFiltering) {
        ImGui::TextUnformatted("Texture filtering");
        radioPair("Bilinear", "Nearest", state.texture.nearestFiltering);
        description("Selects how samples between adjacent texels are reconstructed.");
      } else {
        fixedProfileValue("Texture filtering", "Nearest");
      }
      ImGui::TextUnformatted("Texture address mode");
      radioPair("Clamp to edge", "Repeat", state.texture.repeat);
      description("Defines how texture coordinates outside the normalized 0-1 range are sampled.");
      if (capabilities.mipmapping) {
        ImGui::Checkbox("Mipmapping", &state.texture.mipmapping);
        description("Selects prefiltered, lower-resolution texture levels during minification.");
        ImGui::BeginDisabled(!state.texture.mipmapping || state.texture.nearestFiltering);
        ImGui::Checkbox("Trilinear mip interpolation", &state.texture.trilinear);
        ImGui::EndDisabled();
        description("Interpolates between the two nearest mip levels as well as between texels.");
      }
      if (capabilities.anisotropy) {
        ImGui::SliderFloat("Anisotropy", &state.texture.anisotropy, 1.0f, 16.0f, "%.0f x");
        description("Uses additional samples to preserve detail when texture footprints are elongated by perspective.");
      }
      ImGui::TextUnformatted("Texture color storage");
      const char* colorModeLabels[] = {"Direct color", "8-bit index + 256-color CLUT", "4-bit index + 16-color CLUT"};
      ImGui::Combo("##texture-color-storage", &state.texture.colorMode, colorModeLabels, 3);
      description("Indexed textures store palette entries rather than RGB texels. CLUT means color lookup table.");
      break;
    }
    case Category::Lighting: {
      ImGui::TextUnformatted("LIGHTING"); ImGui::Separator();
      ImGui::TextUnformatted("Lighting model");
      if (capabilities.perFragmentLighting) {
        const char* lightingLabels[] = {"Unlit", "Gouraud / per-vertex Lambert", "Phong shading / per-fragment Lambert",
          "Phong reflection", "Blinn-Phong reflection"};
        ImGui::Combo("##lighting-model", &state.lighting.model, lightingLabels, 5);
      } else {
        const char* lightingLabels[] = {"Unlit", "Gouraud / per-vertex Lambert"};
        ImGui::Combo("##lighting-model", &state.lighting.model, lightingLabels, 2);
      }
      description("Gouraud interpolates computed vertex lighting; Phong shading interpolates normals and lights each fragment.");
      ImGui::SliderFloat("Ambient term", &state.lighting.ambient, 0.0f, 1.0f, "%.2f");
      if (capabilities.perFragmentLighting && state.lighting.model >= 3)
        ImGui::SliderFloat("Specular exponent", &state.lighting.shininess, 2.0f, 128.0f, "%.0f", ImGuiSliderFlags_Logarithmic);
      ImGui::SliderFloat("Light azimuth", &state.lighting.azimuth, -180.0f, 180.0f, "%.0f deg");
      ImGui::SliderFloat("Light elevation", &state.lighting.elevation, -90.0f, 90.0f, "%.0f deg");
      description("Azimuth rotates around the vertical axis; elevation moves above or below the horizon.");
      if (capabilities.shadowMapping) {
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
      }
      ImGui::Checkbox("Vertex depth cueing", &state.lighting.depthCue);
      ImGui::BeginDisabled(!state.lighting.depthCue);
      ImGui::SliderFloat("Cue start", &state.lighting.depthCueStart, 0.0f, 15.0f, "%.2f units");
      ImGui::SliderFloat("Cue end", &state.lighting.depthCueEnd, 0.1f, 30.0f, "%.2f units");
      ImGui::ColorEdit3("Far color", &state.lighting.farColor.x, ImGuiColorEditFlags_NoInputs);
      ImGui::EndDisabled();
      description("Computes a depth factor at vertices, interpolates it, then blends shaded color toward the far color.");
      break;
    }
    case Category::Depth: {
      ImGui::TextUnformatted("DEPTH"); ImGui::Separator();
      if (capabilities.depthBuffer) {
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
      } else {
        fixedProfileValue("Opaque visibility", "Depth-buffer emulation");
        description("The PS1 had no depth buffer. The lab keeps opaque objects stable because its ordering table currently sorts objects, not individual mesh triangles.");
      }
      ImGui::Checkbox("Object ordering table", &state.depth.orderingTable);
      ImGui::BeginDisabled(!state.depth.orderingTable);
      ImGui::SliderInt("Depth buckets", &state.depth.orderingBuckets, 4, 256);
      ImGui::EndDisabled();
      description("Bins transparent objects by camera depth and submits far buckets first with depth testing disabled. Granularity: object, not polygon.");
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
      if (capabilities.configurableColorDepth) {
        ImGui::TextUnformatted("Output color depth");
        const char* labels[] = {"24-bit (8:8:8)", "15-bit (5:5:5)", "12-bit (4:4:4)"};
        int selected = state.color.bitsPerChannel == 8 ? 0 : state.color.bitsPerChannel == 5 ? 1 : 2;
        if (ImGui::Combo("##depth", &selected, labels, 3)) state.color.bitsPerChannel = selected == 0 ? 8 : selected == 1 ? 5 : 4;
        description("Quantizes each output color channel to a fixed number of levels.");
      } else {
        fixedProfileValue("Output color depth", "15-bit RGB (5:5:5)");
      }
      ImGui::Checkbox("Ordered dithering (4 x 4 Bayer)", &state.color.dithering);
      description("Offsets pixels with a fixed threshold matrix before color quantization.");
      if (capabilities.linearLight) {
        ImGui::TextUnformatted("Lighting color space");
        radioPair("Encoded RGB (incorrect)", "Linear light", state.color.linearLight);
        description("Linear-light mode decodes texture values before lighting and encodes the final image for display.");
      } else {
        fixedProfileValue("Lighting color space", "Encoded RGB");
      }
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
      const char* unrestrictedLabels[] = {"1280 x 960", "640 x 480", "320 x 240", "256 x 192", "160 x 120"};
      const int unrestrictedWidths[] = {1280, 640, 320, 256, 160};
      const int unrestrictedHeights[] = {960, 480, 240, 192, 120};
      const char* playStationLabels[] = {"320 x 240", "256 x 192", "160 x 120"};
      const int playStationWidths[] = {320, 256, 160};
      const int playStationHeights[] = {240, 192, 120};
      const bool restricted = profile == HardwareProfile::PlayStation;
      const char* const* labels = restricted ? playStationLabels : unrestrictedLabels;
      const int* widths = restricted ? playStationWidths : unrestrictedWidths;
      const int* heights = restricted ? playStationHeights : unrestrictedHeights;
      const int count = restricted ? 3 : 5;
      int selected = restricted ? 0 : 1;
      for (int i = 0; i < count; ++i) if (state.output.width == widths[i] && state.output.height == heights[i]) selected = i;
      if (ImGui::Combo("##resolution", &selected, labels, count)) { state.output.width = widths[selected]; state.output.height = heights[selected]; }
      description("Scene and output passes render at this exact pixel resolution.");
      if (profile == HardwareProfile::PlayStation) {
        fixedProfileValue("Viewport upscaling", "Nearest");
      } else {
        ImGui::TextUnformatted("Viewport upscaling");
        radioPair("Bilinear", "Nearest", state.output.nearestUpscaling);
        description("Filters the completed internal-resolution framebuffer when enlarging it to the viewport.");
      }
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

} // namespace gfxlab::ui
