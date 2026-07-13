#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include "Tools/TransformGizmo.h"

#include "Renderer/OverlayDrawList.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>     // glm::rotation, glm::toMat4
#include <glm/gtx/euler_angles.hpp>   // eulerAngleXYZ, extractEulerAngleXYZ

#include <algorithm>
#include <cmath>
#include <limits>

namespace Tools {

using renderer::OverlayDepth;
using renderer::OverlayDrawList;

// ── Geometry constants (fractions of the screen-constant gizmo size) ────────
namespace {
constexpr float SHAFT_LEN   = 0.80f;
constexpr float TIP_LEN     = 0.18f;
constexpr float TIP_R       = 0.055f;
constexpr float AXIS_PICK_R = 0.10f;  // forgiving radial pick distance
constexpr float AXIS_MIN_T  = 0.13f;  // ignore picks near the centre handle
constexpr float PLANE_OFF   = 0.34f;
constexpr float PLANE_HALF  = 0.12f;
constexpr float RING_R      = 0.92f;
constexpr float RING_PICK   = 0.07f;
constexpr float SCALE_TIP   = 0.085f; // cube half-extent at shaft end
constexpr float CENTER_HALF = 0.11f;

// ── Magnetic help zone (updateHover) ───────────────────────────────────────
// Gizmo handles are only a few pixels wide on screen, which makes them fiddly
// to hit. SNAP_ZONE widens every handle's pick tolerance; inside that slack the
// handle still highlights and the 3D cursor snaps onto it. The gizmo holds a
// constant apparent size (gizmoScale), so 1.0 unit ~= 18% of the viewport
// half-height: on a 1000px-tall view the strict axis tolerance (0.10) is ~9px
// and the help zone stretches it to ~22px.
constexpr float SNAP_ZONE = 2.5f;
// Deadband, in the same units (~3px), for engaging/releasing the magnet. Small
// enough to feel immediate, large enough that hand jitter can't flicker it.
constexpr float SNAP_HYSTERESIS = 0.03f;

const glm::vec3 kAxisColor[3] = {
    glm::vec3(0.91f, 0.27f, 0.30f),   // X red
    glm::vec3(0.45f, 0.82f, 0.27f),   // Y green
    glm::vec3(0.27f, 0.50f, 0.95f),   // Z blue
};
const glm::vec3 kHighlight(1.0f, 0.85f, 0.16f);
const glm::vec3 kCenterColor(0.85f, 0.85f, 0.88f);

// Closest points between a ray (o,d) and an infinite line (p,a). Returns the
// ray parameter and line parameter; `a` and `d` are assumed unit-length.
void closestRayLine(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p,
                    const glm::vec3& a, float& sRay, float& tLine) {
    glm::vec3 r = o - p;
    float dd = glm::dot(d, d);
    float aa = glm::dot(a, a);
    float da = glm::dot(d, a);
    float dr = glm::dot(d, r);
    float ar = glm::dot(a, r);
    float denom = dd * aa - da * da;
    if (std::fabs(denom) < 1e-7f) { sRay = 0.0f; tLine = 0.0f; return; }
    sRay  = (da * ar - aa * dr) / denom;
    tLine = (dd * ar - da * dr) / denom;
}

// Ray/plane intersection. Returns false when (near) parallel or behind.
bool rayPlane(const glm::vec3& o, const glm::vec3& d, const glm::vec3& p,
              const glm::vec3& n, glm::vec3& hit, float& t) {
    float denom = glm::dot(d, n);
    if (std::fabs(denom) < 1e-6f) return false;
    t = glm::dot(p - o, n) / denom;
    if (t <= 0.0f) return false;
    hit = o + d * t;
    return true;
}

// Two unit vectors orthogonal to n (for ring / cone tangent space).
void planeBasis(const glm::vec3& n, glm::vec3& u, glm::vec3& v) {
    glm::vec3 ref = (std::fabs(n.x) < 0.9f) ? glm::vec3(1, 0, 0)
                                            : glm::vec3(0, 1, 0);
    u = glm::normalize(glm::cross(ref, n));
    v = glm::normalize(glm::cross(n, u));
}

// ── Overlay geometry emitters (world-space, appended as triangles) ──────────
void appendCube(OverlayDrawList& list, const glm::vec3& c, float half,
                const glm::vec4& color, OverlayDepth depth) {
    const glm::vec3 v[8] = {
        c + glm::vec3(-half, -half, -half), c + glm::vec3(half, -half, -half),
        c + glm::vec3(half, half, -half),   c + glm::vec3(-half, half, -half),
        c + glm::vec3(-half, -half, half),  c + glm::vec3(half, -half, half),
        c + glm::vec3(half, half, half),    c + glm::vec3(-half, half, half)};
    const int f[6][4] = {{0, 1, 2, 3}, {5, 4, 7, 6}, {4, 0, 3, 7},
                         {1, 5, 6, 2}, {3, 2, 6, 7}, {4, 5, 1, 0}};
    glm::vec3 tris[36];
    int n = 0;
    for (auto& q : f) {
        tris[n++] = v[q[0]]; tris[n++] = v[q[1]]; tris[n++] = v[q[2]];
        tris[n++] = v[q[0]]; tris[n++] = v[q[2]]; tris[n++] = v[q[3]];
    }
    list.triangles(tris, 36, color, depth);
}

void appendCone(OverlayDrawList& list, const glm::vec3& base, const glm::vec3& axis,
                float r, float len, const glm::vec4& color, OverlayDepth depth) {
    glm::vec3 u, v;
    planeBasis(axis, u, v);
    const glm::vec3 apex = base + axis * len;
    const int SEG = 16;
    glm::vec3 tris[SEG * 6];
    int n = 0;
    for (int i = 0; i < SEG; ++i) {
        float a0 = glm::two_pi<float>() * i / SEG;
        float a1 = glm::two_pi<float>() * (i + 1) / SEG;
        glm::vec3 b0 = base + (std::cos(a0) * u + std::sin(a0) * v) * r;
        glm::vec3 b1 = base + (std::cos(a1) * u + std::sin(a1) * v) * r;
        tris[n++] = b0; tris[n++] = b1; tris[n++] = apex;  // side
        tris[n++] = base; tris[n++] = b1; tris[n++] = b0;  // base cap
    }
    list.triangles(tris, SEG * 6, color, depth);
}

void appendQuad(OverlayDrawList& list, const glm::vec3& center, const glm::vec3& u,
                const glm::vec3& v, float half, const glm::vec4& color,
                OverlayDepth depth) {
    const glm::vec3 a = center + (-u - v) * half;
    const glm::vec3 b = center + (u - v) * half;
    const glm::vec3 c = center + (u + v) * half;
    const glm::vec3 d = center + (-u + v) * half;
    const glm::vec3 tris[6] = {a, b, c, a, c, d};
    list.triangles(tris, 6, color, depth);
}
} // namespace

// ────────────────────────────────────────────────────────────────────────────
// Target binding & configuration
// ────────────────────────────────────────────────────────────────────────────
void TransformGizmo::setTarget(glm::vec3* position, glm::vec3* rotationEulerDeg,
                               glm::vec3* scale) {
    m_position = position;
    m_rotation = rotationEulerDeg;
    m_scale = scale;
}

void TransformGizmo::clearTarget() {
    if (isDragging()) return; // don't drop a live drag
    m_position = nullptr;
    m_rotation = nullptr;
    m_scale = nullptr;
    m_hover = Handle::None;
    m_hasInteractionPoint = false;
    m_interactionLatched = false;
    resetSnap();
}

void TransformGizmo::setMode(Mode m) {
    m_mode = m;
    m_interactionLatched = false;
    resetSnap(); // different handles under the cursor: re-arm the magnet
}

void TransformGizmo::cycleMode() {
    m_mode = (m_mode == Mode::Translate) ? Mode::Rotate
           : (m_mode == Mode::Rotate)    ? Mode::Scale
                                         : Mode::Translate;
    m_interactionLatched = false;
    resetSnap();
}

TransformGizmo::Mode TransformGizmo::effectiveMode() const {
    if (m_mode == Mode::Rotate && !canRotate()) return Mode::Translate;
    if (m_mode == Mode::Scale && !canScale())   return Mode::Translate;
    return m_mode;
}

glm::mat3 TransformGizmo::rotationMatrix() const {
    if (!m_rotation) return glm::mat3(1.0f);
    return glm::mat3(glm::eulerAngleXYZ(glm::radians(m_rotation->x),
                                        glm::radians(m_rotation->y),
                                        glm::radians(m_rotation->z)));
}

void TransformGizmo::computeAxes(glm::vec3 outAxes[3], Mode forMode) const {
    bool local = (forMode == Mode::Scale) ||
                 (space == Space::Local && m_rotation != nullptr);
    if (local) {
        glm::mat3 R = rotationMatrix();
        outAxes[0] = glm::normalize(R[0]);
        outAxes[1] = glm::normalize(R[1]);
        outAxes[2] = glm::normalize(R[2]);
    } else {
        outAxes[0] = glm::vec3(1, 0, 0);
        outAxes[1] = glm::vec3(0, 1, 0);
        outAxes[2] = glm::vec3(0, 0, 1);
    }
}

float TransformGizmo::gizmoScale(const glm::vec3& cameraPos) const {
    if (!m_position) return 1.0f;
    float dist = glm::distance(cameraPos, *m_position);
    // Constant apparent size: a fixed fraction of the viewport half-height
    // projected back into world units at the pivot distance, FOV-folded so a
    // wider FOV doesn't balloon the gizmo.
    return std::max(dist * m_tanHalfFov * screenSize, 1e-3f);
}

// ────────────────────────────────────────────────────────────────────────────
// Hit testing
//
// pickHandle() is the single source of truth: it measures the distance from the
// ray to every handle of the current mode, normalised by the gizmo's world scale
// (so the numbers read as screen-space and behave identically at any zoom), and
// widens each handle's tolerance by `tolScale`.
//
// Selection deliberately has two tiers. A STRICT hit (inside the real geometry
// tolerance) wins on DEPTH, so an axis passing in front of a plane quad still
// picks the axis exactly as before. Only when nothing is strictly hit does the
// help zone speak, and there the NEAREST handle wins — that is what "magnetic"
// means: from empty space, the thing you were clearly aiming at.
// ────────────────────────────────────────────────────────────────────────────
bool TransformGizmo::pickHandle(const glm::vec3& o, const glm::vec3& d,
                                const glm::vec3& cameraPos, float tolScale,
                                Pick& out) const {
    if (!enabled || !hasTarget()) return false;

    const glm::vec3 pivot = *m_position;
    const float s = gizmoScale(cameraPos);
    const Mode m = effectiveMode();
    glm::vec3 axes[3];
    computeAxes(axes, m);

    // Absolute slack (in gizmo units) the help zone adds around every handle.
    // Applied as a PAD rather than a multiplier so fat handles (plane quads) and
    // thin ones (axis shafts, rings) all gain the same reach — a multiplier
    // would make the already-easy targets balloon and swallow the hard ones.
    const float pad = (tolScale - 1.0f) * AXIS_PICK_R;

    Pick best;
    bool have = false;
    // Strict hits beat help-zone hits outright; among equals, strict compares by
    // depth (front-most) and help-zone compares by distance (nearest).
    auto consider = [&](Handle h, float depth, float dist, bool exact,
                        const glm::vec3& point) {
        if (depth <= 0.0f) return;
        // With no pad (tolScale == 1) there IS no help zone: only strict hits
        // count, so hitTest() stays byte-for-byte the query it always was.
        if (!exact && pad <= 0.0f) return;
        Pick c;
        c.handle = h; c.depth = depth; c.dist = dist; c.exact = exact;
        c.point = point;
        if (!have) { best = c; have = true; return; }
        if (best.exact != c.exact) {           // strict always outranks help-zone
            if (c.exact) best = c;
            return;
        }
        if (c.exact ? (c.depth < best.depth) : (c.dist < best.dist))
            best = c;
    };

    if (m == Mode::Rotate) {
        const Handle ringH[3] = {Handle::RingX, Handle::RingY, Handle::RingZ};
        for (int i = 0; i < 3; ++i) {
            glm::vec3 hit; float t;
            if (!rayPlane(o, d, pivot, axes[i], hit, t)) continue;
            const float radial = glm::length(hit - pivot);
            // Distance to the ring's circle, not to its plane.
            const float dist = std::fabs(radial - RING_R * s) / s;
            if (dist >= RING_PICK + pad) continue;
            const glm::vec3 onRing =
                (radial > 1e-5f) ? pivot + (hit - pivot) * (RING_R * s / radial)
                                 : hit;
            consider(ringH[i], t, dist, dist < RING_PICK, onRing);
        }
        if (!have) return false;
        out = best;
        return true;
    }

    // Translate & Scale share single-axis shafts.
    const Handle axisH[3] = {Handle::AxisX, Handle::AxisY, Handle::AxisZ};
    const float axisLen = (m == Mode::Scale ? SHAFT_LEN + SCALE_TIP
                                            : SHAFT_LEN + TIP_LEN) * s;
    for (int i = 0; i < 3; ++i) {
        float sRay, tLine;
        closestRayLine(o, d, pivot, axes[i], sRay, tLine);
        if (sRay <= 0.0f) continue;
        // Clamp along the shaft so the magnet still reaches a cursor sitting just
        // off either END of the axis, instead of only off its side.
        const float tClamped = glm::clamp(tLine, AXIS_MIN_T * s, axisLen);
        const glm::vec3 pAxis = pivot + axes[i] * tClamped;
        const glm::vec3 pRay = o + d * sRay;
        const float dist = glm::length(pRay - pAxis) / s;
        if (dist >= AXIS_PICK_R + pad) continue;
        const bool exact = dist < AXIS_PICK_R && tLine >= AXIS_MIN_T * s &&
                           tLine <= axisLen;
        consider(axisH[i], sRay, dist, exact, pAxis);
    }

    if (m == Mode::Translate) {
        // Two-axis plane quads.
        const Handle planeH[3] = {Handle::PlaneYZ, Handle::PlaneZX, Handle::PlaneXY};
        const int ai[3] = {1, 2, 0};
        const int aj[3] = {2, 0, 1};
        const int nk[3] = {0, 1, 2};
        for (int k = 0; k < 3; ++k) {
            glm::vec3 hit; float t;
            if (!rayPlane(o, d, pivot, axes[nk[k]], hit, t)) continue;
            const float u = glm::dot(hit - pivot, axes[ai[k]]);
            const float v = glm::dot(hit - pivot, axes[aj[k]]);
            const float lo = (PLANE_OFF - PLANE_HALF) * s;
            const float hi = (PLANE_OFF + PLANE_HALF) * s;
            // Distance to the quad in its own plane (zero inside it).
            const float du = std::max(0.0f, std::max(lo - u, u - hi));
            const float dv = std::max(0.0f, std::max(lo - v, v - hi));
            const bool inside = (du == 0.0f && dv == 0.0f);
            const float dist = std::sqrt(du * du + dv * dv) / s;
            if (!inside && dist >= pad) continue;
            const glm::vec3 onQuad = pivot +
                                     axes[ai[k]] * glm::clamp(u, lo, hi) +
                                     axes[aj[k]] * glm::clamp(v, lo, hi);
            consider(planeH[k], t, dist, inside, onQuad);
        }
    }

    // Centre handle (sphere).
    {
        const Handle h = (m == Mode::Scale)     ? Handle::ScaleUniform
                         : (m == Mode::Translate) ? Handle::ScreenMove
                                                  : Handle::None;
        const float r = CENTER_HALF * 1.35f * s;
        // Closest approach of the ray to the pivot gives both the surface
        // distance (for the help zone) and, when it hits, the sphere's front
        // intersection — which is the depth the strict pick must order by, so a
        // handle in front of the centre box still wins as it always did.
        const float tc = glm::dot(pivot - o, d);
        if (h != Handle::None && tc > 0.0f) {
            const float perp = glm::length((o + d * tc) - pivot);
            const bool exact = perp <= r;
            const float dist = std::max(0.0f, perp - r) / s;
            if (exact || dist < pad) {
                const float depth =
                    exact ? tc - std::sqrt(std::max(0.0f, r * r - perp * perp))
                          : tc;
                consider(h, depth, dist, exact, pivot);
            }
        }
    }

    if (!have) return false;
    out = best;
    return true;
}

TransformGizmo::Handle
TransformGizmo::hitTest(const glm::vec3& o, const glm::vec3& d,
                        const glm::vec3& cameraPos,
                        glm::vec3* outHitPoint) const {
    Pick p;
    if (!pickHandle(o, d, cameraPos, 1.0f, p)) return Handle::None;
    if (outHitPoint) *outHitPoint = p.point;
    return p.handle;
}

void TransformGizmo::resetSnap() {
    m_snapHandle = Handle::None;
    m_snapTracked = Handle::None;
    m_snapBlocked = false;
    m_snapMinDist = 0.0f;
    m_snapMaxDist = 0.0f;
}

TransformGizmo::Handle
TransformGizmo::updateHover(const glm::vec3& o, const glm::vec3& d,
                            const glm::vec3& cameraPos) {
    if (isDragging()) return m_activeHandle;

    auto noHover = [&]() {
        m_hover = Handle::None;
        if (!m_interactionLatched) m_hasInteractionPoint = false;
        return Handle::None;
    };
    auto hoverAt = [&](Handle h, const glm::vec3& point) {
        m_hover = h;
        m_interactionPoint = point;   // ON the handle: the 3D cursor snaps to it
        m_hasInteractionPoint = true;
        m_interactionLatched = false;
        return h;
    };

    Pick p;
    if (!pickHandle(o, d, cameraPos, SNAP_ZONE, p)) {
        resetSnap(); // left the help zone entirely: the magnet re-arms
        return noHover();
    }

    if (p.exact) {
        // Genuinely on the handle — no magnet needed, and being here re-arms it.
        m_snapHandle = p.handle;
        m_snapTracked = p.handle;
        m_snapBlocked = false;
        m_snapMinDist = p.dist;
        return hoverAt(p.handle, p.point);
    }

    // Inside the help zone but not on the handle: the magnet decides.
    if (p.handle != m_snapTracked) {
        // Just entered THIS handle's zone — entering is by definition an
        // approach, so snap straight away.
        m_snapTracked = p.handle;
        m_snapBlocked = false;
        m_snapHandle = p.handle;
        m_snapMinDist = p.dist;
    } else if (m_snapBlocked) {
        // The user pulled away from this handle. Stay off it until they clearly
        // move back TOWARD it — otherwise the magnet fights every attempt to
        // leave, which is the whole annoyance we're avoiding.
        m_snapMaxDist = std::max(m_snapMaxDist, p.dist);
        if (p.dist < m_snapMaxDist - SNAP_HYSTERESIS) {
            m_snapBlocked = false;
            m_snapHandle = p.handle;
            m_snapMinDist = p.dist;
        }
    } else if (m_snapHandle == p.handle) {
        // Held by the magnet. Track the closest approach and let go only once
        // the cursor has retreated measurably from it (the deadband keeps hand
        // jitter from flickering the snap on and off).
        m_snapMinDist = std::min(m_snapMinDist, p.dist);
        if (p.dist > m_snapMinDist + SNAP_HYSTERESIS) {
            m_snapHandle = Handle::None;
            m_snapBlocked = true;
            m_snapMaxDist = p.dist;
        }
    }

    if (m_snapHandle != Handle::None)
        return hoverAt(m_snapHandle, p.point);
    return noHover();
}

// ────────────────────────────────────────────────────────────────────────────
// Drag lifecycle
// ────────────────────────────────────────────────────────────────────────────
bool TransformGizmo::beginDrag(Handle handle, const glm::vec3& o,
                               const glm::vec3& d, const glm::vec3& cameraPos) {
    if (!hasTarget() || handle == Handle::None) return false;

    const glm::vec3 pivot = *m_position;
    m_startPos = pivot;
    m_startScale = m_scale ? *m_scale : glm::vec3(1.0f);
    m_startRot = rotationMatrix();
    m_gizmoScaleAtDrag = gizmoScale(cameraPos);
    const Mode m = effectiveMode();
    glm::vec3 axes[3];
    computeAxes(axes, m);

    auto axisIndex = [](Handle h) {
        switch (h) {
        case Handle::AxisX: case Handle::RingX: return 0;
        case Handle::AxisY: case Handle::RingY: return 1;
        case Handle::AxisZ: case Handle::RingZ: return 2;
        default: return -1;
        }
    };

    switch (handle) {
    case Handle::AxisX: case Handle::AxisY: case Handle::AxisZ: {
        int idx = axisIndex(handle);
        m_dragAxisIndex = idx;
        m_dragAxis = axes[idx];
        float sRay, tLine;
        closestRayLine(o, d, pivot, m_dragAxis, sRay, tLine);
        m_startProj = tLine;
        break;
    }
    case Handle::PlaneYZ: case Handle::PlaneZX: case Handle::PlaneXY: {
        int nk = (handle == Handle::PlaneYZ) ? 0
               : (handle == Handle::PlaneZX) ? 1 : 2;
        m_dragPlaneNormal = axes[nk];
        float t;
        if (!rayPlane(o, d, pivot, m_dragPlaneNormal, m_dragStartHit, t))
            m_dragStartHit = pivot;
        break;
    }
    case Handle::RingX: case Handle::RingY: case Handle::RingZ: {
        int idx = axisIndex(handle);
        m_dragAxis = axes[idx];
        planeBasis(m_dragAxis, m_planeU, m_planeV);
        glm::vec3 hit; float t;
        if (rayPlane(o, d, pivot, m_dragAxis, hit, t)) {
            glm::vec3 v = glm::normalize(hit - pivot);
            m_startAngle = std::atan2(glm::dot(v, m_planeV), glm::dot(v, m_planeU));
        } else {
            m_startAngle = 0.0f;
        }
        m_lastAngle = m_startAngle;
        m_turnCount = 0;
        break;
    }
    case Handle::ScreenMove: {
        m_dragPlaneNormal = glm::normalize(cameraPos - pivot);
        float t;
        if (!rayPlane(o, d, pivot, m_dragPlaneNormal, m_dragStartHit, t))
            m_dragStartHit = pivot;
        break;
    }
    case Handle::ScaleUniform: {
        m_dragPlaneNormal = glm::normalize(cameraPos - pivot);
        glm::vec3 hit; float t;
        m_startRadius =
            rayPlane(o, d, pivot, m_dragPlaneNormal, hit, t)
                ? glm::length(hit - pivot)
                : 0.0f;
        break;
    }
    default:
        return false;
    }

    m_activeHandle = handle;
    m_interactionPoint = pivot;
    m_hasInteractionPoint = true;
    m_interactionLatched = false;
    return true;
}

void TransformGizmo::updateDrag(const glm::vec3& o, const glm::vec3& d,
                                const glm::vec3& cameraPos, bool snapHeld) {
    if (!isDragging() || !hasTarget()) return;
    const bool snap = snapEnabled || snapHeld;
    const glm::vec3 pivot = m_startPos;

    switch (m_activeHandle) {
    case Handle::AxisX: case Handle::AxisY: case Handle::AxisZ: {
        float sRay, tLine;
        closestRayLine(o, d, pivot, m_dragAxis, sRay, tLine);
        // Keep the 3D cursor ON the axis (not out at the ray, which sits wherever
        // the mouse drifted perpendicular to it). Matches the snapped hover, so
        // engaging the drag doesn't pop the cursor off the line.
        if (sRay > 0.0f) m_interactionPoint = pivot + m_dragAxis * tLine;
        float delta = tLine - m_startProj;
        if (effectiveMode() == Mode::Scale && m_scale) {
            float factor = 1.0f + delta / m_gizmoScaleAtDrag;
            factor = std::max(factor, 0.01f);
            float val = m_startScale[m_dragAxisIndex] * factor;
            if (snap) val = std::max(snapScale,
                                     std::round(val / snapScale) * snapScale);
            (*m_scale)[m_dragAxisIndex] = val;
        } else {
            if (snap) delta = std::round(delta / snapTranslate) * snapTranslate;
            *m_position = m_startPos + m_dragAxis * delta;
        }
        break;
    }
    case Handle::PlaneYZ: case Handle::PlaneZX: case Handle::PlaneXY:
    case Handle::ScreenMove: {
        glm::vec3 hit; float t;
        if (rayPlane(o, d, pivot, m_dragPlaneNormal, hit, t)) {
            m_interactionPoint = hit;
            glm::vec3 delta = hit - m_dragStartHit;
            if (snap) {
                delta.x = std::round(delta.x / snapTranslate) * snapTranslate;
                delta.y = std::round(delta.y / snapTranslate) * snapTranslate;
                delta.z = std::round(delta.z / snapTranslate) * snapTranslate;
            }
            *m_position = m_startPos + delta;
        }
        break;
    }
    case Handle::RingX: case Handle::RingY: case Handle::RingZ: {
        if (!m_rotation) break;
        glm::vec3 hit; float t;
        if (!rayPlane(o, d, pivot, m_dragAxis, hit, t)) break;
        {
            glm::vec3 radial = hit - pivot;
            float rl = glm::length(radial);
            m_interactionPoint = (rl > 1e-5f)
                ? pivot + radial * (RING_R * m_gizmoScaleAtDrag / rl)
                : hit;
        }
        glm::vec3 v = glm::normalize(hit - pivot);
        float ang = std::atan2(glm::dot(v, m_planeV), glm::dot(v, m_planeU));
        if (ang - m_lastAngle > glm::pi<float>()) m_turnCount--;
        else if (ang - m_lastAngle < -glm::pi<float>()) m_turnCount++;
        m_lastAngle = ang;
        float delta = (ang + m_turnCount * glm::two_pi<float>()) - m_startAngle;
        if (snap) {
            float step = glm::radians(snapRotateDeg);
            delta = std::round(delta / step) * step;
        }
        glm::mat3 deltaRot =
            glm::mat3(glm::rotate(glm::mat4(1.0f), delta, m_dragAxis));
        glm::mat3 R = deltaRot * m_startRot;
        float ex, ey, ez;
        glm::extractEulerAngleXYZ(glm::mat4(R), ex, ey, ez);
        *m_rotation = glm::degrees(glm::vec3(ex, ey, ez));
        break;
    }
    case Handle::ScaleUniform: {
        if (!m_scale) break;
        glm::vec3 hit; float t;
        if (rayPlane(o, d, pivot, m_dragPlaneNormal, hit, t)) {
            m_interactionPoint = hit;
            float len = glm::length(hit - pivot);
            float factor = std::max(
                0.01f, 1.0f + (len - m_startRadius) / m_gizmoScaleAtDrag);
            glm::vec3 ns = m_startScale * factor;
            if (snap) {
                ns.x = std::max(snapScale, std::round(ns.x / snapScale) * snapScale);
                ns.y = std::max(snapScale, std::round(ns.y / snapScale) * snapScale);
                ns.z = std::max(snapScale, std::round(ns.z / snapScale) * snapScale);
            }
            *m_scale = ns;
        }
        break;
    }
    default:
        break;
    }
}

void TransformGizmo::endDrag() {
    m_activeHandle = Handle::None;
    m_dragAxisIndex = -1;
    // Latch the final interaction point so a co-located 3D cursor stays where
    // the drag finished instead of snapping back to scene depth on mouse-up.
    m_interactionLatched = true;
    // Re-arm the magnet: the next hover re-decides from a clean slate rather
    // than inheriting approach state from before the drag.
    resetSnap();
}

// ────────────────────────────────────────────────────────────────────────────
// Rendering — describe the gizmo into the shared overlay list
// ────────────────────────────────────────────────────────────────────────────
void TransformGizmo::appendTo(OverlayDrawList& list, const glm::mat4& projection,
                              const glm::vec3& cameraPos) {
    if (!enabled || !hasTarget()) return;

    // Cache the vertical FOV so gizmoScale() keeps a constant on-screen size.
    if (projection[1][1] != 0.0f)
        m_tanHalfFov = std::clamp(1.0f / std::fabs(projection[1][1]), 0.05f, 3.0f);

    const glm::vec3 pivot = *m_position;
    const float s = gizmoScale(cameraPos);
    const Mode m = effectiveMode();
    glm::vec3 axes[3];
    computeAxes(axes, m);

    // The overlay carries no depth of its own; draw always-on-top and paint the
    // hot handle LAST within its kind so it never hides behind a sibling.
    const OverlayDepth D = OverlayDepth::Always;
    const bool engaged =
        (m_activeHandle != Handle::None) || (m_hover != Handle::None);
    auto isHot = [&](Handle h) {
        return (m_activeHandle == h) ||
               (m_activeHandle == Handle::None && m_hover == h);
    };
    auto solidColor = [&](Handle h, const glm::vec3& axisCol) -> glm::vec3 {
        if (isHot(h)) return kHighlight;
        return engaged ? axisCol * 0.55f : axisCol;
    };

    const Handle axisH[3] = {Handle::AxisX, Handle::AxisY, Handle::AxisZ};
    const Handle ringH[3] = {Handle::RingX, Handle::RingY, Handle::RingZ};

    if (m == Mode::Rotate) {
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < 3; ++i) {
                const bool hot = isHot(ringH[i]);
                if ((pass == 0) == hot) continue; // pass 0 = non-hot, pass 1 = hot
                glm::vec3 u, v;
                planeBasis(axes[i], u, v);
                constexpr int RSEG = 64;
                glm::vec3 pts[RSEG];
                for (int k = 0; k < RSEG; ++k) {
                    float a = glm::two_pi<float>() * k / RSEG;
                    pts[k] = pivot + (std::cos(a) * u + std::sin(a) * v) * (RING_R * s);
                }
                list.polyline(pts, RSEG, true,
                              glm::vec4(solidColor(ringH[i], kAxisColor[i]), 1.0f),
                              hot ? 5.0f : 3.0f, D);
            }
    } else {
        // Axis shafts (crisp lines) + tips (translate cones / scale cubes).
        for (int pass = 0; pass < 2; ++pass)
            for (int i = 0; i < 3; ++i) {
                const bool hot = isHot(axisH[i]);
                if ((pass == 0) == hot) continue;
                const glm::vec4 col(solidColor(axisH[i], kAxisColor[i]), 1.0f);
                const glm::vec3 tip = pivot + axes[i] * (SHAFT_LEN * s);
                list.line(pivot, tip, col, hot ? 5.0f : 3.0f, D);
                if (m == Mode::Scale) {
                    float k = SCALE_TIP * 2.0f * (hot ? 1.4f : 1.0f) * s;
                    appendCube(list, tip, k * 0.5f, col, D);
                } else {
                    float tr = TIP_R * (hot ? 1.35f : 1.0f) * s;
                    float tl = TIP_LEN * (hot ? 1.15f : 1.0f) * s;
                    appendCone(list, tip, axes[i], tr, tl, col, D);
                }
            }

        if (m == Mode::Translate) {
            const Handle planeH[3] = {Handle::PlaneYZ, Handle::PlaneZX, Handle::PlaneXY};
            const int ai[3] = {1, 2, 0}, aj[3] = {2, 0, 1}, nk[3] = {0, 1, 2};
            for (int k = 0; k < 3; ++k) {
                const bool hot = isHot(planeH[k]);
                const glm::vec3 center =
                    pivot + (axes[ai[k]] + axes[aj[k]]) * (PLANE_OFF * s);
                const glm::vec3 col = hot ? kHighlight : kAxisColor[nk[k]];
                const float alpha = hot ? 0.6f : (engaged ? 0.16f : 0.32f);
                appendQuad(list, center, axes[ai[k]], axes[aj[k]], PLANE_HALF * s,
                           glm::vec4(col, alpha), D);
            }
        }

        // Centre handle cube (uniform scale / screen-plane move).
        const Handle centreH =
            (m == Mode::Scale) ? Handle::ScaleUniform : Handle::ScreenMove;
        const bool hotC = isHot(centreH);
        const float ch = CENTER_HALF * 2.0f * (hotC ? 1.25f : 1.0f) * s;
        const glm::vec3 cc =
            hotC ? kHighlight : (engaged ? kCenterColor * 0.55f : kCenterColor);
        appendCube(list, pivot, ch * 0.5f, glm::vec4(cc, 1.0f), D);
    }

    // Cursor marker: camera-facing stacked discs pinned to the engaged handle
    // point (dark halo → highlight ring → white core), painted above everything.
    if (m_hasInteractionPoint) {
        list.disc(m_interactionPoint, 0.085f * s, glm::vec4(0.04f, 0.04f, 0.05f, 0.9f), D);
        list.disc(m_interactionPoint, 0.065f * s, glm::vec4(kHighlight, 1.0f), D);
        list.disc(m_interactionPoint, 0.038f * s, glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), D);
    }
}

} // namespace Tools
