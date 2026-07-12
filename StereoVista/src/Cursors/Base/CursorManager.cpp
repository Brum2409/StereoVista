#include "Cursors/Base/CursorManager.h"

#include "Core/Camera.h"
#include "Cursors/CursorTypes.h"
#include "Renderer/FrameSubmission.h"
#include "Renderer/OverlayDrawList.h"

#include <GLFW/glfw3.h>

#include <algorithm>
#include <cmath>

namespace Cursor {
namespace {

// Reverse-Z convention: the cleared background / far plane maps to depth ~0,
// and any real geometry writes depth > 0 (the GL code tested depth < 0.9999
// against a far=1.0 buffer). Anything at or below this is "no hit".
constexpr float kBackgroundDepthEps = 1e-4f;

// Reconstruct a world position from a scene-target pixel + a [0,1] reverse-Z
// depth using an inverse view-projection. Vulkan NDC: the framebuffer TOP
// (py = 0) maps to ndc.y = -1 because the Y-flip is baked into the projection
// (renderer::perspective), so a straight (pixel / extent) * 2 - 1 is correct
// for both axes.
glm::vec3 reconstructWorld(const glm::mat4 &invViewProj, const glm::vec2 &pixel,
                           const glm::vec2 &extent, float depth) {
  const glm::vec4 ndc((pixel.x / extent.x) * 2.0f - 1.0f,
                      (pixel.y / extent.y) * 2.0f - 1.0f, depth, 1.0f);
  const glm::vec4 world = invViewProj * ndc;
  return glm::vec3(world) / world.w;
}

} // namespace

CursorManager::CursorManager()
    : m_cursorPosition(0.0f), m_cursorPositionValid(false),
      m_cursorPositionCalculatedThisFrame(false),
      m_backgroundCursorPosition(0.0f), m_hasBackgroundCursorPosition(false),
      m_showOrbitCenter(false), m_alwaysShowOrbitCenter(false),
      m_orbitCenterColor(0.0f, 1.0f, 0.0f, 0.7f), m_orbitCenterSphereRadius(0.2f),
      m_viewportW(1920.0f), m_viewportH(1080.0f), m_lastX(0.0f), m_lastY(0.0f),
      m_cursorInsideWindow(true), m_positionLocked(false) {
  m_sphereCursor = std::make_unique<SphereCursor>();
  m_fragmentCursor = std::make_unique<FragmentCursor>();
  m_planeCursor = std::make_unique<PlaneCursor>();
}

CursorManager::~CursorManager() = default;

void CursorManager::initialize() {
  m_sphereCursor->initialize();
  m_fragmentCursor->initialize();
  m_planeCursor->initialize();
}

void CursorManager::updateCursorPosition(GLFWwindow *hostWindow,
                                         const glm::vec2 &mousePx,
                                         const glm::vec2 &viewportPx,
                                         bool mouseInViewport,
                                         const glm::mat4 &projection,
                                         const glm::mat4 &view,
                                         const Camera &camera,
                                         const renderer::DepthReadback &depth,
                                         bool forceRecalculate) {
  // Already resolved this frame (unless a caller forces a recompute).
  if (m_cursorPositionCalculatedThisFrame && !forceRecalculate)
    return;

  // The GUI owns the mouse (hovering a panel / outside the docked viewport
  // image): show the OS cursor, keep the last cursor state.
  if (!mouseInViewport) {
    glfwSetInputMode(hostWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    return;
  }
  if (!m_cursorInsideWindow)
    return;

  // Position locked (e.g. tweaking a cursor parameter): keep it put.
  if (m_positionLocked) {
    m_cursorPositionCalculatedThisFrame = true;
    return;
  }

  // Orbiting: the shared cursor stays at the captured world point; just keep
  // the per-type cursors and the sphere radius in sync with it.
  if (camera.IsOrbiting) {
    if (m_cursorPositionValid) {
      m_sphereCursor->setPosition(m_cursorPosition);
      m_sphereCursor->setPositionValid(true);
      m_fragmentCursor->setPosition(m_cursorPosition);
      m_fragmentCursor->setPositionValid(true);
      m_planeCursor->setPosition(m_cursorPosition);
      m_planeCursor->setPositionValid(true);
      m_sphereCursor->calculateRadius(camera.Position);
    }
    m_cursorPositionCalculatedThisFrame = true;
    return;
  }

  if (camera.IsAnimating)
    return;

  // Current mouse position (viewport-local render-target pixels). The actual
  // geometry pick uses the renderer's depth readback rect (queued last frame);
  // the current mouse is used only for the bounds test, the background
  // fallback ray, and the last-depth cache.
  m_lastX = mousePx.x;
  m_lastY = mousePx.y;
  m_viewportW = std::max(viewportPx.x, 1.0f);
  m_viewportH = std::max(viewportPx.y, 1.0f);

  // Outside the render target (the image edge during a resize): invalidate.
  if (m_lastX < 0.0f || m_lastX >= m_viewportW || m_lastY < 0.0f ||
      m_lastY >= m_viewportH) {
    m_cursorPositionValid = false;
    m_sphereCursor->setPositionValid(false);
    m_fragmentCursor->setPositionValid(false);
    m_planeCursor->setPositionValid(false);
    m_cursorPositionCalculatedThisFrame = true;
    return;
  }

  const bool anyCursorVisible = m_sphereCursor->isVisible() ||
                                m_fragmentCursor->isVisible() ||
                                m_planeCursor->isVisible();

  // NDC of the current mouse within the viewport (Vulkan convention:
  // top -> -1), reused by the cache re-projection and background fallback.
  const float ndcX = (m_lastX / m_viewportW) * 2.0f - 1.0f;
  const float ndcY = (m_lastY / m_viewportH) * 2.0f - 1.0f;
  const glm::mat4 currentInvVP = glm::inverse(projection * view);

  // --- Geometry pick from the renderer's depth readback (one frame stale) ---
  // The renderer copied a small rect at last frame's mouse pixel and published
  // it with the invViewProj/extent of THAT frame; reconstructing with those
  // (not the live camera) keeps the picked point from smearing under motion.
  bool isHit = false;
  glm::vec3 worldPos(0.0f);
  if (depth.valid && !depth.rects.empty()) {
    const renderer::DepthQueryRect &rect = depth.rects[0];
    const glm::ivec2 centerPixel = rect.origin + rect.size / 2;
    float sampled = 0.0f;
    if (depth.sample(centerPixel, sampled) && sampled > kBackgroundDepthEps) {
      worldPos = reconstructWorld(
          depth.invViewProj, glm::vec2(centerPixel) + 0.5f,
          glm::vec2(static_cast<float>(depth.extent.width),
                    static_cast<float>(depth.extent.height)),
          sampled);
      isHit = true;
      // Remember this hit so the cursor can stay at this depth while the mouse
      // skims over the background (sparse point clouds would otherwise flicker
      // the cursor between 2D and 3D).
      m_lastValidDepth = sampled;
      m_hasLastValidDepth = true;
      m_lastHitTime = glfwGetTime();
      m_lastHitScreenX = m_lastX;
      m_lastHitScreenY = m_lastY;
    }
  }

  // --- Over background: optionally keep following at the last valid depth ---
  if (!isHit && m_keepLastDepthOnBackground && m_hasLastValidDepth &&
      anyCursorVisible) {
    bool cacheValid = true;
    if (m_backgroundCacheMode == CURSOR_CACHE_TIMED) {
      cacheValid = (glfwGetTime() - m_lastHitTime) <= m_backgroundCacheTime;
    } else if (m_backgroundCacheMode == CURSOR_CACHE_DISTANCE) {
      const float dx = m_lastX - m_lastHitScreenX;
      const float dy = m_lastY - m_lastHitScreenY;
      cacheValid =
          std::sqrt(dx * dx + dy * dy) <= m_backgroundCacheDistance;
    }
    if (cacheValid) {
      // Re-project the CURRENT mouse at the cached depth with the CURRENT
      // camera, so the cursor keeps tracking the mouse at that distance until
      // it hits geometry again (or the cache expires).
      const glm::vec4 h = currentInvVP * glm::vec4(ndcX, ndcY, m_lastValidDepth, 1.0f);
      worldPos = glm::vec3(h) / h.w;
      isHit = true;
    }
  }

  if (isHit && anyCursorVisible) {
    m_cursorPositionValid = true;
    m_cursorPosition = worldPos;

    m_sphereCursor->setPosition(m_cursorPosition);
    m_sphereCursor->setPositionValid(true);
    m_fragmentCursor->setPosition(m_cursorPosition);
    m_fragmentCursor->setPositionValid(true);
    m_planeCursor->setPosition(m_cursorPosition);
    m_planeCursor->setPositionValid(true);
    m_sphereCursor->calculateRadius(camera.Position);

    m_hasBackgroundCursorPosition = false;

    // Panning keeps its own (disabled) cursor mode; right-button free-look
    // leaves the OS cursor visible. Otherwise hide the OS cursor so only the
    // 3D cursor shows over geometry.
    if (camera.IsPanning) {
      m_cursorPositionCalculatedThisFrame = true;
      return;
    }
    glfwSetInputMode(hostWindow, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
  } else {
    m_cursorPositionValid = false;
    m_sphereCursor->setPositionValid(false);
    m_fragmentCursor->setPositionValid(false);
    m_planeCursor->setPositionValid(false);

    // Background cursor (a point along the mouse ray) for zoom/orbit fallback.
    m_backgroundCursorPosition = calculateBackgroundCursorPosition(projection, view);
    m_hasBackgroundCursorPosition = true;

    if (camera.IsPanning) {
      m_cursorPositionCalculatedThisFrame = true;
      return;
    }
    glfwSetInputMode(hostWindow, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }

  m_cursorPositionCalculatedThisFrame = true;
}

void CursorManager::resetFrameCalculationFlag() {
  m_cursorPositionCalculatedThisFrame = false;
}

void CursorManager::setForcedCursorPosition(const glm::vec3 &position,
                                            const glm::vec3 &cameraPosition) {
  m_cursorPosition = position;
  m_cursorPositionValid = true;

  m_sphereCursor->setPosition(position);
  m_sphereCursor->setPositionValid(true);
  m_sphereCursor->calculateRadius(cameraPosition);
  m_fragmentCursor->setPosition(position);
  m_fragmentCursor->setPositionValid(true);
  m_planeCursor->setPosition(position);
  m_planeCursor->setPositionValid(true);

  // We have an authoritative 3D point this frame; drop any stale background
  // fallback so it can't override the forced position downstream.
  m_hasBackgroundCursorPosition = false;
}

void CursorManager::renderCursors(renderer::OverlayDrawList &list,
                                  const Camera &camera) {
  if (!m_cursorInsideWindow)
    return;
  if (m_sphereCursor->isVisible())
    m_sphereCursor->appendTo(list, camera.Position);
  if (m_planeCursor->isVisible())
    m_planeCursor->appendTo(list, camera.Position);
  // The fragment (ring) cursor is drawn by mesh.frag via FrameSubmission, not
  // the overlay list — see fillFragmentCursorState.
}

void CursorManager::fillFragmentCursorState(renderer::FragmentCursorState &out,
                                            const Camera &camera) const {
  m_fragmentCursor->fillState(out);
  // Always valid while orbiting (matches the GL uber-shader path, which kept
  // the ring pinned to the captured point during an orbit).
  if (camera.IsOrbiting) {
    out.position = m_cursorPosition;
    out.valid = true;
  }
  out.show = m_fragmentCursor->isVisible() && out.valid;
}

void CursorManager::renderOrbitCenter(renderer::OverlayDrawList &list,
                                      const glm::vec3 &orbitPoint) {
  if (!m_showOrbitCenter)
    return;
  m_sphereCursor->appendOrbitSphere(list, orbitPoint, m_orbitCenterSphereRadius,
                                    m_orbitCenterColor);
}

glm::vec3
CursorManager::calculateBackgroundCursorPosition(const glm::mat4 &projection,
                                                 const glm::mat4 &view) {
  // NDC (Vulkan convention: top -> -1; the projection carries the Y-flip).
  const float x = (m_lastX / m_viewportW) * 2.0f - 1.0f;
  const float y = (m_lastY / m_viewportH) * 2.0f - 1.0f;

  // Reverse-Z: near plane -> 1, far plane -> 0. Mix the two ray hits to land a
  // point mid-frustum along the mouse ray.
  const glm::mat4 invViewProj = glm::inverse(projection * view);
  glm::vec4 nearWorld = invViewProj * glm::vec4(x, y, 1.0f, 1.0f);
  glm::vec4 farWorld = invViewProj * glm::vec4(x, y, 0.0f, 1.0f);
  nearWorld /= nearWorld.w;
  farWorld /= farWorld.w;

  return glm::mix(glm::vec3(nearWorld), glm::vec3(farWorld), 0.5f);
}

} // namespace Cursor
