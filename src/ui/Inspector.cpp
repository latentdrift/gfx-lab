#include "ui/Inspector.hpp"

#include "app/Spectral.hpp"
#include "app/FileDialog.hpp"
#include "app/RenderStack.hpp"
#include "assets/ModelAsset.hpp"
#include "ui/AnimationControls.hpp"
#include "ui/InstrumentWidgets.hpp"

#include <imgui.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

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

void drawCombinerCycle(const char* label, RendererState::CombinerCycle& cycle, RenderPass& pass,
    AnimationTimeline& timeline, const std::array<AnimationProperty, 4>& properties) {
  const char* sources[] = {"ZERO", "TEXEL0", "ONE", "SHADE", "PRIMITIVE", "ENVIRONMENT", "TEXEL1", "COMBINED", "LOD_FRACTION"};
  ImGui::PushID(label);
  ImGui::TextUnformatted(label);
  ImGui::SetNextItemWidth(128.0f); animationKeyControl(pass, properties[0], timeline, ImGui::Combo("A", &cycle.a, sources, 9));
  ImGui::SetNextItemWidth(128.0f); animationKeyControl(pass, properties[1], timeline, ImGui::Combo("B", &cycle.b, sources, 9));
  ImGui::SetNextItemWidth(128.0f); animationKeyControl(pass, properties[2], timeline, ImGui::Combo("C", &cycle.c, sources, 9));
  ImGui::SetNextItemWidth(128.0f); animationKeyControl(pass, properties[3], timeline, ImGui::Combo("D", &cycle.d, sources, 9));
  ImGui::TextDisabled("(A - B) x C + D");
  ImGui::PopID();
}

int n64TextureBytes(const RendererState::N64& state) {
  constexpr int bitsPerTexel[] = {16, 32, 4, 8, 4, 8, 16, 4, 8};
  const int textureBytes = state.tileWidth * state.tileHeight * bitsPerTexel[std::clamp(state.textureFormat, 0, 8)] / 8;
  const int paletteBytes = state.textureFormat == 2 ? 128 : state.textureFormat == 3 ? 2048 : 0;
  return textureBytes + paletteBytes;
}

