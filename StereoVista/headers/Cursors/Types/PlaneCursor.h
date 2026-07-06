#pragma once

#include "Cursors/Base/Cursor.h"
#include <glm/glm.hpp>

namespace renderer {
class OverlayDrawList;
}

namespace Cursor {
    // Flat camera-facing disc cursor. GPU-free: one billboard on the overlay
    // draw list (the GL triangle-fan VBO + planeCursor shaders are gone).
    class PlaneCursor : public BaseCursor {
    public:
        PlaneCursor();
        ~PlaneCursor() override;

        void initialize() override;
        void appendTo(renderer::OverlayDrawList& list, const glm::vec3& cameraPosition);

        float getDiameter() const { return getBaseSize(); }
        void setDiameter(float diameter) { setBaseSize(diameter); }
        float getCurrentDiameter() const { return getBaseSize() * getCurrentScale(); }
        const glm::vec4& getColor() const { return m_color; }
        void setColor(const glm::vec4& color) { m_color = color; }

    private:
        glm::vec4 m_color;
    };
}
