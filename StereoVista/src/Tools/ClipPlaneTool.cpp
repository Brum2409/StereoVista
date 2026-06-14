#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "Tools/ClipPlaneTool.h"
#include "Core/UndoManager.h"

#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/euler_angles.hpp> // eulerAngleXYZ, extractEulerAngleXYZ

#include <algorithm>
#include <cmath>
#include <iostream>
#include <memory>

namespace Tools {

    // ── Embedded overlay shader ──────────────────────────────────────────────
    // Flat-colour pass: positions only, colour (incl. alpha) via uniform. Same
    // self-contained approach as MeasurementTool / BVHDebugRenderer.
    static const char* kVertexSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uViewProj;
void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

    static const char* kFragmentSrc = R"(
#version 330 core
out vec4 FragColor;
uniform vec4 uColor;
void main() {
    FragColor = uColor;
}
)";

    static GLuint compileProgram(const char* vsSrc, const char* fsSrc) {
        auto compile = [&](GLenum type, const char* src) -> GLuint {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);
            GLint ok = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024];
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::cerr << "[ClipPlaneTool] shader compile error: " << log << std::endl;
            }
            return shader;
        };

        GLuint vs = compile(GL_VERTEX_SHADER, vsSrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, fsSrc);
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
            std::cerr << "[ClipPlaneTool] program link error: " << log << std::endl;
        }
        return program;
    }

    ClipPlaneTool::ClipPlaneTool() = default;

    ClipPlaneTool::~ClipPlaneTool() {
        cleanup();
    }

    void ClipPlaneTool::initialize() {
        if (m_initialized) return;

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        m_program = compileProgram(kVertexSrc, kFragmentSrc);
        m_initialized = true;
    }

    void ClipPlaneTool::cleanup() {
        if (m_vao) { glDeleteVertexArrays(1, &m_vao); m_vao = 0; }
        if (m_vbo) { glDeleteBuffers(1, &m_vbo); m_vbo = 0; }
        if (m_program) { glDeleteProgram(m_program); m_program = 0; }
        m_initialized = false;
    }

    // ── Normal <-> Euler conversion (TransformGizmo convention) ──────────────
    // The gizmo stores rotations as X-Y-Z Euler degrees and its rotation matrix
    // is mat3(glm::eulerAngleXYZ(...)). We define the plane normal as the matrix
    // +Z column, so the gizmo's rotate handles steer the normal directly.
    glm::vec3 ClipPlaneTool::eulerFromNormal(const glm::vec3& normal) {
        glm::vec3 z = normal;
        const float len = glm::length(z);
        z = (len > 1e-6f) ? z / len : glm::vec3(0.0f, 0.0f, 1.0f);
        // Build an orthonormal basis [x, y, z] with z = normal. Determinant is
        // +1, so extractEulerAngleXYZ recovers a clean rotation.
        const glm::vec3 ref = (std::fabs(z.y) < 0.99f) ? glm::vec3(0, 1, 0)
                                                       : glm::vec3(1, 0, 0);
        const glm::vec3 x = glm::normalize(glm::cross(ref, z));
        const glm::vec3 y = glm::cross(z, x);
        glm::mat4 R(1.0f);
        R[0] = glm::vec4(x, 0.0f);
        R[1] = glm::vec4(y, 0.0f);
        R[2] = glm::vec4(z, 0.0f);
        float ex, ey, ez;
        glm::extractEulerAngleXYZ(R, ex, ey, ez);
        return glm::degrees(glm::vec3(ex, ey, ez));
    }

    glm::vec3 ClipPlaneTool::normalFromEuler(const glm::vec3& eulerDeg) {
        const glm::mat4 R = glm::eulerAngleXYZ(glm::radians(eulerDeg.x),
                                               glm::radians(eulerDeg.y),
                                               glm::radians(eulerDeg.z));
        return glm::normalize(glm::vec3(R[2])); // R * (0,0,1)
    }

    // ── Selection ────────────────────────────────────────────────────────────
    bool ClipPlaneTool::validIndex(int index) const {
        return m_planes && index >= 0 && index < static_cast<int>(m_planes->size());
    }

    bool ClipPlaneTool::hasActivePlane() const { return validIndex(m_activeIndex); }

    void ClipPlaneTool::setActiveIndex(int index) {
        m_activeIndex = (index >= 0 && validIndex(index)) ? index : -1;
    }

    void ClipPlaneTool::clampActiveIndex() {
        if (!m_planes || m_planes->empty()) { m_activeIndex = -1; return; }
        if (m_activeIndex >= static_cast<int>(m_planes->size()))
            m_activeIndex = static_cast<int>(m_planes->size()) - 1;
    }

    // ── Creation / editing ─────────────────────────────────────────────────
    int ClipPlaneTool::addPlane(const glm::vec3& position, const glm::vec3& normal) {
        if (!m_planes) return -1;
        if (static_cast<int>(m_planes->size()) >= Engine::MAX_CLIP_PLANES) {
            std::cerr << "[ClipPlaneTool] clip-plane budget full ("
                      << Engine::MAX_CLIP_PLANES << ")\n";
            return -1;
        }

        Engine::ClipPlane p;
        p.position = position;
        const float len = glm::length(normal);
        p.normal = (len > 1e-6f) ? (normal / len) : glm::vec3(0.0f, 1.0f, 0.0f);
        p.enabled = true;
        p.name = "Plane " + std::to_string(++m_nameCounter);

        m_planes->push_back(p);
        const int idx = static_cast<int>(m_planes->size()) - 1;
        m_activeIndex = idx;

        std::vector<Engine::ClipPlane>* planes = m_planes;
        const Engine::ClipPlane val = p;
        Engine::UndoManager::instance().record(std::make_unique<Engine::LambdaUndoCommand>(
            "Add clip plane",
            [planes, idx]() {
                if (idx < static_cast<int>(planes->size()))
                    planes->erase(planes->begin() + idx);
            },
            [planes, idx, val]() {
                const size_t at = std::min(static_cast<size_t>(idx), planes->size());
                planes->insert(planes->begin() + at, val);
            }));
        return idx;
    }

    int ClipPlaneTool::addAxisAlignedPlane(int axis, const glm::vec3& center) {
        glm::vec3 n(0.0f);
        n[std::min(std::max(axis, 0), 2)] = 1.0f;
        return addPlane(center, n);
    }

    void ClipPlaneTool::deletePlane(int index) {
        if (!validIndex(index)) return;
        std::vector<Engine::ClipPlane>* planes = m_planes;
        const Engine::ClipPlane val = (*planes)[index];
        planes->erase(planes->begin() + index);
        clampActiveIndex();

        Engine::UndoManager::instance().record(std::make_unique<Engine::LambdaUndoCommand>(
            "Delete clip plane",
            [planes, index, val]() {
                const size_t at = std::min(static_cast<size_t>(index), planes->size());
                planes->insert(planes->begin() + at, val);
            },
            [planes, index]() {
                if (index < static_cast<int>(planes->size()))
                    planes->erase(planes->begin() + index);
            }));
    }

    void ClipPlaneTool::flipNormal(int index) {
        if (!validIndex(index)) return;
        std::vector<Engine::ClipPlane>* planes = m_planes;
        (*planes)[index].normal = -(*planes)[index].normal;

        Engine::UndoManager::instance().record(std::make_unique<Engine::LambdaUndoCommand>(
            "Flip clip plane",
            [planes, index]() {
                if (index < static_cast<int>(planes->size()))
                    (*planes)[index].normal = -(*planes)[index].normal;
            },
            [planes, index]() {
                if (index < static_cast<int>(planes->size()))
                    (*planes)[index].normal = -(*planes)[index].normal;
            }));
    }

    void ClipPlaneTool::nudgeActive(float distance) {
        if (!hasActivePlane()) return;
        Engine::ClipPlane& p = (*m_planes)[m_activeIndex];
        p.position += p.unitNormal() * distance;
    }

    // ── Shader / point-cloud integration ─────────────────────────────────────
    int ClipPlaneTool::collectEnabledPlanes(glm::vec4 out[]) const {
        int n = 0;
        if (!m_planes) return 0;
        for (const auto& p : *m_planes) {
            if (!p.enabled) continue;
            if (n >= Engine::MAX_CLIP_PLANES) break;
            out[n++] = p.packed();
        }
        return n;
    }

    int ClipPlaneTool::applyToShader(Engine::Shader* shader) const {
        glm::vec4 packed[Engine::MAX_CLIP_PLANES];
        const int n = collectEnabledPlanes(packed);
        if (!shader) return n;
        shader->use();
        shader->setInt("clipPlaneCount", n);
        for (int i = 0; i < n; ++i)
            shader->setVec4("clipPlanes[" + std::to_string(i) + "]", packed[i]);
        return n;
    }

    // ── Gizmo glue ───────────────────────────────────────────────────────────
    bool ClipPlaneTool::bindGizmo(TransformGizmo& gizmo) {
        if (!hasActivePlane()) return false;
        Engine::ClipPlane& p = (*m_planes)[m_activeIndex];
        // Refresh the Euler scratch from the current normal while not dragging so
        // the gizmo's rotation handles show the plane's true orientation.
        if (!gizmo.isDragging())
            m_gizmoEuler = eulerFromNormal(p.normal);
        gizmo.setTarget(&p.position, &m_gizmoEuler, nullptr);
        return true;
    }

    void ClipPlaneTool::syncActiveNormalFromGizmo() {
        if (!hasActivePlane()) return;
        (*m_planes)[m_activeIndex].normal = normalFromEuler(m_gizmoEuler);
    }

    void ClipPlaneTool::captureGizmoUndo() {
        if (!hasActivePlane()) { m_undoIndex = -1; return; }
        m_undoIndex = m_activeIndex;
        m_undoPos = (*m_planes)[m_activeIndex].position;
        m_undoNormal = (*m_planes)[m_activeIndex].normal;
    }

    void ClipPlaneTool::recordGizmoUndo() {
        if (m_undoIndex < 0 || !validIndex(m_undoIndex)) { m_undoIndex = -1; return; }
        std::vector<Engine::ClipPlane>* planes = m_planes;
        const int idx = m_undoIndex;
        const glm::vec3 beforePos = m_undoPos, beforeN = m_undoNormal;
        const glm::vec3 afterPos = (*planes)[idx].position;
        const glm::vec3 afterN = (*planes)[idx].normal;
        m_undoIndex = -1;
        if (beforePos == afterPos && beforeN == afterN) return; // no-op drag

        Engine::UndoManager::instance().record(std::make_unique<Engine::LambdaUndoCommand>(
            "Edit clip plane",
            [planes, idx, beforePos, beforeN]() {
                if (idx < static_cast<int>(planes->size())) {
                    (*planes)[idx].position = beforePos;
                    (*planes)[idx].normal = beforeN;
                }
            },
            [planes, idx, afterPos, afterN]() {
                if (idx < static_cast<int>(planes->size())) {
                    (*planes)[idx].position = afterPos;
                    (*planes)[idx].normal = afterN;
                }
            }));
    }

    // ── Overlay rendering ──────────────────────────────────────────────────
    void ClipPlaneTool::appendLine(std::vector<float>& out, const glm::vec3& a,
                                   const glm::vec3& b) const {
        out.insert(out.end(), { a.x, a.y, a.z, b.x, b.y, b.z });
    }

    void ClipPlaneTool::drawTris(const glm::mat4& viewProj,
                                 const std::vector<float>& verts,
                                 const glm::vec4& color) {
        if (verts.empty()) return;
        glUseProgram(m_program);
        glUniformMatrix4fv(glGetUniformLocation(m_program, "uViewProj"), 1, GL_FALSE,
                           &viewProj[0][0]);
        glUniform4fv(glGetUniformLocation(m_program, "uColor"), 1, &color[0]);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(verts.size() / 3));
    }

    void ClipPlaneTool::drawLines(const glm::mat4& viewProj,
                                  const std::vector<float>& verts,
                                  const glm::vec4& color, float width) {
        if (verts.empty()) return;
        glLineWidth(width);
        glUseProgram(m_program);
        glUniformMatrix4fv(glGetUniformLocation(m_program, "uViewProj"), 1, GL_FALSE,
                           &viewProj[0][0]);
        glUniform4fv(glGetUniformLocation(m_program, "uColor"), 1, &color[0]);
        glBindVertexArray(m_vao);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(verts.size() * sizeof(float)),
                     verts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(verts.size() / 3));
    }

    void ClipPlaneTool::render(const glm::mat4& projection, const glm::mat4& view,
                               const glm::vec3& /*cameraPos*/) {
        if (!m_enabled || !m_planes || m_planes->empty()) return;
        if (!m_initialized) initialize();

        clampActiveIndex();

        // Save the GL state we touch.
        GLint prevDepthFunc = GL_LESS;
        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
        GLboolean prevDepthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
        const GLboolean prevBlend = glIsEnabled(GL_BLEND);
        const GLboolean prevCull = glIsEnabled(GL_CULL_FACE);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDisable(GL_CULL_FACE);     // the quad must be visible from both sides
        glDepthMask(GL_FALSE);       // never write depth (cursor reads scene depth)
        glDepthFunc(GL_LEQUAL);

        const glm::mat4 viewProj = projection * view;
        const float S = displaySize;

        for (int i = 0; i < static_cast<int>(m_planes->size()); ++i) {
            const Engine::ClipPlane& p = (*m_planes)[i];
            const bool active = (i == m_activeIndex);

            const glm::vec3 n = p.unitNormal();
            const glm::vec3 ref = (std::fabs(n.y) < 0.99f) ? glm::vec3(0, 1, 0)
                                                          : glm::vec3(1, 0, 0);
            const glm::vec3 u = glm::normalize(glm::cross(ref, n));
            const glm::vec3 v = glm::cross(n, u);

            const glm::vec3 c0 = p.position + (-u - v) * S;
            const glm::vec3 c1 = p.position + ( u - v) * S;
            const glm::vec3 c2 = p.position + ( u + v) * S;
            const glm::vec3 c3 = p.position + (-u + v) * S;

            // Translucent fill (two triangles).
            const std::vector<float> tris = {
                c0.x, c0.y, c0.z, c1.x, c1.y, c1.z, c2.x, c2.y, c2.z,
                c0.x, c0.y, c0.z, c2.x, c2.y, c2.z, c3.x, c3.y, c3.z,
            };
            const float fillA = p.enabled ? (active ? 0.22f : 0.12f) : 0.05f;
            drawTris(viewProj, tris, glm::vec4(p.color, fillA));

            // Border quad + normal arrow.
            std::vector<float> lines;
            appendLine(lines, c0, c1);
            appendLine(lines, c1, c2);
            appendLine(lines, c2, c3);
            appendLine(lines, c3, c0);

            const glm::vec3 tip = p.position + n * S;
            appendLine(lines, p.position, tip);
            const glm::vec3 back = tip - n * (S * 0.18f);
            appendLine(lines, tip, back + u * (S * 0.10f));
            appendLine(lines, tip, back - u * (S * 0.10f));
            appendLine(lines, tip, back + v * (S * 0.10f));
            appendLine(lines, tip, back - v * (S * 0.10f));

            const float lineA = p.enabled ? (active ? 1.0f : 0.7f) : 0.35f;
            const glm::vec3 lineCol =
                active ? glm::mix(p.color, glm::vec3(1.0f), 0.4f) : p.color;
            drawLines(viewProj, lines, glm::vec4(lineCol, lineA),
                      active ? 2.5f : 1.5f);
        }

        // Restore state.
        glDepthFunc(prevDepthFunc);
        glDepthMask(prevDepthMask);
        if (!prevBlend) glDisable(GL_BLEND);
        if (prevCull) glEnable(GL_CULL_FACE);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glUseProgram(0);
    }

} // namespace Tools
