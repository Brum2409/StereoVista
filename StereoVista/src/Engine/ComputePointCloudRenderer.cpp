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
        m_rasterShader = new Shader(
            "assets/shaders/core/pointcloud_rasterize.comp", Shader::ComputeShaderTag{});
        m_resolveShader = new Shader(
            "assets/shaders/core/pointcloud_resolve.vert",
            "assets/shaders/core/pointcloud_resolve.frag");
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
    m_locPointBaseSize    = glGetUniformLocation(pid, "uPointBaseSize");
    m_locFieldOfView      = glGetUniformLocation(pid, "uFieldOfView");

    // Cache resolve shader uniform location
    m_resolveShader->use();
    m_locResolveImageSize = glGetUniformLocation(m_resolveShader->getID(), "uImageSize");

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

    m_initialized = true;
    std::cout << "[ComputePC] Initialised (" << width << "×" << height << ")\n";
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
}

void ComputePointCloudRenderer::freeBuffers() {
    if (m_framebufferSSBO) { glDeleteBuffers(1, &m_framebufferSSBO); m_framebufferSSBO = 0; }
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::resize(int width, int height) {
    m_width  = width;
    m_height = height;
    if (m_initialized) allocateBuffers();
}

void ComputePointCloudRenderer::cleanup() {
    freeBuffers();

    delete m_rasterShader;  m_rasterShader  = nullptr;
    delete m_resolveShader; m_resolveShader = nullptr;

    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1,     &m_quadVBO);  m_quadVBO = 0; }

    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::beginFrame() {
    if (!m_initialized) return;

    // Clear the framebuffer SSBO to 0xFFFFFFFFFFFFFFFF (sentinel: no point).
    GLuint clearVal = 0xFFFFFFFFu;
    glClearNamedBufferSubData(m_framebufferSSBO, GL_R32UI,
                              0,
                              m_width * m_height * static_cast<GLsizeiptr>(sizeof(uint64_t)),
                              GL_RED_INTEGER, GL_UNSIGNED_INT,
                              &clearVal);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Bind framebuffer SSBO to slot 1 (matches shader binding)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_framebufferSSBO);
}

// ─────────────────────────────────────────────────────────────────────────────

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
        float pointBaseSize,
        float fieldOfView)
{
    if (!m_initialized || numBatches == 0) return;
    if (!batchSSBO || !xyz4bSSBO || !rgbaSSBO) return;

    // Always re-bind the rasterize shader before setting uniforms.
    // main.cpp calls shader->set*(…) on the *scene* shader between beginFrame()
    // and renderNode().  Those methods use glGetUniformLocation on the scene
    // shader's program ID, but issue glUniform* against the *currently-bound*
    // program (our rasterize shader after beginFrame()).  A colliding location
    // number would silently overwrite one of our uniforms, including uImageSize
    // which is only set here and not anywhere else per-cloud.
    m_rasterShader->use();
    glUniform2i(m_locImageSize, m_width, m_height);

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
    glUniform1f(m_locPointBaseSize,   pointBaseSize);
    glUniform1f(m_locFieldOfView,     fieldOfView);

    // One workgroup per batch (matches Schütz's glDispatchCompute(numBatches,1,1))
    glDispatchCompute(numBatches, 1, 1);
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::endFrame() {
    if (!m_initialized) return;

    // Single barrier after all rasterize dispatches
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // ── Resolve / composite pass ────────────────────────────────────────────
    // Framebuffer SSBO (binding 1) and RGBA SSBO (binding 44) remain bound
    // from the rasterize pass; the fragment shader reads both.

    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthWriteWasOn;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasOn);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    m_resolveShader->use();
    glUniform2i(m_locResolveImageSize, m_width, m_height);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    // Restore depth state
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
    if (depthWriteWasOn) glDepthMask(GL_TRUE);

    // Unbind SSBOs
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER,  1, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 40, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 41, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 42, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 43, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 44, 0);
}

} // namespace Engine
