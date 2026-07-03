#version 460
#extension GL_EXT_scalar_block_layout : require

// Sun shadow casters: depth-only, no fragment stage (early-Z fast path).
// sunViewProj is the reverse-Z ortho in FrameData.

#include "scene_types.h"

layout(set = 0, binding = 0, scalar) uniform FrameBlock { FrameData uFrame; };
layout(push_constant, scalar) uniform PushBlock { DepthSunPush uPush; };

layout(location = 0) in vec3 aPosition;

void main() {
    gl_Position = uFrame.sunViewProj * (uPush.model * vec4(aPosition, 1.0));
}
