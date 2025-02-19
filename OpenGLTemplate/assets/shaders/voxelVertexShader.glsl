#version 450 core
layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoords;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform vec3 cameraPos;
uniform float voxelGridSize;

out vec3 worldPositionGeom;
out vec3 normalGeom;
out vec2 texCoordsGeom;

void main() {
    // Transform to world space
    worldPositionGeom = vec3(model * vec4(position, 1.0));
    
    // Center grid around camera
    worldPositionGeom = worldPositionGeom - cameraPos;
    
    // Scale to voxel grid space (-1 to 1)
    worldPositionGeom = worldPositionGeom / (voxelGridSize * 0.5);
    
    normalGeom = normalize(mat3(transpose(inverse(model))) * normal);
    texCoordsGeom = texCoords;
    
    gl_Position = projection * view * vec4(worldPositionGeom, 1.0);
}