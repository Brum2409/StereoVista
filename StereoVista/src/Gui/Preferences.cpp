#include "Gui/Preferences.h"

#include "Core/CommandRegistry.h"
#include "Gui/Settings.h"

#include <json.h>

#include <algorithm>
#include <fstream>

namespace Gui {
namespace Preferences {

namespace {

using nlohmann::json;

// Read a key if present and convertible; otherwise leave `out` (the default)
// untouched — guardrail 5.
template <typename T>
void get(const json& j, const char* key, T& out) {
    const auto it = j.find(key);
    if (it == j.end())
        return;
    try {
        out = it->get<T>();
    } catch (const std::exception&) {
        // wrong type in a hand-edited file: keep the default
    }
}

void getVec3(const json& j, const char* key, glm::vec3& out) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 3)
        return;
    try {
        out = glm::vec3((*it)[0].get<float>(), (*it)[1].get<float>(),
                        (*it)[2].get<float>());
    } catch (const std::exception&) {
    }
}

json vec3(const glm::vec3& v) {
    return json::array({ v.x, v.y, v.z });
}

const json* section(const json& doc, const char* name) {
    const auto it = doc.find(name);
    if (it == doc.end() || !it->is_object())
        return nullptr;
    return &*it;
}

// ── Settings -> document ────────────────────────────────────────────────────

json toJson(const Settings& s) {
    json doc;
    doc["version"] = kVersion;

    json& camera = doc["camera"];
    camera["fovDeg"] = s.camera.fovDeg;
    camera["speed"] = s.camera.speed;
    camera["nearPlane"] = s.camera.nearPlane;
    camera["farPlane"] = s.camera.farPlane;
    camera["sensitivity"] = s.camera.sensitivity;
    camera["speedFactor"] = s.camera.speedFactor;
    camera["adaptiveSpeed"] = s.camera.adaptiveSpeed;
    camera["useSmoothScrolling"] = s.camera.useSmoothScrolling;
    camera["scrollMomentum"] = s.camera.scrollMomentum;
    camera["scrollDeceleration"] = s.camera.scrollDeceleration;
    camera["maxScrollVelocity"] = s.camera.maxScrollVelocity;
    camera["zoomToCursor"] = s.camera.zoomToCursor;
    camera["orbitAroundCursor"] = s.camera.orbitAroundCursor;

    json& stereo = doc["stereo"];
    stereo["separation"] = s.stereo.separation;
    stereo["convergence"] = s.stereo.convergence;
    stereo["autoConvergence"] = s.stereo.autoConvergence;
    stereo["convergenceFactor"] = s.stereo.convergenceFactor;
    stereo["convergenceSmoothing"] = s.stereo.convergenceSmoothing;
    stereo["flipEyes"] = s.stereo.flipEyes;

    json& vr = doc["vr"];
    vr["worldScale"] = s.vr.worldScale;
    vr["mirrorToWindow"] = s.vr.mirrorToWindow;
    vr["useScenePlanes"] = s.vr.useScenePlanes;
    vr["nearPlane"] = s.vr.nearPlane;
    vr["farPlane"] = s.vr.farPlane;

    json& lighting = doc["lighting"];
    lighting["shadows"] = s.lighting.shadows;
    lighting["softShadows"] = s.lighting.softShadows;
    lighting["pointShadowRange"] = s.lighting.pointShadowRange;
    lighting["ambient"] = s.lighting.ambient;
    json& sun = lighting["sun"];
    sun["enabled"] = s.lighting.sun.enabled;
    sun["direction"] = vec3(s.lighting.sun.direction);
    sun["color"] = vec3(s.lighting.sun.color);
    sun["intensity"] = s.lighting.sun.intensity;
    sun["angularSizeDeg"] = s.lighting.sun.angularSizeDeg;

    json& sky = doc["sky"];
    sky["mode"] = static_cast<int>(s.sky.mode);
    sky["intensity"] = s.sky.intensity;
    sky["solidColor"] = vec3(s.sky.solidColor);
    sky["gradientBottom"] = vec3(s.sky.gradientBottom);
    sky["gradientTop"] = vec3(s.sky.gradientTop);

    json& pc = doc["pointCloud"];
    pc["downsample"] = s.pointCloud.downsample;
    pc["mortonResort"] = s.pointCloud.mortonResort;
    pc["hqs"] = s.pointCloud.hqs;
    pc["hqsThreshold"] = s.pointCloud.hqsThreshold;
    pc["splatEnabled"] = s.pointCloud.splatEnabled;
    pc["splatMaxRadius"] = s.pointCloud.splatMaxRadius;
    pc["lodEnabled"] = s.pointCloud.lodEnabled;
    pc["lodPointsPerPixel"] = s.pointCloud.lodPointsPerPixel;

    json& cursor = doc["cursor"];
    cursor["show"] = s.cursor.show;
    cursor["type"] = s.cursor.type;

    json& render = doc["render"];
    render["wireframe"] = s.render.wireframe;
    render["asyncCompute"] = s.render.asyncCompute;

    json& files = doc["files"];
    files["openSceneMode"] = s.files.openSceneMode;
    files["recentScenes"] = s.files.recentScenes;
    files["safetySnapshotBeforeReplace"] = s.files.safetySnapshotBeforeReplace;
    files["autosaveEnabled"] = s.files.autosaveEnabled;
    files["autosaveMinutes"] = s.files.autosaveMinutes;
    files["autosaveSlots"] = s.files.autosaveSlots;
    files["showWelcomeHub"] = s.files.showWelcomeHub;

    json& ui = doc["ui"];
    ui["theme"] = s.ui.theme;
    ui["guiScale"] = s.ui.guiScale;
    ui["showStatusBar"] = s.ui.showStatusBar;
    ui["reduceMotion"] = s.ui.reduceMotion;
    json& panels = ui["panels"];
    panels["scene"] = s.ui.panels.scene;
    panels["inspector"] = s.ui.panels.inspector;
    panels["settings"] = s.ui.panels.settings;
    panels["cursor"] = s.ui.panels.cursor;
    panels["pointClouds"] = s.ui.panels.pointClouds;
    panels["clipPlanes"] = s.ui.panels.clipPlanes;
    panels["diagnostics"] = s.ui.panels.diagnostics;
    panels["slpk"] = s.ui.panels.slpk;
    panels["history"] = s.ui.panels.history;
    panels["snapshots"] = s.ui.panels.snapshots;
    // Inspector per-kind section collapsed state (UI redesign Pass 2 §8).
    json& insp = ui["inspector"];
    insp["transform"] = s.ui.inspector.transform;
    insp["material"] = s.ui.inspector.material;
    insp["textures"] = s.ui.inspector.textures;
    insp["display"] = s.ui.inspector.display;
    insp["info"] = s.ui.inspector.info;
    insp["exportSection"] = s.ui.inspector.exportSection;
    insp["lightProps"] = s.ui.inspector.lightProps;
    insp["sun"] = s.ui.inspector.sun;
    insp["sky"] = s.ui.inspector.sky;
    insp["layer"] = s.ui.inspector.layer;

    return doc;
}

// ── document -> Settings (v3) ───────────────────────────────────────────────

void fromJson(const json& doc, Settings& s) {
    if (const json* camera = section(doc, "camera")) {
        get(*camera, "fovDeg", s.camera.fovDeg);
        get(*camera, "speed", s.camera.speed);
        get(*camera, "nearPlane", s.camera.nearPlane);
        get(*camera, "farPlane", s.camera.farPlane);
        get(*camera, "sensitivity", s.camera.sensitivity);
        get(*camera, "speedFactor", s.camera.speedFactor);
        get(*camera, "adaptiveSpeed", s.camera.adaptiveSpeed);
        get(*camera, "useSmoothScrolling", s.camera.useSmoothScrolling);
        get(*camera, "scrollMomentum", s.camera.scrollMomentum);
        get(*camera, "scrollDeceleration", s.camera.scrollDeceleration);
        get(*camera, "maxScrollVelocity", s.camera.maxScrollVelocity);
        get(*camera, "zoomToCursor", s.camera.zoomToCursor);
        get(*camera, "orbitAroundCursor", s.camera.orbitAroundCursor);
    }
    if (const json* stereo = section(doc, "stereo")) {
        get(*stereo, "separation", s.stereo.separation);
        get(*stereo, "convergence", s.stereo.convergence);
        get(*stereo, "autoConvergence", s.stereo.autoConvergence);
        get(*stereo, "convergenceFactor", s.stereo.convergenceFactor);
        get(*stereo, "convergenceSmoothing", s.stereo.convergenceSmoothing);
        get(*stereo, "flipEyes", s.stereo.flipEyes);
    }
    if (const json* vr = section(doc, "vr")) {
        get(*vr, "worldScale", s.vr.worldScale);
        get(*vr, "mirrorToWindow", s.vr.mirrorToWindow);
        get(*vr, "useScenePlanes", s.vr.useScenePlanes);
        get(*vr, "nearPlane", s.vr.nearPlane);
        get(*vr, "farPlane", s.vr.farPlane);
    }
    if (const json* lighting = section(doc, "lighting")) {
        get(*lighting, "shadows", s.lighting.shadows);
        get(*lighting, "softShadows", s.lighting.softShadows);
        get(*lighting, "pointShadowRange", s.lighting.pointShadowRange);
        get(*lighting, "ambient", s.lighting.ambient);
        if (const json* sun = section(*lighting, "sun")) {
            get(*sun, "enabled", s.lighting.sun.enabled);
            getVec3(*sun, "direction", s.lighting.sun.direction);
            getVec3(*sun, "color", s.lighting.sun.color);
            get(*sun, "intensity", s.lighting.sun.intensity);
            get(*sun, "angularSizeDeg", s.lighting.sun.angularSizeDeg);
        }
    }
    if (const json* sky = section(doc, "sky")) {
        int mode = static_cast<int>(s.sky.mode);
        get(*sky, "mode", mode);
        s.sky.mode = static_cast<renderer::SkyMode>(std::clamp(mode, 0, 3));
        get(*sky, "intensity", s.sky.intensity);
        getVec3(*sky, "solidColor", s.sky.solidColor);
        getVec3(*sky, "gradientBottom", s.sky.gradientBottom);
        getVec3(*sky, "gradientTop", s.sky.gradientTop);
    }
    if (const json* pc = section(doc, "pointCloud")) {
        get(*pc, "downsample", s.pointCloud.downsample);
        s.pointCloud.downsample = std::max(s.pointCloud.downsample, 1);
        get(*pc, "mortonResort", s.pointCloud.mortonResort);
        get(*pc, "hqs", s.pointCloud.hqs);
        get(*pc, "hqsThreshold", s.pointCloud.hqsThreshold);
        get(*pc, "splatEnabled", s.pointCloud.splatEnabled);
        get(*pc, "splatMaxRadius", s.pointCloud.splatMaxRadius);
        s.pointCloud.splatMaxRadius = std::clamp(s.pointCloud.splatMaxRadius, 1, 8);
        get(*pc, "lodEnabled", s.pointCloud.lodEnabled);
        get(*pc, "lodPointsPerPixel", s.pointCloud.lodPointsPerPixel);
    }
    if (const json* cursor = section(doc, "cursor")) {
        get(*cursor, "show", s.cursor.show);
        get(*cursor, "type", s.cursor.type);
        s.cursor.type = std::clamp(s.cursor.type, 0, 2);
    }
    if (const json* render = section(doc, "render")) {
        get(*render, "wireframe", s.render.wireframe);
        get(*render, "asyncCompute", s.render.asyncCompute);
    }
    if (const json* files = section(doc, "files")) {
        get(*files, "openSceneMode", s.files.openSceneMode);
        s.files.openSceneMode = std::clamp(s.files.openSceneMode, 0, 2);
        get(*files, "safetySnapshotBeforeReplace", s.files.safetySnapshotBeforeReplace);
        get(*files, "autosaveEnabled", s.files.autosaveEnabled);
        get(*files, "autosaveMinutes", s.files.autosaveMinutes);
        s.files.autosaveMinutes = std::clamp(s.files.autosaveMinutes, 1, 60);
        get(*files, "autosaveSlots", s.files.autosaveSlots);
        s.files.autosaveSlots = std::clamp(s.files.autosaveSlots, 1, 10);
        get(*files, "showWelcomeHub", s.files.showWelcomeHub);
        get(*files, "recentScenes", s.files.recentScenes);
        if (s.files.recentScenes.size() >
            static_cast<size_t>(Settings::Files::kMaxRecentScenes))
            s.files.recentScenes.resize(
                static_cast<size_t>(Settings::Files::kMaxRecentScenes));
    }
    if (const json* ui = section(doc, "ui")) {
        get(*ui, "theme", s.ui.theme);
        s.ui.theme = std::max(s.ui.theme, 0); // upper bound clamped at apply
        get(*ui, "guiScale", s.ui.guiScale);
        s.ui.guiScale = std::clamp(s.ui.guiScale, 0.5f, 2.0f);
        get(*ui, "showStatusBar", s.ui.showStatusBar);
        get(*ui, "reduceMotion", s.ui.reduceMotion);
        if (const json* panels = section(*ui, "panels")) {
            get(*panels, "scene", s.ui.panels.scene);
            get(*panels, "inspector", s.ui.panels.inspector);
            get(*panels, "settings", s.ui.panels.settings);
            get(*panels, "cursor", s.ui.panels.cursor);
            get(*panels, "pointClouds", s.ui.panels.pointClouds);
            get(*panels, "clipPlanes", s.ui.panels.clipPlanes);
            get(*panels, "diagnostics", s.ui.panels.diagnostics);
            get(*panels, "slpk", s.ui.panels.slpk);
            get(*panels, "history", s.ui.panels.history);
            get(*panels, "snapshots", s.ui.panels.snapshots);
        }
        if (const json* insp = section(*ui, "inspector")) {
            get(*insp, "transform", s.ui.inspector.transform);
            get(*insp, "material", s.ui.inspector.material);
            get(*insp, "textures", s.ui.inspector.textures);
            get(*insp, "display", s.ui.inspector.display);
            get(*insp, "info", s.ui.inspector.info);
            get(*insp, "exportSection", s.ui.inspector.exportSection);
            get(*insp, "lightProps", s.ui.inspector.lightProps);
            get(*insp, "sun", s.ui.inspector.sun);
            get(*insp, "sky", s.ui.inspector.sky);
            get(*insp, "layer", s.ui.inspector.layer);
        }
    }
}

// ── GL-era document -> Settings (one-time migration of the overlap) ─────────

void migrateLegacy(const json& doc, Settings& s) {
    // The GL app persisted per-feature sections with its own key names; carry
    // over what has a 1:1 meaning here so a user's tuned feel survives the
    // rewrite. Everything belonging to unported features (VCT/HDR/SSAO/radar/
    // SpaceMouse/scene-save/…) is intentionally dropped — those preferences
    // return with their features.
    if (const json* camera = section(doc, "camera")) {
        get(*camera, "fov", s.camera.fovDeg);
        get(*camera, "nearPlane", s.camera.nearPlane);
        get(*camera, "farPlane", s.camera.farPlane);
        get(*camera, "mouseSensitivity", s.camera.sensitivity);
        get(*camera, "speedFactor", s.camera.speedFactor);
        get(*camera, "useSmoothScrolling", s.camera.useSmoothScrolling);
        get(*camera, "scrollMomentum", s.camera.scrollMomentum);
        get(*camera, "scrollDeceleration", s.camera.scrollDeceleration);
        get(*camera, "maxScrollVelocity", s.camera.maxScrollVelocity);
        get(*camera, "zoomToCursor", s.camera.zoomToCursor);
        get(*camera, "orbitAroundCursor", s.camera.orbitAroundCursor);
        // The GL app kept the stereo tunables inside "camera".
        get(*camera, "separation", s.stereo.separation);
        get(*camera, "convergence", s.stereo.convergence);
        get(*camera, "autoConvergence", s.stereo.autoConvergence);
        get(*camera, "convergenceDistanceFactor", s.stereo.convergenceFactor);
        get(*camera, "convergenceSmoothingSpeed", s.stereo.convergenceSmoothing);
        get(*camera, "flipEyes", s.stereo.flipEyes);
    }
    if (const json* xr = section(doc, "openxr")) {
        get(*xr, "worldScale", s.vr.worldScale);
        get(*xr, "mirrorToWindow", s.vr.mirrorToWindow);
        get(*xr, "useScenePlanes", s.vr.useScenePlanes);
        get(*xr, "nearPlane", s.vr.nearPlane);
        get(*xr, "farPlane", s.vr.farPlane);
    }
    if (const json* lighting = section(doc, "lighting"))
        get(*lighting, "enableShadows", s.lighting.shadows);
    if (const json* shadows = section(doc, "shadows"))
        get(*shadows, "enablePCSS", s.lighting.softShadows);
    if (const json* sun = section(doc, "sun")) {
        get(*sun, "enabled", s.lighting.sun.enabled);
        getVec3(*sun, "direction", s.lighting.sun.direction);
        getVec3(*sun, "color", s.lighting.sun.color);
        get(*sun, "intensity", s.lighting.sun.intensity);
    }
    if (const json* skybox = section(doc, "skybox")) {
        // GL SkyboxType and renderer::SkyMode use the same 0..3 order.
        int mode = static_cast<int>(s.sky.mode);
        get(*skybox, "type", mode);
        s.sky.mode = static_cast<renderer::SkyMode>(std::clamp(mode, 0, 3));
        getVec3(*skybox, "solidColor", s.sky.solidColor);
        getVec3(*skybox, "gradientTop", s.sky.gradientTop);
        getVec3(*skybox, "gradientBottom", s.sky.gradientBottom);
    }
    if (const json* pc = section(doc, "pointcloud")) {
        get(*pc, "hqsEnabled", s.pointCloud.hqs);
        get(*pc, "hqsDepthThreshold", s.pointCloud.hqsThreshold);
        get(*pc, "splatEnabled", s.pointCloud.splatEnabled);
        get(*pc, "splatMaxRadius", s.pointCloud.splatMaxRadius);
        s.pointCloud.splatMaxRadius = std::clamp(s.pointCloud.splatMaxRadius, 1, 8);
        get(*pc, "mortonResort", s.pointCloud.mortonResort);
    }
    if (const json* ui = section(doc, "ui")) {
        get(*ui, "theme", s.ui.theme);
        s.ui.theme = std::max(s.ui.theme, 0);
        get(*ui, "guiScaleFactor", s.ui.guiScale);
        s.ui.guiScale = std::clamp(s.ui.guiScale, 0.5f, 2.0f);
        get(*ui, "show3DCursor", s.cursor.show);
    }
}

// ── Command frecency ────────────────────────────────────────────────────────

void frecencyToJson(json& doc, const core::CommandRegistry& commands) {
    json& out = doc["commandFrecency"] = json::object();
    for (const auto& [id, entry] : commands.frecencyTable()) {
        if (entry.score <= 0.001)
            continue;
        json e;
        e["score"] = entry.score;
        e["lastUse"] = entry.lastUse;
        out[id] = std::move(e);
    }
}

void frecencyFromJson(const json& doc, core::CommandRegistry& commands) {
    const json* table = section(doc, "commandFrecency");
    if (!table)
        return;
    for (const auto& [id, e] : table->items()) {
        if (!e.is_object())
            continue;
        core::CommandRegistry::FrecencyEntry entry;
        get(e, "score", entry.score);
        get(e, "lastUse", entry.lastUse);
        commands.setFrecencyEntry(id, entry);
    }
}

} // namespace

bool load(const std::string& path, Settings& settings,
          core::CommandRegistry* commands) {
    std::ifstream file(path);
    if (!file.is_open())
        return false;
    json doc;
    try {
        file >> doc;
    } catch (const std::exception&) {
        return false; // unreadable: keep defaults; the next save rewrites it
    }
    if (!doc.is_object())
        return false;

    if (doc.value("version", 0) >= kVersion) {
        fromJson(doc, settings);
        if (commands)
            frecencyFromJson(doc, *commands);
    } else {
        migrateLegacy(doc, settings);
    }
    return true;
}

bool save(const std::string& path, const Settings& settings,
          const core::CommandRegistry* commands) {
    json doc = toJson(settings);
    if (commands)
        frecencyToJson(doc, *commands);
    std::ofstream file(path);
    if (!file.is_open())
        return false;
    file << doc.dump(4);
    return file.good();
}

std::string snapshot(const Settings& settings,
                     const core::CommandRegistry* commands) {
    json doc = toJson(settings);
    if (commands)
        frecencyToJson(doc, *commands);
    return doc.dump();
}

} // namespace Preferences
} // namespace Gui
