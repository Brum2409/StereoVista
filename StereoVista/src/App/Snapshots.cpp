#include "App/Application.h"

#include "Gui/Services.h" // Gui::SnapshotAspect
#include "Scene/SceneDocument.h"

#include <cstdio>
#include <ctime>
#include <filesystem>
#include <string>
#include <system_error>

// Application snapshot operations (UI redesign Pass 3 §9). Named checkpoints
// that complement the fine-grained undo history. Scene state cannot be copied
// in-memory (a Model's MeshBuffer is a move-only GPU resource), so a scene
// snapshot is serialized to a snapshots/ file via scene::SceneDocument and
// restored by reload; the camera pose is a small value copy. Additive file
// (SceneOps.cpp precedent). Session-scoped for now — cross-restart persistence
// arrives with autosave (Pass 5).

namespace app {

namespace {

namespace fs = std::filesystem;

std::string nowStamp() {
    const std::time_t t = std::time(nullptr);
    std::tm lt{};
#ifdef _MSC_VER
    localtime_s(&lt, &t);
#else
    if (const std::tm* p = std::localtime(&t))
        lt = *p;
#endif
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02d %02d:%02d", lt.tm_year + 1900,
                  lt.tm_mon + 1, lt.tm_mday, lt.tm_hour, lt.tm_min);
    return buf;
}

bool sceneHasContent(const scene::Scene& s) {
    return !s.models.empty() || !s.pointClouds.empty() || !s.i3sLayers.empty() ||
           !s.pointLights.empty() || !s.measurements.empty() || !s.clipPlanes.empty();
}

} // namespace

void Application::createSnapshot(const std::string& name, uint32_t aspects) {
    SnapshotEntry entry;
    entry.name = name.empty() ? ("Snapshot " + std::to_string(snapshots_.size() + 1))
                              : name;
    entry.timestamp = nowStamp();
    entry.aspects = aspects;

    if (aspects & Gui::kSnapshotCamera)
        entry.camera = camera_.GetState();

    if (aspects & Gui::kSnapshotScene) {
        std::error_code ec;
        fs::create_directories("snapshots", ec);
        const std::string path =
            "snapshots/snap_" + std::to_string(snapshotCounter_++) + ".scene";

        // Camera + environment block (shared with saveScene and autosave).
        const scene::SceneSaveState state = currentSaveState();

        std::string error;
        if (scene::saveSceneDocument(path, scene_, state, renderer_.materials(),
                                     &error)) {
            entry.filePath = path;
        } else {
            pushToast("Snapshot scene save failed: " + error,
                      Plugins::ToastLevel::Warning);
            entry.aspects &= ~uint32_t(Gui::kSnapshotScene);
        }
    }

    snapshots_.push_back(std::move(entry));
    pushToast("Snapshot \"" + snapshots_.back().name + "\" captured",
              Plugins::ToastLevel::Success);
}

void Application::restoreSnapshot(size_t index, uint32_t restoreAspects) {
    if (index >= snapshots_.size())
        return;
    // Copy everything we need out first: restoring a scene may push a safety
    // snapshot and reallocate snapshots_, invalidating the reference.
    const SnapshotEntry& e = snapshots_[index];
    const uint32_t aspects = e.aspects;
    const std::string path = e.filePath;
    const Camera::CameraState cam = e.camera;
    const std::string label = e.name;

    const bool wantScene =
        (restoreAspects & Gui::kSnapshotScene) && (aspects & Gui::kSnapshotScene) &&
        !path.empty();
    const bool wantCamera =
        (restoreAspects & Gui::kSnapshotCamera) && (aspects & Gui::kSnapshotCamera);

    if (wantScene) {
        replaceSceneFromFile(path); // rebuilds GPU residency; applies file camera
        if (wantCamera)
            camera_.SetState(cam); // snapshot camera wins over the file's
        pushToast("Restored snapshot \"" + label + "\"", Plugins::ToastLevel::Info);
        return;
    }
    if (wantCamera) {
        camera_.SetState(cam);
        pushToast("Restored camera from \"" + label + "\"", Plugins::ToastLevel::Info);
    }
}

void Application::deleteSnapshot(size_t index) {
    if (index >= snapshots_.size())
        return;
    if (!snapshots_[index].filePath.empty()) {
        std::error_code ec;
        fs::remove(snapshots_[index].filePath, ec);
    }
    snapshots_.erase(snapshots_.begin() + static_cast<std::ptrdiff_t>(index));
}

void Application::maybeSafetySnapshotBeforeReplace() {
    if (!settings_.files.safetySnapshotBeforeReplace || !sceneHasContent(scene_))
        return;
    createSnapshot("Before replace \xE2\x80\xA2 " + nowStamp(), Gui::kSnapshotScene);
}

} // namespace app
