#pragma once

// Asset/file path resolution shared by every runtime loader: try the working
// directory first (VS debugging runs with the project dir as cwd, where the
// source assets live), then next to the executable (the post-build robocopy
// layout), then the repo-relative project dir (running bin\x64\...\*.exe from
// a checkout). Returns an empty path when nothing exists.

#include <filesystem>
#include <string>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace Platform {

inline std::filesystem::path executableDir() {
#ifdef _WIN32
    char buffer[MAX_PATH] = {};
    DWORD len = GetModuleFileNameA(nullptr, buffer, MAX_PATH);
    if (len > 0 && len < MAX_PATH)
        return std::filesystem::path(buffer).parent_path();
#endif
    return std::filesystem::current_path();
}

inline std::filesystem::path resolveAssetPath(const std::string& logicalPath) {
    namespace fs = std::filesystem;
    std::error_code ec;
    fs::path direct(logicalPath);
    if (fs::exists(direct, ec))
        return direct;
    const fs::path exeDir = executableDir();
    fs::path fromExe = exeDir / logicalPath;
    if (fs::exists(fromExe, ec))
        return fromExe;
    // bin\x64\<config>\ -> repo root -> StereoVista\<logicalPath>
    fs::path fromRepo = exeDir / ".." / ".." / ".." / "StereoVista" / logicalPath;
    if (fs::exists(fromRepo, ec))
        return fromRepo.lexically_normal();
    return {};
}

} // namespace Platform
