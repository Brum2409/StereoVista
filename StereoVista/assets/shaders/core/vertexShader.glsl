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
    // Normal matrix is now passed as a uniform from C++ for efficiency
    
    // Calculate fragment position in world space
    vs_out.FragPos = vec3(model * vec4(aPos, 1.0));
    
    if (isPointCloud) {
        // Point cloud specific attributes
        vs_out.VertexColor = aNormal;         // Using normal data for color
        vs_out.Intensity = aTexCoords.x;      // Using texCoord.x for intensity
        vs_out.Normal = vec3(0.0);            // Not used for point clouds
        vs_out.TexCoords = vec2(0.0);         // Not used for point clouds
        vs_out.TBN = mat3(1.0);               // Not used for point clouds
    } else {
        // Regular model attributes (LearnOpenGL style)
        vs_out.Normal = normalMatrix * aNormal;
        vs_out.TexCoords = aTexCoords;
        
        // Calculate tangent space basis vectors for normal mapping
        vec3 T = normalize(normalMatrix * aTangent);
        vec3 B = normalize(normalMatrix * aBitangent);
        vec3 N = normalize(vs_out.Normal);
        
        // Re-orthogonalize T with respect to N using Gram-Schmidt process
        T = normalize(T - dot(T, N) * N);
        // Then re-orthogonalize B with respect to N and T
        B = normalize(B - dot(B, N) * N - dot(B, T) * T);
        
        vs_out.TBN = mat3(T, B, N);
        vs_out.VertexColor = vec3(1.0);
        vs_out.Intensity = 1.0;
        vs_out.meshIndex = currentMeshIndex;
    }
    
    // Calculate light space position for shadow mapping (regardless of lighting mode)
    // This ensures we always have this data available even if shadow mapping is toggled on later
    vs_out.FragPosLightSpace = lightSpaceMatrix * vec4(vs_out.FragPos, 1.0);
    
    // Final position calculation
    gl_Position = projection * view * vec4(vs_out.FragPos, 1.0);
}