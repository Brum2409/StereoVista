#include "../../headers/Engine/DDGIVolume.h"
#include "../../headers/Engine/Shader.h"

#include <algorithm>
#include <iostream>

namespace Engine {

DDGIVolume::DDGIVolume()
    : m_probeCounts(16, 8, 16)
    , m_raysPerProbe(64)
    , m_gridStart(-10.0f)
    , m_gridStep(1.0f)
    , m_boundsMin(-10.0f)
    , m_boundsMax(10.0f)
    , m_irradianceTex(0)
    , m_depthTex(0)
    , m_rayBufferSSBO(0)
    , m_initialized(false)
    , m_firstFrame(true) {}

DDGIVolume::~DDGIVolume() { destroy(); }

void DDGIVolume::initialize(const glm::ivec3& probeCounts, int raysPerProbe) {
    destroy();

    m_probeCounts = glm::max(probeCounts, glm::ivec3(1));
    m_raysPerProbe = std::max(raysPerProbe, 1);

    createAtlases();
    createRayBuffer();

    m_initialized = true;
    m_firstFrame = true;
}

void DDGIVolume::reconfigure(const glm::ivec3& probeCounts, int raysPerProbe) {
    glm::ivec3 counts = glm::max(probeCounts, glm::ivec3(1));
    int rays = std::max(raysPerProbe, 1);
    if (m_initialized && counts == m_probeCounts && rays == m_raysPerProbe) {
        return;
    }
    initialize(counts, rays);
    // Re-derive grid placement for the new probe counts.
    setSceneBounds(m_boundsMin, m_boundsMax);
}

void DDGIVolume::setSceneBounds(const glm::vec3& minBounds, const glm::vec3& maxBounds) {
    m_boundsMin = minBounds;
    m_boundsMax = maxBounds;

    glm::vec3 extent = maxBounds - minBounds;
    for (int i = 0; i < 3; ++i) {
        int divisor = std::max(m_probeCounts[i] - 1, 1);
        m_gridStep[i] = extent[i] / static_cast<float>(divisor);
        // Guard against degenerate (flat) bounds so probes never coincide.
        if (m_gridStep[i] <= 1e-5f) m_gridStep[i] = 1.0f;
    }
    m_gridStart = minBounds;
}

void DDGIVolume::clear() {
    if (!m_initialized) return;

    const float zeroRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    if (m_irradianceTex != 0) {
        glClearTexImage(m_irradianceTex, 0, GL_RGBA, GL_FLOAT, zeroRGBA);
    }
    if (m_depthTex != 0) {
        glClearTexImage(m_depthTex, 0, GL_RG, GL_FLOAT, zeroRGBA);
    }
    m_firstFrame = true;
}

void DDGIVolume::bindRayBuffer(GLuint binding) const {
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, binding, m_rayBufferSSBO);
}

void DDGIVolume::bindImagesForUpdate() const {
    glBindImageTexture(0, m_irradianceTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
    glBindImageTexture(1, m_depthTex, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG16F);
}

void DDGIVolume::bindTexturesForSampling(int irradianceUnit, int depthUnit) const {
    glActiveTexture(GL_TEXTURE0 + irradianceUnit);
    glBindTexture(GL_TEXTURE_2D, m_irradianceTex);
    glActiveTexture(GL_TEXTURE0 + depthUnit);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    glActiveTexture(GL_TEXTURE0);
}

void DDGIVolume::setGridUniforms(Shader* shader) const {
    if (!shader) return;
    shader->setVec3("ddgi_gridStart", m_gridStart);
    shader->setVec3("ddgi_gridStep", m_gridStep);
    shader->setIVec3("ddgi_probeCounts", m_probeCounts.x, m_probeCounts.y, m_probeCounts.z);
    shader->setInt("ddgi_raysPerProbe", m_raysPerProbe);
    shader->setInt("ddgi_irradianceSide", IRRADIANCE_SIDE);
    shader->setInt("ddgi_depthSide", DEPTH_SIDE);
    glm::ivec2 irr = getIrradianceAtlasSize();
    glm::ivec2 dep = getDepthAtlasSize();
    shader->setVec2("ddgi_irradianceTexels", glm::vec2(irr));
    shader->setVec2("ddgi_depthTexels", glm::vec2(dep));
}

void DDGIVolume::setSamplingUniforms(Shader* shader, int irradianceUnit, int depthUnit) const {
    if (!shader) return;
    setGridUniforms(shader);
    shader->setInt("ddgi_irradianceMap", irradianceUnit);
    shader->setInt("ddgi_depthMap", depthUnit);
}

bool DDGIVolume::consumeFirstFrame() {
    bool was = m_firstFrame;
    m_firstFrame = false;
    return was;
}

glm::ivec2 DDGIVolume::getIrradianceAtlasSize() const {
    int tile = IRRADIANCE_SIDE + 2 * BORDER;
    return glm::ivec2(m_probeCounts.x * tile, m_probeCounts.y * m_probeCounts.z * tile);
}

glm::ivec2 DDGIVolume::getDepthAtlasSize() const {
    int tile = DEPTH_SIDE + 2 * BORDER;
    return glm::ivec2(m_probeCounts.x * tile, m_probeCounts.y * m_probeCounts.z * tile);
}

void DDGIVolume::createAtlases() {
    glm::ivec2 irr = getIrradianceAtlasSize();
    glm::ivec2 dep = getDepthAtlasSize();

    glGenTextures(1, &m_irradianceTex);
    glBindTexture(GL_TEXTURE_2D, m_irradianceTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, irr.x, irr.y, 0, GL_RGBA, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glGenTextures(1, &m_depthTex);
    glBindTexture(GL_TEXTURE_2D, m_depthTex);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RG16F, dep.x, dep.y, 0, GL_RG, GL_FLOAT, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glBindTexture(GL_TEXTURE_2D, 0);

    // Start cleared so the very first sampled frame reads black rather than garbage.
    const float zeroRGBA[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    glClearTexImage(m_irradianceTex, 0, GL_RGBA, GL_FLOAT, zeroRGBA);
    glClearTexImage(m_depthTex, 0, GL_RG, GL_FLOAT, zeroRGBA);
}

void DDGIVolume::createRayBuffer() {
    glGenBuffers(1, &m_rayBufferSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, m_rayBufferSSBO);
    GLsizeiptr bytes = static_cast<GLsizeiptr>(getProbeCount()) * m_raysPerProbe * 4 * sizeof(float);
    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_DYNAMIC_COPY);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void DDGIVolume::destroy() {
    if (m_irradianceTex != 0) {
        glDeleteTextures(1, &m_irradianceTex);
        m_irradianceTex = 0;
    }
    if (m_depthTex != 0) {
        glDeleteTextures(1, &m_depthTex);
        m_depthTex = 0;
    }
    if (m_rayBufferSSBO != 0) {
        glDeleteBuffers(1, &m_rayBufferSSBO);
        m_rayBufferSSBO = 0;
    }
    m_initialized = false;
}

} // namespace Engine
