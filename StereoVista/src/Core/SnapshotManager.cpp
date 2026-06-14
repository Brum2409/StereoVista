#include "Core/SnapshotManager.h"

#include "Engine/Screenshot.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>

#include <json.h>
#include <stb_image.h>

using json = nlohmann::json;
namespace fs = std::filesystem;

namespace Core {

namespace {

// Build a human-readable "YYYY-MM-DD HH:MM:SS" timestamp for display.
std::string nowTimestamp() {
  std::time_t t = std::time(nullptr);
  std::tm tmBuf{};
#if defined(_WIN32)
  localtime_s(&tmBuf, &t);
#else
  localtime_r(&t, &tmBuf);
#endif
  std::ostringstream ss;
  ss << std::put_time(&tmBuf, "%Y-%m-%d %H:%M:%S");
  return ss.str();
}

// The per-session folder where freshly captured thumbnails are written. Created
// once on first use and reused for the lifetime of the process.
const fs::path &sessionThumbDir() {
  static fs::path dir = [] {
    std::time_t t = std::time(nullptr);
    fs::path d = fs::temp_directory_path() /
                 ("StereoVista_snapshots_" + std::to_string(static_cast<long long>(t)));
    std::error_code ec;
    fs::create_directories(d, ec);
    return d;
  }();
  return dir;
}

// Resolve the scene's asset directory the same way Engine::saveScene /
// loadScene do: "<parent>/<stem>" of the .scene path.
fs::path sceneDirOf(const std::string &sceneFile) {
  fs::path p(sceneFile);
  if (p.extension() != ".scene")
    p.replace_extension(".scene");
  return p.parent_path() / p.stem();
}

// ---- JSON helpers -------------------------------------------------------

json vec3ToJson(const glm::vec3 &v) { return json::array({v.x, v.y, v.z}); }

glm::vec3 jsonToVec3(const json &j, const glm::vec3 &def) {
  if (j.is_array() && j.size() == 3)
    return glm::vec3(j[0].get<float>(), j[1].get<float>(), j[2].get<float>());
  return def;
}

json modelStateToJson(const Engine::Undo::ModelEditState &m) {
  json j;
  j["position"] = vec3ToJson(m.position);
  j["rotation"] = vec3ToJson(m.rotation);
  j["scale"] = vec3ToJson(m.scale);
  j["visible"] = m.visible;
  j["color"] = vec3ToJson(m.color);
  j["shininess"] = m.shininess;
  j["emissive"] = m.emissive;
  j["metallicFactor"] = m.metallicFactor;
  j["roughnessFactor"] = m.roughnessFactor;
  j["F0"] = vec3ToJson(m.F0);
  j["normalScale"] = m.normalScale;
  j["heightScale"] = m.heightScale;
  j["diffuseReflectivity"] = m.diffuseReflectivity;
  j["specularColor"] = vec3ToJson(m.specularColor);
  j["specularDiffusion"] = m.specularDiffusion;
  j["specularReflectivity"] = m.specularReflectivity;
  j["refractiveIndex"] = m.refractiveIndex;
  j["transparency"] = m.transparency;
  j["materialType"] = static_cast<int>(m.materialType);
  json meshes = json::array();
  for (const auto &mm : m.meshMaterials) {
    json mj;
    mj["visible"] = mm.visible;
    mj["color"] = vec3ToJson(mm.color);
    mj["shininess"] = mm.shininess;
    mj["emissive"] = mm.emissive;
    meshes.push_back(mj);
  }
  j["meshMaterials"] = meshes;
  return j;
}

Engine::Undo::ModelEditState modelStateFromJson(const json &j) {
  Engine::Undo::ModelEditState m;
  m.position = jsonToVec3(j.value("position", json()), m.position);
  m.rotation = jsonToVec3(j.value("rotation", json()), m.rotation);
  m.scale = jsonToVec3(j.value("scale", json()), m.scale);
  m.visible = j.value("visible", m.visible);
  m.color = jsonToVec3(j.value("color", json()), m.color);
  m.shininess = j.value("shininess", m.shininess);
  m.emissive = j.value("emissive", m.emissive);
  m.metallicFactor = j.value("metallicFactor", m.metallicFactor);
  m.roughnessFactor = j.value("roughnessFactor", m.roughnessFactor);
  m.F0 = jsonToVec3(j.value("F0", json()), m.F0);
  m.normalScale = j.value("normalScale", m.normalScale);
  m.heightScale = j.value("heightScale", m.heightScale);
  m.diffuseReflectivity = j.value("diffuseReflectivity", m.diffuseReflectivity);
  m.specularColor = jsonToVec3(j.value("specularColor", json()), m.specularColor);
  m.specularDiffusion = j.value("specularDiffusion", m.specularDiffusion);
  m.specularReflectivity = j.value("specularReflectivity", m.specularReflectivity);
  m.refractiveIndex = j.value("refractiveIndex", m.refractiveIndex);
  m.transparency = j.value("transparency", m.transparency);
  m.materialType =
      static_cast<decltype(m.materialType)>(j.value("materialType", 0));
  if (j.contains("meshMaterials") && j["meshMaterials"].is_array()) {
    for (const auto &mj : j["meshMaterials"]) {
      Engine::Undo::MeshMaterialState mm;
      mm.visible = mj.value("visible", mm.visible);
      mm.color = jsonToVec3(mj.value("color", json()), mm.color);
      mm.shininess = mj.value("shininess", mm.shininess);
      mm.emissive = mj.value("emissive", mm.emissive);
      m.meshMaterials.push_back(mm);
    }
  }
  return m;
}

json cloudStateToJson(const Engine::Undo::PointCloudEditState &p) {
  json j;
  j["position"] = vec3ToJson(p.position);
  j["rotation"] = vec3ToJson(p.rotation);
  j["scale"] = vec3ToJson(p.scale);
  j["visible"] = p.visible;
  j["basePointSize"] = p.basePointSize;
  return j;
}

Engine::Undo::PointCloudEditState cloudStateFromJson(const json &j) {
  Engine::Undo::PointCloudEditState p;
  p.position = jsonToVec3(j.value("position", json()), p.position);
  p.rotation = jsonToVec3(j.value("rotation", json()), p.rotation);
  p.scale = jsonToVec3(j.value("scale", json()), p.scale);
  p.visible = j.value("visible", p.visible);
  p.basePointSize = j.value("basePointSize", p.basePointSize);
  return p;
}

json sceneStateToJson(const SnapshotSceneState &s) {
  json j;
  json models = json::array();
  for (const auto &m : s.models)
    models.push_back(modelStateToJson(m));
  j["models"] = models;

  json clouds = json::array();
  for (const auto &p : s.pointClouds)
    clouds.push_back(cloudStateToJson(p));
  j["pointClouds"] = clouds;

  json plights = json::array();
  for (const auto &l : s.pointLights) {
    json lj;
    lj["position"] = vec3ToJson(l.position);
    lj["color"] = vec3ToJson(l.color);
    lj["intensity"] = l.intensity;
    lj["linear"] = l.linear;
    lj["quadratic"] = l.quadratic;
    lj["castShadows"] = l.castShadows;
    plights.push_back(lj);
  }
  j["pointLights"] = plights;

  json slights = json::array();
  for (const auto &l : s.spotLights) {
    json lj;
    lj["position"] = vec3ToJson(l.position);
    lj["direction"] = vec3ToJson(l.direction);
    lj["color"] = vec3ToJson(l.color);
    lj["intensity"] = l.intensity;
    lj["innerCutOff"] = l.innerCutOff;
    lj["outerCutOff"] = l.outerCutOff;
    lj["castShadows"] = l.castShadows;
    slights.push_back(lj);
  }
  j["spotLights"] = slights;

  json meas = json::array();
  for (const auto &m : s.measurements) {
    json mj;
    mj["type"] = static_cast<int>(m.type);
    mj["name"] = m.name;
    mj["color"] = vec3ToJson(m.color);
    mj["visible"] = m.visible;
    json pts = json::array();
    for (const auto &p : m.points)
      pts.push_back(vec3ToJson(p));
    mj["points"] = pts;
    meas.push_back(mj);
  }
  j["measurements"] = meas;

  json planes = json::array();
  for (const auto &c : s.clipPlanes) {
    json cj;
    cj["name"] = c.name;
    cj["position"] = vec3ToJson(c.position);
    cj["normal"] = vec3ToJson(c.normal);
    cj["color"] = vec3ToJson(c.color);
    cj["enabled"] = c.enabled;
    planes.push_back(cj);
  }
  j["clipPlanes"] = planes;

  json sun;
  sun["direction"] = vec3ToJson(s.sun.direction);
  sun["color"] = vec3ToJson(s.sun.color);
  sun["intensity"] = s.sun.intensity;
  sun["enabled"] = s.sun.enabled;
  j["sun"] = sun;

  return j;
}

SnapshotSceneState sceneStateFromJson(const json &j) {
  SnapshotSceneState s;
  if (j.contains("models"))
    for (const auto &m : j["models"])
      s.models.push_back(modelStateFromJson(m));
  if (j.contains("pointClouds"))
    for (const auto &p : j["pointClouds"])
      s.pointClouds.push_back(cloudStateFromJson(p));
  if (j.contains("pointLights")) {
    for (const auto &lj : j["pointLights"]) {
      Engine::PointLight l;
      l.position = jsonToVec3(lj.value("position", json()), glm::vec3(0.0f));
      l.color = jsonToVec3(lj.value("color", json()), glm::vec3(1.0f));
      l.intensity = lj.value("intensity", 1.0f);
      l.linear = lj.value("linear", l.linear);
      l.quadratic = lj.value("quadratic", l.quadratic);
      l.castShadows = lj.value("castShadows", l.castShadows);
      s.pointLights.push_back(l);
    }
  }
  if (j.contains("spotLights")) {
    for (const auto &lj : j["spotLights"]) {
      Engine::SpotLight l;
      l.position = jsonToVec3(lj.value("position", json()), glm::vec3(0.0f));
      l.direction = jsonToVec3(lj.value("direction", json()), glm::vec3(0, -1, 0));
      l.color = jsonToVec3(lj.value("color", json()), glm::vec3(1.0f));
      l.intensity = lj.value("intensity", 1.0f);
      l.innerCutOff = lj.value("innerCutOff", 0.9f);
      l.outerCutOff = lj.value("outerCutOff", 0.82f);
      l.castShadows = lj.value("castShadows", true);
      s.spotLights.push_back(l);
    }
  }
  if (j.contains("measurements")) {
    for (const auto &mj : j["measurements"]) {
      Engine::Measurement m;
      m.type = static_cast<Engine::Measurement::Type>(mj.value("type", 0));
      m.name = mj.value("name", std::string("Measurement"));
      m.color = jsonToVec3(mj.value("color", json()), m.color);
      m.visible = mj.value("visible", true);
      if (mj.contains("points"))
        for (const auto &p : mj["points"])
          m.points.push_back(jsonToVec3(p, glm::vec3(0.0f)));
      s.measurements.push_back(m);
    }
  }
  if (j.contains("clipPlanes")) {
    for (const auto &cj : j["clipPlanes"]) {
      Engine::ClipPlane c;
      c.name = cj.value("name", std::string("Plane"));
      c.position = jsonToVec3(cj.value("position", json()), c.position);
      c.normal = jsonToVec3(cj.value("normal", json()), c.normal);
      c.color = jsonToVec3(cj.value("color", json()), c.color);
      c.enabled = cj.value("enabled", true);
      s.clipPlanes.push_back(c);
    }
  }
  if (j.contains("sun")) {
    const auto &sj = j["sun"];
    s.sun.direction = jsonToVec3(sj.value("direction", json()), glm::vec3(0, -1, 0));
    s.sun.color = jsonToVec3(sj.value("color", json()), glm::vec3(1.0f));
    s.sun.intensity = sj.value("intensity", 1.0f);
    s.sun.enabled = sj.value("enabled", true);
  }
  return s;
}

json toolStateToJson(const SnapshotToolState &t) {
  json j;
  j["brushEnabled"] = t.brushEnabled;
  j["brushRadius"] = t.brushRadius;
  j["brushDensity"] = t.brushDensity;
  j["brushMinSpacing"] = t.brushMinSpacing;
  j["brushSelectedModel"] = t.brushSelectedModel;
  j["measureEnabled"] = t.measureEnabled;
  j["measureMode"] = t.measureMode;
  j["measureLineWidth"] = t.measureLineWidth;
  j["measureShowLabels"] = t.measureShowLabels;
  j["measureShowSegmentLabels"] = t.measureShowSegmentLabels;
  j["measureXRay"] = t.measureXRay;
  j["measureUnitScale"] = t.measureUnitScale;
  j["measureUnitSuffix"] = t.measureUnitSuffix;
  j["measureNextColor"] = vec3ToJson(t.measureNextColor);
  j["clipEnabled"] = t.clipEnabled;
  j["clipDisplaySize"] = t.clipDisplaySize;
  j["clipNudgeStep"] = t.clipNudgeStep;
  j["clipActiveIndex"] = t.clipActiveIndex;
  return j;
}

SnapshotToolState toolStateFromJson(const json &j) {
  SnapshotToolState t;
  t.brushEnabled = j.value("brushEnabled", t.brushEnabled);
  t.brushRadius = j.value("brushRadius", t.brushRadius);
  t.brushDensity = j.value("brushDensity", t.brushDensity);
  t.brushMinSpacing = j.value("brushMinSpacing", t.brushMinSpacing);
  t.brushSelectedModel = j.value("brushSelectedModel", t.brushSelectedModel);
  t.measureEnabled = j.value("measureEnabled", t.measureEnabled);
  t.measureMode = j.value("measureMode", t.measureMode);
  t.measureLineWidth = j.value("measureLineWidth", t.measureLineWidth);
  t.measureShowLabels = j.value("measureShowLabels", t.measureShowLabels);
  t.measureShowSegmentLabels =
      j.value("measureShowSegmentLabels", t.measureShowSegmentLabels);
  t.measureXRay = j.value("measureXRay", t.measureXRay);
  t.measureUnitScale = j.value("measureUnitScale", t.measureUnitScale);
  t.measureUnitSuffix = j.value("measureUnitSuffix", t.measureUnitSuffix);
  t.measureNextColor = jsonToVec3(j.value("measureNextColor", json()), t.measureNextColor);
  t.clipEnabled = j.value("clipEnabled", t.clipEnabled);
  t.clipDisplaySize = j.value("clipDisplaySize", t.clipDisplaySize);
  t.clipNudgeStep = j.value("clipNudgeStep", t.clipNudgeStep);
  t.clipActiveIndex = j.value("clipActiveIndex", t.clipActiveIndex);
  return t;
}

json cameraStateToJson(const Camera::CameraState &c) {
  json j;
  j["position"] = vec3ToJson(c.position);
  j["front"] = vec3ToJson(c.front);
  j["up"] = vec3ToJson(c.up);
  j["yaw"] = c.yaw;
  j["pitch"] = c.pitch;
  j["zoom"] = c.zoom;
  j["orientation"] =
      json::array({c.orientation.x, c.orientation.y, c.orientation.z, c.orientation.w});
  return j;
}

Camera::CameraState cameraStateFromJson(const json &j) {
  Camera::CameraState c{};
  c.position = jsonToVec3(j.value("position", json()), glm::vec3(0.0f));
  c.front = jsonToVec3(j.value("front", json()), glm::vec3(0, 0, -1));
  c.up = jsonToVec3(j.value("up", json()), glm::vec3(0, 1, 0));
  c.yaw = j.value("yaw", -90.0f);
  c.pitch = j.value("pitch", 0.0f);
  c.zoom = j.value("zoom", 45.0f);
  if (j.contains("orientation") && j["orientation"].is_array() &&
      j["orientation"].size() == 4) {
    const auto &o = j["orientation"];
    c.orientation = glm::quat(o[3].get<float>(), o[0].get<float>(),
                              o[1].get<float>(), o[2].get<float>());
  } else {
    c.orientation = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
  }
  return c;
}

// ---- Thumbnail pixel helpers --------------------------------------------

// Nearest-neighbour downscale of an upright (top-down) RGB image so its
// largest side is at most `maxDim`. Keeps aspect ratio; never upscales.
void downscaleRGB(const std::vector<unsigned char> &src, int srcW, int srcH,
                  int maxDim, std::vector<unsigned char> &dst, int &dstW,
                  int &dstH) {
  if (srcW <= 0 || srcH <= 0 ||
      src.size() < static_cast<size_t>(srcW) * srcH * 3) {
    dst.clear();
    dstW = dstH = 0;
    return;
  }

  float scale = 1.0f;
  int largest = std::max(srcW, srcH);
  if (largest > maxDim)
    scale = static_cast<float>(maxDim) / static_cast<float>(largest);

  dstW = std::max(1, static_cast<int>(srcW * scale));
  dstH = std::max(1, static_cast<int>(srcH * scale));

  dst.assign(static_cast<size_t>(dstW) * dstH * 3, 0);
  for (int y = 0; y < dstH; ++y) {
    int sy = std::min(srcH - 1, static_cast<int>(y / scale));
    for (int x = 0; x < dstW; ++x) {
      int sx = std::min(srcW - 1, static_cast<int>(x / scale));
      const unsigned char *s = &src[(static_cast<size_t>(sy) * srcW + sx) * 3];
      unsigned char *d = &dst[(static_cast<size_t>(y) * dstW + x) * 3];
      d[0] = s[0];
      d[1] = s[1];
      d[2] = s[2];
    }
  }
}

// Upload an upright (top-down) RGB image to a freshly created GL texture.
// Returns 0 on failure. With top-down data and ImGui's default UVs the image
// displays upright.
GLuint uploadThumbnail(const unsigned char *rgb, int w, int h) {
  if (!rgb || w <= 0 || h <= 0)
    return 0;

  GLuint tex = 0;
  glGenTextures(1, &tex);
  if (tex == 0)
    return 0;

  GLint prevAlign = 4;
  glGetIntegerv(GL_UNPACK_ALIGNMENT, &prevAlign);
  GLint prevTex = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);

