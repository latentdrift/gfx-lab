#include "ui/TextureMappingEditor.hpp"

#include "app/Animation.hpp"
#include "app/FileDialog.hpp"
#include "app/PassEditing.hpp"
#include "app/RenderStack.hpp"
#include "assets/ModelAsset.hpp"
#include "ui/AnimationControls.hpp"
#include "ui/InstrumentWidgets.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace gfxlab::ui {
namespace {

void description(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

} // namespace

void drawTextureMappingEditor(bool& open, RenderStack& stack, AnimationTimeline& timeline,
    const ModelAsset* importedModel, const TestScene scene, const bool globalScope,
    const float timeSeconds, const unsigned int texturePreview) {
  if (!open) return;
  if (!ImGui::Begin("Texture Mapping", &open)) {
    ImGui::End();
    return;
  }
  keepCurrentWindowVisible();

  const RenderPass displayedBefore = globalScope
    ? evaluateRenderPass(stack.global(), timeSeconds)
    : materializeRenderPass(stack, stack.selectedIndex(), timeSeconds);
  RenderPass edited = displayedBefore;
  ImGui::TextDisabled("%s", globalScope ? "GLOBAL BASE" : stack.selected().name.c_str());

  ImGui::TextDisabled("IMAGE SOURCE");
  constexpr const char* sourceLabels[] = {"Scene material", "Built-in checker", "Imported override", "White texel"};
  int textureSource = static_cast<int>(edited.textureSource);
  ImGui::TextUnformatted("Texture source");
  const bool sourceChanged = ImGui::Combo("##texture-source", &textureSource, sourceLabels, 4);
  if (sourceChanged) edited.textureSource = static_cast<TextureSource>(textureSource);
  animationKeyControl(edited, AnimationProperty::TextureSource, timeline, sourceChanged);

  static std::string importError;
  if (ImGui::Button(edited.importedTexture == nullptr ? "Import image..." : "Replace image...")) {
    importError.clear();
    const FileDialogResult dialog = openTextureFileDialog();
    if (!dialog.error.empty()) importError = dialog.error;
    else if (dialog.path.has_value()) {
      const TextureImportResult imported = importTextureAsset(*dialog.path);
      if (imported) {
        edited.importedTexture = imported.asset;
        edited.importedTextureSrgb = true;
        edited.textureSource = TextureSource::ImportedOverride;
      } else importError = imported.error;
    }
    if (!importError.empty()) ImGui::OpenPopup("Texture mapping import failed");
  }
  if (edited.importedTexture != nullptr) {
    ImGui::SameLine();
    if (ImGui::Button("Reload")) {
      const TextureImportResult imported = importTextureAsset(edited.importedTexture->sourcePath);
      if (imported) edited.importedTexture = imported.asset;
      else {
        importError = imported.error;
        ImGui::OpenPopup("Texture mapping import failed");
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Remove")) {
      edited.importedTexture.reset();
      if (edited.textureSource == TextureSource::ImportedOverride)
        edited.textureSource = TextureSource::SceneMaterial;
    }
  }
  if (ImGui::BeginPopupModal("Texture mapping import failed", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::TextWrapped("%s", importError.c_str());
    if (ImGui::Button("Close")) ImGui::CloseCurrentPopup();
    ImGui::EndPopup();
  }

  if (edited.importedTexture != nullptr) {
    const TextureAsset& texture = *edited.importedTexture;
    ImGui::Text("%s", texture.name.c_str());
    ImGui::TextDisabled("%d x %d RGBA8   alpha %s", texture.width, texture.height,
      texture.hasAlpha ? "present" : "opaque");
    if (texturePreview != 0 && texture.width > 0 && texture.height > 0) {
      const float availableWidth = ImGui::GetContentRegionAvail().x;
      const float previewWidth = std::min(availableWidth, 300.0f);
      const float previewHeight = std::min(180.0f,
        previewWidth * static_cast<float>(texture.height) / static_cast<float>(texture.width));
      ImGui::Image(static_cast<ImTextureID>(texturePreview), ImVec2(previewWidth, previewHeight),
        ImVec2(0, 1), ImVec2(1, 0));
    }
    bool srgb = edited.importedTextureSrgb;
    const bool srgbChanged = ImGui::Checkbox("Decode image as sRGB color", &srgb);
    if (srgbChanged) edited.importedTextureSrgb = srgb;
    animationKeyControl(edited, AnimationProperty::TextureColorInterpretation, timeline, srgbChanged);
  } else if (edited.textureSource == TextureSource::ImportedOverride) {
    ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.34f, 1.0f), "No imported image is assigned");
  }

  ImGui::Separator();
  ImGui::TextDisabled("COORDINATES");
  constexpr const char* mappingLabels[] = {"Mesh UV0", "Planar XY", "Planar XZ", "Planar YZ"};
  int mapping = static_cast<int>(edited.perturbation.uvMapping);
  ImGui::TextUnformatted("Coordinate source");
  const bool mappingChanged = ImGui::Combo("##coordinate-source", &mapping, mappingLabels, 4);
  if (mappingChanged) edited.perturbation.uvMapping = static_cast<UvMapping>(mapping);
  animationKeyControl(edited, AnimationProperty::UvMapping, timeline, mappingChanged);
  if (edited.perturbation.uvMapping == UvMapping::MeshUv0) {
    if (scene == TestScene::ImportedModel && importedModel != nullptr && !importedModel->hasTextureCoordinates)
      ImGui::TextColored(ImVec4(0.95f, 0.58f, 0.34f, 1.0f),
        "This model has no UV0 coordinates. Choose a planar projection.");
    else description("Uses the texture coordinates authored on the mesh.");
  } else {
    description("Projects normalized model-space vertex positions onto the selected axis pair. Useful for unwrapped or diagnostic geometry.");
  }

  ImGui::TextUnformatted("UV transform");
  const UvCanvasResult canvasChanged = uvTransformCanvas("##uv-transform-canvas",
    edited.perturbation.uvOffset, edited.perturbation.uvScale,
    edited.perturbation.uvRotation, edited.perturbation.uvPivot, texturePreview);
  ImGui::PushID("uv-canvas-animation-keys");
  animationKeyControl(edited, AnimationProperty::UvOffset, timeline, canvasChanged.offsetChanged);
  animationKeyControl(edited, AnimationProperty::UvScale, timeline, canvasChanged.scaleChanged);
  animationKeyControl(edited, AnimationProperty::UvRotation, timeline, canvasChanged.rotationChanged);
  ImGui::PopID();

  ImGui::TextUnformatted("Exact transform");
  ImGui::TextUnformatted("Scale / tiling");
  animationKeyControl(edited, AnimationProperty::UvScale, timeline,
    ImGui::DragFloat2("##mapping-scale", &edited.perturbation.uvScale.x, 0.005f, -16.0f, 16.0f, "%.4f"));
  ImGui::TextUnformatted("Rotation");
  animationKeyControl(edited, AnimationProperty::UvRotation, timeline,
    ImGui::SliderAngle("##mapping-rotation", &edited.perturbation.uvRotation, -180.0f, 180.0f, "%.1f deg"));
  ImGui::TextUnformatted("Rotation pivot");
  animationKeyControl(edited, AnimationProperty::UvPivot, timeline,
    ImGui::DragFloat2("##mapping-pivot", &edited.perturbation.uvPivot.x, 0.005f, -2.0f, 2.0f, "%.3f"));
  ImGui::TextUnformatted("Offset");
  animationKeyControl(edited, AnimationProperty::UvOffset, timeline,
    ImGui::DragFloat2("##mapping-offset", &edited.perturbation.uvOffset.x, 1.0f / 512.0f, -4.0f, 4.0f, "%.5f"));
  if (ImGui::Button("Reset mapping")) {
    edited.perturbation.uvScale = glm::vec2(1.0f);
    edited.perturbation.uvRotation = 0.0f;
    edited.perturbation.uvPivot = glm::vec2(0.5f);
    edited.perturbation.uvOffset = glm::vec2(0.0f);
  }
  ImGui::SameLine();
  if (ImGui::Button("Flip U")) edited.perturbation.uvScale.x *= -1.0f;
  ImGui::SameLine();
  if (ImGui::Button("Flip V")) edited.perturbation.uvScale.y *= -1.0f;

  ImGui::Separator();
  ImGui::TextDisabled("SAMPLING");
  animationKeyControl(edited, AnimationProperty::TextureRepeat, timeline,
    ImGui::Checkbox("Repeat addressing", &edited.renderer.texture.repeat));
  description("When disabled, coordinates outside 0 to 1 clamp to the image edge.");
  bool bilinear = !edited.renderer.texture.nearestFiltering;
  const bool bilinearChanged = ImGui::Checkbox("Bilinear filtering", &bilinear);
  if (bilinearChanged) edited.renderer.texture.nearestFiltering = !bilinear;
  animationKeyControl(edited, AnimationProperty::NearestFiltering, timeline, bilinearChanged);
  animationKeyControl(edited, AnimationProperty::Mipmapping, timeline,
    ImGui::Checkbox("Mipmapping", &edited.renderer.texture.mipmapping));
  if (edited.renderer.texture.mipmapping && !edited.renderer.texture.nearestFiltering)
    animationKeyControl(edited, AnimationProperty::TrilinearFiltering, timeline,
      ImGui::Checkbox("Trilinear mip interpolation", &edited.renderer.texture.trilinear));

  if (globalScope)
    applyEditedPass(stack.global(), displayedBefore, edited);
  else
    applyEditedLocalPass(stack, displayedBefore, edited, timeSeconds);
  ImGui::End();
}

} // namespace gfxlab::ui
