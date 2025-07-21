#version 450 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec4 VoxelColor;

uniform vec3 viewPos;
uniform float opacity;
uniform float colorIntensity;
uniform int visualizationMode; // 0 = normal, 1 = luminance, 2 = alpha, 3 = emissive

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
        // Alpha visualization - show alpha channel as grayscale
        baseColor = vec3(VoxelColor.a);
    }
    else if (visualizationMode == 3) {
        // Emissive visualization - show voxels that contribute to lighting
        // Use the luminance as an indicator of emissive contribution
        float emissive = calculateLuminance(baseColor);
        baseColor = vec3(emissive * 2.0); // Boost emissive visualization
    }

    // Final color - make voxels fully opaque for better debugging
    vec3 result = baseColor;
    FragColor = vec4(result, 1.0);
}