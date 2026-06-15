#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef GLM_ENABLE_EXPERIMENTAL
#define GLM_ENABLE_EXPERIMENTAL
#endif
#include "Tools/TransformGizmo.h"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>     // glm::rotation, glm::toMat4
#include <glm/gtx/euler_angles.hpp>   // eulerAngleXYZ, extractEulerAngleXYZ

#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>
#include <vector>

namespace Tools {

// ── Geometry constants (fractions of the screen-constant gizmo size) ────────
namespace {
constexpr float SHAFT_LEN   = 0.80f;
constexpr float SHAFT_R     = 0.018f;
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

// Two unit vectors orthogonal to n (for ring tangent space).
void planeBasis(const glm::vec3& n, glm::vec3& u, glm::vec3& v) {
    glm::vec3 ref = (std::fabs(n.x) < 0.9f) ? glm::vec3(1, 0, 0)
                                            : glm::vec3(0, 1, 0);
    u = glm::normalize(glm::cross(ref, n));
    v = glm::normalize(glm::cross(n, u));
}

unsigned int compileShader(unsigned int type, const char* src) {
    unsigned int s = glCreateShader(type);
    glShaderSource(s, 1, &src, nullptr);
    glCompileShader(s);
    int ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[512];
        glGetShaderInfoLog(s, 512, nullptr, log);
        std::cerr << "[TransformGizmo] shader compile error: " << log << std::endl;
    }
    return s;
}
} // namespace

// ── Embedded overlay shader ─────────────────────────────────────────────────
static const char* kVertSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
uniform mat4 uMVP;
void main() { gl_Position = uMVP * vec4(aPos, 1.0); }
)";

static const char* kFragSrc = R"(
#version 330 core
out vec4 FragColor;
uniform vec3 uColor;
uniform float uAlpha;
void main() { FragColor = vec4(uColor, uAlpha); }
)";

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
}

void TransformGizmo::setMode(Mode m) { m_mode = m; }

void TransformGizmo::cycleMode() {
    m_mode = (m_mode == Mode::Translate) ? Mode::Rotate
           : (m_mode == Mode::Rotate)    ? Mode::Scale
                                         : Mode::Translate;
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
    // Constant apparent size: project a fixed fraction of the viewport
    // half-height back into world units at the pivot distance. Folding in
    // tan(fovY/2) makes the on-screen size field-of-view-independent so a wider
    // FOV no longer balloons the gizmo. The result is the gizmo's outer radius
    // in world units (~= screenSize/2 of the full viewport height on screen).
    return std::max(dist * m_tanHalfFov * screenSize, 1e-3f);
}

