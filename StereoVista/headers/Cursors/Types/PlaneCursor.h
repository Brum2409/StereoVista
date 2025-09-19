#pragma once

#include "Cursors/Base/Cursor.h"
#include <glm/glm.hpp>

namespace Cursor {
    class PlaneCursor : public BaseCursor {
    public:
        PlaneCursor();
        ~PlaneCursor() override;

        // Implementation of base class virtual methods
        void initialize() override;
        void render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPosition);
        void cleanup() override;
        void updateShaderUniforms(Engine::Shader* shader) override;

        // Getters and setters for specific properties
        // Note: Diameter now uses unified scaling system
        float getDiameter() const { return getBaseSize(); }
        void setDiameter(float diameter) { setBaseSize(diameter); }
        float getCurrentDiameter() const { return getBaseSize() * getCurrentScale(); }
        const glm::vec4& getColor() const { return m_color; }
        void setColor(const glm::vec4& color) { m_color = color; }

    private:
        GLuint m_vao, m_vbo, m_ebo;
        Engine::Shader* m_shader;
        glm::vec4 m_color;
    };
}