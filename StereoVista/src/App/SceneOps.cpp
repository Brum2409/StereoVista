// windows.h (pulled in by portable-file-dialogs.h below) defines min/max
// macros unless NOMINMAX is set before its first inclusion in this
// translation unit; those macros mangle every std::min/std::max call further
// down this file (same guard as Plugins/MeasurementPlugin.cpp).
#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "App/Application.h"

// ============================================================================
// Application scene operations (UI redesign Pass 1) — the scene-document
// commands (new / open / save / save-as / merge, recents, the replace-or-
// merge-or-ask flow) and the Outliner item operations (delete / duplicate /
// visibility / lock / group / rename / frame / isolate).
//
// Split out of Application.cpp (additive files over surgery). Every
// user-visible mutation here is ONE undoable step (contract C4) whose records
// resolve objects by ObjectId (contract C3). Deleted/duplicated objects are
// MOVED into the undo closures (shared_ptr — std::function must stay
// copyable), so undo restores the exact object, GPU residency included; the
// closures die on undo_.clear(), which only happens with the device idle
// (clearSceneContent / shutdown).
// ============================================================================

#include "Loaders/PointCloudLoader.h"
#include "Renderer/PointCloudGpu.h"
#include "Scene/SceneDocument.h"

#include <portable-file-dialogs.h>

#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include <glm/gtc/matrix_transform.hpp>