  glBindTexture(GL_TEXTURE_2D, tex);
  glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE, rgb);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
  return tex;
}

// Load a PNG from disk and upload it as a thumbnail texture. Fills outW/outH.
GLuint loadThumbnailTexture(const std::string &path, int &outW, int &outH) {
  outW = outH = 0;
  if (path.empty())
    return 0;
  int w = 0, h = 0, n = 0;
  // Force 3 channels; stb_image returns rows top-to-bottom (upright).
  unsigned char *data = stbi_load(path.c_str(), &w, &h, &n, 3);
  if (!data)
    return 0;
  GLuint tex = uploadThumbnail(data, w, h);
  stbi_image_free(data);
  outW = w;
  outH = h;
  return tex;
}

} // namespace

SnapshotManager &SnapshotManager::instance() {
  static SnapshotManager s;
  return s;
}

Snapshot &SnapshotManager::create(
    const std::string &name, uint32_t flags, const Camera &camera,
    const Engine::Scene &scene,
    const std::vector<Engine::PointLight> &pointLights,
    const std::vector<Engine::SpotLight> &spotLights, const Engine::Sun &sun,
    const Tools::BrushTool &brush, const Tools::MeasurementTool &measure,
    const Tools::ClipPlaneTool &clip, const std::vector<unsigned char> &fullRGB,
    int fullWidth, int fullHeight) {
  Snapshot snap;
  snap.name = name.empty() ? "Snapshot" : name;
  snap.timestamp = nowTimestamp();
  snap.flags = flags;

  // Camera ----------------------------------------------------------------
  if (flags & SNAPSHOT_CAMERA)
    snap.camera = camera.GetState();

  // Scene -----------------------------------------------------------------
  if (flags & SNAPSHOT_SCENE) {
    snap.scene.models.reserve(scene.models.size());
    for (const auto &m : scene.models)
      snap.scene.models.push_back(Engine::Undo::ModelEditState::capture(m));

    snap.scene.pointClouds.reserve(scene.pointClouds.size());
    for (const auto &pc : scene.pointClouds)
      snap.scene.pointClouds.push_back(
          Engine::Undo::PointCloudEditState::capture(pc));

    snap.scene.pointLights = pointLights;
    snap.scene.spotLights = spotLights;
    snap.scene.measurements = scene.measurements;
    snap.scene.clipPlanes = scene.clipPlanes;
    snap.scene.sun = sun;
  }

  // Tools -----------------------------------------------------------------
  if (flags & SNAPSHOT_TOOLS) {
    SnapshotToolState &t = snap.tools;
    t.brushEnabled = brush.isEnabled();
    t.brushRadius = brush.getBrushRadius();
    t.brushDensity = brush.getDensity();
    t.brushMinSpacing = brush.getMinSpacing();
    t.brushSelectedModel = brush.getSelectedModel();

    t.measureEnabled = measure.isEnabled();
    t.measureMode = static_cast<int>(measure.getMode());
    t.measureLineWidth = measure.lineWidth;
    t.measureShowLabels = measure.showLabels;
    t.measureShowSegmentLabels = measure.showSegmentLabels;
    t.measureXRay = measure.xRay;
    t.measureUnitScale = measure.unitScale;
    t.measureUnitSuffix = measure.unitSuffix;
    t.measureNextColor = measure.nextColor;

    t.clipEnabled = clip.isEnabled();
    t.clipDisplaySize = clip.displaySize;
    t.clipNudgeStep = clip.nudgeStep;
    t.clipActiveIndex = clip.activeIndex();
  }

  // Thumbnail (always captured): downscale, write to the session temp folder
  // as PNG, then upload a GL texture for display. The full pixels are not
  // retained in memory.
  std::vector<unsigned char> thumb;
  downscaleRGB(fullRGB, fullWidth, fullHeight, 320, thumb, snap.thumbWidth,
               snap.thumbHeight);
  if (!thumb.empty()) {
    std::ostringstream fn;
    fn << "snapshot_" << m_captureCounter++ << "_" << std::time(nullptr)
       << ".png";
    fs::path path = sessionThumbDir() / fn.str();
    if (Engine::Screenshot::writePNG(path.string(), snap.thumbWidth,
                                     snap.thumbHeight, 3, thumb.data())) {
      snap.thumbnailPath = path.string();
    }
    snap.thumbnailTexture =
        uploadThumbnail(thumb.data(), snap.thumbWidth, snap.thumbHeight);
  }

  m_snapshots.push_back(std::move(snap));
  return m_snapshots.back();
}

