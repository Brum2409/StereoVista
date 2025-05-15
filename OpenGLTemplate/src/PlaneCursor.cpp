#include "PlaneCursor.h"
#include "engine/shader.h"
#include <corecrt_math_defines.h>
#include <vector>

namespace Cursor {
    PlaneCursor::PlaneCursor() :
        BaseCursor(),
        m_vao(0),
        m_vbo(0),
        m_ebo(0),
        m_shader(nullptr),
        m_diameter(0.5f),
        m_color(0.0f, 1.0f, 0.0f, 0.7f)
    {
        m_name = "PlaneCursor";
    }

    PlaneCursor::~PlaneCursor() {
        cleanup();
    }

    void PlaneCursor::initialize() {
        // Create a circular plane using triangles
        std::vector<float> vertices;
        std::vector<unsigned int> indices;

        const int segments = 32;
        const float radius = 0.5f; // Unit radius, will be scaled by diameter

        // Center vertex
        vertices.push_back(0.0f); // x
        vertices.push_back(0.0f); // y
        vertices.push_back(0.0f); // z

        // Circle vertices
        for (int i = 0; i <= segments; i++) {
            float angle = (2.0f * M_PI * i) / segments;
            vertices.push_back(radius * cos(angle));
            vertices.push_back(radius * sin(angle));
            vertices.push_back(0.0f);
        }

        // Indices for triangle fan
        for (int i = 0; i < segments; i++) {
            indices.push_back(0);
            indices.push_back(i + 1);
            indices.push_back(i + 2);
        }

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);

        glBindVertexArray(m_vao);

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        try {
            m_shader = Engine::loadShader("planeCursorVertexShader.glsl", "planeCursorFragmentShader.glsl");
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to load plane cursor shaders: " << e.what() << std::endl;
        }
    }

    void PlaneCursor::render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPosition) {
        if (!m_visible || !m_positionValid || !m_shader) return;

        m_shader->use();
        m_shader->setMat4("projection", projection);
        m_shader->setMat4("view", view);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), m_position);

        // Orient plane to face camera
        glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
        glm::vec3 forward = glm::normalize(cameraPosition - m_position);
        glm::vec3 right = glm::normalize(glm::cross(up, forward));
        up = glm::cross(forward, right);

        glm::mat4 rotation = glm::mat4(
            glm::vec4(right, 0.0f),
            glm::vec4(up, 0.0f),
            glm::vec4(forward, 0.0f),
            glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
        );

        model = model * rotation;
        model = glm::scale(model, glm::vec3(m_diameter));

        m_shader->setMat4("model", model);
        m_shader->setVec4("color", m_color);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        glBindVertexArray(m_vao);
        glDrawElements(GL_TRIANGLES, 96, GL_UNSIGNED_INT, 0);

        glDisable(GL_BLEND);
        glBindVertexArray(0);
    }

    void PlaneCursor::cleanup() {
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_ebo) glDeleteBuffers(1, &m_ebo);

        m_vao = m_vbo = m_ebo = 0;

        if (m_shader) {
            delete m_shader;
            m_shader = nullptr;
        }
    }

    void PlaneCursor::updateShaderUniforms(Engine::Shader* shader) {
        // No specific uniforms to update for the main shader
    }
}