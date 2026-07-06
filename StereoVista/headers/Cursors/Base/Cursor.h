#pragma once

// 3D cursor base class. GPU-API-free since Phase 6 of the Vulkan migration:
// cursors no longer own shaders/VAOs — they append world-space geometry to
// the unified renderer::OverlayDrawList (playbook C.9), and the fragment
// (ring) cursor publishes its state into the FrameSubmission for mesh.frag.

#include "Gui/GuiTypes.h"
#include <glm/glm.hpp>
#include <string>

namespace Cursor {
    // Base cursor class that all cursor types will inherit from
    class BaseCursor {
    public:
        BaseCursor();
        virtual ~BaseCursor();

        // Common cursor methods
        virtual void initialize() = 0;

        // Common cursor properties
        bool isVisible() const { return m_visible; }
        void setVisible(bool visible) { m_visible = visible; }
        const glm::vec3& getPosition() const { return m_position; }
        void setPosition(const glm::vec3& position) { m_position = position; }
        bool isPositionValid() const { return m_positionValid; }
        void setPositionValid(bool valid) { m_positionValid = valid; }

        // Unified scaling system for all cursor types
        GUI::CursorScalingMode getScalingMode() const { return m_scalingMode; }
        void setScalingMode(GUI::CursorScalingMode mode) { m_scalingMode = mode; }
        float getBaseSize() const { return m_baseSize; }
        void setBaseSize(float size) { m_baseSize = size; }
        float getCurrentScale() const { return m_currentScale; }
        float calculateScale(const glm::vec3& cameraPosition);

        // Scaling parameters
        float getMinDiff() const { return m_minDiff; }
        void setMinDiff(float diff) { m_minDiff = diff; }
        float getMaxDiff() const { return m_maxDiff; }
        void setMaxDiff(float diff) { m_maxDiff = diff; }

    protected:
        bool m_visible;
        glm::vec3 m_position;
        bool m_positionValid;
        std::string m_name;

        // Unified scaling properties
        GUI::CursorScalingMode m_scalingMode;
        float m_baseSize;
        float m_currentScale;
        float m_minDiff;
        float m_maxDiff;
    };
}
