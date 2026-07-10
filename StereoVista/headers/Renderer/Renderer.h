#pragma once

#include "RHI/Buffer.h"
#include "RHI/DescriptorAllocator.h"
#include "RHI/Pipeline.h"
#include "RHI/Swapchain.h"
#include "RHI/Texture.h"
#include "RHI/UploadRing.h"
#include "RHI/VulkanCommon.h"
#include "Renderer/FrameSubmission.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/MaterialSystem.h"
#include "Renderer/passes/ForwardPass.h"
#include "Renderer/passes/OverlayPass.h"
#include "Renderer/passes/PointCloudPass.h"
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
//     -> PointCloudPass compute (Schütz rasterize/HQS + barriers)
//     -> ForwardPass (single metallic-roughness PBR, linear HDR)
//     -> PointCloudPass resolve (depth-composited fullscreen draw)
//     -> SkyboxPass (background, depth-tested)     [multiview scene target]
//     -> depth-picking rect copies (scene depth -> per-slot readback)
//     -> TonemapPass + OverlayPass in one backbuffer pass (scene depth
//        attached: overlays depth-test post-tonemap, matching GL where
//        tonemap lived in the mesh shader under the overlays)
//     -> ImGui in its own backbuffer pass (no depth)
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
//
// ASYNC COMPUTE (docs/TODO.md §G): the point-cloud compute (dispatch-data
// copy + clears + rasterize/HQS + lookup) is submitted on the device's async
// compute queue, overlapped with the same frame's upload + shadow + forward
// graphics work. The frame is three submissions chained by timelines:
//
//   graphics batch 1  [ring flush | aux | shadow]
//       waits  computeTimeline >= prev   @ALL_TRANSFER   (WAR: streamed
//              copies may rewrite cloud ranges the previous frame's compute
//              still reads — resort-in-place)
//       signals uploadTimeline = N       @ALL_TRANSFER   (fires when the
//              flush copies retire, NOT when shadow finishes)
//   compute           [dispatch copy | clears | rasterize/HQS | lookup]
//       waits  frameTimeline  >= N-1     @ALL_COMMANDS   (prev frame's
//              resolve was the last reader of the per-pixel buffers)
//       waits  uploadTimeline >= N       @COMPUTE        (cloud chunks
//              staged THIS frame are flushed in batch 1)
//       signals computeTimeline = cN     @ALL_COMMANDS
//   graphics batch 2  [scene pass | resolve | tonemap | overlay | UI]
//       waits  acquire                   @COLOR_ATTACHMENT_OUTPUT
//       waits  computeTimeline >= cN     @FRAGMENT       (the fullscreen
//              resolve reads the per-pixel buffers via BDA)
//       signals frameTimeline = N, renderFinished
//
// Timeline waits may be submitted before their signals (wait-before-signal),
// so submission order across the two queues is free. Cross-queue buffers are
// CONCURRENT-shared (BufferDesc::shareGraphicsCompute) — zero queue-family
// ownership transfers. Devices without a distinct compute queue (and the
// debug-panel toggle) fall back to recording the same dispatches inline
// before the scene pass, ordered by queue-scoped barriers as before; the
// fallback is decided per frame, so the toggle is always safe.
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

    // Switches how many views the scene renders (1 = mono, 2 = stereo). Rebuilds
    // the layered scene target + the multiview scene-pass pipelines (device idle;
    // rare — a stereo-mode toggle). No-op when unchanged. The backbuffer resolve
    // layout (mono / quad-buffer / side-by-side) is then derived each frame from
    // viewCount_ and the swapchain's layer count, so switching side-by-side <->
    // quad-buffer (both 2 views) only needs a swapchain recreate, not this.
    void setViewCount(uint32_t count);
    bool stereo() const { return viewCount_ > 1; }

    // ---- OpenXR HMD output (Phase 7b) ----
    // Enter/leave XR mode: size the layered scene target to the HMD eye
    // resolution (both eyes = 2 views) instead of the window, and rebuild the
    // multiview scene pipelines. endXR() restores the window-sized mono target;
    // the app re-applies its desktop stereo mode afterwards. Device-idle, rare.
    void beginXR(VkExtent2D eyeExtent);
    void endXR();
    bool xrActive() const { return xrActive_; }

    // One acquired HMD eye swapchain image (from Engine::XRSession): the VkImage
    // (for the layout barrier) + a color VkImageView (the resolve attachment).
    struct XrEyeTarget {
        VkImage image = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
    };

    // Records + submits one XR frame: the same multiview scene pass as the
    // desktop path (already 2-view), then a per-eye resolve into the two HMD
    // eye images at eye resolution. The window backbuffer ALWAYS receives ImGui
    // (so the GUI stays usable to leave VR) plus, when mirrorToWindow, the left
    // eye behind it. eyeSrgb: the eye swapchain is an _SRGB format (resolve
    // outputs linear, hardware encodes). Returns the window present result
    // (OutOfDate if the window swapchain needs recreating — XR still rendered).
    rhi::PresentResult renderFrameXR(const FrameSubmission& submission,
                                     const XrEyeTarget eyes[2], bool eyeSrgb,
                                     bool mirrorToWindow, ImDrawData* uiDrawData);

    // Records + submits + presents one frame of the submitted scene.
    // uiDrawData may be null (UI not built this frame). OutOfDate means
    // nothing was presented — the caller recreates the swapchain and calls
    // onSwapchainRecreated(). Suboptimal still presented; the caller decides
    // whether a recreate is worth it.
    rhi::PresentResult renderFrame(const FrameSubmission& submission,
                                   ImDrawData* uiDrawData);

    uint32_t viewCount() const { return viewCount_; }
    const FrameStats& frameStats() const { return frameStats_; }

    // ---- Async compute (see the class comment) ----
    // Enabled by default whenever the device exposes a distinct compute
    // queue; the setter is the debug-panel toggle and may flip every frame.
    void setAsyncCompute(bool enabled) { asyncComputeRequested_ = enabled; }
    bool asyncComputeSupported() const;
    bool asyncComputeEnabled() const {
        return asyncComputeRequested_ && asyncComputeSupported();
    }
    // True when the last submitted frame actually placed compute work on the
    // async queue (enabled AND point clouds were visible that frame).
    bool asyncComputeActive() const { return lastFrameAsyncCompute_; }

    // ---- Depth picking (Phase 6) ----
    // Results of the depth rectangles requested via FrameSubmission
    // ::depthQueries, published when the frame that copied them retires
    // (typically kFramesInFlight frames later). The result type (DepthReadback,
    // defined in FrameSubmission.h next to DepthQueryRect) carries the
    // invViewProj OF THE FRAME THAT RENDERED THE DEPTH, so world positions are
    // reconstructed with that matrix, not the current camera — camera motion
    // can't smear the picked point.
    const DepthReadback& depthSamples() const { return depthResult_; }

    // Scene-resource registries the loaders feed (valid after init()).
    MaterialSystem& materials() { return materials_; }
    SkyboxPass& skybox() { return skyboxPass_; }

    // Per-frame staging ring for the streaming loaders (Phase 4): copies
    // staged anywhere in the frame are recorded at the top of the next frame
    // command buffer; space retires against the frame timeline.
    rhi::UploadRing& uploadRing() { return uploadRing_; }

    // Frame-timeline sync points for streaming systems that defer GPU
    // destruction (M2). frameRetireValue() is the value the NEXT submission
    // will signal — releasing a resource between frames, it upper-bounds
    // every frame that may still reference it (in-flight binds AND ring
    // copies flushed into the next frame). completedFrameValue() is the
    // counter the GPU has retired; a deferred destroy with retireValue <= it
    // is safe now.
    uint64_t frameRetireValue() const { return timelineValue_ + 1; }
    uint64_t completedFrameValue() const;

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
        // Two graphics command buffers per slot, submitted as two batches of
        // ONE vkQueueSubmit2: preCommandBuffer carries the upload flush + aux
        // work + shadow passes (its transfer stages signal the upload
        // timeline so the async compute dispatches may consume this frame's
        // streamed cloud chunks); commandBuffer carries the scene pass and
        // everything after it (and waits the compute timeline).
        VkCommandBuffer preCommandBuffer = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        // Async compute: per-slot pool (created on the compute queue family)
        // + the command buffer submitted on Device::computeQueue(). Recorded
        // only on frames that take the async path; resetting with the slot is
        // safe because the slot's graphics retirement implies its compute
        // submission retired (the scene batch waited on it).
        VkCommandPool computePool = VK_NULL_HANDLE;
        VkCommandBuffer computeCommandBuffer = VK_NULL_HANDLE;
        VkSemaphore acquireSemaphore = VK_NULL_HANDLE;
        rhi::DescriptorAllocator descriptors;
        rhi::Buffer frameUbo;      // gpu::FrameData
        rhi::Buffer lightsBuffer;  // gpu::PointLightData[kMaxPointLights]
        rhi::Buffer materialsBuffer; // gpu::MaterialData[], grown on demand
        uint64_t submittedTimelineValue = 0;
        // Depth-picking readback owned by this slot: the copy is recorded
        // into this slot's frame; the result is parsed when the slot's
        // submission retires (the next time the slot is reused).
        rhi::Buffer depthReadback;
        DepthReadback pendingDepth; // valid=true while a copy is in flight
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
    void destroySceneDepthLayerViews();
    void createFrameSetLayout();
    // Rebuilds the view-count-dependent GPU state (layered scene target +
    // multiview scene-pass pipelines + dropped depth samples). Shared by
    // setViewCount / beginXR / endXR — it always rebuilds (no unchanged-count
    // early-out), so a same-count extent change (window <-> HMD eye-res) applies.
    void rebuildViewDependentState();

    // Per-frame slot lifecycle shared by renderFrame / renderFrameXR: block on
    // this slot's previous submission, publish its completed depth pick, and
    // reclaim retired upload-ring space.
    void beginFrameSlot(FrameContext& frame);
    // Clamp submission.depthQueries to the scene extent, size the slot readback,
    // and arm frame.pendingDepth for this frame's copy (view 0).
    void armDepthPick(FrameContext& frame, const FrameSubmission& submission);
    // First graphics batch (uploads -> aux -> shadow), recorded into
    // frame.preCommandBuffer. The caller owns vkBeginCommandBuffer/End.
    void recordPreScene(VkCommandBuffer cmd, const FrameSubmission& submission,
                        VkDescriptorSet frameSet, uint32_t shadowedLightCount);
    // Decides this frame's compute path and, on the async path, records the
    // slot's compute command buffer (begin/end included). Returns true when a
    // compute submission must accompany this frame; false = the point-cloud
    // compute (if any) records inline in the scene batch.
    bool recordAsyncCompute(FrameContext& frame);
    // Scene recording for the second graphics batch ([inline point-cloud
    // compute when !asyncCompute] -> scene target barriers -> multiview scene
    // pass -> depth-pick copies). Leaves sceneColor_ in
    // COLOR_ATTACHMENT_OPTIMAL and sceneDepth_ in DEPTH_ATTACHMENT_OPTIMAL. The
    // caller owns vkBeginCommandBuffer/End and the post-scene resolve.
    void recordScene(VkCommandBuffer cmd, FrameContext& frame,
                     const FrameSubmission& submission, VkDescriptorSet frameSet,
                     bool asyncCompute);
    void recordFrameXR(FrameContext& frame, const XrEyeTarget eyes[2], bool eyeSrgb,
                       uint32_t windowImageIndex, bool haveWindow, bool doMirror,
                       const FrameSubmission& submission, VkDescriptorSet frameSet,
                       bool asyncCompute, ImDrawData* uiDrawData);
    // Submits the frame: the compute batch on the async queue (when
    // computeActive) plus the two graphics batches, wiring the timeline chain
    // documented in the class comment. Advances timelineValue_ /
    // uploadTimelineValue_ / computeTimelineValue_, stamps
    // frame.submittedTimelineValue and notifies the upload ring.
    // renderFinished may be VK_NULL_HANDLE (XR frame without a window image);
    // waitAcquire gates the frame.acquireSemaphore wait the same way.
    void submitFrame(FrameContext& frame, bool computeActive, bool waitAcquire,
                     VkSemaphore renderFinished);

    // One backbuffer-resolve target per eye, derived from viewCount_ + the
    // swapchain layer count: mono = 1 full-window eye; quad-buffer stereo = 2
    // full-window eyes into the 2 swapchain layers; side-by-side = 2 half-window
    // eyes into the single (mono) backbuffer layer.
    struct EyeResolve {
        VkRect2D rect;             // on-screen destination (framebuffer px)
        uint32_t sceneLayer;       // scene-target array layer (eye) to sample
        uint32_t backbufferLayer;  // swapchain image layer to render into
        bool overlayDepthTest;     // true = per-eye scene depth; false = overlays on top
    };
    void buildEyeLayout(EyeResolve out[kMaxViews], uint32_t& outCount) const;
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
                     bool asyncCompute, ImDrawData* uiDrawData,
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
    // Async-compute chain (class comment): computeTimeline_ is signaled by
    // every compute-queue submission and waited by the scene batch (and, at
    // transfer stages, by the NEXT frame's upload batch — the WAR guard);
    // uploadTimeline_ is signaled by each frame's upload/shadow batch at its
    // transfer stages and waited by that frame's compute submission. Both
    // count monotonically; the *_Value_ members are the last SIGNALED values.
    VkSemaphore computeTimeline_ = VK_NULL_HANDLE;
    uint64_t computeTimelineValue_ = 0;
    VkSemaphore uploadTimeline_ = VK_NULL_HANDLE;
    uint64_t uploadTimelineValue_ = 0;
    bool asyncComputeRequested_ = true; // debug-panel toggle (default on)
    bool lastFrameAsyncCompute_ = false;
    uint32_t frameSlot_ = 0;
    FrameStats frameStats_;

    rhi::UploadRing uploadRing_;
    static constexpr VkDeviceSize kUploadRingBytes = 64ull << 20;

    // Layered HDR scene target (color + depth), one layer per view.
    rhi::Texture sceneColor_;
    rhi::Texture sceneDepth_;
    // Single-layer depth views (one per view) for the per-eye backbuffer resolve
    // attachment; the multiview scene pass uses sceneDepth_.view() (the array view).
    VkImageView sceneDepthLayerViews_[kMaxViews]{};
    VkExtent2D sceneExtent_{};
    // When non-zero, createSceneTarget sizes the scene target to this instead of
    // the swapchain extent (Phase 7b: the HMD eye resolution while in XR).
    VkExtent2D sceneExtentOverride_{ 0, 0 };
    bool xrActive_ = false;
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
    PointCloudPass pointCloudPass_;
    SkyboxPass skyboxPass_;
    OverlayPass overlayPass_;
    TonemapPass tonemapPass_;
    TonemapSettings tonemapSettings_;
    VkFormat tonemapTargetFormat_ = VK_FORMAT_UNDEFINED;

    DepthReadback depthResult_;

    Screenshot screenshot_;
    std::string screenshotStatus_;
};

} // namespace renderer
