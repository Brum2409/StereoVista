#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

// Unified overlay renderer (Phase 6, playbook C.9) — vertex expansion.
//
// Every batch is a triangle list; the push-constant `kind` selects how the
// attributes are interpreted:
//   FLAT / SPHERE      aPos is a world-space triangle vertex.
//   LINE               aPos/aAux are the segment endpoints; aParams.x = side
//                      (-1/+1), aParams.y = end (0 = A, 1 = B), aParams.z =
//                      width in pixels. The quad is expanded perpendicular to
//                      the segment in SCREEN space, with the segment clipped
//                      against the near plane first (GL raster clipping did
//                      that for free; without it a behind-camera endpoint
//                      would smear across the viewport).
//   MARKER             aPos = world center, aParams.xy = corner (-1..1),
//                      aParams.z = marker size in pixels.
//   DISC / RINGS       aPos = world center, aParams.xy = corner (-1..1),
//                      aParams.z = world-space radius; the quad billboards
//                      toward the camera.
//   BACKGROUND         aPos.xy is already NDC, aParams.xy = uv.

#include "overlay_types.h"

layout(buffer_reference, scalar, buffer_reference_align = 8) readonly buffer ViewParamsRO {
    OverlayViewParams v[];
};

layout(push_constant, scalar) uniform Push { OverlayPush pc; };

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aAux;    // LINE: other endpoint; SPHERE: normal
layout(location = 2) in vec4 aParams;
layout(location = 3) in vec4 aColor;  // RGBA8 unorm

layout(location = 0) out vec4 vColor;
layout(location = 1) out vec2 vUV;
layout(location = 2) out vec3 vWorldPos;
layout(location = 3) out vec3 vNormal;

void main() {
    OverlayViewParams view = ViewParamsRO(pc.viewParams).v[pc.viewIndex];
    vColor = aColor;
    vUV = aParams.xy;
    vWorldPos = aPos;
    vNormal = aAux;

    if (pc.kind == SV_OVERLAY_KIND_BACKGROUND) {
        gl_Position = vec4(aPos.xy, 0.0, 1.0);
        return;
    }

    if (pc.kind == SV_OVERLAY_KIND_LINE) {
        vec4 clipA = view.viewProj * vec4(aPos, 1.0);
        vec4 clipB = view.viewProj * vec4(aAux, 1.0);

        // Clip the segment against w = epsilon so an endpoint behind the eye
        // cannot flip across the screen after the divide.
        const float wEps = 1e-4;
        if (clipA.w < wEps && clipB.w < wEps) {
            gl_Position = vec4(2.0, 2.0, 2.0, 1.0); // fully behind: degenerate
            return;
        }
        if (clipA.w < wEps) {
            float t = (wEps - clipA.w) / (clipB.w - clipA.w);
            clipA = mix(clipA, clipB, t);
        } else if (clipB.w < wEps) {
            float t = (wEps - clipB.w) / (clipA.w - clipB.w);
            clipB = mix(clipB, clipA, t);
        }

        vec2 ndcA = clipA.xy / clipA.w;
        vec2 ndcB = clipB.xy / clipB.w;
        vec2 pxA = ndcA * view.viewportPx.xy * 0.5;
        vec2 pxB = ndcB * view.viewportPx.xy * 0.5;
        vec2 dir = pxB - pxA;
        float len = length(dir);
        dir = (len > 1e-4) ? dir / len : vec2(1.0, 0.0);
        vec2 normal = vec2(-dir.y, dir.x);

        bool isB = aParams.y > 0.5;
        vec4 clip = isB ? clipB : clipA;
        vec2 offsetPx = normal * (aParams.z * 0.5) * aParams.x;
        clip.xy += offsetPx * 2.0 * view.viewportPx.zw * clip.w;
        gl_Position = clip;
        return;
    }

    if (pc.kind == SV_OVERLAY_KIND_MARKER) {
        vec4 clip = view.viewProj * vec4(aPos, 1.0);
        vec2 offsetPx = aParams.xy * (aParams.z * 0.5);
        clip.xy += offsetPx * 2.0 * view.viewportPx.zw * clip.w;
        gl_Position = clip;
        return;
    }

    if (pc.kind == SV_OVERLAY_KIND_DISC || pc.kind == SV_OVERLAY_KIND_RINGS) {
        // Camera-facing billboard in world space (same basis construction as
        // the GL PlaneCursor / CursorPreview3D billboards).
        vec3 forward = view.cameraPos.xyz - aPos;
        float fl = length(forward);
        forward = (fl > 1e-5) ? forward / fl : vec3(0.0, 0.0, 1.0);
        vec3 upRef = (abs(forward.y) < 0.99) ? vec3(0.0, 1.0, 0.0)
                                             : vec3(1.0, 0.0, 0.0);
        vec3 right = normalize(cross(upRef, forward));
        vec3 up = cross(forward, right);
        vec3 world = aPos + (right * aParams.x + up * aParams.y) * aParams.z;
        gl_Position = view.viewProj * vec4(world, 1.0);
        return;
    }

    // FLAT / SPHERE: plain world-space vertices.
    gl_Position = view.viewProj * vec4(aPos, 1.0);
}
