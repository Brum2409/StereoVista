#pragma once

#include "RHI/Pipeline.h"
#include "RHI/Texture.h"
#include "RHI/VulkanCommon.h"
#include "Renderer/FrameSubmission.h"
#include "Renderer/GpuTypes.h"

#include <vector>

namespace rhi {
class Device;
class ShaderCompiler;
}

namespace renderer {

// Shadow caster rendering: one reverse-Z directional (sun) shadow map plus a
// depth cubemap ARRAY for up to kMaxShadowedPointLights point lights. Each
// point light renders all 6 faces in ONE multiview pass (viewMask 0x3F,
// gl_ViewIndex = face) — no geometry shader, real hardware depth (early-Z
// intact), unlike the GL reference. Both caster pipelines are depth-only
// (no fragment stage) with a small negative (reverse-Z) slope bias; the
// receiver-side normal-offset bias in mesh.frag does the heavy lifting
// (playbook C.7).
//
// prepare() is the CPU half: fits the sun ortho to the submitted draws'
// bounds with texel snapping (ported from the GL calculateLightSpaceMatrix),
// builds the cube face matrices, assigns cube-array slots to the first
// shadow-casting point lights, and writes all of it into the frame's
// gpu::FrameData. record() is the GPU half and leaves both maps
// SHADER_READ_ONLY for the forward pass.
class ShadowPass {
public:
    ShadowPass() = default;
    ~ShadowPass() { shutdown(); }
    ShadowPass(const ShadowPass&) = delete;
    ShadowPass& operator=(const ShadowPass&) = delete;

    void init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
              VkDescriptorSetLayout frameSetLayout);
    void shutdown();

    // Fills frame.sunViewProj / shadowTexelWorldSize / pointShadowFaceVP /
    // pointShadowNear/Far and returns the cube-array slot per submitted point
    // light (-1 = unshadowed), aligned with submission.pointLights.
    std::vector<int> prepare(const FrameSubmission& submission,
                             gpu::FrameData& frame) const;

    // Records both caster passes. shadowedLightCount = number of slots
    // assigned by prepare(). Draw lists come from the submission.
    void record(VkCommandBuffer cmd, VkDescriptorSet frameSet,
                const FrameSubmission& submission, uint32_t shadowedLightCount);

    // For the per-frame descriptor writes (set 0 bindings 3/4 compare,
    // 7/8 raw — the PCSS blocker search reads plain depth values).
    VkImageView sunShadowView() const { return sunShadow_.view(); }
    VkImageView pointShadowView() const { return pointShadow_.view(); }
    VkSampler compareSampler2D() const { return compareSampler2D_; }
    VkSampler compareSamplerCube() const { return compareSamplerCube_; }
    VkSampler rawSampler() const { return rawSampler_; }

    static constexpr float kPointShadowNear = 0.05f;
    static constexpr float kPointShadowFar = 50.0f; // GL far_plane

private:
    rhi::Device* device_ = nullptr;

    rhi::Texture sunShadow_;   // D32, kSunShadowResolution^2
    rhi::Texture pointShadow_; // D32 cube array (CUBE_ARRAY default view),
                               // 6 * kMaxShadowedPointLights layers
    VkImageView pointShadowFaceViews_[kMaxShadowedPointLights] = {}; // 6-layer 2D_ARRAY
    // Slot -> light position, written by prepare() for record()'s range cull
    // (mutable: prepare is logically const — this is a same-frame scratch).
    mutable glm::vec3 slotLightPos_[kMaxShadowedPointLights] = {};
    VkSampler compareSampler2D_ = VK_NULL_HANDLE;
    VkSampler compareSamplerCube_ = VK_NULL_HANDLE;
    VkSampler rawSampler_ = VK_NULL_HANDLE; // NEAREST, no compare (blocker search)

    rhi::Pipeline sunPipeline_;
    rhi::Pipeline pointPipeline_;
};

} // namespace renderer
