#version 460
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

// Instance data (if needed)
layout (location = 5) in mat4 instanceMatrix;

out VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec3 VertexColor;
    float Intensity;
    flat int meshIndex;
} vs_out;

// Transformation matrices
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Lighting mode constants
const int LIGHTING_SHADOW_MAPPING = 0;
const int LIGHTING_VOXEL_CONE_TRACING = 1;
const int LIGHTING_RADIANCE = 2;

// Render states
uniform bool isPointCloud;
uniform bool useInstancing;
uniform int currentMeshIndex;

void main() {
    // Calculate model matrix based on whether we're using instancing
    mat4 finalModel = useInstancing ? instanceMatrix : model;
    mat3 normalMatrix = transpose(inverse(mat3(finalModel)));
    
    // Calculate fragment position in world space
    vs_out.FragPos = vec3(finalModel * vec4(aPos, 1.0));
    
    if (isPointCloud) {
        // Point cloud specific attributes
        vs_out.VertexColor = aNormal;         // Using normal data for color
        vs_out.Intensity = aTexCoords.x;      // Using texCoord.x for intensity
        vs_out.Normal = vec3(0.0);            // Not used for point clouds
        vs_out.TexCoords = vec2(0.0);         // Not used for point clouds
    } else {
        // Regular model attributes
        vs_out.Normal = normalize(normalMatrix * aNormal);
        vs_out.TexCoords = aTexCoords;
        vs_out.VertexColor = vec3(1.0);
        vs_out.Intensity = 1.0;
        vs_out.meshIndex = currentMeshIndex;
    }
    
    // Final position calculation
    gl_Position = projection * view * vec4(vs_out.FragPos, 1.0);
}