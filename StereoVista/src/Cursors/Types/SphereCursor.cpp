#include "Cursors/Types/SphereCursor.h"
#include "Renderer/OverlayDrawList.h"

#include <glm/gtc/matrix_transform.hpp>

#include <corecrt_math_defines.h>
#include <cmath>

namespace Cursor {
    namespace {
        // VkCullModeFlagBits values, kept as raw ints so this file stays
        // Vulkan-header-free (OverlayBatch carries them as uint32_t).
        constexpr uint32_t kCullFront = 0x00000001; // VK_CULL_MODE_FRONT_BIT
        constexpr uint32_t kCullBack = 0x00000002;  // VK_CULL_MODE_BACK_BIT

        // Effective "center alpha" of the GL sphere shader: its
        // distFromCenter/radius term is constant over the sphere surface
        // (length of the baked mesh vertex), so it collapses to a constant
        // computed here — see overlay.frag's SPHERE branch.
        float centerAlpha(float centerTransparency, float meshRadius) {
            const float t = glm::clamp(meshRadius, 0.0f, 1.0f);
            const float s = t * t * (3.0f - 2.0f * t); // smoothstep(0,1,t)
            return centerTransparency + (1.0f - centerTransparency) * s;
        }
    }

    SphereCursor::SphereCursor() :
        BaseCursor(),
        m_color(1.0f, 0.0f, 0.0f, 0.7f),
        m_transparency(0.7f),
        m_edgeSoftness(0.8f),
        m_centerTransparency(0.2f),
        m_showInnerSphere(false),
        m_innerSphereColor(0.0f, 1.0f, 0.0f, 1.0f),
        m_innerSphereFactor(0.1f)
    {
        m_name = "SphereCursor";
    }

    SphereCursor::~SphereCursor() = default;

    void SphereCursor::initialize() {
        m_meshRadius = getBaseSize();
        generateMesh(m_meshRadius, 32, 32);
    }

    void SphereCursor::appendTo(renderer::OverlayDrawList& list,
                                const glm::vec3& cameraPosition) {
        if (!m_visible || !m_positionValid || m_indices.empty())
            return;

        const float currentRadius = getBaseSize() * calculateScale(cameraPosition);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), m_position);
        model = glm::scale(model, glm::vec3(currentRadius));
        const glm::mat4 innerModel = glm::scale(model, glm::vec3(m_innerSphereFactor));

        const glm::vec4 outerParams(m_transparency, m_edgeSoftness,
                                    centerAlpha(m_centerTransparency, m_meshRadius),
                                    0.0f);
        const glm::vec4 innerParams(1.0f, 0.0f, 0.0f, 1.0f); // isInner: flat alpha

        // GL two-pass transparency: back faces (depth write ON) first, then
        // front faces (depth write OFF), inner sphere leading each pass.
        struct Pass { uint32_t cull; bool depthWrite; };
        const Pass passes[2] = { { kCullFront, true }, { kCullBack, false } };
        for (const Pass& pass : passes) {
            if (m_showInnerSphere)
                list.sphereMesh(m_vertices.data(), m_indices.data(), m_indices.size(),
                                innerModel, m_innerSphereColor, innerParams,
                                pass.cull, pass.depthWrite,
                                renderer::OverlayDepth::Occluded);
            list.sphereMesh(m_vertices.data(), m_indices.data(), m_indices.size(),
                            model, m_color, outerParams, pass.cull, pass.depthWrite,
                            renderer::OverlayDepth::Occluded);
        }
    }

    void SphereCursor::appendOrbitSphere(renderer::OverlayDrawList& list,
                                         const glm::vec3& center, float radius,
                                         const glm::vec4& color) {
        if (m_indices.empty())
            return;
        glm::mat4 model = glm::translate(glm::mat4(1.0f), center);
        // The mesh bakes m_meshRadius; normalise so `radius` is the true
        // world radius of the marker.
        model = glm::scale(model, glm::vec3(radius / std::max(m_meshRadius, 1e-5f)));
        // Solid-ish marker: edge softness 0, no center fade (GL parity with
        // renderOrbitCenter's uniforms), same two-pass ordering.
        const glm::vec4 params(1.0f, 0.0f, 1.0f, 0.0f);
        list.sphereMesh(m_vertices.data(), m_indices.data(), m_indices.size(), model,
                        color, params, kCullFront, true,
                        renderer::OverlayDepth::Occluded);
        list.sphereMesh(m_vertices.data(), m_indices.data(), m_indices.size(), model,
                        color, params, kCullBack, false,
                        renderer::OverlayDepth::Occluded);
    }

    float SphereCursor::calculateRadius(const glm::vec3& cameraPosition) {
        // Use the unified scaling system from BaseCursor
        return getBaseSize() * calculateScale(cameraPosition);
    }

    void SphereCursor::generateMesh(float radius, unsigned int rings, unsigned int sectors) {
        m_vertices.clear();
        m_indices.clear();

        float const R = 1.0f / (float)(rings - 1);
        float const S = 1.0f / (float)(sectors - 1);

        for (unsigned int r = 0; r < rings; ++r) {
            for (unsigned int s = 0; s < sectors; ++s) {
                float const y = sin(-M_PI_2 + M_PI * r * R);
                float const x = cos(2 * M_PI * s * S) * sin(M_PI * r * R);
                float const z = sin(2 * M_PI * s * S) * sin(M_PI * r * R);

                m_vertices.push_back(x * radius);
                m_vertices.push_back(y * radius);
                m_vertices.push_back(z * radius);

                m_vertices.push_back(x);
                m_vertices.push_back(y);
                m_vertices.push_back(z);
            }
        }

        for (unsigned int r = 0; r < rings - 1; ++r) {
            for (unsigned int s = 0; s < sectors - 1; ++s) {
                m_indices.push_back(r * sectors + s);
                m_indices.push_back(r * sectors + (s + 1));
                m_indices.push_back((r + 1) * sectors + (s + 1));

                m_indices.push_back(r * sectors + s);
                m_indices.push_back((r + 1) * sectors + (s + 1));
                m_indices.push_back((r + 1) * sectors + s);
            }
        }
    }
}
