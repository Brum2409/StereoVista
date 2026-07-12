#include "Scene/SceneDocument.h"

#include "Loaders/PointCloudLoader.h"
#include "RHI/Device.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/MaterialSystem.h"

#include <json.h>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>

// The .scene reader/writer (see SceneDocument.h). The GL SceneManager
// (excluded from build) is the behaviour reference for v1/v2; everything here
// is written fresh against the live scene::Scene.

namespace scene {

namespace {

using nlohmann::json;
namespace fs = std::filesystem;

std::string currentTimestamp() {
    std::time_t t = std::time(nullptr);
    std::tm tmBuf{};
#if defined(_WIN32)
    localtime_s(&tmBuf, &t);
#else
    localtime_r(&t, &tmBuf);
#endif
    char buf[32];
    std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
    return std::string(buf);
}

std::string lowerExt(const fs::path& p) {
    std::string ext = p.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ext;
}

json vec3(const glm::vec3& v) { return json::array({ v.x, v.y, v.z }); }

glm::vec3 readVec3(const json& j, const char* key, glm::vec3 fallback) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 3)
        return fallback;
    try {
        return { (*it)[0].get<float>(), (*it)[1].get<float>(),
                 (*it)[2].get<float>() };
    } catch (const std::exception&) {
        return fallback;
    }
}

// Write `content` to `target` atomically: stream to a sibling .tmp file,
// flush, then rename over the destination. A crash mid-write therefore leaves
// the previous scene file intact instead of a truncated one.
void writeFileAtomic(const fs::path& target, const std::string& content) {
    fs::path tmp = target;
    tmp += ".tmp";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        if (!out.is_open())
            throw std::runtime_error("Failed to create temp file: " + tmp.string());
        out.write(content.data(), static_cast<std::streamsize>(content.size()));
        out.flush();
        if (!out)
            throw std::runtime_error("Failed while writing: " + tmp.string());
    }
    std::error_code ec;
    fs::rename(tmp, target, ec);
    if (ec) {
        // Some platforms refuse rename over an existing file; fall back to
        // remove + rename, then to a direct copy.
        fs::remove(target, ec);
        fs::rename(tmp, target, ec);
        if (ec) {
            fs::copy_file(tmp, target, fs::copy_options::overwrite_existing, ec);
            fs::remove(tmp, ec);
            if (ec)
                throw std::runtime_error("Failed to finalize scene file: " +
                                         target.string());
        }
    }
}

// Scene-relative primary + absolute fallback (v3 asset references, §7.2).
// Returns "" for the relative form when no sane relative path exists (other
// drive on Windows).
std::string relativeTo(const fs::path& base, const fs::path& absolute) {
    std::error_code ec;
    const fs::path rel = fs::relative(absolute, base, ec);
    if (ec || rel.empty())
        return std::string();
    return rel.generic_string();
}

// Resolve a v3 asset reference: scene-relative primary, absolute fallback.
// Returns "" when neither exists (caller reports the miss).
std::string resolveAsset(const fs::path& sceneDir, const std::string& relPath,
                         const std::string& absPath) {
    std::error_code ec;
    if (!relPath.empty()) {
        const fs::path candidate = sceneDir / fs::path(relPath);
        if (fs::exists(candidate, ec))
            return fs::absolute(candidate, ec).string();
    }
    if (!absPath.empty() && fs::exists(absPath, ec))
        return absPath;
    return std::string();
}

// ── Point-cloud source loading (shared by v2 dataPath and v3 references) ─────

Engine::PointCloud loadPointCloudSource(const std::string& absPath,
                                        int downsample, bool mortonResort) {
    const std::string ext = lowerExt(absPath);
    const size_t ds = static_cast<size_t>(std::max(downsample, 1));
    if (ext == ".las" || ext == ".laz")
        return Engine::PointCloudLoader::beginLoadLASProgressive(
            absPath, ds, nullptr, mortonResort);
    if (ext == ".pcb")
        return Engine::PointCloudLoader::loadFromBinary(absPath);
    return Engine::PointCloudLoader::loadPointCloudFile(absPath, ds);
}

// ── Shared object readers ────────────────────────────────────────────────────