namespace app {

namespace {

namespace fs = std::filesystem;
using scene::SceneItemRef;
using Kind = scene::SceneItemRef::Kind;

SceneItemRef makeRef(Kind kind, uint64_t id, int index = -1, int sub = -1) {
    SceneItemRef ref;
    ref.kind = kind;
    ref.id = id;
    ref.index = index;
    ref.sub = sub;
    return ref;
}

std::string fileStem(const std::string& path) {
    return fs::path(path).stem().string();
}

// Normalize a batch: resolve every ref, collapse Mesh -> Model, and dedupe.
std::vector<SceneItemRef> normalizeRefs(scene::Scene& scene,
                                        const std::vector<SceneItemRef>& refs) {
    std::vector<SceneItemRef> out;
    for (SceneItemRef ref : refs) {
        if (ref.kind == Kind::Mesh) {
            ref.kind = Kind::Model;
            ref.sub = -1;
        }
        if (!scene.resolve(ref))
            continue;
        bool seen = false;
        for (const SceneItemRef& have : out)
            seen = seen || have == ref;
        if (!seen)
            out.push_back(ref);
    }
    return out;
}

// Expand Group refs into their member objects + subgroups (recursively).
// Non-group refs pass through. Used by delete, frame and isolate.
std::vector<SceneItemRef> expandGroups(scene::Scene& scene,
                                       const std::vector<SceneItemRef>& refs) {
    std::vector<SceneItemRef> out = refs;
    std::unordered_set<uint64_t> groupIds;
    // Transitive closure over subgroups.
    for (size_t i = 0; i < out.size(); ++i) {
        if (out[i].kind != Kind::Group)
            continue;
        if (!groupIds.insert(out[i].id).second)
            continue;
        for (size_t g = 0; g < scene.groups.size(); ++g)
            if (scene.groups[g].parentId == out[i].id)
                out.push_back(makeRef(Kind::Group, scene.groups[g].id, int(g)));
    }
    if (groupIds.empty())
        return normalizeRefs(scene, out);

    auto inGroups = [&](uint64_t groupId) {
        return groupId != 0 && groupIds.count(groupId) > 0;
    };
    for (size_t i = 0; i < scene.models.size(); ++i)
        if (inGroups(scene.models[i].groupId))
            out.push_back(makeRef(Kind::Model, scene.models[i].id, int(i)));
    for (size_t i = 0; i < scene.pointClouds.size(); ++i)
        if (inGroups(scene.pointClouds[i].groupId))
            out.push_back(
                makeRef(Kind::PointCloud, scene.pointClouds[i].id, int(i)));
    for (size_t i = 0; i < scene.i3sLayers.size(); ++i)
        if (scene.i3sLayers[i] && inGroups(scene.i3sLayers[i]->groupId))
            out.push_back(
                makeRef(Kind::SceneLayer, scene.i3sLayers[i]->id, int(i)));
    for (size_t i = 0; i < scene.pointLights.size(); ++i)
        if (inGroups(scene.pointLights[i].groupId))
            out.push_back(
                makeRef(Kind::PointLight, scene.pointLights[i].id, int(i)));
    return normalizeRefs(scene, out);
}

// The visible/enabled flag behind a ref (nullptr = not visibility-capable).
// Clip planes use `enabled` as their eye (enabled == clipping == "shown").
bool* visibleFlagFor(scene::Scene& scene, SceneItemRef ref) {
    if (!scene.resolve(ref))
        return nullptr;
    switch (ref.kind) {
    case Kind::Model:
    case Kind::Mesh:        return &scene.models[ref.index].visible;
    case Kind::PointCloud:  return &scene.pointClouds[ref.index].visible;
    case Kind::SceneLayer:  return &scene.i3sLayers[ref.index]->visible;
    case Kind::PointLight:  return &scene.pointLights[ref.index].visible;
    case Kind::Measurement: return &scene.measurements[ref.index].visible;
    case Kind::ClipPlane:   return &scene.clipPlanes[ref.index].enabled;
    case Kind::Group: {
        scene::Group* g = scene.findGroup(ref.id);
        return g ? &g->visible : nullptr;
    }
    default:                return nullptr;
    }
}

bool* lockedFlagFor(scene::Scene& scene, SceneItemRef ref) {
    if (!scene.resolve(ref))
        return nullptr;
    switch (ref.kind) {
    case Kind::Model:
    case Kind::Mesh:        return &scene.models[ref.index].locked;
    case Kind::PointCloud:  return &scene.pointClouds[ref.index].locked;
    case Kind::SceneLayer:  return &scene.i3sLayers[ref.index]->locked;
    case Kind::PointLight:  return &scene.pointLights[ref.index].locked;
    case Kind::Measurement: return &scene.measurements[ref.index].locked;
    case Kind::ClipPlane:   return &scene.clipPlanes[ref.index].locked;
    case Kind::Group: {
        scene::Group* g = scene.findGroup(ref.id);
        return g ? &g->locked : nullptr;
    }
    default:                return nullptr;
    }
}

// The groupId field behind a groupable ref (nullptr = not groupable).
// For a Group ref this is its parentId (grouping a group nests it).
uint64_t* groupFieldFor(scene::Scene& scene, SceneItemRef ref) {
    if (!scene.resolve(ref))
        return nullptr;
    switch (ref.kind) {
    case Kind::Model:
    case Kind::Mesh:       return &scene.models[ref.index].groupId;
    case Kind::PointCloud: return &scene.pointClouds[ref.index].groupId;
    case Kind::SceneLayer: return &scene.i3sLayers[ref.index]->groupId;
    case Kind::PointLight: return &scene.pointLights[ref.index].groupId;
    case Kind::Group: {
        scene::Group* g = scene.findGroup(ref.id);
        return g ? &g->parentId : nullptr;
    }
    default:               return nullptr;
    }
}

// True when making `child` a member of `parentId` would create a cycle
// (parentId inside child's own subtree).
bool wouldCycle(const scene::Scene& scene, const SceneItemRef& child,
                uint64_t parentId) {
    if (child.kind != Kind::Group)
        return false;
    int guard = 0;
    while (parentId != 0 && guard++ < 64) {
        if (parentId == child.id)
            return true;
        const scene::Group* g = scene.findGroup(parentId);
        parentId = g ? g->parentId : 0;
    }
    return false;
}

std::string* nameFieldFor(scene::Scene& scene, SceneItemRef ref) {
    if (!scene.resolve(ref))
        return nullptr;
    switch (ref.kind) {
    case Kind::Model:       return &scene.models[ref.index].name;
    case Kind::Mesh:        return &scene.models[ref.index].meshes[ref.sub].name;
    case Kind::PointCloud:  return &scene.pointClouds[ref.index].name;
    case Kind::SceneLayer:  return &scene.i3sLayers[ref.index]->name;
    case Kind::PointLight:  return &scene.pointLights[ref.index].name;
    case Kind::Measurement: return &scene.measurements[ref.index].name;
    case Kind::ClipPlane:   return &scene.clipPlanes[ref.index].name;
    case Kind::Group: {
        scene::Group* g = scene.findGroup(ref.id);
        return g ? &g->name : nullptr;
    }
    default:                return nullptr;
    }
}

} // namespace

// ── Esc cascade ──────────────────────────────────────────────────────────────

bool Application::escapeAction() {
    // Active tool first, then the selection (§7.1). Since Pass 7 this asks the
    // ToolManager instead of naming the clip tool, so EVERY registered tool —
    // including ones added later — exits on Esc with no edit here (§13).
    if (toolManager_.deactivateAll())
        return true;
    if (!selection_.empty()) {
        selection_.clear();
        return true;
    }
    return false;
}

// ── Recents ──────────────────────────────────────────────────────────────────

void Application::addRecentScene(const std::string& path) {
    std::error_code ec;
    std::string abs = fs::absolute(path, ec).string();
    if (ec || abs.empty())
        abs = path;
    std::vector<std::string>& recents = settings_.files.recentScenes;
    recents.erase(std::remove(recents.begin(), recents.end(), abs), recents.end());
    recents.insert(recents.begin(), abs);
    if (recents.size() > size_t(Gui::Settings::Files::kMaxRecentScenes))
        recents.resize(size_t(Gui::Settings::Files::kMaxRecentScenes));
}

// ── New / clear ──────────────────────────────────────────────────────────────

void Application::clearSceneContent() {
    // Detach everything that points into the scene, release layer GPU
    // residency, then wait for the device before the GPU-owning objects die
    // (models' MeshBuffers / clouds' pools may be referenced by an in-flight
    // frame — same policy the layer unload always used).
    gizmo_.clearTarget();
    gizmoDragging_ = false;
    gizmoUndo_.clear();
    isolate_ = IsolateState{};
    selection_.clear();
    pendingLayerStates_.clear();

    for (std::unique_ptr<scene::I3SSceneLayer>& layer : scene_.i3sLayers)
        if (layer)
            layer->releaseGpu(renderer_.materials(), renderer_.uploadRing(),
                              renderer_.frameRetireValue());
    if (device_.device() != VK_NULL_HANDLE)
        device_.waitIdle();

    // Undo closures may own deleted objects (GPU residency included) — they
    // must die while the device is idle.
    undo_.clear();

    scene_.models.clear();
    scene_.pointClouds.clear();
    scene_.i3sLayers.clear();
    scene_.pointLights.clear();
    scene_.measurements.clear();
    scene_.clipPlanes.clear();
    scene_.groups.clear();
    scene_.nextObjectId = 1;
    scene_.sourcePath.clear();
    scene_.camera = scene::CameraPose{};
    scene_.worldBoundsMin = glm::vec3(-5.0f);
    scene_.worldBoundsMax = glm::vec3(5.0f);
    clipPlaneTool_.notifyStorageChanged();
}

void Application::newScene() {
    clearSceneContent();
    scenePath_.clear();
    pushToast("New scene", Plugins::ToastLevel::Info);
}

// ── Open / merge ─────────────────────────────────────────────────────────────

void Application::openSceneDialog() {
    const std::vector<std::string> files =
        pfd::open_file("Open scene", "",
                       { "Scene files (*.scene)", "*.scene", "All files", "*" })
            .result();
    if (!files.empty())
        openSceneFile(files[0]);
}

void Application::mergeSceneDialog() {
    const std::vector<std::string> files =
        pfd::open_file("Merge scene", "",
                       { "Scene files (*.scene)", "*.scene", "All files", "*" })
            .result();
    if (!files.empty())
        mergeSceneFromFile(files[0]);
}

void Application::openSceneFile(const std::string& path) {
    const bool hasContent =
        !scene_.models.empty() || !scene_.pointClouds.empty() ||
        !scene_.i3sLayers.empty() || !scene_.pointLights.empty() ||
        !scene_.measurements.empty() || !scene_.clipPlanes.empty();
    if (!hasContent) {
        replaceSceneFromFile(path);
        return;
    }
    switch (settings_.files.openSceneMode) {
    case 1: maybeSafetySnapshotBeforeReplace(); replaceSceneFromFile(path); break;
    case 2: mergeSceneFromFile(path); break;
    default:
        // Ask (C8): park the path; the GuiSystem modal calls
        // resolvePendingSceneOpen with the choice (+ optional remember).
        pendingOpenPath_ = path;
        break;
    }
}

void Application::resolvePendingSceneOpen(int action, bool remember) {
    const std::string path = pendingOpenPath_;
    pendingOpenPath_.clear();
    if (path.empty() || action == 0)
        return;
    if (remember)
        settings_.files.openSceneMode = action; // 1 = replace, 2 = merge
    if (action == 1) {
        maybeSafetySnapshotBeforeReplace();
        replaceSceneFromFile(path);
    }
    else if (action == 2)
        mergeSceneFromFile(path);
}

void Application::reportSceneLoad(const scene::SceneLoadReport& report) {
    constexpr size_t kMaxToasts = 4;
    for (size_t i = 0; i < report.warnings.size() && i < kMaxToasts; ++i)
        pushToast(report.warnings[i], Plugins::ToastLevel::Warning);
    if (report.warnings.size() > kMaxToasts)
        pushToast("... and " +
                      std::to_string(report.warnings.size() - kMaxToasts) +
                      " more warnings (see the log)",
                  Plugins::ToastLevel::Warning);
}

void Application::replaceSceneFromFile(const std::string& path) {
    scene::SceneLoadResult result;
    try {
        result = scene::loadSceneDocument(path, device_, renderer_.materials(),
                                          settings_.pointCloud.downsample,
                                          settings_.pointCloud.mortonResort);
    } catch (const std::exception& e) {
        pushToast(std::string("Scene load failed: ") + e.what(),
                  Plugins::ToastLevel::Error);
        return;
    }

    clearSceneContent();
    // Member-wise move: the vector OBJECTS stay scene_'s members (the
    // measurement/clip tools' storage bindings stay valid), only their
    // contents are replaced.
    scene_ = std::move(result.scene);
    clipPlaneTool_.notifyStorageChanged();
    applyLoadedCamera(result.camera);
    applyLoadedEnvironment(result.environment);
    for (const scene::PendingLayerState& layer : result.layers) {
        pendingLayerStates_.push_back(layer);
        openSlpk(layer.sourcePath);
    }

    std::error_code ec;
    const std::string abs = fs::absolute(path, ec).string();
    scenePath_ = (ec || abs.empty()) ? path : abs;
    addRecentScene(scenePath_);
    reportSceneLoad(result.report);
    pushToast("Opened " + fileStem(path) +
                  (result.report.formatVersion < scene::kSceneFormatVersion
                       ? " (legacy v" +
                             std::to_string(result.report.formatVersion) +
                             " scene)"
                       : ""),
              Plugins::ToastLevel::Success);
}

void Application::mergeSceneFromFile(const std::string& path) {
    scene::SceneLoadResult result;
    try {
        result = scene::loadSceneDocument(path, device_, renderer_.materials(),
                                          settings_.pointCloud.downsample,
                                          settings_.pointCloud.mortonResort);
    } catch (const std::exception& e) {
        pushToast(std::string("Scene merge failed: ") + e.what(),
                  Plugins::ToastLevel::Error);
        return;
    }

    // The merged file's ids came from ITS counter — remap everything onto
    // this scene's counter, keeping the group structure intact.
    std::unordered_map<uint64_t, uint64_t> groupRemap;
    for (scene::Group& g : result.scene.groups) {
        const uint64_t fresh = scene_.allocateId();
        groupRemap[g.id] = fresh;
        g.id = fresh;
    }
    auto remapGroup = [&](uint64_t groupId) -> uint64_t {
        const auto it = groupRemap.find(groupId);
        return it != groupRemap.end() ? it->second : 0;
    };
    for (scene::Group& g : result.scene.groups) {
        g.parentId = remapGroup(g.parentId);
        scene_.groups.push_back(std::move(g));
    }
    size_t merged = 0;
    for (scene::Model& m : result.scene.models) {
        m.id = scene_.allocateId();
        m.groupId = remapGroup(m.groupId);
        scene_.models.push_back(std::move(m));
        ++merged;
    }
    for (Engine::PointCloud& pc : result.scene.pointClouds) {
        pc.id = scene_.allocateId();
        pc.groupId = remapGroup(pc.groupId);
        scene_.pointClouds.push_back(std::move(pc));
        ++merged;
    }
    for (scene::PointLight& l : result.scene.pointLights) {
        l.id = scene_.allocateId();
        l.groupId = remapGroup(l.groupId);
        scene_.pointLights.push_back(std::move(l));
        ++merged;
    }
    for (Engine::Measurement& m : result.scene.measurements) {
        m.id = scene_.allocateId();
        scene_.measurements.push_back(std::move(m));
        ++merged;
    }
    for (Engine::ClipPlane& p : result.scene.clipPlanes) {
        if (int(scene_.clipPlanes.size()) >= Engine::MAX_CLIP_PLANES) {
            pushToast("Clip plane '" + p.name + "' dropped: budget is full",
                      Plugins::ToastLevel::Warning);
            continue;
        }
        p.id = scene_.allocateId();
        scene_.clipPlanes.push_back(std::move(p));
        ++merged;
    }
    for (scene::PendingLayerState layer : result.layers) {
        layer.id = scene_.allocateId();
        layer.groupId = remapGroup(layer.groupId);
        pendingLayerStates_.push_back(layer);
        openSlpk(layer.sourcePath);
        ++merged;
    }

    // Camera/environment stay the user's — a merge adds objects, not a look.
    scene_.computeWorldBounds();
    reportSceneLoad(result.report);
    addRecentScene(path);
    pushToast("Merged " + std::to_string(merged) + " object(s) from " +
                  fileStem(path),
              Plugins::ToastLevel::Success);
}

// ── Save ─────────────────────────────────────────────────────────────────────

bool Application::saveScene() {
    if (scenePath_.empty())
        return saveSceneAs();

    // Camera + environment block (shared with autosave and snapshots).
    const scene::SceneSaveState state = currentSaveState();

    // Mirror the saver's extension fix so scenePath_ matches the file written.
    fs::path target(scenePath_);
    std::string ext = target.extension().string();
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return char(std::tolower(c)); });
    if (ext != ".scene")
        target.replace_extension(".scene");

    std::string error;
    if (!scene::saveSceneDocument(target.string(), scene_, state,
                                  renderer_.materials(), &error)) {
        pushToast("Scene save failed: " + error, Plugins::ToastLevel::Error);
        return false;
    }
    scenePath_ = target.string();
    addRecentScene(scenePath_);
    undo_.markSaved(); // History panel's last-saved marker (Pass 3 §9)
    pushToast("Saved " + fileStem(scenePath_), Plugins::ToastLevel::Success);
    return true;
}

