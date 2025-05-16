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
        float getDiameter() const { return m_diameter; }
        void setDiameter(float diameter) { m_diameter = diameter; }
        const glm::vec4& getColor() const { return m_color; }
        void setColor(const glm::vec4& color) { m_color = color; }

    private:
        GLuint m_vao, m_vbo, m_ebo;
        Engine::Shader* m_shader;
        float m_diameter;
        glm::vec4 m_color;
    };
}