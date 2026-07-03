#include "App/Application.h"

#include "Engine/Screenshot.h"
#include "Platform/Paths.h"
#include "Renderer/Projection.h"

#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/imgui_sytle.h"

#include <algorithm>
#include <cfloat>
#include <cmath>
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

    loadScene();

    // Sun defaults follow the GL app (dimmed warm directional); the repo
    // skybox becomes the background when its faces resolve.
    sun_.enabled = true;
    if (renderer_.skybox().loadCubemap("skybox"))
        sky_.mode = renderer::SkyMode::Cubemap;

    initImGui();
}

void Application::loadScene() {
    const std::filesystem::path scenePath = Platform::resolveAssetPath("office.scene");
    if (!scenePath.empty()) {
        try {
            scene_ = scene::loadSceneFile(scenePath.string(), device_,
                                          renderer_.materials());
        } catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << "\n";
        }
    }
    if (scene_.models.empty())
        scene_ = scene::createDefaultScene(device_, renderer_.materials());

    if (scene_.camera.valid) {
        cameraPos_ = scene_.camera.position;
        const glm::vec3 front = scene_.camera.front;
        cameraPitch_ = glm::degrees(std::asin(glm::clamp(front.y, -1.0f, 1.0f)));
        cameraYaw_ = glm::degrees(std::atan2(front.z, front.x));
    }
}

glm::vec3 Application::cameraFront() const {
    const float yaw = glm::radians(cameraYaw_);
    const float pitch = glm::radians(cameraPitch_);
    return glm::normalize(glm::vec3(std::cos(yaw) * std::cos(pitch), std::sin(pitch),
                                    std::sin(yaw) * std::cos(pitch)));
}

void Application::updateCamera(float dt) {
    GLFWwindow* window = window_.handle();
    const ImGuiIO& io = ImGui::GetIO();

    // RMB drag = mouse look (disabled cursor for unlimited travel). ImGui gets
    // right of way only when the press STARTS over a UI element.
    const bool rmbDown = glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    if (rmbDown && !looking_ && !io.WantCaptureMouse) {
        looking_ = true;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
        glfwGetCursorPos(window, &lastMouseX_, &lastMouseY_);
    } else if (!rmbDown && looking_) {
        looking_ = false;
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    }
    if (looking_) {
        double mouseX = 0.0, mouseY = 0.0;
        glfwGetCursorPos(window, &mouseX, &mouseY);
        const float sensitivity = 0.12f;
        cameraYaw_ += float(mouseX - lastMouseX_) * sensitivity;
        cameraPitch_ -= float(mouseY - lastMouseY_) * sensitivity;
        cameraPitch_ = glm::clamp(cameraPitch_, -89.0f, 89.0f);
        lastMouseX_ = mouseX;
        lastMouseY_ = mouseY;
    }

    if (io.WantCaptureKeyboard)
        return;
    const glm::vec3 front = cameraFront();
    const glm::vec3 right = glm::normalize(glm::cross(front, glm::vec3(0, 1, 0)));
    glm::vec3 move(0.0f);
    if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) move += front;
    if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) move -= front;
    if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) move += right;
    if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) move -= right;
    if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) move.y += 1.0f;
    if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) move.y -= 1.0f;
    if (glm::dot(move, move) > 0.0f) {
        float speed = cameraSpeed_;
        if (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS)
            speed *= 4.0f;
        cameraPos_ += glm::normalize(move) * speed * dt;
    }
}