bool Application::saveSceneAs() {
    const std::string suggested =
        scenePath_.empty() ? std::string("untitled.scene") : scenePath_;
    const std::string path =
        pfd::save_file("Save scene", suggested,
                       { "Scene files (*.scene)", "*.scene", "All files", "*" })
            .result();
    if (path.empty())
        return false;
    scenePath_ = path;
    return saveScene();
}

// ── Delete (batch, one undo step, objects live on in the closure) ───────────

void Application::deleteItems(const std::vector<SceneItemRef>& refs) {
    std::vector<SceneItemRef> targets = expandGroups(scene_, refs);
    if (targets.empty())
        return;

    // The gizmo may point into an object about to move out of the scene.
    gizmo_.clearTarget();
    gizmoDragging_ = false;

    // Removed objects are MOVED into shared storage captured by the undo
    // closures: undo moves them back, redo moves them out again.
    struct Store {
        std::vector<std::pair<int, scene::Model>> models;
        std::vector<std::pair<int, Engine::PointCloud>> clouds;
        std::vector<std::pair<int, scene::PointLight>> lights;
        std::vector<std::pair<int, Engine::Measurement>> measurements;
        std::vector<std::pair<int, Engine::ClipPlane>> planes;
        std::vector<std::pair<int, scene::Group>> groups;
        std::vector<scene::PendingLayerState> layers; // undo re-opens async
    };
    auto store = std::make_shared<Store>();

    // Partition by kind, remember the ORIGINAL indices (ascending), then
    // erase descending so indices stay valid during removal.
    std::vector<int> modelIdx, cloudIdx, lightIdx, measurementIdx, planeIdx,
        groupIdx, layerIdx;
    for (SceneItemRef& ref : targets) {
        if (!scene_.resolve(ref))
            continue;
        switch (ref.kind) {
        case Kind::Model:       modelIdx.push_back(ref.index); break;
        case Kind::PointCloud:  cloudIdx.push_back(ref.index); break;
        case Kind::PointLight:  lightIdx.push_back(ref.index); break;
        case Kind::Measurement: measurementIdx.push_back(ref.index); break;
        case Kind::ClipPlane:   planeIdx.push_back(ref.index); break;
        case Kind::SceneLayer:  layerIdx.push_back(ref.index); break;
        case Kind::Group: {
            for (size_t g = 0; g < scene_.groups.size(); ++g)
                if (scene_.groups[g].id == ref.id)
                    groupIdx.push_back(int(g));
            break;
        }
        default: break; // Sun/Environment are not deletable
        }
    }
    auto sortUnique = [](std::vector<int>& v) {
        std::sort(v.begin(), v.end());
        v.erase(std::unique(v.begin(), v.end()), v.end());
    };
    sortUnique(modelIdx);
    sortUnique(cloudIdx);
    sortUnique(lightIdx);
    sortUnique(measurementIdx);
    sortUnique(planeIdx);
    sortUnique(groupIdx);
    sortUnique(layerIdx);

    const size_t count = modelIdx.size() + cloudIdx.size() + lightIdx.size() +
                         measurementIdx.size() + planeIdx.size() +
                         groupIdx.size() + layerIdx.size();
    if (count == 0)
        return;

    // Move objects out (descending), keeping their original index for the
    // ascending re-insert on undo.
    for (auto it = modelIdx.rbegin(); it != modelIdx.rend(); ++it) {
        store->models.emplace_back(*it, std::move(scene_.models[*it]));
        scene_.models.erase(scene_.models.begin() + *it);
    }
    for (auto it = cloudIdx.rbegin(); it != cloudIdx.rend(); ++it) {
        store->clouds.emplace_back(*it, std::move(scene_.pointClouds[*it]));
        scene_.pointClouds.erase(scene_.pointClouds.begin() + *it);
    }
    for (auto it = lightIdx.rbegin(); it != lightIdx.rend(); ++it) {
        store->lights.emplace_back(*it, scene_.pointLights[*it]);
        scene_.pointLights.erase(scene_.pointLights.begin() + *it);
    }
    for (auto it = measurementIdx.rbegin(); it != measurementIdx.rend(); ++it) {
        store->measurements.emplace_back(*it,
                                         std::move(scene_.measurements[*it]));
        scene_.measurements.erase(scene_.measurements.begin() + *it);
    }
    for (auto it = planeIdx.rbegin(); it != planeIdx.rend(); ++it) {
        store->planes.emplace_back(*it, scene_.clipPlanes[*it]);
        scene_.clipPlanes.erase(scene_.clipPlanes.begin() + *it);
    }
    for (auto it = groupIdx.rbegin(); it != groupIdx.rend(); ++it) {
        store->groups.emplace_back(*it, scene_.groups[*it]);
        scene_.groups.erase(scene_.groups.begin() + *it);
    }
    // Layers can't be parked in RAM (GPU residency + worker threads): release
    // for real; undo re-opens the package with the saved identity state.
    for (auto it = layerIdx.rbegin(); it != layerIdx.rend(); ++it) {
        scene::I3SSceneLayer& layer = *scene_.i3sLayers[*it];
        scene::PendingLayerState st;
        st.sourcePath = layer.sourcePath;
        st.id = layer.id;
        st.groupId = layer.groupId;
        st.name = layer.name;
        st.visible = layer.visible;
        st.locked = layer.locked;
        st.showGeometry = layer.showGeometry;
        store->layers.push_back(std::move(st));
        layer.releaseGpu(renderer_.materials(), renderer_.uploadRing(),
                         renderer_.frameRetireValue());
        device_.waitIdle();
        scene_.i3sLayers.erase(scene_.i3sLayers.begin() + *it);
    }

    selection_.revalidate([this](SceneItemRef& r) { return scene_.resolve(r); });
    clipPlaneTool_.notifyStorageChanged();
    scene_.computeWorldBounds();

    // Every deleted id (all kinds) — the redo side re-deletes by id, because
    // container indices may have shifted between undo and redo (C3).
    auto idSet = std::make_shared<std::unordered_set<uint64_t>>();
    for (const auto& e : store->models) idSet->insert(e.second.id);
    for (const auto& e : store->clouds) idSet->insert(e.second.id);
    for (const auto& e : store->lights) idSet->insert(e.second.id);
    for (const auto& e : store->measurements) idSet->insert(e.second.id);
    for (const auto& e : store->planes) idSet->insert(e.second.id);
    for (const auto& e : store->groups) idSet->insert(e.second.id);
    auto layerStates = std::make_shared<std::vector<scene::PendingLayerState>>(
        store->layers);

    const std::string label =
        count == 1 ? "Delete object"
                   : "Delete " + std::to_string(count) + " objects";
    undo_.record(
        label,
        // undo: put everything back at its original index (the stores were
        // filled descending, so reverse iteration reinserts ascending);
        // deleted layers re-open asynchronously with their saved identity.
        [this, store]() {
            auto reinsert = [](auto& vec, auto& stored) {
                for (auto it = stored.rbegin(); it != stored.rend(); ++it) {
                    const size_t at = std::min(size_t(it->first), vec.size());
                    vec.insert(vec.begin() + std::ptrdiff_t(at),
                               std::move(it->second));
                }
                stored.clear();
            };
            reinsert(scene_.models, store->models);
            reinsert(scene_.pointClouds, store->clouds);
            reinsert(scene_.pointLights, store->lights);
            reinsert(scene_.measurements, store->measurements);
            reinsert(scene_.clipPlanes, store->planes);
            reinsert(scene_.groups, store->groups);
            for (const scene::PendingLayerState& st : store->layers) {
                pendingLayerStates_.push_back(st);
                openSlpk(st.sourcePath);
            }
            store->layers.clear();
            clipPlaneTool_.notifyStorageChanged();
            scene_.computeWorldBounds();
        },
        // redo: take the same objects out again, located by id.
        [this, store, idSet, layerStates]() {
            gizmo_.clearTarget();
            gizmoDragging_ = false;
            auto stash = [&](auto& vec, auto& stored, auto idOf) {
                for (int i = int(vec.size()) - 1; i >= 0; --i) {
                    if (idSet->count(idOf(vec[size_t(i)])) == 0)
                        continue;
                    stored.emplace_back(i, std::move(vec[size_t(i)]));
                    vec.erase(vec.begin() + i);
                }
            };
            stash(scene_.models, store->models,
                  [](const scene::Model& m) { return m.id; });
            stash(scene_.pointClouds, store->clouds,
                  [](const Engine::PointCloud& pc) { return pc.id; });
            stash(scene_.pointLights, store->lights,
                  [](const scene::PointLight& l) { return l.id; });
            stash(scene_.measurements, store->measurements,
                  [](const Engine::Measurement& m) { return m.id; });
            stash(scene_.clipPlanes, store->planes,
                  [](const Engine::ClipPlane& p) { return p.id; });
            stash(scene_.groups, store->groups,
                  [](const scene::Group& g) { return g.id; });
            for (const scene::PendingLayerState& st : *layerStates) {
                store->layers.push_back(st);
                const int idx = scene_.layerIndexOf(st.id);
                if (idx >= 0) {
                    scene_.i3sLayers[idx]->releaseGpu(
                        renderer_.materials(), renderer_.uploadRing(),
                        renderer_.frameRetireValue());
                    device_.waitIdle();
                    scene_.i3sLayers.erase(scene_.i3sLayers.begin() + idx);
                } else {
                    // The undo's async re-open hasn't finished: cancel both
                    // the pending identity state and the in-flight adopt.
                    pendingLayerStates_.erase(
                        std::remove_if(pendingLayerStates_.begin(),
                                       pendingLayerStates_.end(),
                                       [&](const scene::PendingLayerState& p) {
                                           return p.id == st.id;
                                       }),
                        pendingLayerStates_.end());
                    cancelledLayerOpens_.push_back(st.sourcePath);
                }
            }
            selection_.revalidate(
                [this](SceneItemRef& r) { return scene_.resolve(r); });
            clipPlaneTool_.notifyStorageChanged();
            scene_.computeWorldBounds();
        });
}

