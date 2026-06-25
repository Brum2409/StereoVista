#pragma once

// ============================================================================
//  PluginManager  —  owns plugins and drives every host-integration point
// ----------------------------------------------------------------------------
//  A single PluginManager instance lives in main.cpp. The host calls into it
//  at a handful of well-defined points (init, shutdown, per-eye render, ImGui
//  pass, Tools menu, and the GLFW input callbacks); the manager fans each call
//  out to the registered plugins.
//
//  Two kinds of plugins:
//    • Registered (owned)  — created from the static REGISTER_PLUGIN registry
//                            by loadRegisteredPlugins(); owned by the manager.
//    • External (borrowed) — an existing global tool migrated onto the Plugin
//                            interface; added with registerExternal(), still
//                            owned by whoever declared it.
//
//  Event dispatch (mouse/scroll/key) returns true when a plugin CONSUMED the
//  event, so the host can early-out and skip its built-in handling.
// ============================================================================

#include "Plugin.h"
#include "PluginContext.h"
#include <glm/glm.hpp>
#include <memory>
#include <string>
#include <vector>

namespace Plugins {

class PluginManager {
public:
    PluginManager() = default;

    // ── Population ──────────────────────────────────────────────────────────
    // Instantiate every statically-registered plugin (REGISTER_PLUGIN) and call
    // onRegister on each. Call once, after the GL context exists.
    void loadRegisteredPlugins(PluginContext& ctx);

    // Add an externally-owned plugin (e.g. a migrated global tool). The manager
    // does NOT take ownership; the pointer must outlive the manager. onRegister
    // is invoked immediately.
    void registerExternal(Plugin* plugin, PluginContext& ctx);

    // ── Lifecycle dispatch ──────────────────────────────────────────────────
    void initializeAllGL(PluginContext& ctx);   // onInitializeGL for all
    void shutdownAllGL();                        // onShutdownGL for all
    void update(PluginContext& ctx, float dt);   // onUpdate for all

    void renderViewport(PluginContext& ctx, const glm::mat4& projection,
                        const glm::mat4& view, const glm::vec3& cameraPos);
    void renderUI(PluginContext& ctx);           // onRenderUI for all
    void renderMenu(PluginContext& ctx);         // onRenderMenu for all

    // ── Input dispatch (true == consumed by some plugin) ────────────────────
    bool dispatchMouseButton(PluginContext& ctx, int button, int action, int mods);
    bool dispatchScroll(PluginContext& ctx, double xoffset, double yoffset);
    bool dispatchKey(PluginContext& ctx, int key, int scancode, int action, int mods);

    // ── Access ──────────────────────────────────────────────────────────────
    Plugin* find(const std::string& id);
    const std::vector<Plugin*>& all() const { return m_all; }

    // Typed lookup, e.g. manager.get<MeasurementPlugin>().
    template <class T> T* get() {
        for (Plugin* p : m_all)
            if (auto* typed = dynamic_cast<T*>(p)) return typed;
        return nullptr;
    }

private:
    std::vector<std::unique_ptr<Plugin>> m_owned; // registered plugins
    std::vector<Plugin*>                 m_all;    // owned + external, in order
    bool m_glInitialized = false;
};

} // namespace Plugins