void Application::buildFrameSubmission(renderer::FrameSubmission& submission) const {
    const VkExtent2D extent = swapchain_.extent();
    const float aspect =
        extent.height ? float(extent.width) / float(extent.height) : 1.0f;

    renderer::ViewCamera camera;
    camera.position = cameraPos_;
    camera.view = glm::lookAt(cameraPos_, cameraPos_ + cameraFront(),
                              glm::vec3(0, 1, 0));
    camera.proj = renderer::perspective(glm::radians(cameraFovDeg_), aspect, 0.05f,
                                        500.0f);
    // Identical eyes until stereo (Phase 7) supplies per-eye transforms.
    for (uint32_t v = 0; v < renderer::kMaxViews; ++v)
        submission.views[v] = camera;

    submission.draws.clear();
    submission.draws.reserve(scene_.models.size() * 2);
    for (const scene::Model& model : scene_.models) {
        if (!model.visible)
            continue;
        const glm::mat4 modelMatrix = model.modelMatrix();
        const glm::mat3 normalMatrix = model.normalMatrix(modelMatrix);
        for (const scene::ModelMesh& mesh : model.meshes) {
            if (!mesh.buffer.valid())
                continue;
            renderer::DrawItem draw;
            draw.mesh = &mesh.buffer;
            draw.model = modelMatrix;
            draw.normalMatrix = normalMatrix;
            draw.materialIndex = mesh.materialIndex;

            // World bounding sphere from the local AABB corners (feeds the sun
            // shadow frustum fit).
            glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local(
                    (corner & 1) ? mesh.boundsMax.x : mesh.boundsMin.x,
                    (corner & 2) ? mesh.boundsMax.y : mesh.boundsMin.y,
                    (corner & 4) ? mesh.boundsMax.z : mesh.boundsMin.z);
                const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(local, 1.0f));
                minB = glm::min(minB, world);
                maxB = glm::max(maxB, world);
            }
            draw.worldBoundsCenter = (minB + maxB) * 0.5f;
            draw.worldBoundsRadius = glm::length(maxB - minB) * 0.5f;
            submission.draws.push_back(draw);
        }
    }

    submission.pointLights.clear();
    submission.pointLights.reserve(scene_.pointLights.size());
    for (const scene::PointLight& light : scene_.pointLights) {
        renderer::PointLightState state;
        state.position = light.position;
        state.color = light.color;
        state.intensity = light.intensity;
        state.attenLinear = light.attenLinear;
        state.attenQuadratic = light.attenQuadratic;
        state.castsShadows = light.castsShadows;
        state.radius = light.radius;
        submission.pointLights.push_back(state);
    }

    submission.sun = sun_;
    submission.sky = sky_;
    submission.shadowsEnabled = shadowsEnabled_;
    submission.softShadows = softShadows_;
    submission.ambient = ambient_;
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

    // The backend copies InitInfo by value but keeps pColorAttachmentFormats
    // as a raw pointer and dereferences it again whenever a dragged-out
    // viewport creates its pipeline — a stack local here means that pipeline
    // gets a garbage color format and the OS window renders black.
    imguiColorFormat_ = swapchain_.format();
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
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &imguiColorFormat_;
    info.CheckVkResultFn = checkImGuiVkResult;
    if (!ImGui_ImplVulkan_Init(&info))
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    imguiInitialized_ = true;
}

