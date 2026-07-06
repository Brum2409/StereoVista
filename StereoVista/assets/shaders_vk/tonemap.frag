#version 460

// HDR resolve: reads one layer of the multiview RGBA16F scene target and
// writes tone-mapped, sRGB-encoded LDR to the backbuffer. Sampled by UV
// (linear) computed from the fragment position relative to this eye's
// on-screen rect: identity for a full-window eye (mono / quad-buffer stereo,
// 1:1 at texel centres == texelFetch), and a horizontal squish when the eye
// occupies a half-viewport (side-by-side preview).
//
// Operator set carried over from the GL app's bloom/final.frag with fixes,
// per the migration's no-stale-copy rule:
//   * AgX is the real minimal AgX (inset/outset matrices + log2 sigmoid fit
//     by Benjamin Wrensch/Troy Sobotka), not the old "gentle log curve"
//     stand-in.
//   * The old "Tony McMapface" was a luminance hack, not the actual LUT;
//     dropped until it can ship as a proper 3D LUT with the post-FX phase.
//   * Display encode is the exact sRGB OETF, not pow(1/2.2).

layout(set = 0, binding = 0) uniform sampler2DArray uSceneColor;

layout(push_constant) uniform TonemapPush {
    float exposure;
    int operatorIndex; // renderer::TonemapOperator
    int layerIndex;    // scene-target array layer to resolve (0 = left/mono)
    // This eye's on-screen rect in framebuffer pixels; maps gl_FragCoord to a
    // [0,1] UV over the full scene layer (origin 0 + invSize 1/extent = full).
    float originX;
    float originY;
    float invSizeX;
    float invSizeY;
    // 1 = apply the sRGB OETF here (UNORM targets: window backbuffer, XR UNORM
    // fallback). 0 = output LINEAR and let an sRGB-format target's hardware
    // encode on write (OpenXR sRGB eye swapchains) — avoids a double gamma.
    int encodeSrgb;
} uPush;

layout(location = 0) out vec4 outColor;

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// 0. Classic Reinhard.
vec3 tonemapReinhard(vec3 hdr) {
    return hdr / (1.0 + hdr);
}

// 1. ACES filmic fit (Krzysztof Narkowicz 2015).
vec3 tonemapAces(vec3 hdr) {
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return clamp((hdr * (a * hdr + b)) / (hdr * (c * hdr + d) + e), 0.0, 1.0);
}

// 2. Uncharted 2 filmic (John Hable's constants, white-point normalized).
vec3 uncharted2Curve(vec3 x) {
    const float A = 0.15, B = 0.50, C = 0.10, D = 0.20, E = 0.02, F = 0.30;
    return ((x * (A * x + C * B) + D * E)) / (x * (A * x + B) + D * F) - E / F;
}

vec3 tonemapUncharted2(vec3 hdr) {
    const float W = 11.2;
    return uncharted2Curve(hdr) / uncharted2Curve(vec3(W));
}

// 3. AgX (minimal implementation: inset -> log2 window -> sigmoid fit ->
//    outset -> linearize). Output is linear; the shared sRGB encode follows.
vec3 agxContrastApprox(vec3 x) {
    vec3 x2 = x * x;
    vec3 x4 = x2 * x2;
    return + 15.5     * x4 * x2
           - 40.14    * x4 * x
           + 31.96    * x4
           - 6.868    * x2 * x
           + 0.4298   * x2
           + 0.1191   * x
           - 0.00232;
}

vec3 tonemapAgx(vec3 hdr) {
    const mat3 agxInset = mat3(
        0.842479062253094,  0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772,  0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104);
    const mat3 agxOutset = mat3(
         1.19687900512017,   -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368,  1.15190312990417,   -0.0980434501171241,
        -0.0990297440797205, -0.0989611768448433,  1.15107367264116);
    const float minEv = -12.47393;
    const float maxEv = 4.026069;

    vec3 val = agxInset * max(hdr, vec3(0.0));
    val = clamp(log2(val), minEv, maxEv);
    val = (val - minEv) / (maxEv - minEv);
    val = agxContrastApprox(val);
    val = agxOutset * val;
    return pow(clamp(val, 0.0, 1.0), vec3(2.2)); // back to linear
}

// 4. Khronos PBR Neutral (official glTF specification).
vec3 tonemapKhronosNeutral(vec3 hdr) {
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;

    float x = min(hdr.r, min(hdr.g, hdr.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    hdr -= offset;

    float peak = max(hdr.r, max(hdr.g, hdr.b));
    if (peak < startCompression)
        return hdr;

    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    hdr *= newPeak / peak;

    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(hdr, vec3(newPeak), g);
}

// Exact sRGB OETF (piecewise), not the pow(1/2.2) approximation.
vec3 srgbEncode(vec3 linear) {
    vec3 lo = linear * 12.92;
    vec3 hi = 1.055 * pow(linear, vec3(1.0 / 2.4)) - 0.055;
    return mix(lo, hi, step(vec3(0.0031308), linear));
}

void main() {
    vec2 uv = (gl_FragCoord.xy - vec2(uPush.originX, uPush.originY)) *
              vec2(uPush.invSizeX, uPush.invSizeY);
    vec3 hdr = textureLod(uSceneColor, vec3(uv, float(uPush.layerIndex)), 0.0).rgb;
    hdr *= uPush.exposure;

    vec3 mapped;
    if (uPush.operatorIndex == 0)
        mapped = tonemapReinhard(hdr);
    else if (uPush.operatorIndex == 2)
        mapped = tonemapUncharted2(hdr);
    else if (uPush.operatorIndex == 3)
        mapped = tonemapAgx(hdr);
    else if (uPush.operatorIndex == 4)
        mapped = tonemapKhronosNeutral(hdr);
    else
        mapped = tonemapAces(hdr); // 1 and any unknown index: ACES default

    vec3 ldr = clamp(mapped, 0.0, 1.0);
    outColor = vec4(uPush.encodeSrgb != 0 ? srgbEncode(ldr) : ldr, 1.0);
}