void readMeasurements(const json& doc, Scene& scene, SceneLoadReport& report) {
    for (const json& m : doc.value("measurements", json::array())) {
        try {
            Engine::Measurement out;
            out.id = m.value("id", uint64_t(0));
            out.locked = m.value("locked", false);
            out.type = static_cast<Engine::Measurement::Type>(m.value("type", 0));
            out.name = m.value("name", std::string("Measurement"));
            out.visible = m.value("visible", true);
            out.color = readVec3(m, "color", out.color);
            for (const json& p : m.value("points", json::array()))
                if (p.is_array() && p.size() >= 3)
                    out.points.emplace_back(p[0].get<float>(), p[1].get<float>(),
                                            p[2].get<float>());
            if (!out.points.empty()) {
                scene.measurements.push_back(std::move(out));
                ++report.measurementsLoaded;
            }
        } catch (const std::exception& e) {
            report.warnings.push_back(std::string("Skipped a measurement: ") +
                                      e.what());
        }
    }
}

void readClipPlanes(const json& doc, Scene& scene, SceneLoadReport& report) {
    for (const json& pj : doc.value("clipPlanes", json::array())) {
        try {
            Engine::ClipPlane plane;
            plane.id = pj.value("id", uint64_t(0));
            plane.locked = pj.value("locked", false);
            plane.name = pj.value("name", std::string("Plane"));
            plane.enabled = pj.value("enabled", true);
            plane.position = readVec3(pj, "position", plane.position);
            plane.normal = readVec3(pj, "normal", plane.normal);
            plane.color = readVec3(pj, "color", plane.color);
            if (static_cast<int>(scene.clipPlanes.size()) >=
                Engine::MAX_CLIP_PLANES) {
                report.warnings.push_back("Clip plane '" + plane.name +
                                          "' dropped: plane budget is full");
                continue;
            }
            scene.clipPlanes.push_back(std::move(plane));
            ++report.clipPlanesLoaded;
        } catch (const std::exception& e) {
            report.warnings.push_back(std::string("Skipped a clip plane: ") +
                                      e.what());
        }
    }
}

void readCamera(const json& doc, SceneCameraState& out) {
    const auto it = doc.find("camera");
    if (it == doc.end() || !it->is_object())
        return;
    const json& cam = *it;
    out.valid = true;
    out.position = readVec3(cam, "position", out.position);
    out.front = readVec3(cam, "front", out.front);
    out.up = readVec3(cam, "up", out.up);
    out.yaw = cam.value("yaw", out.yaw);
    out.pitch = cam.value("pitch", out.pitch);
    out.zoom = cam.value("zoom", out.zoom);
    const auto q = cam.find("orientation");
    if (q != cam.end() && q->is_array() && q->size() >= 4) {
        try {
            // Stored x,y,z,w (GL order); glm::quat's constructor takes w first.
            out.orientation = glm::quat((*q)[3].get<float>(), (*q)[0].get<float>(),
                                        (*q)[1].get<float>(), (*q)[2].get<float>());
            out.hasOrientation = true;
        } catch (const std::exception&) {
        }
    }
}

void readSun(const json& doc, SceneEnvironmentState& env) {
    const auto it = doc.find("sun");
    if (it == doc.end() || !it->is_object())
        return;
    const json& s = *it;
    env.hasSun = true;
    env.sun.direction = readVec3(s, "direction", env.sun.direction);
    env.sun.color = readVec3(s, "color", env.sun.color);
    env.sun.intensity = s.value("intensity", env.sun.intensity);
    env.sun.enabled = s.value("enabled", env.sun.enabled);
    env.sun.angularSizeDeg = s.value("angularSizeDeg", env.sun.angularSizeDeg);
}

// ── v1/v2 (GL-era) load ──────────────────────────────────────────────────────

