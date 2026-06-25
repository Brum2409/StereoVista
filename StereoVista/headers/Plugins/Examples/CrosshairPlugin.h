#pragma once

// ============================================================================
//  CrosshairPlugin  —  the canonical example / template plugin
// ----------------------------------------------------------------------------
//  Copy this pair of files (.h + .cpp) as the starting point for a new tool.
//  It is intentionally small but exercises the whole Plugin API surface:
//
//    • info()             — identity + Tools-menu metadata
//    • onInitializeGL /
//      onShutdownGL       — create / destroy a tiny overlay shader + buffers
//    • onRenderViewport   — draw a world-space crosshair at the 3D cursor and
//                           at every dropped marker (called once per eye)
//    • onRenderUI         — an ImGui settings window (color, size, list)
//    • onMouseButton      — left-click drops a marker and raises a toast
//    • the default menu /
//      windowOpen() flow  — toggled from the Tools menu
//
//  Everything the tool needs from the host arrives through PluginContext, and
//  it registers itself via REGISTER_PLUGIN in the .cpp — no edits to main.cpp
//  or GUI.cpp are required to add it.
// ============================================================================

#include "Plugins/Plugin.h"
#include <glm/glm.hpp>
#include <vector>

namespace Plugins {

class CrosshairPlugin : public Plugin {
public:
    PluginInfo info() const override;

    void onInitializeGL(PluginContext& ctx) override;
    void onShutdownGL() override;

    void onRenderViewport(PluginContext& ctx, const glm::mat4& projection,
                          const glm::mat4& view, const glm::vec3& cameraPos) override;
    void onRenderUI(PluginContext& ctx) override;

    bool onMouseButton(PluginContext& ctx, int button, int action, int mods) override;

private:
    // Append a 3-axis crosshair (6 vertices) centred at p to the line buffer.
    void appendCrosshair(std::vector<float>& verts, const glm::vec3& p,
                         float size) const;

    // ── Settings (exposed in the ImGui window) ──
    glm::vec3 m_color      = glm::vec3(0.10f, 0.90f, 0.40f);
    float     m_size       = 0.25f;   // half-length of each crosshair arm (world)
    bool      m_showCursor = true;    // draw a crosshair at the live 3D cursor

    // ── Dropped markers (the tool's own data) ──
    std::vector<glm::vec3> m_markers;

    // ── GL resources (created in onInitializeGL) ──
    unsigned int m_program = 0;
    unsigned int m_vao     = 0;
    unsigned int m_vbo     = 0;
    int          m_uViewProj = -1;
    int          m_uColor    = -1;
};

} // namespace Plugins
