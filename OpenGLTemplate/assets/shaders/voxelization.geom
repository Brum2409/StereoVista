#version 450 core

layout(triangles) in;
layout(triangle_strip, max_vertices = 3) out;

in vec3 worldPositionGeom[];
in vec3 normalGeom[];

out vec3 worldPositionFrag;
out vec3 normalFrag;

void main(){
    // Determine dominant axis for projection
    const vec3 p1 = worldPositionGeom[1] - worldPositionGeom[0];
    const vec3 p2 = worldPositionGeom[2] - worldPositionGeom[0];
    const vec3 p = abs(cross(p1, p2)); 
    
    for(uint i = 0; i < 3; ++i){
        // Pass through world position and normal unchanged
        worldPositionFrag = worldPositionGeom[i];
        normalFrag = normalGeom[i];
        
        // Choose projection plane based on dominant axis
        if(p.z > p.x && p.z > p.y){
            // Project onto XY plane
            gl_Position = vec4(worldPositionFrag.x, worldPositionFrag.y, 0, 1);
        } else if (p.x > p.y && p.x > p.z){
            // Project onto YZ plane
            gl_Position = vec4(worldPositionFrag.y, worldPositionFrag.z, 0, 1);
        } else {
            // Project onto XZ plane
            gl_Position = vec4(worldPositionFrag.x, worldPositionFrag.z, 0, 1);
        }
        EmitVertex();
    }
    EndPrimitive();
}