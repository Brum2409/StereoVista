#include "RHI/Device.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <sstream>

namespace rhi {

namespace {

// Everything in this table is core Vulkan 1.1–1.3 and present on every modern
// NVIDIA/AMD desktop driver. shaderInt64 + shaderBufferInt64Atomics carry the
// point-cloud compute rasterizer (uint64_t atomicMin depth sort); multiview
// carries single-pass stereo; dynamic rendering / sync2 / timeline semaphores
// are the frame backbone; the descriptor-indexing set backs bindless-style
// materials. A GPU missing any of these is below the app's floor — report all
// misses at once and abort.
struct FeatureRequirement {
    const char* name;
    VkBool32 VkPhysicalDeviceFeatures::* core;
    VkBool32 VkPhysicalDeviceVulkan11Features::* v11;
    VkBool32 VkPhysicalDeviceVulkan12Features::* v12;
    VkBool32 VkPhysicalDeviceVulkan13Features::* v13;
};

const FeatureRequirement kRequiredFeatures[] = {
    { "samplerAnisotropy", &VkPhysicalDeviceFeatures::samplerAnisotropy, nullptr, nullptr, nullptr },
    // CUBE_ARRAY image views (point-light shadow cubemap array).
    { "imageCubeArray", &VkPhysicalDeviceFeatures::imageCubeArray, nullptr, nullptr, nullptr },
    { "shaderInt64", &VkPhysicalDeviceFeatures::shaderInt64, nullptr, nullptr, nullptr },
    // HARD requirement since Phase 6: mesh.vert writes the user section
    // planes to gl_ClipDistance (hardware clipping). Universal on desktop.
    { "shaderClipDistance", &VkPhysicalDeviceFeatures::shaderClipDistance, nullptr, nullptr, nullptr },
    // HARD requirement since SLPK M2: streamed I3S textures upload as BC7
    // (KTX2/Basis transcode). Universal on desktop (the Windows target).
    { "textureCompressionBC", &VkPhysicalDeviceFeatures::textureCompressionBC, nullptr, nullptr, nullptr },
    { "multiview", nullptr, &VkPhysicalDeviceVulkan11Features::multiview, nullptr, nullptr },
    { "shaderBufferInt64Atomics", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::shaderBufferInt64Atomics, nullptr },
    { "timelineSemaphore", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::timelineSemaphore, nullptr },
    // Mandatory-supported at 1.2+; must still be ENABLED for the renderer's
    // depth-only VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL usage to be legal.
    { "separateDepthStencilLayouts", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::separateDepthStencilLayouts, nullptr },
    { "bufferDeviceAddress", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::bufferDeviceAddress, nullptr },
    { "descriptorIndexing", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::descriptorIndexing, nullptr },
    { "runtimeDescriptorArray", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::runtimeDescriptorArray, nullptr },
    { "descriptorBindingPartiallyBound", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::descriptorBindingPartiallyBound, nullptr },
    { "descriptorBindingSampledImageUpdateAfterBind", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::descriptorBindingSampledImageUpdateAfterBind, nullptr },
    { "descriptorBindingUpdateUnusedWhilePending", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::descriptorBindingUpdateUnusedWhilePending, nullptr },
    { "descriptorBindingVariableDescriptorCount", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::descriptorBindingVariableDescriptorCount, nullptr },
    { "shaderSampledImageArrayNonUniformIndexing", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::shaderSampledImageArrayNonUniformIndexing, nullptr },
    // HARD requirement since Phase 3: every scene shader binds its blocks
    // with layout(scalar) against the shared C++/GLSL structs (playbook A.1).
    // Universal on modern NVIDIA/AMD Windows drivers (the compat target).
    { "scalarBlockLayout", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::scalarBlockLayout, nullptr },
    { "hostQueryReset", nullptr, nullptr, &VkPhysicalDeviceVulkan12Features::hostQueryReset, nullptr },
    { "dynamicRendering", nullptr, nullptr, nullptr, &VkPhysicalDeviceVulkan13Features::dynamicRendering },
    { "synchronization2", nullptr, nullptr, nullptr, &VkPhysicalDeviceVulkan13Features::synchronization2 },
    { "maintenance4", nullptr, nullptr, nullptr, &VkPhysicalDeviceVulkan13Features::maintenance4 },
};

struct QueriedFeatures {
    VkPhysicalDeviceFeatures2 f2{};
    VkPhysicalDeviceVulkan11Features v11{};
    VkPhysicalDeviceVulkan12Features v12{};
    VkPhysicalDeviceVulkan13Features v13{};

