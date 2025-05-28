#version 450 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

// Instance data
layout (location = 2) in vec3 aInstancePos;
layout (location = 3) in vec4 aInstanceColor;

out vec3 Normal;
out vec3 FragPos;
out vec4 VoxelColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float voxelSize;

void main() {
    // Calculate voxel position and size
    // aInstancePos already comes in at the correct world position
    vec3 scaledPos = aPos * voxelSize; // Scale the unit cube to voxel size
    vec3 worldPos = scaledPos + aInstancePos; // Final world position
    
    Normal = mat3(transpose(inverse(model))) * aNormal;
    FragPos = worldPos;
    VoxelColor = aInstanceColor;
    
    gl_Position = projection * view * vec4(worldPos, 1.0);
}