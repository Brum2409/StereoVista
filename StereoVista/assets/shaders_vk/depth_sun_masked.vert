#version 460
#extension GL_EXT_scalar_block_layout : require

// Alpha-masked sun shadow casters: depth_sun.vert plus a UV pass-through for
// depth_masked.frag's alpha-cutoff discard. Only draws whose material carries
// SV_MATERIAL_ALPHA_MASK + an albedo texture come through here — everything
// else keeps the fragment-less early-Z pipeline.

#include "scene_types.h"

layout(set = 0, binding = 0, scalar) uniform FrameBlock { FrameData uFrame; };
layout(push_constant, scalar) uniform PushBlock { DepthMaskedPush uPush; };

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec2 aUV;

layout(location = 0) out vec2 vUV;

void main() {
    vUV = aUV;
    gl_Position = uFrame.sunViewProj * (uPush.model * vec4(aPosition, 1.0));
}
