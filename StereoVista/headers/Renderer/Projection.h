#pragma once

// ============================================================================
// HOUSE PROJECTION CONVENTIONS — every projection matrix in the Vulkan
// renderer MUST come from these factories. Do not call glm::perspective/ortho
// directly above the RHI.
//
//  1. Depth range [0,1]: GLM_FORCE_DEPTH_ZERO_TO_ONE is defined project-wide
//     (Vulkan clip space; GL's [-1,1] convention no longer exists here).
//  2. REVERSE-Z: near maps to depth 1, far to 0. Float depth precision is
//     dramatically better distributed; use VK_COMPARE_OP_GREATER (or
//     GREATER_OR_EQUAL) and clear depth to 0.0.
//  3. Vulkan's NDC +Y points down. The factories negate the Y row, which
//     keeps view/world space right-handed Y-up like the GL app and the image
//     upright. Because the flip is baked into the projection (not applied as
//     a negative-height viewport), triangles keep the same on-screen
//     orientation as in GL, and Vulkan's front-face test — evaluated in
//     framebuffer space, i.e. as seen on screen — agrees with GL: CCW-authored
//     front faces stay COUNTER-CLOCKWISE. Use VK_FRONT_FACE_COUNTER_CLOCKWISE
//     with these matrices. (Only the negative-viewport-height flip method
//     toggles winding, because it acts after clipping, inside the viewport
//     transform the front-face test observes.)
// ============================================================================

#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
#error "GLM_FORCE_DEPTH_ZERO_TO_ONE must be defined project-wide (StereoVista.vcxproj)"
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>

namespace renderer {

// depth' = 1 - depth on any [0,1]-depth clip-space matrix (z_clip' =
// w_clip - z_clip), i.e. the z output row becomes (row3 - row2).
inline glm::mat4 reverseZ(glm::mat4 proj) {
    for (int col = 0; col < 4; ++col)
        proj[col][2] = proj[col][3] - proj[col][2];
    return proj;
}

inline glm::mat4 flipY(glm::mat4 proj) {
    proj[1][1] *= -1.0f;
    return proj;
}

inline glm::mat4 perspective(float fovYRadians, float aspect, float zNear, float zFar) {
    return flipY(reverseZ(glm::perspective(fovYRadians, aspect, zNear, zFar)));
}

// Infinite far plane (still reverse-Z: depth → 0 at infinity).
inline glm::mat4 perspectiveInfinite(float fovYRadians, float aspect, float zNear) {
    const float f = 1.0f / std::tan(fovYRadians * 0.5f);
    glm::mat4 proj(0.0f);
    proj[0][0] = f / aspect;
    proj[1][1] = -f;          // Y flip baked in
    proj[2][3] = -1.0f;
    proj[3][2] = zNear;       // depth = zNear / viewDistance ∈ (0,1], 1 at near
    return proj;
}

inline glm::mat4 ortho(float left, float right, float bottom, float top,
                       float zNear, float zFar) {
    return flipY(reverseZ(glm::ortho(left, right, bottom, top, zNear, zFar)));
}

// Asymmetric frustum (stereo eye projections, Phase 7). left/right/bottom/top
// are on the near plane, exactly like glm::frustum / glFrustum.
inline glm::mat4 frustumAsymmetric(float left, float right, float bottom, float top,
                                   float zNear, float zFar) {
    return flipY(reverseZ(glm::frustum(left, right, bottom, top, zNear, zFar)));
}

// ---- Cube-map face rendering (point-light shadows) ----
// DELIBERATELY NO flipY: the house Y-flip exists to keep the PRESENTED image
// upright; a cube face is only ever resampled through the fixed-function
// cube lookup, whose direction→texel mapping is identical in GL and Vulkan
// (NDC -1..+1 maps to texel row 0..N the same way in both APIs). Rendering
// the faces with the classic GL cubemap view matrices and a plain reverse-Z
// projection therefore reproduces GL sampling exactly; adding the flip would
// mirror every face. Winding does flip without the Y-flip — the point-shadow
// pipeline culls NONE so it cannot matter.
inline glm::mat4 perspectiveCubeFace(float zNear, float zFar) {
    return reverseZ(glm::perspective(glm::radians(90.0f), 1.0f, zNear, zFar));
}

// faceIndex follows the cube-layer order +X,-X,+Y,-Y,+Z,-Z with the standard
// GL cubemap up vectors.
inline glm::mat4 cubeFaceView(const glm::vec3& eye, int faceIndex) {
    static const glm::vec3 dirs[6] = {
        { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 },
    };
    static const glm::vec3 ups[6] = {
        { 0, -1, 0 }, { 0, -1, 0 }, { 0, 0, 1 }, { 0, 0, -1 }, { 0, -1, 0 }, { 0, -1, 0 },
    };
    return glm::lookAt(eye, eye + dirs[faceIndex], ups[faceIndex]);
}

} // namespace renderer
