#pragma once

#include "Engine/Core.h"
#include <glm/glm.hpp>
#include <string>

namespace Engine {
    class Shader;
}

namespace Cursor {
    // Base cursor class that all cursor types will inherit from
    class BaseCursor {
    public:
        BaseCursor();
        virtual ~BaseCursor();

        // Common cursor methods
        virtual void initialize() = 0;
        virtual void render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPosition) = 0;
        virtual void cleanup() = 0;
        virtual void updateShaderUniforms(Engine::Shader* shader) = 0;

        // Common cursor properties
        bool isVisible() const { return m_visible; }
        void setVisible(bool visible) { m_visible = visible; }
        const glm::vec3& getPosition() const { return m_position; }
        void setPosition(const glm::vec3& position) { m_position = position; }
        bool isPositionValid() const { return m_positionValid; }
        void setPositionValid(bool valid) { m_positionValid = valid; }

    protected:
        bool m_visible;
        glm::vec3 m_position;
        bool m_positionValid;
        std::string m_name;
    };
}