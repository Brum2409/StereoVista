#version 460
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;


out VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec4 FragPosLightSpace;
    vec3 VertexColor;
    float Intensity;
    mat3 TBN;
    flat int meshIndex;
} vs_out;

// Transformation matrices
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform mat3 normalMatrix;

// Lighting mode constants
const int LIGHTING_SHADOW_MAPPING = 0;
const int LIGHTING_VOXEL_CONE_TRACING = 1;

// Render states
uniform bool isPointCloud;
uniform int lightingMode;
uniform int currentMeshIndex;

void main() {
    // Normal matrix is provided via uniform

    // World-space position
    vs_out.FragPos = vec3(model * vec4(aPos, 1.0));

    if (isPointCloud) {
        // Point cloud: pack color/intensity in attributes
        vs_out.VertexColor = aNormal;         // pack normal as color
        vs_out.Intensity = aTexCoords.x;      // intensity from texcoord.x
        vs_out.Normal = vec3(0.0);            // not used
        vs_out.TexCoords = vec2(0.0);         // not used
        vs_out.TBN = mat3(1.0);               // not used
    } else {
        // Mesh attributes
        vs_out.Normal = normalMatrix * aNormal;
        vs_out.TexCoords = aTexCoords;

        // TBN for normal mapping
        vec3 T = normalize(normalMatrix * aTangent);
        vec3 B = normalize(normalMatrix * aBitangent);
        vec3 N = normalize(vs_out.Normal);

        // Re-orthogonalize T and B (Gram-Schmidt)
        T = normalize(T - dot(T, N) * N);
        B = normalize(B - dot(B, N) * N - dot(B, T) * T);

        vs_out.TBN = mat3(T, B, N);
        vs_out.VertexColor = vec3(1.0);
        vs_out.Intensity = 1.0;
        vs_out.meshIndex = currentMeshIndex;
    }

    // Light-space position for shadow mapping
    vs_out.FragPosLightSpace = lightSpaceMatrix * vec4(vs_out.FragPos, 1.0);

    // Clip-space position
    gl_Position = projection * view * vec4(vs_out.FragPos, 1.0);
}
