#version 460 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D hdrBuffer;
uniform sampler2D bloomBlur;
uniform sampler2D ssaoTexture;
uniform bool enableBloom;
uniform bool enableSSAO;
uniform float bloomIntensity;
uniform float exposure;
uniform int toneMapOperator; // 0=Reinhard, 1=ACES, 2=Uncharted2, 3=AgX, 4=Khronos PBR Neutral, 5=Tony McMapface

// ---- Color grading (applied after tone mapping, before gamma) ----
uniform float cgContrast;   // 1.0 = neutral
uniform float cgSaturation; // 1.0 = neutral, 0.0 = grayscale
uniform bool  enableVignette;
uniform float vignetteIntensity; // corner darkening amount [0..1]
uniform float vignetteRadius;    // distance (0=center, 1=corner) where falloff starts
uniform float vignetteSoftness;  // falloff width

// ---- Schütz Phase 1: Eye-Dome Lighting uniforms ----
uniform sampler2D depthTexture;
uniform bool  enableEDL;
uniform float edlStrength;   // darkness at depth edges (default 1.0)
uniform float edlRadius;     // neighbourhood radius in pixels (default 1.5)
uniform float edlNear;       // near-plane distance
uniform float edlFar;        // far-plane distance
uniform vec2  edlTexelSize;  // 1 / viewport_size

// KEY FIXES:
// - AgX: Simplified to avoid excessive brightness from complex matrix operations
// - Tony McMapface: Reverted to luminance-based approach to prevent gray appearance 
// - Uncharted 2: Fixed constants and white scale calculation
// - Khronos PBR Neutral: Updated to official specification

// ---- Utility Functions ----
vec3 linearToSRGB(vec3 color) {
    return pow(color, vec3(1.0/2.2));
}

float luminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// ---- Tone Mapping Functions ----

// 1. Reinhard Tone Mapping (Classic, simple)
vec3 reinhardToneMapping(vec3 hdrColor, float exposure) {
    vec3 mapped = hdrColor * exposure;
    return mapped / (1.0 + mapped);
}

// 2. ACES Filmic Tone Mapping (Narkowicz 2015 - Industry standard)
// From Krzysztof Narkowicz's blog and used in UE4/UE5
vec3 acesToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // ACES approximation by Narkowicz
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    
    return clamp((hdrColor * (a * hdrColor + b)) / (hdrColor * (c * hdrColor + d) + e), 0.0, 1.0);
}

// 3. Uncharted 2 Filmic Tone Mapping (John Hable's implementation)
// Exact values from John Hable's blog post
vec3 uncharted2ToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // John Hable's exact constants
    const float A = 0.15; // Shoulder Strength
    const float B = 0.50; // Linear Strength  
    const float C = 0.10; // Linear Angle
    const float D = 0.20; // Toe Strength
    const float E = 0.02; // Toe Numerator
    const float F = 0.30; // Toe Denominator
    const float W = 11.2; // Linear White Point Value
    
    // Apply tone mapping function
    vec3 mapped = ((hdrColor * (A * hdrColor + C * B) + D * E)) / (hdrColor * (A * hdrColor + B) + D * F) - E / F;
    
    // White scale calculation  
    vec3 whiteScale = vec3(1.0) / (((vec3(W) * (A * vec3(W) + C * B) + D * E)) / (vec3(W) * (A * vec3(W) + B) + D * F) - E / F);
    
    return mapped * whiteScale;
}

// 4. AgX Tone Mapping (Simple approximation)
// Simplified AgX implementation that avoids excessive brightness
vec3 agxToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // Simple AgX approximation without the complex matrix operations
    // that can cause excessive brightness
    hdrColor = max(hdrColor, 0.0);
    
    // Apply a gentle logarithmic curve that mimics AgX characteristics
    vec3 result = log2(1.0 + hdrColor);
    result = result / (result + 1.0);
    
    // Apply a subtle S-curve for contrast
    result = result * result * (3.0 - 2.0 * result);
    
    return clamp(result, 0.0, 1.0);
}

// 5. Khronos PBR Neutral Tone Mapping (Official glTF implementation)
// From the official Khronos specification
vec3 khronosPbrNeutralToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    
    float x = min(hdrColor.r, min(hdrColor.g, hdrColor.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    hdrColor -= offset;
    
    float peak = max(hdrColor.r, max(hdrColor.g, hdrColor.b));
    if (peak < startCompression) return hdrColor;
    
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    hdrColor *= newPeak / peak;
    
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(hdrColor, vec3(newPeak), g);
}

// 6. Tony McMapface (Luminance-based approach)
// Based on research by Tomasz Stachowiak - luminance preserving
vec3 tonyMcMapfaceToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // Constants for the tone mapping curve
    const float c_r = 0.36;
    const float s = 0.25;
    const float m = 0.11;
    const float a = 0.004;
    const float c_b = 0.14;
    
    // Luminance-based processing (as in original)
    float luma = luminance(hdrColor);
    
    if (luma <= 0.0) return vec3(0.0);
    
    // Toe section
    float toe = exp(-luma / a);
    
    // Shoulder section
    float shoulder = exp(-(luma - c_r) / s);
    shoulder = c_r + s * log(1.0 + shoulder);
    
    // Blend between toe and shoulder based on luminance
    float t = clamp((luma - a) / (c_r - a), 0.0, 1.0);
    t = smoothstep(0.0, 1.0, t);
    
    float mapped_luma = mix(luma * toe, shoulder, t);
    
    // Preserve color ratios while applying luminance mapping
    vec3 result = hdrColor * (mapped_luma / max(luma, 1e-6));
    
    return clamp(result, 0.0, 1.0);
}

