#pragma once

#include "Engine/Core.h"
#include <glm/glm.hpp>

namespace Tools {

    // Interactive transform gizmo that integrates with the 3D cursor / selection
    // system. When an object is selected the gizmo anchors at the object's pivot
    // and lets the user translate / rotate / scale it with constrained handles,
    // while the existing Ctrl/Alt body-drag free-move stays untouched.
    //
    // The gizmo is bound to a target each frame via setTarget(): a position
    // (always), an optional Euler-rotation (degrees, X-Y-Z order to match the
    // model matrix convention) and an optional non-uniform scale. Lights pass
    // only a position, which automatically restricts the gizmo to translation.
    //
    // Rendering mirrors MeasurementTool: a self-contained world-space overlay
    // drawn once per eye with that eye's matrices, using embedded shaders so the
    // tool has no asset-file dependency. Hit-testing is fully analytic, so it
    // needs no GL context and can run inside the input callbacks.
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
        ~TransformGizmo() = default;

        void cleanup();

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

        // Return the handle under the ray, or Handle::None. Pure query.
        // When outHitPoint is given it receives the world-space point on the
        // view ray at the picked handle's depth (only written when a handle is
        // hit), so callers can snap the 3D cursor onto the gizmo.
        Handle hitTest(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                       const glm::vec3& cameraPos,
                       glm::vec3* outHitPoint = nullptr) const;

        // Begin / update / end a drag on the given handle. updateDrag writes the
        // new transform straight into the bound target pointers.
        bool beginDrag(Handle handle, const glm::vec3& rayOrigin,
                       const glm::vec3& rayDir, const glm::vec3& cameraPos);
        void updateDrag(const glm::vec3& rayOrigin, const glm::vec3& rayDir,
                        const glm::vec3& cameraPos, bool snapHeld);
        void endDrag();
        bool isDragging() const { return m_activeHandle != Handle::None; }
        Handle activeHandle() const { return m_activeHandle; }

        // World-space point on the handle currently under the cursor (while
        // hovering) or being dragged. Valid only while a handle is engaged;
        // lets the caller place the 3D cursor at the gizmo's depth so the two
        // tools agree about where the handle sits in space.
        bool hasInteractionPoint() const { return m_hasInteractionPoint; }
        const glm::vec3& interactionPoint() const { return m_interactionPoint; }

        // Drop the cached interaction point and any post-drag latch. The host
        // calls this when the gizmo is idle (Ctrl not held and no active drag)
        // so a later re-engagement starts from a clean cursor state instead of
        // snapping the 3D cursor onto a stale handle point from a past drag.
        void clearInteractionPoint() {
            m_hasInteractionPoint = false;
            m_interactionLatched = false;
        }

        // ── Rendering ───────────────────────────────────────────────────────
        // World-space overlay; call once per eye with that eye's matrices.
        void render(const glm::mat4& projection, const glm::mat4& view,
                    const glm::vec3& cameraPos);

    private:
        // Target transform (borrowed, owned by the scene).
        glm::vec3* m_position = nullptr;
        glm::vec3* m_rotation = nullptr; // Euler degrees (X-Y-Z), may be null
        glm::vec3* m_scale = nullptr;    // may be null

        Mode   m_mode = Mode::Translate;
        Handle m_hover = Handle::None;
        Handle m_activeHandle = Handle::None;

        // Cached world point on the engaged handle (see interactionPoint()).
        glm::vec3 m_interactionPoint = glm::vec3(0.0f);
        bool      m_hasInteractionPoint = false;
        // After a drag ends the interaction point is "latched": updateHover()
        // then keeps it (instead of clearing it when the cursor isn't on a
        // handle) so the 3D cursor stays where the drag finished. Released by
        // the next hover that re-acquires a handle, a new drag, a mode/target
        // change, or clearInteractionPoint().
        bool      m_interactionLatched = false;

        // Geometry size at the pivot for the current camera distance.
        float gizmoScale(const glm::vec3& cameraPos) const;
        // Unit basis used for axis handles. translate/rotate honour `space`;
        // scale is always object-local. Returns world-space axes.
        void computeAxes(glm::vec3 outAxes[3], Mode forMode) const;
        glm::mat3 rotationMatrix() const; // from Euler degrees (identity if none)

        // Drag state captured at beginDrag.
        glm::vec3 m_startPos = glm::vec3(0.0f);
        glm::vec3 m_startScale = glm::vec3(1.0f);
        glm::mat3 m_startRot = glm::mat3(1.0f);
        glm::vec3 m_dragAxis = glm::vec3(0.0f);        // world axis / ring normal
        glm::vec3 m_dragPlaneNormal = glm::vec3(0.0f);
        glm::vec3 m_dragStartHit = glm::vec3(0.0f);
        glm::vec3 m_planeU = glm::vec3(0.0f);          // ring tangent basis
        glm::vec3 m_planeV = glm::vec3(0.0f);
        float m_startProj = 0.0f;                      // axis-drag reference
        float m_startAngle = 0.0f;                     // ring-drag reference
        float m_lastAngle = 0.0f;                      // ring continuity tracking
        int   m_turnCount = 0;                         // ring full-turn counter
        float m_startRadius = 1.0f;                    // uniform-scale reference
        float m_gizmoScaleAtDrag = 1.0f;
        // tan(verticalFOV/2), cached from the projection each render() so the
        // screen-constant size stays correct when the field of view changes.
        // Hit-testing (which runs in the input callbacks, before render) reads
        // last frame's value, which is fine as the FOV rarely changes per frame.
        float m_tanHalfFov = 0.4142f;                  // ≈ tan(22.5°) for a 45° FOV
        int   m_dragAxisIndex = -1;                    // 0/1/2 for scale

        // ── GL resources (lazy, embedded shaders) ──────────────────────────
        bool   m_initialized = false;
        void   ensureInit();
        void   drawMesh(unsigned int vao, int count, unsigned int prim,
                        const glm::mat4& mvp, const glm::vec4& color, float alpha);

        unsigned int m_program = 0;
        int m_uMVP = -1, m_uColor = -1, m_uAlpha = -1;

        unsigned int m_cylVAO = 0, m_cylVBO = 0; int m_cylCount = 0;   // +Y unit
        unsigned int m_coneVAO = 0, m_coneVBO = 0; int m_coneCount = 0; // +Y unit
        unsigned int m_cubeVAO = 0, m_cubeVBO = 0; int m_cubeCount = 0; // centred
        unsigned int m_ringVAO = 0, m_ringVBO = 0; int m_ringCount = 0; // XY loop
        unsigned int m_quadVAO = 0, m_quadVBO = 0; int m_quadCount = 0; // XY quad
        unsigned int m_discVAO = 0, m_discVBO = 0; int m_discCount = 0; // XY disc
    };

} // namespace Tools
