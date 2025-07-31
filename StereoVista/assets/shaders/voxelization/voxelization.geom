#version 450 core

layout(triangles) in;
layout(triangle_strip, max_vertices = 9) out; // 3 vertices * 3 projections max

in vec3 worldPositionGeom[];
in vec3 normalGeom[];
in vec2 texCoordGeom[];

out vec3 worldPositionFrag;
out vec3 normalFrag;
out vec2 texCoordFrag;

uniform float gridSize;

void main() {
    // Determine the dominant axis based on face normal
    vec3 v0 = worldPositionGeom[1] - worldPositionGeom[0];
    vec3 v1 = worldPositionGeom[2] - worldPositionGeom[0];
    vec3 faceNormal = normalize(cross(v0, v1));
    vec3 absFaceNormal = abs(faceNormal);
    
    // Scale factor to map world coordinates to [-1, 1] range
    float scale = 2.0 / gridSize;
    
    // Choose the projection axis based on the largest component of the face normal
    if(absFaceNormal.x >= absFaceNormal.y && absFaceNormal.x >= absFaceNormal.z) {
        // Project onto X-axis (YZ plane)
        for(int i = 0; i < 3; ++i) {
            worldPositionFrag = worldPositionGeom[i];
            normalFrag = normalGeom[i];
            texCoordFrag = texCoordGeom[i];
            
            vec3 projectedPos = worldPositionFrag * scale;
            gl_Position = vec4(projectedPos.y, projectedPos.z, 0.0, 1.0);
            EmitVertex();
        }
        EndPrimitive();
    } else if(absFaceNormal.y >= absFaceNormal.z) {
        // Project onto Y-axis (XZ plane)
        for(int i = 0; i < 3; ++i) {
            worldPositionFrag = worldPositionGeom[i];
            normalFrag = normalGeom[i];
            texCoordFrag = texCoordGeom[i];
            
            vec3 projectedPos = worldPositionFrag * scale;
            gl_Position = vec4(projectedPos.x, projectedPos.z, 0.0, 1.0);
            EmitVertex();
        }
        EndPrimitive();
    } else {
        // Project onto Z-axis (XY plane)
        for(int i = 0; i < 3; ++i) {
            worldPositionFrag = worldPositionGeom[i];
            normalFrag = normalGeom[i];
            texCoordFrag = texCoordGeom[i];
            
            vec3 projectedPos = worldPositionFrag * scale;
            gl_Position = vec4(projectedPos.x, projectedPos.y, 0.0, 1.0);
            EmitVertex();
        }
        EndPrimitive();
    }
}