    void query(VkPhysicalDevice gpu) {
        f2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        v11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
        v12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
        v13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
        f2.pNext = &v11;
        v11.pNext = &v12;
        v12.pNext = &v13;
        vkGetPhysicalDeviceFeatures2(gpu, &f2);
    }

    VkBool32 get(const FeatureRequirement& req) const {
        if (req.core) return f2.features.*(req.core);
        if (req.v11) return v11.*(req.v11);
        if (req.v12) return v12.*(req.v12);
        return v13.*(req.v13);
    }

    std::vector<const char*> missingRequired() const {
        std::vector<const char*> missing;
        for (const FeatureRequirement& req : kRequiredFeatures)
            if (!get(req))
                missing.push_back(req.name);
        return missing;
    }
};

VKAPI_ATTR VkBool32 VKAPI_CALL debugCallback(
    VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT /*types*/,
    const VkDebugUtilsMessengerCallbackDataEXT* data,
    void* /*userData*/) {
    const char* tag = (severity >= VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
                          ? "[vulkan][error] "
                          : "[vulkan][warn ] ";
    std::cerr << tag << (data && data->pMessage ? data->pMessage : "?") << "\n";
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT makeMessengerInfo() {
    VkDebugUtilsMessengerCreateInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
    info.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
    info.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                       VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
    info.pfnUserCallback = debugCallback;
    return info;
}

bool hasLayer(const char* name) {
    uint32_t count = 0;
    vkEnumerateInstanceLayerProperties(&count, nullptr);
    std::vector<VkLayerProperties> layers(count);
    vkEnumerateInstanceLayerProperties(&count, layers.data());
    for (const VkLayerProperties& layer : layers)
        if (std::strcmp(layer.layerName, name) == 0)
            return true;
    return false;
}

std::vector<VkExtensionProperties> enumerateDeviceExtensions(VkPhysicalDevice gpu) {
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> exts(count);
    vkEnumerateDeviceExtensionProperties(gpu, nullptr, &count, exts.data());
    return exts;
}

bool hasExtension(const std::vector<VkExtensionProperties>& exts, const char* name) {
    for (const VkExtensionProperties& e : exts)
        if (std::strcmp(e.extensionName, name) == 0)
            return true;
    return false;
}

// Runtime files (preferences.json, imgui.ini, screenshots/) are working-
// directory-relative in this app; the pipeline cache follows the same
// convention. A cold cache after a cwd change is harmless.
std::filesystem::path pipelineCachePath() {
    return "pipeline_cache.bin";
}

} // namespace

void Device::init(GLFWwindow* window, bool enableValidation) {
    VK_CHECK(volkInitialize());

    createInstance(enableValidation);

    VK_CHECK(glfwCreateWindowSurface(instance_, window, nullptr, &surface_));

    pickPhysicalDevice();
    createLogicalDevice();
    createAllocator();
    createPipelineCache();

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
    poolInfo.queueFamilyIndex = graphicsQueueFamily_;
    VK_CHECK(vkCreateCommandPool(device_, &poolInfo, nullptr, &immediatePool_));
}

void Device::createInstance(bool enableValidation) {
    uint32_t loaderVersion = volkGetInstanceVersion();
    if (loaderVersion < VK_API_VERSION_1_3) {
        std::ostringstream oss;
        oss << "The system Vulkan loader/driver only supports Vulkan "
            << VK_API_VERSION_MAJOR(loaderVersion) << "." << VK_API_VERSION_MINOR(loaderVersion)
            << " but StereoVista requires Vulkan 1.3. Update the GPU driver.";
        throw std::runtime_error(oss.str());
    }

    VkApplicationInfo appInfo{};
    appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    appInfo.pApplicationName = "StereoVista";
    appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.pEngineName = "StereoVista";
    appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    appInfo.apiVersion = VK_API_VERSION_1_3;

    uint32_t glfwExtCount = 0;
    const char** glfwExts = glfwGetRequiredInstanceExtensions(&glfwExtCount);
    if (!glfwExts || glfwExtCount == 0)
        throw std::runtime_error("GLFW reports no Vulkan instance extensions - "
                                 "no Vulkan-capable display path is available.");
    std::vector<const char*> extensions(glfwExts, glfwExts + glfwExtCount);

    std::vector<const char*> layers;
    const bool wantValidation = enableValidation;
    if (wantValidation) {
        if (hasLayer("VK_LAYER_KHRONOS_validation")) {
            layers.push_back("VK_LAYER_KHRONOS_validation");
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        } else {
            std::cout << "[vulkan] validation requested but VK_LAYER_KHRONOS_validation "
                         "is not installed; continuing without it\n";
        }
    }
    const bool validationActive = !layers.empty();

    VkInstanceCreateInfo createInfo{};
    createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    createInfo.pApplicationInfo = &appInfo;
    createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    createInfo.ppEnabledExtensionNames = extensions.data();
    createInfo.enabledLayerCount = static_cast<uint32_t>(layers.size());
    createInfo.ppEnabledLayerNames = layers.data();

    // Chain a messenger into instance create/destroy so those calls are
    // covered too.
    VkDebugUtilsMessengerCreateInfoEXT messengerInfo = makeMessengerInfo();
    if (validationActive)
        createInfo.pNext = &messengerInfo;

    VK_CHECK(vkCreateInstance(&createInfo, nullptr, &instance_));
    volkLoadInstance(instance_);

    if (validationActive) {
        VK_CHECK(vkCreateDebugUtilsMessengerEXT(instance_, &messengerInfo, nullptr,
                                                &debugMessenger_));
        std::cout << "[vulkan] validation layer active\n";
    }
}

void Device::pickPhysicalDevice() {
    uint32_t count = 0;
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &count, nullptr));
    if (count == 0)
        throw std::runtime_error("No Vulkan-capable GPU found.");
    std::vector<VkPhysicalDevice> gpus(count);
    VK_CHECK(vkEnumeratePhysicalDevices(instance_, &count, gpus.data()));