void SnapshotManager::restore(const Snapshot &snap, Camera &camera,
                              Engine::Scene &scene,
                              std::vector<Engine::PointLight> &pointLights,
                              std::vector<Engine::SpotLight> &spotLights,
                              Engine::Sun &sun, Tools::BrushTool &brush,
                              Tools::MeasurementTool &measure,
                              Tools::ClipPlaneTool &clip) {
  // Camera ----------------------------------------------------------------
  // Glide the view into place (position, orientation and zoom all eased)
  // instead of snapping, so restoring a camera reads as a smooth move.
  if (snap.flags & SNAPSHOT_CAMERA)
    camera.StartStateAnimation(snap.camera);

  // Scene -----------------------------------------------------------------
  if (snap.flags & SNAPSHOT_SCENE) {
    // Re-apply object transforms / materials by index. Objects added or
    // removed since the snapshot are left untouched (no GPU reload here).
    size_t nModels = std::min(snap.scene.models.size(), scene.models.size());
    for (size_t i = 0; i < nModels; ++i)
      snap.scene.models[i].apply(scene.models[i]);

    size_t nClouds =
        std::min(snap.scene.pointClouds.size(), scene.pointClouds.size());
    for (size_t i = 0; i < nClouds; ++i)
      snap.scene.pointClouds[i].apply(scene.pointClouds[i]);

    // Lights / sun / annotations are cheap to copy, so restore them fully.
    pointLights = snap.scene.pointLights;
    spotLights = snap.scene.spotLights;
    scene.pointLights = snap.scene.pointLights;
    scene.spotLights = snap.scene.spotLights;
    // Assigning the vectors' contents keeps the same vector objects, so the
    // tool pointers bound to scene.measurements / scene.clipPlanes stay valid.
    scene.measurements = snap.scene.measurements;
    scene.clipPlanes = snap.scene.clipPlanes;
    sun = snap.scene.sun;

    // Re-clamp the clip tool's active selection to the restored plane count.
    clip.setActiveIndex(clip.activeIndex());
  }

  // Tools -----------------------------------------------------------------
  if (snap.flags & SNAPSHOT_TOOLS) {
    const SnapshotToolState &t = snap.tools;

    brush.setBrushRadius(t.brushRadius);
    brush.setDensity(t.brushDensity);
    brush.setMinSpacing(t.brushMinSpacing);
    brush.setSelectedModel(t.brushSelectedModel);
    if (t.brushEnabled)
      brush.enable();
    else
      brush.disable();

    measure.setEnabled(t.measureEnabled);
    measure.setMode(static_cast<Engine::Measurement::Type>(t.measureMode));
    measure.lineWidth = t.measureLineWidth;
    measure.showLabels = t.measureShowLabels;
    measure.showSegmentLabels = t.measureShowSegmentLabels;
    measure.xRay = t.measureXRay;
    measure.unitScale = t.measureUnitScale;
    measure.unitSuffix = t.measureUnitSuffix;
    measure.nextColor = t.measureNextColor;

    clip.setEnabled(t.clipEnabled);
    clip.displaySize = t.clipDisplaySize;
    clip.nudgeStep = t.clipNudgeStep;
    clip.setActiveIndex(t.clipActiveIndex);
  }
}

