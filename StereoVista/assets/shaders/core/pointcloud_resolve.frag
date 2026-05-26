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
// No 64-bit extension required — the SSBO is read as uvec2 (same std430 layout
// as uint64_t: .x = low 32 bits, .y = high 32 bits) to avoid requiring
// GL_ARB_gpu_shader_int64, which NVIDIA drivers may not expose for fragment shaders.

in  vec2 TexCoords;
out vec4 FragColor;

// Per-pixel framebuffer.  After the color-lookup compute pass this holds
// (depth:32 | packedRGBA8:32).  Sentinel 0xFFFFFFFFFFFFFFFF = no point.
// Declared as uvec2 instead of uint64_t for cross-vendor fragment compatibility:
//   .y = high 32 bits = depth
//   .x = low  32 bits = packed RGBA8 colour
layout(std430, binding = 1) readonly buffer ssFramebuffer {
    uvec2 framebuffer[];
};

uniform ivec2 uImageSize;

void main() {
    ivec2 coord   = ivec2(gl_FragCoord.xy);
    int   pixelID = coord.y * uImageSize.x + coord.x;

    uvec2 entry = framebuffer[pixelID];
    if (entry.x == 0xFFFFFFFFu && entry.y == 0xFFFFFFFFu) discard;

    // Depth (high 32 bits = .y) → gl_FragDepth for hardware depth test
    gl_FragDepth = uintBitsToFloat(entry.y);

    // Colour (low 32 bits = .x) → unpack RGBA8 directly, no scatter
    float r = float( entry.x        & 0xFFu) / 255.0;
    float g = float((entry.x >>  8u) & 0xFFu) / 255.0;
    float b = float((entry.x >> 16u) & 0xFFu) / 255.0;
    FragColor = vec4(r, g, b, 1.0);
}
