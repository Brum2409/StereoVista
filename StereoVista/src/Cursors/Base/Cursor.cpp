#include "Cursors/Base/Cursor.h"
#include <algorithm>
#include <cmath>

namespace Cursor {
    BaseCursor::BaseCursor() :
        m_visible(true),
        m_position(0.0f, 0.0f, 0.0f),
        m_positionValid(false),
        m_name("BaseCursor"),
        m_scalingMode(GUI::CURSOR_CONSTRAINED_DYNAMIC),
        m_baseSize(0.05f),
        m_currentScale(1.0f),
        m_minDiff(0.01f),
        m_maxDiff(0.1f)
    {
    }

    BaseCursor::~BaseCursor() {
        // Base destructor
    }

    float BaseCursor::calculateScale(const glm::vec3& cameraPosition) {
        float distance = glm::distance(m_position, cameraPosition);

        switch (m_scalingMode) {
        case GUI::CURSOR_NORMAL:
            m_currentScale = 1.0f;  // Fixed scale, no distance dependency
            break;

        case GUI::CURSOR_FIXED:
            m_currentScale = distance;  // Linear scaling with distance
            break;

        case GUI::CURSOR_CONSTRAINED_DYNAMIC: {
            float distanceFactor = std::sqrt(distance);
            float defaultScreenSize = std::pow(m_baseSize, 2) * distanceFactor;
            float minScreenSize = std::pow(m_baseSize - m_minDiff, 2) * distanceFactor;
            float maxScreenSize = std::pow(m_baseSize + m_maxDiff, 2) * distanceFactor;
            m_currentScale = std::clamp(m_currentScale, minScreenSize / m_baseSize, maxScreenSize / m_baseSize);
            break;
        }

        case GUI::CURSOR_LOGARITHMIC:
            m_currentScale = (1.0f + std::log(distance));  // Logarithmic scaling
            break;

        default:
            m_currentScale = distance;  // Default to fixed scaling
            break;
        }

        return m_currentScale;
    }
}