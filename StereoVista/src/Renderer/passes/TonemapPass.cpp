#include "Renderer/passes/TonemapPass.h"

#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"

namespace renderer {

void TonemapPass::init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
                       VkFormat targetFormat) {
    shutdown();
    device_ = &device;

    pipeline_ = rhi::GraphicsPipelineBuilder{}
                    .setShaders(shaderCompiler.load("assets/shaders_vk/fullscreen.vert"),
                                shaderCompiler.load("assets/shaders_vk/tonemap.frag"))
                    .setColorFormats({ targetFormat })
                    .setCullMode(VK_CULL_MODE_NONE)
                    .setDepth(false, false)
                    .setDebugName("tonemap resolve")
                    .build(device);

    // texelFetch ignores filtering, but a combined image sampler still needs
    // a sampler object; nearest/clamp states the 1:1 intent.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_NEAREST;
    samplerInfo.minFilter = VK_FILTER_NEAREST;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device.device(), &samplerInfo, nullptr, &sampler_));
}

void TonemapPass::record(VkCommandBuffer cmd, VkDescriptorSet sceneSet,
                         const TonemapSettings& settings, uint32_t layerIndex) const {
    struct Push {
        float exposure;
        int32_t operatorIndex;
        int32_t layerIndex;
    } push{ settings.exposure, static_cast<int32_t>(settings.op),
            static_cast<int32_t>(layerIndex) };

    pipeline_.bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout(),
                            0, 1, &sceneSet, 0, nullptr);
    pipeline_.pushConstants(cmd, &push, sizeof(push));
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

void TonemapPass::shutdown() {
    if (!device_)
        return;
    if (sampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_->device(), sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    pipeline_.destroy();
    device_ = nullptr;
}

} // namespace renderer