// ────────────────────────────────────────────────────────────────────────────
// Hit testing
// ────────────────────────────────────────────────────────────────────────────
TransformGizmo::Handle
TransformGizmo::hitTest(const glm::vec3& o, const glm::vec3& d,
                        const glm::vec3& cameraPos,
                        glm::vec3* outHitPoint) const {
    if (!enabled || !hasTarget()) return Handle::None;

    const glm::vec3 pivot = *m_position;
    const float s = gizmoScale(cameraPos);
    const Mode m = effectiveMode();
    glm::vec3 axes[3];
    computeAxes(axes, m);

    Handle best = Handle::None;
    float bestDepth = std::numeric_limits<float>::max();
    auto consider = [&](Handle h, float depth) {
        if (depth > 0.0f && depth < bestDepth) { bestDepth = depth; best = h; }
    };

    if (m == Mode::Rotate) {
        const Handle ringH[3] = {Handle::RingX, Handle::RingY, Handle::RingZ};
        glm::vec3 bestHit(0.0f);
        for (int i = 0; i < 3; ++i) {
            glm::vec3 hit; float t;
            if (!rayPlane(o, d, pivot, axes[i], hit, t)) continue;
            float radial = glm::length(hit - pivot);
            if (std::fabs(radial - RING_R * s) < RING_PICK * s) {
                consider(ringH[i], t);
                if (best == ringH[i]) {
                    // Snap onto the ring circle (radius RING_R) so the cursor /
                    // marker sit on the visible ring exactly where a drag would
                    // grab. The plane intersection alone can sit off the ring or
                    // jump when the ring is viewed edge-on.
                    bestHit = (radial > 1e-5f)
                                  ? pivot + (hit - pivot) * (RING_R * s / radial)
                                  : hit;
                }
            }
        }
        // This branch returns early, so write the hit point here rather than
        // falling through to the shared assignment below.
        if (outHitPoint && best != Handle::None) *outHitPoint = bestHit;
        return best;
    }

    // Translate & Scale share single-axis shafts.
    const Handle axisH[3] = {Handle::AxisX, Handle::AxisY, Handle::AxisZ};
    const float axisLen = (m == Mode::Scale ? SHAFT_LEN + SCALE_TIP
                                            : SHAFT_LEN + TIP_LEN) * s;
    for (int i = 0; i < 3; ++i) {
        float sRay, tLine;
        closestRayLine(o, d, pivot, axes[i], sRay, tLine);
        if (sRay <= 0.0f) continue;
        if (tLine < AXIS_MIN_T * s || tLine > axisLen) continue;
        glm::vec3 pRay = o + d * sRay;
        glm::vec3 pAxis = pivot + axes[i] * tLine;
        if (glm::length(pRay - pAxis) < AXIS_PICK_R * s) consider(axisH[i], sRay);
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
            float u = glm::dot(hit - pivot, axes[ai[k]]);
            float v = glm::dot(hit - pivot, axes[aj[k]]);
            float lo = (PLANE_OFF - PLANE_HALF) * s;
            float hi = (PLANE_OFF + PLANE_HALF) * s;
            if (u > lo && u < hi && v > lo && v < hi) consider(planeH[k], t);
        }
    }

    // Centre handle (sphere pick).
    {
        glm::vec3 oc = o - pivot;
        float r = CENTER_HALF * 1.35f * s;
        float b = glm::dot(oc, d);
        float c = glm::dot(oc, oc) - r * r;
        float disc = b * b - c;
        if (disc >= 0.0f) {
            float t = -b - std::sqrt(disc);
            if (t > 0.0f) {
                if (m == Mode::Scale) consider(Handle::ScaleUniform, t);
                else if (m == Mode::Translate) consider(Handle::ScreenMove, t);
            }
        }
    }

    // `bestDepth` is the ray parameter (distance along the unit ray) to the
    // picked handle, so the point on the view ray at that depth is where the
    // handle sits under the cursor — exactly the spot to snap the 3D cursor to.
    if (outHitPoint && best != Handle::None)
        *outHitPoint = o + d * bestDepth;

    return best;
}

TransformGizmo::Handle
TransformGizmo::updateHover(const glm::vec3& o, const glm::vec3& d,
                            const glm::vec3& cameraPos) {
    if (isDragging()) return m_activeHandle;
    glm::vec3 hitPoint;
    m_hover = hitTest(o, d, cameraPos, &hitPoint);
    m_hasInteractionPoint = (m_hover != Handle::None);
    if (m_hasInteractionPoint) m_interactionPoint = hitPoint;
    return m_hover;
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
        // Store the start distance from the pivot; the update uses an additive
        // factor so the response is stable even when the grab starts near the
        // centre (no division by a tiny radius).
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
    // Seed the interaction point so the 3D cursor snaps to the handle on the
    // very first frame; updateDrag refines it to the live grab point below.
    m_interactionPoint = pivot;
    m_hasInteractionPoint = true;
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
        if (sRay > 0.0f) m_interactionPoint = o + d * sRay;
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
        // Snap the cursor onto the ring itself (clamp the in-plane hit to the
        // ring radius) rather than the unbounded plane intersection.
        {
            glm::vec3 radial = hit - pivot;
            float rl = glm::length(radial);
            m_interactionPoint = (rl > 1e-5f)
                ? pivot + radial * (RING_R * m_gizmoScaleAtDrag / rl)
                : hit;
        }
        glm::vec3 v = glm::normalize(hit - pivot);
        float ang = std::atan2(glm::dot(v, m_planeV), glm::dot(v, m_planeU));
        // Track full turns for continuous multi-revolution dragging.
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
    // Drop the snap so the 3D cursor falls back to scene depth until the next
    // hover re-establishes a handle.
    m_hasInteractionPoint = false;
}

