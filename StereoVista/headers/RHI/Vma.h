#pragma once

// VMA is driven exclusively through volk's dynamically loaded entry points:
// no static Vulkan functions exist in this build (VK_NO_PROTOTYPES), so the
// allocator receives vkGetInstanceProcAddr/vkGetDeviceProcAddr at creation
// (see Device::init). Every translation unit must see identical VMA
// configuration — include VMA only through this header.
#include "RHI/VulkanCommon.h"

#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vma/vk_mem_alloc.h>
