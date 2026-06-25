#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Plugins/PluginManager.h"
#include "Plugins/PluginRegistry.h"
#include "imgui/imgui.h"

#include <iostream>

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
    if (ImGui::MenuItem(meta.name.c_str(), shortcut, &m_windowOpen)) {
        // toggled; the plugin's onRenderUI reads windowOpen()
    }
    if (!meta.description.empty() && ImGui::IsItemHovered()) {
        ImGui::SetTooltip("%s", meta.description.c_str());
    }
}

// ───────────────────────── PluginContext GL helper ─────────────────────────

GLuint PluginContext::compileOverlayProgram(const char* vertexSrc,
                                            const char* fragmentSrc,
                                            const char* label) {
    auto compile = [&](GLenum type, const char* src, const char* stage) -> GLuint {
        GLuint shader = glCreateShader(type);
        glShaderSource(shader, 1, &src, nullptr);
        glCompileShader(shader);
        GLint ok = GL_FALSE;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
            std::cerr << "[Plugin:" << (label ? label : "?") << "] " << stage
                      << " shader compile error: " << log << std::endl;
        }
        return shader;
    };

    GLuint vs = compile(GL_VERTEX_SHADER, vertexSrc, "vertex");
    GLuint fs = compile(GL_FRAGMENT_SHADER, fragmentSrc, "fragment");

    GLuint program = glCreateProgram();
    glAttachShader(program, vs);
    glAttachShader(program, fs);
    glLinkProgram(program);
    glDeleteShader(vs);
    glDeleteShader(fs);

    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[Plugin:" << (label ? label : "?") << "] program link error: "
                  << log << std::endl;
        glDeleteProgram(program);
        return 0;
    }
    return program;
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
    // Already-initialised manager: bring the latecomer up to GL state too.
    if (m_glInitialized) plugin->onInitializeGL(ctx);
}

void PluginManager::initializeAllGL(PluginContext& ctx) {
    for (Plugin* p : m_all) p->onInitializeGL(ctx);
    m_glInitialized = true;
}

void PluginManager::shutdownAllGL() {
    for (Plugin* p : m_all) p->onShutdownGL();
    m_glInitialized = false;
}

void PluginManager::update(PluginContext& ctx, float dt) {
    for (Plugin* p : m_all) p->onUpdate(ctx, dt);
}

void PluginManager::renderViewport(PluginContext& ctx, const glm::mat4& projection,
                                   const glm::mat4& view, const glm::vec3& cameraPos) {
    for (Plugin* p : m_all) p->onRenderViewport(ctx, projection, view, cameraPos);
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
