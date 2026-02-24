// Schütz compute rasterizer – Phase 2
// C++ implementation of ComputePointCloudRenderer.
// See header for design notes.
#include "Engine/ComputePointCloudRenderer.h"
#include <iostream>

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

    // Load compute shaders
    try {
        m_clearShader  = new Shader(
            "assets/shaders/core/pointcloud_clear.comp", true);
        m_rasterShader = new Shader(
            "assets/shaders/core/pointcloud_rasterize.comp", true);
        m_resolveShader = new Shader(
            "assets/shaders/core/pointcloud_resolve.vert",
            "assets/shaders/core/pointcloud_resolve.frag");
    } catch (const std::exception& e) {
        std::cerr << "[ComputePC] Shader load failed: " << e.what() << "\n";
        return;
    }

    allocateBuffers();

    // Cache rasterize shader uniform locations (avoids per-frame string lookup)
    m_rasterShader->use();
    m_locImageSize = glGetUniformLocation(m_rasterShader->getID(), "uImageSize");
    m_locNumPoints = glGetUniformLocation(m_rasterShader->getID(), "uNumPoints");

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

    // Depth SSBO: one uint per pixel
    glGenBuffers(1, &m_depthSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_depthSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER,
                 totalPixels * static_cast<GLsizeiptr>(sizeof(GLuint)),
                 nullptr, GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

    // Colour image: r32ui GL_TEXTURE_2D
    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexStorage2D(GL_TEXTURE_2D, 1, GL_R32UI, m_width, m_height);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void ComputePointCloudRenderer::freeBuffers() {
    if (m_depthSSBO)    { glDeleteBuffers(1,  &m_depthSSBO);    m_depthSSBO    = 0; }
    if (m_colorTexture) { glDeleteTextures(1, &m_colorTexture); m_colorTexture = 0; }
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::resize(int width, int height) {
    m_width  = width;
    m_height = height;
    if (m_initialized) allocateBuffers();
}

void ComputePointCloudRenderer::cleanup() {
    freeBuffers();

    delete m_clearShader;   m_clearShader   = nullptr;
    delete m_rasterShader;  m_rasterShader  = nullptr;
    delete m_resolveShader; m_resolveShader = nullptr;

    if (m_quadVAO) { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO) { glDeleteBuffers(1,     &m_quadVBO);  m_quadVBO = 0; }

    m_initialized = false;
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::beginFrame() {
    if (!m_initialized) return;

    // Bind depth SSBO to slot 1 and colour image to image unit 0
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, m_depthSSBO);
    glBindImageTexture(0, m_colorTexture, 0, GL_FALSE, 0,
                       GL_READ_WRITE, GL_R32UI);

    // Dispatch clear compute shader
    m_clearShader->use();
    m_clearShader->setInt("uTotalPixels", m_width * m_height);

    int groups = (m_width * m_height + 255) / 256;
    glDispatchCompute(static_cast<GLuint>(groups), 1, 1);

    // Ensure the clear is visible to subsequent compute dispatches
    glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT |
                    GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

    // Bind rasterize shader once for the whole frame; set the per-frame
    // uImageSize uniform here so renderNode() only needs per-node uniforms.
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

    // Shader is already bound by beginFrame(); just update per-node uniforms.
    m_rasterShader->setMat4("uMVP", mvp);
    m_rasterShader->setFloat("uPointBaseSize", pointBaseSize);
    m_rasterShader->setFloat("uFieldOfView",   fieldOfView);

    // Use cached location – avoids per-call glGetUniformLocation string lookup.
    glUniform1ui(m_locNumPoints, numPoints);

    // Bind point data as SSBO slot 0 (VBOs can also be used as SSBOs)
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, vbo);

    int groups = (static_cast<int>(numPoints) + 255) / 256;
    glDispatchCompute(static_cast<GLuint>(groups), 1, 1);

    // No per-node barrier – accumulate across nodes; endFrame() barriers once.
}

// ─────────────────────────────────────────────────────────────────────────────

void ComputePointCloudRenderer::endFrame() {
    if (!m_initialized) return;

    // Single barrier after all rasterize dispatches
    glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT |
                    GL_SHADER_STORAGE_BARRIER_BIT);

    // ── Resolve / composite pass ────────────────────────────────────────────
    // Bind the colour image read-only for the fragment shader
    glBindImageTexture(0, m_colorTexture, 0, GL_FALSE, 0,
                       GL_READ_ONLY, GL_R32UI);

    // Save and disable depth test so we draw over the existing scene content
    GLboolean depthWasEnabled = glIsEnabled(GL_DEPTH_TEST);
    GLboolean depthWriteWasOn;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthWriteWasOn);
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);

    m_resolveShader->use();

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glBindVertexArray(0);

    // Restore depth state
    if (depthWasEnabled) glEnable(GL_DEPTH_TEST);
    if (depthWriteWasOn) glDepthMask(GL_TRUE);

    // Unbind image to avoid hazards
    glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_ONLY, GL_R32UI);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, 0);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, 0);
}

} // namespace Engine
