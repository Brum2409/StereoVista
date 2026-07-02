#include "App/Application.h"

#include <GLFW/glfw3.h>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/imgui_sytle.h"

#include <iostream>
#include <stdexcept>

namespace app {

namespace {

void checkImGuiVkResult(VkResult result) {
    if (result != VK_SUCCESS)
        std::cerr << "[imgui][vulkan] VkResult " << static_cast<int>(result) << "\n";
}

} // namespace

Application::~Application() {
    shutdown();
}

void Application::init() {
    window_.init(1920, 1080, "StereoVista");

#ifdef SV_VULKAN_VALIDATION
    const bool enableValidation = true;
#else
    const bool enableValidation = false;
#endif
    device_.init(window_.handle(), enableValidation);

    int fbWidth = 0, fbHeight = 0;
    window_.framebufferSize(fbWidth, fbHeight);
    swapchain_.init(device_, static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight));

    renderer_.init(device_, swapchain_, shaderCompiler_);

    initImGui();
}

void Application::initImGui() {
    // Pool for the backend's font/user textures. FREE_DESCRIPTOR_SET is a
    // backend requirement (it frees per-texture sets individually).
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 128;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 128;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &imguiDescriptorPool_));

    // Context + docking/viewport flags + GLFW platform backend + theme +
    // fonts (project-local imgui_style.cpp, preserved from the GL build).
    int fbWidth = 0, fbHeight = 0;
    window_.framebufferSize(fbWidth, fbHeight);
    UpdateGuiScale(fbWidth, fbHeight);
    InitializeImGuiWithFonts(window_.handle(), true);

    VkFormat swapchainFormat = swapchain_.format();
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = device_.instance();
    info.PhysicalDevice = device_.physicalDevice();
    info.Device = device_.device();
    info.QueueFamily = device_.graphicsQueueFamily();
    info.Queue = device_.graphicsQueue();
    info.DescriptorPool = imguiDescriptorPool_;
    info.MinImageCount = 2;
    info.ImageCount = swapchain_.imageCount();
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &swapchainFormat;
    info.CheckVkResultFn = checkImGuiVkResult;
    if (!ImGui_ImplVulkan_Init(&info))
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    imguiInitialized_ = true;
}

void Application::run() {
    while (!window_.shouldClose()) {
        window_.pollEvents();

        if (window_.isMinimized()) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        if (window_.consumeResizeFlag())
            handleResize();

        // Font atlas rebuilds must happen outside NewFrame/Render, with the
        // GPU idle (the texture may still be bound by an in-flight frame).
        if (g_GuiScale.needsRescale || g_GuiScale.needsFontRebuild) {
            device_.waitIdle();
            RescaleImGuiFonts(window_.handle(), IsGuiThemeDark(g_currentTheme));
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        buildUi();
        ImGui::Render();

        const bool presented = renderer_.renderFrame(ImGui::GetDrawData(), glfwGetTime());

        // Secondary OS windows (dragged-out panels): the Vulkan backend owns
        // their swapchains and presents them itself.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        if (!presented)
            handleResize();
    }
}

void Application::buildUi() {
    ImGui::SetNextWindowPos(ImVec2(20, 20), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(420, 0), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("StereoVista - Vulkan migration")) {
        ImGui::Text("GPU: %s", device_.deviceName());
        ImGui::Text("Vulkan %u.%u.%u",
                    VK_API_VERSION_MAJOR(device_.properties().apiVersion),
                    VK_API_VERSION_MINOR(device_.properties().apiVersion),
                    VK_API_VERSION_PATCH(device_.properties().apiVersion));
        ImGui::Text("Validation: %s", device_.validationActive() ? "on" : "off");
        ImGui::Text("Swapchain: %ux%u, %u images", swapchain_.extent().width,
                    swapchain_.extent().height, swapchain_.imageCount());
        ImGui::Text("Multiview views: %u (mono)", renderer_.viewCount());
        ImGui::Text("Stereo present capable: %s",
                    swapchain_.stereoPresentSupported() ? "yes" : "no");
        ImGui::Text("%.1f fps (%.2f ms)", ImGui::GetIO().Framerate,
                    1000.0f / ImGui::GetIO().Framerate);
        ImGui::Separator();
        ImGui::TextWrapped("Phase 1: Vulkan bootstrap. The triangle below is "
                           "rendered through the multiview scene target; drag "
                           "this panel out of the window to test viewports.");
        ImGui::Checkbox("ImGui demo window", &showDemoWindow_);
    }
    ImGui::End();

    if (showDemoWindow_)
        ImGui::ShowDemoWindow(&showDemoWindow_);
}

void Application::handleResize() {
    int width = 0, height = 0;
    window_.framebufferSize(width, height);
    if (width == 0 || height == 0)
        return;
    swapchain_.recreate(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    renderer_.onSwapchainRecreated();
}

void Application::shutdownImGui() {
    if (imguiInitialized_) {
        device_.waitIdle();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
    if (imguiDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.device(), imguiDescriptorPool_, nullptr);
        imguiDescriptorPool_ = VK_NULL_HANDLE;
    }
}

void Application::shutdown() {
    if (device_.device() != VK_NULL_HANDLE)
        device_.waitIdle();
    shutdownImGui();
    renderer_.shutdown();
    swapchain_.shutdown();
    device_.shutdown();
    window_.shutdown();
}

} // namespace app
