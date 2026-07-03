#pragma once

#include "Platform/Window.h"
#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"
#include "RHI/Swapchain.h"
#include "Renderer/Renderer.h"
#include "Scene/Scene.h"

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
    void loadScene();
    void updateCamera(float dt);
    glm::vec3 cameraFront() const;
    void buildFrameSubmission(renderer::FrameSubmission& submission) const;

    Platform::Window window_;
    rhi::Device device_;
    rhi::Swapchain swapchain_;
    rhi::ShaderCompiler shaderCompiler_;
    renderer::Renderer renderer_;

    // Interim scene host (full SceneManager returns in a later phase) and the
    // per-frame lighting/sky state the debug panel edits.
    scene::Scene scene_;
    renderer::SunState sun_;
    renderer::SkyState sky_;
    bool shadowsEnabled_ = true;
    bool softShadows_ = true; // PCSS contact hardening
    float ambient_ = 0.03f;

    // Fly camera (GL yaw/pitch conventions), driven by RMB-look + WASDQE.
    glm::vec3 cameraPos_{ 3.0f, 3.0f, 7.0f };
    float cameraYaw_ = -90.0f;
    float cameraPitch_ = 0.0f;
    float cameraFovDeg_ = 60.0f;
    float cameraSpeed_ = 4.0f;
    bool looking_ = false;
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    double lastFrameTime_ = 0.0;

    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;
    // Backing store for ImGui_ImplVulkan_InitInfo::PipelineRenderingCreateInfo
    // ::pColorAttachmentFormats. The backend keeps that POINTER for its whole
    // lifetime (secondary-viewport pipelines are created lazily on first
    // drag-out), so it must not point at a stack local.
    VkFormat imguiColorFormat_ = VK_FORMAT_UNDEFINED;
    bool imguiInitialized_ = false;

    // Debug-panel state: deferred present-mode switch (applied at the top of
    // the next loop iteration, never mid-frame) and a recreate counter that
    // makes a recreate storm visible at a glance.
    VkPresentModeKHR pendingPresentMode_ = VK_PRESENT_MODE_MAX_ENUM_KHR;
    uint32_t swapchainRecreations_ = 0;
};

} // namespace app
