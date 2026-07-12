#pragma once

// ============================================================================
//  scene::SceneDocument — the .scene file format (UI redesign Pass 1; §7.2)
// ----------------------------------------------------------------------------
//  Native re-implementation of the excluded GL SceneManager's save/load — the
//  GL sources are the behaviour reference only, nothing here includes them.
//
//    * SAVE writes the v3 format: object ids + the id counter (contract C3),
//      user groups ({id, name, visible, locked, parentId} + groupId on
//      objects), light names, object display names, and REFERENCE asset paths
//      (scene-relative primary + absolute fallback — unlike the GL v2 save,
//      which copied every asset into a scene folder; v3 scenes are cheap
//      enough for autosave, §11). SLPK layers serialize source path + display
//      state; streaming state does not.
//    * LOAD reads v3 natively and GL-era v1/v2 files forever (guardrail):
//      untagged v1, tagged v2 (metadata/environment/sun blocks, "<stem>/"
//      folder-relative "localPath"/"dataPath" assets, measurements, clip
//      planes, chunked files). Legacy objects get fresh ids assigned.
//    * Per-object failures never abort a load: they are collected as warnings
//      in the report (missing assets are reported, not silently dropped).
//
//  I3S layers parse on worker threads (Application::openSlpk), so a load
//  returns them as PendingLayerState entries for the app to open async and
//  re-apply the saved identity/display state on adoption.
//
//  This is the Scene layer: it may touch rhi::Device / MaterialSystem /
//  PointCloudLoader (like the importers), never the GUI.
// ============================================================================

#include "Scene/Scene.h"

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <string>
#include <vector>

namespace rhi {
class Device;
}
namespace renderer {
class MaterialSystem;
}

namespace scene {

// Current on-disk format. v1 = untagged GL, v2 = tagged GL, v3 = this branch.
inline constexpr int kSceneFormatVersion = 3;

struct SceneLoadReport {
    int formatVersion = 0;
    std::vector<std::string> warnings; // human-readable, toast-able
    int modelsLoaded = 0, modelsFailed = 0;
    int pointCloudsLoaded = 0, pointCloudsFailed = 0;
    int lightsLoaded = 0;
    int measurementsLoaded = 0;
    int clipPlanesLoaded = 0;
    int layersQueued = 0; // SLPK opens handed back for async loading

    bool hasWarnings() const { return !warnings.empty(); }
};

// Serialized identity/display state of one SLPK layer: the app opens the
// package asynchronously and re-applies this when the parsed layer is adopted.
struct PendingLayerState {
    std::string sourcePath; // absolute path of the .slpk
    uint64_t id = 0;
    uint64_t groupId = 0;
    std::string name;   // saved display name ("" = keep the layer's own)
    bool visible = true;
    bool locked = false;
    bool showGeometry = true;
};

// Full camera state (GL v1/v2-compatible keys). `orientation` is the
// authoritative pose when hasOrientation is set; position/front/up remain for
// older files.
struct SceneCameraState {
    bool valid = false;
    glm::vec3 position{ 3.0f, 3.0f, 7.0f };
    glm::vec3 front{ 0.0f, 0.0f, -1.0f };
    glm::vec3 up{ 0.0f, 1.0f, 0.0f };
    float yaw = 0.0f, pitch = 0.0f, zoom = 45.0f;
    bool hasOrientation = false;
    glm::quat orientation{ 1.0f, 0.0f, 0.0f, 0.0f };
};

// Scene-authored environment (v2 "environment"/"sun" blocks, v3 native).
struct SceneEnvironmentState {
    bool hasSun = false;
    renderer::SunState sun;
    bool hasSky = false;
    renderer::SkyState sky;
};

struct SceneLoadResult {
    Scene scene; // models/clouds/lights/measurements/planes/groups (layers async)
    std::vector<PendingLayerState> layers;
    SceneCameraState camera;
    SceneEnvironmentState environment;
    SceneLoadReport report;
};

// Load a .scene file of any supported version. Throws std::runtime_error when
// the file cannot be read or parsed at all; individual object failures land in
// result.report.warnings. pointCloudDownsample/mortonResort mirror the app's
// point-cloud load settings (LAS/LAZ sources come back streaming).
SceneLoadResult loadSceneDocument(const std::string& path, rhi::Device& device,
                                  renderer::MaterialSystem& materials,
                                  int pointCloudDownsample, bool mortonResort);

// Everything the v3 save serializes besides the scene itself.
struct SceneSaveState {
    SceneCameraState camera;
    SceneEnvironmentState environment;
};

// Write `scene` to `path` as v3 (atomic: temp file + rename, so a crash
// mid-save never corrupts the previous file). `materials` supplies the live
// material values for primitive/mesh blocks. Returns false with *error set on
// I/O failure.
bool saveSceneDocument(const std::string& path, const Scene& scene,
                       const SceneSaveState& state,
                       const renderer::MaterialSystem& materials,
                       std::string* error = nullptr);

} // namespace scene
