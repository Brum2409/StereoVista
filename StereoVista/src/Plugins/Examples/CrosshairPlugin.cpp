#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Plugins/Examples/CrosshairPlugin.h"
#include "Plugins/PluginRegistry.h"

#include "imgui/imgui.h"
#include <glm/gtc/type_ptr.hpp>

namespace Plugins {

// ── Embedded overlay shaders ────────────────────────────────────────────────
// Self-contained so the plugin has no asset-file dependency. Position-only
// geometry; a flat colour is supplied through a uniform.
static const char* kVertexSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() { gl_Position = uViewProj * vec4(aPos, 1.0); }
)";

static const char* kFragmentSrc = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
void main() { FragColor = vec4(uColor, 1.0); }
)";

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

// ── GL lifecycle ────────────────────────────────────────────────────────────
void CrosshairPlugin::onInitializeGL(PluginContext& ctx) {
    // The host's helper compiles + links the embedded program for us.
    m_program  = ctx.compileOverlayProgram(kVertexSrc, kFragmentSrc, "crosshair");
    m_uViewProj = glGetUniformLocation(m_program, "uViewProj");
    m_uColor    = glGetUniformLocation(m_program, "uColor");

    glGenVertexArrays(1, &m_vao);
    glGenBuffers(1, &m_vbo);
    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);
}

void CrosshairPlugin::onShutdownGL() {
    if (m_vbo)     { glDeleteBuffers(1, &m_vbo);            m_vbo = 0; }
    if (m_vao)     { glDeleteVertexArrays(1, &m_vao);       m_vao = 0; }
    if (m_program) { glDeleteProgram(m_program);            m_program = 0; }
}

// ── Geometry helper ─────────────────────────────────────────────────────────
void CrosshairPlugin::appendCrosshair(std::vector<float>& v, const glm::vec3& p,
                                      float s) const {
    const float pts[] = {
        p.x - s, p.y, p.z,  p.x + s, p.y, p.z, // X arm
        p.x, p.y - s, p.z,  p.x, p.y + s, p.z, // Y arm
        p.x, p.y, p.z - s,  p.x, p.y, p.z + s, // Z arm
    };
    v.insert(v.end(), pts, pts + (sizeof(pts) / sizeof(pts[0])));
}

// ── World-space overlay (called once per eye) ───────────────────────────────
void CrosshairPlugin::onRenderViewport(PluginContext& ctx, const glm::mat4& projection,
                                       const glm::mat4& view, const glm::vec3& /*cameraPos*/) {
    if (m_program == 0) return;

    std::vector<float> verts;
    for (const glm::vec3& m : m_markers) appendCrosshair(verts, m, m_size);

    // A crosshair tracking the live 3D cursor while the tool is enabled.
    glm::vec3 cursor;
    if (m_enabled && m_showCursor && ctx.cursorWorldPos(cursor))
        appendCrosshair(verts, cursor, m_size * 1.4f);

    if (verts.empty()) return;

    // Overlay must not write depth (the cursor system samples scene depth).
    GLboolean prevDepthMask = GL_TRUE;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
    glDepthMask(GL_FALSE);

    const glm::mat4 viewProj = projection * view;
    glUseProgram(m_program);
    glUniformMatrix4fv(m_uViewProj, 1, GL_FALSE, glm::value_ptr(viewProj));
    glUniform3fv(m_uColor, 1, glm::value_ptr(m_color));

    glBindVertexArray(m_vao);
    glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
    glBufferData(GL_ARRAY_BUFFER,
                 static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                 verts.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 3));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glUseProgram(0);
    glDepthMask(prevDepthMask);
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
