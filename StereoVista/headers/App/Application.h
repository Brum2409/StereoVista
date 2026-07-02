#pragma once

#include "Platform/Window.h"
#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"
#include "RHI/Swapchain.h"
#include "Renderer/Renderer.h"

namespace app {

// Owns the application: window, RHI, renderer, ImGui — and the main loop
// (poll → update → render → present). This replaces the old main.cpp
// orchestration; systems (scene, tools, GUI panels) mount here as their
// migration phases land.
class Application {
public:
    Application() = default;
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void init();
    void run();
    void shutdown();

private:
    void initImGui();
    void shutdownImGui();
    void buildUi();
    void handleResize();

    Platform::Window window_;
    rhi::Device device_;
    rhi::Swapchain swapchain_;
    rhi::ShaderCompiler shaderCompiler_;
    renderer::Renderer renderer_;

    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;
    bool imguiInitialized_ = false;
    bool showDemoWindow_ = false;
};

} // namespace app
