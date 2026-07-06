#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Plugins/PluginManager.h"
#include "Plugins/PluginRegistry.h"

#include "imgui/imgui.h"

namespace Plugins {

// ───────────────────────────── Static registry ─────────────────────────────

std::vector<PluginFactory>& pluginFactoryRegistry() {
    // Function-local static: constructed on first use, so it is safe to append
    // to from other translation units' static initializers (REGISTER_PLUGIN).
    static std::vector<PluginFactory> registry;
    return registry;
}

PluginRegistrar::PluginRegistrar(PluginFactory factory) {
    pluginFactoryRegistry().push_back(std::move(factory));
}

// ──────────────────────────── Plugin defaults ──────────────────────────────

void Plugin::onRenderMenu(PluginContext& /*ctx*/) {
    // Default Tools-menu entry: a checkbox toggling this plugin's window.
    const PluginInfo meta = info();
    const char* shortcut = meta.shortcut.empty() ? nullptr : meta.shortcut.c_str();
    ImGui::MenuItem(meta.name.c_str(), shortcut, &m_windowOpen);
    if (!meta.description.empty() && ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", meta.description.c_str());
}

// ──────────────────────────── PluginManager ────────────────────────────────

void PluginManager::loadRegisteredPlugins(PluginContext& ctx) {
    for (PluginFactory& factory : pluginFactoryRegistry()) {
        std::unique_ptr<Plugin> plugin = factory();
        if (!plugin) continue;
        plugin->onRegister(ctx);
        m_all.push_back(plugin.get());
        m_owned.push_back(std::move(plugin));
    }
}

void PluginManager::registerExternal(Plugin* plugin, PluginContext& ctx) {
    if (!plugin) return;
    plugin->onRegister(ctx);
    m_all.push_back(plugin);
}

void PluginManager::update(PluginContext& ctx, float dt) {
    for (Plugin* p : m_all) p->onUpdate(ctx, dt);
}

void PluginManager::buildOverlay(PluginContext& ctx) {
    for (Plugin* p : m_all) p->onBuildOverlay(ctx);
}

void PluginManager::renderUI(PluginContext& ctx) {
    for (Plugin* p : m_all) p->onRenderUI(ctx);
}

void PluginManager::renderMenu(PluginContext& ctx) {
    for (Plugin* p : m_all) p->onRenderMenu(ctx);
}

bool PluginManager::dispatchMouseButton(PluginContext& ctx, int button,
                                        int action, int mods) {
    for (Plugin* p : m_all)
        if (p->onMouseButton(ctx, button, action, mods)) return true;
    return false;
}

bool PluginManager::dispatchScroll(PluginContext& ctx, double xoffset, double yoffset) {
    for (Plugin* p : m_all)
        if (p->onScroll(ctx, xoffset, yoffset)) return true;
    return false;
}

bool PluginManager::dispatchKey(PluginContext& ctx, int key, int scancode,
                                int action, int mods) {
    for (Plugin* p : m_all)
        if (p->onKey(ctx, key, scancode, action, mods)) return true;
    return false;
}

Plugin* PluginManager::find(const std::string& id) {
    for (Plugin* p : m_all)
        if (p->info().id == id) return p;
    return nullptr;
}

} // namespace Plugins