void loadLegacy(const json& doc, const fs::path& scenePath, rhi::Device& device,
                renderer::MaterialSystem& materials, int downsample,
                bool mortonResort, SceneLoadResult& result) {
    (void)downsample;
    (void)mortonResort;
    SceneLoadReport& report = result.report;
    Scene& scene = result.scene;
    // GL v1/v2 assets live in a sibling folder named after the file's stem
    // (SceneManager: sceneDir = parent / stem).
    const fs::path sceneDir = scenePath.parent_path() / scenePath.stem();

    // Models: primitives, or files copied under sceneDir ("localPath").
    for (const json& mj : doc.value("models", json::array())) {
        try {
            Model model;
            const bool isPrimitive =
                mj.contains("primitiveType") || !mj.contains("localPath");
            if (!isPrimitive) {
                fs::path modelPath = sceneDir / mj["localPath"].get<std::string>();
                std::error_code ec;
                if (!fs::exists(modelPath, ec)) {
                    // The v2 save also kept the original absolute path.
                    const std::string original = mj.value("path", std::string());
                    if (!original.empty() && fs::exists(original, ec))
                        modelPath = original;
                }
                if (!importModelFile(modelPath.string(), device, materials, model)) {
                    report.warnings.push_back("Model file missing or unreadable: " +
                                              modelPath.string());
                    ++report.modelsFailed;
                    continue;
                }
            } else {
                const glm::vec3 color = readVec3(mj, "color", glm::vec3(0.8f));
                model = makePrimitiveModel(
                    device, materials,
                    primitiveTypeFromString(mj.value(
                        "primitiveType", mj.value("path", std::string("cube")))),
                    mj.value("name", std::string("Primitive")), color,
                    mj.value("metallicFactor", 0.0f),
                    mj.value("roughnessFactor", 0.5f), mj.value("emissive", 0.0f));
            }
            model.name = mj.value("name", model.name);
            model.position = readVec3(mj, "position", glm::vec3(0.0f));
            model.rotationDeg = readVec3(mj, "rotation", glm::vec3(0.0f));
            model.scale = readVec3(mj, "scale", glm::vec3(1.0f));
            model.visible = mj.value("visible", true);
            model.emissive = mj.value("emissive", model.emissive);
            scene.models.push_back(std::move(model));
            ++report.modelsLoaded;
        } catch (const std::exception& e) {
            report.warnings.push_back(std::string("Skipped a model: ") + e.what());
            ++report.modelsFailed;
        }
    }

    // Point clouds: v2 exported .pcb data under sceneDir ("dataPath").
    for (const json& pj : doc.value("pointClouds", json::array())) {
        try {
            const std::string dataPath = pj.value("dataPath", std::string());
            if (dataPath.empty())
                throw std::runtime_error("point cloud entry has no dataPath");
            const fs::path pcPath = sceneDir / dataPath;
            std::error_code ec;
            if (!fs::exists(pcPath, ec)) {
                report.warnings.push_back("Point cloud data missing: " +
                                          pcPath.string());
                ++report.pointCloudsFailed;
                continue;
            }
            Engine::PointCloud pc =
                Engine::PointCloudLoader::loadFromBinary(pcPath.string());
            pc.name = pj.value("name", std::string("Point cloud"));
            pc.position = readVec3(pj, "position", glm::vec3(0.0f));
            pc.rotation = readVec3(pj, "rotation", glm::vec3(0.0f));
            pc.scale = readVec3(pj, "scale", glm::vec3(1.0f));
            pc.visible = pj.value("visible", true);
            pc.basePointSize = pj.value("basePointSize", 2.0f);
            pc.filePath = pcPath.string();
            if (!pc.isLoaded()) {
                report.warnings.push_back("Point cloud '" + pc.name +
                                          "' loaded with no points (" +
                                          pcPath.string() + ")");
            }
            scene.pointClouds.push_back(std::move(pc));
            ++report.pointCloudsLoaded;
        } catch (const std::exception& e) {
            report.warnings.push_back(std::string("Skipped a point cloud: ") +
                                      e.what());
            ++report.pointCloudsFailed;
        }
    }

    // Point lights (spot lights were never lit by either renderer — warn).
    for (const json& lj : doc.value("pointLights", json::array())) {
        PointLight light;
        light.position = readVec3(lj, "position", glm::vec3(0.0f));
        light.color = readVec3(lj, "color", glm::vec3(1.0f));
        light.intensity = lj.value("intensity", 1.0f);
        light.attenLinear = lj.value("linear", light.attenLinear);
        light.attenQuadratic = lj.value("quadratic", light.attenQuadratic);
        light.castsShadows = lj.value("castShadows", true);
        light.radius = lj.value("radius", light.radius);
        scene.pointLights.push_back(std::move(light));
        ++report.lightsLoaded;
    }
    if (doc.contains("spotLights") && doc["spotLights"].is_array() &&
        !doc["spotLights"].empty())
        report.warnings.push_back(
            std::to_string(doc["spotLights"].size()) +
            " spot light(s) skipped (no spot lights in the Vulkan renderer yet)");

    readMeasurements(doc, scene, report);
    readClipPlanes(doc, scene, report);
    readCamera(doc, result.camera);
    readSun(doc, result.environment); // v2 top-level "sun" block

    // v2 "environment": skybox type/colors (GL SkyboxType shares the 0..3
    // order with renderer::SkyMode). HDR-path/exposure details belong to
    // unported GL features and are dropped.
    const auto env = doc.find("environment");
    if (env != doc.end() && env->is_object()) {
        result.environment.hasSky = true;
        renderer::SkyState& sky = result.environment.sky;
        const int type = env->value("skyboxType", static_cast<int>(sky.mode));
        sky.mode = static_cast<renderer::SkyMode>(std::clamp(type, 0, 3));
        sky.solidColor = readVec3(*env, "skyboxSolidColor", sky.solidColor);
        sky.gradientTop = readVec3(*env, "skyboxGradientTop", sky.gradientTop);
        sky.gradientBottom =
            readVec3(*env, "skyboxGradientBottom", sky.gradientBottom);
    }
}

