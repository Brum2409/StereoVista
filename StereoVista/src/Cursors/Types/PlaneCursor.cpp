#include "Cursors/Types/PlaneCursor.h"
#include "Renderer/OverlayDrawList.h"

namespace Cursor {
    PlaneCursor::PlaneCursor() :
        BaseCursor(),
        m_color(0.0f, 1.0f, 0.0f, 0.7f)
    {
        m_name = "PlaneCursor";
        setBaseSize(0.5f);  // Set default diameter
    }

    PlaneCursor::~PlaneCursor() = default;

    void PlaneCursor::initialize() {
        // No GPU resources anymore; the overlay renderer draws the disc.
    }

    void PlaneCursor::appendTo(renderer::OverlayDrawList& list,
                               const glm::vec3& cameraPosition) {
        if (!m_visible || !m_positionValid)
            return;
        // GL parity: unit-radius 0.5 fan scaled by the current diameter =
        // world radius diameter/2, camera-facing, blended, depth-tested.
        const float currentDiameter = getBaseSize() * calculateScale(cameraPosition);
        list.disc(m_position, currentDiameter * 0.5f, m_color,
                  renderer::OverlayDepth::Occluded, /*softEdge=*/false);
    }
}
