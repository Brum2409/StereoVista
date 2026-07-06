#pragma once

// ============================================================================
//  CrosshairPlugin  —  the canonical example / template plugin
// ----------------------------------------------------------------------------
//  Copy this pair of files (.h + .cpp) as the starting point for a new tool.
//  It is intentionally small but exercises the whole (Vulkan) Plugin API:
//
//    • info()           — identity + Tools-menu metadata
//    • onBuildOverlay   — append a world-space crosshair at the 3D cursor and
//                         at every dropped marker to ctx.overlay()
//    • onRenderUI       — an ImGui settings window (color, size, list)
//    • onMouseButton    — left-click drops a marker and raises a toast
//    • the default menu / windowOpen() flow — toggled from the Tools menu
//
//  Everything the tool needs arrives through PluginContext, and it registers
//  itself via REGISTER_PLUGIN in the .cpp — no host edits required. Note there
//  is no GL lifecycle and no shader/VAO ownership anymore: geometry is just
//  described into the shared overlay list.
// ============================================================================

#include "Plugins/Plugin.h"
#include <glm/glm.hpp>
#include <vector>

namespace renderer { class OverlayDrawList; }

namespace Plugins {

class CrosshairPlugin : public Plugin {
public:
    PluginInfo info() const override;

    void onBuildOverlay(PluginContext& ctx) override;
    void onRenderUI(PluginContext& ctx) override;
    bool onMouseButton(PluginContext& ctx, int button, int action, int mods) override;

private:
    // Append a 3-axis world-space crosshair centred at p to the overlay list.
    void appendCrosshair(renderer::OverlayDrawList& list, const glm::vec3& p,
                         float size) const;

    // ── Settings (exposed in the ImGui window) ──
    glm::vec3 m_color      = glm::vec3(0.10f, 0.90f, 0.40f);
    float     m_size       = 0.25f;   // half-length of each crosshair arm (world)
    bool      m_showCursor = true;    // draw a crosshair at the live 3D cursor

    // ── Dropped markers (the tool's own data) ──
    std::vector<glm::vec3> m_markers;
};

} // namespace Plugins
