#version 450 core

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in vec3 worldPositionGeom[];
in vec3 normalGeom[];
in vec2 texCoordGeom[];

out vec3 worldPositionFrag;
out vec3 normalFrag;
out vec2 texCoordFrag;

uniform float gridSize;

void main() {
    // Calculate face normal for dominant axis selection
    vec3 v0 = worldPositionGeom[1] - worldPositionGeom[0];
    vec3 v1 = worldPositionGeom[2] - worldPositionGeom[0];
    vec3 faceNormal = abs(normalize(cross(v0, v1)));
    
    // Find dominant axis
    int axis = 2; // default to Z-axis
    if(faceNormal.x > faceNormal.y && faceNormal.x > faceNormal.z) {
        axis = 0; // X-axis dominant
    } else if(faceNormal.y > faceNormal.z) {
        axis = 1; // Y-axis dominant
    }
    
    // Scale factor to map world coordinates to [-1, 1] range
    float scale = 2.0 / gridSize;
    
    for(int i = 0; i < 3; ++i) {
        worldPositionFrag = worldPositionGeom[i];
        normalFrag = normalGeom[i];
        texCoordFrag = texCoordGeom[i];
        
        // Project triangle onto the dominant axis plane
        vec3 projectedPos = worldPositionFrag * scale; // Scale to [-1, 1]
        
        if(axis == 0) {
            // Project onto YZ plane (X is dominant)
            gl_Position = vec4(projectedPos.y, projectedPos.z, 0.0, 1.0);
        } else if(axis == 1) {
            // Project onto XZ plane (Y is dominant)  
            gl_Position = vec4(projectedPos.x, projectedPos.z, 0.0, 1.0);
        } else {
            // Project onto XY plane (Z is dominant)
            gl_Position = vec4(projectedPos.x, projectedPos.y, 0.0, 1.0);
        }
        
        EmitVertex();
    }
    EndPrimitive();
}