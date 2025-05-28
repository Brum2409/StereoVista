#version 450 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec4 VoxelColor;

uniform vec3 viewPos;
uniform float opacity;
uniform float colorIntensity;
uniform int visualizationMode; // 0 = normal, 1 = luminance, 2 = alpha

// Calculate luminance from RGB using the standard formula
float calculateLuminance(vec3 color) {
    return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

void main() {
    // Base color calculation
    vec3 baseColor = VoxelColor.rgb * colorIntensity;
    
    // Apply visualization mode
    if (visualizationMode == 1) {
        // Luminance visualization
        float luminance = calculateLuminance(baseColor);
        baseColor = vec3(luminance);
    }
    else if (visualizationMode == 2) {
        // Alpha visualization
        baseColor = vec3(VoxelColor.a);
    }

    // Final color
    vec3 result = baseColor;
    
    // Apply opacity and final color
    FragColor = vec4(result, VoxelColor.a * opacity);
}