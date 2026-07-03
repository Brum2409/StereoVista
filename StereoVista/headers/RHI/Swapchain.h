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

// Present-side of the frame. Phase 1 presents mono (imageArrayLayers = 1).
//
// STEREO SEAM (Phase 7): quad-buffer stereo present in Vulkan is a swapchain
// property — imageArrayLayers = 2, available when the surface reports
// maxImageArrayLayers >= 2 (workstation GPUs with a stereo display; consumer
// GeForce/Radeon expose 1). stereoPresentSupported() is that runtime probe.
// The renderer always draws into its own layered multiview target, so
// enabling stereo present later only changes this class (2 layers + per-layer
// copy) — not the render path.
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
    VkImageView imageView(uint32_t i) const { return imageViews_[i]; }

    // Present-completion wait semaphore, one per swapchain image (a per-frame
    // semaphore could still be in flight for an image the presenter holds).
    VkSemaphore renderFinishedSemaphore(uint32_t imageIndex) const {
        return renderFinished_[imageIndex];
    }

    bool stereoPresentSupported() const { return maxSurfaceLayers_ >= 2; }

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
    std::vector<VkImageView> imageViews_;
    std::vector<VkSemaphore> renderFinished_;
    uint32_t maxSurfaceLayers_ = 1;
};

} // namespace rhi
