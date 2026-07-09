#version 460
// Schütz High-Quality Shading — PASS 3 of 3: fullscreen resolve.
//
// Divides the accumulated R,G,B by the point count for the averaged
// (anti-aliased) colour and reconstructs the reverse-Z window depth from the
// nearest linear eye depth d:
//
//     depth = -proj[2][2] + proj[3][2] / d        (clamped to [0,1])
//
// — the [0,1] form of the GL shader's "ndcZ*0.5+0.5" math (C.2). Both
// coefficients are unaffected by asymmetric-frustum x/y shear, so this stays
// correct per stereo eye; they arrive per view in the push constants.
// Depth state matches the standard resolve (reverse-Z GREATER, write ON).

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_multiview : require

#include "pointcloud_types.h"

layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer PcHqsDepthRO {
    uint v[];
};
layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer PcHqsAccumRO {
    uint v[]; // R,G,B,count interleaved, 4 per pixel
};

layout(push_constant, scalar) uniform Push {
    PointCloudHqsResolvePush pc;
};

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);
    int pixelID = coord.y * pc.imageWidth + coord.x;
    uint view = uint(gl_ViewIndex);

    restrict PcHqsAccumRO accum =
        PcHqsAccumRO(pc.hqsAccum + uint64_t(view) * uint64_t(pc.pixelsPerView) * 16ul);
    uint count = accum.v[4 * pixelID + 3];
    if (count == 0u)
        discard; // no point covered this pixel → show the scene behind

    uint R = accum.v[4 * pixelID + 0];
    uint G = accum.v[4 * pixelID + 1];
    uint B = accum.v[4 * pixelID + 2];
    vec3 color = vec3(float(R), float(G), float(B)) / (float(count) * 255.0);

    restrict PcHqsDepthRO depthBuf =
        PcHqsDepthRO(pc.hqsDepth + uint64_t(view) * uint64_t(pc.pixelsPerView) * 4ul);
    float d = uintBitsToFloat(depthBuf.v[pixelID]); // nearest linear eye depth
    vec2 projAB = pc.projAB[view].xy;
    gl_FragDepth = clamp(-projAB.x + projAB.y / d, 0.0, 1.0);

    outColor = vec4(color, 1.0); // linear HDR; tonemap handles the rest
}
