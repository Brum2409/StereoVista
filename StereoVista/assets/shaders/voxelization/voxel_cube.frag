#version 450 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec4 VoxelColor;

uniform vec3 viewPos;
uniform float opacity;
uniform float colorIntensity;
uniform int visualizationMode; // 0=normal, 1=luminance, 2=alpha, 3=emissive

// Luminance (Rec. 709)
float calculateLuminance(vec3 color) {
    return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

void main() {
    // Base color
    vec3 baseColor = VoxelColor.rgb * colorIntensity;
    
    // Visualization mode
    if (visualizationMode == 1) {
        // Luminance
        float luminance = calculateLuminance(baseColor);
        baseColor = vec3(luminance);
    }
    else if (visualizationMode == 2) {
        // Alpha as grayscale
        baseColor = vec3(VoxelColor.a);
    }
    else if (visualizationMode == 3) {
        // Emissive (scaled luminance)
        float emissive = calculateLuminance(baseColor);
        baseColor = vec3(emissive * 2.0);
    }

    // Output color (opaque)
    vec3 result = baseColor;
    FragColor = vec4(result, 1.0);
}