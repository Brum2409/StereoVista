#include "RHI/ShaderCompiler.h"

#include <shaderc/shaderc.hpp>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <sstream>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace fs = std::filesystem;

namespace rhi {

namespace {

fs::path executableDir() {
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return fs::path(buffer).parent_path();
#endif
    return fs::current_path();
}

// Resolve a logical asset path against cwd first (VS debugging runs with the
// project dir as cwd, where the source assets live), then the exe dir (the
// post-build robocopy layout).
fs::path resolveExisting(const std::string& logicalPath) {
    fs::path direct(logicalPath);
    std::error_code ec;
    if (fs::exists(direct, ec))
        return direct;
    fs::path fromExe = executableDir() / logicalPath;
    if (fs::exists(fromExe, ec))
        return fromExe;
    return {};
}

shaderc_shader_kind kindFromExtension(const fs::path& path) {
    const std::string ext = path.extension().string();
    if (ext == ".vert") return shaderc_vertex_shader;
    if (ext == ".frag") return shaderc_fragment_shader;
    if (ext == ".comp") return shaderc_compute_shader;
    if (ext == ".geom") return shaderc_geometry_shader;
    throw std::runtime_error("ShaderCompiler: cannot infer shader stage from '" +
                             path.string() + "' (expected .vert/.frag/.comp/.geom)");
}

std::string readTextFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        throw std::runtime_error("ShaderCompiler: cannot open " + path.string());
    std::ostringstream oss;
    oss << file.rdbuf();
    return oss.str();
}

std::vector<uint32_t> readSpirvFile(const fs::path& path) {
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file)
        throw std::runtime_error("ShaderCompiler: cannot open " + path.string());
    const std::streamsize size = file.tellg();
    if (size <= 0 || (size % 4) != 0)
        throw std::runtime_error("ShaderCompiler: invalid SPIR-V size in " + path.string());
    std::vector<uint32_t> words(static_cast<size_t>(size) / 4);
    file.seekg(0);
    file.read(reinterpret_cast<char*>(words.data()), size);
    return words;
}

// #include resolution relative to the including file's directory.
class FileIncluder : public shaderc::CompileOptions::IncluderInterface {
public:
    shaderc_include_result* GetInclude(const char* requested, shaderc_include_type,
                                       const char* requesting, size_t) override {
        auto data = std::make_unique<Holder>();
        fs::path resolved = fs::path(requesting).parent_path() / requested;
        std::error_code ec;
        if (!fs::exists(resolved, ec)) {
            data->content = "cannot open include file: " + std::string(requested);
            data->result.source_name = "";
            data->result.source_name_length = 0;
        } else {
            data->name = resolved.string();
            data->content = readTextFile(resolved);
            data->result.source_name = data->name.c_str();
            data->result.source_name_length = data->name.size();
        }
        data->result.content = data->content.c_str();
        data->result.content_length = data->content.size();
        data->result.user_data = data.get();
        return &data.release()->result;
    }

    void ReleaseInclude(shaderc_include_result* result) override {
        delete static_cast<Holder*>(result->user_data);
    }

private:
    struct Holder {
        shaderc_include_result result{};
        std::string name;
        std::string content;
    };
};

} // namespace

std::vector<uint32_t> ShaderCompiler::load(const std::string& logicalPath) const {
    const fs::path source = resolveExisting(logicalPath);
    const fs::path spirv = resolveExisting(logicalPath + ".spv");

    // Prefer the precompiled .spv unless the GLSL source next to us is newer
    // (i.e. it was edited after the last build).
    if (!spirv.empty()) {
        std::error_code ec;
        bool spirvCurrent = true;
        if (!source.empty()) {
            auto srcTime = fs::last_write_time(source, ec);
            auto spvTime = fs::last_write_time(spirv, ec);
            spirvCurrent = !ec && spvTime >= srcTime;
        }
        if (spirvCurrent)
            return readSpirvFile(spirv);
    }

    if (source.empty())
        throw std::runtime_error("ShaderCompiler: shader not found: " + logicalPath +
                                 " (looked in the working directory and next to the executable)");

    shaderc::Compiler compiler;
    shaderc::CompileOptions options;
    options.SetTargetEnvironment(shaderc_target_env_vulkan, shaderc_env_version_vulkan_1_3);
    options.SetOptimizationLevel(shaderc_optimization_level_performance);
    options.SetIncluder(std::make_unique<FileIncluder>());
#ifdef SV_VULKAN_VALIDATION
    options.SetGenerateDebugInfo();
#endif

    const std::string glsl = readTextFile(source);
    const std::string name = source.string();
    shaderc::SpvCompilationResult result = compiler.CompileGlslToSpv(
        glsl, kindFromExtension(source), name.c_str(), options);
    if (result.GetCompilationStatus() != shaderc_compilation_status_success) {
        throw std::runtime_error("ShaderCompiler: GLSL compile failed for " + name +
                                 ":\n" + result.GetErrorMessage());
    }
    if (result.GetNumWarnings() > 0)
        std::cerr << "[shaderc] warnings for " << name << ":\n"
                  << result.GetErrorMessage();

    return std::vector<uint32_t>(result.cbegin(), result.cend());
}

VkShaderModule ShaderCompiler::loadModule(VkDevice device, const std::string& logicalPath) const {
    const std::vector<uint32_t> code = load(logicalPath);
    VkShaderModuleCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    info.codeSize = code.size() * sizeof(uint32_t);
    info.pCode = code.data();
    VkShaderModule module = VK_NULL_HANDLE;
    VK_CHECK(vkCreateShaderModule(device, &info, nullptr, &module));
    return module;
}

} // namespace rhi
