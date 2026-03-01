#pragma once
// Schütz compute rasterizer – Phase 2
// Implements the software rasterization approach from:
//   m-schuetz/compute_rasterizer  modules/compute_loop_las/
//
// Per-frame pipeline:
//   1. beginFrame() – clear the uint64_t framebuffer SSBO via
//      glClearNamedBufferSubData (one driver call, no shader dispatch).
//   2. renderNode() – for each point cloud, one glDispatchCompute covers ALL
//      points.  The rasterize shader packs (depth:32 | point_index:32) into
//      each pixel via atomicMin, and writes per-point packed RGBA into ssColors.
//   3. endFrame() – single glMemoryBarrier, then a fullscreen-quad resolve pass
//      reads the framebuffer SSBO + ssColors to composite the result into the
//      currently-bound HDR FBO.
#include "shader.h"
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <cstdint>

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

    // Clear the uint64_t framebuffer SSBO to 0xFFFF…FF (sentinel: no point).
    // Must be called before any renderNode() calls for this frame.
    void beginFrame();

    // Rasterize one point cloud with a single compute dispatch.
    //   vbo           – GL buffer holding PointCloudPoint data (7 floats / 28 bytes per point)
    //   numPoints     – total number of points in that buffer
    //   mvp           – combined model-view-projection matrix for this point cloud
    //   pointBaseSize – world-space splat radius in metres
    //   fieldOfView   – vertical FOV in radians
    void renderNode(GLuint vbo, uint32_t numPoints, const glm::mat4& mvp,
                    float pointBaseSize, float fieldOfView);

    // Wait for all compute work to finish, then composite the point cloud result
    // over the currently-bound HDR framebuffer with a fullscreen quad.
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
    // Framebuffer SSBO: uint64_t[width*height] – packed (depth:32 | index:32).
    // Matches Schütz's ssFramebuffer (binding 1).
    GLuint m_framebufferSSBO = 0;

    // Per-point colour SSBO: uint[numPoints] – packed ABGR.
    // Matches Schütz's ssColors (binding 44).
    // Allocated lazily in renderNode(); grown as needed.
    GLuint   m_colorsSSBO         = 0;
    uint32_t m_colorsSSBOCapacity = 0;

    // Shaders (no clear shader – cleared via glClearNamedBufferSubData)
    Shader* m_rasterShader  = nullptr;   // pointcloud_rasterize.comp
    Shader* m_resolveShader = nullptr;   // pointcloud_resolve.vert/frag

    // Fullscreen quad for the resolve pass
    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;

    // Cached rasterize-shader uniform locations (queried once at init)
    GLint m_locImageSize      = -1;
    GLint m_locNumPoints      = -1;
    GLint m_locMVP            = -1;
    GLint m_locPointBaseSize  = -1;
    GLint m_locFieldOfView    = -1;

    // Cached resolve-shader uniform location
    GLint m_locResolveImageSize = -1;
};

} // namespace Engine
