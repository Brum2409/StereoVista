#version 460

// Fullscreen triangle at depth 0 (reverse-Z far plane). Drawn AFTER the
// opaque pass with GREATER_OR_EQUAL depth test (no write): only background
// pixels survive. Passes NDC xy through for ray reconstruction.

layout(location = 0) out vec2 vNdc;

void main() {
    vec2 pos = vec2((gl_VertexIndex << 1) & 2, gl_VertexIndex & 2);
    vNdc = pos * 2.0 - 1.0;
    gl_Position = vec4(vNdc, 0.0, 1.0);
}
