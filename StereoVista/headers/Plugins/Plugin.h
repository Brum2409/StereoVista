#pragma once

// ============================================================================
//  Plugin  —  base class for every StereoVista tool/plugin
// ----------------------------------------------------------------------------
//  A plugin is a self-contained tool compiled into the application. It opts in
//  to whichever host hooks it needs by overriding the corresponding virtual
//  method below; every hook has a no-op default, so a minimal plugin overrides
//  only info() plus the one or two hooks it cares about.
//
//  All per-frame and event hooks receive a Plugins::PluginContext& giving safe
//  access to the scene, overlay, picking, selection, undo and toasts.
//
//  Vulkan rewrite: the GL lifecycle (onInitializeGL/onShutdownGL) is gone —
//  the overlay is immediate-mode, so plugins own no GPU objects — and the
//  per-eye onRenderViewport is replaced by a single onBuildOverlay that appends
//  world-space geometry to ctx.overlay() once per frame.
//
//  Registration is automatic: drop REGISTER_PLUGIN(MyPlugin) in the plugin's
//  .cpp (see PluginRegistry.h); the PluginManager instantiates it at startup.
// ============================================================================

#include "PluginContext.h"
#include <glm/glm.hpp>
#include <string>

namespace Plugins {

// Loose grouping used for menu organisation / filtering. Purely cosmetic.
enum class PluginCategory {
    Tool,           // interactive editing tool (default)
    Visualization,  // overlays / debug draws
    Import,         // data ingestion
    Export,         // data output
    Utility,        // helpers, generators
    Experimental
};

// Static metadata describing a plugin. Returned by Plugin::info().
struct PluginInfo {
    std::string    id;                 // stable unique id, e.g. "stereovista.crosshair"
    std::string    name;               // menu / window label, e.g. "Crosshair"
    std::string    description;        // one-line summary (tooltip)
    std::string    version  = "1.0.0";
    PluginCategory category = PluginCategory::Tool;
    std::string    shortcut;           // optional display hint, e.g. "Ctrl+H"
};

class Plugin {
public:
    virtual ~Plugin() = default;

    // Identity / metadata. The only mandatory override.
    virtual PluginInfo info() const = 0;

    // ── Lifecycle ───────────────────────────────────────────────────────────
    // onRegister: called once when the plugin is added to the manager. Good for
    // wiring scene data. (No GL lifecycle — the overlay owns no per-plugin GPU
    // objects.)
    virtual void onRegister(PluginContext&) {}

    // ── Enabled (active) state ──────────────────────────────────────────────
    // "Enabled" means the tool is actively capturing interaction (e.g. clicks
    // place points). onEnable/onDisable fire only on an actual transition.
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled) {
        if (enabled == m_enabled) return;
        m_enabled = enabled;
        if (enabled) onEnable(); else onDisable();
    }
    virtual void onEnable()  {}
    virtual void onDisable() {}

    // ── Per-frame ───────────────────────────────────────────────────────────
    // Logic update, once per frame before rendering. dt is seconds.
    virtual void onUpdate(PluginContext&, float /*dt*/) {}

    // World-space overlay. Called once per frame; append geometry to
    // ctx.overlay(). Purely descriptive — no GPU state, no matrices; the
    // renderer transforms and draws it for every eye.
    virtual void onBuildOverlay(PluginContext&) {}

    // ImGui pass. Called once per frame (inside the host's ImGui frame). Draw
    // your own windows here; gate them on windowOpen() if you use the default
    // menu entry below.
    virtual void onRenderUI(PluginContext&) {}

    // Inspector Tool card (UI redesign Pass 7 §13). Called ONLY while this
    // plugin is enabled (it is the active tool), inside the Inspector window at
    // the top. Draw the tool's options here — no Begin/End, just widgets — and
    // they appear where the user is already looking instead of in a floating
    // window of their own. Optional: a plugin with no options ignores it.
    virtual void onRenderInspector(PluginContext&) {}

    // Tools-menu entry. Default: a checkbox menu item toggling windowOpen().
    // Override for a custom submenu.
    virtual void onRenderMenu(PluginContext&);

    // ── Input hooks (return true to CONSUME the event) ──────────────────────
    // The manager queries plugins before the host's built-in handling; the
    // first plugin to return true stops propagation. Arguments mirror GLFW.
    virtual bool onMouseButton(PluginContext&, int /*button*/, int /*action*/,
                               int /*mods*/)                       { return false; }
    virtual bool onScroll(PluginContext&, double /*xoffset*/,
                          double /*yoffset*/)                      { return false; }
    virtual bool onKey(PluginContext&, int /*key*/, int /*scancode*/,
                       int /*action*/, int /*mods*/)               { return false; }

    // Window-visibility flag the default menu entry toggles and onRenderUI can
    // read. Plugins without a window can ignore it.
    bool&       windowOpen()       { return m_windowOpen; }
    const bool& windowOpen() const { return m_windowOpen; }

protected:
    bool m_enabled    = false;
    bool m_windowOpen = false;
};

} // namespace Plugins
