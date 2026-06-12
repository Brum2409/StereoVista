#pragma once

#include "Core/Camera.h"
#include "Cursor.h"
#include "Cursors/Types/FragmentCursor.h"
#include "Cursors/Types/PlaneCursor.h"
#include "Cursors/Types/SphereCursor.h"
#include <memory>

namespace Cursor {
// Central manager for all cursor types
class CursorManager {
public:
  CursorManager();
  ~CursorManager();

  // Initialize all cursor types
  void initialize();

  // Update cursor position based on ray casting
  void updateCursorPosition(GLFWwindow *window, const glm::mat4 &projection,
                            const glm::mat4 &view, Engine::Shader *shader);

  // Update cursor position with control over when to actually calculate
  void updateCursorPosition(GLFWwindow *window, const glm::mat4 &projection,
                            const glm::mat4 &view, Engine::Shader *shader,
                            bool forceRecalculate);

  // Update cursor position with stereo support (checks both eye buffers)
  void updateCursorPosition(GLFWwindow *window, const glm::mat4 &projection,
                            const glm::mat4 &view, Engine::Shader *shader,
                            bool forceRecalculate, bool isStereo,
                            const glm::mat4 *leftProjection = nullptr,
                            const glm::mat4 *leftView = nullptr,
                            const glm::mat4 *rightProjection = nullptr,
                            const glm::mat4 *rightView = nullptr);

  // Reset frame calculation flag (call at start of each frame)
  void resetFrameCalculationFlag();

  // Define the sub-rectangle of the render target the scene is drawn into, so
  // mouse picking maps correctly when the 3D viewport doesn't fill the window.
  //  originX/originYGL : bottom-left of the scene region in the currently bound
  //                      framebuffer's pixel space (GL origin, bottom-left).
  //  width/height      : size of the scene region in pixels.
  //  leftInset/topInset: window-space gap to the left of / above the region,
  //                      used to convert the OS cursor position into the region.
  // Defaults (all zero / unset) reproduce full-window behavior.
  void setViewport(int originX, int originYGL, int width, int height,
                   int leftInset, int topInset);

  // Render all visible cursors
  void renderCursors(const glm::mat4 &projection, const glm::mat4 &view);

  // Update fragment shader uniforms for cursors
  void updateShaderUniforms(Engine::Shader *shader);

  // Clean up all cursors
  void cleanup();

  // Getters for cursor instances
  SphereCursor *getSphereCursor() { return m_sphereCursor.get(); }
  FragmentCursor *getFragmentCursor() { return m_fragmentCursor.get(); }
  PlaneCursor *getPlaneCursor() { return m_planeCursor.get(); }

  // Orbit center rendering
  void renderOrbitCenter(const glm::mat4 &projection, const glm::mat4 &view,
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

  // Enhanced cursor position setter that integrates with synchronization system
  void setCapturedCursorPositionWithSync(const glm::vec3 &position,
                                         bool enableSync = true) {
    setCapturedCursorPosition(position);

    // If synchronization is enabled, this position will be used for cursor sync
    // The actual synchronization happens in mouse button release handlers
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
  // Used when adjusting parameters that affect projection matrices (like
  // convergence) to keep visual cursor stable
  void setPositionLocked(bool locked) { m_positionLocked = locked; }
  bool isPositionLocked() const { return m_positionLocked; }

  // When enabled, the 3D cursor does not fall back to the Windows cursor over
  // the background. Instead it stays at the last valid depth and follows the
  // mouse at that depth until geometry is hit again. Useful for sparse point
  // clouds where the cursor would otherwise flicker between 2D and 3D.
  void setKeepLastDepthOnBackground(bool keep) {
    m_keepLastDepthOnBackground = keep;
    if (!keep) {
      m_hasLastValidDepth = false;
    }
  }
  bool isKeepLastDepthOnBackground() const {
    return m_keepLastDepthOnBackground;
  }

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

  // Window dimensions
  int m_windowWidth;
  int m_windowHeight;

  // Scene-region sub-rectangle within the render target (see setViewport).
  // Zero width/height means "unset" -> fall back to the full window.
  int m_vpOriginX = 0;
  int m_vpOriginYGL = 0;
  int m_vpWidth = 0;
  int m_vpHeight = 0;
  int m_vpLeftInset = 0;
  int m_vpTopInset = 0;

  // Mouse position
  float m_lastX;
  float m_lastY;

  // Cursor inside window tracking
  bool m_cursorInsideWindow;

  // Position locking
  bool m_positionLocked;

  // Last-known-depth caching (see setKeepLastDepthOnBackground)
  bool m_keepLastDepthOnBackground = false;
  float m_lastValidDepth = 0.5f; // depth buffer value of the last geometry hit
  bool m_hasLastValidDepth = false;

  // Helper function to calculate background cursor position
  glm::vec3 calculateBackgroundCursorPosition(GLFWwindow *window,
                                              const glm::mat4 &projection,
                                              const glm::mat4 &view);
};
} // namespace Cursor