// ── v3 load ──────────────────────────────────────────────────────────────────

void loadV3(const json& doc, const fs::path& scenePath, rhi::Device& device,
            renderer::MaterialSystem& materials, int downsample,
            bool mortonResort, SceneLoadResult& result) {
    SceneLoadReport& report = result.report;
    Scene& scene = result.scene;
    const fs::path sceneDir = scenePath.parent_path();

    scene.nextObjectId = doc.value("nextObjectId", uint64_t(1));

    for (const json& gj : doc.value("groups", json::array())) {
        Group g;
        g.id = gj.value("id", uint64_t(0));
        g.name = gj.value("name", std::string("Group"));
        g.visible = gj.value("visible", true);
        g.locked = gj.value("locked", false);
        g.parentId = gj.value("parentId", uint64_t(0));
        scene.groups.push_back(std::move(g));
    }

    for (const json& mj : doc.value("models", json::array())) {
        try {
            Model model;
            const std::string primitive = mj.value("primitiveType", std::string());
            if (!primitive.empty()) {
                const glm::vec3 color = readVec3(mj, "color", glm::vec3(0.8f));
                model = makePrimitiveModel(
                    device, materials, primitiveTypeFromString(primitive),
                    mj.value("name", std::string("Primitive")), color,
                    mj.value("metallicFactor", 0.0f),
                    mj.value("roughnessFactor", 0.5f), mj.value("emissive", 0.0f));
            } else {
                const std::string abs =
                    resolveAsset(sceneDir, mj.value("path", std::string()),
                                 mj.value("absolutePath", std::string()));
                if (abs.empty()) {
                    report.warnings.push_back(
                        "Model file missing: " +
                        mj.value("path", mj.value("absolutePath",
                                                  std::string("<no path>"))));
                    ++report.modelsFailed;
                    continue;
                }
                if (!importModelFile(abs, device, materials, model)) {
                    report.warnings.push_back("Model failed to import: " + abs);
                    ++report.modelsFailed;
                    continue;
                }
            }
            model.id = mj.value("id", uint64_t(0));
            model.groupId = mj.value("groupId", uint64_t(0));
            model.locked = mj.value("locked", false);
            model.name = mj.value("name", model.name);
            model.position = readVec3(mj, "position", glm::vec3(0.0f));
            model.rotationDeg = readVec3(mj, "rotation", glm::vec3(0.0f));
            model.scale = readVec3(mj, "scale", glm::vec3(1.0f));
            model.visible = mj.value("visible", true);
            model.emissive = mj.value("emissive", model.emissive);
            // Re-apply saved per-mesh material values (base color, metallic,
            // roughness, emissive, normal scale) onto the freshly imported
            // bindless materials.
            for (const json& mm : mj.value("meshMaterials", json::array())) {
                const int meshIdx = mm.value("mesh", -1);
                if (meshIdx < 0 || meshIdx >= static_cast<int>(model.meshes.size()))
                    continue;
                const uint32_t matIdx = model.meshes[meshIdx].materialIndex;
                if (matIdx >= materials.materials().size())
                    continue;
                renderer::gpu::MaterialData& mat = materials.material(matIdx);
                const glm::vec3 base =
                    readVec3(mm, "baseColor", glm::vec3(mat.baseColor));
                mat.baseColor = glm::vec4(base, mat.baseColor.a);
                mat.metallic = mm.value("metallic", mat.metallic);
                mat.roughness = mm.value("roughness", mat.roughness);
                mat.emissive = mm.value("emissive", mat.emissive);
                mat.normalScale = mm.value("normalScale", mat.normalScale);
            }
            scene.models.push_back(std::move(model));
            ++report.modelsLoaded;
        } catch (const std::exception& e) {
            report.warnings.push_back(std::string("Skipped a model: ") + e.what());
            ++report.modelsFailed;
        }
    }

    for (const json& pj : doc.value("pointClouds", json::array())) {
        try {
            const std::string abs =
                resolveAsset(sceneDir, pj.value("path", std::string()),
                             pj.value("absolutePath", std::string()));
            if (abs.empty()) {
                report.warnings.push_back(
                    "Point cloud file missing: " +
                    pj.value("path",
                             pj.value("absolutePath", std::string("<no path>"))));
                ++report.pointCloudsFailed;
                continue;
            }
            Engine::PointCloud pc =
                loadPointCloudSource(abs, downsample, mortonResort);
            if (!pc.isLoaded()) {
                report.warnings.push_back("Point cloud failed to load: " + abs);
                ++report.pointCloudsFailed;
                continue;
            }
            pc.id = pj.value("id", uint64_t(0));
            pc.groupId = pj.value("groupId", uint64_t(0));
            pc.locked = pj.value("locked", false);
            pc.name = pj.value("name", pc.name);
            pc.position = readVec3(pj, "position", pc.position);
            pc.rotation = readVec3(pj, "rotation", pc.rotation);
            pc.scale = readVec3(pj, "scale", pc.scale);
            pc.visible = pj.value("visible", true);
            pc.basePointSize = pj.value("basePointSize", pc.basePointSize);
            scene.pointClouds.push_back(std::move(pc));
            ++report.pointCloudsLoaded;
        } catch (const std::exception& e) {
            report.warnings.push_back(std::string("Skipped a point cloud: ") +
                                      e.what());
            ++report.pointCloudsFailed;
        }
    }

    // SLPK layers parse on worker threads: hand the sources back to the app.
    for (const json& lj : doc.value("sceneLayers", json::array())) {
        const std::string abs =
            resolveAsset(sceneDir, lj.value("path", std::string()),
                         lj.value("absolutePath", std::string()));
        if (abs.empty()) {
            report.warnings.push_back(
                "Scene layer package missing: " +
                lj.value("path", lj.value("absolutePath", std::string("<no path>"))));
            continue;
        }
        PendingLayerState pending;
        pending.sourcePath = abs;
        pending.id = lj.value("id", uint64_t(0));
        pending.groupId = lj.value("groupId", uint64_t(0));
        pending.name = lj.value("name", std::string());
        pending.visible = lj.value("visible", true);
        pending.locked = lj.value("locked", false);
        pending.showGeometry = lj.value("showGeometry", true);
        result.layers.push_back(std::move(pending));
        ++report.layersQueued;
    }

    for (const json& lj : doc.value("pointLights", json::array())) {
        PointLight light;
        light.id = lj.value("id", uint64_t(0));
        light.groupId = lj.value("groupId", uint64_t(0));
        light.locked = lj.value("locked", false);
        light.name = lj.value("name", std::string());
        light.visible = lj.value("visible", true);
        light.position = readVec3(lj, "position", glm::vec3(0.0f));
        light.color = readVec3(lj, "color", glm::vec3(1.0f));
        light.intensity = lj.value("intensity", 1.0f);
        light.attenLinear = lj.value("linear", light.attenLinear);
        light.attenQuadratic = lj.value("quadratic", light.attenQuadratic);
        light.castsShadows = lj.value("castShadows", true);
        light.radius = lj.value("radius", light.radius);
        scene.pointLights.push_back(std::move(light));
        ++report.lightsLoaded;
    }

    readMeasurements(doc, scene, report);
    readClipPlanes(doc, scene, report);
    readCamera(doc, result.camera);
    readSun(doc, result.environment);

    const auto sky = doc.find("sky");
    if (sky != doc.end() && sky->is_object()) {
        result.environment.hasSky = true;
        renderer::SkyState& out = result.environment.sky;
        const int mode = sky->value("mode", static_cast<int>(out.mode));
        out.mode = static_cast<renderer::SkyMode>(std::clamp(mode, 0, 3));
        out.intensity = sky->value("intensity", out.intensity);
        out.solidColor = readVec3(*sky, "solidColor", out.solidColor);
        out.gradientTop = readVec3(*sky, "gradientTop", out.gradientTop);
        out.gradientBottom = readVec3(*sky, "gradientBottom", out.gradientBottom);
    }
}

} // namespace

