#pragma once

// The ONE place Vulkan enters the codebase. volk is the loader (GLAD's
// replacement): VK_NO_PROTOTYPES is defined project-wide, so including
// <vulkan/vulkan.h> directly anywhere else cannot accidentally link against
// the static loader. Include this header before <GLFW/glfw3.h> so GLFW's
// Vulkan declarations (glfwCreateWindowSurface) become visible.
#include <volk.h>

#include <cstdint>
#include <stdexcept>
#include <string>

namespace rhi {

// Human-readable VkResult for error messages (covers the codes worth naming;
// anything else is reported numerically).
const char* vkResultName(VkResult result);

[[noreturn]] void throwVkError(VkResult result, const char* expr,
                               const char* file, int line);

} // namespace rhi

// Wrap every Vulkan call that returns VkResult. Failure throws with the
// callsite and decoded result; the Application catches at top level, so a
// broken device/driver produces one clear fatal message instead of UB.
#define VK_CHECK(expr)                                                        \
    do {                                                                      \
        VkResult vkCheckResult_ = (expr);                                     \
        if (vkCheckResult_ != VK_SUCCESS)                                     \
            ::rhi::throwVkError(vkCheckResult_, #expr, __FILE__, __LINE__);   \
    } while (0)
