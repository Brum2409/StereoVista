#version 450 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec4 VoxelColor;

uniform vec3 viewPos;
uniform float opacity;
uniform float colorIntensity;

void main() {
    
    // Final color
    vec3 baseColor = VoxelColor.rgb * colorIntensity;
    vec3 result = baseColor;

    
    // Apply opacity and final color
    FragColor = vec4(result, 1);
}