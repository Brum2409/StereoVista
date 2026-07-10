#version 460
#extension GL_EXT_multiview : require
#extension GL_EXT_scalar_block_layout : require

// Alpha-masked point-light shadow casters: depth_point.vert (all 6 cube faces
// in one multiview pass) plus a UV pass-through for depth_masked.frag's
// alpha-cutoff discard.

#include "scene_types.h"

layout(set = 0, binding = 0, scalar) uniform FrameBlock { FrameData uFrame; };
layout(push_constant, scalar) uniform PushBlock { DepthPointMaskedPush uPush; };

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;

layout(location = 0) out vec2 vUV;

void main() {
    vUV = aUV;
    mat4 faceVP = uFrame.pointShadowFaceVP[uPush.lightSlot * 6u + gl_ViewIndex];
    gl_Position = faceVP * (uPush.model * vec4(aPosition, 1.0));
}
