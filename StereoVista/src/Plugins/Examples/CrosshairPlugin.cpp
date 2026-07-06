#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Plugins/Examples/CrosshairPlugin.h"
#include "Plugins/PluginRegistry.h"

#include "Renderer/OverlayDrawList.h"

#include "imgui/imgui.h"
#include <GLFW/glfw3.h>
#include <glm/gtc/type_ptr.hpp>

namespace Plugins {

// ── Identity ────────────────────────────────────────────────────────────────
PluginInfo CrosshairPlugin::info() const {
    PluginInfo meta;
    meta.id          = "stereovista.crosshair";
    meta.name        = "Crosshair (Example)";
    meta.description = "Example plugin: drops world-space crosshair markers.";
    meta.version     = "1.0.0";
    meta.category    = PluginCategory::Visualization;
    return meta;
}

// ── Geometry helper ─────────────────────────────────────────────────────────
void CrosshairPlugin::appendCrosshair(renderer::OverlayDrawList& list,
                                      const glm::vec3& p, float s) const {
    const glm::vec4 color(m_color, 1.0f);
    list.line(p - glm::vec3(s, 0, 0), p + glm::vec3(s, 0, 0), color, 2.0f);
    list.line(p - glm::vec3(0, s, 0), p + glm::vec3(0, s, 0), color, 2.0f);
    list.line(p - glm::vec3(0, 0, s), p + glm::vec3(0, 0, s), color, 2.0f);
}

// ── World-space overlay (once per frame; both eyes reuse it) ─────────────────
void CrosshairPlugin::onBuildOverlay(PluginContext& ctx) {
    renderer::OverlayDrawList& list = ctx.overlay();
    for (const glm::vec3& m : m_markers)
        appendCrosshair(list, m, m_size);

    // A crosshair tracking the live 3D cursor while the tool is enabled.
    glm::vec3 cursor;
    if (m_enabled && m_showCursor && ctx.cursorWorldPos(cursor))
        appendCrosshair(list, cursor, m_size * 1.4f);
}

// ── ImGui settings window ───────────────────────────────────────────────────
void CrosshairPlugin::onRenderUI(PluginContext& ctx) {
    if (!m_windowOpen) return; // toggled from the Tools menu (default entry)

    if (ImGui::Begin(info().name.c_str(), &m_windowOpen)) {
        bool enabled = m_enabled;
        if (ImGui::Checkbox("Enabled (click to drop markers)", &enabled))
            setEnabled(enabled);

        ImGui::ColorEdit3("Color", glm::value_ptr(m_color));
        ImGui::SliderFloat("Size", &m_size, 0.02f, 2.0f, "%.2f");
        ImGui::Checkbox("Track 3D cursor", &m_showCursor);

        ImGui::Separator();
        ImGui::Text("Markers: %d", static_cast<int>(m_markers.size()));
        if (ImGui::Button("Clear markers")) {
            m_markers.clear();
            ctx.toast("Crosshair markers cleared", ToastLevel::Info);
        }
    }
    ImGui::End();
}

// ── Input: left-click drops a marker at the 3D cursor ───────────────────────
bool CrosshairPlugin::onMouseButton(PluginContext& ctx, int button, int action,
                                    int mods) {
    if (!m_enabled) return false;                 // only when actively editing
    if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS) return false;
    if (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) return false; // leave nav/select

    glm::vec3 cursor;
    if (!ctx.cursorWorldPos(cursor)) return false; // nothing under the cursor

    m_markers.push_back(cursor);
    ctx.toast("Marker dropped", ToastLevel::Success);
    return true; // consume: don't also orbit / select
}

} // namespace Plugins

// Self-register so the PluginManager instantiates this at startup.
REGISTER_PLUGIN(Plugins::CrosshairPlugin);
