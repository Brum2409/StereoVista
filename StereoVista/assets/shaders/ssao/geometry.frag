#version 460
layout (location = 0) out vec4 gPosition;
layout (location = 1) out vec3 gNormal;

in vec3 FragPos;   // View-space position
in vec3 Normal;    // View-space normal

void main()
{
    // Store view-space position (use w=1 to mark valid fragments)
    gPosition = vec4(FragPos, 1.0);

    // Store normalized view-space normal
    gNormal = normalize(Normal);
}
