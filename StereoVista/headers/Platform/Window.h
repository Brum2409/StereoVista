#pragma once

#include "RHI/VulkanCommon.h"

struct GLFWwindow;

namespace Platform {

// GLFW window without any client API (GLFW_NO_API): Vulkan owns presentation,
// so there is no GL context, no glfwMakeContextCurrent, no glfwSwapBuffers and
// no GLFW_STEREO pixel-format hint — stereo present is a swapchain property
// handled by rhi::Swapchain in Phase 7.
class Window {
public:
    Window() = default;
    ~Window() { shutdown(); }
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    // Initializes GLFW and creates the window. Throws on failure (including
    // "Vulkan loader not found" — checked via glfwVulkanSupported).
    void init(int width, int height, const char* title);
    void shutdown();

    GLFWwindow* handle() const { return window_; }
    bool shouldClose() const;
    void pollEvents() const;

    void framebufferSize(int& width, int& height) const;
    bool isMinimized() const;

    // Set by the framebuffer-size callback; the renderer consumes it to
    // recreate size-dependent resources.
    bool consumeResizeFlag();

private:
    static void framebufferSizeCallback(GLFWwindow* window, int width, int height);

    GLFWwindow* window_ = nullptr;
    bool resized_ = false;
    bool ownsGlfw_ = false;
};

} // namespace Platform
