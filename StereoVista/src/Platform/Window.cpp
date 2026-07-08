#include "Platform/Window.h"

#include <GLFW/glfw3.h>

#include <iostream>
#include <stdexcept>

#include "imgui/imgui_sytle.h"

namespace Platform {

void Window::init(int width, int height, const char* title) {
    if (!glfwInit())
        throw std::runtime_error("Failed to initialize GLFW.");
    ownsGlfw_ = true;

    if (!glfwVulkanSupported())
        throw std::runtime_error(
            "GLFW cannot find a Vulkan loader (vulkan-1.dll). A current GPU "
            "driver on Windows 10/11 always provides one - update the driver.");

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);

    window_ = glfwCreateWindow(width, height, title, nullptr, nullptr);
    if (!window_) {
        glfwTerminate();
        ownsGlfw_ = false;
        throw std::runtime_error("Failed to create GLFW window.");
    }

    glfwSetWindowUserPointer(window_, this);
    glfwSetFramebufferSizeCallback(window_, framebufferSizeCallback);
    // Installed before ImGui's backend init; the ImGui GLFW backend does not
    // hook the drop callback, so this stays ours.
    glfwSetDropCallback(window_, dropCallback);

    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
    else
        std::cout << "Raw mouse motion not supported." << std::endl;
}

void Window::framebufferSizeCallback(GLFWwindow* window, int width, int height) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (self)
        self->resized_ = true;
    // Keep the ImGui font/style scale in sync with the window, as the GL app
    // did from its resize callback.
    UpdateGuiScale(width, height);
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(window_);
}

void Window::pollEvents() const {
    glfwPollEvents();
}

void Window::framebufferSize(int& width, int& height) const {
    glfwGetFramebufferSize(window_, &width, &height);
}

bool Window::isMinimized() const {
    int w = 0, h = 0;
    framebufferSize(w, h);
    return w == 0 || h == 0;
}

bool Window::consumeResizeFlag() {
    bool value = resized_;
    resized_ = false;
    return value;
}

void Window::dropCallback(GLFWwindow* window, int count, const char** paths) {
    Window* self = static_cast<Window*>(glfwGetWindowUserPointer(window));
    if (!self)
        return;
    for (int i = 0; i < count; ++i)
        if (paths[i])
            self->droppedFiles_.push_back(paths[i]);
}

std::vector<std::string> Window::consumeDroppedFiles() {
    std::vector<std::string> out;
    out.swap(droppedFiles_);
    return out;
}

void Window::shutdown() {
    if (window_) {
        glfwDestroyWindow(window_);
        window_ = nullptr;
    }
    if (ownsGlfw_) {
        glfwTerminate();
        ownsGlfw_ = false;
    }
}

} // namespace Platform
