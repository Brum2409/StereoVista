#include "Renderer/passes/ForwardPass.h"

#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/MeshBuffer.h"

namespace renderer {

void ForwardPass::init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
                       VkFormat colorFormat, VkFormat depthFormat, uint32_t viewMask,
                       VkDescriptorSetLayout frameSetLayout,
                       VkDescriptorSetLayout materialSetLayout) {
    shutdown();
    auto builder = [&](VkPolygonMode mode, const char* name) {
        return rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/mesh.vert"),
                        shaderCompiler.load("assets/shaders_vk/mesh.frag"))
            .setColorFormats({ colorFormat })
            .setDepthFormat(depthFormat)
            .setViewMask(viewMask)
            .setDepth(true, true) // reverse-Z GREATER default
            .setPolygonMode(mode)
            .addVertexBinding(MeshBuffer::vertexBinding())
            .externalSetLayout(0, frameSetLayout)
            .externalSetLayout(1, materialSetLayout)
            .setDebugName(name)
            .build(device);
    };
    pipeline_ = builder(VK_POLYGON_MODE_FILL, "forward PBR");
    // Line-mode twin for DrawItem::wireframe (identical layout, so the bound
    // descriptor sets stay valid across the pipeline switch). Optional
    // feature: without it wireframe draws silently render filled.
    if (device.fillModeNonSolidEnabled())
        wirePipeline_ = builder(VK_POLYGON_MODE_LINE, "forward PBR wireframe");
}

void ForwardPass::record(VkCommandBuffer cmd, VkDescriptorSet frameSet,
                         VkDescriptorSet materialSet,
                         const FrameSubmission& submission) const {
    if (submission.draws.empty())
        return;

    pipeline_.bind(cmd);
    VkDescriptorSet sets[2] = { frameSet, materialSet };
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_.layout(),
                            0, 2, sets, 0, nullptr);

    auto recordDraw = [&](const DrawItem& draw) {
        gpu::DrawPush push{};
        push.model = draw.model;
        push.normalMatCol0 = glm::vec4(draw.normalMatrix[0], 0.0f);
        push.normalMatCol1 = glm::vec4(draw.normalMatrix[1], 0.0f);
        push.normalMatCol2 = glm::vec4(draw.normalMatrix[2], 0.0f);
        push.materialIndex = draw.materialIndex;
        push.tint = draw.tint;
        pipeline_.pushConstants(cmd, &push, sizeof(push));
        draw.mesh->bindAndDraw(cmd);
    };

    // Fill draws first (wireframe ones too when the line pipeline is
    // unavailable), then the wireframe set under one pipeline switch.
    const bool haveWire = wirePipeline_.valid();
    bool anyWire = false;
    for (const DrawItem& draw : submission.draws) {
        if (!draw.mesh)
            continue;
        if (haveWire && draw.wireframe) {
            anyWire = true;
            continue;
        }
        recordDraw(draw);
    }
    if (anyWire) {
        wirePipeline_.bind(cmd);
        for (const DrawItem& draw : submission.draws)
            if (draw.mesh && draw.wireframe)
                recordDraw(draw);
    }
}

void ForwardPass::shutdown() {
    pipeline_.destroy();
    wirePipeline_.destroy();
}

} // namespace renderer
