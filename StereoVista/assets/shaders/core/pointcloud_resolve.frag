// Schütz compute rasterizer – Phase 2
// Resolve pass: read the uint64_t framebuffer (depth:32 | index:32) and
// composite point-cloud colours into the currently-bound HDR framebuffer.
// Pixels where the framebuffer still holds the sentinel 0xFFFFFFFFFFFFFFFF
// are discarded, leaving the underlying scene content intact.
//
// Depth interaction: gl_FragDepth is written with the point's NDC depth so
// that the hardware depth test compares it against the existing mesh depth.
// Points behind meshes are discarded by GL_LESS; points in front overwrite
// the mesh colour and update the depth buffer for EDL.
#version 460 core
#extension GL_ARB_gpu_shader_int64 : require

in  vec2 TexCoords;
out vec4 FragColor;

// Per-pixel framebuffer written by pointcloud_rasterize.comp
// (depth:32 | point_index:32).  Cleared to 0xFFFFFFFFFFFFFFFF each frame.
layout(std430, binding = 1) readonly coherent buffer ssFramebuffer {
    uint64_t framebuffer[];
};

// Per-point packed ABGR colours written during rasterisation.
layout(std430, binding = 44) readonly coherent buffer ssColors {
    uint packedColor[];
};

uniform ivec2 uImageSize;

void main() {
    ivec2 coord   = ivec2(gl_FragCoord.xy);
    int   pixelID = coord.y * uImageSize.x + coord.x;

    uint64_t entry = framebuffer[pixelID];

    // Sentinel: no point was rasterised here – keep the scene pixel.
    if (entry == 0xFFFFFFFFFFFFFFFFUL) discard;

    // Write the point's NDC depth [0,1] so the hardware depth test compares it
    // against the existing depth buffer (written by mesh rendering).  Points
    // behind meshes are discarded; points in front overwrite the mesh pixel
    // and update the depth buffer (used by EDL in the bloom final pass).
    uint depth_uint = uint(entry >> 32UL);
    gl_FragDepth    = uintBitsToFloat(depth_uint);

    // Extract point index from the low 32 bits.
    uint idx    = uint(entry & 0xFFFFFFFFUL);
    uint packed = packedColor[idx];

    float r = float( packed        & 0xFFu) / 255.0;
    float g = float((packed >>  8u) & 0xFFu) / 255.0;
    float b = float((packed >> 16u) & 0xFFu) / 255.0;

    FragColor = vec4(r, g, b, 1.0);
}
