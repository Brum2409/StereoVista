#version 460

// Single fullscreen triangle from gl_VertexIndex — no vertex buffer. Shared
// by every fullscreen resolve/post pass. Draw with vkCmdDraw(cmd, 3, 1, 0, 0)
// and cull mode NONE.

void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    gl_Position = vec4(pos * 2.0 - 1.0, 0.0, 1.0);
}
