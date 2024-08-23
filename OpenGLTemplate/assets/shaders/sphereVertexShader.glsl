#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;

out vec3 Normal;
out vec3 FragPos;
out vec3 ModelPos;

uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform float innerSphereFactor;

void main()
{
    ModelPos = aPos;
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    
    // Scale the inner sphere
    vec3 scaledPos = aPos * (gl_InstanceID == 1 ? innerSphereFactor : 1.0);
    gl_Position = projection * view * model * vec4(scaledPos, 1.0);
}