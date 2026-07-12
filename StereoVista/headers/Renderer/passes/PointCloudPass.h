#pragma once

#include "RHI/Buffer.h"
#include "RHI/Pipeline.h"
#include "RHI/VulkanCommon.h"
#include "Renderer/FrameSubmission.h"
#include "Renderer/GpuTypes.h"

#include <cstdint>
#include <vector>

namespace rhi {
class Device;
class ShaderCompiler;
}

namespace renderer {

// Schütz compute point-cloud rasterizer (Phase 5) — the Vulkan rewrite of
// Engine::ComputePointCloudRenderer.
//
// Standard path per frame (per-view sections of the per-pixel buffers; a
// VIEW is one (viewport, eye) pair — multiple docked viewports and stereo
// eyes are the same mechanism):
//   clear 64-bit framebuffer -> rasterize dispatches (one workgroup per
//   batch, SINGLE-PASS MULTI-VIEW: each point is read and decoded once, then
//   projected + atomicMin'd (dist24|cloudID|index) once per (viewport, eye)
//   — neither stereo/XR nor extra viewports rerun the geometry phase) ->
//   colour lookup dispatch per view -> fullscreen resolve drawn INSIDE each
//   viewport's scene pass after the opaque meshes (writes gl_FragDepth;
//   reverse-Z GREATER composites points against mesh depth and updates the
//   depth attachment).
//
// HQS path swaps the middle for depth (atomicMin linear eye depth) ->
// colour accumulate (atomicAdd within the relative depth window) -> HQS
// resolve, averaging every point near the visible surface.
//
// Everything the shaders consume travels as buffer device addresses in ONE
// PointCloudDispatch struct per (cloud, view) — the whole pipeline binds
// ZERO descriptor sets (playbook A.2). Fixes carried over from the GL
// shaders: core int64 atomics only (C.1) and [0,1] reverse-Z depth math
// (C.2).
//
// Frame protocol (driven by Renderer):
//   prepare()        before recording — sizes buffers, uploads dispatch data
//   recordCompute()  the clears + dispatches. Two homes (Renderer decides per
//                    frame): the ASYNC COMPUTE queue's command buffer
//                    (asyncQueue=true — the default frame path, overlapped
//                    with the shadow + forward graphics work and ordered by
//                    the Renderer's timeline semaphores), or inline in the
//                    frame command buffer before the scene pass
//                    (asyncQueue=false — single-queue fallback, ordered by
//                    queue-scoped barriers as before)
//   recordResolve()  inside the scene render pass, after ForwardPass
//   onSwapchainRecreated()  device idle — drops the size-dependent buffers
//                           (recreated lazily by the next prepare)
//
// Cross-queue state: every buffer the dispatches touch that the graphics
// queue also touches (the per-pixel buffers read by the resolve, the cloud
// geometry streamed by the upload ring, the dispatch data) is created with
// BufferDesc::shareGraphicsCompute, so no queue-family ownership transfers
// appear anywhere in the pass.
class PointCloudPass {
public:
    PointCloudPass() = default;
    ~PointCloudPass() { shutdown(); }
    PointCloudPass(const PointCloudPass&) = delete;
    PointCloudPass& operator=(const PointCloudPass&) = delete;

    void init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
              VkFormat colorFormat, VkFormat depthFormat, uint32_t viewMask,
              uint32_t framesInFlight);
    void shutdown();
    void onSwapchainRecreated();

    // One docked viewport's contribution to the frame: target extent + its
    // per-eye cameras (kMaxViews entries). extent {0,0} (or views == null)
    // skips the viewport for the frame — its GUI tab is hidden.
    struct ViewportInput {
        VkExtent2D extent{ 0, 0 };
        const ViewCamera* views = nullptr;
    };

    // Builds this frame's dispatch list into the slot's staging buffer
    // (recordCompute copies it device-local before the dispatches). ONE flat
    // view list covers every (viewport, eye): viewportCount * viewCount <=
    // SV_PC_MAX_VIEWS, each view with its own extent + per-pixel sections.
    // No-op frame (no visible clouds / no visible viewport) leaves the pass
    // inactive.
    void prepare(const FrameSubmission& submission, uint32_t frameSlot,
                 const ViewportInput* viewports, uint32_t viewportCount,
                 uint32_t viewCount);

    bool active() const { return active_; }

