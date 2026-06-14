#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Core/Camera.h"
#include "Core/SceneManager.h"
#include "Core/UndoManager.h"
#include "Engine/Core.h"
#include "Engine/Data.h"
#include "Tools/BrushTool.h"
#include "Tools/ClipPlaneTool.h"
#include "Tools/MeasurementTool.h"

namespace Core {

// -----------------------------------------------------------------------
// Snapshot system.
//
// A Snapshot captures a chosen subset of the live scene so the user can
// return to it later (a coarse-grained, named "checkpoint" that complements
// the fine-grained Undo history). The aspects saved are selectable per
// snapshot via SnapshotFlags, so a snapshot may store only the camera, only
// the scene, only the tools, or any combination. Every snapshot also keeps a
// thumbnail captured from the viewer when it was taken.
//
// Thumbnails are written to a per-session temporary folder on disk as PNG
// (not kept in RAM); when a scene is saved each snapshot's data and thumbnail
// are persisted into the scene directory, and loaded back with the scene.
//
// Scene restore re-applies object transforms / materials by index and fully
// replaces the lights, sun, measurements and clip planes; it does not
// re-create objects that were deleted after the snapshot was taken.
// -----------------------------------------------------------------------

enum SnapshotFlags : uint32_t {
  SNAPSHOT_NONE = 0,
  SNAPSHOT_CAMERA = 1u << 0,
  SNAPSHOT_SCENE = 1u << 1,
  SNAPSHOT_TOOLS = 1u << 2,
};

// Saved per-object scene state. Models / point clouds reuse the Undo edit
// snapshots (transform + material), applied back by index. Lights, sun,
// measurements and clip planes are small/POD-like and stored as full copies.
struct SnapshotSceneState {
  std::vector<Engine::Undo::ModelEditState> models;
  std::vector<Engine::Undo::PointCloudEditState> pointClouds;
  std::vector<Engine::PointLight> pointLights;
  std::vector<Engine::SpotLight> spotLights;
  std::vector<Engine::Measurement> measurements;
  std::vector<Engine::ClipPlane> clipPlanes;
  Engine::Sun sun{};
};

// Saved runtime tool settings (the tools' editable scene data -- measurements
// and clip planes -- live in the scene and are covered by SnapshotSceneState).
struct SnapshotToolState {
  // Brush tool
  bool brushEnabled = false;
  float brushRadius = 1.0f;
  float brushDensity = 1.0f;
  float brushMinSpacing = 0.0f;
  int brushSelectedModel = -1;
  // Measurement tool
  bool measureEnabled = false;
  int measureMode = 0;
  float measureLineWidth = 2.5f;
  bool measureShowLabels = true;
  bool measureShowSegmentLabels = true;
  bool measureXRay = true;
  float measureUnitScale = 1.0f;
  std::string measureUnitSuffix = "m";
  glm::vec3 measureNextColor = glm::vec3(1.0f, 0.76f, 0.03f);
  // Clip-plane tool
  bool clipEnabled = false;
  float clipDisplaySize = 2.0f;
  float clipNudgeStep = 0.10f;
  int clipActiveIndex = -1;
};

struct Snapshot {
  std::string name;
  std::string timestamp;
  uint32_t flags = SNAPSHOT_NONE;

  // Organization metadata (user-editable after capture, persisted with the
  // scene). `tags` are freeform labels / categories used for filtering and
  // search; `color` is an optional marker used to visually group snapshots in
  // the panel (only shown when `hasColor` is set).
  std::vector<std::string> tags;
  bool hasColor = false;
  glm::vec3 color = glm::vec3(0.40f, 0.65f, 1.0f);

  Camera::CameraState camera{};
  SnapshotSceneState scene;
  SnapshotToolState tools;

  // Thumbnail: a downscaled PNG written to disk (thumbnailPath) plus the GL
  // texture built from it for display in the GUI. The texture is owned by the
  // snapshot and released through SnapshotManager::remove()/clear().
  std::string thumbnailPath;
  int thumbWidth = 0;
  int thumbHeight = 0;
  GLuint thumbnailTexture = 0;

  Snapshot() = default;
  Snapshot(const Snapshot &) = delete;
  Snapshot &operator=(const Snapshot &) = delete;
  Snapshot(Snapshot &&) = default;
  Snapshot &operator=(Snapshot &&) = default;
};

class SnapshotManager {
public:
  static SnapshotManager &instance();

  // Capture a new snapshot of the requested aspects. `fullRGB` is an upright
  // (top-down) RGB image of the viewer at `fullWidth` x `fullHeight`; it is
  // downscaled into a thumbnail and uploaded to a GL texture, so this must be
  // called with a current GL context. Returns the stored snapshot.
  Snapshot &create(const std::string &name, uint32_t flags,
                   const Camera &camera, const Engine::Scene &scene,
                   const std::vector<Engine::PointLight> &pointLights,
                   const std::vector<Engine::SpotLight> &spotLights,
                   const Engine::Sun &sun, const Tools::BrushTool &brush,
                   const Tools::MeasurementTool &measure,
                   const Tools::ClipPlaneTool &clip,
                   const std::vector<unsigned char> &fullRGB, int fullWidth,
                   int fullHeight);

  // Apply a snapshot's saved aspects back onto the live globals.
  void restore(const Snapshot &snap, Camera &camera, Engine::Scene &scene,
               std::vector<Engine::PointLight> &pointLights,
               std::vector<Engine::SpotLight> &spotLights, Engine::Sun &sun,
               Tools::BrushTool &brush, Tools::MeasurementTool &measure,
               Tools::ClipPlaneTool &clip);

  std::vector<Snapshot> &snapshots() { return m_snapshots; }
  const std::vector<Snapshot> &snapshots() const { return m_snapshots; }

  // Delete a single snapshot (releases its GL texture). Requires a current GL
  // context.
  void remove(int index);
  // Delete all snapshots (releases their GL textures).
  void clear();

  // Persist every snapshot (metadata + thumbnail PNG) into the scene's
  // directory so they are saved with the scene. `sceneFile` is the .scene path
  // passed to Engine::saveScene.
  void saveToScene(const std::string &sceneFile);
  // Replace the in-memory snapshots with those stored alongside `sceneFile`
  // (the inverse of saveToScene). Recreates the thumbnail GL textures, so this
  // must be called with a current GL context. Does nothing if the scene has no
  // saved snapshots.
  void loadFromScene(const std::string &sceneFile);

private:
  SnapshotManager() = default;
  std::vector<Snapshot> m_snapshots;
  int m_captureCounter = 0; // for unique thumbnail filenames this session
};

// ---- Glue implemented in main.cpp (operate on the live application globals) ----

// Arm a deferred snapshot capture: the next clean (GUI-free) frame is read
// from the viewer and turned into a snapshot with the given name/flags.
void RequestSnapshotCapture(const std::string &name, uint32_t flags);

// Restore the snapshot at `index` onto the live scene/camera/tools.
void RestoreSnapshot(int index);

} // namespace Core
