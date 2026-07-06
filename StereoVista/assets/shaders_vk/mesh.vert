#version 460
#extension GL_EXT_multiview : require
#extension GL_EXT_scalar_block_layout : require

// Forward scene pass, multiview: camera data is a per-view array indexed by
// gl_ViewIndex (mono renders view 0 only). Projections are reverse-Z, [0,1]
// depth, Y-flip baked in (Renderer/Projection.h).

#include "scene_types.h"

layout(set = 0, binding = 0, scalar) uniform FrameBlock { FrameData uFrame; };
layout(push_constant, scalar) uniform PushBlock { DrawPush uPush; };

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;
layout(location = 3) in vec4 aTangent; // xyz tangent, w bitangent sign

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;
layout(location = 2) out vec2 vUV;
layout(location = 3) out vec4 vTangent;

// User section planes as HARDWARE clip distances (Phase 6 ClipPlaneTool) —
// the Vulkan equivalent of the GL scene shaders' gl_ClipDistance[1..N] path
// (no fragment-shader discard; requires the shaderClipDistance device
// feature, enabled in rhi::Device). Unused slots emit +1 (always kept).
out float gl_ClipDistance[SV_MAX_CLIP_PLANES];

void main() {
    vec4 world = uPush.model * vec4(aPosition, 1.0);
    mat3 normalMat = mat3(uPush.normalMatCol0.xyz, uPush.normalMatCol1.xyz,
                          uPush.normalMatCol2.xyz);
    vWorldPos = world.xyz;
    vNormal = normalMat * aNormal;
    vTangent = vec4(normalMat * aTangent.xyz, aTangent.w);
    vUV = aUV;
    for (int i = 0; i < SV_MAX_CLIP_PLANES; ++i)
        gl_ClipDistance[i] = (uint(i) < uFrame.clipPlaneCount)
                                 ? dot(uFrame.clipPlanes[i].xyz, world.xyz) +
                                       uFrame.clipPlanes[i].w
                                 : 1.0;
    gl_Position = uFrame.views[gl_ViewIndex].viewProj * world;
}
