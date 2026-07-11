#pragma once

#include "RHI/Buffer.h"
#include "RHI/Pipeline.h"
#include "RHI/VulkanCommon.h"
#include "Renderer/OverlayDrawList.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace rhi {
class Device;
class ShaderCompiler;
}

namespace renderer {

// ============================================================================
// The ONE overlay renderer (Phase 6, playbook C.9): draws an OverlayDrawList
// with a single triangle-list pipeline. Depth test / compare / write and cull
// mode are core-1.3 dynamic state, so always-on-top, depth-tested, x-ray and
// the sphere cursor's two-pass culling all share the pipeline object.
//
// The main instance draws on the BACKBUFFER after tonemap with the scene
// depth attached (reverse-Z), matching where the GL overlays effectively
// blended (GL baked tonemap into the mesh shader, so its overlays also drew
// over display-ready colors). A second instance renders CursorPreview3D's
// offscreen thumbnail — same pipeline recipe, different formats.
//
// View/camera data rides ONE buffer device address in the push constants
// (playbook A.2): the pipeline binds ZERO descriptor sets.
//
// Frame protocol: prepare() before recording uploads the vertices + view
// params into the slot's HostUpload buffer; record() inside the caller's
// render pass emits the draws.
// ============================================================================

struct OverlayViewInfo {
    glm::mat4 viewProj{ 1.0f };
    glm::vec2 viewportPx{ 1.0f };
    glm::vec3 cameraPos{ 0.0f };
};

class OverlayPass {
public:
    OverlayPass() = default;
    ~OverlayPass() { shutdown(); }
    OverlayPass(const OverlayPass&) = delete;
    OverlayPass& operator=(const OverlayPass&) = delete;

    // viewMask 0 = plain (backbuffer today, preview); non-zero enables
    // multiview for the Phase 7 stereo backbuffer.
    void init(rhi::Device& device, rhi::ShaderCompiler& shaderCompiler,
              VkFormat colorFormat, VkFormat depthFormat, uint32_t viewMask,
              uint32_t framesInFlight);
    void shutdown();

    // Uploads this frame's geometry. Safe when the slot's previous submission
    // has retired (the Renderer waits on the frame timeline before reuse).
    void prepare(uint32_t frameSlot, const OverlayDrawList& list,
                 const OverlayViewInfo* views, uint32_t viewCount);

    bool active() const { return active_; }

    // Records the draws inside the caller's active render pass (viewport and
    // scissor already set). viewIndex selects the OverlayViewParams entry.
    // forceOnTop draws every batch with no depth test/write (side-by-side
    // stereo, where the squished eye image can't share the full-res scene depth).
    void record(VkCommandBuffer cmd, uint32_t viewIndex = 0, bool forceOnTop = false) const;

private:
    rhi::Device* device_ = nullptr;
    rhi::Pipeline pipeline_;

    struct SlotBuffers {
        rhi::Buffer vertices;   // OverlayVertex[] (HostUpload, grown on demand)
        rhi::Buffer viewParams; // gpu::OverlayViewParams[kMaxViews * kMaxViewports]
    };
    std::vector<SlotBuffers> slots_;

    // Per-frame state written by prepare().
    std::vector<OverlayBatch> batches_;
    VkBuffer vertexBuffer_ = VK_NULL_HANDLE;
    VkDeviceAddress viewParamsAddress_ = 0;
    bool active_ = false;
};

} // namespace renderer
