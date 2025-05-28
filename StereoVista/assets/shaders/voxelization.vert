#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;
layout(location = 2) in vec2 texCoord;  // Added texture coordinates input

uniform mat4 M;
uniform mat4 V;
uniform mat4 P;
uniform int mipmapLevel;

out vec3 worldPositionGeom;
out vec3 normalGeom;
out vec2 texCoordGeom;     // Added texture coordinates output

void main() {
    // Transform position to world space
    worldPositionGeom = vec3(M * vec4(position, 1));
    
    // Transform normal to world space
    normalGeom = normalize(mat3(transpose(inverse(M))) * normal);
    
    // Pass texture coordinates to geometry shader
    texCoordGeom = texCoord;
    
    // Set position for rendering
    gl_Position = P * V * vec4(worldPositionGeom, 1);
}