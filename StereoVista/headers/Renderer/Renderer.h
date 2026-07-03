#pragma once

#include "RHI/Buffer.h"
#include "RHI/DescriptorAllocator.h"
#include "RHI/Pipeline.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"
#include "RHI/VulkanCommon.h"
#include "Renderer/FrameSubmission.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/MaterialSystem.h"
#include "Renderer/passes/ForwardPass.h"
#include "Renderer/passes/ShadowPass.h"
#include "Renderer/passes/SkyboxPass.h"
#include "Renderer/passes/TonemapPass.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

struct ImDrawData;

namespace rhi {
class Device;
class ShaderCompiler;
}

namespace renderer {

// Frame orchestration for the Vulkan renderer.
//
// SCENE INPUT: the renderer holds no scene state. Every frame the application
// hands it a FrameSubmission (cameras + draw list + lights + sky); rendering
// is a fixed sequence of PASS OBJECTS over that submission:
//
//   ShadowPass (sun map + point cubemaps)          [own depth targets]
//     -> ForwardPass (single metallic-roughness PBR, linear HDR)
//     -> SkyboxPass (background, depth-tested)     [multiview scene target]
//     -> TonemapPass + ImGui in ONE backbuffer pass
//     -> optional screenshot copy -> present.
//
// recordFrame only sequences the passes and owns the frame-graph barriers
// between them; everything drawn lives in the pass objects (the renderEye()
// monolith this migration kills must not regrow here).
//
// MULTIVIEW BY DESIGN: the scene is always drawn into a layered offscreen
// target through VK_KHR_multiview (core 1.1). Camera data is a per-view
// array indexed by gl_ViewIndex in every shader. Mono is simply viewCount()
// == 1; quad-buffer stereo (Phase 7) raises it to 2 and resolves both layers
// to a stereo swapchain — nothing in the scene pass changes. Do not write
// single-view assumptions into passes or shaders.
//
// DESCRIPTORS: set 0 is ONE shared per-frame set (layout below) written once
// per frame and bound by every scene pipeline (via externalSetLayout); set 1
// is the MaterialSystem's persistent bindless set. Per-draw data rides push
// constants. GPU-visible structs live in assets/shaders_vk/scene_types.h
// (scalar layout, shared with GLSL — playbook A.1).
//
// Synchronization: one timeline semaphore paces the CPU against the GPU
// (per-slot wait before reuse — no fences); binary semaphores exist only
// where WSI demands them. All barriers/submits use synchronization2.
class Renderer {
public:
    static constexpr uint32_t kFramesInFlight = 2;

    // Where the frame time goes, exponentially smoothed. slotWaitMs is the
    // CPU blocking on this slot's previous GPU submission; acquireMs and
    // presentMs are the WSI calls — under FIFO those two carry the vsync
    // pacing, so they finger the culprit when the frame rate is wrong.
    struct FrameStats {
        float slotWaitMs = 0.0f;
        float acquireMs = 0.0f;
        float presentMs = 0.0f;
    };

    Renderer() = default;
    ~Renderer() { shutdown(); }
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void init(rhi::Device& device, rhi::Swapchain& swapchain,
              rhi::ShaderCompiler& shaderCompiler);
    void shutdown();

    // Rebuilds the size-dependent scene target after the swapchain changed.
    void onSwapchainRecreated();

    // Records + submits + presents one frame of the submitted scene.
    // uiDrawData may be null (UI not built this frame). OutOfDate means
    // nothing was presented — the caller recreates the swapchain and calls
    // onSwapchainRecreated(). Suboptimal still presented; the caller decides
    // whether a recreate is worth it.
    rhi::PresentResult renderFrame(const FrameSubmission& submission,
                                   ImDrawData* uiDrawData);

    uint32_t viewCount() const { return viewCount_; }
    const FrameStats& frameStats() const { return frameStats_; }

    // Scene-resource registries the loaders feed (valid after init()).
    MaterialSystem& materials() { return materials_; }
    SkyboxPass& skybox() { return skyboxPass_; }

    TonemapSettings& tonemapSettings() { return tonemapSettings_; }