// ---- Schütz Phase 1: Eye-Dome Lighting helper ----
// Returns a shade factor in [0,1] based on the depth-edge response at TexCoords.
// Implementation follows the original EDL algorithm:
//   Boucheny et al. (2008) "A multiscale interactive focus+context technique
//   based on an eye-dome lighting model"
// and Schütz's Potree adaptation.
// Linearize an OpenGL depth-buffer value d ∈ [0,1] → positive view-space depth.
// Standard formula: linear = near*far / (far - d*(far-near))
float edlLinearize(float d) {
    return edlNear * edlFar / (edlFar - d * (edlFar - edlNear));
}

float computeEDL() {
    float d = texture(depthTexture, TexCoords).r;
    if (d >= 1.0) return 1.0; // background – no shading

    float linearD = edlLinearize(d);
    float logD = log(max(linearD, 1e-6));

    // 8-neighbourhood sampling at radius edlRadius pixels
    const vec2 dirs[8] = vec2[8](
        vec2( 1.0,  0.0), vec2(-1.0,  0.0),
        vec2( 0.0,  1.0), vec2( 0.0, -1.0),
        vec2( 0.707,  0.707), vec2(-0.707,  0.707),
        vec2( 0.707, -0.707), vec2(-0.707, -0.707)
    );
    float response = 0.0;
    for (int i = 0; i < 8; ++i) {
        float nd = texture(depthTexture,
                           TexCoords + dirs[i] * edlTexelSize * edlRadius).r;
        if (nd >= 1.0) nd = d; // treat background as same depth → no response
        float logNd = log(max(edlLinearize(nd), 1e-6));
        response += max(0.0, logD - logNd);
    }
    response /= 8.0;
    return exp(-edlStrength * response);
}

void main()
{
    vec3 hdrColor = texture(hdrBuffer, TexCoords).rgb;

    // Schütz Phase 1: Eye-Dome Lighting – applied before any other post-process
    if (enableEDL) {
        hdrColor *= computeEDL();
    }

    // Apply SSAO - darken occluded areas before tone mapping
    if(enableSSAO) {
        float ao = texture(ssaoTexture, TexCoords).r;
        hdrColor *= ao;
    }

    // Add bloom if enabled
    if(enableBloom) {
        vec3 bloomColor = texture(bloomBlur, TexCoords).rgb;
        hdrColor += bloomColor * bloomIntensity; // additive blending
    }
    
    // Apply tone mapping
    vec3 result;
    if (toneMapOperator == 0) {
        result = reinhardToneMapping(hdrColor, exposure);
    } else if (toneMapOperator == 1) {
        result = acesToneMapping(hdrColor, exposure);
    } else if (toneMapOperator == 2) {
        result = uncharted2ToneMapping(hdrColor, exposure);
    } else if (toneMapOperator == 3) {
        result = agxToneMapping(hdrColor, exposure);
    } else if (toneMapOperator == 4) {
        result = khronosPbrNeutralToneMapping(hdrColor, exposure);
    } else if (toneMapOperator == 5) {
        result = tonyMcMapfaceToneMapping(hdrColor, exposure);
    } else {
        result = acesToneMapping(hdrColor, exposure); // Default to ACES (industry standard)
    }
    
    // Color grading: saturation and contrast in tone-mapped [0,1] space.
    // Defaults (1.0 / 1.0) leave the image untouched.
    result = mix(vec3(luminance(result)), result, cgSaturation);
    result = clamp(mix(vec3(0.5), result, cgContrast), 0.0, 1.0);

    // Vignette: smooth radial darkening towards the corners
    if (enableVignette) {
        // Normalize distance so 0 = screen center, 1 = corner
        float dist = length(TexCoords - 0.5) * 1.41421356;
        float falloff = smoothstep(vignetteRadius,
                                   vignetteRadius + max(vignetteSoftness, 1e-4),
                                   dist);
        result *= 1.0 - falloff * vignetteIntensity;
    }

    // Gamma correction (sRGB conversion)
    result = linearToSRGB(result);

    FragColor = vec4(result, 1.0);
}