void Application::run() {
    lastFrameTime_ = glfwGetTime();
    renderer::FrameSubmission submission;
    while (!window_.shouldClose()) {
        window_.pollEvents();

        const double now = glfwGetTime();
        const float dt = std::min(float(now - lastFrameTime_), 0.1f);
        lastFrameTime_ = now;

        if (window_.isMinimized()) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        if (window_.consumeResizeFlag())
            handleResize();

        if (pendingPresentMode_ != VK_PRESENT_MODE_MAX_ENUM_KHR) {
            swapchain_.setPreferredPresentMode(pendingPresentMode_);
            pendingPresentMode_ = VK_PRESENT_MODE_MAX_ENUM_KHR;
            handleResize();
        }

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

        updateCamera(dt);
        buildFrameSubmission(submission);
        const rhi::PresentResult presentResult =
            renderer_.renderFrame(submission, ImGui::GetDrawData());

        // Secondary OS windows (dragged-out panels): the Vulkan backend owns
        // their swapchains and presents them itself.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        if (presentResult == rhi::PresentResult::OutOfDate) {
            handleResize();
        } else if (presentResult == rhi::PresentResult::Suboptimal) {
            // Recreate only when the size really changed. A driver that
            // reports Suboptimal persistently must not trap the loop in a
            // waitIdle+recreate cycle (which shows up as a hard fps cap and
            // laggy input).
            int width = 0, height = 0;
            window_.framebufferSize(width, height);
            if (width > 0 && height > 0 &&
                (static_cast<uint32_t>(width) != swapchain_.extent().width ||
                 static_cast<uint32_t>(height) != swapchain_.extent().height))
                handleResize();
        }
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

        // Frame pacing: under FIFO the vsync wait must sit in acquire and/or
        // present; a large slot wait means the GPU (or a driver cap) is the
        // bottleneck. Sum far below the frame time = external throttle
        // (battery/power-save caps, NVIDIA Battery Boost / Max Frame Rate).
        const renderer::Renderer::FrameStats& stats = renderer_.frameStats();
        ImGui::Text("Waits (ms): slot %.2f, acquire %.2f, present %.2f",
                    stats.slotWaitMs, stats.acquireMs, stats.presentMs);
        ImGui::Text("Swapchain recreations: %u", swapchainRecreations_);

        if (ImGui::BeginCombo("Present mode",
                              rhi::presentModeName(swapchain_.presentMode()))) {
            for (VkPresentModeKHR mode : swapchain_.availablePresentModes()) {
                if (mode > VK_PRESENT_MODE_FIFO_RELAXED_KHR)
                    continue; // shared-present modes need a different frame path
                const bool selected = (mode == swapchain_.presentMode());
                if (ImGui::Selectable(rhi::presentModeName(mode), selected) && !selected)
                    pendingPresentMode_ = mode;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // ---- HDR tonemap (Phase 2) ----
        ImGui::Separator();
        renderer::TonemapSettings& tonemap = renderer_.tonemapSettings();
        ImGui::SliderFloat("Exposure", &tonemap.exposure, 0.1f, 8.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic);
        if (ImGui::BeginCombo("Tonemap", renderer::tonemapOperatorName(tonemap.op))) {
            const renderer::TonemapOperator ops[] = {
                renderer::TonemapOperator::Reinhard,
                renderer::TonemapOperator::ACES,
                renderer::TonemapOperator::Uncharted2,
                renderer::TonemapOperator::AgX,
                renderer::TonemapOperator::KhronosPBRNeutral,
            };
            for (renderer::TonemapOperator op : ops) {
                const bool selected = (op == tonemap.op);
                if (ImGui::Selectable(renderer::tonemapOperatorName(op), selected))
                    tonemap.op = op;
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }

        // ---- Screenshot (Phase 2: Vulkan copy-to-buffer readback) ----
        ImGui::Separator();
        ImGui::BeginDisabled(renderer_.screenshotPending());
        if (ImGui::Button("Save screenshot"))
            renderer_.requestScreenshot(
                Engine::Screenshot::makeTimestampedPath("screenshots"));
        ImGui::EndDisabled();
        if (!renderer_.screenshotStatus().empty()) {
            ImGui::SameLine();
            ImGui::TextWrapped("%s", renderer_.screenshotStatus().c_str());
        }

        // ---- Scene + lighting (Phase 3) ----
        ImGui::Separator();
        size_t meshCount = 0;
        for (const scene::Model& model : scene_.models)
            meshCount += model.meshes.size();
        ImGui::Text("Scene: %s", scene_.sourcePath.c_str());
        ImGui::Text("%zu models / %zu meshes, %zu point lights",
                    scene_.models.size(), meshCount, scene_.pointLights.size());
        ImGui::TextWrapped("Camera: RMB-drag look, WASD move, Q/E down/up, "
                           "Shift fast");
        ImGui::SliderFloat("Camera speed", &cameraSpeed_, 0.5f, 30.0f, "%.1f");
        ImGui::SliderFloat("FOV", &cameraFovDeg_, 30.0f, 100.0f, "%.0f deg");

        ImGui::Separator();
        ImGui::Checkbox("Shadows", &shadowsEnabled_);
        if (shadowsEnabled_) {
            ImGui::SameLine();
            ImGui::Checkbox("Soft (PCSS)", &softShadows_);
        }
        ImGui::Checkbox("Sun", &sun_.enabled);
        if (sun_.enabled) {
            if (ImGui::SliderFloat3("Sun direction", &sun_.direction.x, -1.0f, 1.0f))
                if (glm::dot(sun_.direction, sun_.direction) < 1e-6f)
                    sun_.direction = glm::vec3(0.0f, -1.0f, 0.0f);
            ImGui::ColorEdit3("Sun color", &sun_.color.x);
            ImGui::SliderFloat("Sun intensity", &sun_.intensity, 0.0f, 5.0f, "%.2f");
            ImGui::SliderFloat("Sun size", &sun_.angularSizeDeg, 0.0f, 10.0f,
                               "%.1f deg");
        }
        ImGui::SliderFloat("Ambient", &ambient_, 0.0f, 0.3f, "%.3f");

        // Sky mode: texture modes only listed once their data loaded.
        const char* skyModeNames[] = { "Cubemap", "Equirect HDR", "Solid color",
                                       "Gradient" };
        int skyMode = static_cast<int>(sky_.mode);
        if (ImGui::BeginCombo("Sky", skyModeNames[skyMode])) {
            for (int i = 0; i < 4; ++i) {
                const bool available =
                    (i != 0 || renderer_.skybox().hasCubemap()) &&
                    (i != 1 || renderer_.skybox().hasEquirect());
                if (!available)
                    continue;
                const bool selected = (i == skyMode);
                if (ImGui::Selectable(skyModeNames[i], selected))
                    sky_.mode = static_cast<renderer::SkyMode>(i);
                if (selected)
                    ImGui::SetItemDefaultFocus();
            }
            ImGui::EndCombo();
        }
        ImGui::SliderFloat("Sky intensity", &sky_.intensity, 0.0f, 4.0f, "%.2f");
        if (sky_.mode == renderer::SkyMode::SolidColor)
            ImGui::ColorEdit3("Sky color", &sky_.solidColor.x);
        if (sky_.mode == renderer::SkyMode::Gradient) {
            ImGui::ColorEdit3("Sky bottom", &sky_.gradientBottom.x);
            ImGui::ColorEdit3("Sky top", &sky_.gradientTop.x);
        }

        if (!scene_.pointLights.empty() &&
            ImGui::TreeNode("Point lights")) {
            for (size_t i = 0; i < scene_.pointLights.size(); ++i) {
                scene::PointLight& light = scene_.pointLights[i];
                ImGui::PushID(static_cast<int>(i));
                ImGui::Text("Light %zu", i);
                ImGui::ColorEdit3("Color", &light.color.x);
                ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 10.0f, "%.2f");
                ImGui::Checkbox("Casts shadows", &light.castsShadows);
                ImGui::SliderFloat("Radius", &light.radius, 0.0f, 0.5f, "%.3f m");
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
    }
    ImGui::End();
}

void Application::handleResize() {
    int width = 0, height = 0;
    window_.framebufferSize(width, height);
    if (width == 0 || height == 0)
        return;
    swapchain_.recreate(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    renderer_.onSwapchainRecreated();
    ++swapchainRecreations_;
    // The surface format selection is deterministic, so this stays stable; if
    // it ever changed, the ImGui main pipeline would need a reinit as well.
    imguiColorFormat_ = swapchain_.format();
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
    // Scene GPU buffers must go before the renderer/device they live on.
    scene_ = scene::Scene{};
    renderer_.shutdown();
    swapchain_.shutdown();
    device_.shutdown();
    window_.shutdown();
}

} // namespace app
