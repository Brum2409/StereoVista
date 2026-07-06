#pragma once

#include "Engine/Data.h"   // Engine::Measurement (GL-free POD + math helpers)

#include <glm/glm.hpp>

#include <string>
#include <vector>

// Vulkan rewrite of the GL MeasurementTool. The interaction + math (point
// placement, auto-commit rules, length/angle/area via Engine::Measurement, CSV
// export) is kept; the GL VAO/shader rendering and the point-sprite program are
// gone — annotations are described into the shared renderer::OverlayDrawList
// (lines + round markers + translucent area fills, with the overlay's
// Occluded/Hidden depth modes giving the x-ray ghost pass for free), and the
// value labels are drawn with ImGui's foreground draw list.
//
// Committed measurements are owned here for now; scene-file persistence returns
// with the full SceneManager (Phase 8).

namespace renderer { class OverlayDrawList; }

namespace Tools {

    class MeasurementTool {
    public:
        MeasurementTool() = default;

        void setEnabled(bool enabled);
        bool isEnabled() const { return m_enabled; }

        void setMode(Engine::Measurement::Type mode);
        Engine::Measurement::Type getMode() const { return m_mode; }

        // ── Interaction ──────────────────────────────────────────────────────
        // Append a point (world space) to the in-progress measurement, starting
        // a new one if needed. Angle auto-commits at 3 points, Point at 1.
        void addPoint(const glm::vec3& worldPos);
        void finishActive();   // commit the in-progress measurement (>= required)
        void cancelActive();
        void undoLastPoint();
        bool hasActive() const { return m_hasActive; }
        const Engine::Measurement& getActive() const { return m_active; }

        void deleteMeasurement(int index);
        void clearAll();
        const std::vector<Engine::Measurement>& measurements() const {
            return m_measurements;
        }

        // ── Rendering ────────────────────────────────────────────────────────
        // Append world-space annotations to the overlay. previewPoint is the live
        // 3D-cursor position (nullptr = none) used for the rubber-band segment.
        void appendTo(renderer::OverlayDrawList& list, const glm::vec3* previewPoint);
        // Screen-space value labels via ImGui's foreground draw list. viewProj =
        // proj*view; the viewport rect is in ImGui screen coordinates.
        void drawLabels(const glm::mat4& viewProj, const glm::vec2& viewportPos,
                        const glm::vec2& viewportSize) const;

        // ── Display settings (runtime) ───────────────────────────────────────
        float lineWidth = 2.5f;
        bool  showLabels = true;
        bool  showSegmentLabels = true;
        bool  xRay = true;          // ghost-render occluded parts of the overlay
        float unitScale = 1.0f;     // world units → displayed units
        std::string unitSuffix = "m";
        glm::vec3 nextColor = glm::vec3(1.0f, 0.76f, 0.03f); // colour for new ones

        std::string formatLength(float worldLength) const;
        std::string formatArea(float worldArea) const;
        bool exportToCSV(const std::string& path) const;

    private:
        void commitActive();

        bool m_enabled = false;
        Engine::Measurement::Type m_mode = Engine::Measurement::Type::Distance;

        std::vector<Engine::Measurement> m_measurements; // owned (persistence later)
        Engine::Measurement m_active;
        bool m_hasActive = false;
        int  m_nameCounter = 0;

        glm::vec3 m_previewPoint = glm::vec3(0.0f);
        bool m_previewValid = false;
    };

} // namespace Tools
