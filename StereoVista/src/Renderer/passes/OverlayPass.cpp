#include "Renderer/passes/OverlayPass.h"

#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"
#include "Renderer/GpuTypes.h"

#include <cstring>

namespace renderer {

void OverlayPass::init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
                       VkFormat colorFormat, VkFormat depthFormat, uint32_t viewMask,
                       uint32_t framesInFlight) {
    shutdown();
    device_ = &device;

    rhi::VertexBinding binding;
    binding.binding = 0;
    binding.stride = sizeof(OverlayVertex);
    binding.attributes = {
        { 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(OverlayVertex, pos) },
        { 1, VK_FORMAT_R32G32B32_SFLOAT, offsetof(OverlayVertex, aux) },
        { 2, VK_FORMAT_R32G32B32A32_SFLOAT, offsetof(OverlayVertex, params) },
        { 3, VK_FORMAT_R8G8B8A8_UNORM, offsetof(OverlayVertex, rgba) },
    };

    pipeline_ =
        rhi::GraphicsPipelineBuilder{}
            .setShaders(shaderCompiler.load("assets/shaders_vk/overlay.vert"),
                        shaderCompiler.load("assets/shaders_vk/overlay.frag"))
            .setColorFormats({ colorFormat })
            .setDepthFormat(depthFormat)
            .setViewMask(viewMask)
            .addVertexBinding(binding)
            .setBlend(rhi::BlendMode::AlphaBlend)
            .setCullMode(VK_CULL_MODE_NONE)
            // Static depth values are placeholders — every batch sets these
            // dynamically (playbook A.6: one pipeline for every depth mode).
            .setDepth(false, false, VK_COMPARE_OP_GREATER_OR_EQUAL)
            .addDynamicState(VK_DYNAMIC_STATE_DEPTH_TEST_ENABLE)
            .addDynamicState(VK_DYNAMIC_STATE_DEPTH_WRITE_ENABLE)
            .addDynamicState(VK_DYNAMIC_STATE_DEPTH_COMPARE_OP)
            .addDynamicState(VK_DYNAMIC_STATE_CULL_MODE)
            .setDebugName("overlay")
            .build(device);

    slots_.resize(framesInFlight);
}

void OverlayPass::prepare(uint32_t frameSlot, const OverlayDrawList& list,
                          const OverlayViewInfo* views, uint32_t viewCount) {
    active_ = false;
    batches_.clear();
    if (list.empty() || !views || viewCount == 0)
        return;

    SlotBuffers& slot = slots_[frameSlot];

    const VkDeviceSize vertexBytes = list.vertices().size() * sizeof(OverlayVertex);
    if (!slot.vertices.valid() || slot.vertices.size() < vertexBytes) {
        rhi::BufferDesc desc{};
        desc.size = std::max<VkDeviceSize>(vertexBytes * 2, 64 * 1024);
        desc.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        desc.memory = rhi::MemoryUsage::HostUpload;
        desc.debugName = "overlay vertices";
        slot.vertices.create(*device_, desc);
    }
    slot.vertices.upload(list.vertices().data(), vertexBytes);

    // One entry per (viewport, eye) view — the record() viewIndex selects it.
    // Unused tail entries repeat the last provided view (harmless: no batch
    // ever records with their index).
    constexpr uint32_t kMaxOverlayViews = kMaxViews * kMaxViewports;
    if (!slot.viewParams.valid() ||
        slot.viewParams.size() < sizeof(gpu::OverlayViewParams) * kMaxOverlayViews) {
        rhi::BufferDesc desc{};
        desc.size = sizeof(gpu::OverlayViewParams) * kMaxOverlayViews;
        desc.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                     VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
        desc.memory = rhi::MemoryUsage::HostUpload;
        desc.debugName = "overlay view params";
        slot.viewParams.create(*device_, desc);
    }
    gpu::OverlayViewParams params[kMaxOverlayViews]{};
    for (uint32_t v = 0; v < kMaxOverlayViews; ++v) {
        const OverlayViewInfo& info = views[std::min(v, viewCount - 1)];
        params[v].viewProj = info.viewProj;
        params[v].viewportPx = glm::vec4(info.viewportPx.x, info.viewportPx.y,
                                         info.viewportPx.x > 0 ? 1.0f / info.viewportPx.x : 0.0f,
                                         info.viewportPx.y > 0 ? 1.0f / info.viewportPx.y : 0.0f);
        params[v].cameraPos = glm::vec4(info.cameraPos, 1.0f);
    }
    slot.viewParams.upload(params, sizeof(params));

    batches_ = list.batches();
    vertexBuffer_ = slot.vertices.handle();
    viewParamsAddress_ = slot.viewParams.deviceAddress();
    active_ = true;
}

void OverlayPass::record(VkCommandBuffer cmd, uint32_t viewIndex, bool forceOnTop) const {
    if (!active_ || batches_.empty())
        return;

    pipeline_.bind(cmd);
    const VkDeviceSize offset = 0;
    vkCmdBindVertexBuffers(cmd, 0, 1, &vertexBuffer_, &offset);

    for (const OverlayBatch& batch : batches_) {
        // Reverse-Z: "normally visible" = GREATER_OR_EQUAL (the GL LEQUAL
        // pass), "only where hidden" = LESS (the GL GREATER x-ray pass).
        // forceOnTop overrides both to draw with no depth interaction.
        const bool test = !forceOnTop && batch.depth != OverlayDepth::Always;
        vkCmdSetDepthTestEnable(cmd, test ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthWriteEnable(cmd, (!forceOnTop && batch.depthWrite) ? VK_TRUE : VK_FALSE);
        vkCmdSetDepthCompareOp(cmd, batch.depth == OverlayDepth::Hidden
                                        ? VK_COMPARE_OP_LESS
                                        : VK_COMPARE_OP_GREATER_OR_EQUAL);
        vkCmdSetCullMode(cmd, batch.cullMode);

        gpu::OverlayPush push{};
        push.viewParams = viewParamsAddress_;
        push.paramsA = batch.paramsA;
        push.colorA = batch.colorA;
        push.colorB = batch.colorB;
        push.kind = batch.kind;
        push.viewIndex = viewIndex;
        pipeline_.pushConstants(cmd, &push, sizeof(push));

        vkCmdDraw(cmd, batch.vertexCount, 1, batch.firstVertex, 0);
    }
}

void OverlayPass::shutdown() {
    if (!device_)
        return;
    pipeline_.destroy();
    slots_.clear();
    batches_.clear();
    vertexBuffer_ = VK_NULL_HANDLE;
    viewParamsAddress_ = 0;
    active_ = false;
    device_ = nullptr;
}

} // namespace renderer
