#include "Core/ImportService.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <sstream>

// The pure planning half of the import pipeline (see Core/ImportService.h).
// No Vulkan, no scene, no GPU — just paths in, a classified plan out.

namespace core {

namespace {

namespace fs = std::filesystem;

bool isOneOf(const std::string& ext, std::initializer_list<const char*> list) {
    for (const char* candidate : list)
        if (ext == candidate)
            return true;
    return false;
}

// A PLY carrying faces is a mesh; one without is a point cloud. The GL drop
// path assumed "cloud" unconditionally, which turned dropped PLY *meshes* into
// vertex clouds — read the header instead of guessing.
ImportKind sniffPly(const std::string& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return ImportKind::PointCloud; // let the cloud loader report the error
    std::string line;
    long long faceCount = 0;
    int lines = 0;
    while (std::getline(file, line) && lines++ < 64) {
        // Normalize CR and case for the header scan.
        while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
            line.pop_back();
        std::string lower = line;
        std::transform(lower.begin(), lower.end(), lower.begin(),
                       [](unsigned char c) { return char(std::tolower(c)); });
        if (lower.rfind("element face", 0) == 0) {
            std::istringstream stream(lower.substr(12));
            stream >> faceCount;
        }
        if (lower.rfind("end_header", 0) == 0)
            break;
    }
    return faceCount > 0 ? ImportKind::Model : ImportKind::PointCloud;
}

// A .txt is an XYZ cloud only when its first data line parses as >= 3 numbers.
ImportKind sniffText(const std::string& path) {
    std::ifstream file(path);
    if (!file)
        return ImportKind::Unknown;
    std::string line;
    int lines = 0;
    while (std::getline(file, line) && lines++ < 32) {
        // Skip blanks and comments.
        size_t first = line.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            continue;
        if (line[first] == '#' || line[first] == '/' || line[first] == ';')
            continue;
        std::istringstream stream(line);
        double value = 0.0;
        int numbers = 0;
        while (stream >> value)
            ++numbers;
        return numbers >= 3 ? ImportKind::PointCloud : ImportKind::Unknown;
    }
    return ImportKind::Unknown;
}

std::string stemOf(const std::string& path) {
    return fs::path(path).stem().string();
}

} // namespace

std::string lowerExtension(const std::string& path) {
    std::string ext = fs::path(path).extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return ext;
}

std::string prettyName(const std::string& path) {
    std::string name = stemOf(path);
    for (char& c : name)
        if (c == '_' || c == '-')
            c = ' ';
    // Collapse runs of spaces and trim.
    std::string out;
    bool space = false;
    for (char c : name) {
        if (c == ' ') {
            space = true;
            continue;
        }
        if (space && !out.empty())
            out.push_back(' ');
        space = false;
        out.push_back(c);
    }
    return out.empty() ? stemOf(path) : out;
}

std::string groupNameFor(const std::vector<std::string>& paths) {
    if (paths.empty())
        return "Imported";
    if (paths.size() == 1)
        return prettyName(paths[0]);

    std::string prefix = stemOf(paths[0]);
    for (size_t i = 1; i < paths.size(); ++i) {
        const std::string stem = stemOf(paths[i]);
        size_t n = 0;
        while (n < prefix.size() && n < stem.size() &&
               std::tolower((unsigned char)prefix[n]) ==
                   std::tolower((unsigned char)stem[n]))
            ++n;
        prefix.resize(n);
        if (prefix.empty())
            break;
    }
    // Trim trailing separators/digits-glue so "tile_01"/"tile_02" -> "tile".
    while (!prefix.empty() &&
           (prefix.back() == '_' || prefix.back() == '-' || prefix.back() == '.' ||
            prefix.back() == ' '))
        prefix.pop_back();

    if (prefix.size() >= 3) {
        std::string name = prefix;
        for (char& c : name)
            if (c == '_' || c == '-')
                c = ' ';
        return name;
    }
    // Too generic — name the group after the folder the files came from.
    const std::string folder = fs::path(paths[0]).parent_path().filename().string();
    return folder.empty() ? std::string("Imported") : folder;
}

ImportPlan planImport(const std::vector<std::string>& paths) {
    ImportPlan plan;
    for (const std::string& path : paths) {
        const std::string ext = lowerExtension(path);
        ImportAction action;
        action.path = path;

        if (isOneOf(ext, { ".obj", ".fbx", ".gltf", ".glb", ".dae", ".stl", ".3ds",
                           ".blend" })) {
            action.kind = ImportKind::Model;
        } else if (isOneOf(ext, { ".las", ".laz", ".xyz", ".pcb", ".h5", ".hdf5",
                                  ".f5" })) {
            action.kind = ImportKind::PointCloud;
        } else if (ext == ".ply") {
            action.kind = sniffPly(path); // mesh or cloud — read the header
        } else if (ext == ".txt") {
            action.kind = sniffText(path);
            if (action.kind == ImportKind::Unknown)
                action.reason = "not an XYZ point list";
        } else if (ext == ".slpk") {
            action.kind = ImportKind::SceneLayer;
        } else if (ext == ".scene") {
            action.kind = ImportKind::SceneFile;
        } else if (isOneOf(ext, { ".hdr", ".exr" })) {
            action.kind = ImportKind::Environment;
        } else if (isOneOf(ext, { ".png", ".jpg", ".jpeg", ".bmp", ".tga" })) {
            action.kind = ImportKind::Texture;
        } else {
            action.kind = ImportKind::Unknown;
            action.reason = ext.empty() ? "no file extension" : ("unsupported " + ext);
        }

        switch (action.kind) {
        case ImportKind::Model: plan.models.push_back(path); break;
        case ImportKind::PointCloud: plan.pointClouds.push_back(path); break;
        case ImportKind::SceneLayer: plan.sceneLayers.push_back(path); break;
        case ImportKind::SceneFile: plan.sceneFiles.push_back(path); break;
        case ImportKind::Environment: plan.environments.push_back(path); break;
        case ImportKind::Texture: plan.textures.push_back(path); break;
        default: plan.unknown.push_back(path); break;
        }
        plan.actions.push_back(std::move(action));
    }
    return plan;
}

} // namespace core
