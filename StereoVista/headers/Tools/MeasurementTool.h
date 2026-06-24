#pragma once

#include "Engine/Core.h"
#include "Engine/Data.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Tools {

    // Interactive 3D measurement tool built on the 3D cursor pipeline.
    // Clicks place measurement points at the cursor's world position; the tool
    // renders the annotations (lines + point markers) as a world-space overlay
    // and exposes the data for screen-space labels drawn by the GUI.
    //
    // Committed measurements live in the active scene
    // (Engine::Scene::measurements) so they are saved/loaded with scene files.
    class MeasurementTool {
    public:
        MeasurementTool();
        ~MeasurementTool();

        void initialize(); // create GL resources (requires a current context)
        void cleanup();

        // Bind the tool to the scene's measurement storage. The pointer must
        // outlive the tool (it points into the global scene object).
        void setMeasurements(std::vector<Engine::Measurement>* measurements) {
            m_measurements = measurements;
        }
        std::vector<Engine::Measurement>* getMeasurements() { return m_measurements; }

        void setEnabled(bool enabled);
        bool isEnabled() const { return m_enabled; }

        void setMode(Engine::Measurement::Type mode);
        Engine::Measurement::Type getMode() const { return m_mode; }

        // ── Interaction ──────────────────────────────────────────────────────
        // Appends a point (world space) to the in-progress measurement,
        // starting a new one if needed. Angle measurements auto-commit at 3
        // points, Point annotations at 1.
        void addPoint(const glm::vec3& worldPos);
        void finishActive();   // commit the in-progress polyline (>= 2 points)
        void cancelActive();
        void undoLastPoint();  // remove last placed point of the active measurement
        bool hasActive() const { return m_hasActive; }
        const Engine::Measurement& getActive() const { return m_active; }

        void deleteMeasurement(int index);
        void clearAll();

        // ── Rendering ────────────────────────────────────────────────────────
        // World-space overlay (lines + point markers). Called once per eye with
        // that eye's matrices. previewPoint is the current 3D cursor position
        // (nullptr when invalid / tool disabled) used for the live rubber-band
        // segment.
        void render(const glm::mat4& projection, const glm::mat4& view,
                    const glm::vec3* previewPoint);

        // Last preview point passed to render(), consumed by the GUI label pass.
        bool getPreviewPoint(glm::vec3& out) const {
            if (m_previewValid) out = m_previewPoint;
            return m_previewValid;
        }

        // ── Display settings (runtime) ───────────────────────────────────────
        float lineWidth = 2.5f;
        bool  showLabels = true;
        bool  showSegmentLabels = true;
        bool  xRay = true;          // ghost-render occluded parts of the overlay
        float unitScale = 1.0f;     // world units → displayed units
        std::string unitSuffix = "m";
        glm::vec3 nextColor = glm::vec3(1.0f, 0.76f, 0.03f); // color for new measurements

        // Format a world-space length with the configured unit scale/suffix.
        std::string formatLength(float worldLength) const;

        // Export all committed measurements to a CSV file (tidy/long format:
        // one row per point, with the measurement's summary value repeated).
        // Lengths are written in the configured display units. Returns false if
        // the file could not be opened for writing.
        bool exportToCSV(const std::string& path) const;

    private:
        void commitActive();
        void appendLine(std::vector<float>& out, const glm::vec3& a,
                        const glm::vec3& b, const glm::vec3& color) const;
        void appendPoint(std::vector<float>& out, const glm::vec3& p,
                         const glm::vec3& color) const;
        void drawBuffers(const glm::mat4& projection, const glm::mat4& view,
                         const std::vector<float>& lineVerts,
                         const std::vector<float>& pointVerts);

        bool m_enabled = false;
        Engine::Measurement::Type m_mode = Engine::Measurement::Type::Distance;

        std::vector<Engine::Measurement>* m_measurements = nullptr;
        Engine::Measurement m_active;
        bool m_hasActive = false;
        int  m_nameCounter = 0;

        glm::vec3 m_previewPoint = glm::vec3(0.0f);
        bool m_previewValid = false;

        // GL resources (interleaved pos3 + color3, dynamic)
        bool   m_initialized = false;
        GLuint m_lineVAO = 0, m_lineVBO = 0;
        GLuint m_pointVAO = 0, m_pointVBO = 0;
        GLuint m_lineProgram = 0;
        GLuint m_pointProgram = 0;
    };

} // namespace Tools
