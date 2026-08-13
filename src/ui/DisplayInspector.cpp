#include "ui/DisplayInspector.hpp"

#include "app/RenderStack.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>

namespace gfxlab::ui {
namespace {

void description(const char* text) {
  ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::TextWrapped("%s", text);
  ImGui::PopStyleColor();
}

} // namespace

void drawDisplayInspector(bool& open, RenderStack& stack) {
  if (!open) return;
  if (ImGui::Begin("Display Reconstruction", &open)) {
    keepCurrentWindowVisible();
    DisplayReconstructionState& state = stack.display();
    ImGui::Checkbox("Enable display reconstruction", &state.enabled);
    description("Runs after the complete render-pass stack. It never changes pass textures or composite operands.");
    if (state.enabled) {
      ImGui::Separator();
      ImGui::TextDisabled("SIGNAL RECONSTRUCTION");
      constexpr const char* signals[] = {"Direct RGB", "Composite NTSC approximation"};
      int signal = static_cast<int>(state.signal);
      if (ImGui::Combo("Input signal", &signal, signals, 2))
        state.signal = static_cast<DisplaySignal>(signal);
      if (state.signal == DisplaySignal::CompositeNtsc) {
        ImGui::SliderFloat("Chroma bleed", &state.chromaBleed, 0.0f, 1.0f, "%.2f");
        description("Reduces horizontal chroma bandwidth while retaining more luminance detail.");
        ImGui::SliderFloat("Luma/chroma crosstalk", &state.lumaChromaCrosstalk, 0.0f, 1.0f, "%.2f");
        description("Lets the encoded color carrier leak into luminance as alternating artifact color.");
      }

      ImGui::Spacing();
      ImGui::TextDisabled("CRT RESPONSE");
      ImGui::SliderFloat("Scanline strength", &state.scanlineStrength, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Aperture-grille strength", &state.phosphorMaskStrength, 0.0f, 1.0f, "%.2f");
      ImGui::SliderFloat("Bloom strength", &state.bloomStrength, 0.0f, 1.0f, "%.2f");
      if (state.bloomStrength > 0.0f)
        ImGui::SliderFloat("Bloom radius", &state.bloomRadiusPixels, 0.5f, 8.0f, "%.1f px");
      description("These controls model display response after signal decoding; they are not scene lighting or fog.");
    }
  }
  ImGui::End();
}

} // namespace gfxlab::ui
