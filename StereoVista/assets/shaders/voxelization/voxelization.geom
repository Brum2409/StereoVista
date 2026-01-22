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
uniform vec3 gridCenter;      // Grid center position in world space
uniform int voxelResolution;  // Voxel grid resolution for dilation calculation

// Dilate a triangle in 2D to ensure conservative rasterization
// This expands each edge outward by half a pixel to cover all voxels the triangle touches
vec2 dilateVertex(vec2 v0, vec2 v1, vec2 v2, int vertexIndex, float pixelSize) {
    // Get the current vertex and its two neighbors
    vec2 current, prev, next;
    if (vertexIndex == 0) {
        current = v0; prev = v2; next = v1;
    } else if (vertexIndex == 1) {
        current = v1; prev = v0; next = v2;
    } else {
        current = v2; prev = v1; next = v0;
    }

    // Compute edge vectors
    vec2 edge1 = current - prev;
    vec2 edge2 = next - current;

    // Compute outward-facing edge normals (perpendicular to edges)
    // For CCW winding, rotating 90 degrees clockwise gives outward normal
    vec2 normal1 = normalize(vec2(edge1.y, -edge1.x));
    vec2 normal2 = normalize(vec2(edge2.y, -edge2.x));

    // Average the two normals to get the vertex displacement direction
    vec2 displacement = normal1 + normal2;
    float len = length(displacement);

    // Handle degenerate case where normals cancel out
    if (len < 0.001) {
        displacement = normal1;
    } else {
        displacement = displacement / len;
    }

    // Scale displacement by half a pixel
    // We use a slightly larger factor (0.5 * sqrt(2)) to ensure corner coverage
    float dilationAmount = pixelSize * 0.7071; // 0.5 * sqrt(2)

    return current + displacement * dilationAmount;
}

void main() {
    // Determine the dominant axis based on face normal
    vec3 e0 = worldPositionGeom[1] - worldPositionGeom[0];
    vec3 e1 = worldPositionGeom[2] - worldPositionGeom[0];
    vec3 faceNormal = cross(e0, e1);
    vec3 absFaceNormal = abs(faceNormal);

    // Scale factor to map world coordinates to [-1, 1] range
    float scale = 2.0 / gridSize;

    // Pixel size in NDC space (viewport is voxelResolution x voxelResolution)
    float pixelSize = 2.0 / float(voxelResolution);

    // Project vertices based on dominant axis and apply dilation
    vec2 projected[3];

    if (absFaceNormal.x >= absFaceNormal.y && absFaceNormal.x >= absFaceNormal.z) {
        // Project onto YZ plane (X is dominant)
        for (int i = 0; i < 3; ++i) {
            vec3 p = (worldPositionGeom[i] - gridCenter) * scale;
            projected[i] = vec2(p.y, p.z);
        }

        // Dilate the triangle
        vec2 dilated[3];
        for (int i = 0; i < 3; ++i) {
            dilated[i] = dilateVertex(projected[0], projected[1], projected[2], i, pixelSize);
        }

        // Emit dilated vertices
        for (int i = 0; i < 3; ++i) {
            worldPositionFrag = worldPositionGeom[i];
            normalFrag = normalGeom[i];
            texCoordFrag = texCoordGeom[i];
            gl_Position = vec4(dilated[i], 0.0, 1.0);
            EmitVertex();
        }
        EndPrimitive();

    } else if (absFaceNormal.y >= absFaceNormal.z) {
        // Project onto XZ plane (Y is dominant)
        for (int i = 0; i < 3; ++i) {
            vec3 p = (worldPositionGeom[i] - gridCenter) * scale;
            projected[i] = vec2(p.x, p.z);
        }

        // Dilate the triangle
        vec2 dilated[3];
        for (int i = 0; i < 3; ++i) {
            dilated[i] = dilateVertex(projected[0], projected[1], projected[2], i, pixelSize);
        }

        // Emit dilated vertices
        for (int i = 0; i < 3; ++i) {
            worldPositionFrag = worldPositionGeom[i];
            normalFrag = normalGeom[i];
            texCoordFrag = texCoordGeom[i];
            gl_Position = vec4(dilated[i], 0.0, 1.0);
            EmitVertex();
        }
        EndPrimitive();

    } else {
        // Project onto XY plane (Z is dominant)
        for (int i = 0; i < 3; ++i) {
            vec3 p = (worldPositionGeom[i] - gridCenter) * scale;
            projected[i] = vec2(p.x, p.y);
        }

        // Dilate the triangle
        vec2 dilated[3];
        for (int i = 0; i < 3; ++i) {
            dilated[i] = dilateVertex(projected[0], projected[1], projected[2], i, pixelSize);
        }

        // Emit dilated vertices
        for (int i = 0; i < 3; ++i) {
            worldPositionFrag = worldPositionGeom[i];
            normalFrag = normalGeom[i];
            texCoordFrag = texCoordGeom[i];
            gl_Position = vec4(dilated[i], 0.0, 1.0);
            EmitVertex();
        }
        EndPrimitive();
    }
}