// ────────────────────────────────────────────────────────────────────────────
// Rendering
// ────────────────────────────────────────────────────────────────────────────
void TransformGizmo::ensureInit() {
    if (m_initialized) return;
    m_initialized = true;

    // Program.
    unsigned int vs = compileShader(GL_VERTEX_SHADER, kVertSrc);
    unsigned int fs = compileShader(GL_FRAGMENT_SHADER, kFragSrc);
    m_program = glCreateProgram();
    glAttachShader(m_program, vs);
    glAttachShader(m_program, fs);
    glLinkProgram(m_program);
    glDeleteShader(vs);
    glDeleteShader(fs);
    m_uMVP = glGetUniformLocation(m_program, "uMVP");
    m_uColor = glGetUniformLocation(m_program, "uColor");
    m_uAlpha = glGetUniformLocation(m_program, "uAlpha");

    auto upload = [](unsigned int& vao, unsigned int& vbo,
                     const std::vector<float>& verts) {
        glGenVertexArrays(1, &vao);
        glGenBuffers(1, &vbo);
        glBindVertexArray(vao);
        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float),
                     verts.data(), GL_STATIC_DRAW);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float),
                              (void*)0);
        glEnableVertexAttribArray(0);
        glBindVertexArray(0);
    };

    const int SEG = 20;
    const float TAU = glm::two_pi<float>();

    // Cylinder +Y, radius 1, y in [0,1] (side only).
    {
        std::vector<float> v;
        for (int i = 0; i < SEG; ++i) {
            float a0 = TAU * i / SEG, a1 = TAU * (i + 1) / SEG;
            glm::vec3 b0(std::cos(a0), 0, std::sin(a0));
            glm::vec3 b1(std::cos(a1), 0, std::sin(a1));
            glm::vec3 t0 = b0 + glm::vec3(0, 1, 0);
            glm::vec3 t1 = b1 + glm::vec3(0, 1, 0);
            glm::vec3 tri[6] = {b0, b1, t1, b0, t1, t0};
            for (auto& p : tri) { v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); }
        }
        m_cylCount = (int)v.size() / 3;
        upload(m_cylVAO, m_cylVBO, v);
    }
    // Cone +Y, base radius 1 at y=0, apex y=1 (+ base cap).
    {
        std::vector<float> v;
        glm::vec3 apex(0, 1, 0), center(0, 0, 0);
        for (int i = 0; i < SEG; ++i) {
            float a0 = TAU * i / SEG, a1 = TAU * (i + 1) / SEG;
            glm::vec3 b0(std::cos(a0), 0, std::sin(a0));
            glm::vec3 b1(std::cos(a1), 0, std::sin(a1));
            glm::vec3 side[3] = {b0, b1, apex};
            for (auto& p : side) { v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); }
            glm::vec3 cap[3] = {center, b1, b0};
            for (auto& p : cap) { v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); }
        }
        m_coneCount = (int)v.size() / 3;
        upload(m_coneVAO, m_coneVBO, v);
    }
    // Cube centred, side 1.
    {
        const float h = 0.5f;
        glm::vec3 c[8] = {
            {-h,-h,-h},{ h,-h,-h},{ h, h,-h},{-h, h,-h},
            {-h,-h, h},{ h,-h, h},{ h, h, h},{-h, h, h}};
        int faces[6][4] = {{0,1,2,3},{5,4,7,6},{4,0,3,7},
                           {1,5,6,2},{3,2,6,7},{4,5,1,0}};
        std::vector<float> v;
        for (auto& f : faces) {
            glm::vec3 q[6] = {c[f[0]], c[f[1]], c[f[2]],
                              c[f[0]], c[f[2]], c[f[3]]};
            for (auto& p : q) { v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); }
        }
        m_cubeCount = (int)v.size() / 3;
        upload(m_cubeVAO, m_cubeVBO, v);
    }
    // Ring: unit circle line loop in XY (normal +Z).
    {
        const int RSEG = 64;
        std::vector<float> v;
        for (int i = 0; i < RSEG; ++i) {
            float a = TAU * i / RSEG;
            v.push_back(std::cos(a)); v.push_back(std::sin(a)); v.push_back(0.0f);
        }
        m_ringCount = (int)v.size() / 3;
        upload(m_ringVAO, m_ringVBO, v);
    }
    // Quad: XY plane, corners +/-1 (two triangles).
    {
        glm::vec3 q[6] = {{-1,-1,0},{1,-1,0},{1,1,0},{-1,-1,0},{1,1,0},{-1,1,0}};
        std::vector<float> v;
        for (auto& p : q) { v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); }
        m_quadCount = (int)v.size() / 3;
        upload(m_quadVAO, m_quadVBO, v);
    }
    // Disc: filled unit circle in XY (triangle fan baked into triangles). Used
    // as the camera-facing cursor marker pinned to the active handle point.
    {
        const int DSEG = 28;
        std::vector<float> v;
        for (int i = 0; i < DSEG; ++i) {
            float a0 = TAU * i / DSEG, a1 = TAU * (i + 1) / DSEG;
            glm::vec3 tri[3] = {glm::vec3(0.0f),
                                glm::vec3(std::cos(a0), std::sin(a0), 0.0f),
                                glm::vec3(std::cos(a1), std::sin(a1), 0.0f)};
            for (auto& p : tri) { v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); }
        }
        m_discCount = (int)v.size() / 3;
        upload(m_discVAO, m_discVBO, v);
    }
}

