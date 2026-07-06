#version 460
#extension GL_EXT_scalar_block_layout : require

// CursorPreview3D reference cube — two-light Phong with a fresnel rim,
// ported from the GL preview's embedded kLitFS so the thumbnail look is
// unchanged. Output is display-ready color (the preview target is sampled
// by ImGui directly, no tonemap).

layout(push_constant, scalar) uniform Push {
    mat4 mvp;
    vec4 rotCol0;
    vec4 rotCol1;
    vec4 rotCol2;
    vec4 baseColor;
} pc;

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;

layout(location = 0) out vec4 outColor;

void main() {
    vec3 viewPos = vec3(pc.rotCol0.w, pc.rotCol1.w, pc.rotCol2.w);
    vec3 N = normalize(vNormal);
    vec3 V = normalize(viewPos - vWorldPos);
    vec3 L1 = normalize(vec3(0.6, 0.85, 0.55));
    vec3 L2 = normalize(vec3(-0.55, 0.25, 0.45));
    float d1 = max(dot(N, L1), 0.0);
    float d2 = max(dot(N, L2), 0.0) * 0.4;
    vec3 H = normalize(L1 + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * 0.25;
    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.22;
    vec3 color = pc.baseColor.rgb * (0.28 + d1 + d2) + vec3(spec) + vec3(fres);
    outColor = vec4(color, 1.0);
}
