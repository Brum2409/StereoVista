#pragma once

#include "Cursors/Base/Cursor.h"
#include "Gui/GuiTypes.h"
#include <cstdint>
#include <vector>

namespace renderer {
class OverlayDrawList;
}

namespace Cursor {
    // Glassy sphere cursor. Owns only its CPU mesh (unit-ish sphere generated
    // once); rendering appends two culling passes (back faces with depth
    // write, then front faces) to the overlay draw list — the exact GL
    // two-pass transparency trick, expressed as dynamic cull state.
    class SphereCursor : public BaseCursor {
    public:
        SphereCursor();
        ~SphereCursor() override;

        void initialize() override;

        // Appends this cursor's geometry (and optionally the inner sphere).
        void appendTo(renderer::OverlayDrawList& list, const glm::vec3& cameraPosition);
        // Same mesh reused for the orbit-center marker (single color/size).
        void appendOrbitSphere(renderer::OverlayDrawList& list, const glm::vec3& center,
                               float radius, const glm::vec4& color);

        // Sphere cursor specific methods
        float calculateRadius(const glm::vec3& cameraPosition);
        void generateMesh(float radius, unsigned int rings, unsigned int sectors);

        float getFixedRadius() const { return getBaseSize(); }
        void setFixedRadius(float radius) { setBaseSize(radius); }
        const glm::vec4& getColor() const { return m_color; }
        void setColor(const glm::vec4& color) { m_color = color; }
        float getTransparency() const { return m_transparency; }
        void setTransparency(float transparency) { m_transparency = transparency; }
        float getEdgeSoftness() const { return m_edgeSoftness; }
        void setEdgeSoftness(float softness) { m_edgeSoftness = softness; }
        float getCenterTransparency() const { return m_centerTransparency; }
        void setCenterTransparency(float transparency) { m_centerTransparency = transparency; }
        bool getShowInnerSphere() const { return m_showInnerSphere; }
        void setShowInnerSphere(bool show) { m_showInnerSphere = show; }
        const glm::vec4& getInnerSphereColor() const { return m_innerSphereColor; }
        void setInnerSphereColor(const glm::vec4& color) { m_innerSphereColor = color; }
        float getInnerSphereFactor() const { return m_innerSphereFactor; }
        void setInnerSphereFactor(float factor) { m_innerSphereFactor = factor; }
        float getCurrentRadius() const { return getBaseSize() * getCurrentScale(); }

        // CPU mesh access (CursorPreview3D renders the same geometry).
        const std::vector<float>& getVertices() const { return m_vertices; }
        const std::vector<uint32_t>& getIndices() const { return m_indices; }

    private:
        friend class CursorManager;

        // Interleaved [pos.xyz, normal.xyz]; positions baked at the initialize-
        // time base radius, exactly like the GL VBO (later size changes only
        // affect the model scale — GL behaviour preserved).
        std::vector<float> m_vertices;
        std::vector<uint32_t> m_indices;
        float m_meshRadius = 0.05f;

        glm::vec4 m_color;
        float m_transparency;
        float m_edgeSoftness;
        float m_centerTransparency;
        bool m_showInnerSphere;
        glm::vec4 m_innerSphereColor;
        float m_innerSphereFactor;
    };
}
