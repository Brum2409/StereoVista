#pragma once

#include "RHI/Vma.h"
#include "RHI/VulkanCommon.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

struct ImDrawData;

namespace rhi {
class Device;
class Swapchain;
class ShaderCompiler;
}

namespace renderer {

// Frame orchestration for the Vulkan renderer.
//
// MULTIVIEW BY DESIGN: the scene is always drawn into a layered offscreen
// target through VK_KHR_multiview (core 1.1). The camera UBO is a per-view
// array indexed by gl_ViewIndex in every shader. Mono is simply viewCount()
// == 1; quad-buffer stereo (Phase 7) raises it to 2 and copies both layers to
// a stereo swapchain — nothing in the scene pass changes. Do not write
// single-view assumptions into passes or shaders.
//
// Synchronization: one timeline semaphore paces the CPU against the GPU
// (per-slot wait before reuse — no fences); binary semaphores exist only
// where WSI demands them (acquire per frame slot, present per swapchain
// image). All barriers/submits use synchronization2.
class Renderer {
public:
    static constexpr uint32_t kFramesInFlight = 2;
    static constexpr uint32_t kMaxViews = 2;

    Renderer() = default;
    ~Renderer() { shutdown(); }
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void init(rhi::Device& device, rhi::Swapchain& swapchain,
              rhi::ShaderCompiler& shaderCompiler);
    void shutdown();

    // Rebuilds the size-dependent scene target after the swapchain changed.
    void onSwapchainRecreated();

    // Records + submits + presents one frame. uiDrawData may be null (UI not
    // built this frame). Returns false when the swapchain is out of date —
    // the caller recreates it and calls onSwapchainRecreated().
    bool renderFrame(ImDrawData* uiDrawData, double timeSeconds);

    uint32_t viewCount() const { return viewCount_; }

private:
    // GPU camera data, one slot per multiview view (std140-compatible).
    struct ViewUniforms {
        glm::mat4 viewProj[kMaxViews];
        glm::vec4 cameraPos[kMaxViews];
    };

    struct FrameContext {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
        VkDescriptorPool descriptorPool = VK_NULL_HANDLE;
        VkBuffer viewUbo = VK_NULL_HANDLE;
        VmaAllocation viewUboAllocation = VK_NULL_HANDLE;
        void* viewUboMapped = nullptr;
        uint64_t submittedTimelineValue = 0;
    };

    void createFrameContexts();
    void destroyFrameContexts();
    void createSceneTarget();
    void destroySceneTarget();
    void createTrianglePipeline(rhi::ShaderCompiler& shaderCompiler);
    void updateViewUniforms(FrameContext& frame, double timeSeconds);
    void recordFrame(FrameContext& frame, uint32_t imageIndex, ImDrawData* uiDrawData);

    rhi::Device* device_ = nullptr;
    rhi::Swapchain* swapchain_ = nullptr;

    uint32_t viewCount_ = 1; // mono today; 2 when stereo present lands (Phase 7)

    FrameContext frames_[kFramesInFlight];
    VkSemaphore frameTimeline_ = VK_NULL_HANDLE;
    uint64_t timelineValue_ = 0;
    uint32_t frameSlot_ = 0;

    // Layered HDR scene target (color + depth), one layer per view.
    VkImage sceneColor_ = VK_NULL_HANDLE;
    VmaAllocation sceneColorAllocation_ = VK_NULL_HANDLE;
    VkImageView sceneColorView_ = VK_NULL_HANDLE;
    VkImage sceneDepth_ = VK_NULL_HANDLE;
    VmaAllocation sceneDepthAllocation_ = VK_NULL_HANDLE;
    VkImageView sceneDepthView_ = VK_NULL_HANDLE;
    VkExtent2D sceneExtent_{};
    static constexpr VkFormat kSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kSceneDepthFormat = VK_FORMAT_D32_SFLOAT;

    VkDescriptorSetLayout viewSetLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout trianglePipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline trianglePipeline_ = VK_NULL_HANDLE;
};

} // namespace renderer
