#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Plugins/MeasurementPlugin.h"
#include "Plugins/PluginRegistry.h"

#include "imgui/imgui.h"
#include <GLFW/glfw3.h>
#include <portable-file-dialogs.h>

#include <cstdio>
#include <string>

namespace Plugins {

PluginInfo MeasurementPlugin::info() const {
    PluginInfo meta;
    meta.id          = "stereovista.measurement";
    meta.name        = "Measure";
    meta.description = "Measure distances, angles, areas and coordinates in the scene.";
    meta.version     = "1.0.0";
    meta.category    = PluginCategory::Tool;
    meta.shortcut    = "M";
    return meta;
}

// ── World-space overlay (once per frame) ────────────────────────────────────
void MeasurementPlugin::onBuildOverlay(PluginContext& ctx) {
    // Committed measurements always draw; the live rubber-band preview only while
    // the tool is enabled and the 3D cursor is over geometry.
    glm::vec3 cursor;
    const glm::vec3* preview =
        (m_tool.isEnabled() && ctx.cursorWorldPos(cursor)) ? &cursor : nullptr;
    m_tool.appendTo(ctx.overlay(), preview);
}

// ── Tools-menu entry ────────────────────────────────────────────────────────
void MeasurementPlugin::onRenderMenu(PluginContext& /*ctx*/) {
    const PluginInfo meta = info();
    ImGui::MenuItem(meta.name.c_str(), meta.shortcut.c_str(), &m_windowOpen);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", meta.description.c_str());
}

// ── ImGui: value labels (always) + settings window ──────────────────────────
void MeasurementPlugin::onRenderUI(PluginContext& ctx) {
    // Value labels over the 3D view are independent of the settings window.
    const ImGuiViewport* vp = ImGui::GetMainViewport();
    m_tool.drawLabels(ctx.viewProj(), glm::vec2(vp->Pos.x, vp->Pos.y),
                      glm::vec2(vp->Size.x, vp->Size.y));

    if (!m_windowOpen) return;
    if (ImGui::Begin("Measure", &m_windowOpen)) {
        bool enabled = m_tool.isEnabled();
        if (ImGui::Checkbox("Enabled (LMB places points)", &enabled))
            m_tool.setEnabled(enabled);

        int mode = static_cast<int>(m_tool.getMode());
        // Order matches Engine::Measurement::Type (Distance,Angle,Point,Area).
        const char* modes[] = { "Distance", "Angle", "Point", "Area" };
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("Mode", &mode, modes, 4))
            m_tool.setMode(static_cast<Engine::Measurement::Type>(mode));
        ImGui::TextDisabled(
            "LMB add \xC2\xB7 RMB/Enter finish \xC2\xB7 Backspace undo \xC2\xB7 Del cancel");

        ImGui::Checkbox("Labels", &m_tool.showLabels);
        ImGui::SameLine();
        ImGui::Checkbox("Segment", &m_tool.showSegmentLabels);
        ImGui::SameLine();
        ImGui::Checkbox("X-ray", &m_tool.xRay);
        ImGui::SetNextItemWidth(120.0f);
        ImGui::DragFloat("Unit scale", &m_tool.unitScale, 0.01f, 0.001f, 1000.0f,
                         "%.3f");

        ImGui::Separator();
        ImGui::Text("Measurements: %zu", m_tool.measurements().size());
        int deleteIdx = -1;
        for (size_t i = 0; i < m_tool.measurements().size(); ++i) {
            const Engine::Measurement& m = m_tool.measurements()[i];
            ImGui::PushID(static_cast<int>(i));
            std::string summary = m.name;
            if (m.type == Engine::Measurement::Type::Distance)
                summary += " \xE2\x80\x94 " + m_tool.formatLength(m.totalLength());
            else if (m.type == Engine::Measurement::Type::Area)
                summary += " \xE2\x80\x94 " + m_tool.formatArea(m.area());
            else if (m.type == Engine::Measurement::Type::Angle) {
                char b[24];
                std::snprintf(b, sizeof(b), " \xE2\x80\x94 %.1f\xC2\xB0", m.angleDegrees());
                summary += b;
            }
            ImGui::TextUnformatted(summary.c_str());
            ImGui::SameLine();
            if (ImGui::SmallButton("X")) deleteIdx = static_cast<int>(i);
            ImGui::PopID();
        }
        if (deleteIdx >= 0) m_tool.deleteMeasurement(deleteIdx);

        if (ImGui::Button("Clear all")) m_tool.clearAll();
        ImGui::SameLine();
        if (ImGui::Button("Export CSV...")) {
            const std::string path =
                pfd::save_file("Export measurements", "measurements.csv",
                               { "CSV files", "*.csv", "All files", "*" })
                    .result();
            if (!path.empty())
                m_tool.exportToCSV(path);
        }
    }
    ImGui::End();
}

// ── Input ───────────────────────────────────────────────────────────────────
bool MeasurementPlugin::onMouseButton(PluginContext& ctx, int button, int action,
                                      int mods) {
    if (action != GLFW_PRESS) return false;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) return false; // leave nav/select
        if (!m_tool.isEnabled()) return false;
        glm::vec3 cursor;
        if (ctx.cursorWorldPos(cursor))
            m_tool.addPoint(cursor);
        return true; // consume: no orbit / selection while measuring
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        // Right-click finishes the in-progress polyline instead of free-looking.
        if (m_tool.isEnabled() && m_tool.hasActive()) {
            m_tool.finishActive();
            return true;
        }
    }
    return false;
}

bool MeasurementPlugin::onKey(PluginContext& /*ctx*/, int key, int /*scancode*/,
                              int action, int /*mods*/) {
    if (action != GLFW_PRESS) return false;
    if (!(m_tool.isEnabled() && m_tool.hasActive())) return false;

    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        m_tool.finishActive();
        return true;
    }
    if (key == GLFW_KEY_DELETE) {
        m_tool.cancelActive();
        return true;
    }
    if (key == GLFW_KEY_BACKSPACE) {
        m_tool.undoLastPoint();
        return true;
    }
    return false;
}

} // namespace Plugins

// Self-register so the PluginManager instantiates the adapter at startup.
REGISTER_PLUGIN(Plugins::MeasurementPlugin);