void TransformGizmo::drawMesh(unsigned int vao, int count, unsigned int prim,
                              const glm::mat4& mvp, const glm::vec4& color,
                              float alpha) {
    glUniformMatrix4fv(m_uMVP, 1, GL_FALSE, glm::value_ptr(mvp));
    glUniform3f(m_uColor, color.r, color.g, color.b);
    glUniform1f(m_uAlpha, alpha);
    glBindVertexArray(vao);
    glDrawArrays(prim, 0, count);
}

void TransformGizmo::render(const glm::mat4& projection, const glm::mat4& view,
                            const glm::vec3& cameraPos) {
    if (!enabled || !hasTarget()) return;
    ensureInit();

    // Cache the vertical FOV so gizmoScale() keeps a constant on-screen size.
    // For symmetric and stereo off-axis frustums alike the vertical scale
    // projection[1][1] == 1 / tan(fovY/2); clamp to guard degenerate matrices.
    if (projection[1][1] != 0.0f)
        m_tanHalfFov = std::clamp(1.0f / std::fabs(projection[1][1]), 0.05f, 3.0f);

    const glm::vec3 pivot = *m_position;
    const float s = gizmoScale(cameraPos);
    const Mode m = effectiveMode();
    const glm::mat4 viewProj = projection * view;
    glm::vec3 axes[3];
    computeAxes(axes, m);

    // Overlay state: always-on-top, blended, no culling. Capture prior state so
    // we don't leak it into subsequent same-eye overlay draws.
    GLboolean depthWasOn = glIsEnabled(GL_DEPTH_TEST);
    GLboolean cullWasOn = glIsEnabled(GL_CULL_FACE);
    GLboolean blendWasOn = glIsEnabled(GL_BLEND);
    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glUseProgram(m_program);

    // Which handle is "hot": being dragged, or hovered while idle. Highlighting
    // keys off this so the part under the cursor is unambiguous.
    const bool engaged =
        (m_activeHandle != Handle::None) || (m_hover != Handle::None);
    auto isHot = [&](Handle h) {
        return (m_activeHandle == h) ||
               (m_activeHandle == Handle::None && m_hover == h);
    };
    // Opaque handles: the hot one turns bright; the rest are darkened (not made
    // translucent) when something is engaged so the active handle clearly pops
    // while the others recede but stay readable.
    auto solidColor = [&](Handle h, const glm::vec3& axisCol) {
        if (isHot(h)) return kHighlight;
        return engaged ? axisCol * 0.55f : axisCol;
    };
    auto alignY = [](const glm::vec3& a) {
        return glm::toMat4(glm::rotation(glm::vec3(0, 1, 0), a));
    };

    // The overlay carries no depth buffer of its own, so collect every part and
    // paint them far-to-near. That gives correct self-occlusion between handles
    // (you can tell which axis is in front); hot handles are forced last so the
    // one under the cursor is never hidden behind a sibling.
    struct Cmd {
        unsigned int vao; int count; unsigned int prim;
        glm::mat4 mvp; glm::vec3 color; float alpha; float lineWidth;
        float sortKey; bool hot;
    };
    std::vector<Cmd> cmds;
    cmds.reserve(12);
    auto push = [&](unsigned int vao, int count, unsigned int prim,
                    const glm::mat4& model, const glm::vec3& color, float alpha,
                    const glm::vec3& repPoint, bool hot, float lineWidth = 1.0f) {
        cmds.push_back({vao, count, prim, viewProj * model, color, alpha,
                        lineWidth, glm::distance(cameraPos, repPoint), hot});
    };

    const Handle axisH[3] = {Handle::AxisX, Handle::AxisY, Handle::AxisZ};
    const Handle ringH[3] = {Handle::RingX, Handle::RingY, Handle::RingZ};

    if (m == Mode::Rotate) {
        for (int i = 0; i < 3; ++i) {
            bool hot = isHot(ringH[i]);
            glm::mat4 model = glm::translate(glm::mat4(1.0f), pivot) *
                              glm::toMat4(glm::rotation(glm::vec3(0, 0, 1), axes[i])) *
                              glm::scale(glm::mat4(1.0f), glm::vec3(RING_R * s));
            // Sort by the ring's nearest arc point so the closest ring wins the
            // (otherwise tied) shared-centre ordering.
            glm::vec3 toCam = cameraPos - pivot;
            glm::vec3 inPlane = toCam - axes[i] * glm::dot(toCam, axes[i]);
            glm::vec3 rep = (glm::length(inPlane) > 1e-5f)
                                ? pivot + glm::normalize(inPlane) * (RING_R * s)
                                : pivot;
            push(m_ringVAO, m_ringCount, GL_LINE_LOOP, model,
                 solidColor(ringH[i], kAxisColor[i]), 1.0f, rep, hot,
                 hot ? 5.5f : 3.0f);
        }
    } else {
        // Axis shafts + tips (translate cones / scale cubes). The hot axis is
        // also fattened so it reads as raised toward the cursor.
        for (int i = 0; i < 3; ++i) {
            bool hot = isHot(axisH[i]);
            glm::vec3 col = solidColor(axisH[i], kAxisColor[i]);
            glm::mat4 R = alignY(axes[i]);
            float shaftR = SHAFT_R * (hot ? 1.7f : 1.0f);
            glm::mat4 shaft = glm::translate(glm::mat4(1.0f), pivot) * R *
                              glm::scale(glm::mat4(1.0f),
                                         glm::vec3(shaftR * s, SHAFT_LEN * s, shaftR * s));
            push(m_cylVAO, m_cylCount, GL_TRIANGLES, shaft, col, 1.0f,
                 pivot + axes[i] * (SHAFT_LEN * 0.5f * s), hot);

            glm::vec3 tipPos = pivot + axes[i] * (SHAFT_LEN * s);
            if (m == Mode::Scale) {
                float k = SCALE_TIP * 2.0f * (hot ? 1.4f : 1.0f);
                glm::mat4 tip = glm::translate(glm::mat4(1.0f), tipPos) *
                                glm::scale(glm::mat4(1.0f), glm::vec3(k * s));
                push(m_cubeVAO, m_cubeCount, GL_TRIANGLES, tip, col, 1.0f, tipPos, hot);
            } else {
                float tr = TIP_R * (hot ? 1.35f : 1.0f);
                float tl = TIP_LEN * (hot ? 1.15f : 1.0f);
                glm::mat4 tip = glm::translate(glm::mat4(1.0f), tipPos) * R *
                                glm::scale(glm::mat4(1.0f),
                                           glm::vec3(tr * s, tl * s, tr * s));
                push(m_coneVAO, m_coneCount, GL_TRIANGLES, tip, col, 1.0f, tipPos, hot);
            }
        }

        if (m == Mode::Translate) {
            // Two-axis plane quads (coloured by their normal axis). The hot quad
            // jumps to a high opacity; idle quads stay faint, and recede further
            // when another handle is engaged.
            const Handle planeH[3] = {Handle::PlaneYZ, Handle::PlaneZX, Handle::PlaneXY};
            const int ai[3] = {1, 2, 0}, aj[3] = {2, 0, 1}, nk[3] = {0, 1, 2};
            for (int k = 0; k < 3; ++k) {
                bool hot = isHot(planeH[k]);
                glm::vec3 center = pivot +
                    (axes[ai[k]] + axes[aj[k]]) * (PLANE_OFF * s);
                glm::mat4 basis(1.0f);
                basis[0] = glm::vec4(axes[ai[k]], 0.0f);
                basis[1] = glm::vec4(axes[aj[k]], 0.0f);
                basis[2] = glm::vec4(axes[nk[k]], 0.0f);
                glm::mat4 model = glm::translate(glm::mat4(1.0f), center) * basis *
                                  glm::scale(glm::mat4(1.0f), glm::vec3(PLANE_HALF * s));
                glm::vec3 col = hot ? kHighlight : kAxisColor[nk[k]];
                float alpha = hot ? 0.6f : (engaged ? 0.16f : 0.32f);
                push(m_quadVAO, m_quadCount, GL_TRIANGLES, model, col, alpha, center, hot);
            }
        }

        // Centre handle.
        Handle centreH = (m == Mode::Scale) ? Handle::ScaleUniform
                                            : Handle::ScreenMove;
        bool hotC = isHot(centreH);
        float ch = CENTER_HALF * 2.0f * (hotC ? 1.25f : 1.0f);
        glm::mat4 cube = glm::translate(glm::mat4(1.0f), pivot) *
                         glm::scale(glm::mat4(1.0f), glm::vec3(ch * s));
        glm::vec3 cc = hotC ? kHighlight
                            : (engaged ? kCenterColor * 0.55f : kCenterColor);
        push(m_cubeVAO, m_cubeCount, GL_TRIANGLES, cube, cc, 1.0f, pivot, hotC);
    }

    std::sort(cmds.begin(), cmds.end(), [](const Cmd& a, const Cmd& b) {
        if (a.hot != b.hot) return !a.hot;  // non-hot first, hot painted last
        return a.sortKey > b.sortKey;       // otherwise farther first
    });
    for (const Cmd& c : cmds) {
        if (c.prim == GL_LINE_LOOP) glLineWidth(c.lineWidth);
        drawMesh(c.vao, c.count, c.prim, c.mvp, glm::vec4(c.color, 1.0f), c.alpha);
    }

    // Cursor marker: a camera-facing dot pinned to the engaged handle point and
    // painted above every handle. The 3D cursor is snapped to this same point
    // but gets occluded by the always-on-top handles, so this marker is what
    // keeps the cursor visible -- the cursor literally becomes the gizmo.
    if (m_hasInteractionPoint) {
        glm::vec3 camRight = glm::normalize(
            glm::vec3(view[0][0], view[1][0], view[2][0]));
        glm::vec3 camUp = glm::normalize(
            glm::vec3(view[0][1], view[1][1], view[2][1]));
        glm::vec3 camFwd = glm::normalize(glm::cross(camRight, camUp));
        auto billboard = [&](float r) {
            glm::mat4 mdl(1.0f);
            mdl[0] = glm::vec4(camRight * r, 0.0f);
            mdl[1] = glm::vec4(camUp * r, 0.0f);
            mdl[2] = glm::vec4(camFwd * r, 0.0f);
            mdl[3] = glm::vec4(m_interactionPoint, 1.0f);
            return viewProj * mdl;
        };
        // Dark halo for contrast on any background, then a bright white core
        // ringed in the highlight colour so it reads as "the active handle".
        drawMesh(m_discVAO, m_discCount, GL_TRIANGLES, billboard(0.085f * s),
                 glm::vec4(0.04f, 0.04f, 0.05f, 1.0f), 0.85f);
        drawMesh(m_discVAO, m_discCount, GL_TRIANGLES, billboard(0.065f * s),
                 glm::vec4(kHighlight, 1.0f), 1.0f);
        drawMesh(m_discVAO, m_discCount, GL_TRIANGLES, billboard(0.038f * s),
                 glm::vec4(1.0f, 1.0f, 1.0f, 1.0f), 1.0f);
    }

    glBindVertexArray(0);
    glUseProgram(0);
    glLineWidth(1.0f);
    if (depthWasOn) glEnable(GL_DEPTH_TEST);
    if (cullWasOn) glEnable(GL_CULL_FACE);
    if (!blendWasOn) glDisable(GL_BLEND);
}

void TransformGizmo::cleanup() {
    if (!m_initialized) return;
    if (m_program) glDeleteProgram(m_program);
    unsigned int vaos[] = {m_cylVAO, m_coneVAO, m_cubeVAO, m_ringVAO, m_quadVAO,
                           m_discVAO};
    unsigned int vbos[] = {m_cylVBO, m_coneVBO, m_cubeVBO, m_ringVBO, m_quadVBO,
                           m_discVBO};
    glDeleteVertexArrays(6, vaos);
    glDeleteBuffers(6, vbos);
    m_program = 0;
    m_initialized = false;
}

} // namespace Tools
