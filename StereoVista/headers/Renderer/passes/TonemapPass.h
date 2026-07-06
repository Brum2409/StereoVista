#pragma once

#include "RHI/Pipeline.h"
#include "RHI/VulkanCommon.h"

#include <glm/glm.hpp>

namespace rhi {
class Device;
class ShaderCompiler;
}

namespace renderer {

// Indices match tonemap.frag's operatorIndex. The set mirrors the GL app's
// options minus the fake Tony McMapface (returns as a real LUT with post-FX).
enum class TonemapOperator : int {
    Reinhard = 0,
    ACES = 1, // default, matching the old app
    Uncharted2 = 2,
    AgX = 3,
    KhronosPBRNeutral = 4,
};

inline const char* tonemapOperatorName(TonemapOperator op) {
    switch (op) {
    case TonemapOperator::Reinhard: return "Reinhard";
    case TonemapOperator::ACES: return "ACES";
    case TonemapOperator::Uncharted2: return "Uncharted 2";
    case TonemapOperator::AgX: return "AgX";
    case TonemapOperator::KhronosPBRNeutral: return "Khronos PBR Neutral";
    }
    return "?";
}

struct TonemapSettings {
    float exposure = 1.0f;
    TonemapOperator op = TonemapOperator::ACES;
};

// Fullscreen HDR resolve: samples one layer of the multiview scene target
// (SHADER_READ_ONLY) and writes tone-mapped sRGB to the current color
// attachment. The caller owns the render pass (vkCmdBeginRendering on the
// backbuffer) and the per-frame descriptor set; this pass owns pipeline and
// sampler. First renderer pass on the Phase 3 pass-based layout.
class TonemapPass {
public:
    TonemapPass() = default;
    ~TonemapPass() { shutdown(); }
    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;

    // depthFormat: the backbuffer pass carries the scene depth attachment
    // since Phase 6 (overlays depth-test against it in the same pass); the
    // tonemap pipeline must declare the format even though it ignores depth.
    // UNDEFINED = no depth attachment in the target pass.
    void init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
              VkFormat targetFormat, VkFormat depthFormat = VK_FORMAT_UNDEFINED);
    void shutdown();

    // Layout for the scene-color descriptor set (set 0: sampler2DArray).
    VkDescriptorSetLayout sceneSetLayout() const { return pipeline_.setLayout(0); }
    VkSampler sampler() const { return sampler_; }

    // Records the fullscreen draw inside the caller's active render pass.
    // Viewport/scissor are expected to be set already. layerIndex selects the
    // scene-target array layer (eye); viewportOrigin/Size is this eye's
    // on-screen rect in framebuffer pixels (full extent for mono / quad-buffer,
    // a half for side-by-side) — it maps gl_FragCoord to a [0,1] UV over the
    // full scene layer. encodeSrgb: apply the sRGB OETF in-shader (true for
    // UNORM targets like the window backbuffer); pass false for an sRGB-format
    // target (OpenXR eye swapchain) so the hardware encodes on write instead.
    void record(VkCommandBuffer cmd, VkDescriptorSet sceneSet,
                const TonemapSettings& settings, uint32_t layerIndex,
                glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                bool encodeSrgb = true) const;

    // Same resolve, but through a DEPTH-LESS pipeline for the Phase 7b XR
    // desktop mirror: the mirror draws sceneColor layer 0 into the window
    // backbuffer (a different size than the HMD eye-res scene depth, so no depth
    // attachment can be shared) with no overlays. Bind with no depth attachment.
    void recordMirror(VkCommandBuffer cmd, VkDescriptorSet sceneSet,
                      const TonemapSettings& settings, uint32_t layerIndex,
                      glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                      bool encodeSrgb = true) const;

private:
    rhi::Device* device_ = nullptr;
    rhi::Pipeline pipeline_;
    rhi::Pipeline mirrorPipeline_; // depth-less variant (XR window mirror)
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace renderer
