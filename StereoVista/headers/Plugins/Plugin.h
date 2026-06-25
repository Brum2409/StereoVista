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
//  access to the scene, picking, preferences, undo, toasts and a GL helper.
//
//  Registration is automatic: drop REGISTER_PLUGIN(MyPlugin) in the plugin's
//  .cpp (see PluginRegistry.h) and the PluginManager will instantiate it at
//  startup — no edits to main.cpp or GUI.cpp required.
//
//  See docs/PLUGINS.md for the lifecycle diagram and a step-by-step guide.
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
                                       // (binding itself stays external)
};

class Plugin {
public:
    virtual ~Plugin() = default;

    // Identity / metadata. The only mandatory override.
    virtual PluginInfo info() const = 0;

    // ── Lifecycle ───────────────────────────────────────────────────────────
    // onRegister     : called once when the plugin is added to the manager,
    //                  before any GL context work. Good for wiring scene data.
    // onInitializeGL : called once with a valid GL context; create VAOs/shaders.
    // onShutdownGL   : called once at exit (GL context still valid); free them.
    virtual void onRegister(PluginContext&)     {}
    virtual void onInitializeGL(PluginContext&) {}
    virtual void onShutdownGL()                 {}

    // ── Enabled (active) state ──────────────────────────────────────────────
    // "Enabled" means the tool is actively capturing interaction (e.g. clicks
    // place points). Toggled by the host or the plugin's own UI. onEnable /
    // onDisable fire only on an actual transition.
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

    // World-space overlay. Called once PER EYE with that eye's matrices, during
    // the scene render. Draw 3D gizmos / annotations here. Never assume this is
    // the only eye — keep it stateless w.r.t. the matrices passed in.
    virtual void onRenderViewport(PluginContext&, const glm::mat4& /*projection*/,
                                  const glm::mat4& /*view*/,
                                  const glm::vec3& /*cameraPos*/) {}

    // ImGui pass. Called once per frame (left-eye GUI build). Draw your own
    // ImGui windows here; gate them on windowOpen() if you use the default
    // menu entry below.
    virtual void onRenderUI(PluginContext&) {}

    // Tools-menu entry. Default implementation adds a checkbox menu item that
    // toggles windowOpen(). Override for a custom submenu.
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
    // read. Plugins without a window can simply ignore it.
    bool&       windowOpen()       { return m_windowOpen; }
    const bool& windowOpen() const { return m_windowOpen; }

protected:
    bool m_enabled    = false;
    bool m_windowOpen = false;
};

} // namespace Plugins
