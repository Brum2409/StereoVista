// Schütz compute rasterizer – Phase 2
// Resolve pass: read the compute-rasterised colour image and composite it
// into the currently-bound HDR framebuffer.
// Pixels where the compute rasteriser wrote nothing (packed == 0) are
// discarded, leaving the underlying scene content intact.
#version 460 core

in vec2 TexCoords;
out vec4 FragColor;

// The r32ui colour image produced by pointcloud_rasterize.comp
layout(r32ui, binding = 0) readonly coherent uniform uimage2D computeColorImage;

void main() {
    ivec2 coord  = ivec2(gl_FragCoord.xy);
    uint  packed = imageLoad(computeColorImage, coord).r;

    // Packed == 0 means no point was rasterised here → keep scene pixel.
    if (packed == 0u) discard;

    // Unpack ABGR (as stored in the rasterize shader):
    //   bits  7-0 : R,  bits 15-8 : G,  bits 23-16 : B,  bits 31-24 : A
    float r = float( packed        & 0xFFu) / 255.0;
    float g = float((packed >>  8u) & 0xFFu) / 255.0;
    float b = float((packed >> 16u) & 0xFFu) / 255.0;

    // Output in linear HDR space (tone-mapping happens later in applyBloom).
    FragColor = vec4(r, g, b, 1.0);
}
