#version 450 core
layout (location = 0) in vec3 aPos;

// Instance attributes
layout (location = 2) in vec3 aInstancePos;
layout (location = 3) in vec4 aInstanceColor;
layout (location = 4) in float aMipmapLevel;

out vec3 Normal;
out vec3 FragPos;
out vec4 VoxelColor;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float baseVoxelSize;  // size at level 0
uniform int resolution;

void main() {
    // Voxel size scales by 2^level
    float mipmapScale = pow(2.0, aMipmapLevel);
    float levelVoxelSize = baseVoxelSize * mipmapScale;
    
    // Position and size
    vec3 scaledPos = aPos * levelVoxelSize; // scale unit cube
    vec3 worldPos = scaledPos + aInstancePos;
    
    // Cube normal from position
    Normal = normalize(aPos);
    FragPos = worldPos;
    VoxelColor = aInstanceColor;
    
    gl_Position = projection * view * model * vec4(worldPos, 1.0);
}