#version 450 core

layout(location = 0) in vec3 position;
layout(location = 1) in vec3 normal;

uniform mat4 M;
uniform mat4 V;
uniform mat4 P;
uniform int mipmapLevel; // Current mipmap level

out vec3 worldPositionGeom;
out vec3 normalGeom;

void main() {
    // Apply model transformation to get the world position
    worldPositionGeom = vec3(M * vec4(position, 1));
    
    // Transform normal to world space
    normalGeom = normalize(mat3(transpose(inverse(M))) * normal);
    
    // Pass through the position
    gl_Position = P * V * vec4(worldPositionGeom, 1);
}