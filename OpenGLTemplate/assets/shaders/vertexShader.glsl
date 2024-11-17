#version 330 core
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
} vs_out;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform bool isPointCloud;

void main()
{
    vs_out.FragPos = vec3(model * vec4(aPos, 1.0));
    
    if (isPointCloud) {
        // For point clouds, we're using different attribute meanings
        vs_out.VertexColor = aNormal;  // Using normal data to store color for point clouds
        vs_out.Intensity = aTexCoords.x;  // Using texCoord.x to store intensity
        vs_out.Normal = vec3(0.0);  // Not used for point clouds
        vs_out.TexCoords = vec2(0.0);  // Not used for point clouds
        vs_out.TBN = mat3(1.0);  // Not used for point clouds
    } else {
        // For regular models
        vs_out.Normal = transpose(inverse(mat3(model))) * aNormal;
        vs_out.TexCoords = aTexCoords;
        
        // Calculate TBN matrix for normal mapping
        vec3 T = normalize(vec3(model * vec4(aTangent, 0.0)));
        vec3 B = normalize(vec3(model * vec4(aBitangent, 0.0)));
        vec3 N = normalize(vec3(model * vec4(aNormal, 0.0)));
        vs_out.TBN = mat3(T, B, N);
        
        vs_out.VertexColor = vec3(1.0);
        vs_out.Intensity = 1.0;
    }
    
    // Transform vertex position to light space for shadow mapping
    vs_out.FragPosLightSpace = lightSpaceMatrix * vec4(vs_out.FragPos, 1.0);
    
    gl_Position = projection * view * vec4(vs_out.FragPos, 1.0);
}