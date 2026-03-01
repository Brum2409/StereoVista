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

    // Load shaders (clear.comp is no longer needed – we use glClearNamedBufferSubData)
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
    GLuint pid      = m_rasterShader->getID();
    m_locImageSize     = glGetUniformLocation(pid, "uImageSize");
    m_locNumPoints     = glGetUniformLocation(pid, "uNumPoints");
    m_locMVP           = glGetUniformLocation(pid, "uMVP");
    m_locPointBaseSize = glGetUniformLocation(pid, "uPointBaseSize");
    m_locFieldOfView   = glGetUniformLocation(pid, "uFieldOfView");

    // Cache resolve shader uniform locations
    m_resolveShader->use();
    m_locResolveImageSize = glGetUniformLocation(m_resolveShader->getID(), "uImageSize");

    // Build fullscreen quad VAO
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(kQuadVerts), kQuadVerts, GL_STATIC_DRAW);
    // location 0 → pos (vec2)
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);
    // location 1 → uv (vec2)
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE,
                          4 * sizeof(float), (void*)(2 * sizeof(float)));
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
    // Matches Schütz's ssFramebuffer exactly.
    glGenBuffers(1, &m_framebufferSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_framebufferSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 totalPixels * static_cast<GLsizeiptr>(sizeof(uint64_t)),
                 nullptr, GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void ComputePointCloudRenderer::freeBuffers() {
    if (m_framebufferSSBO) { glDeleteBuffers(1, &m_framebufferSSBO); m_framebufferSSBO = 0; }
    if (m_colorsSSBO)      { glDeleteBuffers(1, &m_colorsSSBO);      m_colorsSSBO      = 0; }
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
    // glClearNamedBufferSubData is faster than a compute clear shader
    // (single driver call, no shader dispatch overhead).
    // We clear as GL_R32UI in two passes (high and low 32-bit halves) since
    // OpenGL has no 64-bit clear format. Using 0xFFFFFFFF for both halves.
    GLuint clearVal = 0xFFFFFFFFu;
    glClearNamedBufferSubData(m_framebufferSSBO, GL_R32UI,
                              0,
                              m_width * m_height * static_cast<GLsizeiptr>(sizeof(uint64_t)),
                              GL_RED_INTEGER, GL_UNSIGNED_INT,
                              &clearVal);

    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // Bind framebuffer SSBO to slot 1 (matches shader binding)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_framebufferSSBO);

    // Bind rasterize shader once for the whole frame
    m_rasterShader->use();
    glUniform2i(m_locImageSize, m_width, m_height);
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::renderNode(GLuint vbo,
                                           uint32_t numPoints,
                                           const glm::mat4& mvp,
                                           float pointBaseSize,
                                           float fieldOfView) {
    if (!m_initialized || numPoints == 0 || vbo == 0) return;

    // Allocate (or reallocate) the per-point colour SSBO if needed.
    // The colour SSBO is sized to the current cloud; reuse it if large enough.
    if (m_colorsSSBO == 0 || m_colorsSSBOCapacity < numPoints) {
        if (m_colorsSSBO) glDeleteBuffers(1, &m_colorsSSBO);
        glGenBuffers(1, &m_colorsSSBO);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_colorsSSBO);
        glBufferData(GL_SHADER_STORAGE_BUFFER,
                     numPoints * static_cast<GLsizeiptr>(sizeof(GLuint)),
                     nullptr, GL_DYNAMIC_COPY);
        m_colorsSSBOCapacity = numPoints;
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    }

    // Bind point data as SSBO slot 0 (VBOs can be used as SSBOs in OpenGL 4.6)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vbo);
    // Bind per-point colour output buffer at slot 44 (matches Schütz's ssColors)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 44, m_colorsSSBO);

    // Set per-cloud uniforms via cached locations (zero string lookups)
    glUniformMatrix4fv(m_locMVP,          1, GL_FALSE, glm::value_ptr(mvp));
    glUniform1f(m_locPointBaseSize, pointBaseSize);
    glUniform1f(m_locFieldOfView,   fieldOfView);
    glUniform1ui(m_locNumPoints,    numPoints);

    // ONE dispatch covers the entire point cloud – matching Schütz's
    // glDispatchCompute(numBatches, 1, 1) where numBatches = ceil(N / groupSize).
    int groups = (static_cast<int>(numPoints) + 127) / 128;
    glDispatchCompute(static_cast<GLuint>(groups), 1, 1);

    // No per-cloud barrier – endFrame() issues a single barrier after all clouds.
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::endFrame() {
    if (!m_initialized) return;

    // Single barrier after all rasterize dispatches
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

    // ── Resolve / composite pass ────────────────────────────────────────────
    // Framebuffer SSBO (binding 1) and colours SSBO (binding 44) remain bound
    // from the rasterize pass; the fragment shader reads both.

    // Save and disable depth test so we composite over the existing scene
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

    // Unbind SSBOs to avoid hazards in subsequent render passes
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0,  0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1,  0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 44, 0);
}

} // namespace Engine
