#pragma once

#include "Engine/Data.h"          // Engine::ClipPlane, Engine::MAX_CLIP_PLANES
#include "Tools/TransformGizmo.h"

#include <glm/glm.hpp>

#include <string>
#include <vector>

// Vulkan rewrite of the GL ClipPlaneTool. Up to Engine::MAX_CLIP_PLANES section
// planes hide scene geometry on one side. The plane math + the normal<->Euler
// gizmo glue are kept (pure glm); the GL VAO/shader rendering is replaced by the
// shared renderer::OverlayDrawList, undo goes through the app's core::UndoManager
// (not the old Engine::UndoManager singleton), and the packed planes feed
// FrameSubmission::clipPlanes (taking over from the interim debug-panel test —
// they already clip both point clouds and, since Phase 6a, meshes).
//
// The active plane is edited with a Tools::TransformGizmo (translate = slide the
// plane, rotate = steer the normal). Clipping applies whenever enabled planes
// exist, regardless of the tool's `enabled` state; `enabled` only drives the
// editing UX (overlay + gizmo takeover).

namespace renderer { class OverlayDrawList; }
namespace core { class UndoManager; }

namespace Tools {

    class ClipPlaneTool {
    public:
        ClipPlaneTool() = default;

        // Undo entries (add/delete/flip/gizmo-edit) go through this if set.
        void setUndoManager(core::UndoManager* undo) { m_undo = undo; }

        // Bind the tool to an externally owned plane vector — since UI redesign
        // Pass 1 that is scene::Scene::clipPlanes ("one heart"): the Outliner
        // and the scene document see exactly the planes this tool edits. The
        // pointed-to vector must outlive the tool (Application guarantees it:
        // scene_ is a member). Unbound, the tool falls back to a private
        // vector (keeps old call sites working).
        void bindStorage(std::vector<Engine::ClipPlane>* store) {
            m_store = store;
            clampActiveIndex();
        }
        // The bound storage's contents were replaced wholesale (scene load/
        // new/merge/undo): re-validate the active index.
        void notifyStorageChanged() {
            m_activeIndex = -1;
            clampActiveIndex();
        }

        void setEnabled(bool enabled) { m_enabled = enabled; }
        bool isEnabled() const { return m_enabled; }

        const std::vector<Engine::ClipPlane>& planes() const { return planesRef(); }
        std::vector<Engine::ClipPlane>& planes() { return planesRef(); }
        void clearPlanes() { planesRef().clear(); m_activeIndex = -1; }

        // ── Active-plane selection ───────────────────────────────────────────
        int  activeIndex() const { return m_activeIndex; }
        void setActiveIndex(int index);
        bool hasActivePlane() const;

        // ── Creation / editing ───────────────────────────────────────────────
        // Returns the new index, or -1 if the plane budget is full. The new
        // plane becomes active.
        int  addPlane(const glm::vec3& position, const glm::vec3& normal);
        int  addAxisAlignedPlane(int axis, const glm::vec3& center); // 0=X 1=Y 2=Z
        void deletePlane(int index);
        void flipNormal(int index);
        void nudgeActive(float distance); // slide the active plane along its normal

        // Fill out[] (size MAX_CLIP_PLANES) with the packed vec4 (n.xyz, d) of
        // every ENABLED plane and return the count. Fed to FrameSubmission.
        int collectEnabledPlanes(glm::vec4 out[]) const;

        // ── Gizmo glue ───────────────────────────────────────────────────────
        // Bind the gizmo to the active plane (position + an Euler scratch whose
        // +Z is the normal). Returns false if there is no active plane.
        bool bindGizmo(TransformGizmo& gizmo);
        void syncActiveNormalFromGizmo(); // after a rotate drag: normal <- Euler
        void captureGizmoUndo();          // at gizmo drag start
        void recordGizmoUndo();           // at gizmo drag end

        // ── Overlay (translucent quad + border + normal arrow, while enabled) ─
        void appendTo(renderer::OverlayDrawList& list) const;

        float displaySize = 2.0f;   // half-extent of the visualised quad (world)
        float nudgeStep   = 0.10f;

    private:
        static glm::vec3 eulerFromNormal(const glm::vec3& normal);
        static glm::vec3 normalFromEuler(const glm::vec3& eulerDeg);
        bool validIndex(int index) const;
        void clampActiveIndex();

        std::vector<Engine::ClipPlane>& planesRef() {
            return m_store ? *m_store : m_ownPlanes;
        }
        const std::vector<Engine::ClipPlane>& planesRef() const {
            return m_store ? *m_store : m_ownPlanes;
        }

        core::UndoManager* m_undo = nullptr;
        std::vector<Engine::ClipPlane>* m_store = nullptr; // scene storage
        std::vector<Engine::ClipPlane> m_ownPlanes;        // unbound fallback
        int  m_activeIndex = -1;
        bool m_enabled = false;
        int  m_nameCounter = 0;

        // Gizmo scratch: the gizmo edits position in-place + this Euler triple;
        // the plane normal is derived from it on drag.
        glm::vec3 m_gizmoEuler = glm::vec3(0.0f);

        // Undo snapshot captured at gizmo drag start.
        int       m_undoIndex = -1;
        glm::vec3 m_undoPos = glm::vec3(0.0f);
        glm::vec3 m_undoNormal = glm::vec3(0.0f, 1.0f, 0.0f);
    };

} // namespace Tools
