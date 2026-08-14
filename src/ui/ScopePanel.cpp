#include "ui/ScopePanel.hpp"

#include "app/Animation.hpp"
#include "ui/Windowing.hpp"

#include <imgui.h>

#include <algorithm>
#include <string>

namespace gfxlab::ui {
namespace {

const char* encodingLabel(const document::SignalEncoding encoding) {
  switch (encoding) {
    case document::SignalEncoding::Unspecified: return "unspecified";
    case document::SignalEncoding::Linear: return "linear";
    case document::SignalEncoding::EncodedRgb: return "encoded RGB";
    case document::SignalEncoding::Signed: return "signed";
    case document::SignalEncoding::UnsignedNormalized: return "normalized";
  }
  return "unknown";
}

const char* severityLabel(const evaluation::DiagnosticSeverity severity) {
  switch (severity) {
    case evaluation::DiagnosticSeverity::Information: return "INFO";
    case evaluation::DiagnosticSeverity::Warning: return "WARNING";
    case evaluation::DiagnosticSeverity::Error: return "ERROR";
  }
  return "ERROR";
}

bool isPinned(const editor::EditorState& state, const document::SignalRef signal) {
  return std::find(state.pinnedSignals.begin(), state.pinnedSignals.end(), signal) !=
    state.pinnedSignals.end();
}

void togglePinned(editor::EditorState& state, const document::SignalRef signal) {
  const auto found = std::find(state.pinnedSignals.begin(), state.pinnedSignals.end(), signal);
  if (found == state.pinnedSignals.end()) state.pinnedSignals.push_back(signal);
  else state.pinnedSignals.erase(found);
}

} // namespace

void drawScopePanel(bool& open, const document::Document& document,
    const evaluation::EvaluationPlan& plan, const evaluation::SignalRegistry& signals,
    editor::EditorState& editorState) {
  if (!open) return;
  if (!ImGui::Begin("Scope", &open)) {
    ImGui::End();
    return;
  }
  keepCurrentWindowVisible();

  struct ToolTab { const char* label; editor::ScopeTool tool; };
  constexpr ToolTab tabs[] = {{"Signals", editor::ScopeTool::Signal},
    {"Measurements", editor::ScopeTool::Measurements},
    {"Automation", editor::ScopeTool::Automation},
    {"Differences", editor::ScopeTool::Differences}};
  for (const ToolTab& tab : tabs) {
    if (tab.tool != editor::ScopeTool::Signal) ImGui::SameLine();
    if (ImGui::RadioButton(tab.label, editorState.scope == tab.tool)) editorState.scope = tab.tool;
  }
  ImGui::Separator();

  if (editorState.scope == editor::ScopeTool::Signal) {
    ImGui::TextDisabled("SIGNAL REGISTRY");
    if (ImGui::BeginTable("signals", 6,
        ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersInnerH | ImGuiTableFlags_Resizable)) {
      ImGui::TableSetupColumn("Producer");
      ImGui::TableSetupColumn("Signal");
      ImGui::TableSetupColumn("Kind");
      ImGui::TableSetupColumn("Domain");
      ImGui::TableSetupColumn("Encoding");
      ImGui::TableSetupColumn("Actions", ImGuiTableColumnFlags_WidthFixed, 188.0f);
      ImGui::TableHeadersRow();
      for (const document::Operation& operation : document.operations) {
        for (const document::SignalDescriptor& descriptor : operation.outputs) {
          const document::SignalRef signal{descriptor.id, 0};
          ImGui::PushID(static_cast<int>(std::hash<document::SignalId>{}(descriptor.id)));
          ImGui::TableNextRow();
          ImGui::TableNextColumn(); ImGui::TextUnformatted(operation.name.c_str());
          ImGui::TableNextColumn(); ImGui::TextUnformatted(descriptor.name.c_str());
          ImGui::TableNextColumn(); ImGui::TextUnformatted(document::signalKindLabel(descriptor.kind));
          ImGui::TableNextColumn(); ImGui::TextUnformatted(document::signalDomainLabel(descriptor.metadata.domain));
          ImGui::TableNextColumn(); ImGui::TextUnformatted(encodingLabel(descriptor.metadata.encoding));
          ImGui::TableNextColumn();
          if (ImGui::SmallButton("View")) editorState.viewer.viewed = signal;
          ImGui::SameLine();
          if (ImGui::SmallButton("Compare")) editorState.viewer.comparison = signal;
          ImGui::SameLine();
          if (ImGui::SmallButton(isPinned(editorState, signal) ? "Unpin" : "Pin"))
            togglePinned(editorState, signal);
          ImGui::PopID();
        }
      }
      ImGui::EndTable();
    }
    for (const evaluation::OperationDiagnostic& diagnostic : plan.diagnostics) {
      const ImVec4 color = diagnostic.severity == evaluation::DiagnosticSeverity::Error
        ? ImVec4(0.95f, 0.36f, 0.30f, 1.0f) : ImVec4(0.95f, 0.70f, 0.30f, 1.0f);
      ImGui::TextColored(color, "%s  %s", severityLabel(diagnostic.severity), diagnostic.message.c_str());
    }
  } else if (editorState.scope == editor::ScopeTool::Measurements) {
    ImGui::TextDisabled("SCALAR OUTPUTS");
    bool any = false;
    for (const auto& [id, resource] : signals.resources()) {
      if (resource.descriptor.kind != document::SignalKind::Scalar) continue;
      any = true;
      ImGui::PushID(static_cast<int>(std::hash<document::SignalId>{}(id)));
      ImGui::Text("%s", resource.descriptor.name.c_str());
      ImGui::SameLine(220.0f);
      if (resource.scalar.has_value()) ImGui::Text("%.6f", *resource.scalar);
      else ImGui::TextDisabled("pending legacy readback");
      ImGui::SameLine();
      if (ImGui::SmallButton("Use for modulation")) {
        const document::SignalRef signal{id, 0};
        if (!isPinned(editorState, signal)) editorState.pinnedSignals.push_back(signal);
        editorState.scope = editor::ScopeTool::Automation;
      }
      ImGui::PopID();
    }
    if (!any) ImGui::TextWrapped("Add a Measure operation. Its Scalar output becomes a routable signal here; the number is useful only when connected to a property modulation.");
  } else if (editorState.scope == editor::ScopeTool::Automation) {
    ImGui::TextDisabled("ANIMATION — %zu TRACKS", document.automation.animation.size());
    for (const document::AnimationTrack& track : document.automation.animation) {
      const document::PropertyDescriptor* info = document::propertyDescriptor(track.target.property);
      ImGui::BulletText("%s  ·  %zu keys", info != nullptr ? info->label.c_str() : "Missing property",
        track.keyframes.size());
    }
    ImGui::Spacing();
    ImGui::TextDisabled("MODULATION — %zu ROUTES", document.automation.modulation.size());
    for (const document::ModulationRoute& route : document.automation.modulation) {
      const document::SignalDescriptor* source = document::findSignal(document, route.source.id);
      const document::PropertyDescriptor* target = document::propertyDescriptor(route.target.property);
      ImGui::BulletText("%s -> %s   [%.3f..%.3f] to [%.3f..%.3f]",
        source != nullptr ? source->name.c_str() : "Missing signal",
        target != nullptr ? target->label.c_str() : "Missing property",
        route.inputRange.x, route.inputRange.y, route.outputRange.x, route.outputRange.y);
    }
    if (document.automation.modulation.empty())
      ImGui::TextWrapped("Pin a Scalar measurement, then connect it to a property. Modulation is evaluated after animation, so the measured signal can drive the rendered document.");
  } else {
    const document::SignalDescriptor* viewed = document::findSignal(document, editorState.viewer.viewed.id);
    const document::SignalDescriptor* comparison = editorState.viewer.comparison.has_value()
      ? document::findSignal(document, editorState.viewer.comparison->id) : nullptr;
    ImGui::TextDisabled("VIEWER COMPARISON");
    ImGui::Text("A  %s", viewed != nullptr ? viewed->name.c_str() : "No viewed signal");
    ImGui::Text("B  %s", comparison != nullptr ? comparison->name.c_str() : "No comparison signal");
    if (comparison == nullptr) {
      ImGui::TextWrapped("Choose Compare on any signal. Comparison is a viewing tool; it does not alter the authored operation graph.");
    } else {
      if (ImGui::Button("Split")) editorState.viewer.mode = editor::ViewerMode::Split;
      ImGui::SameLine();
      if (ImGui::Button("Absolute difference")) editorState.viewer.mode = editor::ViewerMode::AbsoluteDifference;
      ImGui::SameLine();
      if (ImGui::Button("Flicker")) editorState.viewer.mode = editor::ViewerMode::Flicker;
      ImGui::SameLine();
      if (ImGui::Button("Clear B")) editorState.viewer.comparison.reset();
    }
  }
  ImGui::End();
}

} // namespace gfxlab::ui
