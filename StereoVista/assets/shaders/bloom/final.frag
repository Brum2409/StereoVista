#version 460 core
out vec4 FragColor;

in vec2 TexCoords;

uniform sampler2D hdrBuffer;
uniform sampler2D bloomBlur;
uniform bool enableBloom;
uniform float bloomIntensity;
uniform float exposure;
uniform int toneMapOperator; // 0=Reinhard, 1=ACES, 2=Uncharted2, 3=AgX, 4=Khronos PBR Neutral, 5=Tony McMapface

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

// 2. ACES Filmic Tone Mapping (Industry standard, used by many AAA games)
// Based on Stephen Hill's implementation
vec3 acesToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // ACES RRT/ODT fit by Stephen Hill
    // https://github.com/TheRealMJP/BakingLab/blob/master/BakingLab/ACES.hlsl
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    
    return clamp((hdrColor * (a * hdrColor + b)) / (hdrColor * (c * hdrColor + d) + e), 0.0, 1.0);
}

// 3. Uncharted 2 Filmic Tone Mapping (John Hable's implementation)
// Used in Uncharted 2, very popular in games
vec3 uncharted2ToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // John Hable's filmic operator
    const float A = 0.15; // Shoulder Strength
    const float B = 0.50; // Linear Strength
    const float C = 0.10; // Linear Angle
    const float D = 0.20; // Toe Strength
    const float E = 0.02; // Toe Numerator
    const float F = 0.30; // Toe Denominator
    
    vec3 numerator = ((hdrColor * (A * hdrColor + C * B) + D * E));
    vec3 denominator = (hdrColor * (A * hdrColor + B) + D * F);
    vec3 mapped = numerator / denominator - E / F;
    
    // White scale
    const float W = 11.2; // Linear White Point Value
    vec3 whiteScale = vec3(1.0) / (((W * (A * W + C * B) + D * E)) / (W * (A * W + B) + D * F) - E / F);
    
    return mapped * whiteScale;
}

// 4. AgX Tone Mapping (Modern, perceptually accurate)
// Based on Blender's AgX implementation
vec3 agxToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // AgX constants
    const mat3 agx_mat = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );
    
    const mat3 agx_mat_inv = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0918716309140477, -0.0918604049019608, 1.15131639628864
    );
    
    // Apply AgX transform
    hdrColor = agx_mat * hdrColor;
    
    // Log2 encoding
    hdrColor = clamp(log2(hdrColor), -10.0, 10.0);
    
    // Apply curve
    hdrColor = (hdrColor + 10.0) / 20.0;
    
    // S-curve approximation
    hdrColor = hdrColor * hdrColor * (3.0 - 2.0 * hdrColor);
    
    // Convert back
    hdrColor = 2.0 * hdrColor - 1.0;
    hdrColor = pow(vec3(2.0), hdrColor);
    
    // Apply inverse transform
    hdrColor = agx_mat_inv * hdrColor;
    
    return clamp(hdrColor, 0.0, 1.0);
}

// 5. Khronos PBR Neutral Tone Mapping (glTF reference implementation)
// Modern, neutral tone mapping for PBR workflows
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
    return mix(hdrColor, newPeak * vec3(1.0), g);
}

// 6. Tony McMapface (Modern, perceptually motivated)
// Based on research by Tomasz Stachowiak
vec3 tonyMcMapfaceToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // Constants for the tone mapping curve
    const float c_r = 0.36;
    const float s = 0.25;
    const float m = 0.11;
    const float a = 0.004;
    const float c_b = 0.14;
    
    // Luminance-based processing
    float luma = luminance(hdrColor);
    
    // Toe
    float toe = exp(-(luma / a));
    
    // Shoulder  
    float shoulder = exp(-(luma - c_r) / s);
    shoulder = c_r + s * log(1.0 + shoulder);
    
    // Blend between toe and shoulder
    float t = clamp((luma - a) / (c_r - a), 0.0, 1.0);
    t = smoothstep(0.0, 1.0, t);
    
    float mapped_luma = mix(luma * toe, shoulder, t);
    
    // Preserve color ratios
    vec3 result = hdrColor * (mapped_luma / max(luma, 1e-6));
    
    return clamp(result, 0.0, 1.0);
}

void main()
{             
    vec3 hdrColor = texture(hdrBuffer, TexCoords).rgb;
    
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