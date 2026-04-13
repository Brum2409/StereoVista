// Schütz compute rasterizer – Two-pass resolve, Pass 2 (colour only).
// Pass 1 (pointcloud_depth_stencil.frag) already wrote gl_FragDepth and set
// stencil=1 for every valid foreground point.  The GL stencil test (GL_EQUAL,
// ref=1) rejects ~90-95% of pixels before this shader even runs, so the
// expensive colour work executes only for the pixels that matter.
//
// Removing gl_FragDepth here lets the GPU skip late-Z serialisation and run
// colour fragments at full throughput.  Depth interaction is correct because
// Pass 1 already updated the depth buffer with the point-cloud depths.
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

    // Stencil test guarantees this is a valid foreground point, but keep the
    // sentinel guard as a safety net against any edge-case stencil leakage.
    if (entry == 0xFFFFFFFFFFFFFFFFUL) discard;

    // Extract point index from the low 32 bits and unpack colour.
    // No gl_FragDepth: depth was written correctly in Pass 1.
    uint idx    = uint(entry & 0xFFFFFFFFUL);
    uint packed = packedColor[idx];

    float r = float( packed        & 0xFFu) / 255.0;
    float g = float((packed >>  8u) & 0xFFu) / 255.0;
    float b = float((packed >> 16u) & 0xFFu) / 255.0;

    FragColor = vec4(r, g, b, 1.0);
}