// ── Duplicate ────────────────────────────────────────────────────────────────

void Application::duplicateItems(const std::vector<SceneItemRef>& refs) {
    std::vector<SceneItemRef> targets = normalizeRefs(scene_, refs);
    if (targets.empty())
        return;

    std::vector<uint64_t> createdIds;
    std::vector<SceneItemRef> newSelection;

    for (SceneItemRef& ref : targets) {
        if (!scene_.resolve(ref))
            continue;
        switch (ref.kind) {
        case Kind::Model: {
            const scene::Model& src = scene_.models[ref.index];
            scene::Model copy;
            if (!src.primitiveType.empty()) {
                // Rebuild the primitive, then mirror the live material.
                renderer::gpu::MaterialData mat{};
                if (!src.meshes.empty() &&
                    src.meshes[0].materialIndex <
                        renderer_.materials().materials().size())
                    mat = renderer_.materials()
                              .materials()[src.meshes[0].materialIndex];
                copy = scene::makePrimitiveModel(
                    device_, renderer_.materials(),
                    scene::primitiveTypeFromString(src.primitiveType),
                    src.name + " copy", glm::vec3(mat.baseColor), mat.metallic,
                    mat.roughness, mat.emissive);
            } else if (!src.sourcePath.empty()) {
                if (!scene::importModelFile(src.sourcePath, device_,
                                            renderer_.materials(), copy)) {
                    pushToast("Duplicate failed: cannot re-import " +
                                  src.sourcePath,
                              Plugins::ToastLevel::Error);
                    continue;
                }
                copy.name = src.name + " copy";
                // Mirror the source's material values (edits included) onto
                // the freshly imported materials, mesh by mesh.
                const size_t n = std::min(copy.meshes.size(), src.meshes.size());
                for (size_t i = 0; i < n; ++i) {
                    const uint32_t from = src.meshes[i].materialIndex;
                    const uint32_t to = copy.meshes[i].materialIndex;
                    if (from < renderer_.materials().materials().size() &&
                        to < renderer_.materials().materials().size())
                        renderer_.materials().material(to) =
                            renderer_.materials().materials()[from];
                }
            } else {
                pushToast("Duplicate failed: '" + src.name +
                              "' has no rebuildable source",
                          Plugins::ToastLevel::Warning);
                continue;
            }
            copy.id = scene_.allocateId();
            copy.groupId = src.groupId;
            copy.position = src.position;
            copy.rotationDeg = src.rotationDeg;
            copy.scale = src.scale;
            copy.visible = src.visible;
            copy.emissive = src.emissive;
            createdIds.push_back(copy.id);
            newSelection.push_back(
                makeRef(Kind::Model, copy.id, int(scene_.models.size())));
            scene_.models.push_back(std::move(copy));
            break;
        }
        case Kind::PointCloud: {
            const Engine::PointCloud& src = scene_.pointClouds[ref.index];
            if (src.isStreaming()) {
                pushToast("'" + src.name +
                              "' is still streaming - duplicate when loaded",
                          Plugins::ToastLevel::Warning);
                continue;
            }
            // Shallow duplicate: the GPU residency is shared (shared_ptr), so
            // a duplicate costs no VRAM — the same points with their own
            // transform. Deleting either keeps the buffers alive for the other.
            Engine::PointCloud copy;
            copy.id = scene_.allocateId();
            copy.groupId = src.groupId;
            copy.name = src.name + " copy";
            copy.filePath = src.filePath;
            copy.position = src.position;
            copy.rotation = src.rotation;
            copy.scale = src.scale;
            copy.visible = src.visible;
            copy.gpu = src.gpu;
            copy.totalPointCount = src.totalPointCount;
            copy.numBatches = src.numBatches;
            copy.computePointsPerThread = src.computePointsPerThread;
            copy.boundsMin = src.boundsMin;
            copy.boundsMax = src.boundsMax;
            copy.basePointSize = src.basePointSize;
            createdIds.push_back(copy.id);
            newSelection.push_back(makeRef(Kind::PointCloud, copy.id,
                                           int(scene_.pointClouds.size())));
            scene_.pointClouds.push_back(std::move(copy));
            break;
        }
        case Kind::PointLight: {
            scene::PointLight copy = scene_.pointLights[ref.index];
            copy.id = scene_.allocateId();
            if (!copy.name.empty())
                copy.name += " copy";
            createdIds.push_back(copy.id);
            newSelection.push_back(makeRef(Kind::PointLight, copy.id,
                                           int(scene_.pointLights.size())));
            scene_.pointLights.push_back(std::move(copy));
            break;
        }
        case Kind::Measurement: {
            Engine::Measurement copy = scene_.measurements[ref.index];
            copy.id = scene_.allocateId();
            copy.name += " copy";
            createdIds.push_back(copy.id);
            newSelection.push_back(makeRef(Kind::Measurement, copy.id,
                                           int(scene_.measurements.size())));
            scene_.measurements.push_back(std::move(copy));
            break;
        }
        case Kind::ClipPlane: {
            if (int(scene_.clipPlanes.size()) >= Engine::MAX_CLIP_PLANES) {
                pushToast("Clip plane budget is full",
                          Plugins::ToastLevel::Warning);
                continue;
            }
            Engine::ClipPlane copy = scene_.clipPlanes[ref.index];
            copy.id = scene_.allocateId();
            copy.name += " copy";
            createdIds.push_back(copy.id);
            newSelection.push_back(makeRef(Kind::ClipPlane, copy.id,
                                           int(scene_.clipPlanes.size())));
            scene_.clipPlanes.push_back(std::move(copy));
            break;
        }
        case Kind::SceneLayer:
            pushToast("Scene layers can't be duplicated (open the package "
                      "again instead)",
                      Plugins::ToastLevel::Info);
            continue;
        case Kind::Group:
            pushToast("Duplicate the group's objects, not the folder",
                      Plugins::ToastLevel::Info);
            continue;
        default:
            continue;
        }
    }

    if (createdIds.empty())
        return;
    scene_.computeWorldBounds();
    selection_.set(newSelection);

    // Undo removes the duplicates (moved into the store so redo can restore
    // them without re-importing).
    struct DupStore {
        std::vector<std::pair<int, scene::Model>> models;
        std::vector<std::pair<int, Engine::PointCloud>> clouds;
        std::vector<std::pair<int, scene::PointLight>> lights;
        std::vector<std::pair<int, Engine::Measurement>> measurements;
        std::vector<std::pair<int, Engine::ClipPlane>> planes;
    };
    auto dupStore = std::make_shared<DupStore>();
    auto idSet = std::make_shared<std::unordered_set<uint64_t>>(
        createdIds.begin(), createdIds.end());
    const std::string label =
        createdIds.size() == 1
            ? "Duplicate object"
            : "Duplicate " + std::to_string(createdIds.size()) + " objects";
    undo_.record(
        label,
        [this, dupStore, idSet]() {
            gizmo_.clearTarget();
            gizmoDragging_ = false;
            auto stash = [&](auto& vec, auto& stored, auto idOf) {
                for (int i = int(vec.size()) - 1; i >= 0; --i) {
                    if (idSet->count(idOf(vec[size_t(i)])) == 0)
                        continue;
                    stored.emplace_back(i, std::move(vec[size_t(i)]));
                    vec.erase(vec.begin() + i);
                }
            };
            stash(scene_.models, dupStore->models,
                  [](const scene::Model& m) { return m.id; });
            stash(scene_.pointClouds, dupStore->clouds,
                  [](const Engine::PointCloud& pc) { return pc.id; });
            stash(scene_.pointLights, dupStore->lights,
                  [](const scene::PointLight& l) { return l.id; });
            stash(scene_.measurements, dupStore->measurements,
                  [](const Engine::Measurement& m) { return m.id; });
            stash(scene_.clipPlanes, dupStore->planes,
                  [](const Engine::ClipPlane& p) { return p.id; });
            selection_.revalidate(
                [this](SceneItemRef& r) { return scene_.resolve(r); });
            clipPlaneTool_.notifyStorageChanged();
            scene_.computeWorldBounds();
        },
        [this, dupStore]() {
            auto reinsert = [](auto& vec, auto& stored) {
                for (auto it = stored.rbegin(); it != stored.rend(); ++it) {
                    const size_t at = std::min(size_t(it->first), vec.size());
                    vec.insert(vec.begin() + std::ptrdiff_t(at),
                               std::move(it->second));
                }
                stored.clear();
            };
            reinsert(scene_.models, dupStore->models);
            reinsert(scene_.pointClouds, dupStore->clouds);
            reinsert(scene_.pointLights, dupStore->lights);
            reinsert(scene_.measurements, dupStore->measurements);
            reinsert(scene_.clipPlanes, dupStore->planes);
            clipPlaneTool_.notifyStorageChanged();
            scene_.computeWorldBounds();
        });
    pushToast(label, Plugins::ToastLevel::Success);
}

