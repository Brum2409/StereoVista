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
// Standard path per frame (per view via multiview-shaped buffer sections):
//   clear 64-bit framebuffer -> rasterize dispatches (one workgroup per
//   batch; atomicMin of (dist24|cloudID|index)) -> per-cloud colour lookup
//   dispatches -> fullscreen resolve drawn INSIDE the scene pass after the
//   opaque meshes (writes gl_FragDepth; reverse-Z GREATER composites points
//   against mesh depth and updates the depth attachment).
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
//   recordCompute()  in the frame command buffer, before the scene pass
//   recordResolve()  inside the scene render pass, after ForwardPass
//   onSwapchainRecreated()  device idle — drops the size-dependent buffers
//                           (recreated lazily by the next prepare)
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

    // Builds this frame's dispatch list into the slot's staging buffer
    // (recordCompute copies it device-local before the dispatches).
    // No-op frame (no visible clouds) leaves the pass inactive.
    void prepare(const FrameSubmission& submission, uint32_t frameSlot,
                 VkExtent2D extent, uint32_t viewCount);

    bool active() const { return active_; }

    // Clears + compute dispatches + barriers; call before the scene pass.
    void recordCompute(VkCommandBuffer cmd);
    // Fullscreen composite; call inside the scene pass after opaque geometry.
    void recordResolve(VkCommandBuffer cmd) const;

private:
    void ensureTargets(uint32_t viewCount);

    rhi::Device* device_ = nullptr;

    rhi::Pipeline rasterizePipeline_;
    rhi::Pipeline lookupPipeline_;
    rhi::Pipeline hqsDepthPipeline_;
    rhi::Pipeline hqsColorPipeline_;
    rhi::Pipeline resolvePipeline_;
    rhi::Pipeline hqsResolvePipeline_;

    // Pass-owned per-pixel buffers, pixelsPerView_ * viewCount entries each
    // (view-major sections). Created lazily on the first frame with clouds so
    // mesh-only scenes never pay the VRAM; the HQS pair additionally waits
    // for the first HQS frame.
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
        VkDeviceAddress dispatchData = 0;
        uint32_t numBatches = 0;
    };
    std::vector<DispatchRecord> dispatches_; // cloud-major, view-minor
    std::vector<gpu::PointCloudLookupPush> lookupPushes_; // one per view
    std::vector<gpu::PointCloudDispatch> hostData_; // scratch, reused per frame
    bool active_ = false;
    bool hqsActive_ = false;
    uint32_t pixelsPerView_ = 0;
    uint32_t viewCount_ = 1;
    // Staging -> device dispatch-data copy recorded by recordCompute().
    uint32_t preparedSlot_ = 0;
    VkDeviceSize dispatchCopyBytes_ = 0;
    gpu::PointCloudResolvePush resolvePush_{};
    gpu::PointCloudHqsResolvePush hqsResolvePush_{};

    uint32_t maxGroupsX_ = 65535;
    bool warnedCloudCount_ = false;
};

} // namespace renderer
