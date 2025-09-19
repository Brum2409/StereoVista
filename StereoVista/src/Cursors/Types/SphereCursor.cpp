#include "Cursors/Types/SphereCursor.h"
#include "Engine/Shader.h"
#include <corecrt_math_defines.h>

namespace Cursor {
    SphereCursor::SphereCursor() :
        BaseCursor(),
        m_vao(0),
        m_vbo(0),
        m_ebo(0),
        m_shader(nullptr),
        m_color(1.0f, 0.0f, 0.0f, 0.7f),
        m_transparency(0.7f),
        m_edgeSoftness(0.8f),
        m_centerTransparency(0.2f),
        m_showInnerSphere(false),
        m_innerSphereColor(0.0f, 1.0f, 0.0f, 1.0f),
        m_innerSphereFactor(0.1f)
    {
        m_name = "SphereCursor";
    }

    SphereCursor::~SphereCursor() {
        cleanup();
    }

    void SphereCursor::initialize() {
        generateMesh(getBaseSize(), 32, 32);

        glGenVertexArrays(1, &m_vao);
        glGenBuffers(1, &m_vbo);
        glGenBuffers(1, &m_ebo);

        glBindVertexArray(m_vao);

        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, m_vertices.size() * sizeof(float), m_vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_indices.size() * sizeof(unsigned int), m_indices.data(), GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

        glBindVertexArray(0);

        try {
            m_shader = Engine::loadShader("cursors/sphereVertexShader.glsl", "cursors/sphereFragmentShader.glsl");
        }
        catch (const std::exception& e) {
            std::cerr << "Fatal error loading sphere cursor shader: " << e.what() << std::endl;
        }

        // Initialize uniforms
        if (m_shader) {
            m_shader->use();
            m_shader->setMat4("projection", glm::mat4(1.0f));
            m_shader->setMat4("view", glm::mat4(1.0f));
            m_shader->setMat4("model", glm::mat4(1.0f));
            m_shader->setVec3("viewPos", glm::vec3(0.0f));
        }
    }

    void SphereCursor::render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPosition) {
        if (!m_visible || !m_positionValid || !m_shader) return;

        // Enable blending and depth testing
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);

        m_shader->use();
        m_shader->setMat4("projection", projection);
        m_shader->setMat4("view", view);
        m_shader->setVec3("viewPos", cameraPosition);

        // Calculate current scale using the unified scaling system
        float currentRadius = getBaseSize() * calculateScale(cameraPosition);

        glm::mat4 model = glm::translate(glm::mat4(1.0f), m_position);
        model = glm::scale(model, glm::vec3(currentRadius));

        m_shader->setMat4("model", model);
        m_shader->setFloat("innerSphereFactor", m_innerSphereFactor);

        glBindVertexArray(m_vao);

        // First pass: Render back faces
        glDepthMask(GL_TRUE);
        glCullFace(GL_FRONT);

        if (m_showInnerSphere) {
            m_shader->setBool("isInnerSphere", true);
            m_shader->setVec4("sphereColor", m_innerSphereColor);
            m_shader->setFloat("transparency", 1.0);
            glm::mat4 innerModel = glm::scale(model, glm::vec3(m_innerSphereFactor));
            m_shader->setMat4("model", innerModel);
            glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, 0);
        }

        m_shader->setBool("isInnerSphere", false);
        m_shader->setVec4("sphereColor", m_color);
        m_shader->setFloat("transparency", m_transparency);
        m_shader->setFloat("edgeSoftness", m_edgeSoftness);
        m_shader->setFloat("centerTransparencyFactor", m_centerTransparency);
        m_shader->setMat4("model", model);
        glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, 0);

        // Second pass: Render front faces
        glDepthMask(GL_FALSE);
        glCullFace(GL_BACK);

        if (m_showInnerSphere) {
            m_shader->setBool("isInnerSphere", true);
            m_shader->setVec4("sphereColor", m_innerSphereColor);
            m_shader->setFloat("transparency", 1.0);
            glm::mat4 innerModel = glm::scale(model, glm::vec3(m_innerSphereFactor));
            m_shader->setMat4("model", innerModel);
            glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, 0);
        }

        m_shader->setBool("isInnerSphere", false);
        m_shader->setVec4("sphereColor", m_color);
        m_shader->setFloat("transparency", m_transparency);
        m_shader->setFloat("edgeSoftness", m_edgeSoftness);
        m_shader->setFloat("centerTransparencyFactor", m_centerTransparency);
        m_shader->setMat4("model", model);
        glDrawElements(GL_TRIANGLES, m_indices.size(), GL_UNSIGNED_INT, 0);

        // Reset OpenGL state
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        glUseProgram(0);
    }

    void SphereCursor::cleanup() {
        if (m_vao) glDeleteVertexArrays(1, &m_vao);
        if (m_vbo) glDeleteBuffers(1, &m_vbo);
        if (m_ebo) glDeleteBuffers(1, &m_ebo);

        m_vao = m_vbo = m_ebo = 0;

        if (m_shader) {
            delete m_shader;
            m_shader = nullptr;
        }
    }

    void SphereCursor::updateShaderUniforms(Engine::Shader* shader) {
        // No need to set any specific uniforms for the main shader
    }

    float SphereCursor::calculateRadius(const glm::vec3& cameraPosition) {
        // Use the unified scaling system from BaseCursor
        return getBaseSize() * calculateScale(cameraPosition);
    }

    void SphereCursor::generateMesh(float radius, unsigned int rings, unsigned int sectors) {
        m_vertices.clear();
        m_indices.clear();

        float const R = 1.0f / (float)(rings - 1);
        float const S = 1.0f / (float)(sectors - 1);

        for (unsigned int r = 0; r < rings; ++r) {
            for (unsigned int s = 0; s < sectors; ++s) {
                float const y = sin(-M_PI_2 + M_PI * r * R);
                float const x = cos(2 * M_PI * s * S) * sin(M_PI * r * R);
                float const z = sin(2 * M_PI * s * S) * sin(M_PI * r * R);

                m_vertices.push_back(x * radius);
                m_vertices.push_back(y * radius);
                m_vertices.push_back(z * radius);

                m_vertices.push_back(x);
                m_vertices.push_back(y);
                m_vertices.push_back(z);
            }
        }

        for (unsigned int r = 0; r < rings - 1; ++r) {
            for (unsigned int s = 0; s < sectors - 1; ++s) {
                m_indices.push_back(r * sectors + s);
                m_indices.push_back(r * sectors + (s + 1));
                m_indices.push_back((r + 1) * sectors + (s + 1));

                m_indices.push_back(r * sectors + s);
                m_indices.push_back((r + 1) * sectors + (s + 1));
                m_indices.push_back((r + 1) * sectors + s);
            }
        }
    }
}