// ── Public entry points ──────────────────────────────────────────────────────

SceneLoadResult loadSceneDocument(const std::string& path, rhi::Device& device,
                                  renderer::MaterialSystem& materials,
                                  int pointCloudDownsample, bool mortonResort) {
    std::ifstream file(path);
    if (!file.is_open())
        throw std::runtime_error("Cannot open scene file: " + path);

    json doc;
    try {
        file >> doc;
    } catch (const std::exception& e) {
        throw std::runtime_error("Malformed scene JSON in " + path + ": " +
                                 e.what());
    }

    // GL v2 could split huge scenes into "<file>.0 .1 ..." chunks with a tiny
    // {"numChunks": N} header — stitch them back (C10).
    if (doc.is_object() && doc.contains("numChunks")) {
        std::string combined;
        const size_t numChunks = doc["numChunks"].get<size_t>();
        for (size_t i = 0; i < numChunks; ++i) {
            const std::string chunkName = path + "." + std::to_string(i);
            std::ifstream chunk(chunkName, std::ios::binary);
            if (!chunk.is_open())
                throw std::runtime_error("Missing scene chunk: " + chunkName);
            combined.append(std::istreambuf_iterator<char>(chunk),
                            std::istreambuf_iterator<char>());
        }
        doc = json::parse(combined);
    }
    if (!doc.is_object())
        throw std::runtime_error("Scene file is not a JSON object: " + path);

    SceneLoadResult result;
    result.scene.sourcePath = path;
    const int version = doc.value("formatVersion", 1);
    result.report.formatVersion = version;

    const fs::path scenePath(path);
    if (version >= 3)
        loadV3(doc, scenePath, device, materials, pointCloudDownsample,
               mortonResort, result);
    else
        loadLegacy(doc, scenePath, device, materials, pointCloudDownsample,
                   mortonResort, result);

    // Identity: legacy objects (and any v3 entry missing an id) get fresh ids;
    // guard the counter against hand-edited files whose ids exceed it.
    uint64_t maxId = 0;
    for (const Model& m : result.scene.models) maxId = std::max(maxId, m.id);
    for (const Engine::PointCloud& pc : result.scene.pointClouds)
        maxId = std::max(maxId, pc.id);
    for (const PointLight& l : result.scene.pointLights)
        maxId = std::max(maxId, l.id);
    for (const Engine::Measurement& m : result.scene.measurements)
        maxId = std::max(maxId, m.id);
    for (const Engine::ClipPlane& p : result.scene.clipPlanes)
        maxId = std::max(maxId, p.id);
    for (const Group& g : result.scene.groups) maxId = std::max(maxId, g.id);
    for (const PendingLayerState& l : result.layers)
        maxId = std::max(maxId, l.id);
    result.scene.nextObjectId = std::max(result.scene.nextObjectId, maxId + 1);
    result.scene.ensureIds();

    // The interim CameraPose stays filled for callers that only want the
    // simple pose (the Application seeds the quaternion path itself).
    result.scene.camera.valid = result.camera.valid;
    result.scene.camera.position = result.camera.position;
    result.scene.camera.front = result.camera.front;

    result.scene.computeWorldBounds();
    std::cout << "Loaded scene " << path << " (v" << version << ": "
              << result.scene.models.size() << " models, "
              << result.scene.pointClouds.size() << " clouds, "
              << result.layers.size() << " layers, "
              << result.scene.pointLights.size() << " lights)\n";
    return result;
}

