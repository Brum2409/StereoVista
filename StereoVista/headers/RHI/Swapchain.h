#pragma once

#include "RHI/VulkanCommon.h"

#include <vector>

namespace rhi {

class Device;

// Outcome of a queue present. Suboptimal still presented — the caller may
// keep going and only needs to recreate when the size actually changed
// (recreating on every Suboptimal risks a waitIdle+recreate loop on drivers
// that report it persistently).
enum class PresentResult { Success, Suboptimal, OutOfDate };

// Display label for the debug panel's present-mode selector.
inline const char* presentModeName(VkPresentModeKHR mode) {
    switch (mode) {
    case VK_PRESENT_MODE_IMMEDIATE_KHR: return "Immediate (no vsync)";
    case VK_PRESENT_MODE_MAILBOX_KHR: return "Mailbox (uncapped vsync)";
    case VK_PRESENT_MODE_FIFO_KHR: return "FIFO (vsync)";
    case VK_PRESENT_MODE_FIFO_RELAXED_KHR: return "FIFO relaxed";
    default: return "Other";
    }
}

// Present-side of the frame. Mono presents imageArrayLayers = 1; quad-buffer
// stereo (Phase 7) presents 2 layers.
//
// STEREO PRESENT (Phase 7): quad-buffer stereo present in Vulkan is a swapchain
// property — imageArrayLayers = 2, available when the surface reports
// maxImageArrayLayers >= 2 (workstation GPUs with a stereo display; consumer
// GeForce/Radeon expose 1). stereoPresentSupported() is that runtime probe;
// setStereo(true) requests it (honored on the next create/recreate, clamped to
// the surface cap). The presented image then carries layer 0 = left eye,
// layer 1 = right eye, and the driver routes them to the display's back
// buffers — vkQueuePresentKHR itself is unchanged. The renderer resolves its
// layered multiview scene target into these per-layer views; nothing in the
// scene render path changes.
class Swapchain {
public:
    Swapchain() = default;
    ~Swapchain() { shutdown(); }
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;

    void init(Device& device, uint32_t width, uint32_t height);
    void shutdown();

    // Recreates for the current surface size (window resize / out-of-date).
    // Width/height are the framebuffer size to clamp against surface caps.
    void recreate(uint32_t width, uint32_t height);

    // Requests a stereo (2-layer) swapchain for the next create/recreate. Only
    // takes effect when the surface actually supports it (stereoPresentSupported);
    // otherwise the swapchain stays mono. Call recreate() to apply.
    void setStereo(bool want) { wantStereo_ = want; }
    // Actual layer count of the current swapchain images (1 mono, 2 stereo).
    uint32_t layers() const { return layers_; }

    // UINT32_MAX on VK_ERROR_OUT_OF_DATE_KHR (caller recreates and retries).
    uint32_t acquireImage(VkSemaphore signalWhenAvailable);

    PresentResult present(VkQueue queue, uint32_t imageIndex);

    // Preference takes effect on the next create/recreate; FIFO (the only
    // guaranteed mode) is the fallback when the surface lacks the preference.
    void setPreferredPresentMode(VkPresentModeKHR mode) { preferredPresentMode_ = mode; }
    VkPresentModeKHR presentMode() const { return presentMode_; }
    const std::vector<VkPresentModeKHR>& availablePresentModes() const {
        return availablePresentModes_;
    }

    VkFormat format() const { return format_; }
    VkExtent2D extent() const { return extent_; }
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }
    VkImage image(uint32_t i) const { return images_[i]; }
    // Layer-0 (left/mono) single-layer view — the existing mono/UI callers.
    VkImageView imageView(uint32_t i) const { return imageViews_[i * layers_]; }
    // Per-layer single-layer view for the stereo backbuffer resolve
    // (layer 0 = left, 1 = right). layer must be < layers().
    VkImageView imageView(uint32_t i, uint32_t layer) const {
        return imageViews_[i * layers_ + layer];
    }

    // Present-completion wait semaphore, one per swapchain image (a per-frame
    // semaphore could still be in flight for an image the presenter holds).
    VkSemaphore renderFinishedSemaphore(uint32_t imageIndex) const {
        return renderFinished_[imageIndex];
    }

    bool stereoPresentSupported() const { return maxSurfaceLayers_ >= 2; }

    // Swapchain images carry TRANSFER_SRC (surface permitting), so the
    // presented frame can be copied out for screenshots.
    bool supportsCapture() const { return supportsCapture_; }

private:
    void create(uint32_t width, uint32_t height, VkSwapchainKHR old);
    void destroyViews();

    Device* device_ = nullptr;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkPresentModeKHR preferredPresentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    VkPresentModeKHR presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkPresentModeKHR> availablePresentModes_;
    VkFormat format_ = VK_FORMAT_UNDEFINED;
    VkColorSpaceKHR colorSpace_ = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkExtent2D extent_{};
    std::vector<VkImage> images_;
    // Single-layer 2D views, layers_ per image, flattened [image*layers_ + layer].
    std::vector<VkImageView> imageViews_;
    std::vector<VkSemaphore> renderFinished_;
    uint32_t maxSurfaceLayers_ = 1;
    bool wantStereo_ = false; // requested; applied at create() if supported
    uint32_t layers_ = 1;     // actual imageArrayLayers of the current swapchain
    bool supportsCapture_ = false;
};

} // namespace rhi
