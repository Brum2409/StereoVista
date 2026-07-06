#pragma once

#include "RHI/Pipeline.h"
#include "RHI/Texture.h"
#include "RHI/VulkanCommon.h"
#include "Renderer/FrameSubmission.h"

#include <string>

namespace rhi {
class Device;
class ShaderCompiler;
}

namespace renderer {

// Sky background: fullscreen triangle at reverse-Z far (depth 0) drawn AFTER
// the opaque pass with GREATER_OR_EQUAL / no write, so only background
// pixels shade. The view ray is reconstructed from the per-view inverse
// view-projection — no cube vertex buffer, multiview-native. Four modes
// (cubemap / equirect HDR / solid / gradient) selected per frame via push
// constants; textures the mode doesn't use fall back to 1x1 dummies so the
// frame descriptor set is always fully valid.
class SkyboxPass {
public:
    SkyboxPass() = default;
    ~SkyboxPass() { shutdown(); }
    SkyboxPass(const SkyboxPass&) = delete;
    SkyboxPass& operator=(const SkyboxPass&) = delete;

    void init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
              VkFormat colorFormat, VkFormat depthFormat, uint32_t viewMask,
              VkDescriptorSetLayout frameSetLayout);
    void shutdown();

    // Rebuilds ONLY the pipeline for a new multiview viewMask (mono<->stereo
    // toggle), preserving the loaded sky textures + sampler that init()/shutdown()
    // would otherwise destroy.
    void rebuildPipeline(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
                         VkFormat colorFormat, VkFormat depthFormat, uint32_t viewMask,
                         VkDescriptorSetLayout frameSetLayout);

    // Loaders (call any time after init; loading-time blocking uploads).
    // Both return false and keep the previous/dummy texture on failure.
    bool loadEquirectHdr(const std::string& path);
    // directory containing right/left/top/bottom/front/back.jpg|png
    bool loadCubemap(const std::string& directory);

    bool hasEquirect() const { return equirectLoaded_; }
    bool hasCubemap() const { return cubemapLoaded_; }

    // Views for the frame set (bindings 5/6); always valid.
    VkImageView cubemapView() const {
        return cubemapLoaded_ ? cubemap_.view() : dummyCube_.view();
    }
    VkImageView equirectView() const {
        return equirectLoaded_ ? equirect_.view() : dummy2D_.view();
    }
    VkSampler sampler() const { return sampler_; }

    // Inside the caller's active scene render pass, after opaque geometry.
    void record(VkCommandBuffer cmd, VkDescriptorSet frameSet,
                const SkyState& sky) const;

private:
    // Shared pipeline recipe for init() + rebuildPipeline().
    void buildPipeline(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
                       VkFormat colorFormat, VkFormat depthFormat, uint32_t viewMask,
                       VkDescriptorSetLayout frameSetLayout);

    rhi::Device* device_ = nullptr;
    rhi::Pipeline pipeline_;
    VkSampler sampler_ = VK_NULL_HANDLE;
    rhi::Texture cubemap_;
    rhi::Texture equirect_;
    rhi::Texture dummyCube_;
    rhi::Texture dummy2D_;
    bool cubemapLoaded_ = false;
    bool equirectLoaded_ = false;
};

} // namespace renderer
