#pragma once

#include "Core/Camera.h"
#include "Cursor.h"
#include "Cursors/Types/FragmentCursor.h"
#include "Cursors/Types/PlaneCursor.h"
#include "Cursors/Types/SphereCursor.h"
#include <glm/glm.hpp>
#include <memory>

struct GLFWwindow;

namespace renderer {
class OverlayDrawList;
struct FragmentCursorState;
struct DepthReadback;
}

namespace Cursor {
// Central manager for all cursor types. Vulkan-era picking: instead of a
// synchronous glReadPixels, the manager samples the renderer's depth-picking
// readback (a small rect under the mouse, copied every frame and published
// one frame later — the GL read was equally one frame stale) and
// reconstructs the world position with the invViewProj THAT RENDERED the
// depth, so camera motion can't smear the picked point.
class CursorManager {
public:
  CursorManager();
  ~CursorManager();

  // Initialize all cursor types
  void initialize();

  // Update the shared 3D cursor from the latest depth readback. Coordinates
  // are 3D-VIEWPORT-LOCAL (the docked viewport image, or the full framebuffer
  // on the classic fullscreen path): mousePx is the mouse in render-target
  // pixels, viewportPx the render-target size (the depth readback's extent
  // space). mouseInViewport=false while the GUI owns the mouse (hovering a
  // panel / outside the viewport image) — the OS cursor is restored and the
  // pick is skipped, keeping the last cursor state. hostWindow is the OS
  // window hosting the 3D view (OS-cursor show/hide only). `depth` is
  // Renderer::depthSamples() (one frame stale — same class as the GL
  // glReadPixels path); proj/view are the CURRENT camera matrices (used only
  // for the background-plane fallback, like the GL version).
  void updateCursorPosition(GLFWwindow *hostWindow, const glm::vec2 &mousePx,
                            const glm::vec2 &viewportPx, bool mouseInViewport,
                            const glm::mat4 &projection, const glm::mat4 &view,
                            const Camera &camera,
                            const renderer::DepthReadback &depth,
                            bool forceRecalculate);

  // Reset frame calculation flag (call at start of each frame)
  void resetFrameCalculationFlag();

  // Append all visible cursors to the overlay draw list.
  void renderCursors(renderer::OverlayDrawList &list, const Camera &camera);

  // Fill the fragment-cursor state consumed by mesh.frag via FrameSubmission.
  void fillFragmentCursorState(renderer::FragmentCursorState &out,
                               const Camera &camera) const;

  // Getters for cursor instances
  SphereCursor *getSphereCursor() { return m_sphereCursor.get(); }
  FragmentCursor *getFragmentCursor() { return m_fragmentCursor.get(); }
  PlaneCursor *getPlaneCursor() { return m_planeCursor.get(); }

  // Orbit center rendering (appends to the overlay list).
  void renderOrbitCenter(renderer::OverlayDrawList &list,
                         const glm::vec3 &orbitPoint);

  // Orbit center properties
  bool isShowOrbitCenter() const { return m_showOrbitCenter; }
  void setShowOrbitCenter(bool show) { m_showOrbitCenter = show; }
  bool isAlwaysShowOrbitCenter() const { return m_alwaysShowOrbitCenter; }
  void setAlwaysShowOrbitCenter(bool show) { m_alwaysShowOrbitCenter = show; }
  const glm::vec4 &getOrbitCenterColor() const { return m_orbitCenterColor; }
  void setOrbitCenterColor(const glm::vec4 &color) {
    m_orbitCenterColor = color;
  }
  float getOrbitCenterSphereRadius() const { return m_orbitCenterSphereRadius; }
  void setOrbitCenterSphereRadius(float radius) {
    m_orbitCenterSphereRadius = radius;
  }

  // Cursor position getters
  const glm::vec3 &getCursorPosition() const { return m_cursorPosition; }
  bool isCursorPositionValid() const { return m_cursorPositionValid; }

  // Cursor position setter for captured position during orbiting
  void setCapturedCursorPosition(const glm::vec3 &position) {
    m_cursorPosition = position;
    m_cursorPositionValid = true;
  }

