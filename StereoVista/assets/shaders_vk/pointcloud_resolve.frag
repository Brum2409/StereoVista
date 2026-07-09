#version 460
// Schütz compute rasterizer — fullscreen resolve (standard path).
//
// Drawn inside the multiview scene pass AFTER the opaque meshes with the
// house reverse-Z depth state (GREATER test, depth write ON): writing
// gl_FragDepth lets the hardware depth test occlude points behind meshes and
// updates the depth attachment for everything that follows (skybox, later
// cursor snapping / EDL). Colour was already resolved per cloud by
// pointcloud_lookup.comp; this pass only reads the two per-pixel buffers.

#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require
#extension GL_EXT_multiview : require

#include "pointcloud_types.h"

layout(buffer_reference, scalar, buffer_reference_align = 8) restrict readonly buffer PcFramebufferRO {
    uint64_t px[];
};
layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer PcColorRO {
    uint v[];
};

layout(push_constant, scalar) uniform Push {
    PointCloudResolvePush pc;
};

layout(location = 0) out vec4 outColor;

void main() {
    ivec2 coord = ivec2(gl_FragCoord.xy);
    int pixelID = coord.y * pc.imageWidth + coord.x;
    uint view = uint(gl_ViewIndex);

    restrict PcFramebufferRO fb =
        PcFramebufferRO(pc.framebuffer + uint64_t(view) * uint64_t(pc.pixelsPerView) * 8ul);
    uint64_t entry = fb.px[pixelID];
    if (entry == 0xFFFFFFFFFFFFFFFFUL)
        discard; // no point → keep the scene pixel

    // Entry: dist24 | cloudID:8 | index:32; dist24 = 1 - reverseZ.
    uint hi = uint(entry >> 32);
    float dist01 = float(hi >> 8) / 16777215.0;
    gl_FragDepth = 1.0 - dist01; // back to reverse-Z [0,1]

    restrict PcColorRO colors =
        PcColorRO(pc.colorbuffer + uint64_t(view) * uint64_t(pc.pixelsPerView) * 4ul);
    uint rgba = colors.v[pixelID];
    vec3 color = vec3(float(rgba & 0xFFu), float((rgba >> 8) & 0xFFu),
                      float((rgba >> 16) & 0xFFu)) / 255.0;
    outColor = vec4(color, 1.0); // linear HDR; tonemap handles the rest
}