// ── Visibility / lock (batch, one undo step) ─────────────────────────────────

void Application::setItemsVisible(const std::vector<SceneItemRef>& refs,
                                  bool visible) {
    struct Change { SceneItemRef ref; bool before; };
    std::vector<Change> changes;
    for (const SceneItemRef& ref : normalizeRefs(scene_, refs)) {
        // The sun's eye toggles a Setting (resettable, not undoable — C4).
        if (ref.kind == Kind::Sun) {
            settings_.lighting.sun.enabled = visible;
            continue;
        }
        bool* flag = visibleFlagFor(scene_, ref);
        if (!flag || *flag == visible)
            continue;
        changes.push_back({ ref, *flag });
        *flag = visible;
    }
    if (changes.empty())
        return;
    scene_.computeWorldBounds();

    const std::string label =
        std::string(visible ? "Show " : "Hide ") +
        (changes.size() == 1 ? "object"
                             : std::to_string(changes.size()) + " objects");
    undo_.record(
        label,
        [this, changes]() {
            for (const Change& c : changes)
                if (bool* flag = visibleFlagFor(scene_, c.ref))
                    *flag = c.before;
            scene_.computeWorldBounds();
        },
        [this, changes, visible]() {
            for (const Change& c : changes)
                if (bool* flag = visibleFlagFor(scene_, c.ref))
                    *flag = visible;
            scene_.computeWorldBounds();
        });
}

