#version 460
#extension GL_EXT_multiview : require

// House rules for every scene shader: camera data is a per-view array indexed
// by gl_ViewIndex (multiview; mono renders view 0 only). Projections are
// reverse-Z with Vulkan [0,1] depth and Y-flip baked in (Renderer/Projection.h).

layout(set = 0, binding = 0) uniform ViewUniforms {
    mat4 viewProj[2];
    vec4 cameraPos[2];
} uView;

layout(location = 0) out vec3 vColor;

const vec3 kPositions[3] = vec3[](
    vec3( 0.0,  0.8, 0.0),
    vec3(-0.8, -0.5, 0.0),
    vec3( 0.8, -0.5, 0.0)
);

const vec3 kColors[3] = vec3[](
    vec3(0.95, 0.35, 0.30),
    vec3(0.35, 0.85, 0.45),
    vec3(0.30, 0.50, 0.95)
);

void main() {
    gl_Position = uView.viewProj[gl_ViewIndex] * vec4(kPositions[gl_VertexIndex], 1.0);
    vColor = kColors[gl_VertexIndex];
}
