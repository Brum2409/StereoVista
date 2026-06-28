// Schütz compute rasterizer – Phase 2
// C++ implementation of ComputePointCloudRenderer.
// See header for design notes.
#include "Engine/ComputePointCloudRenderer.h"
#include <glm/gtc/type_ptr.hpp>
#include <iostream>
#include <cstring>

namespace Engine {

// ── Fullscreen quad (NDC positions + UV) ─────────────────────────────────────
// Two triangles (GL_TRIANGLE_STRIP): top-left, bottom-left, top-right, bottom-right
static const float kQuadVerts[] = {
    // pos (NDC)    UV
    -1.0f,  1.0f,  0.0f, 1.0f,
    -1.0f, -1.0f,  0.0f, 0.0f,
     1.0f,  1.0f,  1.0f, 1.0f,
     1.0f, -1.0f,  1.0f, 0.0f,
};

// Max simultaneous point clouds per frame.  Must match the cloudID bit width in
// pointcloud_rasterize.comp / pointcloud_color_lookup.comp (8 bits → 256).
static constexpr uint32_t kMaxComputeClouds = 256;

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::init(int width, int height) {
    m_width  = width;
    m_height = height;

    // Verify 64-bit atomic support (needed by pointcloud_rasterize.comp)
    {
        auto hasExtension = [](const char* name) {
            GLint n = 0;
            glGetIntegerv(GL_NUM_EXTENSIONS, &n);
            for (GLint i = 0; i < n; i++)
                if (strcmp(reinterpret_cast<const char*>(glGetStringi(GL_EXTENSIONS, i)), name) == 0)
                    return true;
            return false;
        };
        bool hasEXT = hasExtension("GL_EXT_shader_atomic_int64");
        bool hasNV  = hasExtension("GL_NV_shader_atomic_int64");
        if (!hasEXT && !hasNV) {
            std::cerr << "[ComputePC] ERROR: Neither GL_EXT_shader_atomic_int64 nor "
                         "GL_NV_shader_atomic_int64 is supported on this GPU/driver. "
                         "Compute point-cloud renderer cannot be used.\n";
            return;
        }
    }

    // Load shaders
    try {
        // Phase 1: software rasterize – one workgroup per batch, atomicMin into SSBO.
        m_rasterShader = new Shader(
            "assets/shaders/core/pointcloud_rasterize.comp", Shader::ComputeShaderTag{});
        // Phase 2: per-pixel colour lookup in compute – much better scatter parallelism
        //          than a fragment shader (16×16 workgroup vs 2×2 quad organisation).
        //          Replaces (depth:32|index:32) with (depth:32|color:32) in the SSBO.
        m_colorLookupShader = new Shader(
            "assets/shaders/core/pointcloud_color_lookup.comp", Shader::ComputeShaderTag{});
        // Phase 3: fullscreen quad that writes gl_FragDepth from the SSBO entry so the
        //          hardware depth test handles mesh occlusion and updates the depth attachment.
        m_resolveShader = new Shader(
            "assets/shaders/core/pointcloud_resolve.vert",
            "assets/shaders/core/pointcloud_resolve.frag");
        // High-Quality Shading (Schütz compute_loop_las_hqs): depth → colour
        // accumulate → resolve. Optional path selected per-frame via setHQS().
        m_hqsDepthShader = new Shader(
            "assets/shaders/core/pointcloud_hqs_depth.comp", Shader::ComputeShaderTag{});
        m_hqsColorShader = new Shader(
            "assets/shaders/core/pointcloud_hqs_color.comp", Shader::ComputeShaderTag{});
        m_hqsResolveShader = new Shader(
            "assets/shaders/core/pointcloud_resolve.vert",
            "assets/shaders/core/pointcloud_hqs_resolve.frag");
    } catch (const std::exception& e) {
        std::cerr << "[ComputePC] Shader load failed: " << e.what() << "\n";
        return;
    }

    allocateBuffers();

    // Cache rasterize shader uniform locations
    m_rasterShader->use();
    GLuint pid = m_rasterShader->getID();
    m_locImageSize        = glGetUniformLocation(pid, "uImageSize");
    m_locMVP              = glGetUniformLocation(pid, "uMVP");
    m_locModelView        = glGetUniformLocation(pid, "uModelView");
    m_locProj             = glGetUniformLocation(pid, "uProj");
    m_locPointsPerThread  = glGetUniformLocation(pid, "uPointsPerThread");
    m_locCloudID          = glGetUniformLocation(pid, "uCloudID");
    m_locSplatMaxRadius   = glGetUniformLocation(pid, "uSplatMaxRadius");
    m_locClipPlaneCount   = glGetUniformLocation(pid, "uClipPlaneCount");
    m_locClipPlanes       = glGetUniformLocation(pid, "uClipPlanes");

    // Cache color-lookup compute uniform location
    m_colorLookupShader->use();
    m_locColorLookupImageSize = glGetUniformLocation(m_colorLookupShader->getID(), "uImageSize");
    m_locLookupCloudID        = glGetUniformLocation(m_colorLookupShader->getID(), "uLookupCloudID");

    // Cache resolve shader uniform locations
    m_resolveShader->use();
    m_locResolveImageSize = glGetUniformLocation(m_resolveShader->getID(), "uImageSize");

    // Cache HQS depth-pass uniform locations
    {
        m_hqsDepthShader->use();
        GLuint id = m_hqsDepthShader->getID();
        m_locHqsDepthMVP             = glGetUniformLocation(id, "uMVP");
        m_locHqsDepthModelView       = glGetUniformLocation(id, "uModelView");
        m_locHqsDepthProj            = glGetUniformLocation(id, "uProj");
        m_locHqsDepthImageSize       = glGetUniformLocation(id, "uImageSize");
        m_locHqsDepthPointsPerThread = glGetUniformLocation(id, "uPointsPerThread");
        m_locHqsDepthSplatMaxRadius  = glGetUniformLocation(id, "uSplatMaxRadius");
        m_locHqsDepthClipCount       = glGetUniformLocation(id, "uClipPlaneCount");
        m_locHqsDepthClipPlanes      = glGetUniformLocation(id, "uClipPlanes");
    }

    // Cache HQS colour-pass uniform locations
    {
        m_hqsColorShader->use();
        GLuint id = m_hqsColorShader->getID();
        m_locHqsColorMVP             = glGetUniformLocation(id, "uMVP");
        m_locHqsColorModelView       = glGetUniformLocation(id, "uModelView");
        m_locHqsColorProj            = glGetUniformLocation(id, "uProj");
        m_locHqsColorImageSize       = glGetUniformLocation(id, "uImageSize");
        m_locHqsColorPointsPerThread = glGetUniformLocation(id, "uPointsPerThread");
        m_locHqsColorSplatMaxRadius  = glGetUniformLocation(id, "uSplatMaxRadius");
        m_locHqsColorClipCount       = glGetUniformLocation(id, "uClipPlaneCount");
        m_locHqsColorClipPlanes      = glGetUniformLocation(id, "uClipPlanes");
        m_locHqsColorThreshold       = glGetUniformLocation(id, "uHqsThreshold");
    }

    // Cache HQS resolve-pass uniform locations
    {
        m_hqsResolveShader->use();
        GLuint id = m_hqsResolveShader->getID();
        m_locHqsResolveImageSize = glGetUniformLocation(id, "uImageSize");
        m_locHqsResolveProjA     = glGetUniformLocation(id, "uProjA");
        m_locHqsResolveProjB     = glGetUniformLocation(id, "uProjB");
    }

    // Build fullscreen quad VAO
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 4 * sizeof(float), (void*)(2 * sizeof(float)));
    glEnableVertexAttribArray(1);
    glBindVertexArray(0);

