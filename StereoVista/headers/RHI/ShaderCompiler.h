#pragma once

#include "RHI/VulkanCommon.h"

#include <cstdint>
#include <string>
#include <vector>

namespace rhi {

// GLSL → SPIR-V, replacing the runtime-GLSL Engine::Shader.
//
// Sources live under assets/shaders_vk/. The build precompiles them with
// glslc (custom build step) into <name>.spv next to the source in the output
// tree; at runtime load() prefers that .spv and falls back to compiling the
// GLSL with shaderc — so shader edits work without rebuilding the project
// (matching the old engine's runtime-compile workflow), while shipped builds
// never pay a compile. Paths are resolved against the working directory and
// the executable directory, like every other asset in the app.
class ShaderCompiler {
public:
    // logicalPath e.g. "assets/shaders_vk/triangle.vert". The shader stage is
    // inferred from the extension (.vert/.frag/.comp/.geom).
    // Throws std::runtime_error with the full shaderc log on compile errors.
    std::vector<uint32_t> load(const std::string& logicalPath) const;

    VkShaderModule loadModule(VkDevice device, const std::string& logicalPath) const;
};

} // namespace rhi
