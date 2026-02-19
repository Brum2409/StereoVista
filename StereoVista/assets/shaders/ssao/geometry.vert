#version 460
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

out vec3 FragPos;   // View-space position
out vec3 Normal;    // View-space normal

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix; // World-space normal matrix

void main()
{
    // View-space position
    vec4 viewPos = view * model * vec4(aPos, 1.0);
    FragPos = viewPos.xyz;

    // View-space normal: transform world normal into view space
    vec3 worldNormal = normalize(normalMatrix * aNormal);
    Normal = normalize(mat3(view) * worldNormal);

    gl_Position = projection * viewPos;
}