void Application::setItemsLocked(const std::vector<SceneItemRef>& refs,
                                 bool locked) {
    struct Change { SceneItemRef ref; bool before; };
    std::vector<Change> changes;
    for (const SceneItemRef& ref : normalizeRefs(scene_, refs)) {
        bool* flag = lockedFlagFor(scene_, ref);
        if (!flag || *flag == locked)
            continue;
        changes.push_back({ ref, *flag });
        *flag = locked;
    }
    if (changes.empty())
        return;
    if (locked) {
        // A locked object must not stay bound to the gizmo.
        gizmo_.clearTarget();
        gizmoDragging_ = false;
    }
    const std::string label =
        std::string(locked ? "Lock " : "Unlock ") +
        (changes.size() == 1 ? "object"
                             : std::to_string(changes.size()) + " objects");
    undo_.record(
        label,
        [this, changes]() {
            for (const Change& c : changes)
                if (bool* flag = lockedFlagFor(scene_, c.ref))
                    *flag = c.before;
            gizmo_.clearTarget();
        },
        [this, changes, locked]() {
            for (const Change& c : changes)
                if (bool* flag = lockedFlagFor(scene_, c.ref))
                    *flag = locked;
            gizmo_.clearTarget();
        });
}

// ── Grouping ─────────────────────────────────────────────────────────────────

uint64_t Application::groupItems(const std::vector<SceneItemRef>& refs) {
    struct Change { SceneItemRef ref; uint64_t before; };
    std::vector<SceneItemRef> targets = normalizeRefs(scene_, refs);
    std::vector<Change> members;
    for (const SceneItemRef& ref : targets)
        if (groupFieldFor(scene_, ref))
            members.push_back({ ref, 0 });
    if (members.empty())
        return 0;

    // The new group nests where its members lived, when they agree.
    uint64_t parentId = 0;
    bool first = true;
    for (Change& m : members) {
        const uint64_t current = *groupFieldFor(scene_, m.ref);
        m.before = current;
        if (first) {
            parentId = current;
            first = false;
        } else if (parentId != current) {
            parentId = 0;
        }
    }
    // Grouping a group under itself would cycle — root the new group then.
    for (const Change& m : members)
        if (wouldCycle(scene_, m.ref, parentId))
            parentId = 0;

    scene::Group group;
    group.id = scene_.allocateId();
    group.parentId = parentId;
    int n = 1;
    for (const scene::Group& g : scene_.groups)
        if (g.name.rfind("Group", 0) == 0)
            ++n;
    group.name = n == 1 ? "Group" : "Group " + std::to_string(n);
    const uint64_t groupId = group.id;
    scene_.groups.push_back(group);
    for (const Change& m : members)
        *groupFieldFor(scene_, m.ref) = groupId;

    undo_.record(
        "Group " + std::to_string(members.size()) +
            (members.size() == 1 ? " object" : " objects"),
        [this, members, groupId]() {
            for (const Change& m : members)
                if (uint64_t* field = groupFieldFor(scene_, m.ref))
                    *field = m.before;
            for (size_t g = 0; g < scene_.groups.size(); ++g) {
                if (scene_.groups[g].id == groupId) {
                    scene_.groups.erase(scene_.groups.begin() +
                                        std::ptrdiff_t(g));
                    break;
                }
            }
            selection_.revalidate(
                [this](SceneItemRef& r) { return scene_.resolve(r); });
        },
        [this, members, group]() {
            scene_.groups.push_back(group);
            for (const Change& m : members)
                if (uint64_t* field = groupFieldFor(scene_, m.ref))
                    *field = group.id;
        });
    return groupId;
}

