// pointcloud_hqs_resolve.frag
// Schütz High-Quality Shading – PASS 3 of 3 (compute_loop_las_hqs/resolve.cs).
//
// Fullscreen quad. For each pixel, divides the accumulated R,G,B by the point
// count to produce the averaged (anti-aliased) colour, and writes gl_FragDepth
// reconstructed from the nearest linear eye depth so the hardware GL_LESS test
// occludes points behind meshes and the depth attachment is updated for EDL /
// cursor snapping — exactly like the standard pointcloud_resolve.frag.
//
// Window-space depth from linear eye depth d (perspective projection):
//     ndcZ = -proj[2][2] + proj[3][2] / d        (uProjA = proj[2][2],
//     gl_FragDepth = ndcZ * 0.5 + 0.5             uProjB = proj[3][2])
// Both coefficients are unaffected by the asymmetric-frustum x/y shear, so this
// is correct for the left/right stereo eyes.
#version 460 core

in  vec2 TexCoords;
out vec4 FragColor;

// Nearest linear depth (uint per pixel) and R,G,B,count accumulator (uint x4).
layout(std430, binding = 2) readonly buffer ssHqsDepthBlock { uint ssDepth[]; };
layout(std430, binding = 3) readonly buffer ssHqsAccumBlock { uint ssAccum[]; };

uniform ivec2 uImageSize;
uniform float uProjA;   // proj[2][2]
uniform float uProjB;   // proj[3][2]

void main() {
    ivec2 coord   = ivec2(gl_FragCoord.xy);
    int   pixelID = coord.y * uImageSize.x + coord.x;

    uint count = ssAccum[4 * pixelID + 3];
    if (count == 0u) discard;   // no point covered this pixel → show scene behind

    uint R = ssAccum[4 * pixelID + 0];
    uint G = ssAccum[4 * pixelID + 1];
    uint B = ssAccum[4 * pixelID + 2];
    vec3 color = vec3(float(R), float(G), float(B)) / (float(count) * 255.0);

    float d    = uintBitsToFloat(ssDepth[pixelID]);  // nearest linear eye depth
    float ndcZ = -uProjA + uProjB / d;
    gl_FragDepth = ndcZ * 0.5 + 0.5;

    FragColor = vec4(color, 1.0);
}
