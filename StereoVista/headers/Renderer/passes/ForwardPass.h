#pragma once

#include "RHI/Pipeline.h"
#include "RHI/VulkanCommon.h"
#include "Renderer/FrameSubmission.h"

namespace rhi {
class Device;
class ShaderCompiler;
}

namespace renderer {

// Opaque lit geometry: ONE metallic-roughness PBR pipeline (mesh.vert /
// mesh.frag) writing linear HDR into the multiview scene target. Set 0 is
// the renderer's shared per-frame set, set 1 the MaterialSystem's bindless
// set — both external layouts; per-draw data rides push constants.
class ForwardPass {
public:
    ForwardPass() = default;
    ~ForwardPass() { shutdown(); }
    ForwardPass(const ForwardPass&) = delete;
    ForwardPass& operator=(const ForwardPass&) = delete;

    void init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
              VkFormat colorFormat, VkFormat depthFormat, uint32_t viewMask,
              VkDescriptorSetLayout frameSetLayout,
              VkDescriptorSetLayout materialSetLayout);
    void shutdown();

    // Inside the caller's active scene render pass; viewport/scissor set.
    void record(VkCommandBuffer cmd, VkDescriptorSet frameSet,
                VkDescriptorSet materialSet, const FrameSubmission& submission) const;

private:
    rhi::Pipeline pipeline_;
};

} // namespace renderer