    // Score every GPU; remember why the rejected ones were rejected so the
    // failure message is actionable on machines with several adapters.
    VkPhysicalDevice best = VK_NULL_HANDLE;
    uint64_t bestScore = 0;
    uint32_t bestFamily = UINT32_MAX;
    std::ostringstream rejections;

    for (VkPhysicalDevice gpu : gpus) {
        VkPhysicalDeviceProperties props{};
        vkGetPhysicalDeviceProperties(gpu, &props);

        if (props.apiVersion < VK_API_VERSION_1_3) {
            rejections << "  - " << props.deviceName << ": driver exposes Vulkan "
                       << VK_API_VERSION_MAJOR(props.apiVersion) << "."
                       << VK_API_VERSION_MINOR(props.apiVersion) << " (< 1.3)\n";
            continue;
        }

        if (!hasExtension(enumerateDeviceExtensions(gpu), VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
            rejections << "  - " << props.deviceName << ": no VK_KHR_swapchain\n";
            continue;
        }

        // One family serving graphics + compute + present. Universal on
        // NVIDIA/AMD primary families; anything without it is not a target.
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(gpu, &familyCount, families.data());

        uint32_t family = UINT32_MAX;
        for (uint32_t i = 0; i < familyCount; ++i) {
            const VkQueueFlags needed = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
            if ((families[i].queueFlags & needed) != needed)
                continue;
            VkBool32 present = VK_FALSE;
            VK_CHECK(vkGetPhysicalDeviceSurfaceSupportKHR(gpu, i, surface_, &present));
            if (present) {
                family = i;
                break;
            }
        }
        if (family == UINT32_MAX) {
            rejections << "  - " << props.deviceName
                       << ": no graphics+compute queue family that can present\n";
            continue;
        }

        QueriedFeatures feats;
        feats.query(gpu);
        std::vector<const char*> missing = feats.missingRequired();
        if (!missing.empty()) {
            rejections << "  - " << props.deviceName << ": missing required feature(s): ";
            for (size_t i = 0; i < missing.size(); ++i)
                rejections << (i ? ", " : "") << missing[i];
            rejections << "\n";
            continue;
        }

        uint64_t score = 1;
        if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
            score += 1u << 20;
        else if (props.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
            score += 1u << 10;
        // Tie-break identical types by device-local VRAM.
        VkPhysicalDeviceMemoryProperties mem{};
        vkGetPhysicalDeviceMemoryProperties(gpu, &mem);
        uint64_t vram = 0;
        for (uint32_t i = 0; i < mem.memoryHeapCount; ++i)
            if (mem.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT)
                vram = std::max<uint64_t>(vram, mem.memoryHeaps[i].size);
        score += vram >> 30; // GiB

        if (score > bestScore) {
            bestScore = score;
            best = gpu;
            bestFamily = family;
        }
    }

    if (best == VK_NULL_HANDLE) {
        std::ostringstream oss;
        oss << "No GPU meets StereoVista's Vulkan 1.3 requirements.\n"
            << "Candidates:\n"
            << (rejections.str().empty() ? "  (none)\n" : rejections.str())
            << "Required features: ";
        for (size_t i = 0; i < std::size(kRequiredFeatures); ++i)
            oss << (i ? ", " : "") << kRequiredFeatures[i].name;
        oss << "\nAny modern NVIDIA or AMD GPU with a current driver satisfies "
               "these; update the graphics driver.";
        throw std::runtime_error(oss.str());
    }

    physicalDevice_ = best;
    graphicsQueueFamily_ = bestFamily;
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties_);
    std::cout << "[vulkan] using GPU: " << properties_.deviceName << " (driver Vulkan "
              << VK_API_VERSION_MAJOR(properties_.apiVersion) << "."
              << VK_API_VERSION_MINOR(properties_.apiVersion) << "."
              << VK_API_VERSION_PATCH(properties_.apiVersion) << ")\n";
}

void Device::createLogicalDevice() {
    // Enable exactly the required set (plus a few universally available
    // extras that upcoming phases rely on). Enabling only what we checked
    // keeps the fail-loud contract honest.
    VkPhysicalDeviceVulkan13Features enable13{};
    enable13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    enable13.dynamicRendering = VK_TRUE;
    enable13.synchronization2 = VK_TRUE;
    enable13.maintenance4 = VK_TRUE;

    VkPhysicalDeviceVulkan12Features enable12{};
    enable12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    enable12.pNext = &enable13;
    enable12.shaderBufferInt64Atomics = VK_TRUE;
    enable12.timelineSemaphore = VK_TRUE;
    enable12.separateDepthStencilLayouts = VK_TRUE;
    enable12.bufferDeviceAddress = VK_TRUE;
    enable12.descriptorIndexing = VK_TRUE;
    enable12.runtimeDescriptorArray = VK_TRUE;
    enable12.descriptorBindingPartiallyBound = VK_TRUE;
    enable12.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
    enable12.descriptorBindingUpdateUnusedWhilePending = VK_TRUE;
    enable12.descriptorBindingVariableDescriptorCount = VK_TRUE;
    enable12.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
    enable12.hostQueryReset = VK_TRUE;
    enable12.scalarBlockLayout = VK_TRUE; // core 1.2 optional, universal on NV/AMD

    VkPhysicalDeviceVulkan11Features enable11{};
    enable11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    enable11.pNext = &enable12;
    enable11.multiview = VK_TRUE;
    enable11.shaderDrawParameters = VK_TRUE;

    VkPhysicalDeviceFeatures2 enable2{};
    enable2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
    enable2.pNext = &enable11;
    enable2.features.samplerAnisotropy = VK_TRUE;
    enable2.features.imageCubeArray = VK_TRUE;
    enable2.features.shaderInt64 = VK_TRUE;
    enable2.features.shaderClipDistance = VK_TRUE; // mesh.vert gl_ClipDistance
    enable2.features.textureCompressionBC = VK_TRUE; // I3S BC7 textures (M2)

    // shaderDrawParameters is enabled above without being in the required
    // table — verify it here so an exotic driver fails soft instead of
    // VK_ERROR_FEATURE_NOT_PRESENT. (scalarBlockLayout used to fall off the
    // same way; since Phase 3 it is a hard requirement checked in the table.)
    QueriedFeatures avail;
    avail.query(physicalDevice_);
    if (!avail.v11.shaderDrawParameters)
        enable11.shaderDrawParameters = VK_FALSE;

    // Optional: line-mode debug pipelines (M4 wireframe inspector). Universal
    // on desktop GPUs but formally optional — enable when present, fail soft.
    if (avail.f2.features.fillModeNonSolid) {
        enable2.features.fillModeNonSolid = VK_TRUE;
        fillModeNonSolid_ = true;
    }

    std::vector<const char*> extensions;
    extensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
    // imgui_impl_vulkan resolves vkCmdBeginRenderingKHR by its KHR alias, so
    // also enable the (core-promoted) extension when the driver still
    // advertises it — NVIDIA and AMD both do.
    std::vector<VkExtensionProperties> available = enumerateDeviceExtensions(physicalDevice_);
    if (hasExtension(available, VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME))
        extensions.push_back(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);

    // Ray-tracing readiness (docs/TODO.md §H): enable
    // VK_KHR_acceleration_structure + VK_KHR_ray_query (plus
    // VK_KHR_deferred_host_operations, an acceleration-structure dependency)
    // whenever the GPU offers them, so the planned RT mode can build
    // BLAS/TLAS from the SAME mesh buffers on this device without a rebuild.
    // Purely additive: nothing dispatches ray queries until the RT passes
    // land; a GPU without the extensions simply reports
    // rayTracingSupported() == false and skips the extra buffer usage.
    VkPhysicalDeviceAccelerationStructureFeaturesKHR enableAccel{};
    enableAccel.sType =
        VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
    VkPhysicalDeviceRayQueryFeaturesKHR enableRayQuery{};
    enableRayQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
    if (hasExtension(available, VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
        hasExtension(available, VK_KHR_RAY_QUERY_EXTENSION_NAME) &&
        hasExtension(available, VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME)) {
        VkPhysicalDeviceAccelerationStructureFeaturesKHR accelAvail{};
        accelAvail.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR;
        VkPhysicalDeviceRayQueryFeaturesKHR rayQueryAvail{};
        rayQueryAvail.sType =
            VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR;
        accelAvail.pNext = &rayQueryAvail;
        VkPhysicalDeviceFeatures2 rtQuery{};
        rtQuery.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;
        rtQuery.pNext = &accelAvail;
        vkGetPhysicalDeviceFeatures2(physicalDevice_, &rtQuery);
        if (accelAvail.accelerationStructure && rayQueryAvail.rayQuery) {
            extensions.push_back(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
            extensions.push_back(VK_KHR_RAY_QUERY_EXTENSION_NAME);
            extensions.push_back(VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
            enableAccel.accelerationStructure = VK_TRUE;
            enableRayQuery.rayQuery = VK_TRUE;
            enableAccel.pNext = &enableRayQuery;
            enable13.pNext = &enableAccel;
            rayTracingSupported_ = true;
            std::cout << "[vulkan] ray tracing available "
                         "(acceleration_structure + ray_query enabled)\n";
        }
    }

    // XR readiness (Phase 7b): an OpenXR runtime shares THIS device to import
    // the headset's swapchain images, and it uses the Win32 external
    // memory/semaphore/fence extensions internally. Enable them up front (only
    // if the driver advertises them) so a later xrCreateSession succeeds on the
    // already-created device WITHOUT the app ever calling an xr* function at
    // startup — that keeps VR a live, zero-startup-cost toggle (no runtime is
    // contacted, no SteamVR spin-up, no overhead) instead of forcing an
    // XR-aware boot path. The base memory/semaphore/fence extensions are core
    // 1.1 (listed only to satisfy the Win32 variants' dependency validation);
    // the Win32 variants are the ones a runtime actually needs. String literals
    // avoid coupling to vulkan_win32.h's platform guard — we only enable them by
    // name; the runtime resolves the entry points itself. Absent on a non-XR
    // GPU → simply skipped, so device creation can never fail because of this.
    static const char* const kXrInteropExtensions[] = {
        "VK_KHR_external_memory",        "VK_KHR_external_memory_win32",
        "VK_KHR_external_semaphore",     "VK_KHR_external_semaphore_win32",
        "VK_KHR_external_fence",         "VK_KHR_external_fence_win32",
        "VK_KHR_get_memory_requirements2", "VK_KHR_dedicated_allocation",
        "VK_KHR_bind_memory2",
    };
    for (const char* name : kXrInteropExtensions)
        if (hasExtension(available, name))
            extensions.push_back(name);

    const float priority = 1.0f;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = graphicsQueueFamily_;
    queueInfo.queueCount = 1;
    queueInfo.pQueuePriorities = &priority;

    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.pNext = &enable2;
    deviceInfo.queueCreateInfoCount = 1;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
    deviceInfo.ppEnabledExtensionNames = extensions.data();

    VK_CHECK(vkCreateDevice(physicalDevice_, &deviceInfo, nullptr, &device_));
    // Single-device app: load device-level entry points directly (skips the
    // loader trampoline on every call).
    volkLoadDevice(device_);

    vkGetDeviceQueue(device_, graphicsQueueFamily_, 0, &graphicsQueue_);
}

void Device::createAllocator() {
    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo info{};
    info.vulkanApiVersion = VK_API_VERSION_1_3;
    info.instance = instance_;
    info.physicalDevice = physicalDevice_;
    info.device = device_;
    info.pVulkanFunctions = &functions;
    info.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    VK_CHECK(vmaCreateAllocator(&info, &allocator_));
}

void Device::createPipelineCache() {
    std::vector<char> initial;
    std::ifstream file(pipelineCachePath(), std::ios::binary | std::ios::ate);
    if (file) {
        const std::streamsize size = file.tellg();
        if (size > 0) {
            initial.resize(static_cast<size_t>(size));
            file.seekg(0);
            file.read(initial.data(), size);
        }
    }

    VkPipelineCacheCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO;
    // The implementation validates the blob's header (vendor/device/cache
    // UUID) and ignores incompatible data, so feeding a stale file is safe.
    info.initialDataSize = initial.size();
    info.pInitialData = initial.empty() ? nullptr : initial.data();
    if (vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_) != VK_SUCCESS) {
        // A corrupt file must not stop the app; retry empty.
        info.initialDataSize = 0;
        info.pInitialData = nullptr;
        VK_CHECK(vkCreatePipelineCache(device_, &info, nullptr, &pipelineCache_));
    }
}

void Device::savePipelineCache() {
    if (pipelineCache_ == VK_NULL_HANDLE)
        return;
    size_t size = 0;
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, nullptr) != VK_SUCCESS ||
        size == 0)
        return;
    std::vector<char> data(size);
    if (vkGetPipelineCacheData(device_, pipelineCache_, &size, data.data()) != VK_SUCCESS)
        return;

    const std::filesystem::path path = pipelineCachePath();
    std::error_code ec;
    if (path.has_parent_path())
        std::filesystem::create_directories(path.parent_path(), ec);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (file)
        file.write(data.data(), static_cast<std::streamsize>(size));
    // Failure is acceptable: the cache is an optimization, not state.
}

void Device::immediateSubmit(const std::function<void(VkCommandBuffer)>& record) const {
    VkCommandBufferAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocInfo.commandPool = immediatePool_;
    allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocInfo.commandBufferCount = 1;
    VkCommandBuffer cmd = VK_NULL_HANDLE;
    VK_CHECK(vkAllocateCommandBuffers(device_, &allocInfo, &cmd));

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VK_CHECK(vkBeginCommandBuffer(cmd, &beginInfo));
    record(cmd);
    VK_CHECK(vkEndCommandBuffer(cmd));

    VkCommandBufferSubmitInfo cmdInfo{};
    cmdInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    cmdInfo.commandBuffer = cmd;
    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &cmdInfo;
    VK_CHECK(vkQueueSubmit2(graphicsQueue_, 1, &submit, VK_NULL_HANDLE));
    VK_CHECK(vkQueueWaitIdle(graphicsQueue_));

    vkFreeCommandBuffers(device_, immediatePool_, 1, &cmd);
}

Device::MemoryBudget Device::deviceLocalBudget() const {
    MemoryBudget total;
    if (allocator_ == VK_NULL_HANDLE)
        return total;
    VkPhysicalDeviceMemoryProperties mem{};
    vkGetPhysicalDeviceMemoryProperties(physicalDevice_, &mem);
    VmaBudget budgets[VK_MAX_MEMORY_HEAPS] = {};
    vmaGetHeapBudgets(allocator_, budgets);
    for (uint32_t heap = 0; heap < mem.memoryHeapCount; ++heap) {
        if (!(mem.memoryHeaps[heap].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT))
            continue;
        total.usageBytes += budgets[heap].usage;
        total.budgetBytes += budgets[heap].budget;
    }
    return total;
}

void Device::waitIdle() const {
    if (device_ != VK_NULL_HANDLE)
        vkDeviceWaitIdle(device_);
}

void Device::setDebugName(VkObjectType type, uint64_t handle, const char* name) const {
    if (debugMessenger_ == VK_NULL_HANDLE || !vkSetDebugUtilsObjectNameEXT)
        return;
    VkDebugUtilsObjectNameInfoEXT info{};
    info.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
    info.objectType = type;
    info.objectHandle = handle;
    info.pObjectName = name;
    vkSetDebugUtilsObjectNameEXT(device_, &info);
}

void Device::shutdown() {
    if (instance_ == VK_NULL_HANDLE)
        return;
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
        savePipelineCache();
        if (pipelineCache_ != VK_NULL_HANDLE)
            vkDestroyPipelineCache(device_, pipelineCache_, nullptr);
        if (immediatePool_ != VK_NULL_HANDLE)
            vkDestroyCommandPool(device_, immediatePool_, nullptr);
        if (allocator_ != VK_NULL_HANDLE)
            vmaDestroyAllocator(allocator_);
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE)
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    if (debugMessenger_ != VK_NULL_HANDLE)
        vkDestroyDebugUtilsMessengerEXT(instance_, debugMessenger_, nullptr);
    vkDestroyInstance(instance_, nullptr);

    pipelineCache_ = VK_NULL_HANDLE;
    immediatePool_ = VK_NULL_HANDLE;
    allocator_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    surface_ = VK_NULL_HANDLE;
    debugMessenger_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
    physicalDevice_ = VK_NULL_HANDLE;
    graphicsQueue_ = VK_NULL_HANDLE;
    graphicsQueueFamily_ = UINT32_MAX;
}

} // namespace rhi
