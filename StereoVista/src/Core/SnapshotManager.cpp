#include "Core/SnapshotManager.h"

#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

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
      const unsigned char *s =
          &src[(static_cast<size_t>(sy) * srcW + sx) * 3];
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
GLuint uploadThumbnail(const std::vector<unsigned char> &rgb, int w, int h) {
  if (w <= 0 || h <= 0 || rgb.size() < static_cast<size_t>(w) * h * 3)
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
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, w, h, 0, GL_RGB, GL_UNSIGNED_BYTE,
               rgb.data());
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

  glPixelStorei(GL_UNPACK_ALIGNMENT, prevAlign);
  glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTex));
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

  // Thumbnail (always captured) -------------------------------------------
  downscaleRGB(fullRGB, fullWidth, fullHeight, 320, snap.thumbnailRGB,
               snap.thumbWidth, snap.thumbHeight);
  snap.thumbnailTexture =
      uploadThumbnail(snap.thumbnailRGB, snap.thumbWidth, snap.thumbHeight);

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
  if (snap.flags & SNAPSHOT_CAMERA)
    camera.SetState(snap.camera);

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

} // namespace Core