    // Diagnostic GPU timer queries (split per operation).
    glGenQueries(2, m_qCompute);  // rasterize compute
    glGenQueries(2, m_qClear);    // (empty slot)
    glGenQueries(2, m_qLookup);   // color-lookup compute
    glGenQueries(2, m_qPass2);    // fragment resolve

    m_initialized = true;
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::allocateBuffers() {
    freeBuffers();

    int totalPixels = m_width * m_height;

    // Framebuffer SSBO: one uint64_t per pixel (depth:32 | point_index:32).
    glGenBuffers(1, &m_framebufferSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_framebufferSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 totalPixels * static_cast<GLsizeiptr>(sizeof(uint64_t)),
                 nullptr, GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Pre-clear so the first beginFrame() can dispatch without stalling.
    GLuint clearVal = 0xFFFFFFFFu;
    glClearNamedBufferSubData(m_framebufferSSBO, GL_R32UI,
                              0,
                              totalPixels * static_cast<GLsizeiptr>(sizeof(uint64_t)),
                              GL_RED_INTEGER, GL_UNSIGNED_INT,
                              &clearVal);

    // Per-pixel resolved-colour buffer (one packed RGBA8 per pixel).  No clear
    // needed: every non-sentinel pixel is written by its owning cloud's lookup
    // pass, and sentinel pixels are discarded by the resolve shader.
    glGenBuffers(1, &m_colorbufferSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_colorbufferSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 totalPixels * static_cast<GLsizeiptr>(sizeof(uint32_t)),
                 nullptr, GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // ── High-Quality Shading scratch buffers ─────────────────────────────────
    // Reused across both eyes (cleared at the end of each endFrameHQS()).  Pre-
    // cleared here so the first HQS frame starts clean and so the invariant holds
    // even if HQS is only toggled on later (the standard path never touches them).
    // Nearest linear-depth buffer: one uint per pixel, sentinel 0xFFFFFFFF.
    glGenBuffers(1, &m_hqsDepthSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_hqsDepthSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 totalPixels * static_cast<GLsizeiptr>(sizeof(uint32_t)),
                 nullptr, GL_DYNAMIC_COPY);
    GLuint depthSentinel = 0xFFFFFFFFu;
    glClearNamedBufferSubData(m_hqsDepthSSBO, GL_R32UI, 0,
                              totalPixels * static_cast<GLsizeiptr>(sizeof(uint32_t)),
                              GL_RED_INTEGER, GL_UNSIGNED_INT, &depthSentinel);

    // Colour accumulator: R,G,B,count interleaved → 4 uints per pixel, cleared to 0.
    glGenBuffers(1, &m_hqsAccumSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_hqsAccumSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 totalPixels * 4 * static_cast<GLsizeiptr>(sizeof(uint32_t)),
                 nullptr, GL_DYNAMIC_COPY);
    GLuint zero = 0;
    glClearNamedBufferSubData(m_hqsAccumSSBO, GL_R32UI, 0,
                              totalPixels * 4 * static_cast<GLsizeiptr>(sizeof(uint32_t)),
                              GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void ComputePointCloudRenderer::freeBuffers() {
    if (m_framebufferSSBO) { glDeleteBuffers(1, &m_framebufferSSBO); m_framebufferSSBO = 0; }
    if (m_colorbufferSSBO) { glDeleteBuffers(1, &m_colorbufferSSBO); m_colorbufferSSBO = 0; }
    if (m_hqsDepthSSBO)    { glDeleteBuffers(1, &m_hqsDepthSSBO);    m_hqsDepthSSBO    = 0; }
    if (m_hqsAccumSSBO)    { glDeleteBuffers(1, &m_hqsAccumSSBO);    m_hqsAccumSSBO    = 0; }
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::resize(int width, int height) {
    m_width  = width;
    m_height = height;
    if (m_initialized) allocateBuffers();
}

void ComputePointCloudRenderer::cleanup() {
    freeBuffers();

    delete m_rasterShader;        m_rasterShader        = nullptr;
    delete m_colorLookupShader;   m_colorLookupShader   = nullptr;
    delete m_resolveShader;       m_resolveShader       = nullptr;
    delete m_hqsDepthShader;      m_hqsDepthShader      = nullptr;
    delete m_hqsColorShader;      m_hqsColorShader      = nullptr;
    delete m_hqsResolveShader;    m_hqsResolveShader    = nullptr;

    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1,     &m_quadVBO);  m_quadVBO = 0; }

    if (m_qCompute[0]) { glDeleteQueries(2, m_qCompute); m_qCompute[0] = m_qCompute[1] = 0; }
    if (m_qClear[0])   { glDeleteQueries(2, m_qClear);   m_qClear[0]   = m_qClear[1]   = 0; }
    if (m_qLookup[0])  { glDeleteQueries(2, m_qLookup);  m_qLookup[0]  = m_qLookup[1]  = 0; }
    if (m_qPass2[0])   { glDeleteQueries(2, m_qPass2);   m_qPass2[0]   = m_qPass2[1]   = 0; }

    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::beginFrame() {
    if (!m_initialized) return;

    // Start a fresh per-frame list of cloud rgba buffers.  Each renderNode()
    // call appends one; its slot index becomes that cloud's id.
    m_frameRGBASSBOs.clear();
    // Fresh per-frame list of recorded draws (HQS mode replays them in endFrame).
    m_frameDraws.clear();

    // Framebuffer was already cleared at the end of the previous endFrame().
    // Just bind it – no stall, the compute dispatch can start immediately.
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_framebufferSSBO);

    // Diagnostic: time the compute dispatch(es) issued before endFrame().
    // HQS mode dispatches inside endFrame() instead, so it manages its own (no)
    // timing — skip the query here to keep glBeginQuery/glEndQuery balanced.
    if (!m_hqsEnabled)
        glBeginQuery(GL_TIME_ELAPSED, m_qCompute[m_qIdx]);
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::setHQS(bool enabled, float depthThreshold) {
    m_hqsEnabled   = enabled;
    m_hqsThreshold = glm::max(0.0f, depthThreshold);
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::setClipPlanes(int count, const glm::vec4* worldPlanes) {
    m_clipPlaneCount = glm::clamp(count, 0, 6);
    for (int i = 0; i < m_clipPlaneCount; ++i)
        m_clipPlanesWorld[i] = worldPlanes[i];
}

void ComputePointCloudRenderer::renderNode(
        GLuint batchSSBO,
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
        int      splatMaxRadius)
{
    if (!m_initialized || numBatches == 0) return;
    if (!batchSSBO || !xyz4bSSBO || !rgbaSSBO) return;

    // ── HQS mode: record the draw, defer dispatch to endFrameHQS() ─────────────
    // High-Quality Shading needs ALL clouds' depth passes to finish before ANY
    // colour pass starts (the colour pass reads the global nearest depth). That
    // doesn't fit the single per-cloud loop in main.cpp, so instead of
    // dispatching here we capture the cloud's parameters and replay them across
    // the two passes in endFrameHQS().
    if (m_hqsEnabled) {
        CloudDraw d;
        d.batchSSBO = batchSSBO; d.xyz12b = xyz12bSSBO; d.xyz8b = xyz8bSSBO;
        d.xyz4b = xyz4bSSBO;     d.rgba = rgbaSSBO;
        d.numBatches = numBatches; d.pointsPerThread = pointsPerThread;
        d.splatMaxRadius = splatMaxRadius;
        d.mvp = mvp; d.modelView = modelView; d.proj = proj;
        d.clipCount = m_clipPlaneCount;
        if (m_clipPlaneCount > 0) {
            const glm::mat4 mt = glm::transpose(model);
            for (int i = 0; i < m_clipPlaneCount; ++i)
                d.localClip[i] = mt * m_clipPlanesWorld[i];
        }
        m_frameDraws.push_back(d);
        // All clouds in an eye share the same projection; capture its z-row
        // coefficients so the HQS resolve can reconstruct window depth.
        m_hqsProjA = proj[2][2];
        m_hqsProjB = proj[3][2];
        return;
    }

    // Assign this cloud a stable per-frame id = its slot in the render order.
    // The id is packed into the framebuffer payload so endFrame() can resolve
    // each pixel's colour against the CORRECT cloud's rgba buffer.  Packed into
    // 8 bits → at most kMaxComputeClouds clouds per frame.
    uint32_t cloudID = static_cast<uint32_t>(m_frameRGBASSBOs.size());
    if (cloudID >= kMaxComputeClouds) {
        static bool warned = false;
        if (!warned) {
            std::cerr << "[ComputePC] WARNING: more than " << kMaxComputeClouds
                      << " point clouds in one frame; extra clouds may show wrong "
                         "colours. Widen the cloudID bit budget to fix.\n";
            warned = true;
        }
        cloudID = kMaxComputeClouds - 1;
    }
    m_frameRGBASSBOs.push_back(rgbaSSBO);

    // Always re-bind the rasterize shader before setting uniforms.
    // main.cpp calls shader->set*(…) on the *scene* shader between beginFrame()
    // and renderNode().  Those methods use glGetUniformLocation on the scene
    // shader's program ID, but issue glUniform* against the *currently-bound*
    // program (our rasterize shader after beginFrame()).  A colliding location
    // number would silently overwrite one of our uniforms, including uImageSize
    // which is only set here and not anywhere else per-cloud.
    m_rasterShader->use();
    glUniform2i(m_locImageSize, m_width, m_height);
    glUniform1ui(m_locCloudID, cloudID);
    glUniform1i(m_locSplatMaxRadius, splatMaxRadius);

    // Section/clip planes: transform each active world-space plane into this
    // cloud's local space (localPlane = transpose(model) * worldPlane) so the
    // shader can test it against the decoded local-space points directly.
    glUniform1i(m_locClipPlaneCount, m_clipPlaneCount);
    if (m_clipPlaneCount > 0 && m_locClipPlanes >= 0) {
        const glm::mat4 mt = glm::transpose(model);
        glm::vec4 localPlanes[6];
        for (int i = 0; i < m_clipPlaneCount; ++i)
            localPlanes[i] = mt * m_clipPlanesWorld[i];
        glUniform4fv(m_locClipPlanes, m_clipPlaneCount, glm::value_ptr(localPlanes[0]));
    }

    // Bind packed-coordinate and batch SSBOs (matching shader bindings)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 40, batchSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 41, xyz12bSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 42, xyz8bSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 43, xyz4bSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 44, rgbaSSBO);

    // Per-cloud uniforms
    glUniformMatrix4fv(m_locMVP,       1, GL_FALSE, glm::value_ptr(mvp));
    glUniformMatrix4fv(m_locModelView, 1, GL_FALSE, glm::value_ptr(modelView));
    glUniformMatrix4fv(m_locProj,      1, GL_FALSE, glm::value_ptr(proj));
    glUniform1i(m_locPointsPerThread, pointsPerThread);

    // One workgroup per batch (matches Schütz's glDispatchCompute(numBatches,1,1))
    glDispatchCompute(numBatches, 1, 1);
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::endFrame() {
    if (!m_initialized) return;

    // High-Quality Shading takes a separate 3-pass path (and never opened the
    // compute timer query in beginFrame()).
    if (m_hqsEnabled) { endFrameHQS(); return; }

    // Close compute timer.
    glEndQuery(GL_TIME_ELAPSED);  // ends m_qCompute[m_qIdx]

    // Wait for all compute SSBO writes to be visible to the fragment shaders.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    glBindVertexArray(m_quadVAO);

    // ── TIMED: (empty slot) ──────────────────────────────────────────────────
    glBeginQuery(GL_TIME_ELAPSED, m_qClear[m_qIdx]);
    glEndQuery(GL_TIME_ELAPSED);

    // ── TIMED: Color-lookup compute pass ─────────────────────────────────────
    // Scatter ssRGBA[index] per pixel in compute (much better memory parallelism
    // than a fragment shader's 2×2 quad organisation).  Rewrites the SSBO entry
    // from (depth:32 | index:32) to (depth:32 | packedRGBA8:32) so the fragment
    // resolve can read colour directly without any scatter.
    //
    // Uses a 1D workgroup (256×1×1) so every warp of 32 threads accesses 32
    // consecutive pixels → 256 consecutive bytes → 4 cache lines → fully
    // coalesced.  The previous 16×16 2D layout put two rows per warp (addresses
    // width×8 bytes apart = 15 KB gap for a 1920-wide screen) → non-coalesced.
    //
    // Bindings: 1 (framebuffer SSBO, set in beginFrame) and 44 (ssRGBA, set in
    // renderNode) are still current — no rebind needed.
    glBeginQuery(GL_TIME_ELAPSED, m_qLookup[m_qIdx]);
    m_colorLookupShader->use();
    glUniform2i(m_locColorLookupImageSize, m_width, m_height);
    // Per-pixel resolved-colour output (read later by the resolve pass).
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 45, m_colorbufferSSBO);
    {
        int totalPixels = m_width * m_height;
        int groupsX = (totalPixels + 255) / 256;
        // One lookup dispatch per cloud, each bound to its own rgba buffer.  A
        // pixel is only written by the pass whose id matches the cloud that won
        // that pixel (cloudID stored in the payload), so colours never bleed
        // between clouds.  The passes write disjoint pixels, so no barrier is
        // needed between them — only the single barrier after the loop (below).
        size_t n = m_frameRGBASSBOs.size();
        for (size_t i = 0; i < n && i < kMaxComputeClouds; ++i) {
            glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 44, m_frameRGBASSBOs[i]);
            glUniform1ui(m_locLookupCloudID, static_cast<GLuint>(i));
            glDispatchCompute(groupsX, 1, 1);
        }
    }
    glEndQuery(GL_TIME_ELAPSED);

    // Make the colour-lookup writes visible to the fragment resolve.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // ── TIMED: Fragment resolve pass ─────────────────────────────────────────
    // Reads (depth:32 | packedRGBA8:32) from the SSBO — no scatter, color was
    // already resolved by the color-lookup compute pass above.
    // Writes gl_FragDepth so the hardware depth test (GL_LESS) correctly
    // occludes points behind meshes and updates the depth attachment for the
    // EDL pass and cursor snapping.  Expected cost: ~0.05 ms/eye.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_STENCIL_TEST);

    m_resolveShader->use();
    glUniform2i(m_locResolveImageSize, m_width, m_height);

    glBeginQuery(GL_TIME_ELAPSED, m_qPass2[m_qIdx]);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glEndQuery(GL_TIME_ELAPSED);

    // ── Restore GL state ──────────────────────────────────────────────────────
    glBindVertexArray(0);
    glDisable(GL_STENCIL_TEST);
    glStencilMask(0xFF);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Unbind SSBOs
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,  1, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 40, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 41, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 42, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 43, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 44, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 45, 0);

    // Clear the framebuffer SSBO for next frame while the GPU heads into vsync
    // idle — the next beginFrame() can bind and dispatch without stalling.
    GLuint clearVal = 0xFFFFFFFFu;
    glClearNamedBufferSubData(m_framebufferSSBO, GL_R32UI,
                              0,
                              m_width * m_height * static_cast<GLsizeiptr>(sizeof(uint64_t)),
                              GL_RED_INTEGER, GL_UNSIGNED_INT,
                              &clearVal);
    // Make the clear visible to the next frame's compute dispatch.
    // Without this barrier the driver must infer the SSBO write-after-clear
    // dependency, which causes a periodic pipeline stall every 3-7 frames.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // ── Diagnostic: read previous cycle's timers (no stall) and print ───────────
    if (m_qHavePrev) {
        GLuint64 ct = 0, clr = 0, lk = 0, p2 = 0;
        int prev = m_qIdx ^ 1;
        glGetQueryObjectui64v(m_qCompute[prev], GL_QUERY_RESULT, &ct);
        glGetQueryObjectui64v(m_qClear[prev],   GL_QUERY_RESULT, &clr);
        glGetQueryObjectui64v(m_qLookup[prev],  GL_QUERY_RESULT, &lk);
        glGetQueryObjectui64v(m_qPass2[prev],   GL_QUERY_RESULT, &p2);
        m_accCompute += double(ct)  * 1e-6;  // ns → ms
        m_accClear   += double(clr) * 1e-6;
        m_accLookup  += double(lk)  * 1e-6;
        m_accPass2   += double(p2)  * 1e-6;
        if (++m_accCount >= 120) {
            //double n = m_accCount;
            //std::cout << "[PC GPU] rasterize=" << (m_accCompute/n)
            //          << " ms  lookup=" << (m_accLookup/n)
            //          << " ms  resolve=" << (m_accPass2/n)
            //          << " ms  total=" << ((m_accCompute + m_accLookup + m_accPass2)/n)
            //          << " ms  (avg/eye)\n";
            m_accCompute = m_accClear = m_accLookup = m_accPass2 = 0.0;
            m_accCount = 0;
        }
    }
    m_qIdx ^= 1;
    m_qHavePrev = true;
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::endFrameHQS() {
    // Schütz compute_loop_las_hqs: depth → colour-accumulate → resolve.
    // m_hqsDepthSSBO / m_hqsAccumSSBO are already clean (cleared at allocation and
    // at the end of the previous HQS frame; the standard path never touches them).
    const int totalPixels = m_width * m_height;
    if (m_frameDraws.empty()) {
        // Nothing recorded (all clouds had empty/invalid SSBOs). The scratch
        // buffers are still clean, so just leave the scene framebuffer as-is.
        m_qHavePrev = false;
        return;
    }

    // ── PASS 1: nearest depth (atomicMin linear eye depth per pixel) ──────────
    m_hqsDepthShader->use();
    glUniform2i(m_locHqsDepthImageSize, m_width, m_height);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_hqsDepthSSBO);
    for (const CloudDraw& d : m_frameDraws) {
        glUniformMatrix4fv(m_locHqsDepthMVP,       1, GL_FALSE, glm::value_ptr(d.mvp));
        glUniformMatrix4fv(m_locHqsDepthModelView, 1, GL_FALSE, glm::value_ptr(d.modelView));
        glUniformMatrix4fv(m_locHqsDepthProj,      1, GL_FALSE, glm::value_ptr(d.proj));
        glUniform1i(m_locHqsDepthPointsPerThread, d.pointsPerThread);
        glUniform1i(m_locHqsDepthSplatMaxRadius,  d.splatMaxRadius);
        glUniform1i(m_locHqsDepthClipCount,       d.clipCount);
        if (d.clipCount > 0 && m_locHqsDepthClipPlanes >= 0)
            glUniform4fv(m_locHqsDepthClipPlanes, d.clipCount, glm::value_ptr(d.localClip[0]));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 40, d.batchSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 41, d.xyz12b);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 42, d.xyz8b);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 43, d.xyz4b);
        glDispatchCompute(d.numBatches, 1, 1);
    }
    // The colour pass reads the FINAL nearest depth, so every depth dispatch must
    // be visible first.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // ── PASS 2: colour accumulate (atomicAdd R,G,B,count within depth window) ─
    m_hqsColorShader->use();
    glUniform2i(m_locHqsColorImageSize, m_width, m_height);
    glUniform1f(m_locHqsColorThreshold, m_hqsThreshold);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_hqsDepthSSBO);   // read nearest depth
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_hqsAccumSSBO);   // accumulate
    for (const CloudDraw& d : m_frameDraws) {
        glUniformMatrix4fv(m_locHqsColorMVP,       1, GL_FALSE, glm::value_ptr(d.mvp));
        glUniformMatrix4fv(m_locHqsColorModelView, 1, GL_FALSE, glm::value_ptr(d.modelView));
        glUniformMatrix4fv(m_locHqsColorProj,      1, GL_FALSE, glm::value_ptr(d.proj));
        glUniform1i(m_locHqsColorPointsPerThread, d.pointsPerThread);
        glUniform1i(m_locHqsColorSplatMaxRadius,  d.splatMaxRadius);
        glUniform1i(m_locHqsColorClipCount,       d.clipCount);
        if (d.clipCount > 0 && m_locHqsColorClipPlanes >= 0)
            glUniform4fv(m_locHqsColorClipPlanes, d.clipCount, glm::value_ptr(d.localClip[0]));
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 40, d.batchSSBO);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 41, d.xyz12b);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 42, d.xyz8b);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 43, d.xyz4b);
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 44, d.rgba);
        glDispatchCompute(d.numBatches, 1, 1);
    }
    // Make the accumulator visible to the fragment resolve.
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // ── PASS 3: resolve (fullscreen quad → averaged colour + gl_FragDepth) ────
    // Same depth/colour-mask state as the standard resolve so the hardware
    // GL_LESS test occludes points behind meshes and updates the depth buffer.
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);
    glDisable(GL_STENCIL_TEST);

    m_hqsResolveShader->use();
    glUniform2i(m_locHqsResolveImageSize, m_width, m_height);
    glUniform1f(m_locHqsResolveProjA, m_hqsProjA);
    glUniform1f(m_locHqsResolveProjB, m_hqsProjB);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, m_hqsDepthSSBO);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, m_hqsAccumSSBO);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    // ── Restore GL state ──────────────────────────────────────────────────────
    glStencilMask(0xFF);
    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LESS);

    // Unbind SSBOs
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,  2, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,  3, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 40, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 41, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 42, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 43, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 44, 0);

    // Clear both scratch buffers for the next frame (mirrors the standard path's
    // end-of-frame clear so the next beginFrame can start without a stall).
    GLuint depthSentinel = 0xFFFFFFFFu;
    glClearNamedBufferSubData(m_hqsDepthSSBO, GL_R32UI, 0,
                              totalPixels * static_cast<GLsizeiptr>(sizeof(uint32_t)),
                              GL_RED_INTEGER, GL_UNSIGNED_INT, &depthSentinel);
    GLuint zero = 0;
    glClearNamedBufferSubData(m_hqsAccumSSBO, GL_R32UI, 0,
                              totalPixels * 4 * static_cast<GLsizeiptr>(sizeof(uint32_t)),
                              GL_RED_INTEGER, GL_UNSIGNED_INT, &zero);
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // HQS frames don't use the GL_TIME_ELAPSED queries; tell the next standard
    // frame not to read a (never-written) previous result.
    m_qHavePrev = false;
}

} // namespace Engine
