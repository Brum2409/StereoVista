#version 330 core
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
    vec4 FragPosLightSpace;
    vec3 VertexColor;
    float Intensity;
    mat3 TBN;
} vs_out;

// Transformation matrices
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;

// Render states
uniform bool isPointCloud;
uniform bool useInstancing;

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
        vs_out.TBN = mat3(1.0);               // Not used for point clouds
    } else {
        // Regular model attributes
        vs_out.Normal = normalMatrix * aNormal;
        vs_out.TexCoords = aTexCoords;
        
        // Calculate tangent space basis vectors for normal mapping
        vec3 T = normalize(normalMatrix * aTangent);
        vec3 B = normalize(normalMatrix * aBitangent);
        vec3 N = normalize(vs_out.Normal);
        
        // Re-orthogonalize T with respect to N
        T = normalize(T - dot(T, N) * N);
        
        // Calculate B as cross product to ensure orthogonal basis
        B = cross(N, T);
        
        vs_out.TBN = mat3(T, B, N);
        vs_out.VertexColor = vec3(1.0);
        vs_out.Intensity = 1.0;
    }
    
    // Transform position to light space for shadow mapping
    vs_out.FragPosLightSpace = lightSpaceMatrix * vec4(vs_out.FragPos, 1.0);
    
    // Final position calculation
    gl_Position = projection * view * vec4(vs_out.FragPos, 1.0);
}