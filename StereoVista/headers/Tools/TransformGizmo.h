#pragma once

#include <glm/glm.hpp>

// Vulkan rewrite of the GL TransformGizmo. The valuable part — fully analytic
// hit-testing + drag math — is unchanged (it was always pure glm, GL-free and
// callable from input handling). What changed: the GL VAO/shader rendering is
// gone; the gizmo now describes its geometry into the shared
// renderer::OverlayDrawList (playbook C.9), and undo/selection are the host's
// job (the app captures the target transform at drag start and records one undo
// entry at drag end — the GL gizmo left that entirely to the caller).

namespace renderer { class OverlayDrawList; }

namespace Tools {

    // Interactive transform gizmo bound to a target transform each frame. It
    // anchors at the target's pivot and lets the user translate / rotate / scale
    // it with constrained handles. Lights / other targets can pass only a
    // position, which restricts the gizmo to translation.
    class TransformGizmo {
    public:
        enum class Mode { Translate, Rotate, Scale };
        enum class Space { World, Local };

        // Individual interactive parts of the gizmo.
        enum class Handle {
            None,
            AxisX, AxisY, AxisZ,        // single-axis translate / scale shafts
            PlaneXY, PlaneYZ, PlaneZX,  // two-axis translate quads
            RingX, RingY, RingZ,        // rotation rings
            ScaleUniform,               // centre box: uniform scale
            ScreenMove                  // centre box: view-plane translate
        };

        TransformGizmo() = default;

        // ── Target binding ──────────────────────────────────────────────────
        // rotationEulerDeg / scale may be null (e.g. lights); the corresponding
        // modes are then unavailable and fall back to translation.
        void setTarget(glm::vec3* position, glm::vec3* rotationEulerDeg,
                       glm::vec3* scale);
        void clearTarget();
        bool hasTarget() const { return m_position != nullptr; }
        bool canRotate() const { return m_rotation != nullptr; }
        bool canScale()  const { return m_scale != nullptr; }

        // ── Configuration ───────────────────────────────────────────────────
        bool  enabled = true;        // master on/off (GUI toggle)
        Space space = Space::World;  // world / local for translate & rotate
        bool  snapEnabled = false;   // persistent snapping (Shift = momentary)
        float snapTranslate = 0.25f; // world units
        float snapRotateDeg = 15.0f; // degrees
        float snapScale = 0.1f;      // scale increments
        float screenSize = 0.18f;    // apparent size as a fraction of the viewport
                                     // half-height (FOV-independent, see gizmoScale)

        Mode mode() const { return m_mode; }
        void setMode(Mode m);
        void cycleMode();            // Translate -> Rotate -> Scale -> Translate
        void toggleSpace() { space = (space == Space::World) ? Space::Local
                                                             : Space::World; }
        // The mode actually used this frame (falls back to Translate when the
        // target can't rotate/scale).
        Mode effectiveMode() const;

        // ── Interaction ─────────────────────────────────────────────────────
        // Update the hovered handle for highlighting (call when not dragging).
        Handle updateHover(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                           const glm::vec3& cameraPos);

        // Return the handle under the ray, or Handle::None. Pure query; when
        // outHitPoint is given it receives the world-space point on the ray at
        // the picked handle's depth.
        Handle hitTest(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                       const glm::vec3& cameraPos,
                       glm::vec3* outHitPoint = nullptr) const;

        // Begin / update / end a drag. updateDrag writes the new transform
        // straight into the bound target pointers.
        bool beginDrag(Handle handle, const glm::vec3& rayOrigin,
                       const glm::vec3& rayDir, const glm::vec3& cameraPos);
        void updateDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                        const glm::vec3& cameraPos, bool snapHeld);
        void endDrag();
        bool isDragging() const { return m_activeHandle != Handle::None; }
        Handle activeHandle() const { return m_activeHandle; }

        // World-space point on the engaged handle (hover or drag). Lets the host
        // co-locate the 3D cursor with the gizmo grab point.
        bool hasInteractionPoint() const { return m_hasInteractionPoint; }
        const glm::vec3& interactionPoint() const { return m_interactionPoint; }
        void clearInteractionPoint() {
            m_hasInteractionPoint = false;
            m_interactionLatched = false;
        }

        // ── Rendering ───────────────────────────────────────────────────────
        // Append the gizmo (world-space) to the shared overlay list. `projection`
        // is read only to keep a constant on-screen size across FOV changes.
        void appendTo(renderer::OverlayDrawList& list, const glm::mat4& projection,
                      const glm::vec3& cameraPos);

    private:
        // Target transform (borrowed, owned by the scene).
        glm::vec3* m_position = nullptr;
        glm::vec3* m_rotation = nullptr; // Euler degrees (X-Y-Z), may be null
        glm::vec3* m_scale = nullptr;    // may be null

        Mode   m_mode = Mode::Translate;
        Handle m_hover = Handle::None;
        Handle m_activeHandle = Handle::None;

        glm::vec3 m_interactionPoint = glm::vec3(0.0f);
        bool      m_hasInteractionPoint = false;
        bool      m_interactionLatched = false;

        float gizmoScale(const glm::vec3& cameraPos) const;
        void computeAxes(glm::vec3 outAxes[3], Mode forMode) const;
        glm::mat3 rotationMatrix() const;

        // Drag state captured at beginDrag.
        glm::vec3 m_startPos = glm::vec3(0.0f);
        glm::vec3 m_startScale = glm::vec3(1.0f);
        glm::mat3 m_startRot = glm::mat3(1.0f);
        glm::vec3 m_dragAxis = glm::vec3(0.0f);
        glm::vec3 m_dragPlaneNormal = glm::vec3(0.0f);
        glm::vec3 m_dragStartHit = glm::vec3(0.0f);
        glm::vec3 m_planeU = glm::vec3(0.0f);
        glm::vec3 m_planeV = glm::vec3(0.0f);
        float m_startProj = 0.0f;
        float m_startAngle = 0.0f;
        float m_lastAngle = 0.0f;
        int   m_turnCount = 0;
        float m_startRadius = 1.0f;
        float m_gizmoScaleAtDrag = 1.0f;
        // tan(verticalFOV/2), cached from the projection each appendTo() so the
        // screen-constant size stays correct. Hit-testing (runs before appendTo
        // in the input phase) reads last frame's value — fine, FOV rarely
        // changes per frame.
        float m_tanHalfFov = 0.4142f;
        int   m_dragAxisIndex = -1;
    };

} // namespace Tools