    // Queues a capture of the next presented frame (tonemapped scene + UI) to
    // `path` as PNG. Completion is asynchronous — poll screenshotStatus().
    // False if a capture is already pending or the surface disallows copies.
    bool requestScreenshot(const std::string& path);
    bool screenshotPending() const { return screenshot_.state != Screenshot::State::Idle; }
    const std::string& screenshotStatus() const { return screenshotStatus_; }

private:
    struct FrameContext {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
        rhi::DescriptorAllocator descriptors;
        rhi::Buffer frameUbo;      // gpu::FrameData
        rhi::Buffer lightsBuffer;  // gpu::PointLightData[kMaxPointLights]
        rhi::Buffer materialsBuffer; // gpu::MaterialData[], grown on demand
        uint64_t submittedTimelineValue = 0;
    };

    // Deferred readback: the copy is recorded into the frame, completion is
    // detected by polling the frame timeline (no stall), then the PNG write
    // happens on the spot.
    struct Screenshot {
        enum class State { Idle, Requested, InFlight };
        State state = State::Idle;
        std::string path;
        rhi::Buffer readback;
        VkExtent2D extent{};
        VkFormat format = VK_FORMAT_UNDEFINED;
        uint64_t timelineValue = 0;
    };

    void createFrameContexts();
    void destroyFrameContexts();
    void createSceneTarget();
    void createFrameSetLayout();
    // Fills the frame UBO/SSBO staging structs from the submission; returns
    // the number of point-shadow slots ShadowPass assigned.
    uint32_t buildFrameData(const FrameSubmission& submission, gpu::FrameData& frame,
                            std::vector<gpu::PointLightData>& lights) const;
    void uploadFrameBuffers(FrameContext& frame, const gpu::FrameData& frameData,
                            const std::vector<gpu::PointLightData>& lights);
    VkDescriptorSet writeFrameSet(FrameContext& frame,
                                  const std::vector<gpu::PointLightData>& lights);
    void recordFrame(FrameContext& frame, uint32_t imageIndex,
                     const FrameSubmission& submission, VkDescriptorSet frameSet,
                     uint32_t shadowedLightCount, ImDrawData* uiDrawData,
                     bool captureThisFrame);
    void pollScreenshot(bool blockUntilDone);
    void finishScreenshot();

    rhi::Device* device_ = nullptr;
    rhi::Swapchain* swapchain_ = nullptr;
    rhi::ShaderCompiler* shaderCompiler_ = nullptr;

    uint32_t viewCount_ = 1; // mono today; 2 when stereo present lands (Phase 7)

    FrameContext frames_[kFramesInFlight];
    VkSemaphore frameTimeline_ = VK_NULL_HANDLE;
    uint64_t timelineValue_ = 0;
    uint32_t frameSlot_ = 0;
    FrameStats frameStats_;

    // Layered HDR scene target (color + depth), one layer per view.
    rhi::Texture sceneColor_;
    rhi::Texture sceneDepth_;
    VkExtent2D sceneExtent_{};
    static constexpr VkFormat kSceneColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    static constexpr VkFormat kSceneDepthFormat = VK_FORMAT_D32_SFLOAT;

    // Shared per-frame set layout (set 0 of every scene pipeline):
    //   0 UBO    FrameData                      (vertex | fragment)
    //   1 SSBO   PointLightData[]               (fragment)
    //   2 SSBO   MaterialData[]                 (fragment)
    //   3 CIS    sun shadow map + compare       (fragment)
    //   4 CIS    point shadow cube array + cmp  (fragment)
    //   5 CIS    sky cubemap                    (fragment)
    //   6 CIS    sky equirect                   (fragment)
    VkDescriptorSetLayout frameSetLayout_ = VK_NULL_HANDLE;

    MaterialSystem materials_;
    ShadowPass shadowPass_;
    ForwardPass forwardPass_;
    SkyboxPass skyboxPass_;
    TonemapPass tonemapPass_;
    TonemapSettings tonemapSettings_;
    VkFormat tonemapTargetFormat_ = VK_FORMAT_UNDEFINED;

    Screenshot screenshot_;
    std::string screenshotStatus_;
};

} // namespace renderer
