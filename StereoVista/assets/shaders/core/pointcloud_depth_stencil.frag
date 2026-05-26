// Schütz compute rasterizer – Two-pass resolve, Pass 1 (stencil-only).
// Sets stencil=1 for every foreground point pixel so Pass 2 can be stencil-guarded.
// NO gl_FragDepth is written here — that keeps Hi-Z/early-Z enabled so the GPU
// runs this pass at full throughput (no late-Z penalty over 2M pixels).
// Depth + colour are both written in Pass 2 (pointcloud_resolve.frag), which is
// stencil-guarded and therefore only invokes the fragment shader for the ~5-10%
// of pixels that actually contain a foreground point.
#version 460 core
#extension GL_ARB_gpu_shader_int64 : require

// Per-pixel framebuffer written by pointcloud_rasterize.comp
// (depth:32 | point_index:32).  Cleared to 0xFFFFFFFFFFFFFFFF each frame.
layout(std430, binding = 1) readonly coherent buffer ssFramebuffer {
    uint64_t framebuffer[];
};

uniform ivec2 uImageSize;

void main() {
    ivec2 coord   = ivec2(gl_FragCoord.xy);
    int   pixelID = coord.y * uImageSize.x + coord.x;

    uint64_t entry = framebuffer[pixelID];

    // Sentinel: no point was rasterised here — discard (stencil stays 0).
    if (entry == 0xFFFFFFFFFFFFFFFFUL) discard;

    // Valid foreground point: stencil=1 is written by GL state (GL_REPLACE).
    // No gl_FragDepth write — depth and colour are handled in Pass 2.
    // No FragColor output — GL_COLOR_WRITEMASK is GL_FALSE for Pass 1.
}
