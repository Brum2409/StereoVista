// Schütz compute rasterizer – final fragment resolve pass.
//
// At this point the framebuffer SSBO has been rewritten by
// pointcloud_color_lookup.comp to contain (depth:32 | packedRGBA8:32) instead
// of (depth:32 | point_index:32).  All scatter into ssRGBA happened in the
// preceding compute pass, where memory parallelism is much higher.
//
// This shader does only the unavoidable per-fragment work:
//   - read the per-pixel SSBO entry (1 contiguous read, cache-friendly)
//   - discard sentinel
//   - write gl_FragDepth so the hardware GL_LESS test handles mesh occlusion
//     and the depth attachment gets updated for cursor snapping / EDL / subseq.
//     geometry
//   - write FragColor to the HDR colour attachment
#version 460 core
#extension GL_ARB_gpu_shader_int64 : require

in  vec2 TexCoords;
out vec4 FragColor;

// Per-pixel framebuffer.  After the color-lookup compute pass this holds
// (depth:32 | packedRGBA8:32).  Sentinel 0xFFFFFFFFFFFFFFFFUL = no point.
layout(std430, binding = 1) readonly buffer ssFramebuffer {
    uint64_t framebuffer[];
};

uniform ivec2 uImageSize;

void main() {
    ivec2 coord   = ivec2(gl_FragCoord.xy);
    int   pixelID = coord.y * uImageSize.x + coord.x;

    uint64_t entry = framebuffer[pixelID];
    if (entry == 0xFFFFFFFFFFFFFFFFUL) discard;

    // Depth (high 32 bits) → gl_FragDepth for hardware depth test
    gl_FragDepth = uintBitsToFloat(uint(entry >> 32UL));

    // Colour (low 32 bits) → unpack RGBA8 directly, no scatter
    uint packed = uint(entry & 0xFFFFFFFFUL);
    float r = float( packed        & 0xFFu) / 255.0;
    float g = float((packed >>  8u) & 0xFFu) / 255.0;
    float b = float((packed >> 16u) & 0xFFu) / 255.0;
    FragColor = vec4(r, g, b, 1.0);
}
