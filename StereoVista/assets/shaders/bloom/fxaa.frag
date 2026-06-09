#version 460 core
out vec4 FragColor;

in vec2 TexCoords;

// FXAA (Fast Approximate Anti-Aliasing).
// Post-process pass run AFTER tone mapping + gamma, i.e. it operates on the
// non-linear (sRGB/gamma-encoded) LDR image exactly as FXAA expects.
// Algorithm: Timothy Lottes' FXAA, reimplemented in the well-known form
// described by Simon Rodriguez ("Implementing FXAA"). Luma-driven edge
// detection + edge-direction search + sub-pixel aliasing removal.

uniform sampler2D screenTexture;  // tone-mapped, gamma-encoded LDR image
uniform vec2  inverseScreenSize;  // 1.0 / viewport size (in pixels)
uniform float fxaaSubpixel;       // sub-pixel aliasing removal amount [0..1]
uniform float fxaaEdgeThreshold;  // relative edge threshold (~0.063..0.333)

// Minimum absolute luma range below which a pixel is never treated as an edge
// (keeps very dark regions from shimmering).
#define EDGE_THRESHOLD_MIN 0.0312

// Number of exploration steps along the edge in each direction.
#define ITERATIONS 12

// Perceptual luma. Input is already gamma-encoded; the sqrt approximates the
// extra perceptual weighting FXAA relies on for stable edge detection.
float rgb2luma(vec3 rgb) {
    return sqrt(dot(rgb, vec3(0.299, 0.587, 0.114)));
}

// Step multipliers used while searching for the end of an edge. Larger steps
// further out trade a little accuracy for far fewer texture fetches.
float qualityStep(int i) {
    if (i < 5)  return 1.0;
    if (i == 5) return 1.5;
    if (i < 10) return 2.0;
    if (i == 10) return 4.0;
    return 8.0;
}