bool saveSceneDocument(const std::string& path, const Scene& scene,
                       const SceneSaveState& state,
                       const renderer::MaterialSystem& materials,
                       std::string* error) {
    try {
        fs::path scenePath(path);
        if (lowerExt(scenePath) != ".scene")
            scenePath.replace_extension(".scene");
        std::error_code ec;
        const fs::path sceneDir = fs::absolute(scenePath, ec).parent_path();

        json doc;
        doc["formatVersion"] = kSceneFormatVersion;
        doc["nextObjectId"] = scene.nextObjectId;

        json meta;
        meta["modifiedAt"] = currentTimestamp();
        meta["appVersion"] = "StereoVista (Vulkan)";
        meta["counts"] = {
            { "models", scene.models.size() },
            { "pointClouds", scene.pointClouds.size() },
            { "sceneLayers", scene.i3sLayers.size() },
            { "pointLights", scene.pointLights.size() },
            { "measurements", scene.measurements.size() },
            { "clipPlanes", scene.clipPlanes.size() },
            { "groups", scene.groups.size() },
        };
        doc["metadata"] = std::move(meta);

        if (state.camera.valid) {
            json cam;
            cam["position"] = vec3(state.camera.position);
            cam["front"] = vec3(state.camera.front);
            cam["up"] = vec3(state.camera.up);
            cam["yaw"] = state.camera.yaw;
            cam["pitch"] = state.camera.pitch;
            cam["zoom"] = state.camera.zoom;
            cam["orientation"] = json::array(
                { state.camera.orientation.x, state.camera.orientation.y,
                  state.camera.orientation.z, state.camera.orientation.w });
            doc["camera"] = std::move(cam);
        }

        json groups = json::array();
        for (const Group& g : scene.groups) {
            json gj;
            gj["id"] = g.id;
            gj["name"] = g.name;
            gj["visible"] = g.visible;
            gj["locked"] = g.locked;
            gj["parentId"] = g.parentId;
            groups.push_back(std::move(gj));
        }
        doc["groups"] = std::move(groups);

        const std::vector<renderer::gpu::MaterialData>& materialTable =
            materials.materials();
        json models = json::array();
        for (const Model& m : scene.models) {
            json mj;
            mj["id"] = m.id;
            mj["groupId"] = m.groupId;
            mj["name"] = m.name;
            mj["visible"] = m.visible;
            mj["locked"] = m.locked;
            mj["position"] = vec3(m.position);
            mj["rotation"] = vec3(m.rotationDeg);
            mj["scale"] = vec3(m.scale);
            mj["emissive"] = m.emissive;
            if (!m.primitiveType.empty()) {
                mj["primitiveType"] = m.primitiveType;
                // Primitive look: read the live material so edits round-trip.
                if (!m.meshes.empty() &&
                    m.meshes[0].materialIndex < materialTable.size()) {
                    const renderer::gpu::MaterialData& mat =
                        materialTable[m.meshes[0].materialIndex];
                    mj["color"] = vec3(glm::vec3(mat.baseColor));
                    mj["metallicFactor"] = mat.metallic;
                    mj["roughnessFactor"] = mat.roughness;
                    mj["emissive"] = mat.emissive;
                }
            } else {
                const std::string rel = relativeTo(sceneDir, m.sourcePath);
                if (!rel.empty())
                    mj["path"] = rel;
                mj["absolutePath"] = m.sourcePath;
                json meshMats = json::array();
                for (size_t i = 0; i < m.meshes.size(); ++i) {
                    if (m.meshes[i].materialIndex >= materialTable.size())
                        continue;
                    const renderer::gpu::MaterialData& mat =
                        materialTable[m.meshes[i].materialIndex];
                    json mm;
                    mm["mesh"] = static_cast<int>(i);
                    mm["baseColor"] = vec3(glm::vec3(mat.baseColor));
                    mm["metallic"] = mat.metallic;
                    mm["roughness"] = mat.roughness;
                    mm["emissive"] = mat.emissive;
                    mm["normalScale"] = mat.normalScale;
                    meshMats.push_back(std::move(mm));
                }
                if (!meshMats.empty())
                    mj["meshMaterials"] = std::move(meshMats);
            }
            models.push_back(std::move(mj));
        }
        doc["models"] = std::move(models);

        json clouds = json::array();
        for (const Engine::PointCloud& pc : scene.pointClouds) {
            json pj;
            pj["id"] = pc.id;
            pj["groupId"] = pc.groupId;
            pj["name"] = pc.name;
            pj["visible"] = pc.visible;
            pj["locked"] = pc.locked;
            pj["position"] = vec3(pc.position);
            pj["rotation"] = vec3(pc.rotation);
            pj["scale"] = vec3(pc.scale);
            pj["basePointSize"] = pc.basePointSize;
            const std::string rel = relativeTo(sceneDir, pc.filePath);
            if (!rel.empty())
                pj["path"] = rel;
            pj["absolutePath"] = pc.filePath;
            clouds.push_back(std::move(pj));
        }
        doc["pointClouds"] = std::move(clouds);

        json layers = json::array();
        for (const std::unique_ptr<I3SSceneLayer>& layer : scene.i3sLayers) {
            if (!layer)
                continue;
            json lj;
            lj["id"] = layer->id;
            lj["groupId"] = layer->groupId;
            lj["name"] = layer->name;
            lj["visible"] = layer->visible;
            lj["locked"] = layer->locked;
            lj["showGeometry"] = layer->showGeometry;
            const std::string rel = relativeTo(sceneDir, layer->sourcePath);
            if (!rel.empty())
                lj["path"] = rel;
            lj["absolutePath"] = layer->sourcePath;
            layers.push_back(std::move(lj));
        }
        doc["sceneLayers"] = std::move(layers);

        json lights = json::array();
        for (const PointLight& l : scene.pointLights) {
            json lj;
            lj["id"] = l.id;
            lj["groupId"] = l.groupId;
            lj["name"] = l.name;
            lj["visible"] = l.visible;
            lj["locked"] = l.locked;
            lj["position"] = vec3(l.position);
            lj["color"] = vec3(l.color);
            lj["intensity"] = l.intensity;
            lj["linear"] = l.attenLinear;
            lj["quadratic"] = l.attenQuadratic;
            lj["castShadows"] = l.castsShadows;
            lj["radius"] = l.radius;
            lights.push_back(std::move(lj));
        }
        doc["pointLights"] = std::move(lights);

        json measurements = json::array();
        for (const Engine::Measurement& m : scene.measurements) {
            json mj;
            mj["id"] = m.id;
            mj["type"] = static_cast<int>(m.type);
            mj["name"] = m.name;
            mj["color"] = vec3(m.color);
            mj["visible"] = m.visible;
            mj["locked"] = m.locked;
            json points = json::array();
            for (const glm::vec3& p : m.points)
                points.push_back(json::array({ p.x, p.y, p.z }));
            mj["points"] = std::move(points);
            measurements.push_back(std::move(mj));
        }
        doc["measurements"] = std::move(measurements);

        json planes = json::array();
        for (const Engine::ClipPlane& p : scene.clipPlanes) {
            json pj;
            pj["id"] = p.id;
            pj["name"] = p.name;
            pj["position"] = vec3(p.position);
            pj["normal"] = vec3(p.normal);
            pj["color"] = vec3(p.color);
            pj["enabled"] = p.enabled;
            pj["locked"] = p.locked;
            planes.push_back(std::move(pj));
        }
        doc["clipPlanes"] = std::move(planes);

        json sun;
        sun["enabled"] = state.environment.sun.enabled;
        sun["direction"] = vec3(state.environment.sun.direction);
        sun["color"] = vec3(state.environment.sun.color);
        sun["intensity"] = state.environment.sun.intensity;
        sun["angularSizeDeg"] = state.environment.sun.angularSizeDeg;
        doc["sun"] = std::move(sun);

        json sky;
        sky["mode"] = static_cast<int>(state.environment.sky.mode);
        sky["intensity"] = state.environment.sky.intensity;
        sky["solidColor"] = vec3(state.environment.sky.solidColor);
        sky["gradientTop"] = vec3(state.environment.sky.gradientTop);
        sky["gradientBottom"] = vec3(state.environment.sky.gradientBottom);
        doc["sky"] = std::move(sky);

        writeFileAtomic(scenePath, doc.dump(2));
        std::cout << "Saved scene " << scenePath.string() << " ("
                  << scene.models.size() << " models, "
                  << scene.pointClouds.size() << " clouds, "
                  << scene.i3sLayers.size() << " layers)\n";
        return true;
    } catch (const std::exception& e) {
        if (error)
            *error = e.what();
        std::cerr << "ERROR: scene save failed: " << e.what() << "\n";
        return false;
    }
}

} // namespace scene
