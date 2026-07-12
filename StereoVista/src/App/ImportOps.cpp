// windows.h (via portable-file-dialogs.h) defines min/max macros unless
// NOMINMAX is set before its first inclusion in this TU (same guard as
// SceneOps.cpp / MeasurementPlugin.cpp).
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "App/Application.h"

#include "Core/ImportService.h"
#include "Loaders/PointCloudLoader.h"
#include "Scene/SceneDocument.h"

#include <portable-file-dialogs.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

// ============================================================================
// Smart import, autosave and crash recovery (UI redesign Pass 5 §11).
//
// importFiles() is the ONE entry point every source funnels into — the File
// menu, drag-and-drop, the Welcome Hub and the palette. It plans the paths
// (core::ImportService — pure, sniffing the ambiguous .ply/.txt), drops
// duplicates, runs the EXISTING loaders (importModelFiles / loadPointCloudFiles
// / openSlpk), then does the things the old scattered paths never did: pretty
// display names, one auto-named group per multi-file batch, select the result,
// and frame it when the scene was empty.
//
// Additive file (SceneOps.cpp / Snapshots.cpp precedent).
// ============================================================================

namespace app {

namespace {

namespace fs = std::filesystem;

// Clock stamp for the autosave whisper / recovery card. localtime is C4996 on
// MSVC (compiled as an error here) — use the guarded _s form.
std::string clockStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm lt{};
#ifdef _MSC_VER
    localtime_s(&lt, &t);
#else
    if (const std::tm* p = std::localtime(&t))
        lt = *p;
#endif
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    return buf;
}

// Same file on disk? Compare lexically-normal paths case-insensitively
// (Windows) — good enough for "already loaded" detection.
bool samePath(const std::string& a, const std::string& b) {
    if (a.empty() || b.empty())
        return false;
    std::error_code ec;
    std::string x = fs::path(a).lexically_normal().string();
    std::string y = fs::path(b).lexically_normal().string();
    (void)ec;
    std::transform(x.begin(), x.end(), x.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    std::transform(y.begin(), y.end(), y.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    return x == y;
}

constexpr const char* kAutosaveDir = "autosave";
constexpr const char* kSessionLock = "autosave/session.lock";

} // namespace

// ── Scene emptiness ──────────────────────────────────────────────────────────

bool Application::sceneEmpty() const {
    return scene_.models.empty() && scene_.pointClouds.empty() &&
           scene_.i3sLayers.empty() && scene_.pointLights.empty() &&
           scene_.measurements.empty() && scene_.clipPlanes.empty();
}

// ── The one import entry point ───────────────────────────────────────────────

void Application::importFiles(const std::vector<std::string>& paths) {
    if (paths.empty())
        return;
    const core::ImportPlan plan = core::planImport(paths);

    // A .scene is a document operation, not an import: hand it to the
    // replace-or-merge-or-ask flow and stop (C8).
    if (!plan.sceneFiles.empty()) {
        openSceneFile(plan.sceneFiles.front());
        if (plan.sceneFiles.size() > 1)
            pushToast("Only the first scene file was opened",
                      Plugins::ToastLevel::Warning);
        return;
    }

    const bool wasEmpty = sceneEmpty();

    // Duplicate awareness (§11): never silently load the same file twice.
    const auto alreadyLoaded = [this](const std::string& path) {
        for (const scene::Model& m : scene_.models)
            if (samePath(m.sourcePath, path))
                return true;
        for (const Engine::PointCloud& pc : scene_.pointClouds)
            if (samePath(pc.filePath, path))
                return true;
        for (const std::unique_ptr<scene::I3SSceneLayer>& layer : scene_.i3sLayers)
            if (layer && samePath(layer->sourcePath, path))
                return true;
        return false;
    };

    std::vector<std::string> models, clouds, layers, imported;
    size_t duplicates = 0;
    for (const std::string& path : plan.models) {
        if (alreadyLoaded(path)) { ++duplicates; continue; }
        models.push_back(path);
        imported.push_back(path);
    }
    for (const std::string& path : plan.pointClouds) {
        if (alreadyLoaded(path)) { ++duplicates; continue; }
        clouds.push_back(path);
        imported.push_back(path);
    }
    for (const std::string& path : plan.sceneLayers) {
        if (alreadyLoaded(path)) { ++duplicates; continue; }
        layers.push_back(path);
    }

    const size_t modelsBefore = scene_.models.size();
    const size_t cloudsBefore = scene_.pointClouds.size();

    if (!models.empty())
        importModelFiles(models);
    if (!clouds.empty())
        loadPointCloudFiles(clouds); // LAS tiles share one centre inside
    for (const std::string& path : layers)
        openSlpk(path); // async; adopted + auto-framed by pumpSlpkLoads

    // Things we recognize but cannot ingest yet — one toast, never a modal (C8).
    if (!plan.environments.empty())
        pushToast("HDR environments are not importable yet — set the sky in Settings",
                  Plugins::ToastLevel::Info);
    if (!plan.textures.empty())
        pushToast("Select an object, then load the texture from its Inspector",
                  Plugins::ToastLevel::Info);
    if (!plan.unknown.empty())
        pushToast(std::to_string(plan.unknown.size()) +
                      " file(s) not recognized (" + plan.unknown.front() + ")",
                  Plugins::ToastLevel::Warning);
    if (duplicates > 0)
        pushToast(std::to_string(duplicates) + " file(s) already loaded — skipped",
                  Plugins::ToastLevel::Info);

    // ── Everything that actually landed (LAS adopts synchronously too) ──────
    scene_.ensureIds();
    std::vector<scene::SceneItemRef> created;
    for (size_t i = modelsBefore; i < scene_.models.size(); ++i) {
        scene::Model& model = scene_.models[i];
        if (!model.sourcePath.empty())
            model.name = core::prettyName(model.sourcePath);
        scene::SceneItemRef ref;
        ref.kind = scene::SceneItemRef::Kind::Model;
        ref.id = model.id;
        ref.index = int(i);
        created.push_back(ref);
    }
    for (size_t i = cloudsBefore; i < scene_.pointClouds.size(); ++i) {
        Engine::PointCloud& cloud = scene_.pointClouds[i];
        if (!cloud.filePath.empty())
            cloud.name = core::prettyName(cloud.filePath);
        scene::SceneItemRef ref;
        ref.kind = scene::SceneItemRef::Kind::PointCloud;
        ref.id = cloud.id;
        ref.index = int(i);
        created.push_back(ref);
    }

    if (created.empty()) {
        if (!layers.empty())
            pushToast("Opening " + std::to_string(layers.size()) + " scene layer(s)",
                      Plugins::ToastLevel::Info);
        return;
    }

    // A multi-file import lands as ONE auto-named group (§11).
    if (created.size() > 1) {
        const uint64_t groupId = groupItems(created);
        if (groupId != 0) {
            if (scene::Group* group = scene_.findGroup(groupId))
                group->name = core::groupNameFor(imported);
        }
    }

    selection_.set(created);
    scene_.computeWorldBounds();
    if (wasEmpty)
        frameItems(created); // first import frames itself

    pushToast(std::to_string(created.size()) + " object(s) imported",
              Plugins::ToastLevel::Success);
}

void Application::importFilesDialog() {
    const std::vector<std::string> files =
        pfd::open_file("Import files", "",
                       { "All supported",
                         "*.obj *.fbx *.gltf *.glb *.dae *.stl *.3ds *.blend "
                         "*.las *.laz *.ply *.xyz *.txt *.pcb *.h5 *.hdf5 *.f5 "
                         "*.slpk *.scene",
                         "Models", "*.obj *.fbx *.gltf *.glb *.dae *.stl *.3ds *.blend",
                         "Point clouds", "*.las *.laz *.ply *.xyz *.txt *.pcb *.h5 *.hdf5",
                         "Scene layers", "*.slpk",
                         "Scenes", "*.scene",
                         "All files", "*" },
                       pfd::opt::multiselect)
            .result();
    if (!files.empty())
        importFiles(files);
}

void Application::addPrimitive(int type) {
    static const char* kNames[] = { "Cube", "Sphere", "Cylinder", "Plane", "Torus" };
    const int clamped = std::max(0, std::min(4, type));
    scene::Model model = scene::makePrimitiveModel(
        device_, renderer_.materials(), static_cast<scene::PrimitiveType>(clamped),
        kNames[clamped], glm::vec3(0.8f, 0.8f, 0.82f), 0.0f, 0.5f, 0.0f);
    model.id = scene_.allocateId();
    const uint64_t id = model.id;
    scene_.models.push_back(std::move(model));
    scene_.computeWorldBounds();

    scene::SceneItemRef ref;
    ref.kind = scene::SceneItemRef::Kind::Model;
    ref.id = id;
    ref.index = int(scene_.models.size()) - 1;
    selection_.selectOne(ref);
    pushToast(std::string(kNames[clamped]) + " added", Plugins::ToastLevel::Success);
}

// ── Autosave ─────────────────────────────────────────────────────────────────

scene::SceneSaveState Application::currentSaveState() const {
    scene::SceneSaveState state;
    const Camera::CameraState cam = camera_.GetState();
    state.camera.valid = true;
    state.camera.position = cam.position;
    state.camera.front = cam.front;
    state.camera.up = cam.up;
    state.camera.yaw = cam.yaw;
    state.camera.pitch = cam.pitch;
    state.camera.zoom = cam.zoom;
    state.camera.orientation = cam.orientation;
    state.camera.hasOrientation = true;
    state.environment.hasSun = true;
    state.environment.sun = settings_.lighting.sun;
    state.environment.hasSky = true;
    state.environment.sky = settings_.sky;
    return state;
}

void Application::maybeAutosave(double now) {
    if (!settings_.files.autosaveEnabled)
        return;
    const double interval =
        60.0 * double(std::max(1, settings_.files.autosaveMinutes));
    if (autosaveNextTime_ <= 0.0) {
        autosaveNextTime_ = now + interval; // arm on the first frame
        return;
    }
    if (now < autosaveNextTime_)
        return;
    autosaveNextTime_ = now + interval;

    if (sceneEmpty())
        return;
    // Never autosave a half-loaded scene: streaming clouds and in-flight SLPK
    // parses would serialize an incomplete document.
    if (!slpkJobs_.empty())
        return;
    for (const Engine::PointCloud& pc : scene_.pointClouds)
        if (pc.isStreaming())
            return;

    std::error_code ec;
    fs::create_directories(kAutosaveDir, ec);
    const int slots = std::max(1, settings_.files.autosaveSlots);
    const std::string path = std::string(kAutosaveDir) + "/autosave_" +
                             std::to_string(autosaveSlot_) + ".scene";
    autosaveSlot_ = (autosaveSlot_ + 1) % slots;

    std::string error;
    if (!scene::saveSceneDocument(path, scene_, currentSaveState(),
                                  renderer_.materials(), &error)) {
        autosaveStatus_.clear();
        return;
    }
    autosaveStatus_ = "Autosaved \xC2\xB7 " + clockStamp();

    // Point the session lock at the newest autosave so a crashed run can be
    // recovered from it without any file-time arithmetic.
    std::ofstream lock(kSessionLock, std::ios::trunc);
    if (lock)
        lock << path << "\n" << clockStamp() << "\n";
}

// ── Session lock / crash recovery ────────────────────────────────────────────

void Application::initSession() {
    std::error_code ec;
    // A lock left behind by the previous run == it did not exit cleanly.
    if (fs::exists(kSessionLock, ec)) {
        std::ifstream lock(kSessionLock);
        std::string path, stamp;
        if (lock && std::getline(lock, path)) {
            std::getline(lock, stamp);
            if (!path.empty() && fs::exists(path, ec)) {
                recoveryAvailable_ = true;
                recoveryPath_ = path;
                recoveryStamp_ = stamp;
            }
        }
    }
    fs::create_directories(kAutosaveDir, ec);
    std::ofstream lock(kSessionLock, std::ios::trunc);
    if (lock)
        lock << "\n\n"; // no autosave yet this session
}

void Application::endSession() {
    std::error_code ec;
    fs::remove(kSessionLock, ec); // a crash leaves it — that IS the signal
}

void Application::restoreLastSession() {
    if (!recoveryAvailable_ || recoveryPath_.empty())
        return;
    const std::string path = recoveryPath_;
    recoveryAvailable_ = false;
    replaceSceneFromFile(path);
    pushToast("Restored the last session", Plugins::ToastLevel::Success);
}

void Application::discardRecovery() {
    recoveryAvailable_ = false;
    recoveryPath_.clear();
    recoveryStamp_.clear();
    std::error_code ec;
    if (!fs::exists(kAutosaveDir, ec))
        return;
    for (const fs::directory_entry& entry : fs::directory_iterator(kAutosaveDir, ec))
        if (entry.path().extension() == ".scene")
            fs::remove(entry.path(), ec);
}

} // namespace app
