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

  // Mouse position
  float m_lastX;
  float m_lastY;

  // Cursor inside window tracking
  bool m_cursorInsideWindow;

  // Position locking
  bool m_positionLocked;

  // Helper function to calculate background cursor position
  glm::vec3 calculateBackgroundCursorPosition(GLFWwindow *window,
                                              const glm::mat4 &projection,
                                              const glm::mat4 &view);
};
} // namespace Cursor