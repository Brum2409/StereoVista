#version 450 core
layout (location = 0) in vec3 aPos;

// Instance data
layout (location = 2) in vec3 aInstancePos;
layout (location = 3) in vec4 aInstanceColor;
layout (location = 4) in float aMipmapLevel;

out vec3 Normal;
out vec3 FragPos;
out vec4 VoxelColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float baseVoxelSize;  // Base size for level 0 voxels
uniform int resolution;

void main() {
    // Calculate voxel size based on mipmap level (2^level scaling as per NVIDIA paper)
    float mipmapScale = pow(2.0, aMipmapLevel);
    float levelVoxelSize = baseVoxelSize * mipmapScale;
    
    // Calculate voxel position and size
    vec3 scaledPos = aPos * levelVoxelSize; // Scale the unit cube to appropriate level size
    vec3 worldPos = scaledPos + aInstancePos; // Final world position
    
    // Calculate normal from vertex position (since it's a cube)
    Normal = normalize(aPos);
    FragPos = worldPos;
    VoxelColor = aInstanceColor;
    
    gl_Position = projection * view * model * vec4(worldPos, 1.0);
}