void Application::ungroupItems(const std::vector<SceneItemRef>& refs) {
    // Group refs dissolve (members move to the group's parent, the folder
    // dies); object refs step one level up.
    struct FieldChange { SceneItemRef ref; uint64_t before, after; };
    struct RemovedGroup { int index; scene::Group group; };
    std::vector<FieldChange> changes;
    std::vector<RemovedGroup> removed;

    std::vector<SceneItemRef> targets = normalizeRefs(scene_, refs);
    for (const SceneItemRef& ref : targets) {
        if (ref.kind == Kind::Group) {
            const scene::Group* g = scene_.findGroup(ref.id);
            if (!g)
                continue;
            const uint64_t parent = g->parentId;
            // Members (objects + subgroups) move to the dissolved group's parent.
            auto migrate = [&](SceneItemRef memberRef, uint64_t* field) {
                if (field && *field == ref.id)
                    changes.push_back({ memberRef, *field, parent });
            };
            for (size_t i = 0; i < scene_.models.size(); ++i)
                migrate(makeRef(Kind::Model, scene_.models[i].id, int(i)),
                        &scene_.models[i].groupId);
            for (size_t i = 0; i < scene_.pointClouds.size(); ++i)
                migrate(makeRef(Kind::PointCloud, scene_.pointClouds[i].id, int(i)),
                        &scene_.pointClouds[i].groupId);
            for (size_t i = 0; i < scene_.i3sLayers.size(); ++i)
                if (scene_.i3sLayers[i])
                    migrate(makeRef(Kind::SceneLayer, scene_.i3sLayers[i]->id,
                                    int(i)),
                            &scene_.i3sLayers[i]->groupId);
            for (size_t i = 0; i < scene_.pointLights.size(); ++i)
                migrate(makeRef(Kind::PointLight, scene_.pointLights[i].id, int(i)),
                        &scene_.pointLights[i].groupId);
            for (size_t i = 0; i < scene_.groups.size(); ++i)
                if (scene_.groups[i].id != ref.id)
                    migrate(makeRef(Kind::Group, scene_.groups[i].id, int(i)),
                            &scene_.groups[i].parentId);
            for (size_t g = 0; g < scene_.groups.size(); ++g)
                if (scene_.groups[g].id == ref.id)
                    removed.push_back({ int(g), scene_.groups[g] });
        } else if (uint64_t* field = groupFieldFor(scene_, ref)) {
            if (*field == 0)
                continue;
            const scene::Group* g = scene_.findGroup(*field);
            changes.push_back({ ref, *field, g ? g->parentId : 0 });
        }
    }
    if (changes.empty() && removed.empty())
        return;

    // Apply: field moves first, then remove dissolved groups (descending).
    for (const FieldChange& c : changes)
        if (uint64_t* field = groupFieldFor(scene_, c.ref))
            *field = c.after;
    std::sort(removed.begin(), removed.end(),
              [](const RemovedGroup& a, const RemovedGroup& b) {
                  return a.index > b.index;
              });
    for (const RemovedGroup& r : removed)
        scene_.groups.erase(scene_.groups.begin() + r.index);
    selection_.revalidate([this](SceneItemRef& r) { return scene_.resolve(r); });

    undo_.record(
        "Ungroup",
        [this, changes, removed]() {
            // Reinsert dissolved groups ascending, then restore fields.
            for (auto it = removed.rbegin(); it != removed.rend(); ++it) {
                const size_t at =
                    std::min(size_t(it->index), scene_.groups.size());
                scene_.groups.insert(scene_.groups.begin() + std::ptrdiff_t(at),
                                     it->group);
            }
            for (const FieldChange& c : changes)
                if (uint64_t* field = groupFieldFor(scene_, c.ref))
                    *field = c.before;
        },
        [this, changes, removed]() {
            for (const FieldChange& c : changes)
                if (uint64_t* field = groupFieldFor(scene_, c.ref))
                    *field = c.after;
            for (const RemovedGroup& r : removed) {
                for (size_t g = 0; g < scene_.groups.size(); ++g) {
                    if (scene_.groups[g].id == r.group.id) {
                        scene_.groups.erase(scene_.groups.begin() +
                                            std::ptrdiff_t(g));
                        break;
                    }
                }
            }
            selection_.revalidate(
                [this](SceneItemRef& r) { return scene_.resolve(r); });
        });
}

void Application::moveItemsToGroup(const std::vector<SceneItemRef>& refs,
                                   uint64_t groupId) {
    if (groupId != 0 && !scene_.findGroup(groupId))
        return;
    struct Change { SceneItemRef ref; uint64_t before; };
    std::vector<Change> changes;
    for (const SceneItemRef& ref : normalizeRefs(scene_, refs)) {
        uint64_t* field = groupFieldFor(scene_, ref);
        if (!field || *field == groupId)
            continue;
        if (ref.kind == Kind::Group &&
            (ref.id == groupId || wouldCycle(scene_, ref, groupId)))
            continue; // never create a parent cycle
        changes.push_back({ ref, *field });
        *field = groupId;
    }
    if (changes.empty())
        return;
    undo_.record(
        changes.size() == 1 ? "Move object to group" : "Move objects to group",
        [this, changes]() {
            for (const Change& c : changes)
                if (uint64_t* field = groupFieldFor(scene_, c.ref))
                    *field = c.before;
        },
        [this, changes, groupId]() {
            for (const Change& c : changes)
                if (uint64_t* field = groupFieldFor(scene_, c.ref))
                    *field = groupId;
        });
}

// ── Rename ───────────────────────────────────────────────────────────────────

void Application::renameItem(const SceneItemRef& ref, const std::string& name) {
    std::string* field = nameFieldFor(scene_, ref);
    if (!field || *field == name || name.empty())
        return;
    const std::string before = *field;
    *field = name;
    undo_.record(
        "Rename to \"" + name + "\"",
        [this, ref, before]() {
            if (std::string* f = nameFieldFor(scene_, ref))
                *f = before;
        },
        [this, ref, name]() {
            if (std::string* f = nameFieldFor(scene_, ref))
                *f = name;
        });
}

// ── Frame ────────────────────────────────────────────────────────────────────

