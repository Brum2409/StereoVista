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

void main()
{             
    vec3 hdrColor = texture(hdrBuffer, TexCoords).rgb;

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
    
    // Gamma correction (sRGB conversion)
    result = linearToSRGB(result);
    
    FragColor = vec4(result, 1.0);
}