#pragma once
// Schütz compute rasterizer – Phase 2
// Implements the software rasterization approach from:
//   m-schuetz/compute_rasterizer  modules/compute_loop_las/
//
// Per-frame pipeline:
//   1. beginFrame() – bind the pre-cleared framebuffer SSBO (no stall).
//   2. renderNode() – for each point cloud, dispatch one workgroup per batch.
//      Each workgroup tests its batch bounding box against the 6 frustum planes
//      (Schütz / Gribb-Hartmann) and returns early if fully outside.
//      Inside batches are decoded from packed 10/20/30-bit coordinates and
//      rasterised into the uint64_t framebuffer via atomicMin.
//   3. endFrame() – GL_ALL_BARRIER_BITS, fullscreen-quad resolve pass, then
//      clears the framebuffer for the next frame while the GPU is idle.
#include "Shader.h"
#include <glm/glm.hpp>
#include <glad/glad.h>
#include <cstdint>
#include <vector>

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

    // Set the active world-space section/clip planes for this frame. Each plane
    // is vec4(unitNormal.xyz, d) with d = -dot(n, pointOnPlane); points on the
    // clipped side (dot(n, worldP) + d < 0) are discarded. renderNode()
    // transforms these into each cloud's local space. Pass count == 0 to clip
    // nothing. Call before the renderNode() calls for the frame.
    void setClipPlanes(int count, const glm::vec4* worldPlanes);

    // Rasterize one point cloud using the Schütz batch-based compute path.
    //
    //   batchSSBO        – binding 40: ComputeBatch descriptor array
    //   xyz12bSSBO       – binding 41: finest packed coord tier  (10 bits/axis)
    //   xyz8bSSBO        – binding 42: middle packed coord tier
    //   xyz4bSSBO        – binding 43: coarsest packed coord tier (always read)
    //   rgbaSSBO         – binding 44: pre-packed uint RGBA per point
    //   numBatches       – number of workgroups to dispatch (one per batch)
    //   pointsPerThread  – ceil(kComputeBatchSize / 128)
    //   mvp              – proj * view * model  (projection + frustum culling)
    //   modelView        – view * model         (precision-level sphere projection)
    //   proj             – projection matrix    (precision-level sphere projection)
    //   splatMaxRadius   – 0 = single-pixel rasterize (off); >0 = max adaptive
    //                      round-splat radius in pixels (fills close-up/sparse gaps)
    //   model            – the cloud's model matrix, used to transform the
    //                       active world-space clip planes into local space
    void renderNode(GLuint batchSSBO,
                    GLuint xyz12bSSBO,
                    GLuint xyz8bSSBO,
                    GLuint xyz4bSSBO,
                    GLuint rgbaSSBO,
                    uint32_t numBatches,
                    int      pointsPerThread,
                    const glm::mat4& mvp,
                    const glm::mat4& modelView,
                    const glm::mat4& proj,
                    const glm::mat4& model,
                    int      splatMaxRadius = 0);

    // Wait for all compute work to finish, then composite the point cloud result
    // over the currently-bound HDR framebuffer with a fullscreen quad.
    // Call this after all renderNode() calls for the frame, while the HDR FBO
    // is still bound.  The resolve shader writes gl_FragDepth so the hardware
    // depth test (GL_LESS) discards points behind meshes and the depth buffer
    // is updated for the EDL pass.
    void endFrame();

    bool isInitialized() const { return m_initialized; }

private:
    void allocateBuffers();
    void freeBuffers();

    bool m_initialized = false;
    int  m_width  = 0;
    int  m_height = 0;

    // Framebuffer SSBO: uint64_t[width*height] – packed (depth:24 | cloudID:8 | index:32).
    // 24-bit depth (hardware-buffer precision) drives the atomicMin sort; 8-bit
    // cloud id (≤256 clouds) selects the colour buffer; 32-bit index addresses up
    // to ~4.29B points per cloud.  Based on Schütz's ssFramebuffer (binding 1).
    GLuint m_framebufferSSBO = 0;

    // Per-pixel resolved colour (packed RGBA8, binding 45).  Written by the
    // per-cloud colour-lookup passes, read by the resolve pass.  Kept separate
    // from the framebuffer so the (depth|cloudID|index) payload survives every
    // per-cloud lookup pass.
    GLuint m_colorbufferSSBO = 0;

    // RGBA SSBO of every cloud rasterised this frame, in render order.  The index
    // into this vector is the cloud id packed into the framebuffer payload, so
    // endFrame() runs one colour-lookup pass per cloud against its own buffer.
    std::vector<GLuint> m_frameRGBASSBOs;

    // Shaders (no clear shader – cleared via glClearNamedBufferSubData)
    Shader* m_rasterShader        = nullptr;  // pointcloud_rasterize.comp   – software rasterize
    Shader* m_colorLookupShader   = nullptr;  // pointcloud_color_lookup.comp – per-pixel scatter
    Shader* m_resolveShader       = nullptr;  // pointcloud_resolve.vert/frag  – depth+colour write

    // Fullscreen quad for the resolve pass
    GLuint m_quadVAO = 0;
    GLuint m_quadVBO = 0;

    // Cached rasterize-shader uniform locations (queried once at init)
    GLint m_locImageSize         = -1;
    GLint m_locMVP               = -1;
    GLint m_locModelView         = -1;
    GLint m_locProj              = -1;
    GLint m_locPointsPerThread   = -1;
    GLint m_locCloudID           = -1;   // uCloudID in the rasterize shader
    GLint m_locSplatMaxRadius    = -1;   // uSplatMaxRadius in the rasterize shader
    GLint m_locClipPlaneCount    = -1;   // uClipPlaneCount in the rasterize shader
    GLint m_locClipPlanes        = -1;   // uClipPlanes[0] in the rasterize shader

    // Active world-space section/clip planes for the current frame (set via
    // setClipPlanes); transformed into each cloud's local space in renderNode.
    int       m_clipPlaneCount = 0;
    glm::vec4 m_clipPlanesWorld[6] = {};

    // Cached color-lookup compute shader uniform location
    GLint m_locColorLookupImageSize  = -1;
    GLint m_locLookupCloudID         = -1;   // uLookupCloudID in the color-lookup shader

    // Cached resolve-shader (pass 2) uniform locations
    GLint m_locResolveImageSize  = -1;

    // ── GPU timing instrumentation (diagnostic; prints to console) ────────────
    // Four ping-ponged GL_TIME_ELAPSED queries measure each GPU operation
    // separately so we can see which one is the bottleneck.
    GLuint m_qCompute[2] = {0, 0};  // rasterize compute dispatch(es)
    GLuint m_qClear[2]   = {0, 0};  // (empty slot, kept for consistent output)
    GLuint m_qLookup[2]  = {0, 0};  // color-lookup compute dispatch
    GLuint m_qPass2[2]   = {0, 0};  // fragment resolve (depth+colour quad)
    int    m_qIdx        = 0;
    bool   m_qHavePrev   = false;
    double m_accCompute  = 0.0;
    double m_accClear    = 0.0;
    double m_accLookup   = 0.0;
    double m_accPass2    = 0.0;
    int    m_accCount    = 0;
};

} // namespace Engine
