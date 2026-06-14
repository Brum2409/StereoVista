#pragma once

#include "Engine/Core.h"
#include "Engine/Data.h"
#include "Engine/Shader.h"
#include "Tools/TransformGizmo.h"
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Tools {

    // Interactive section / clipping-plane tool. Up to Engine::MAX_CLIP_PLANES
    // user planes hide the scene geometry on one side of each plane. The planes
    // live in the active scene (Engine::Scene::clipPlanes) so they are saved /
    // loaded with scene files, exactly like measurements.
    //
    // Responsibilities:
    //   • Own the "active plane" selection and the gizmo Euler scratch used to
    //     drive a Tools::TransformGizmo bound to the active plane.
    //   • Pack the enabled planes for the scene vertex shaders (gl_ClipDistance
    //     1..N) and the point-cloud compute rasterizer.
    //   • Draw a translucent quad + normal arrow overlay per eye, mirroring the
    //     MeasurementTool / TransformGizmo overlay pattern (embedded shaders, no
    //     asset dependency).
    //
    // Clipping is applied whenever planes exist, regardless of the tool's
    // enabled state; "enabled" only controls the editing UX (overlay, gizmo
    // takeover, cursor/scroll nudging).
    class ClipPlaneTool {
    public:
        ClipPlaneTool();
        ~ClipPlaneTool();

        void initialize(); // create GL resources (requires a current context)
        void cleanup();

        // Bind the tool to the scene's clip-plane storage. The pointer must
        // outlive the tool (it points into the global scene object).
        void setPlanes(std::vector<Engine::ClipPlane>* planes) { m_planes = planes; }
        std::vector<Engine::ClipPlane>* getPlanes() { return m_planes; }

        // Editing mode (overlay + gizmo takeover + cursor/scroll nudge).
        void setEnabled(bool enabled) { m_enabled = enabled; }
        bool isEnabled() const { return m_enabled; }

        // ── Active-plane selection ───────────────────────────────────────────
        int  activeIndex() const { return m_activeIndex; }
        void setActiveIndex(int index);
        // True when there is a valid, in-range active plane.
        bool hasActivePlane() const;

        // ── Plane creation / editing ─────────────────────────────────────────
        // Add a plane at a world point with the given normal (camera-facing
        // fallback should be supplied by the caller). Returns the new index, or
        // -1 if the plane budget is full. The new plane becomes active.
        int addPlane(const glm::vec3& position, const glm::vec3& normal);
        // Add an axis-aligned plane (0 = X, 1 = Y, 2 = Z) through `center`.
        int addAxisAlignedPlane(int axis, const glm::vec3& center);
        void deletePlane(int index);
        void flipNormal(int index);
        // Slide the active plane along its normal (e.g. from scroll). Not
        // recorded for undo (used for quick scrubbing).
        void nudgeActive(float distance);

        // ── Shader / point-cloud integration ─────────────────────────────────
        // Fill out[] (size Engine::MAX_CLIP_PLANES) with the packed vec4 planes
        // (normal.xyz, d) of every ENABLED plane and return the count.
        int collectEnabledPlanes(glm::vec4 out[]) const;
        // Set clipPlaneCount + clipPlanes[i] on the given scene shader and return
        // the number of active (enabled) planes so the caller can enable the
        // matching GL_CLIP_DISTANCE slots. The shader must already be in use.
        int applyToShader(Engine::Shader* shader) const;

        // ── Gizmo glue ───────────────────────────────────────────────────────
        // Bind the gizmo to the active plane (position + Euler scratch). Returns
        // false when there is no valid active plane.
        bool bindGizmo(TransformGizmo& gizmo);
        // After a gizmo rotate drag, refresh the active plane's normal from the
        // gizmo Euler scratch.
        void syncActiveNormalFromGizmo();
        // Capture the active plane's state before a gizmo drag (for undo).
        void captureGizmoUndo();
        // Record an undo entry for the gizmo drag just finished.
        void recordGizmoUndo();

        // ── Rendering ────────────────────────────────────────────────────────
        // World-space overlay (translucent quad + normal arrow). Called once per
        // eye with that eye's matrices. Only drawn while the tool is enabled.
        void render(const glm::mat4& projection, const glm::mat4& view,
                    const glm::vec3& cameraPos);

        // ── Display settings (runtime) ───────────────────────────────────────
        float displaySize = 2.0f;   // half-extent of the visualised quad (world)
        float nudgeStep   = 0.10f;  // world units per scroll notch

    private:
        // Convert between a plane normal and the gizmo's X-Y-Z Euler degrees
        // (matching the TransformGizmo convention: normal = R(euler) * +Z).
        static glm::vec3 eulerFromNormal(const glm::vec3& normal);
        static glm::vec3 normalFromEuler(const glm::vec3& eulerDeg);

        bool validIndex(int index) const;
        void clampActiveIndex();
        void appendLine(std::vector<float>& out, const glm::vec3& a,
                        const glm::vec3& b) const;
        void drawTris(const glm::mat4& viewProj, const std::vector<float>& verts,
                      const glm::vec4& color);
        void drawLines(const glm::mat4& viewProj, const std::vector<float>& verts,
                       const glm::vec4& color, float width);

        std::vector<Engine::ClipPlane>* m_planes = nullptr;
        int  m_activeIndex = -1;
        bool m_enabled = false;
        int  m_nameCounter = 0;

        // Gizmo scratch: the gizmo edits position (in-place on the plane) and
        // this Euler triple; the plane normal is derived from it on drag.
        glm::vec3 m_gizmoEuler = glm::vec3(0.0f);

        // Undo snapshot captured at gizmo drag start.
        int       m_undoIndex = -1;
        glm::vec3 m_undoPos = glm::vec3(0.0f);
        glm::vec3 m_undoNormal = glm::vec3(0.0f, 1.0f, 0.0f);

        // GL resources (pos-only verts, dynamic; flat colour via uniform).
        bool   m_initialized = false;
        GLuint m_vao = 0, m_vbo = 0;
        GLuint m_program = 0;
    };

} // namespace Tools