void Application::frameItems(const std::vector<SceneItemRef>& refs) {
    // Union world-space bounds over every ref (groups expand to members).
    glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
    auto unionPoint = [&](const glm::vec3& p) {
        lo = glm::min(lo, p);
        hi = glm::max(hi, p);
    };
    auto unionCorners = [&](const glm::mat4& m, glm::vec3 bmin, glm::vec3 bmax) {
        if (bmin.x > bmax.x)
            return;
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local((corner & 1) ? bmax.x : bmin.x,
                                  (corner & 2) ? bmax.y : bmin.y,
                                  (corner & 4) ? bmax.z : bmin.z);
            unionPoint(glm::vec3(m * glm::vec4(local, 1.0f)));
        }
    };

    for (SceneItemRef ref : expandGroups(scene_, refs)) {
        if (!scene_.resolve(ref))
            continue;
        switch (ref.kind) {
        case Kind::Model:
        case Kind::Mesh: {
            const scene::Model& model = scene_.models[ref.index];
            const glm::mat4 m = model.modelMatrix();
            if (ref.kind == Kind::Mesh) {
                unionCorners(m, model.meshes[ref.sub].boundsMin,
                             model.meshes[ref.sub].boundsMax);
            } else {
                for (const scene::ModelMesh& mesh : model.meshes)
                    unionCorners(m, mesh.boundsMin, mesh.boundsMax);
            }
            break;
        }
        case Kind::PointCloud: {
            const Engine::PointCloud& pc = scene_.pointClouds[ref.index];
            if (!pc.hasBounds())
                break;
            glm::mat4 m(1.0f);
            m = glm::translate(m, pc.position);
            m = glm::rotate(m, glm::radians(pc.rotation.x), glm::vec3(1, 0, 0));
            m = glm::rotate(m, glm::radians(pc.rotation.y), glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(pc.rotation.z), glm::vec3(0, 0, 1));
            m = glm::scale(m, pc.scale);
            unionCorners(m, pc.boundsMin, pc.boundsMax);
            break;
        }
        case Kind::SceneLayer: {
            const scene::I3SSceneLayer& layer = *scene_.i3sLayers[ref.index];
            if (!layer.nodeBoxes.empty())
                unionCorners(glm::mat4(1.0f), layer.boundsMin, layer.boundsMax);
            break;
        }
        case Kind::PointLight: {
            const glm::vec3 p = scene_.pointLights[ref.index].position;
            unionPoint(p - glm::vec3(0.5f));
            unionPoint(p + glm::vec3(0.5f));
            break;
        }
        case Kind::Measurement: {
            for (const glm::vec3& p : scene_.measurements[ref.index].points)
                unionPoint(p);
            break;
        }
        case Kind::ClipPlane: {
            const Engine::ClipPlane& plane = scene_.clipPlanes[ref.index];
            const float s = clipPlaneTool_.displaySize;
            unionPoint(plane.position - glm::vec3(s));
            unionPoint(plane.position + glm::vec3(s));
            break;
        }
        default:
            break;
        }
    }
    if (lo.x > hi.x)
        return;

    // Same fit as frameI3SLayer: bounding sphere into the vertical FOV from a
    // pleasant 3/4 view, on the ACTIVE viewport's camera (§5.2).
    const glm::vec3 center = (lo + hi) * 0.5f;
    const float radius = std::max(glm::length(hi - lo) * 0.5f, 0.25f);
    const float fovRad = glm::radians(std::max(settings_.camera.fovDeg, 10.0f));
    const float distance = radius / std::tan(fovRad * 0.5f) * 1.15f;
    const glm::vec3 viewDir = glm::normalize(glm::vec3(0.55f, 0.45f, 0.9f));

    Camera& cam = activeCamera();
    Camera::CameraState st = cam.GetState();
    st.position = center + viewDir * distance;
    const glm::vec3 f = glm::normalize(center - st.position);
    const glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 u = glm::normalize(glm::cross(r, f));
    st.orientation = glm::normalize(glm::quat_cast(glm::mat3(r, u, -f)));
    cam.SetState(st);
    cam.SetOrbitPointDirectly(center);
}

// ── Isolate ──────────────────────────────────────────────────────────────────

void Application::isolateItems(const std::vector<SceneItemRef>& refs) {
    if (isolate_.active)
        exitIsolate(); // re-isolating replaces the current isolation

    std::unordered_set<uint64_t> keep;
    for (const SceneItemRef& ref : expandGroups(scene_, refs))
        keep.insert(ref.id);
    if (keep.empty())
        return;

    // Save + rewrite the visible flags: targets show, everything else hides.
    // Groups all go visible so a target inside a hidden folder still shows
    // (per-object flags carry the hiding).
    std::vector<IsolateEntry> saved;
    auto apply = [&](SceneItemRef ref, bool* flag) {
        if (!flag)
            return;
        saved.push_back({ ref, *flag });
        *flag = keep.count(ref.id) > 0;
    };
    for (size_t i = 0; i < scene_.models.size(); ++i)
        apply(makeRef(Kind::Model, scene_.models[i].id, int(i)),
              &scene_.models[i].visible);
    for (size_t i = 0; i < scene_.pointClouds.size(); ++i)
        apply(makeRef(Kind::PointCloud, scene_.pointClouds[i].id, int(i)),
              &scene_.pointClouds[i].visible);
    for (size_t i = 0; i < scene_.i3sLayers.size(); ++i)
        if (scene_.i3sLayers[i])
            apply(makeRef(Kind::SceneLayer, scene_.i3sLayers[i]->id, int(i)),
                  &scene_.i3sLayers[i]->visible);
    for (size_t i = 0; i < scene_.pointLights.size(); ++i)
        apply(makeRef(Kind::PointLight, scene_.pointLights[i].id, int(i)),
              &scene_.pointLights[i].visible);
    for (size_t i = 0; i < scene_.measurements.size(); ++i)
        apply(makeRef(Kind::Measurement, scene_.measurements[i].id, int(i)),
              &scene_.measurements[i].visible);
    for (size_t i = 0; i < scene_.groups.size(); ++i) {
        saved.push_back({ makeRef(Kind::Group, scene_.groups[i].id, int(i)),
                          scene_.groups[i].visible });
        scene_.groups[i].visible = true;
    }

    isolate_.active = true;
    isolate_.saved = saved;
    scene_.computeWorldBounds();

    undo_.record(
        "Isolate selection",
        [this, saved]() {
            for (const IsolateEntry& e : saved)
                if (bool* flag = visibleFlagFor(scene_, e.ref))
                    *flag = e.visible;
            isolate_ = IsolateState{};
            scene_.computeWorldBounds();
        },
        [this, saved, keep]() {
            for (const IsolateEntry& e : saved) {
                if (e.ref.kind == Kind::Group) {
                    if (scene::Group* g = scene_.findGroup(e.ref.id))
                        g->visible = true;
                } else if (bool* flag = visibleFlagFor(scene_, e.ref)) {
                    *flag = keep.count(e.ref.id) > 0;
                }
            }
            isolate_.active = true;
            isolate_.saved = saved;
            scene_.computeWorldBounds();
        });
}

void Application::exitIsolate() {
    if (!isolate_.active)
        return;
    const std::vector<IsolateEntry> saved = isolate_.saved;
    // Capture the isolation-time flag values BEFORE restoring, so undo of
    // "exit" re-enters the exact same isolation (targets visible, rest
    // hidden — including any eye toggles the user made while isolated).
    std::vector<IsolateEntry> isoState;
    isoState.reserve(saved.size());
    for (const IsolateEntry& e : saved) {
        bool current = e.visible;
        if (bool* flag = visibleFlagFor(scene_, e.ref))
            current = *flag;
        isoState.push_back({ e.ref, current });
    }

    isolate_ = IsolateState{};
    for (const IsolateEntry& e : saved)
        if (bool* flag = visibleFlagFor(scene_, e.ref))
            *flag = e.visible;
    scene_.computeWorldBounds();

    undo_.record(
        "Exit isolate",
        [this, saved, isoState]() {
            for (const IsolateEntry& e : isoState)
                if (bool* flag = visibleFlagFor(scene_, e.ref))
                    *flag = e.visible;
            isolate_.active = true;
            isolate_.saved = saved;
            scene_.computeWorldBounds();
        },
        [this, saved]() {
            for (const IsolateEntry& e : saved)
                if (bool* flag = visibleFlagFor(scene_, e.ref))
                    *flag = e.visible;
            isolate_ = IsolateState{};
            scene_.computeWorldBounds();
        });
}

} // namespace app
