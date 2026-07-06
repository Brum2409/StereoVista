#include "Cursors/Types/FragmentCursor.h"
#include "Renderer/FrameSubmission.h"

namespace Cursor {
    FragmentCursor::FragmentCursor() : BaseCursor() {
        m_name = "FragmentCursor";

        // Initialize with default settings
        m_settings.baseOuterRadius = 0.04f;
        m_settings.baseOuterBorderThickness = 0.005f;
        m_settings.baseInnerRadius = 0.004f;
        m_settings.baseInnerBorderThickness = 0.005f;
        m_settings.outerColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        m_settings.innerColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
    }

    FragmentCursor::~FragmentCursor() = default;

    void FragmentCursor::initialize() {
        // Implemented entirely in the scene fragment shader.
    }

    void FragmentCursor::fillState(renderer::FragmentCursorState& out) const {
        out.show = m_visible;
        out.position = m_position;
        out.valid = m_positionValid;
        out.outerColor = m_settings.outerColor;
        out.innerColor = m_settings.innerColor;
        out.outerRadius = m_settings.baseOuterRadius;
        out.outerThickness = m_settings.baseOuterBorderThickness;
        out.innerRadius = m_settings.baseInnerRadius;
        out.innerThickness = m_settings.baseInnerBorderThickness;
    }
}