void main()
{
    vec3 colorCenter = texture(screenTexture, TexCoords).rgb;

    // Luma at the current fragment and its 4 direct neighbours.
    float lumaCenter = rgb2luma(colorCenter);
    float lumaDown  = rgb2luma(textureOffset(screenTexture, TexCoords, ivec2( 0, -1)).rgb);
    float lumaUp    = rgb2luma(textureOffset(screenTexture, TexCoords, ivec2( 0,  1)).rgb);
    float lumaLeft  = rgb2luma(textureOffset(screenTexture, TexCoords, ivec2(-1,  0)).rgb);
    float lumaRight = rgb2luma(textureOffset(screenTexture, TexCoords, ivec2( 1,  0)).rgb);

    float lumaMin = min(lumaCenter, min(min(lumaDown, lumaUp), min(lumaLeft, lumaRight)));
    float lumaMax = max(lumaCenter, max(max(lumaDown, lumaUp), max(lumaLeft, lumaRight)));
    float lumaRange = lumaMax - lumaMin;

    // If the local contrast is below the threshold (or we're in a very dark
    // area), this is not an edge - output the original pixel unchanged.
    if (lumaRange < max(EDGE_THRESHOLD_MIN, lumaMax * fxaaEdgeThreshold)) {
        FragColor = vec4(colorCenter, 1.0);
        return;
    }

    // Diagonal neighbours, needed to estimate edge orientation.
    float lumaDownLeft  = rgb2luma(textureOffset(screenTexture, TexCoords, ivec2(-1, -1)).rgb);
    float lumaUpRight   = rgb2luma(textureOffset(screenTexture, TexCoords, ivec2( 1,  1)).rgb);
    float lumaUpLeft    = rgb2luma(textureOffset(screenTexture, TexCoords, ivec2(-1,  1)).rgb);
    float lumaDownRight = rgb2luma(textureOffset(screenTexture, TexCoords, ivec2( 1, -1)).rgb);

    float lumaDownUp    = lumaDown + lumaUp;
    float lumaLeftRight = lumaLeft + lumaRight;

    float lumaLeftCorners  = lumaDownLeft  + lumaUpLeft;
    float lumaDownCorners  = lumaDownLeft  + lumaDownRight;
    float lumaRightCorners = lumaDownRight + lumaUpRight;
    float lumaUpCorners    = lumaUpRight   + lumaUpLeft;

    // Estimate gradient along horizontal and vertical directions.
    float edgeHorizontal = abs(-2.0 * lumaLeft   + lumaLeftCorners)  +
                           abs(-2.0 * lumaCenter + lumaDownUp) * 2.0 +
                           abs(-2.0 * lumaRight  + lumaRightCorners);
    float edgeVertical   = abs(-2.0 * lumaUp     + lumaUpCorners)    +
                           abs(-2.0 * lumaCenter + lumaLeftRight) * 2.0 +
                           abs(-2.0 * lumaDown   + lumaDownCorners);

    bool isHorizontal = (edgeHorizontal >= edgeVertical);

    // Luma of the two neighbours across the edge.
    float luma1 = isHorizontal ? lumaDown : lumaLeft;
    float luma2 = isHorizontal ? lumaUp   : lumaRight;
    float gradient1 = luma1 - lumaCenter;
    float gradient2 = luma2 - lumaCenter;

    bool is1Steepest = abs(gradient1) >= abs(gradient2);
    float gradientScaled = 0.25 * max(abs(gradient1), abs(gradient2));

    // One-texel step in the direction perpendicular to the edge.
    float stepLength = isHorizontal ? inverseScreenSize.y : inverseScreenSize.x;

    float lumaLocalAverage = 0.0;
    if (is1Steepest) {
        stepLength = -stepLength;
        lumaLocalAverage = 0.5 * (luma1 + lumaCenter);
    } else {
        lumaLocalAverage = 0.5 * (luma2 + lumaCenter);
    }

    // Shift UV by half a texel toward the edge.
    vec2 currentUv = TexCoords;
    if (isHorizontal) {
        currentUv.y += stepLength * 0.5;
    } else {
        currentUv.x += stepLength * 0.5;
    }

    // Explore along the edge in both directions until the end is reached.
    vec2 offset = isHorizontal ? vec2(inverseScreenSize.x, 0.0)
                               : vec2(0.0, inverseScreenSize.y);
    vec2 uv1 = currentUv - offset;
    vec2 uv2 = currentUv + offset;

    float lumaEnd1 = rgb2luma(texture(screenTexture, uv1).rgb) - lumaLocalAverage;
    float lumaEnd2 = rgb2luma(texture(screenTexture, uv2).rgb) - lumaLocalAverage;

    bool reached1 = abs(lumaEnd1) >= gradientScaled;
    bool reached2 = abs(lumaEnd2) >= gradientScaled;
    bool reachedBoth = reached1 && reached2;

    if (!reached1) uv1 -= offset;
    if (!reached2) uv2 += offset;

    if (!reachedBoth) {
        for (int i = 2; i < ITERATIONS; i++) {
            if (!reached1) {
                lumaEnd1 = rgb2luma(texture(screenTexture, uv1).rgb) - lumaLocalAverage;
            }
            if (!reached2) {
                lumaEnd2 = rgb2luma(texture(screenTexture, uv2).rgb) - lumaLocalAverage;
            }
            reached1 = abs(lumaEnd1) >= gradientScaled;
            reached2 = abs(lumaEnd2) >= gradientScaled;
            reachedBoth = reached1 && reached2;

            if (!reached1) uv1 -= offset * qualityStep(i);
            if (!reached2) uv2 += offset * qualityStep(i);

            if (reachedBoth) break;
        }
    }

    // Distance to each end of the edge.
    float distance1 = isHorizontal ? (TexCoords.x - uv1.x) : (TexCoords.y - uv1.y);
    float distance2 = isHorizontal ? (uv2.x - TexCoords.x) : (uv2.y - TexCoords.y);

    bool isDirection1 = distance1 < distance2;
    float distanceFinal = min(distance1, distance2);
    float edgeThickness = (distance1 + distance2);

    // Pixel offset toward the nearer end of the edge.
    float pixelOffset = -distanceFinal / edgeThickness + 0.5;

    bool isLumaCenterSmaller = lumaCenter < lumaLocalAverage;
    bool correctVariation = ((isDirection1 ? lumaEnd1 : lumaEnd2) < 0.0) != isLumaCenterSmaller;
    float finalOffset = correctVariation ? pixelOffset : 0.0;

    // Sub-pixel anti-aliasing: blend toward the local average for thin features.
    float lumaAverage = (1.0 / 12.0) *
        (2.0 * (lumaDownUp + lumaLeftRight) + lumaLeftCorners + lumaRightCorners);
    float subPixelOffset1 = clamp(abs(lumaAverage - lumaCenter) / lumaRange, 0.0, 1.0);
    float subPixelOffset2 = (-2.0 * subPixelOffset1 + 3.0) * subPixelOffset1 * subPixelOffset1;
    float subPixelOffsetFinal = subPixelOffset2 * subPixelOffset2 * fxaaSubpixel;

    finalOffset = max(finalOffset, subPixelOffsetFinal);

    // Final sample, nudged perpendicular to the edge by the computed offset.
    vec2 finalUv = TexCoords;
    if (isHorizontal) {
        finalUv.y += finalOffset * stepLength;
    } else {
        finalUv.x += finalOffset * stepLength;
    }

    FragColor = vec4(texture(screenTexture, finalUv).rgb, 1.0);
}
