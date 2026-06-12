#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "Tools/MeasurementTool.h"
#include <cstdio>
#include <iostream>

namespace Tools {

    // ── Embedded overlay shaders ─────────────────────────────────────────────
    // Same pattern as BVHDebugRenderer: tiny self-contained programs so the
    // tool has no asset-file dependency.

    static const char* kLineVertexSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 uViewProj;

out vec3 vColor;

void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    vColor = aColor;
}
)";

    static const char* kLineFragmentSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;

uniform float uAlpha;

void main() {
    FragColor = vec4(vColor, uAlpha);
}
)";

    static const char* kPointVertexSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 uViewProj;
uniform float uPointSize;

out vec3 vColor;

void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
    gl_PointSize = uPointSize;
    vColor = aColor;
}
)";

    static const char* kPointFragmentSrc = R"(
#version 330 core
in vec3 vColor;
out vec4 FragColor;

uniform float uAlpha;

void main() {
    // Round marker with a dark rim for contrast on any background.
    vec2 d = gl_PointCoord * 2.0 - 1.0;
    float r2 = dot(d, d);
    if (r2 > 1.0) discard;
    float rim = smoothstep(0.45, 0.85, r2);
    FragColor = vec4(mix(vColor, vec3(0.05), rim), uAlpha);
}
)";

    static GLuint compileProgram(const char* vsSrc, const char* fsSrc, const char* label) {
        auto compile = [&](GLenum type, const char* src) -> GLuint {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);
            GLint ok = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024];
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::cerr << "[MeasurementTool] " << label << " shader compile error: "
                          << log << std::endl;
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
            std::cerr << "[MeasurementTool] " << label << " program link error: "
                      << log << std::endl;
        }
        return program;
    }

    MeasurementTool::MeasurementTool() = default;

    MeasurementTool::~MeasurementTool() {
        cleanup();
    }

    void MeasurementTool::initialize() {
        if (m_initialized) return;

        auto makeBuffers = [](GLuint& vao, GLuint& vbo) {
            glGenVertexArrays(1, &vao);
            glGenBuffers(1, &vbo);
            glBindVertexArray(vao);
            glBindBuffer(GL_ARRAY_BUFFER, vbo);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
            glEnableVertexAttribArray(0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float),
                                  (void*)(3 * sizeof(float)));
            glEnableVertexAttribArray(1);
            glBindBuffer(GL_ARRAY_BUFFER, 0);
            glBindVertexArray(0);
        };

        makeBuffers(m_lineVAO, m_lineVBO);
        makeBuffers(m_pointVAO, m_pointVBO);

        m_lineProgram = compileProgram(kLineVertexSrc, kLineFragmentSrc, "line");
        m_pointProgram = compileProgram(kPointVertexSrc, kPointFragmentSrc, "point");

        m_initialized = true;
    }

    void MeasurementTool::cleanup() {
        if (m_lineVAO) { glDeleteVertexArrays(1, &m_lineVAO); m_lineVAO = 0; }
        if (m_lineVBO) { glDeleteBuffers(1, &m_lineVBO); m_lineVBO = 0; }
        if (m_pointVAO) { glDeleteVertexArrays(1, &m_pointVAO); m_pointVAO = 0; }
        if (m_pointVBO) { glDeleteBuffers(1, &m_pointVBO); m_pointVBO = 0; }
        if (m_lineProgram) { glDeleteProgram(m_lineProgram); m_lineProgram = 0; }
        if (m_pointProgram) { glDeleteProgram(m_pointProgram); m_pointProgram = 0; }
        m_initialized = false;
    }

    void MeasurementTool::setEnabled(bool enabled) {
        if (m_enabled == enabled) return;
        m_enabled = enabled;
        if (!enabled) {
            finishActive(); // commit (or discard) whatever was in progress
            m_previewValid = false;
        }
    }

    void MeasurementTool::setMode(Engine::Measurement::Type mode) {
        if (m_mode == mode) return;
        finishActive();
        m_mode = mode;
    }

    void MeasurementTool::addPoint(const glm::vec3& worldPos) {
        if (!m_hasActive) {
            m_active = Engine::Measurement();
            m_active.type = m_mode;
            m_active.color = nextColor;
            m_nameCounter++;
            const char* prefix =
                (m_mode == Engine::Measurement::Type::Angle) ? "Angle" :
                (m_mode == Engine::Measurement::Type::Point) ? "Point" : "Distance";
            m_active.name = std::string(prefix) + " " + std::to_string(m_nameCounter);
            m_hasActive = true;
        }

        m_active.points.push_back(worldPos);

        if (m_active.type == Engine::Measurement::Type::Point &&
            m_active.points.size() >= 1) {
            commitActive();
        } else if (m_active.type == Engine::Measurement::Type::Angle &&
                   m_active.points.size() >= 3) {
            commitActive();
        }
    }

    void MeasurementTool::finishActive() {
        if (!m_hasActive) return;
        const size_t required =
            (m_active.type == Engine::Measurement::Type::Angle) ? 3 :
            (m_active.type == Engine::Measurement::Type::Point) ? 1 : 2;
        if (m_active.points.size() >= required) {
            commitActive();
        } else {
            cancelActive();
        }
    }

    void MeasurementTool::cancelActive() {
        m_hasActive = false;
        m_active = Engine::Measurement();
    }

    void MeasurementTool::undoLastPoint() {
        if (!m_hasActive || m_active.points.empty()) return;
        m_active.points.pop_back();
        if (m_active.points.empty()) {
            cancelActive();
        }
    }

    void MeasurementTool::commitActive() {
        if (m_measurements && !m_active.points.empty()) {
            m_measurements->push_back(std::move(m_active));
        }
        m_hasActive = false;
        m_active = Engine::Measurement();
    }

    void MeasurementTool::deleteMeasurement(int index) {
        if (!m_measurements) return;
        if (index >= 0 && index < static_cast<int>(m_measurements->size())) {
            m_measurements->erase(m_measurements->begin() + index);
        }
    }

    void MeasurementTool::clearAll() {
        cancelActive();
        if (m_measurements) m_measurements->clear();
    }

    std::string MeasurementTool::formatLength(float worldLength) const {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%.3f %s",
                      worldLength * unitScale, unitSuffix.c_str());
        return buf;
    }

    void MeasurementTool::appendLine(std::vector<float>& out, const glm::vec3& a,
                                     const glm::vec3& b, const glm::vec3& color) const {
        out.insert(out.end(), { a.x, a.y, a.z, color.r, color.g, color.b,
                                b.x, b.y, b.z, color.r, color.g, color.b });
    }

    void MeasurementTool::appendPoint(std::vector<float>& out, const glm::vec3& p,
                                      const glm::vec3& color) const {
        out.insert(out.end(), { p.x, p.y, p.z, color.r, color.g, color.b });
    }

    void MeasurementTool::render(const glm::mat4& projection, const glm::mat4& view,
                                 const glm::vec3* previewPoint) {
        if (!m_initialized) initialize();

        std::vector<float> lineVerts;
        std::vector<float> pointVerts;

        // Committed measurements (scene data)
        if (m_measurements) {
            for (const auto& m : *m_measurements) {
                if (!m.visible) continue;
                for (size_t i = 1; i < m.points.size(); i++)
                    appendLine(lineVerts, m.points[i - 1], m.points[i], m.color);
                for (const auto& p : m.points)
                    appendPoint(pointVerts, p, m.color);
            }
        }

        // In-progress measurement + live rubber-band to the cursor
        const glm::vec3 activeColor = glm::mix(nextColor, glm::vec3(1.0f), 0.25f);
        if (m_hasActive) {
            for (size_t i = 1; i < m_active.points.size(); i++)
                appendLine(lineVerts, m_active.points[i - 1], m_active.points[i], activeColor);
            for (const auto& p : m_active.points)
                appendPoint(pointVerts, p, activeColor);
        }

        m_previewValid = false;
        if (m_enabled && previewPoint) {
            m_previewPoint = *previewPoint;
            m_previewValid = true;
            if (m_hasActive && !m_active.points.empty()) {
                appendLine(lineVerts, m_active.points.back(), m_previewPoint,
                           glm::mix(activeColor, glm::vec3(1.0f), 0.5f));
            }
            appendPoint(pointVerts, m_previewPoint, glm::vec3(1.0f));
        }

        if (lineVerts.empty() && pointVerts.empty()) return;

        drawBuffers(projection, view, lineVerts, pointVerts);
    }

    void MeasurementTool::drawBuffers(const glm::mat4& projection, const glm::mat4& view,
                                      const std::vector<float>& lineVerts,
                                      const std::vector<float>& pointVerts) {
        // Save the GL state we touch.
        GLint prevDepthFunc = GL_LESS;
        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
        GLboolean prevDepthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
        const GLboolean prevBlend = glIsEnabled(GL_BLEND);
        const GLboolean prevPointSize = glIsEnabled(GL_PROGRAM_POINT_SIZE);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_PROGRAM_POINT_SIZE);
        // Overlay must never write depth: the cursor system samples scene depth
        // from this buffer.
        glDepthMask(GL_FALSE);
        glLineWidth(lineWidth);

        const glm::mat4 viewProj = projection * view;

        auto drawPass = [&](float alpha) {
            if (!lineVerts.empty()) {
                glUseProgram(m_lineProgram);
                glUniformMatrix4fv(glGetUniformLocation(m_lineProgram, "uViewProj"),
                                   1, GL_FALSE, &viewProj[0][0]);
                glUniform1f(glGetUniformLocation(m_lineProgram, "uAlpha"), alpha);
                glBindVertexArray(m_lineVAO);
                glBindBuffer(GL_ARRAY_BUFFER, m_lineVBO);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(lineVerts.size() * sizeof(float)),
                             lineVerts.data(), GL_DYNAMIC_DRAW);
                glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVerts.size() / 6));
            }
            if (!pointVerts.empty()) {
                glUseProgram(m_pointProgram);
                glUniformMatrix4fv(glGetUniformLocation(m_pointProgram, "uViewProj"),
                                   1, GL_FALSE, &viewProj[0][0]);
                glUniform1f(glGetUniformLocation(m_pointProgram, "uAlpha"), alpha);
                glUniform1f(glGetUniformLocation(m_pointProgram, "uPointSize"),
                            glm::max(6.0f, lineWidth * 4.0f));
                glBindVertexArray(m_pointVAO);
                glBindBuffer(GL_ARRAY_BUFFER, m_pointVBO);
                glBufferData(GL_ARRAY_BUFFER,
                             static_cast<GLsizeiptr>(pointVerts.size() * sizeof(float)),
                             pointVerts.data(), GL_DYNAMIC_DRAW);
                glDrawArrays(GL_POINTS, 0, static_cast<GLsizei>(pointVerts.size() / 6));
            }
        };

        // Pass 1: normally depth-tested, fully opaque.
        glDepthFunc(GL_LEQUAL);
        drawPass(1.0f);

        // Pass 2: occluded parts re-drawn ghosted so measurements stay legible
        // through geometry.
        if (xRay) {
            glDepthFunc(GL_GREATER);
            drawPass(0.22f);
        }

        // Restore state.
        glDepthFunc(prevDepthFunc);
        glDepthMask(prevDepthMask);
        if (!prevBlend) glDisable(GL_BLEND);
        if (!prevPointSize) glDisable(GL_PROGRAM_POINT_SIZE);
        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glUseProgram(0);
    }

} // namespace Tools
