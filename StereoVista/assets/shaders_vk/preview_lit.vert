#version 460
#extension GL_EXT_scalar_block_layout : require

// CursorPreview3D reference cube (Phase 6): tiny self-contained lit pass for
// the settings-panel thumbnail. Push constants only — no descriptors.
// rotCols carry the rotation-only model columns in xyz; their w components
// smuggle the camera position (fits the 128-byte push budget exactly).

layout(push_constant, scalar) uniform Push {
    mat4 mvp;
    vec4 rotCol0; // xyz rotation column 0, w = viewPos.x
    vec4 rotCol1; // xyz rotation column 1, w = viewPos.y
    vec4 rotCol2; // xyz rotation column 2, w = viewPos.z
    vec4 baseColor;
} pc;

layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;

layout(location = 0) out vec3 vWorldPos;
layout(location = 1) out vec3 vNormal;

void main() {
    mat3 rot = mat3(pc.rotCol0.xyz, pc.rotCol1.xyz, pc.rotCol2.xyz);
    vWorldPos = rot * aPos;
    vNormal = rot * aNormal;
    gl_Position = pc.mvp * vec4(aPos, 1.0);
}