void SnapshotManager::remove(int index) {
  if (index < 0 || index >= static_cast<int>(m_snapshots.size()))
    return;
  if (m_snapshots[index].thumbnailTexture != 0)
    glDeleteTextures(1, &m_snapshots[index].thumbnailTexture);
  m_snapshots.erase(m_snapshots.begin() + index);
}

void SnapshotManager::clear() {
  for (auto &snap : m_snapshots) {
    if (snap.thumbnailTexture != 0)
      glDeleteTextures(1, &snap.thumbnailTexture);
  }
  m_snapshots.clear();
}

void SnapshotManager::saveToScene(const std::string &sceneFile) {
  try {
    fs::path dir = sceneDirOf(sceneFile);
    fs::path thumbDir = dir / "snapshots";
    std::error_code ec;
    fs::create_directories(thumbDir, ec);

    json root;
    root["snapshots"] = json::array();
    for (size_t i = 0; i < m_snapshots.size(); ++i) {
      const Snapshot &s = m_snapshots[i];
      json j;
      j["name"] = s.name;
      j["timestamp"] = s.timestamp;
      j["flags"] = s.flags;
      if (!s.tags.empty())
        j["tags"] = s.tags;
      if (s.hasColor)
        j["color"] = vec3ToJson(s.color);
      if (s.flags & SNAPSHOT_CAMERA)
        j["camera"] = cameraStateToJson(s.camera);
      if (s.flags & SNAPSHOT_SCENE)
        j["scene"] = sceneStateToJson(s.scene);
      if (s.flags & SNAPSHOT_TOOLS)
        j["tools"] = toolStateToJson(s.tools);

      // Copy the thumbnail PNG into the scene directory, preserving each
      // snapshot's own (unique) filename so reordering/deletion can't make one
      // copy clobber another's source.
      if (!s.thumbnailPath.empty() && fs::exists(s.thumbnailPath)) {
        std::string thumbName = fs::path(s.thumbnailPath).filename().string();
        fs::path dst = thumbDir / thumbName;
        try {
          std::error_code cmpEc;
          bool sameFile = fs::exists(dst) &&
                          fs::equivalent(s.thumbnailPath, dst, cmpEc) && !cmpEc;
          if (!sameFile)
            fs::copy_file(s.thumbnailPath, dst,
                          fs::copy_options::overwrite_existing);
          j["thumbnail"] = "snapshots/" + thumbName;
        } catch (const std::exception &e) {
          std::cerr << "Snapshot thumbnail copy failed: " << e.what()
                    << std::endl;
        }
      }
      root["snapshots"].push_back(j);
    }

    std::ofstream out(dir / "snapshots.json");
    if (out.is_open())
      out << root.dump(2);
  } catch (const std::exception &e) {
    std::cerr << "Failed to save snapshots: " << e.what() << std::endl;
  }
}

