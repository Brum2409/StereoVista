#pragma once

#include "Cursor.h"
#include "Cursors/Types/SphereCursor.h"
#include "Cursors/Types/FragmentCursor.h"
#include "Cursors/Types/PlaneCursor.h"
#include "Core/Camera.h"
#include <memory>

namespace Cursor {
    // Central manager for all cursor types
    class CursorManager {
    public:
        CursorManager();
        ~CursorManager();

        // Initialize all cursor types
        void initialize();

        // Update cursor position based on ray casting
        void updateCursorPosition(GLFWwindow* window, const glm::mat4& projection, const glm::mat4& view, Engine::Shader* shader);
        
        // Update cursor position with control over when to actually calculate
        void updateCursorPosition(GLFWwindow* window, const glm::mat4& projection, const glm::mat4& view, Engine::Shader* shader, bool forceRecalculate);
        
        // Reset frame calculation flag (call at start of each frame)
        void resetFrameCalculationFlag();

        // Render all visible cursors
        void renderCursors(const glm::mat4& projection, const glm::mat4& view);

        // Update fragment shader uniforms for cursors
        void updateShaderUniforms(Engine::Shader* shader);

        // Clean up all cursors
        void cleanup();

        // Getters for cursor instances
        SphereCursor* getSphereCursor() { return m_sphereCursor.get(); }
        FragmentCursor* getFragmentCursor() { return m_fragmentCursor.get(); }
        PlaneCursor* getPlaneCursor() { return m_planeCursor.get(); }

        // Orbit center rendering
        void renderOrbitCenter(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& orbitPoint);

        // Orbit center properties
        bool isShowOrbitCenter() const { return m_showOrbitCenter; }
        void setShowOrbitCenter(bool show) { m_showOrbitCenter = show; }
        const glm::vec4& getOrbitCenterColor() const { return m_orbitCenterColor; }
        void setOrbitCenterColor(const glm::vec4& color) { m_orbitCenterColor = color; }
        float getOrbitCenterSphereRadius() const { return m_orbitCenterSphereRadius; }
        void setOrbitCenterSphereRadius(float radius) { m_orbitCenterSphereRadius = radius; }

        // Cursor position getters
        const glm::vec3& getCursorPosition() const { return m_cursorPosition; }
        bool isCursorPositionValid() const { return m_cursorPositionValid; }

    private:
        std::unique_ptr<SphereCursor> m_sphereCursor;
        std::unique_ptr<FragmentCursor> m_fragmentCursor;
        std::unique_ptr<PlaneCursor> m_planeCursor;

        // Cached cursor position shared between cursor types
        glm::vec3 m_cursorPosition;
        bool m_cursorPositionValid;
        bool m_cursorPositionCalculatedThisFrame;

        // Orbit center properties
        bool m_showOrbitCenter;
        glm::vec4 m_orbitCenterColor;
        float m_orbitCenterSphereRadius;

        // Window dimensions
        int m_windowWidth;
        int m_windowHeight;

        // Mouse position
        float m_lastX;
        float m_lastY;
    };
}