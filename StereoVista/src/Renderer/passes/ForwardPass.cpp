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
    pipeline_ =
        rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/mesh.vert"),
                        shaderCompiler.load("assets/shaders_vk/mesh.frag"))
            .setColorFormats({ colorFormat })
            .setDepthFormat(depthFormat)
            .setViewMask(viewMask)
            .setDepth(true, true) // reverse-Z GREATER default
            .addVertexBinding(MeshBuffer::vertexBinding())
            .externalSetLayout(0, frameSetLayout)
            .externalSetLayout(1, materialSetLayout)
            .setDebugName("forward PBR")
            .build(device);
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

    for (const DrawItem& draw : submission.draws) {
        if (!draw.mesh)
            continue;
        gpu::DrawPush push{};
        push.model = draw.model;
        push.normalMatCol0 = glm::vec4(draw.normalMatrix[0], 0.0f);
        push.normalMatCol1 = glm::vec4(draw.normalMatrix[1], 0.0f);
        push.normalMatCol2 = glm::vec4(draw.normalMatrix[2], 0.0f);
        push.materialIndex = draw.materialIndex;
        push.tint = draw.tint;
        pipeline_.pushConstants(cmd, &push, sizeof(push));
        draw.mesh->bindAndDraw(cmd);
    }
}

void ForwardPass::shutdown() {
    pipeline_.destroy();
}

} // namespace renderer
