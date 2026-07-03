#include "RHI/Swapchain.h"

#include "RHI/Device.h"

#include <algorithm>
#include <iostream>

namespace rhi {

void Swapchain::init(Device& device, uint32_t width, uint32_t height) {
    device_ = &device;
    create(width, height, VK_NULL_HANDLE);
}

void Swapchain::recreate(uint32_t width, uint32_t height) {
    device_->waitIdle();
    VkSwapchainKHR old = swapchain_;
    destroyViews();
    create(width, height, old);
    if (old != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device_->device(), old, nullptr);
}

void Swapchain::create(uint32_t width, uint32_t height, VkSwapchainKHR old) {
    VkPhysicalDevice gpu = device_->physicalDevice();
    VkSurfaceKHR surface = device_->surface();

    VkSurfaceCapabilitiesKHR caps{};
    VK_CHECK(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(gpu, surface, &caps));
    maxSurfaceLayers_ = caps.maxImageArrayLayers;
    if (old == VK_NULL_HANDLE)
        std::cout << "[vulkan] surface maxImageArrayLayers = " << maxSurfaceLayers_
                  << (stereoPresentSupported() ? " (stereo present available)"
                                               : " (mono present)")
                  << "\n";

    uint32_t formatCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, nullptr));
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    VK_CHECK(vkGetPhysicalDeviceSurfaceFormatsKHR(gpu, surface, &formatCount, formats.data()));

    // UNORM (not sRGB) backbuffer: ImGui writes its colors raw, and the scene
    // arrives via an explicit tonemap/blit — gamma stays under our control.
    VkSurfaceFormatKHR chosen = formats[0];
    for (const VkSurfaceFormatKHR& f : formats) {
        if ((f.format == VK_FORMAT_B8G8R8A8_UNORM || f.format == VK_FORMAT_R8G8B8A8_UNORM) &&
            f.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosen = f;
            break;
        }
    }
    format_ = chosen.format;
    colorSpace_ = chosen.colorSpace;

    // FIFO (the only guaranteed mode, and the GL app's swap-interval-1
    // behaviour) is the default; the debug panel can switch to any other mode
    // the surface offers — useful both as a preference and to diagnose
    // driver-side FIFO pacing problems (hybrid-GPU laptops).
    uint32_t modeCount = 0;
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount, nullptr));
    availablePresentModes_.resize(modeCount);
    VK_CHECK(vkGetPhysicalDeviceSurfacePresentModesKHR(gpu, surface, &modeCount,
                                                       availablePresentModes_.data()));
    presentMode_ = VK_PRESENT_MODE_FIFO_KHR;
    for (VkPresentModeKHR mode : availablePresentModes_)
        if (mode == preferredPresentMode_)
            presentMode_ = mode;

    VkExtent2D wanted{};
    if (caps.currentExtent.width != UINT32_MAX) {
        wanted = caps.currentExtent;
    } else {
        wanted.width = std::clamp(width, caps.minImageExtent.width, caps.maxImageExtent.width);
        wanted.height = std::clamp(height, caps.minImageExtent.height, caps.maxImageExtent.height);
    }
    extent_ = wanted;

    uint32_t minImages = caps.minImageCount + 1;
    if (caps.maxImageCount > 0)
        minImages = std::min(minImages, caps.maxImageCount);

    // The scene reaches the backbuffer as a color attachment (tonemap pass);
    // TRANSFER_SRC additionally lets the screenshot path copy the presented
    // frame back. Desktop drivers universally support it, but honor the caps
    // rather than assume (capture is simply refused without it).
    VkImageUsageFlags usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    supportsCapture_ = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
    if (supportsCapture_)
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;

    VkSwapchainCreateInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    info.surface = surface;
    info.minImageCount = minImages;
    info.imageFormat = format_;
    info.imageColorSpace = colorSpace_;
    info.imageExtent = extent_;
    info.imageArrayLayers = 1; // Phase 7: 2 when stereoPresentSupported()
    info.imageUsage = usage;
    info.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    info.preTransform = caps.currentTransform;
    info.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    info.presentMode = presentMode_;
    info.clipped = VK_TRUE;
    info.oldSwapchain = old;

    VK_CHECK(vkCreateSwapchainKHR(device_->device(), &info, nullptr, &swapchain_));

    uint32_t imageCount = 0;
    VK_CHECK(vkGetSwapchainImagesKHR(device_->device(), swapchain_, &imageCount, nullptr));
    images_.resize(imageCount);
    VK_CHECK(vkGetSwapchainImagesKHR(device_->device(), swapchain_, &imageCount, images_.data()));

    imageViews_.resize(imageCount);
    renderFinished_.resize(imageCount);
    for (uint32_t i = 0; i < imageCount; ++i) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = images_[i];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = format_;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.levelCount = 1;
        viewInfo.subresourceRange.layerCount = 1;
        VK_CHECK(vkCreateImageView(device_->device(), &viewInfo, nullptr, &imageViews_[i]));

        VkSemaphoreCreateInfo semInfo{};
        semInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(device_->device(), &semInfo, nullptr, &renderFinished_[i]));
    }
}

uint32_t Swapchain::acquireImage(VkSemaphore signalWhenAvailable) {
    uint32_t imageIndex = 0;
    VkResult result = vkAcquireNextImageKHR(device_->device(), swapchain_, UINT64_MAX,
                                            signalWhenAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
        return UINT32_MAX;
    if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)
        VK_CHECK(result);
    return imageIndex;
}

PresentResult Swapchain::present(VkQueue queue, uint32_t imageIndex) {
    VkSemaphore wait = renderFinished_[imageIndex];
    VkPresentInfoKHR info{};
    info.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    info.waitSemaphoreCount = 1;
    info.pWaitSemaphores = &wait;
    info.swapchainCount = 1;
    info.pSwapchains = &swapchain_;
    info.pImageIndices = &imageIndex;
    VkResult result = vkQueuePresentKHR(queue, &info);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
        return PresentResult::OutOfDate;
    if (result == VK_SUBOPTIMAL_KHR)
        return PresentResult::Suboptimal;
    VK_CHECK(result);
    return PresentResult::Success;
}

void Swapchain::destroyViews() {
    for (VkImageView view : imageViews_)
        vkDestroyImageView(device_->device(), view, nullptr);
    imageViews_.clear();
    for (VkSemaphore sem : renderFinished_)
        vkDestroySemaphore(device_->device(), sem, nullptr);
    renderFinished_.clear();
    images_.clear();
}

void Swapchain::shutdown() {
    if (!device_ || device_->device() == VK_NULL_HANDLE)
        return;
    device_->waitIdle();
    destroyViews();
    if (swapchain_ != VK_NULL_HANDLE)
        vkDestroySwapchainKHR(device_->device(), swapchain_, nullptr);
    swapchain_ = VK_NULL_HANDLE;
    device_ = nullptr;
}

} // namespace rhi