  // Force the shared 3D cursor onto an explicit world point (e.g. a transform
  // gizmo handle), updating every cursor type so the visible cursor sits
  // exactly there at that depth. Call after updateCursorPosition() for the
  // frame.
  void setForcedCursorPosition(const glm::vec3 &position,
                               const glm::vec3 &cameraPosition);

  // Enhanced cursor position setter that integrates with synchronization system
  void setCapturedCursorPositionWithSync(const glm::vec3 &position,
                                         bool enableSync = true) {
    setCapturedCursorPosition(position);
  }

  // Background cursor position getters (for when cursor is over empty space)
  const glm::vec3 &getBackgroundCursorPosition() const {
    return m_backgroundCursorPosition;
  }
  bool hasBackgroundCursorPosition() const {
    return m_hasBackgroundCursorPosition;
  }

  // Cursor inside window tracking
  bool IsCursorInsideWindow() const { return m_cursorInsideWindow; }
  void SetCursorInsideWindow(bool inside) { m_cursorInsideWindow = inside; }

  // Set cursor position lock (prevents updates while true)
  void setPositionLocked(bool locked) { m_positionLocked = locked; }
  bool isPositionLocked() const { return m_positionLocked; }

  // When enabled, the 3D cursor does not fall back to the Windows cursor over
  // the background; it stays at the last valid depth (see GL-era comments).
  void setKeepLastDepthOnBackground(bool keep) {
    m_keepLastDepthOnBackground = keep;
    if (!keep) {
      m_hasLastValidDepth = false;
    }
  }
  bool isKeepLastDepthOnBackground() const {
    return m_keepLastDepthOnBackground;
  }

  // Cache-expiry policy for the last-depth behaviour (values mirror
  // Cursor::BackgroundCacheMode).
  void setBackgroundCacheMode(int mode) { m_backgroundCacheMode = mode; }
  int getBackgroundCacheMode() const { return m_backgroundCacheMode; }
  void setBackgroundCacheTime(float seconds) {
    m_backgroundCacheTime = seconds;
  }
  float getBackgroundCacheTime() const { return m_backgroundCacheTime; }
  void setBackgroundCacheDistance(float pixels) {
    m_backgroundCacheDistance = pixels;
  }
  float getBackgroundCacheDistance() const { return m_backgroundCacheDistance; }

private:
  std::unique_ptr<SphereCursor> m_sphereCursor;
  std::unique_ptr<FragmentCursor> m_fragmentCursor;
  std::unique_ptr<PlaneCursor> m_planeCursor;

  // Cached cursor position shared between cursor types
  glm::vec3 m_cursorPosition;
  bool m_cursorPositionValid;
  bool m_cursorPositionCalculatedThisFrame;

  // Background cursor position (for when cursor is over empty space)
  glm::vec3 m_backgroundCursorPosition;
  bool m_hasBackgroundCursorPosition;

  // Orbit center properties
  bool m_showOrbitCenter;
  bool m_alwaysShowOrbitCenter;
  glm::vec4 m_orbitCenterColor;
  float m_orbitCenterSphereRadius;

  // 3D-viewport size (render-target pixels, refreshed every update)
  float m_viewportW;
  float m_viewportH;

  // Mouse position (viewport-local render-target pixels)
  float m_lastX;
  float m_lastY;

  // Cursor inside window tracking
  bool m_cursorInsideWindow;

  // Position locking
  bool m_positionLocked;

  // Last-known-depth caching (reverse-Z [0,1] depth values now)
  bool m_keepLastDepthOnBackground = false;
  float m_lastValidDepth = 0.5f;
  bool m_hasLastValidDepth = false;
  int m_backgroundCacheMode = 0;            // Cursor::BackgroundCacheMode
  float m_backgroundCacheTime = 1.0f;       // seconds (timed mode)
  float m_backgroundCacheDistance = 250.0f; // screen pixels (distance mode)
  double m_lastHitTime = 0.0;               // glfwGetTime() at the last hit
  float m_lastHitScreenX = 0.0f;
  float m_lastHitScreenY = 0.0f;

  // Helper function to calculate background cursor position
  glm::vec3 calculateBackgroundCursorPosition(const glm::mat4 &projection,
                                              const glm::mat4 &view);
};
} // namespace Cursor