    // Clears + compute dispatches + barriers. asyncQueue=true when cmd will
    // be submitted on the async compute queue: the cross-frame and
    // compute->fragment hand-off barriers are omitted (the Renderer's
    // semaphore chain owns that ordering, and their graphics stage bits are
    // illegal on a compute-only family); the internal copy/clear->compute and
    // compute->compute barriers are recorded either way.
    void recordCompute(VkCommandBuffer cmd, bool asyncQueue);
    // Fullscreen composite for ONE viewport (multiview across its eyes); call
    // inside that viewport's scene pass after opaque geometry. No-op for
    // viewports skipped by prepare().
    void recordResolve(VkCommandBuffer cmd, uint32_t viewportIndex) const;

private:
    void ensureTargets(VkDeviceSize totalPixels);

    rhi::Device* device_ = nullptr;

    // The three GEOMETRY kernels come in two specializations of ONE shader
    // (spec constant 0 = the per-invocation view-array capacity): [0] =
    // kMaxViews, bound whenever the frame's flat view count fits one
    // viewport's eyes — the single-viewport hot path keeps exactly its
    // pre-multi-viewport register/scratch footprint — and [1] = kMaxViews *
    // kMaxViewports, bound only on frames rendering several viewports.
    // Lookup/resolve hold no per-view arrays and need no variants.
    rhi::Pipeline rasterizePipelines_[2];
    rhi::Pipeline hqsDepthPipelines_[2];
    rhi::Pipeline hqsColorPipelines_[2];
    rhi::Pipeline lookupPipeline_;
    rhi::Pipeline resolvePipeline_;
    rhi::Pipeline hqsResolvePipeline_;

    // Pass-owned per-pixel buffers, one section per flat (viewport, eye) view
    // (view-major; sections differ in size when viewports do). Created lazily
    // on the first frame with clouds so mesh-only scenes never pay the VRAM;
    // the HQS pair additionally waits for the first HQS frame.
    rhi::Buffer framebuffer_; // uint64 (dist24|cloudID|index), sentinel ~0
    rhi::Buffer colorbuffer_; // uint packed RGBA8 (lookup output)
    rhi::Buffer hqsDepth_;    // uint nearest linear depth bits, sentinel ~0
    rhi::Buffer hqsAccum_;    // uint x4 R,G,B,count

    // Per-frame-in-flight dispatch-data buffers, grown on demand. The
    // geometry passes read the whole PointCloudDispatch per workgroup and the
    // lookup pass dereferences it per pixel, so the shaders must NEVER read
    // it from host-visible memory (PCIe): prepare() writes the staging half,
    // recordCompute() copies it into the device-local half that every BDA in
    // the pass points at. Per-slot so growth can recreate a buffer without
    // racing the previous frame (the slot's last submission has retired).
    std::vector<rhi::Buffer> dispatchStaging_; // HostUpload, TRANSFER_SRC
    std::vector<rhi::Buffer> dispatchDevice_;  // GpuOnly, BDA target

    // ---- per-frame state written by prepare() ----
    struct DispatchRecord {
        VkDeviceAddress dispatchData = 0; // (cloud, view 0) struct; the shader
                                          // strides to the sibling views
        uint32_t numBatches = 0;
    };
    // One flat (viewport, eye) view: extent, camera, and its section of the
    // per-pixel buffers (element offset — byte scaling depends on the buffer).
    struct FlatView {
        uint32_t viewport = 0;
        VkExtent2D extent{ 0, 0 };
        const ViewCamera* camera = nullptr;
        VkDeviceSize pixelOffset = 0;
        uint32_t pixels = 0;
    };
    // Per-viewport fullscreen-resolve state (multiview across that viewport's
    // eyes); inactive when the viewport was skipped this frame.
    struct ResolveRecord {
        bool active = false;
        gpu::PointCloudResolvePush push{};
        gpu::PointCloudHqsResolvePush hqsPush{};
    };
    std::vector<DispatchRecord> dispatches_; // one per cloud (all views)
    std::vector<FlatView> flatViews_;        // scratch, rebuilt per frame
    std::vector<gpu::PointCloudLookupPush> lookupPushes_; // one per flat view
    std::vector<gpu::PointCloudDispatch> hostData_; // scratch, reused per frame
    ResolveRecord resolves_[kMaxViewports];
    bool active_ = false;
    bool hqsActive_ = false;
    uint32_t viewCount_ = 1;  // eyes per viewport
    uint32_t totalViews_ = 0; // flat views this frame (<= SV_PC_MAX_VIEWS)
    // Staging -> device dispatch-data copy recorded by recordCompute().
    uint32_t preparedSlot_ = 0;
    VkDeviceSize dispatchCopyBytes_ = 0;

    uint32_t maxGroupsX_ = 65535;
    bool warnedCloudCount_ = false;
};

} // namespace renderer
