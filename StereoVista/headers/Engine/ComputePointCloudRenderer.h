#pragma once
// Schütz compute rasterizer – Phase 2
// Implements the software rasterization approach from:
//   m-schuetz/CudaLOD  shaders/compute_tiles_draw.cs
//   m-schuetz/compute_rasterizer README
//
// Instead of GL_POINTS (one draw call per visible node via the fixed-function
// pipeline), this renderer:
//   1. Allocates a per-pixel uint depth SSBO and an r32ui colour image.
//   2. Each frame: dispatches a clear compute shader, then for every visible
//      octree node dispatches a rasterize compute shader that does
//      atomicMin(depthbuffer[px], depth) + imageAtomicExchange(colorImage, px, rgba).
//   3. Composites the result into the currently-bound HDR framebuffer via a
//      fullscreen quad fragment shader.
#include "shader.h"
#include <glm/glm.hpp>
#include <glad/glad.h>

namespace Engine {

class ComputePointCloudRenderer {
public:
    ComputePointCloudRenderer()  = default;
    ~ComputePointCloudRenderer() { cleanup(); }

    // Call once after the OpenGL context is ready.
    void init(int width, int height);

    // Call when the window / viewport is resized.
    void resize(int width, int height);

    // Release all GPU resources and shaders.
    void cleanup();

    // ── Per-frame API ────────────────────────────────────────────────────────

    // Clear the depth SSBO (to 0xFFFFFFFF) and colour image (to 0).
    // Must be called before any renderNode() calls for this frame.
    void beginFrame();

    // Rasterize one octree node.
    //   vbo       – GL buffer holding PointCloudPoint data (7 floats / 28 bytes per point)
    //   numPoints – number of points in that buffer
    //   mvp       – combined model-view-projection matrix for this point cloud
    // Can be called many times per frame (once per visible node).
    void renderNode(GLuint vbo, uint32_t numPoints, const glm::mat4& mvp);

    // Wait for all compute work to finish, then composite the point cloud colour
    // image over the currently-bound framebuffer with a fullscreen quad.
    // Call this after all renderNode() calls for the frame, while the HDR FBO
    // is still bound.
    void endFrame();

    bool isInitialized() const { return m_initialized; }

private:
    void allocateBuffers();
    void freeBuffers();

    bool m_initialized = false;
    int  m_width  = 0;
    int  m_height = 0;

    // GPU resources
    GLuint m_depthSSBO    = 0;   // uint[width*height] – per-pixel depth
    GLuint m_colorTexture = 0;   // r32ui GL_TEXTURE_2D – per-pixel packed RGBA

    // Shaders
    Shader* m_clearShader   = nullptr;   // pointcloud_clear.comp
    Shader* m_rasterShader  = nullptr;   // pointcloud_rasterize.comp
    Shader* m_resolveShader = nullptr;   // pointcloud_resolve.vert/frag

    // Fullscreen quad for the resolve pass
    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;
};

} // namespace Engine
