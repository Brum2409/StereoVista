// Schütz compute rasterizer – Two-pass resolve, Pass 1.
// Writes point-cloud depth into the hardware depth buffer so that:
//   (a) points behind meshes are discarded by the GL_LESS hardware depth test,
//   (b) stencil=1 is recorded for every pixel that passes (foreground point).
// Color is written in Pass 2 (pointcloud_resolve.frag), which uses the stencil
// mask to skip ~90-95% of pixels before the fragment shader even runs.
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

    // Sentinel: no point was rasterised here — skip (no depth or stencil write).
    if (entry == 0xFFFFFFFFFFFFFFFFUL) discard;

    // Write the point's NDC depth [0,1] so the hardware depth test (GL_LESS)
    // discards points behind meshes.  Pixels that pass write stencil=1 via the
    // GL stencil state set by the caller (GL_REPLACE on depth-pass).
    uint depth_uint = uint(entry >> 32UL);
    gl_FragDepth    = uintBitsToFloat(depth_uint);
    // No FragColor output — GL_COLOR_WRITEMASK is GL_FALSE for Pass 1.
}
