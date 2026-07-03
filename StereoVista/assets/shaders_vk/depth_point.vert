#version 460
#extension GL_EXT_multiview : require
#extension GL_EXT_scalar_block_layout : require

// Point-light shadow casters: all 6 cube faces in ONE multiview pass
// (viewMask 0x3F, gl_ViewIndex = face) — replaces the GL geometry-shader
// broadcast. Depth-only with real hardware depth from the reverse-Z face
// projection; the GL path's linear-distance gl_FragDepth write (which killed
// early-Z) was deliberately not ported (playbook C.2/C.7 spirit).

#include "scene_types.h"

layout(set = 0, binding = 0, scalar) uniform FrameBlock { FrameData uFrame; };
layout(push_constant, scalar) uniform PushBlock { DepthPointPush uPush; };

layout(location = 0) in vec3 aPosition;

void main() {
    mat4 faceVP = uFrame.pointShadowFaceVP[uPush.lightSlot * 6u + gl_ViewIndex];
    gl_Position = faceVP * (uPush.model * vec4(aPosition, 1.0));
}
