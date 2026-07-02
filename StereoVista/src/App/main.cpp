// Temporary Phase 0 entry point: proves the vendored Vulkan toolchain (volk
// loader, Vulkan headers, VMA, shaderc) compiles and links with the GL loader
// removed. Replaced by the real Application bootstrap in Phase 1.

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#define VMA_IMPLEMENTATION
#include <volk.h>
#include <vma/vk_mem_alloc.h>

#include <shaderc/shaderc.hpp>

#include <iostream>

int main() {
    const VkResult loader = volkInitialize();
    std::cout << "[phase0] Vulkan headers: VK_HEADER_VERSION " << VK_HEADER_VERSION << "\n";
    if (loader == VK_SUCCESS) {
        const uint32_t version = volkGetInstanceVersion();
        std::cout << "[phase0] volk: loader found, instance version "
                  << VK_API_VERSION_MAJOR(version) << "." << VK_API_VERSION_MINOR(version)
                  << "\n";
    } else {
        std::cout << "[phase0] volk: no Vulkan loader on this machine\n";
    }
    shaderc::Compiler compiler;
    std::cout << "[phase0] shaderc: " << (compiler.IsValid() ? "ok" : "FAILED") << "\n";
    return 0;
}
