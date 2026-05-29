#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>

namespace Engine {

class Shader;

// ---------------------------------------------------------------------------
// DDGIVolume - Dynamic Diffuse Global Illumination probe volume.
//
// A regular 3D grid of light probes placed in the air. Each probe stores its
// incident lighting as two small octahedral maps packed into texture atlases:
//   * Irradiance atlas (RGBA16F) - one tile per probe, IRRADIANCE_SIDE^2
//     interior texels + a 1px border, holds the cosine-weighted average
//     incident radiance per direction (== E / pi).
//   * Depth/visibility atlas (RG16F) - one tile per probe, DEPTH_SIDE^2
//     interior texels + a 1px border, holds (meanDist, meanDist^2) per
//     direction for a Chebyshev (variance) visibility test.
//
// Each frame the probes are refreshed by tracing rays through the scene BVH
// (ddgiTraceRays.glsl) into the ray-data SSBO, then blending those results
// into the atlases with temporal hysteresis (ddgiUpdate*/ddgiBorder* shaders).
//
// Atlas tile layout: tileX = probeX, tileY = probeY + probeZ * countY, so the
// probe linear index x + y*Nx + z*Nx*Ny maps to a tile column/row directly.
// ---------------------------------------------------------------------------
class DDGIVolume {
public:
    // Octahedral interior texels per probe (excludes the 1px border).
    static constexpr int IRRADIANCE_SIDE = 6;
    static constexpr int DEPTH_SIDE = 14;
    static constexpr int BORDER = 1;

    DDGIVolume();
    ~DDGIVolume();

    // (Re)create all GPU resources for the given probe grid and ray budget.
    void initialize(const glm::ivec3& probeCounts, int raysPerProbe);

    // Recreate resources only if the configuration actually changed.
    void reconfigure(const glm::ivec3& probeCounts, int raysPerProbe);

    // Place the probe grid so probes span [minBounds, maxBounds] inclusive.
    void setSceneBounds(const glm::vec3& minBounds, const glm::vec3& maxBounds);

    // Zero both atlases and force the next update to overwrite (no blending).
    void clear();

    // --- Binding helpers -----------------------------------------------------
    // Ray-data SSBO (written by trace, read by update). Default binding = 6.
    void bindRayBuffer(GLuint binding = 6) const;
    // Atlases as read/write images for the update/border passes (units 0,1).
    void bindImagesForUpdate() const;
    // Atlases as sampler textures for trace recursion / fragment sampling.
    void bindTexturesForSampling(int irradianceUnit, int depthUnit) const;

    // Set the probe-grid + atlas-layout uniforms a shader needs (compute or
    // fragment). Uniform names are prefixed "ddgi_".
    void setGridUniforms(Shader* shader) const;
    // Grid uniforms + sampler-unit bindings for a sampling shader.
    void setSamplingUniforms(Shader* shader, int irradianceUnit, int depthUnit) const;

    // --- State / queries -----------------------------------------------------
    bool isInitialized() const { return m_initialized; }
    // Returns true once after a clear()/initialize(), then latches to false.
    bool consumeFirstFrame();

    glm::ivec3 getProbeCounts() const { return m_probeCounts; }
    int getRaysPerProbe() const { return m_raysPerProbe; }
    int getProbeCount() const { return m_probeCounts.x * m_probeCounts.y * m_probeCounts.z; }
    glm::vec3 getGridStart() const { return m_gridStart; }
    glm::vec3 getGridStep() const { return m_gridStep; }

    GLuint getIrradianceTexture() const { return m_irradianceTex; }
    GLuint getDepthTexture() const { return m_depthTex; }
    GLuint getRayBufferSSBO() const { return m_rayBufferSSBO; }

    glm::ivec2 getIrradianceAtlasSize() const;
    glm::ivec2 getDepthAtlasSize() const;

private:
    void destroy();
    void createAtlases();
    void createRayBuffer();

    glm::ivec3 m_probeCounts;
    int m_raysPerProbe;

    glm::vec3 m_gridStart;  // world position of probe (0,0,0)
    glm::vec3 m_gridStep;   // spacing between adjacent probes
    glm::vec3 m_boundsMin;
    glm::vec3 m_boundsMax;

    GLuint m_irradianceTex;  // RGBA16F octahedral atlas
    GLuint m_depthTex;       // RG16F   octahedral atlas
    GLuint m_rayBufferSSBO;  // vec4 per (probe,ray): rgb = radiance, a = signed hit distance

    bool m_initialized;
    bool m_firstFrame;  // next update writes directly (skips hysteresis blend)
};

} // namespace Engine
