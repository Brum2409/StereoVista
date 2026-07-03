#pragma once

#include "Renderer/GpuTypes.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace renderer {

class MeshBuffer;

// ============================================================================
// The renderer's per-frame SCENE INPUT. The application (scene + camera +
// GUI state) builds one of these each frame and hands it to
// Renderer::renderFrame — the renderer holds no scene state of its own.
// Everything here is multiview-shaped: views[] always carries kMaxViews
// entries (mono duplicates view 0) so Phase 7 stereo only changes who fills
// view 1, not the structures.
// ============================================================================

struct ViewCamera {
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f }; // from the renderer::projection factories ONLY
    glm::vec3 position{ 0.0f };
};

struct DrawItem {
    const MeshBuffer* mesh = nullptr;
    glm::mat4 model{ 1.0f };
    glm::mat3 normalMatrix{ 1.0f };
    uint32_t materialIndex = 0;
    bool castsShadows = true;
    // World-space bounding sphere; drives the sun shadow frustum fit (and
    // later CPU culling). Radius 0 = unknown, treated as a point.
    glm::vec3 worldBoundsCenter{ 0.0f };
    float worldBoundsRadius = 0.0f;
};

struct SunState {
    bool enabled = false;
    glm::vec3 direction{ -0.408f, -0.816f, -0.408f }; // travels this way
    glm::vec3 color{ 1.0f, 0.98f, 0.95f };
    float intensity = 0.16f;
    // Apparent angular diameter in degrees; drives PCSS penumbra growth.
    // The real sun is ~0.53; the default is stylized for readable softness.
    float angularSizeDeg = 1.5f;
};

struct PointLightState {
    glm::vec3 position{ 0.0f };
    glm::vec3 color{ 1.0f };
    float intensity = 1.0f;
    float attenLinear = 0.09f;
    float attenQuadratic = 0.032f;
    bool castsShadows = true;
    float radius = 0.05f; // emitter world radius, drives PCSS penumbra width
};

// Matches skybox.frag's SKY_MODE_* indices.
enum class SkyMode : int {
    Cubemap = 0,
    Equirect = 1,
    SolidColor = 2,
    Gradient = 3,
};

struct SkyState {
    SkyMode mode = SkyMode::Gradient;
    float intensity = 1.0f;
    glm::vec3 solidColor{ 0.2f, 0.3f, 0.4f };
    glm::vec3 gradientBottom{ 0.7f, 0.7f, 1.0f };
    glm::vec3 gradientTop{ 0.1f, 0.1f, 0.3f };
};

struct FrameSubmission {
    ViewCamera views[kMaxViews];
    std::vector<DrawItem> draws;
    SunState sun;
    std::vector<PointLightState> pointLights; // beyond kMaxPointLights ignored
    bool shadowsEnabled = true;
    bool softShadows = true; // PCSS contact hardening (else fixed-width PCF)
    float ambient = 0.03f;   // flat ambient albedo multiplier (GL parity)
    SkyState sky;
};

} // namespace renderer
