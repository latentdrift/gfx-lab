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
      constexpr const char* signals[] = {
        "Direct RGB", "Composite NTSC approximation",
        "LMS receptor triplet", "Rod response", "Mesopic rod/cone mix",
        "L-cone response", "M-cone response", "S-cone response",
        "L - M opponent", "S - (L + M)/2 opponent",
        "Rod/cone absolute difference", "Rod/cone quantized XOR"
      };
      int signal = static_cast<int>(state.signal);
      if (ImGui::Combo("Input signal", &signal, signals, 12))
        state.signal = static_cast<DisplaySignal>(signal);
      if (state.signal == DisplaySignal::CompositeNtsc) {
        ImGui::SliderFloat("Chroma bleed", &state.chromaBleed, 0.0f, 1.0f, "%.2f");
        description("Reduces horizontal chroma bandwidth while retaining more luminance detail.");
        ImGui::SliderFloat("Luma/chroma crosstalk", &state.lumaChromaCrosstalk, 0.0f, 1.0f, "%.2f");
        description("Lets the encoded color carrier leak into luminance as alternating artifact color.");
      }
      if (signal >= static_cast<int>(DisplaySignal::LmsReceptorTriplet)) {
        ImGui::SeparatorText("RGB OBSERVER APPROXIMATION");
        ImGui::SliderFloat("Receptor exposure", &state.observerExposureStops, -6.0f, 6.0f, "%+.1f stops");
        ImGui::SliderFloat("Dark adaptation / rod fraction", &state.darkAdaptation, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Rod sensitivity", &state.rodSensitivity, 0.25f, 16.0f, "%.2fx",
          ImGuiSliderFlags_Logarithmic);
        ImGui::SliderFloat("Opponent gain", &state.opponentGain, 0.25f, 16.0f, "%.2fx",
          ImGuiSliderFlags_Logarithmic);
        ImGui::SliderInt("Receptor XOR bit depth", &state.receptorXorBits, 1, 8, "%d bits");
        description("Converts linear sRGB into approximate L, M, S cone and rod responses. This is a perceptual RGB experiment, not spectral rendering: metameric spectra remain indistinguishable.");
        if (state.signal == DisplaySignal::LmsReceptorTriplet)
          description("False color: red stores L response, green stores M response, and blue stores S response.");
        else if (state.signal == DisplaySignal::RedGreenOpponent)
          description("Black means L/M balance. Red means positive L-M; green means negative L-M.");
        else if (state.signal == DisplaySignal::BlueYellowOpponent)
          description("Black means balance. Blue means positive S opposition; yellow means negative S opposition.");
        else if (state.signal == DisplaySignal::RodConeXor)
          description("Quantizes the cone reconstruction and blue-tinted rod reconstruction, then XORs each RGB channel. This is expressive bit arithmetic, not retinal physiology.");
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
