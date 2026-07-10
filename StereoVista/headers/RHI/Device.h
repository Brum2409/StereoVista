#pragma once

#include "RHI/VulkanCommon.h"
#include "RHI/Vma.h"

#include <functional>
#include <string>
#include <vector>

struct GLFWwindow;

namespace rhi {

// Instance + surface + physical device selection + logical device + queues +
// VMA. Owns everything created here; destroyed in reverse order in shutdown().
//
// Feature policy (see docs/VULKAN_MIGRATION_STATUS.md §4): the app targets
// Vulkan 1.3 on any modern NVIDIA/AMD GPU under Windows 10/11. Features listed
// in RequiredFeatures below are hard requirements — init() throws with a
// message naming every missing one (fail loudly, no silent degradation).
// Anything workstation-only (stereo present) is runtime-detected elsewhere.
class Device {
public:
    Device() = default;
    ~Device() { shutdown(); }
    Device(const Device&) = delete;
    Device& operator=(const Device&) = delete;

    // window: surface source (GLFW window created with GLFW_NO_API).
    // enableValidation: request VK_LAYER_KHRONOS_validation + debug messenger;
    // silently skipped (with a console note) when the layer is not installed,
    // so a validation-enabled build still runs on machines without the SDK.
    void init(GLFWwindow* window, bool enableValidation);
    void shutdown();

    VkInstance instance() const { return instance_; }
    VkSurfaceKHR surface() const { return surface_; }
    VkPhysicalDevice physicalDevice() const { return physicalDevice_; }
    VkDevice device() const { return device_; }
    VmaAllocator allocator() const { return allocator_; }

    uint32_t graphicsQueueFamily() const { return graphicsQueueFamily_; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }

    // Disk-backed pipeline cache (StereoVista/pipeline_cache.bin) shared by
    // every pipeline build; the driver itself rejects stale/foreign blobs.
    VkPipelineCache pipelineCache() const { return pipelineCache_; }

    const VkPhysicalDeviceProperties& properties() const { return properties_; }
    const char* deviceName() const { return properties_.deviceName; }
    bool validationActive() const { return debugMessenger_ != VK_NULL_HANDLE; }

    // True when the optional fillModeNonSolid feature was available and
    // enabled — gates the line-mode (wireframe) debug pipelines.
    bool fillModeNonSolidEnabled() const { return fillModeNonSolid_; }

    // True when VK_KHR_acceleration_structure + VK_KHR_ray_query were
    // available and enabled at device creation (docs/TODO.md §H: the planned
    // ray-traced shadows/AO/GI mode). Gates the extra AS-build-input usage on
    // mesh buffers today so the future BLAS builds can consume the SAME
    // vertex/index buffers without recreating them.
    bool rayTracingSupported() const { return rayTracingSupported_; }

    // Records into a transient command buffer, submits on the graphics queue
    // and blocks until completion. For setup/upload work only, never per-frame.
    void immediateSubmit(const std::function<void(VkCommandBuffer)>& record) const;

    // Live device-local heap totals from VMA (usage covers every allocation,
    // budget is the OS/driver's current recommendation). Cheap per-frame; the
    // streaming caps derive their ceiling from it (SLPK plan §6.5).
    struct MemoryBudget {
        uint64_t usageBytes = 0;
        uint64_t budgetBytes = 0;
    };
    MemoryBudget deviceLocalBudget() const;

    void waitIdle() const;

    // Debug-utils object name; no-op unless validation is active.
    void setDebugName(VkObjectType type, uint64_t handle, const char* name) const;

private:
    void createInstance(bool enableValidation);
    void pickPhysicalDevice();
    void createLogicalDevice();
    void createAllocator();
    void createPipelineCache();
    void savePipelineCache();

    VkInstance instance_ = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT debugMessenger_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VmaAllocator allocator_ = VK_NULL_HANDLE;

    uint32_t graphicsQueueFamily_ = UINT32_MAX;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;

    VkPhysicalDeviceProperties properties_{};
    VkCommandPool immediatePool_ = VK_NULL_HANDLE;
    VkPipelineCache pipelineCache_ = VK_NULL_HANDLE;
    bool fillModeNonSolid_ = false;
    bool rayTracingSupported_ = false;
};

} // namespace rhi
