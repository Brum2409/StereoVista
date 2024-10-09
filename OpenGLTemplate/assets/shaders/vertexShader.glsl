#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

out vec3 FragPos;
out vec3 Normal;
out vec2 TexCoords;
out mat3 TBN;
out vec3 VertexColor;
out float Intensity;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform bool isPointCloud;

void main()
{
    FragPos = vec3(model * vec4(aPos, 1.0));
    
    if (isPointCloud) {
    VertexColor = aNormal;
    Intensity = aTexCoords.x;  

    } else {
        Normal = mat3(transpose(inverse(model))) * aNormal;  
        TexCoords = aTexCoords;
        
        vec3 T = normalize(vec3(model * vec4(aTangent, 0.0)));
        vec3 B = normalize(vec3(model * vec4(aBitangent, 0.0)));
        vec3 N = normalize(vec3(model * vec4(aNormal, 0.0)));
        TBN = mat3(T, B, N);
        
        VertexColor = vec3(1.0);  // Default color for non-point cloud vertices
        Intensity = 1.0;  // Default intensity for non-point cloud vertices
    }
    
    gl_Position = projection * view * vec4(FragPos, 1.0);
}