void drawInspector(Category category, RenderPass& pass, HardwareProfile profile, AnimationTimeline& timeline,
    const ModelAsset* importedModel, const TestScene scene, const bool textureSamplingOnly,
    const bool editingSceneDefaults) {
  RendererState& state = pass.renderer;
  const ProfileCapabilities& capabilities = hardwareProfileCapabilities(profile);
  switch (category) {
    case Category::Geometry: {
      ImGui::TextUnformatted("GEOMETRY"); ImGui::Separator();
      ImGui::TextUnformatted("Vertex position precision");
      const char* labels[] = {"Full precision", "1/64 unit", "1/32 unit", "1/16 unit", "1/8 unit"};
      const float values[] = {0.0f, 1.0f/64.0f, 1.0f/32.0f, 1.0f/16.0f, 1.0f/8.0f};
      int selected = 0;
      for (int i = 1; i < 5; ++i) if (std::abs(state.geometry.vertexQuantization - values[i]) < 0.0001f) selected = i;
      const bool precisionChanged = ImGui::Combo("##precision", &selected, labels, 5);
      if (precisionChanged) state.geometry.vertexQuantization = values[selected];
      animationKeyControl(pass, AnimationProperty::VertexQuantization, timeline, precisionChanged);
      description("Rounds model-space vertex positions to a fixed grid before projection.");
      animationKeyControl(pass, AnimationProperty::ClippingEnabled, timeline,
        ImGui::Checkbox("World-space clipping plane", &state.geometry.clipping));
      animationKeyControl(pass, AnimationProperty::ClippingHeight, timeline,
        ImGui::SliderFloat("Plane height", &state.geometry.clipHeight, -1.5f, 1.5f, "y = %.2f"));
      animationKeyControl(pass, AnimationProperty::ClippingKeepAbove, timeline,
        radioPair("Keep above", "Keep below", state.geometry.clipAbove));
      description("The GPU clips primitives against a horizontal world-space plane before rasterization.");
      break;
    }
    case Category::Camera: {
      ImGui::TextUnformatted("CAMERA"); ImGui::Separator();
      if (capabilities.projectionModes) {
        ImGui::TextUnformatted("Projection");
        animationKeyControl(pass, AnimationProperty::ProjectionOrthographic, timeline,
          radioPair("Perspective", "Orthographic", state.camera.orthographic));
        description("Perspective divides by depth; orthographic projection preserves apparent size with distance.");
      } else {
        fixedProfileValue("Projection", "Perspective");
      }
      if (state.camera.orthographic)
        animationKeyControl(pass, AnimationProperty::OrthographicSize, timeline,
          ImGui::SliderFloat("View height", &state.camera.orthographicSize, 1.0f, 10.0f, "%.2f units"));
      else
        animationKeyControl(pass, AnimationProperty::FieldOfView, timeline,
          ImGui::SliderFloat("Field of view", &state.camera.fieldOfView, 20.0f, 100.0f, "%.0f deg"));
      animationKeyControl(pass, AnimationProperty::NearPlane, timeline,
        ImGui::SliderFloat("Near clipping plane", &state.camera.nearPlane, 0.01f, 2.0f, "%.3f unit", ImGuiSliderFlags_Logarithmic));
      description("Geometry closer than this camera-space distance is clipped. It also strongly affects depth precision.");
      break;
    }
    case Category::Rasterization: {
      ImGui::TextUnformatted("RASTERIZATION"); ImGui::Separator();
      if (profile == HardwareProfile::PlayStation) {
        fixedProfileValue("Texture interpolation", "Affine");
      } else {
        ImGui::TextUnformatted("Texture coordinate interpolation");
        animationKeyControl(pass, AnimationProperty::AffineMapping, timeline,
          radioPair("Perspective-correct", "Affine", state.rasterization.affineMapping));
        description("Affine interpolation does not compensate texture coordinates for perspective depth.");
      }
      ImGui::TextUnformatted("Face culling");
      const char* cullLabels[] = {"None", "Back faces", "Front faces"};
      animationKeyControl(pass, AnimationProperty::CullMode, timeline,
        ImGui::Combo("##culling", &state.rasterization.cullMode, cullLabels, 3));
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
        animationKeyControl(pass, AnimationProperty::PolygonOffsetEnabled, timeline,
          ImGui::Checkbox("Polygon offset fill", &state.rasterization.polygonOffset));
        ImGui::BeginDisabled(!state.rasterization.polygonOffset);
        animationKeyControl(pass, AnimationProperty::PolygonOffsetFactor, timeline,
          ImGui::SliderFloat("Slope factor", &state.rasterization.polygonOffsetFactor, -4.0f, 4.0f, "%.2f"));
        animationKeyControl(pass, AnimationProperty::PolygonOffsetUnits, timeline,
          ImGui::SliderFloat("Constant units", &state.rasterization.polygonOffsetUnits, -8.0f, 8.0f, "%.2f"));
        ImGui::EndDisabled();
        description("Offsets generated depth values by a slope-dependent term plus a minimum-depth-step term.");
      }
      if (profile == HardwareProfile::Nintendo64) {
        animationKeyControl(pass, AnimationProperty::N64CoverageAntialiasing, timeline,
          ImGui::Checkbox("RDP coverage antialiasing", &state.n64.coverageAntialiasing));
        description("Approximates RDP edge coverage with four raster samples. This is not conventional N64 bit-exact coverage storage.");
      }
      break;
    }
    case Category::Surface: {
      ImGui::TextUnformatted("SURFACE"); ImGui::Separator();
      if (profile == HardwareProfile::Nintendo64) {
        ImGui::TextUnformatted("RDP cycle type");
        const char* cycleLabels[] = {"1-cycle", "2-cycle"};
        int cycleIndex = state.n64.cycleType - 1;
        const bool cycleTypeChanged = ImGui::Combo("##rdp-cycle-type", &cycleIndex, cycleLabels, 2);
        if (cycleTypeChanged) state.n64.cycleType = cycleIndex + 1;
        animationKeyControl(pass, AnimationProperty::N64CycleType, timeline, cycleTypeChanged);
        description("Two-cycle mode evaluates a second combiner equation and halves nominal pixel throughput.");
        drawCombinerCycle("Color combiner cycle 0", state.n64.cycle0, pass, timeline,
          {AnimationProperty::N64Cycle0A, AnimationProperty::N64Cycle0B,
           AnimationProperty::N64Cycle0C, AnimationProperty::N64Cycle0D});
        if (state.n64.cycleType == 2) drawCombinerCycle("Color combiner cycle 1", state.n64.cycle1, pass,
          timeline, {AnimationProperty::N64Cycle1A, AnimationProperty::N64Cycle1B,
            AnimationProperty::N64Cycle1C, AnimationProperty::N64Cycle1D});
        animationKeyControl(pass, AnimationProperty::PrimitiveColor, timeline,
          ImGui::ColorEdit4("Primitive color", &state.n64.primitiveColor.x));
        animationKeyControl(pass, AnimationProperty::EnvironmentColor, timeline,
          ImGui::ColorEdit4("Environment color", &state.n64.environmentColor.x));
        description("Combiner sources are clamped after each (A - B) x C + D cycle.");
        ImGui::TextUnformatted("RDP surface / Z mode");
        const char* surfaceModes[] = {"Opaque", "Translucent", "Decal", "Interpenetrating"};
        const bool surfaceModeChanged = ImGui::Combo("##n64-surface-mode", &state.n64.surfaceMode, surfaceModes, 4);
        if (surfaceModeChanged)
          state.n64.zUpdate = state.n64.surfaceMode != 1;
        animationKeyControl(pass, AnimationProperty::N64SurfaceMode, timeline, surfaceModeChanged);
        description("Configures standard depth-update and blending behavior. Decal depth bias is an OpenGL approximation.");
        ImGui::TextUnformatted("Alpha compare");
        const char* alphaModes[] = {"Off", "Threshold", "Dither"};
        animationKeyControl(pass, AnimationProperty::N64AlphaCompare, timeline,
          ImGui::Combo("##n64-alpha-compare", &state.n64.alphaCompare, alphaModes, 3));
        if (state.n64.alphaCompare == 1)
          animationKeyControl(pass, AnimationProperty::AlphaThreshold, timeline,
            ImGui::SliderFloat("Alpha threshold", &state.n64.alphaThreshold, 0.0f, 1.0f, "%.2f"));
        description("Rejects fragments before framebuffer blending using a constant threshold or spatial noise.");
      }
      if (capabilities.surfaceDiagnostics) {
        ImGui::TextUnformatted("Surface visualization");
        const char* visualizationLabels[] = {"Texture", "UV coordinates", "Normals", "Vertex colors", "Tangents", "Bitangents"};
        animationKeyControl(pass, AnimationProperty::SurfaceVisualization, timeline,
          ImGui::Combo("##visualization", &state.surface.visualization, visualizationLabels, 6));
        description("Selects the mesh attribute used as the surface's base color.");
      }
      if (capabilities.smoothAndFlatNormals) {
        ImGui::TextUnformatted("Shading interpolation");
        bool flat = !state.surface.smoothShading;
        const bool shadingChanged = radioPair("Smooth", "Flat", flat);
        if (shadingChanged) state.surface.smoothShading = !flat;
        animationKeyControl(pass, AnimationProperty::SmoothShading, timeline, shadingChanged);
        description("Smooth shading interpolates vertex normals; flat shading uses one face normal per triangle.");
      }
      if (capabilities.wireframeOverlay) {
        animationKeyControl(pass, AnimationProperty::WireframeOverlay, timeline,
          ImGui::Checkbox("Wireframe overlay", &state.surface.wireframe));
        description("Draws triangle boundaries over the shaded surface.");
      }
      if (capabilities.normalMapping) {
        animationKeyControl(pass, AnimationProperty::NormalMappingEnabled, timeline,
          ImGui::Checkbox("Tangent-space normal mapping", &state.surface.normalMapping));
        ImGui::BeginDisabled(!state.surface.normalMapping);
        animationKeyControl(pass, AnimationProperty::NormalStrength, timeline,
          ImGui::SliderFloat("Normal-map strength", &state.surface.normalStrength, 0.0f, 2.0f, "%.2f"));
        ImGui::EndDisabled();
        description("Transforms sampled tangent-space normals into world space with the tangent-bitangent-normal basis.");
      }
      if (profile != HardwareProfile::Nintendo64) ImGui::TextUnformatted("Transparency operation");
      if (capabilities.generalBlendModes) {
        const char* transparencyLabels[] = {"Opaque", "Alpha test (discard)", "Straight alpha blend",
          "Premultiplied alpha blend", "Additive blend", "Multiply blend", "PS1 average (B/2 + F/2)",
          "PS1 additive (B + F)", "PS1 subtractive (B - F)", "PS1 quarter-add (B + F/4)"};
        animationKeyControl(pass, AnimationProperty::TransparencyOperation, timeline,
          ImGui::Combo("##transparency", &state.surface.transparency, transparencyLabels, 10));
      } else if (profile == HardwareProfile::PlayStation) {
        const char* labels[] = {"Opaque", "Texture cutout", "Average (B/2 + F/2)", "Additive (B + F)",
          "Subtractive (B - F)", "Quarter-add (B + F/4)"};
        constexpr int values[] = {0, 1, 6, 7, 8, 9};
        int selected = 0;
        for (int index = 0; index < 6; ++index) if (state.surface.transparency == values[index]) selected = index;
        const bool transparencyChanged = ImGui::Combo("##transparency", &selected, labels, 6);
        if (transparencyChanged) state.surface.transparency = values[selected];
        animationKeyControl(pass, AnimationProperty::TransparencyOperation, timeline, transparencyChanged);
      }
      if (profile != HardwareProfile::Nintendo64 && state.surface.transparency == 1)
        animationKeyControl(pass, AnimationProperty::AlphaCutoff, timeline,
          ImGui::SliderFloat("Alpha cutoff", &state.surface.alphaCutoff, 0.0f, 1.0f, "%.2f"));
      if (profile != HardwareProfile::Nintendo64) {
        description("Each blend mode configures explicit source and destination factors in the framebuffer blend equation.");
        animationKeyControl(pass, AnimationProperty::ReverseDrawOrder, timeline,
          ImGui::Checkbox("Reverse object draw order", &state.surface.reverseDrawOrder));
        description("Transparent surfaces generally require back-to-front submission because blending is order-dependent.");
      }
      break;
    }
    case Category::Texture: {
      ImGui::TextUnformatted(textureSamplingOnly ? "SAMPLING & STORAGE" : "TEXTURE"); ImGui::Separator();
      const bool imageAssetSource = pass.textureSource == TextureSource::ImportedOverride ||
        (pass.textureSource == TextureSource::SceneMaterial && scene == TestScene::ImportedModel);
      if (!textureSamplingOnly) {
      const char* sourceLabels[] = {"Scene material", "Built-in checker", "Imported override", "White texel"};
      int textureSource = static_cast<int>(pass.textureSource);
      const bool textureSourceChanged = ImGui::Combo("Texture source", &textureSource, sourceLabels, 4);
      if (textureSourceChanged)
        pass.textureSource = static_cast<TextureSource>(textureSource);
      animationKeyControl(pass, AnimationProperty::TextureSource, timeline, textureSourceChanged);
      if (pass.textureSource == TextureSource::SceneMaterial) {
        if (scene == TestScene::ImportedModel && importedModel != nullptr) {
          ImGui::TextDisabled("%zu materials   %zu base-color images", importedModel->materials.size(),
            importedModel->textures.size());
          if (!importedModel->hasTextureCoordinates && pass.perturbation.uvMapping == UvMapping::MeshUv0)
            ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.34f, 1.0f), "Model has no UV0 coordinates");
          description("Each imported submesh uses the base-color factor and image assigned by its source material.");
        } else {
          description("The supplied diagnostic scenes use the built-in checker as their scene material.");
        }
      } else if (pass.textureSource == TextureSource::White) {
        description("Samples a constant white texel. Material factors and lighting remain, but image variation is removed.");
      } else if (pass.textureSource == TextureSource::BuiltInChecker) {
        description("Uses the lab's diagnostic color checker instead of the scene's material image.");
      }

      static std::string textureImportError;
      if (ImGui::Button(pass.importedTexture == nullptr ? "Import texture" : "Replace texture")) {
        textureImportError.clear();
        const FileDialogResult dialog = openTextureFileDialog();
        if (!dialog.error.empty()) textureImportError = dialog.error;
        else if (dialog.path.has_value()) {
          const TextureImportResult imported = importTextureAsset(*dialog.path);
          if (imported) {
            pass.importedTexture = imported.asset;
            pass.importedTextureSrgb = true;
            pass.textureSource = TextureSource::ImportedOverride;
            textureImportError.clear();
          } else {
            textureImportError = imported.error;
          }
        }
        if (!textureImportError.empty()) ImGui::OpenPopup("Texture import failed");
      }
      if (pass.importedTexture != nullptr) {
        ImGui::SameLine();
        if (ImGui::Button("Reload")) {
          const TextureImportResult imported = importTextureAsset(pass.importedTexture->sourcePath);
          if (imported) pass.importedTexture = imported.asset;
          else {
            textureImportError = imported.error;
            ImGui::OpenPopup("Texture import failed");
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Remove")) {
          pass.importedTexture.reset();
          if (pass.textureSource == TextureSource::ImportedOverride)
            pass.textureSource = TextureSource::SceneMaterial;
        }
      }
      if (ImGui::BeginPopupModal("Texture import failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextWrapped("%s", textureImportError.c_str());
        if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
      }
      if (pass.importedTexture != nullptr) {
        const TextureAsset& texture = *pass.importedTexture;
        ImGui::Text("%s", texture.name.c_str());
        ImGui::TextDisabled("%d x %d RGBA8   alpha %s", texture.width, texture.height,
          texture.hasAlpha ? "present" : "opaque");
        if (scene == TestScene::ImportedModel && importedModel != nullptr && !importedModel->hasTextureCoordinates &&
            pass.perturbation.uvMapping == UvMapping::MeshUv0)
          ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.34f, 1.0f), "Override cannot vary: model has no UV0");
        ImGui::TextUnformatted("Color interpretation");
        bool colorInterpretationChanged = false;
        if (ImGui::RadioButton("sRGB color", pass.importedTextureSrgb)) {
          colorInterpretationChanged = !pass.importedTextureSrgb; pass.importedTextureSrgb = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Linear data", !pass.importedTextureSrgb)) {
          colorInterpretationChanged = pass.importedTextureSrgb; pass.importedTextureSrgb = false;
        }
        animationKeyControl(pass, AnimationProperty::TextureColorInterpretation, timeline,
          colorInterpretationChanged);
        description("sRGB decodes the stored RGB values before linear-light operations. Linear data leaves them unchanged.");
        if (pass.textureSource != TextureSource::ImportedOverride) {
          if (ImGui::Button("Use imported override")) pass.textureSource = TextureSource::ImportedOverride;
          ImGui::SameLine();
          ImGui::TextDisabled("Loaded but not selected");
        }
      } else if (pass.textureSource == TextureSource::ImportedOverride) {
        ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.34f, 1.0f), "No override texture is loaded");
      }
      ImGui::Spacing();
      ImGui::Separator();
      }
      if (profile == HardwareProfile::Nintendo64) {
        ImGui::TextUnformatted("RDP texture filter");
        const char* filterLabels[] = {"Point sampling", "Three-point approximate bilinear", "Four-texel box average"};
        animationKeyControl(pass, AnimationProperty::N64TextureFilter, timeline,
          ImGui::Combo("##n64-filter", &state.n64.textureFilter, filterLabels, 3));
        description("The RDP's usual filtered mode interpolates the three nearest texels and has a diagonal bias.");
      } else if (capabilities.textureFiltering) {
        ImGui::TextUnformatted("Texture filtering");
        animationKeyControl(pass, AnimationProperty::NearestFiltering, timeline,
          radioPair("Bilinear", "Nearest", state.texture.nearestFiltering));
        description("Selects how samples between adjacent texels are reconstructed.");
      } else {
        fixedProfileValue("Texture filtering", "Nearest");
      }
      if (profile == HardwareProfile::Nintendo64) {
        animationKeyControl(pass, AnimationProperty::N64MirrorS, timeline,
          ImGui::Checkbox("Mirror S", &state.n64.mirrorS)); ImGui::SameLine();
        animationKeyControl(pass, AnimationProperty::N64MirrorT, timeline,
          ImGui::Checkbox("Mirror T", &state.n64.mirrorT));
        animationKeyControl(pass, AnimationProperty::N64ShiftS, timeline,
          ImGui::SliderInt("S coordinate shift", &state.n64.shiftS, -5, 5));
        animationKeyControl(pass, AnimationProperty::N64ShiftT, timeline,
          ImGui::SliderInt("T coordinate shift", &state.n64.shiftT, -5, 5));
        description("Tile mirroring and signed power-of-two coordinate shifts are applied before texel addressing.");
      } else {
        ImGui::TextUnformatted("Texture address mode");
        animationKeyControl(pass, AnimationProperty::TextureRepeat, timeline,
          radioPair("Clamp to edge", "Repeat", state.texture.repeat));
        description("Defines how texture coordinates outside the normalized 0-1 range are sampled.");
      }
      if (capabilities.mipmapping) {
        animationKeyControl(pass, AnimationProperty::Mipmapping, timeline,
          ImGui::Checkbox("Mipmapping", &state.texture.mipmapping));
        description("Selects prefiltered, lower-resolution texture levels during minification.");
        ImGui::BeginDisabled(!state.texture.mipmapping || state.texture.nearestFiltering);
        animationKeyControl(pass, AnimationProperty::TrilinearFiltering, timeline,
          ImGui::Checkbox("Trilinear mip interpolation", &state.texture.trilinear));
        ImGui::EndDisabled();
        description("Interpolates between the two nearest mip levels as well as between texels.");
      }
      if (capabilities.anisotropy) {
        animationKeyControl(pass, AnimationProperty::Anisotropy, timeline,
          ImGui::SliderFloat("Anisotropy", &state.texture.anisotropy, 1.0f, 16.0f, "%.0f x"));
        description("Uses additional samples to preserve detail when texture footprints are elongated by perspective.");
      }
      if (profile == HardwareProfile::Nintendo64) {
        ImGui::TextUnformatted("RDP mip/detail mode");
        const char* mipLabels[] = {"Disabled", "Nearest mip level", "Trilinear interpolation", "Sharpen", "Detail texture"};
        animationKeyControl(pass, AnimationProperty::N64MipmapMode, timeline,
          ImGui::Combo("##n64-mipmap", &state.n64.mipmapMode, mipLabels, 5));
        if (state.n64.mipmapMode >= 2 && state.n64.cycleType != 2)
          ImGui::TextColored(ImVec4(0.92f, 0.67f, 0.35f, 1.0f), "Requires 2-cycle mode");
        description("Trilinear, sharpen, and detail modes consume the second texture/combiner path.");
        ImGui::TextUnformatted("Texture image format");
        const char* formats[] = {"RGBA16 (5:5:5:1)", "RGBA32 (8:8:8:8)", "CI4 + RGBA16 TLUT", "CI8 + RGBA16 TLUT",
          "IA4 (3:1)", "IA8 (4:4)", "IA16 (8:8)", "I4", "I8"};
        animationKeyControl(pass, AnimationProperty::N64TextureFormat, timeline,
          ImGui::Combo("##n64-format", &state.n64.textureFormat, formats, 9));
        if (imageAssetSource && (state.n64.textureFormat == 2 || state.n64.textureFormat == 3))
          ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.34f, 1.0f),
            "No generated TLUT: CI is approximated as intensity");
        const char* tileLabels[] = {"16", "32", "64"};
        constexpr int tileValues[] = {16, 32, 64};
        int widthIndex = state.n64.tileWidth == 16 ? 0 : state.n64.tileWidth == 64 ? 2 : 1;
        int heightIndex = state.n64.tileHeight == 16 ? 0 : state.n64.tileHeight == 64 ? 2 : 1;
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::Combo("Tile width", &widthIndex, tileLabels, 3)) state.n64.tileWidth = tileValues[widthIndex];
        ImGui::SetNextItemWidth(90.0f);
        if (ImGui::Combo("Tile height", &heightIndex, tileLabels, 3)) state.n64.tileHeight = tileValues[heightIndex];
        const int bytes = n64TextureBytes(state.n64);
        if (bytes <= 4096) ImGui::Text("TMEM working set: %d / 4096 bytes", bytes);
        else ImGui::TextColored(ImVec4(0.95f, 0.45f, 0.35f, 1.0f), "TMEM working set: %d / 4096 bytes", bytes);
        description("The calculation includes tile texels and CI palette storage. Oversized tiles would require subdivision or another format.");
      } else {
        if (imageAssetSource) {
          fixedProfileValue("Texture color storage", "Direct color (image asset)");
          description("Palette generation is not implicit. Indexed storage remains available for the built-in diagnostic texture.");
        } else {
          ImGui::TextUnformatted("Texture color storage");
          const char* colorModeLabels[] = {"Direct color", "8-bit index + 256-color CLUT", "4-bit index + 16-color CLUT"};
          animationKeyControl(pass, AnimationProperty::TextureColorStorage, timeline,
            ImGui::Combo("##texture-color-storage", &state.texture.colorMode, colorModeLabels, 3));
          description("Indexed textures store palette entries rather than RGB texels. CLUT means color lookup table.");
        }
      }
      break;
    }
    case Category::Lighting: {
      ImGui::TextUnformatted("LIGHTING"); ImGui::Separator();
      ImGui::TextUnformatted("Lighting model");
      if (capabilities.perFragmentLighting) {
        const char* lightingLabels[] = {"Unlit", "Gouraud / per-vertex Lambert", "Phong shading / per-fragment Lambert",
          "Phong reflection", "Blinn-Phong reflection"};
        animationKeyControl(pass, AnimationProperty::LightingModel, timeline,
          ImGui::Combo("##lighting-model", &state.lighting.model, lightingLabels, 5));
      } else {
        const char* lightingLabels[] = {"Unlit", "Gouraud / per-vertex Lambert"};
        animationKeyControl(pass, AnimationProperty::LightingModel, timeline,
          ImGui::Combo("##lighting-model", &state.lighting.model, lightingLabels, 2));
      }
      description("Gouraud interpolates computed vertex lighting; Phong shading interpolates normals and lights each fragment.");
      animationKeyControl(pass, AnimationProperty::Ambient, timeline,
        ImGui::SliderFloat("Ambient term", &state.lighting.ambient, 0.0f, 1.0f, "%.2f"));
      if (capabilities.perFragmentLighting && state.lighting.model >= 3) {
        animationKeyControl(pass, AnimationProperty::Shininess, timeline,
          ImGui::SliderFloat("Specular exponent", &state.lighting.shininess, 2.0f, 128.0f, "%.0f", ImGuiSliderFlags_Logarithmic));
      }
      ImGui::TextUnformatted("Directional light orientation");
      const DirectionFieldResult directionChanged = directionField("##light-direction",
        state.lighting.azimuth, state.lighting.elevation);
      ImGui::PushID("light-direction-field-animation-keys");
      animationKeyControl(pass, AnimationProperty::LightAzimuth, timeline, directionChanged.azimuthChanged);
      animationKeyControl(pass, AnimationProperty::LightElevation, timeline, directionChanged.elevationChanged);
      ImGui::PopID();
      ImGui::SetNextItemWidth(std::max(90.0f, ImGui::GetContentRegionAvail().x * 0.5f - 5.0f));
      const bool azimuthChanged = ImGui::DragFloat("##light-azimuth", &state.lighting.azimuth,
        0.25f, -180.0f, 180.0f, "Azimuth %.1f deg");
      animationKeyControl(pass, AnimationProperty::LightAzimuth, timeline, azimuthChanged);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.0f);
      const bool elevationChanged = ImGui::DragFloat("##light-elevation", &state.lighting.elevation,
        0.25f, -90.0f, 90.0f, "Elevation %.1f deg");
      animationKeyControl(pass, AnimationProperty::LightElevation, timeline, elevationChanged);
      description("Azimuth rotates around the vertical axis; elevation moves above or below the horizon.");
      if (capabilities.shadowMapping) {
        animationKeyControl(pass, AnimationProperty::ShadowsEnabled, timeline,
          ImGui::Checkbox("Directional shadow map", &state.lighting.shadows));
        ImGui::BeginDisabled(!state.lighting.shadows);
        const char* shadowResolutionLabels[] = {"256 x 256", "512 x 512", "1024 x 1024", "2048 x 2048"};
        const int shadowResolutions[] = {256, 512, 1024, 2048};
        int shadowResolutionIndex = state.lighting.shadowResolution == 256 ? 0 : state.lighting.shadowResolution == 512 ? 1 :
          state.lighting.shadowResolution == 2048 ? 3 : 2;
        if (ImGui::Combo("Shadow-map resolution", &shadowResolutionIndex, shadowResolutionLabels, 4))
          state.lighting.shadowResolution = shadowResolutions[shadowResolutionIndex];
        animationKeyControl(pass, AnimationProperty::ShadowBias, timeline,
          ImGui::SliderFloat("Depth comparison bias", &state.lighting.shadowBias, 0.0f, 0.02f, "%.5f", ImGuiSliderFlags_Logarithmic));
        animationKeyControl(pass, AnimationProperty::ShadowPcf, timeline,
          ImGui::Checkbox("3 x 3 percentage-closer filtering", &state.lighting.shadowPcf));
        animationKeyControl(pass, AnimationProperty::ShadowMapVisualization, timeline,
          ImGui::Checkbox("Visualize light-space depth", &state.lighting.visualizeShadowMap));
        ImGui::EndDisabled();
        description("Renders scene depth from the light, then compares each camera fragment against that depth map.");
      }
      if (profile == HardwareProfile::Nintendo64) {
        animationKeyControl(pass, AnimationProperty::N64TextureGeneration, timeline,
          ImGui::Checkbox("RSP texture-coordinate generation", &state.n64.textureGeneration));
        description("Generates sphere-map-like texture coordinates from transformed vertex normals for reflection approximations.");
      }
      animationKeyControl(pass, AnimationProperty::DepthCueEnabled, timeline,
        ImGui::Checkbox(profile == HardwareProfile::Nintendo64 ? "RSP vertex fog" : "Vertex depth cueing", &state.lighting.depthCue));
      ImGui::BeginDisabled(!state.lighting.depthCue);
      ImGui::TextUnformatted("Depth-cue interval");
      const float cueMaximum = std::max(30.0f, state.lighting.depthCueEnd * 1.2f);
      const IntervalFieldResult cueChanged = intervalField("##depth-cue-range",
        state.lighting.depthCueStart, state.lighting.depthCueEnd, 0.0f, cueMaximum,
        "surface color", "far color");
      ImGui::PushID("depth-cue-field-animation-keys");
      animationKeyControl(pass, AnimationProperty::DepthCueStart, timeline, cueChanged.startChanged);
      animationKeyControl(pass, AnimationProperty::DepthCueEnd, timeline, cueChanged.endChanged);
      ImGui::PopID();
      ImGui::SetNextItemWidth(std::max(90.0f, ImGui::GetContentRegionAvail().x * 0.5f - 5.0f));
      const bool cueStartChanged = ImGui::DragFloat("##cue-start", &state.lighting.depthCueStart,
        0.01f, 0.0f, state.lighting.depthCueEnd, "Start %.2f units");
      animationKeyControl(pass, AnimationProperty::DepthCueStart, timeline, cueStartChanged);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.0f);
      const bool cueEndChanged = ImGui::DragFloat("##cue-end", &state.lighting.depthCueEnd,
        0.01f, state.lighting.depthCueStart, 30.0f, "End %.2f units");
      animationKeyControl(pass, AnimationProperty::DepthCueEnd, timeline, cueEndChanged);
      animationKeyControl(pass, AnimationProperty::FarColor, timeline,
        ImGui::ColorEdit3("Far color", &state.lighting.farColor.x, ImGuiColorEditFlags_NoInputs));
      ImGui::EndDisabled();
      description(profile == HardwareProfile::Nintendo64
        ? "Computes fog alpha at vertices and interpolates it; the lab applies the far-color blend in the RDP material path."
        : "Computes a depth factor at vertices, interpolates it, then blends shaded color toward the far color.");
      break;
    }
    case Category::Field: {
      ImGui::TextUnformatted("FIELD"); ImGui::Separator();
      animationKeyControl(pass, AnimationProperty::FieldEnabled, timeline,
        ImGui::Checkbox("Evaluate world-space field", &state.field.enabled));
      const char* producerKinds[] = {"Wave interference", "Signed distance field", "Persistent elemental simulation"};
      bool producerKindChanged = ImGui::Combo("Producer family", &state.field.producerKind,
        producerKinds, 3);
      animationKeyControl(pass, AnimationProperty::FieldProducerKind, timeline, producerKindChanged);
      description("Selects what scalar quantity the pass field buffer stores. SDF mode preserves signed distance in world units.");
      ImGui::BeginDisabled(!state.field.enabled);
      ImGui::BeginDisabled(state.field.producerKind != 0);
      ImGui::TextUnformatted("Source positions and interference preview");
      const FieldSourceCanvasResult sourceCanvas = fieldSourceCanvas("##field-sources",
        state.field.sourceA, state.field.sourceB, state.field.wavelength, state.field.phaseOffset,
        state.field.amplitudeA, state.field.amplitudeB, state.field.falloff);
      ImGui::PushID("field-source-canvas-animation-keys");
      animationKeyControl(pass, AnimationProperty::FieldSourceA, timeline, sourceCanvas.sourceAChanged);
      animationKeyControl(pass, AnimationProperty::FieldSourceB, timeline, sourceCanvas.sourceBChanged);
      ImGui::PopID();
      animationKeyControl(pass, AnimationProperty::FieldSourceA, timeline,
        ImGui::DragFloat3("Source A position", &state.field.sourceA.x, 0.01f, -8.0f, 8.0f, "%.2f"));
      animationKeyControl(pass, AnimationProperty::FieldSourceB, timeline,
        ImGui::DragFloat3("Source B position", &state.field.sourceB.x, 0.01f, -8.0f, 8.0f, "%.2f"));
      ImGui::SeparatorText("WAVE MODEL");
      animationKeyControl(pass, AnimationProperty::FieldWavelength, timeline,
        ImGui::DragFloat("Wavelength", &state.field.wavelength, 0.005f, 0.05f, 8.0f, "%.3f units"));
      animationKeyControl(pass, AnimationProperty::FieldPhaseOffset, timeline,
        ImGui::SliderAngle("Relative phase", &state.field.phaseOffset, -180.0f, 180.0f, "%.1f deg"));
      ImGui::SetNextItemWidth(std::max(90.0f, ImGui::GetContentRegionAvail().x * 0.5f - 5.0f));
      const bool amplitudeAChanged = ImGui::DragFloat("##field-amplitude-a", &state.field.amplitudeA,
        0.01f, 0.0f, 4.0f, "A amplitude %.2f");
      animationKeyControl(pass, AnimationProperty::FieldAmplitudeA, timeline, amplitudeAChanged);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.0f);
      const bool amplitudeBChanged = ImGui::DragFloat("##field-amplitude-b", &state.field.amplitudeB,
        0.01f, 0.0f, 4.0f, "B amplitude %.2f");
      animationKeyControl(pass, AnimationProperty::FieldAmplitudeB, timeline, amplitudeBChanged);
      animationKeyControl(pass, AnimationProperty::FieldFalloff, timeline,
        ImGui::DragFloat("Distance falloff", &state.field.falloff, 0.005f, 0.0f, 2.0f, "%.3f"));
      ImGui::SeparatorText("VISUALIZE");
      constexpr const char* viewLabels[] = {"Source A phase", "Source B phase", "Phase difference",
        "Interference intensity", "Absolute distance difference", "Distance-difference contours"};
      bool fieldVisualizationChanged = false;
      if (ImGui::BeginTable("field-visualizations", 2, ImGuiTableFlags_SizingStretchSame)) {
        for (int view = 0; view < 6; ++view) {
          ImGui::TableNextColumn();
          if (ImGui::Selectable(viewLabels[view], state.field.visualization == view,
              ImGuiSelectableFlags_None, ImVec2(0.0f, ImGui::GetFrameHeight())))
            { state.field.visualization = view; fieldVisualizationChanged = true; }
        }
        ImGui::EndTable();
      }
      animationKeyControl(pass, AnimationProperty::FieldVisualization, timeline, fieldVisualizationChanged);
      animationKeyControl(pass, AnimationProperty::FieldBandSharpness, timeline,
        ImGui::DragFloat("Band sharpness", &state.field.bandSharpness, 0.01f, 0.1f, 8.0f, "%.2f"));
      description("Interference intensity evaluates |E_A + E_B| squared.");
      ImGui::EndDisabled();
      ImGui::SeparatorText("PERSISTENT ELEMENTAL MEDIUM");
      ImGui::BeginDisabled(state.field.producerKind != 2);
      if (!editingSceneDefaults)
        description("The medium is shared by the whole scene. Edit its injector under Scene Defaults; passes independently choose which persistent channel they expose.");
      ImGui::BeginDisabled(!editingSceneDefaults);
      animationKeyControl(pass, AnimationProperty::FieldSourceA, timeline,
        ImGui::DragFloat3("Injector position XYZ", &state.field.sourceA.x, 0.01f, -8.0f, 8.0f, "%.2f"));
      animationKeyControl(pass, AnimationProperty::FieldWavelength, timeline,
        ImGui::DragFloat("Injector radius", &state.field.wavelength, 0.01f, 0.08f, 4.0f, "%.2f units"));
      animationKeyControl(pass, AnimationProperty::FieldAmplitudeA, timeline,
        ImGui::DragFloat("Heat injection rate", &state.field.amplitudeA, 0.01f, 0.0f, 4.0f, "%.2f"));
      animationKeyControl(pass, AnimationProperty::FieldAmplitudeB, timeline,
        ImGui::DragFloat("Fuel injection rate", &state.field.amplitudeB, 0.01f, 0.0f, 4.0f, "%.2f"));
      animationKeyControl(pass, AnimationProperty::FieldPhaseOffset, timeline,
        ImGui::SliderAngle("Jet direction", &state.field.phaseOffset, -180.0f, 180.0f, "%.1f deg"));
      animationKeyControl(pass, AnimationProperty::FieldFalloff, timeline,
        ImGui::DragFloat("Jet strength", &state.field.falloff, 0.01f, 0.0f, 2.0f, "%.2f"));
      ImGui::EndDisabled();
      constexpr const char* simulationChannels[] = {"Temperature", "Smoke density", "Fuel", "Pressure",
        "Flow speed", "Moisture", "Combustion rate"};
      bool simulationChannelChanged = ImGui::Combo("Exposed field channel", &state.field.visualization,
        simulationChannels, 7);
      animationKeyControl(pass, AnimationProperty::FieldVisualization, timeline, simulationChannelChanged);
      description("The chamber retains these quantities between frames. This pass selects one channel for surface consumers and its named R16F field output.");
      ImGui::EndDisabled();
      ImGui::SeparatorText("SIGNED DISTANCE PRODUCERS");
      ImGui::BeginDisabled(state.field.producerKind != 1);
      constexpr const char* sdfTypes[] = {"Sphere", "Box", "Torus"};
      const auto sdfParameterControls = [&](const char* id, RendererState::Field::SdfProducer& producer,
          const AnimationProperty property) {
        ImGui::PushID(id);
        bool changed = false;
        if (producer.type == 0) {
          changed = ImGui::DragFloat("Radius", &producer.parameters.x, 0.01f, 0.01f, 8.0f, "%.2f units");
        } else if (producer.type == 1) {
          changed = ImGui::DragFloat3("Half-extents XYZ", &producer.parameters.x,
            0.01f, 0.01f, 8.0f, "%.2f units");
        } else {
          changed = ImGui::DragFloat("Major radius", &producer.parameters.x,
            0.01f, 0.01f, 8.0f, "%.2f units");
          changed |= ImGui::DragFloat("Tube radius", &producer.parameters.y,
            0.01f, 0.01f, 8.0f, "%.2f units");
        }
        animationKeyControl(pass, property, timeline, changed);
        ImGui::PopID();
      };
      bool sdfATypeChanged = ImGui::Combo("Producer A", &state.field.sdfA.type, sdfTypes, 3);
      animationKeyControl(pass, AnimationProperty::SdfAType, timeline, sdfATypeChanged);
      animationKeyControl(pass, AnimationProperty::SdfAPosition, timeline,
        ImGui::DragFloat3("A position", &state.field.sdfA.position.x, 0.01f, -8.0f, 8.0f, "%.2f"));
      sdfParameterControls("producer-a-parameters", state.field.sdfA, AnimationProperty::SdfAParameters);
      bool sdfBTypeChanged = ImGui::Combo("Producer B", &state.field.sdfB.type, sdfTypes, 3);
      animationKeyControl(pass, AnimationProperty::SdfBType, timeline, sdfBTypeChanged);
      animationKeyControl(pass, AnimationProperty::SdfBPosition, timeline,
        ImGui::DragFloat3("B position", &state.field.sdfB.position.x, 0.01f, -8.0f, 8.0f, "%.2f"));
      sdfParameterControls("producer-b-parameters", state.field.sdfB, AnimationProperty::SdfBParameters);
      constexpr const char* sdfOperations[] = {"Union: min(A, B)", "Intersection: max(A, B)",
        "Difference: max(A, -B)", "Smooth union"};
      bool sdfOperationChanged = ImGui::Combo("Combination", &state.field.sdfOperation,
        sdfOperations, 4);
      animationKeyControl(pass, AnimationProperty::SdfOperation, timeline, sdfOperationChanged);
      ImGui::BeginDisabled(state.field.sdfOperation != 3);
      animationKeyControl(pass, AnimationProperty::SdfSmoothness, timeline,
        ImGui::DragFloat("Smooth-union radius", &state.field.sdfSmoothness,
          0.005f, 0.001f, 4.0f, "%.3f units"));
      ImGui::EndDisabled();
      animationKeyControl(pass, AnimationProperty::SdfPreviewRange, timeline,
        ImGui::DragFloat("Signed preview range", &state.field.sdfPreviewRange,
          0.01f, 0.01f, 10.0f, "%.2f units"));
      description("Each producer evaluates signed distance in world space: negative inside, zero at its boundary, positive outside.");
      ImGui::SeparatorText("ISO-SURFACE");
      animationKeyControl(pass, AnimationProperty::IsoSurfaceEnabled, timeline,
        ImGui::Checkbox("Ray-march iso-surface", &state.field.isoSurfaceEnabled));
      animationKeyControl(pass, AnimationProperty::IsoLevel, timeline,
        ImGui::DragFloat("Iso level", &state.field.isoLevel, 0.005f, -4.0f, 4.0f, "%.3f units"));
      animationKeyControl(pass, AnimationProperty::IsoColor, timeline,
        ImGui::ColorEdit3("Iso-surface color", &state.field.isoColor.x));
      animationKeyControl(pass, AnimationProperty::IsoMaximumSteps, timeline,
        ImGui::DragInt("Maximum march steps", &state.field.isoMaxSteps, 1.0f, 8, 512));
      animationKeyControl(pass, AnimationProperty::IsoHitEpsilon, timeline,
        ImGui::DragFloat("Hit epsilon", &state.field.isoEpsilon,
          0.0001f, 0.0001f, 0.1f, "%.4f units"));
      animationKeyControl(pass, AnimationProperty::IsoMaximumDistance, timeline,
        ImGui::DragFloat("Maximum ray distance", &state.field.isoMaxDistance,
          0.1f, 1.0f, 100.0f, "%.1f units"));
      description("The zero crossing is a real implicit surface. Ray marching writes window depth, so raster geometry and iso-surfaces occlude one another.");
      ImGui::EndDisabled();
      ImGui::SeparatorText("FIELD PREVIEW");
      animationKeyControl(pass, AnimationProperty::FieldLowColor, timeline,
        ImGui::ColorEdit3(state.field.producerKind == 1 ? "Negative-distance color" : "Low-value color",
          &state.field.lowColor.x));
      animationKeyControl(pass, AnimationProperty::FieldHighColor, timeline,
        ImGui::ColorEdit3(state.field.producerKind == 1 ? "Positive-distance color" : "High-value color",
          &state.field.highColor.x));
      description("These colors affect only Field signal preview. Named pass-field inputs retain the raw scalar value, including negative SDF distances.");
      ImGui::SeparatorText("CONSUMERS");
      animationKeyControl(pass, AnimationProperty::FieldVertexDisplacement, timeline,
        ImGui::DragFloat("Vertex normal displacement", &state.field.vertexDisplacement,
          0.005f, -2.0f, 2.0f, "%.3f units"));
      animationKeyControl(pass, AnimationProperty::FieldSignedDisplacement, timeline,
        ImGui::Checkbox("Signed displacement (-1..1)", &state.field.signedDisplacement));
      description("Samples the field once per mesh vertex, then moves that vertex along its transformed normal. Mesh density therefore changes the result.");
      description(state.field.producerKind == 0
        ? "The Field interference scene places a 16x8 torus beside a 64x32 torus so the sampling difference is directly visible."
        : "SDF consumers convert distance to proximity around the selected iso-level. Mesh density still controls how finely vertex deformation can follow that field.");
      animationKeyControl(pass, AnimationProperty::FieldDiscardEnabled, timeline,
        ImGui::Checkbox("Discard below field threshold", &state.field.discardBelowEnabled));
      ImGui::BeginDisabled(!state.field.discardBelowEnabled);
      animationKeyControl(pass, AnimationProperty::FieldDiscardThreshold, timeline,
        ImGui::SliderFloat("Discard threshold", &state.field.discardThreshold, 0.0f, 1.0f, "%.3f"));
      ImGui::EndDisabled();
      description("Interpolates the per-vertex signal across each triangle and discards fragments below the threshold. This opens actual holes in depth and color.");
      animationKeyControl(pass, AnimationProperty::FieldSurfaceColorInfluence, timeline,
        ImGui::SliderFloat("Surface color influence", &state.field.surfaceColorInfluence,
          0.0f, 1.0f, "%.2f"));
      animationKeyControl(pass, AnimationProperty::FieldEmissionInfluence, timeline,
        ImGui::SliderFloat("Emission influence", &state.field.emissionInfluence,
          0.0f, 8.0f, "%.2fx", ImGuiSliderFlags_Logarithmic));
      description("Surface color replaces shaded material color; emission adds field color after lighting. They are independent consumers of the same scalar signal.");
      ImGui::EndDisabled();
      break;
    }
    case Category::Spectral: {
      ImGui::TextUnformatted("SPECTRAL LIGHT"); ImGui::Separator();
      constexpr const char* illuminants[] = {"Reference daylight", "Tungsten 2856 K", "Tri-band LED"};
      constexpr const char* observers[] = {"Reference human LMS", "Shifted observer", "Rod monochrome"};
      animationKeyControl(pass, AnimationProperty::SpectralIlluminant, timeline,
        ImGui::Combo("Illuminant", &state.spectral.illuminant, illuminants, 3));
      animationKeyControl(pass, AnimationProperty::SpectralObserver, timeline,
        ImGui::Combo("Observer", &state.spectral.observer, observers, 3));
      animationKeyControl(pass, AnimationProperty::SpectralExposure, timeline,
        ImGui::SliderFloat("Presentation exposure", &state.spectral.exposure, -4.0f, 4.0f, "%+.2f stops"));
      description("The scene retains sixteen radiance samples from 400 to 700 nm. RGB is produced only after the selected observer integrates those samples.");
      ImGui::SeparatorText("REFLECTANCE SPECTRA");
      const ImVec2 plotOrigin = ImGui::GetCursorScreenPos();
      const ImVec2 plotSize(std::max(240.0f, ImGui::GetContentRegionAvail().x), 150.0f);
      ImDrawList* draw = ImGui::GetWindowDrawList();
      const ImVec2 plotEnd(plotOrigin.x + plotSize.x, plotOrigin.y + plotSize.y);
      draw->AddRectFilled(plotOrigin, plotEnd, IM_COL32(18, 20, 23, 255));
      draw->AddRect(plotOrigin, plotEnd, IM_COL32(70, 74, 80, 255));
      for (int band = 0; band < static_cast<int>(spectral::bandCount) - 1; ++band) {
        const float x0 = plotOrigin.x + plotSize.x * static_cast<float>(band) / 15.0f;
        const float x1 = plotOrigin.x + plotSize.x * static_cast<float>(band + 1) / 15.0f;
        const auto point = [&](const float x, const float value) { return ImVec2(x,
          plotOrigin.y + plotSize.y * (1.0f - std::clamp(value, 0.0f, 1.0f))); };
        draw->AddLine(point(x0, spectral::reflectanceA[band]), point(x1, spectral::reflectanceA[band + 1]),
          IM_COL32(92, 205, 255, 255), 2.0f);
        draw->AddLine(point(x0, spectral::reflectanceB[band]), point(x1, spectral::reflectanceB[band + 1]),
          IM_COL32(255, 174, 74, 255), 2.0f);
      }
      ImGui::Dummy(plotSize);
      ImGui::TextColored(ImVec4(0.36f, 0.80f, 1.0f, 1.0f), "A"); ImGui::SameLine();
      ImGui::TextColored(ImVec4(1.0f, 0.68f, 0.29f, 1.0f), "B"); ImGui::SameLine();
      ImGui::TextDisabled("  400 nm                              700 nm");
      if (state.spectral.illuminant == 0 && state.spectral.observer == 0)
        ImGui::TextWrapped("Reference condition: A and B are metamers. Their spectra differ, but their integrated LMS responses match.");
      else
        ImGui::TextWrapped("Non-reference condition: the illuminant or receptor curves weight the spectra differently, so the match can fail.");
      break;
    }
    case Category::Depth: {
      ImGui::TextUnformatted("DEPTH"); ImGui::Separator();
      if (profile == HardwareProfile::Nintendo64) {
        animationKeyControl(pass, AnimationProperty::N64ZCompare, timeline,
          ImGui::Checkbox("RDP Z compare", &state.n64.zCompare));
        animationKeyControl(pass, AnimationProperty::N64ZUpdate, timeline,
          ImGui::Checkbox("RDP Z update", &state.n64.zUpdate));
        fixedProfileValue("Z representation", "Compressed Z + delta-Z");
        description("Surface mode selects ordinary, translucent, decal, or interpenetrating semantics; compare and update remain explicit flags.");
      } else if (capabilities.depthBuffer) {
        animationKeyControl(pass, AnimationProperty::DepthTestEnabled, timeline,
          ImGui::Checkbox("Depth testing", &state.depth.testing));
        description("Compares each fragment's depth against the stored depth value before drawing it.");
        animationKeyControl(pass, AnimationProperty::DepthWriteEnabled, timeline,
          ImGui::Checkbox("Depth writes", &state.depth.writing));
        description("Stores passing fragment depths in the depth buffer. Disabling the depth test also prevents writes.");
        ImGui::TextUnformatted("Depth comparison function");
        const char* functionLabels[] = {"Less", "Less or equal", "Greater", "Always"};
        animationKeyControl(pass, AnimationProperty::DepthComparison, timeline,
          ImGui::Combo("##depth-function", &state.depth.function, functionLabels, 4));
        description("Determines which comparison between incoming and stored depth values passes.");
        ImGui::TextUnformatted("Depth buffer precision");
        const char* depthLabels[] = {"16-bit fixed point", "24-bit fixed point"};
        int selected = state.depth.precision == 16 ? 0 : 1;
        if (ImGui::Combo("##depth-precision", &selected, depthLabels, 2)) state.depth.precision = selected == 0 ? 16 : 24;
        description("Sets the actual storage precision of the framebuffer's depth attachment.");
        ImGui::TextUnformatted("Depth visualization");
        const char* viewLabels[] = {"Off", "Raw window-space depth", "Linear camera depth (0-10 units)"};
        animationKeyControl(pass, AnimationProperty::DepthVisualization, timeline,
          ImGui::Combo("##depth-view", &state.depth.visualization, viewLabels, 3));
        description("Raw perspective depth is nonlinear; linearization reconstructs camera-space distance.");
      } else {
        fixedProfileValue("Opaque visibility", "Depth-buffer emulation");
        description("The PS1 had no depth buffer. The lab keeps opaque objects stable because its ordering table currently sorts objects, not individual mesh triangles.");
      }
      animationKeyControl(pass, AnimationProperty::OrderingTableEnabled, timeline,
        ImGui::Checkbox("Object ordering table", &state.depth.orderingTable));
      ImGui::BeginDisabled(!state.depth.orderingTable);
      animationKeyControl(pass, AnimationProperty::OrderingBuckets, timeline,
        ImGui::SliderInt("Depth buckets", &state.depth.orderingBuckets, 4, 256));
      ImGui::EndDisabled();
      description("Bins transparent objects by camera depth and submits far buckets first with depth testing disabled. Granularity: object, not polygon.");
      break;
    }
    case Category::Stencil:
      ImGui::TextUnformatted("STENCIL"); ImGui::Separator();
      animationKeyControl(pass, AnimationProperty::StencilEnabled, timeline,
        ImGui::Checkbox("Two-pass stencil mask", &state.stencil.enabled));
      description("First pass writes a projected sphere silhouette while color and depth writes are disabled.");
      animationKeyControl(pass, AnimationProperty::StencilReference, timeline,
        ImGui::SliderInt("Reference value", &state.stencil.reference, 0, 255));
      animationKeyControl(pass, AnimationProperty::StencilInverted, timeline,
        radioPair("Equal", "Not equal", state.stencil.invert));
      description("Second pass compares each stored 8-bit stencil value against the reference before shading.");
      ImGui::TextDisabled("Pass 1: Always / Replace");
      ImGui::TextDisabled("Pass 2: Equal or Not equal / Keep");
      ImGui::TextDisabled("Attachment: DEPTH24_STENCIL8");
      description("Select the Stencil mask scene to isolate this operation.");
      break;
    case Category::Color: {
      ImGui::TextUnformatted("COLOR"); ImGui::Separator();
      if (profile == HardwareProfile::Nintendo64) {
        ImGui::TextUnformatted("RDP framebuffer format");
        const char* framebufferFormats[] = {"RGBA16 (5:5:5:1 + coverage)", "RGBA32 (8:8:8:8)"};
        animationKeyControl(pass, AnimationProperty::N64FramebufferFormat, timeline,
          ImGui::Combo("##n64-framebuffer", &state.n64.framebufferFormat, framebufferFormats, 2));
        ImGui::TextUnformatted("RDP color dithering");
        const char* ditherModes[] = {"Disabled", "Magic-square 4 x 4", "Bayer 4 x 4", "Noise"};
        ImGui::BeginDisabled(state.n64.framebufferFormat == 1);
        animationKeyControl(pass, AnimationProperty::N64ColorDither, timeline,
          ImGui::Combo("##n64-color-dither", &state.n64.colorDither, ditherModes, 4));
        ImGui::EndDisabled();
        description("Dither is applied before reduced-precision framebuffer storage. Patterns are signal-level approximations.");
        fixedProfileValue("Lighting color space", "Encoded RGB");
      } else if (capabilities.configurableColorDepth) {
        ImGui::TextUnformatted("Output color depth");
        const char* labels[] = {"24-bit (8:8:8)", "15-bit (5:5:5)", "12-bit (4:4:4)"};
        int selected = state.color.bitsPerChannel == 8 ? 0 : state.color.bitsPerChannel == 5 ? 1 : 2;
        const bool colorDepthChanged = ImGui::Combo("##depth", &selected, labels, 3);
        if (colorDepthChanged) state.color.bitsPerChannel = selected == 0 ? 8 : selected == 1 ? 5 : 4;
        animationKeyControl(pass, AnimationProperty::BitsPerChannel, timeline, colorDepthChanged);
        description("Quantizes each output color channel to a fixed number of levels.");
      } else {
        fixedProfileValue("Output color depth", "15-bit RGB (5:5:5)");
      }
      if (profile != HardwareProfile::Nintendo64) {
        animationKeyControl(pass, AnimationProperty::DitheringEnabled, timeline,
          ImGui::Checkbox("Ordered dithering (4 x 4 Bayer)", &state.color.dithering));
        description("Offsets pixels with a fixed threshold matrix before color quantization.");
      }
      if (profile != HardwareProfile::Nintendo64 && capabilities.linearLight) {
        ImGui::TextUnformatted("Lighting color space");
        animationKeyControl(pass, AnimationProperty::LinearLight, timeline,
          radioPair("Encoded RGB (incorrect)", "Linear light", state.color.linearLight));
        description("Linear-light mode decodes texture values before lighting and encodes the final image for display.");
      } else if (profile == HardwareProfile::PlayStation) {
        fixedProfileValue("Lighting color space", "Encoded RGB");
      }
      break;
    }
    case Category::Post: {
      ImGui::TextUnformatted("POST"); ImGui::Separator();
      animationKeyControl(pass, AnimationProperty::FogEnabled, timeline,
        ImGui::Checkbox("Linear distance fog", &state.post.fog));
      description("Blends shaded fragments toward the background according to camera distance.");
      ImGui::BeginDisabled(!state.post.fog);
      ImGui::TextUnformatted("Fog interval");
      const float fogMaximum = std::max(12.0f, state.post.fogEnd * 1.2f);
      const IntervalFieldResult fogChanged = intervalField("##fog-range", state.post.fogStart,
        state.post.fogEnd, 0.0f, fogMaximum, "clear", "fully fogged");
      ImGui::PushID("fog-field-animation-keys");
      animationKeyControl(pass, AnimationProperty::FogStart, timeline, fogChanged.startChanged);
      animationKeyControl(pass, AnimationProperty::FogEnd, timeline, fogChanged.endChanged);
      ImGui::PopID();
      ImGui::SetNextItemWidth(std::max(90.0f, ImGui::GetContentRegionAvail().x * 0.5f - 5.0f));
      const bool fogStartChanged = ImGui::DragFloat("##fog-start", &state.post.fogStart,
        0.01f, 0.0f, state.post.fogEnd, "Start %.2f units");
      animationKeyControl(pass, AnimationProperty::FogStart, timeline, fogStartChanged);
      ImGui::SameLine();
      ImGui::SetNextItemWidth(-1.0f);
      const bool fogEndChanged = ImGui::DragFloat("##fog-end", &state.post.fogEnd,
        0.01f, state.post.fogStart, 100.0f, "End %.2f units");
      animationKeyControl(pass, AnimationProperty::FogEnd, timeline, fogEndChanged);
      ImGui::EndDisabled();
      description("Start is fully clear; end is fully fogged.");
      animationKeyControl(pass, AnimationProperty::OverdrawEnabled, timeline,
        ImGui::Checkbox("Overdraw visualization", &state.post.overdraw));
      ImGui::BeginDisabled(!state.post.overdraw);
      animationKeyControl(pass, AnimationProperty::OverdrawRange, timeline,
        ImGui::SliderFloat("Heat-map maximum", &state.post.overdrawRange, 1.0f, 32.0f, "%.0f fragments"));
      ImGui::EndDisabled();
      description("An additive floating-point pass counts rasterized fragments with depth testing disabled.");
      break;
    }
    case Category::Output: {
      ImGui::TextUnformatted("OUTPUT"); ImGui::Separator();
      ImGui::TextUnformatted("Internal render resolution");
      const char* unrestrictedLabels[] = {"1280 x 960", "640 x 480", "320 x 240", "256 x 192", "160 x 120"};
      const int unrestrictedWidths[] = {1280, 640, 320, 256, 160};
      const int unrestrictedHeights[] = {960, 480, 240, 192, 120};
      const char* playStationLabels[] = {"320 x 240", "256 x 192", "160 x 120"};
      const int playStationWidths[] = {320, 256, 160};
      const int playStationHeights[] = {240, 192, 120};
      const char* n64Labels[] = {"640 x 480", "320 x 240", "256 x 192", "160 x 120"};
      const int n64Widths[] = {640, 320, 256, 160};
      const int n64Heights[] = {480, 240, 192, 120};
      const bool ps1 = profile == HardwareProfile::PlayStation;
      const bool n64 = profile == HardwareProfile::Nintendo64;
      const char* const* labels = ps1 ? playStationLabels : n64 ? n64Labels : unrestrictedLabels;
      const int* widths = ps1 ? playStationWidths : n64 ? n64Widths : unrestrictedWidths;
      const int* heights = ps1 ? playStationHeights : n64 ? n64Heights : unrestrictedHeights;
      const int count = ps1 ? 3 : n64 ? 4 : 5;
      int selected = ps1 ? 0 : n64 ? 1 : 1;
      for (int i = 0; i < count; ++i) if (state.output.width == widths[i] && state.output.height == heights[i]) selected = i;
      if (ImGui::Combo("##resolution", &selected, labels, count)) { state.output.width = widths[selected]; state.output.height = heights[selected]; }
      description("Scene and output passes render at this exact pixel resolution.");
      if (profile != HardwareProfile::Unrestricted) {
        fixedProfileValue("Viewport upscaling", "Nearest");
      } else {
        ImGui::TextUnformatted("Viewport upscaling");
        animationKeyControl(pass, AnimationProperty::NearestUpscaling, timeline,
          radioPair("Bilinear", "Nearest", state.output.nearestUpscaling));
        description("Filters the completed internal-resolution framebuffer when enlarging it to the viewport.");
      }
      if (profile == HardwareProfile::Nintendo64) {
        animationKeyControl(pass, AnimationProperty::N64ViReconstruction, timeline,
          ImGui::Checkbox("VI reconstruction filter", &state.n64.viReconstruction));
        ImGui::BeginDisabled(!state.n64.viReconstruction);
        animationKeyControl(pass, AnimationProperty::N64ViDivot, timeline,
          ImGui::Checkbox("VI divot filter", &state.n64.viDivot));
        ImGui::EndDisabled();
        description("Approximates final Video Interface reconstruction and horizontal median divot removal on the rendered framebuffer.");
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
    case Category::Field: return "Field";
    case Category::Spectral: return "Spectral";
    case Category::Depth: return "Depth";
    case Category::Stencil: return "Stencil";
    case Category::Color: return "Color";
    case Category::Post: return "Post";
    case Category::Output: return "Output";
  }
  return "";
}

} // namespace gfxlab::ui
