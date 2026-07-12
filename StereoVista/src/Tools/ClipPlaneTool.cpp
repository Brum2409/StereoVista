#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif

#include "Tools/ClipPlaneTool.h"

#include "Core/UndoManager.h"
#include "Renderer/OverlayDrawList.h"

#include <glm/gtx/euler_angles.hpp> // eulerAngleXYZ, extractEulerAngleXYZ

#include <algorithm>
#include <cmath>
#include <iostream>

namespace Tools {

using renderer::OverlayDepth;
using renderer::OverlayDrawList;

// ── Normal <-> Euler conversion (TransformGizmo convention) ──────────────────
// The gizmo stores rotations as X-Y-Z Euler degrees; its rotation matrix is
// mat3(eulerAngleXYZ(...)). We define the plane normal as the +Z column, so the
// gizmo's rotate handles steer the normal directly.
glm::vec3 ClipPlaneTool::eulerFromNormal(const glm::vec3& normal) {
    glm::vec3 z = normal;
    const float len = glm::length(z);
    z = (len > 1e-6f) ? z / len : glm::vec3(0.0f, 0.0f, 1.0f);
    const glm::vec3 ref = (std::fabs(z.y) < 0.99f) ? glm::vec3(0, 1, 0)
                                                   : glm::vec3(1, 0, 0);
    const glm::vec3 x = glm::normalize(glm::cross(ref, z));
    const glm::vec3 y = glm::cross(z, x);
    glm::mat4 R(1.0f);
    R[0] = glm::vec4(x, 0.0f);
    R[1] = glm::vec4(y, 0.0f);
    R[2] = glm::vec4(z, 0.0f);
    float ex, ey, ez;
    glm::extractEulerAngleXYZ(R, ex, ey, ez);
    return glm::degrees(glm::vec3(ex, ey, ez));
}

glm::vec3 ClipPlaneTool::normalFromEuler(const glm::vec3& eulerDeg) {
    const glm::mat4 R = glm::eulerAngleXYZ(glm::radians(eulerDeg.x),
                                           glm::radians(eulerDeg.y),
                                           glm::radians(eulerDeg.z));
    return glm::normalize(glm::vec3(R[2])); // R * (0,0,1)
}

// ── Selection ────────────────────────────────────────────────────────────────
bool ClipPlaneTool::validIndex(int index) const {
    return index >= 0 && index < static_cast<int>(planesRef().size());
}

bool ClipPlaneTool::hasActivePlane() const { return validIndex(m_activeIndex); }

void ClipPlaneTool::setActiveIndex(int index) {
    m_activeIndex = validIndex(index) ? index : -1;
}

void ClipPlaneTool::clampActiveIndex() {
    if (planesRef().empty()) { m_activeIndex = -1; return; }
    if (m_activeIndex >= static_cast<int>(planesRef().size()))
        m_activeIndex = static_cast<int>(planesRef().size()) - 1;
}

// ── Creation / editing ───────────────────────────────────────────────────────
int ClipPlaneTool::addPlane(const glm::vec3& position, const glm::vec3& normal) {
    if (static_cast<int>(planesRef().size()) >= Engine::MAX_CLIP_PLANES) {
        std::cerr << "[ClipPlaneTool] clip-plane budget full ("
                  << Engine::MAX_CLIP_PLANES << ")\n";
        return -1;
    }

    Engine::ClipPlane p;
    p.position = position;
    const float len = glm::length(normal);
    p.normal = (len > 1e-6f) ? (normal / len) : glm::vec3(0.0f, 1.0f, 0.0f);
    p.enabled = true;
    p.name = "Plane " + std::to_string(++m_nameCounter);

    planesRef().push_back(p);
    const int idx = static_cast<int>(planesRef().size()) - 1;
    m_activeIndex = idx;

    if (m_undo) {
        std::vector<Engine::ClipPlane>* planes = &planesRef();
        const Engine::ClipPlane val = p;
        m_undo->record(
            "Add clip plane",
            [planes, idx]() {
                if (idx < static_cast<int>(planes->size()))
                    planes->erase(planes->begin() + idx);
            },
            [planes, idx, val]() {
                const size_t at = std::min(static_cast<size_t>(idx), planes->size());
                planes->insert(planes->begin() + at, val);
            });
    }
    return idx;
}

int ClipPlaneTool::addAxisAlignedPlane(int axis, const glm::vec3& center) {
    glm::vec3 n(0.0f);
    n[std::min(std::max(axis, 0), 2)] = 1.0f;
    return addPlane(center, n);
}

void ClipPlaneTool::deletePlane(int index) {
    if (!validIndex(index)) return;
    std::vector<Engine::ClipPlane>* planes = &planesRef();
    const Engine::ClipPlane val = planesRef()[index];
    planesRef().erase(planesRef().begin() + index);
    clampActiveIndex();

    if (m_undo) {
        m_undo->record(
            "Delete clip plane",
            [planes, index, val]() {
                const size_t at = std::min(static_cast<size_t>(index), planes->size());
                planes->insert(planes->begin() + at, val);
            },
            [planes, index]() {
                if (index < static_cast<int>(planes->size()))
                    planes->erase(planes->begin() + index);
            });
    }
}

void ClipPlaneTool::flipNormal(int index) {
    if (!validIndex(index)) return;
    planesRef()[index].normal = -planesRef()[index].normal;

    if (m_undo) {
        std::vector<Engine::ClipPlane>* planes = &planesRef();
        auto flip = [planes, index]() {
            if (index < static_cast<int>(planes->size()))
                (*planes)[index].normal = -(*planes)[index].normal;
        };
        m_undo->record("Flip clip plane", flip, flip);
    }
}

void ClipPlaneTool::nudgeActive(float distance) {
    if (!hasActivePlane()) return;
    Engine::ClipPlane& p = planesRef()[m_activeIndex];
    p.position += p.unitNormal() * distance;
}

// ── FrameSubmission integration ──────────────────────────────────────────────
int ClipPlaneTool::collectEnabledPlanes(glm::vec4 out[]) const {
    int n = 0;
    for (const Engine::ClipPlane& p : planesRef()) {
        if (!p.enabled) continue;
        if (n >= Engine::MAX_CLIP_PLANES) break;
        out[n++] = p.packed();
    }
    return n;
}

// ── Gizmo glue ───────────────────────────────────────────────────────────────
bool ClipPlaneTool::bindGizmo(TransformGizmo& gizmo) {
    if (!hasActivePlane()) return false;
    Engine::ClipPlane& p = planesRef()[m_activeIndex];
    // Refresh the Euler scratch from the normal while not dragging so the gizmo's
    // rotation handles show the plane's true orientation.
    if (!gizmo.isDragging())
        m_gizmoEuler = eulerFromNormal(p.normal);
    gizmo.setTarget(&p.position, &m_gizmoEuler, nullptr); // no scale for a plane
    return true;
}

void ClipPlaneTool::syncActiveNormalFromGizmo() {
    if (!hasActivePlane()) return;
    planesRef()[m_activeIndex].normal = normalFromEuler(m_gizmoEuler);
}

void ClipPlaneTool::captureGizmoUndo() {
    if (!hasActivePlane()) { m_undoIndex = -1; return; }
    m_undoIndex = m_activeIndex;
    m_undoPos = planesRef()[m_activeIndex].position;
    m_undoNormal = planesRef()[m_activeIndex].normal;
}

void ClipPlaneTool::recordGizmoUndo() {
    if (!validIndex(m_undoIndex)) { m_undoIndex = -1; return; }
    const int idx = m_undoIndex;
    m_undoIndex = -1;
    const glm::vec3 beforePos = m_undoPos, beforeN = m_undoNormal;
    const glm::vec3 afterPos = planesRef()[idx].position;
    const glm::vec3 afterN = planesRef()[idx].normal;
    if (beforePos == afterPos && beforeN == afterN) return; // no-op drag
    if (!m_undo) return;

    std::vector<Engine::ClipPlane>* planes = &planesRef();
    m_undo->record(
        "Edit clip plane",
        [planes, idx, beforePos, beforeN]() {
            if (idx < static_cast<int>(planes->size())) {
                (*planes)[idx].position = beforePos;
                (*planes)[idx].normal = beforeN;
            }
        },
        [planes, idx, afterPos, afterN]() {
            if (idx < static_cast<int>(planes->size())) {
                (*planes)[idx].position = afterPos;
                (*planes)[idx].normal = afterN;
            }
        });
}

// ── Overlay rendering ─────────────────────────────────────────────────────────
void ClipPlaneTool::appendTo(OverlayDrawList& list) const {
    if (!m_enabled || planesRef().empty()) return;
    const float S = displaySize;

    for (int i = 0; i < static_cast<int>(planesRef().size()); ++i) {
        const Engine::ClipPlane& p = planesRef()[i];
        const bool active = (i == m_activeIndex);

        const glm::vec3 n = p.unitNormal();
        const glm::vec3 ref = (std::fabs(n.y) < 0.99f) ? glm::vec3(0, 1, 0)
                                                       : glm::vec3(1, 0, 0);
        const glm::vec3 u = glm::normalize(glm::cross(ref, n));
        const glm::vec3 v = glm::cross(n, u);

        const glm::vec3 c0 = p.position + (-u - v) * S;
        const glm::vec3 c1 = p.position + (u - v) * S;
        const glm::vec3 c2 = p.position + (u + v) * S;
        const glm::vec3 c3 = p.position + (-u + v) * S;
        const glm::vec3 tip = p.position + n * S;
        const glm::vec3 back = tip - n * (S * 0.18f);

        const glm::vec3 lineCol =
            active ? glm::mix(p.color, glm::vec3(1.0f), 0.4f) : p.color;
        const float w = active ? 2.5f : 1.5f;

        // Translucent fill (two triangles, depth-tested against the clipped scene
        // so you see the plane through the section cut).
        const float fillA = p.enabled ? (active ? 0.22f : 0.12f) : 0.05f;
        const glm::vec3 tris[6] = { c0, c1, c2, c0, c2, c3 };
        list.triangles(tris, 6, glm::vec4(p.color, fillA), OverlayDepth::Occluded);

        auto outline = [&](OverlayDepth depth, float alpha) {
            const glm::vec4 col(lineCol, alpha);
            list.line(c0, c1, col, w, depth);
            list.line(c1, c2, col, w, depth);
            list.line(c2, c3, col, w, depth);
            list.line(c3, c0, col, w, depth);
            list.line(p.position, tip, col, w, depth); // normal shaft
            list.line(tip, back + u * (S * 0.10f), col, w, depth); // arrow fins
            list.line(tip, back - u * (S * 0.10f), col, w, depth);
            list.line(tip, back + v * (S * 0.10f), col, w, depth);
            list.line(tip, back - v * (S * 0.10f), col, w, depth);
        };

        const float lineA = p.enabled ? (active ? 1.0f : 0.7f) : 0.35f;
        outline(OverlayDepth::Occluded, lineA);
        // Improvement over GL: ghost the ACTIVE plane's outline through geometry
        // so the section plane you are editing is always findable.
        if (active)
            outline(OverlayDepth::Hidden, 0.25f);
    }
}

} // namespace Tools