void SnapshotManager::loadFromScene(const std::string &sceneFile) {
  // Always start from a clean slate so snapshots match the loaded scene.
  clear();

  try {
    fs::path dir = sceneDirOf(sceneFile);
    fs::path jsonPath = dir / "snapshots.json";
    if (!fs::exists(jsonPath))
      return;

    std::ifstream in(jsonPath);
    if (!in.is_open())
      return;
    json root;
    in >> root;
    if (!root.contains("snapshots") || !root["snapshots"].is_array())
      return;

    for (const auto &j : root["snapshots"]) {
      Snapshot s;
      s.name = j.value("name", std::string("Snapshot"));
      s.timestamp = j.value("timestamp", std::string());
      s.flags = j.value("flags", 0u);
      if (j.contains("tags") && j["tags"].is_array()) {
        for (const auto &t : j["tags"])
          if (t.is_string())
            s.tags.push_back(t.get<std::string>());
      }
      if (j.contains("color")) {
        s.color = jsonToVec3(j["color"], s.color);
        s.hasColor = true;
      }
      if (j.contains("camera"))
        s.camera = cameraStateFromJson(j["camera"]);
      if (j.contains("scene"))
        s.scene = sceneStateFromJson(j["scene"]);
      if (j.contains("tools"))
        s.tools = toolStateFromJson(j["tools"]);
      if (j.contains("thumbnail")) {
        fs::path thumb = dir / j["thumbnail"].get<std::string>();
        s.thumbnailPath = thumb.string();
        s.thumbnailTexture =
            loadThumbnailTexture(s.thumbnailPath, s.thumbWidth, s.thumbHeight);
      }
      m_snapshots.push_back(std::move(s));
    }
  } catch (const std::exception &e) {
    std::cerr << "Failed to load snapshots: " << e.what() << std::endl;
  }
}

} // namespace Core
