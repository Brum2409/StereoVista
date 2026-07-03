#pragma once

#include "RHI/Pipeline.h"
#include "RHI/VulkanCommon.h"

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

    void init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
              VkFormat targetFormat);
    void shutdown();

    // Layout for the scene-color descriptor set (set 0: sampler2DArray).
    VkDescriptorSetLayout sceneSetLayout() const { return pipeline_.setLayout(0); }
    VkSampler sampler() const { return sampler_; }

    // Records the fullscreen draw inside the caller's active render pass.
    // Viewport/scissor are expected to be set already.
    void record(VkCommandBuffer cmd, VkDescriptorSet sceneSet,
                const TonemapSettings& settings, uint32_t layerIndex) const;

private:
    rhi::Device* device_ = nullptr;
    rhi::Pipeline pipeline_;
    VkSampler sampler_ = VK_NULL_HANDLE;
};

} // namespace renderer
