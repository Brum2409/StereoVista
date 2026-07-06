#include "Renderer/passes/TonemapPass.h"

#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"

namespace renderer {

void TonemapPass::init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
                       VkFormat targetFormat, VkFormat depthFormat) {
    shutdown();
    device_ = &device;

    pipeline_ = rhi::GraphicsPipelineBuilder{}
                    .setShaders(shaderCompiler.load("assets/shaders_vk/fullscreen.vert"),
                                shaderCompiler.load("assets/shaders_vk/tonemap.frag"))
                    .setColorFormats({ targetFormat })
                    .setDepthFormat(depthFormat)
                    .setCullMode(VK_CULL_MODE_NONE)
                    .setDepth(false, false)
                    .setDebugName("tonemap resolve")
                    .build(device);

    // Depth-less twin for the XR window mirror (renders with no depth
    // attachment). Same shaders => an identically-defined set-0 layout, so the
    // scene-color set allocated from pipeline_'s layout binds to it too.
    mirrorPipeline_ = rhi::GraphicsPipelineBuilder{}
                          .setShaders(shaderCompiler.load("assets/shaders_vk/fullscreen.vert"),
                                      shaderCompiler.load("assets/shaders_vk/tonemap.frag"))
                          .setColorFormats({ targetFormat })
                          .setDepthFormat(VK_FORMAT_UNDEFINED)
                          .setCullMode(VK_CULL_MODE_NONE)
                          .setDepth(false, false)
                          .setDebugName("tonemap mirror")
                          .build(device);

    // Linear + clamp: a full-window eye samples at texel centres (== 1:1), and
    // the side-by-side half-viewport gets a clean horizontal downscale.
    VkSamplerCreateInfo samplerInfo{};
    samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    samplerInfo.magFilter = VK_FILTER_LINEAR;
    samplerInfo.minFilter = VK_FILTER_LINEAR;
    samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    VK_CHECK(vkCreateSampler(device.device(), &samplerInfo, nullptr, &sampler_));
}

namespace {

void recordResolve(const rhi::Pipeline& pipe, VkCommandBuffer cmd,
                   VkDescriptorSet sceneSet, const TonemapSettings& settings,
                   uint32_t layerIndex, glm::vec2 viewportOrigin,
                   glm::vec2 viewportSize, bool encodeSrgb) {
    struct Push {
        float exposure;
        int32_t operatorIndex;
        int32_t layerIndex;
        float originX, originY;
        float invSizeX, invSizeY;
        int32_t encodeSrgb;
    } push{ settings.exposure, static_cast<int32_t>(settings.op),
            static_cast<int32_t>(layerIndex),
            viewportOrigin.x, viewportOrigin.y,
            viewportSize.x > 0.0f ? 1.0f / viewportSize.x : 0.0f,
            viewportSize.y > 0.0f ? 1.0f / viewportSize.y : 0.0f,
            encodeSrgb ? 1 : 0 };

    pipe.bind(cmd);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe.layout(),
                            0, 1, &sceneSet, 0, nullptr);
    pipe.pushConstants(cmd, &push, sizeof(push));
    vkCmdDraw(cmd, 3, 1, 0, 0);
}

} // namespace

void TonemapPass::record(VkCommandBuffer cmd, VkDescriptorSet sceneSet,
                         const TonemapSettings& settings, uint32_t layerIndex,
                         glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                         bool encodeSrgb) const {
    recordResolve(pipeline_, cmd, sceneSet, settings, layerIndex, viewportOrigin,
                  viewportSize, encodeSrgb);
}

void TonemapPass::recordMirror(VkCommandBuffer cmd, VkDescriptorSet sceneSet,
                               const TonemapSettings& settings, uint32_t layerIndex,
                               glm::vec2 viewportOrigin, glm::vec2 viewportSize,
                               bool encodeSrgb) const {
    recordResolve(mirrorPipeline_, cmd, sceneSet, settings, layerIndex, viewportOrigin,
                  viewportSize, encodeSrgb);
}

void TonemapPass::shutdown() {
    if (!device_)
        return;
    if (sampler_ != VK_NULL_HANDLE)
        vkDestroySampler(device_->device(), sampler_, nullptr);
    sampler_ = VK_NULL_HANDLE;
    pipeline_.destroy();
    mirrorPipeline_.destroy();
    device_ = nullptr;
}

} // namespace renderer
