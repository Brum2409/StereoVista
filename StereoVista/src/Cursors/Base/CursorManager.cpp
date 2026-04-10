#include "Cursors/Base/CursorManager.h"
#include <GLFW/glfw3.h>
#include <imgui.h>
#include <iostream>

extern Camera camera;
extern int windowWidth;
extern int windowHeight;

namespace Cursor {
// Initialize cursor manager with default properties
CursorManager::CursorManager()
    : m_cursorPosition(0.0f), m_cursorPositionValid(false),
      m_cursorPositionCalculatedThisFrame(false),
      m_backgroundCursorPosition(0.0f), m_hasBackgroundCursorPosition(false),
      m_showOrbitCenter(false), m_alwaysShowOrbitCenter(false),
      m_orbitCenterColor(0.0f, 1.0f, 0.0f, 0.7f),
      m_orbitCenterSphereRadius(0.2f), m_windowWidth(1920),
      m_windowHeight(1080), m_lastX(0.0f), m_lastY(0.0f),
      m_cursorInsideWindow(true), m_positionLocked(false) {
  m_sphereCursor = std::make_unique<SphereCursor>();
  m_fragmentCursor = std::make_unique<FragmentCursor>();
  m_planeCursor = std::make_unique<PlaneCursor>();
}

CursorManager::~CursorManager() { cleanup(); }

// Initialize all cursor types and set window dimensions
void CursorManager::initialize() {
  m_sphereCursor->initialize();
  m_fragmentCursor->initialize();
  m_planeCursor->initialize();

  m_windowWidth = windowWidth;
  m_windowHeight = windowHeight;

  // Allocate 1-float PBO for async cursor depth readback.
  glGenBuffers(1, &m_depthPBO);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, m_depthPBO);
  glBufferData(GL_PIXEL_PACK_BUFFER, sizeof(float), nullptr, GL_STREAM_READ);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
}

// Updates 3D cursor position based on mouse position and depth buffer
void CursorManager::updateCursorPosition(GLFWwindow *window,
                                         const glm::mat4 &projection,
                                         const glm::mat4 &view,
                                         Engine::Shader *shader) {
  updateCursorPosition(window, projection, view, shader, true, false, nullptr,
                       nullptr, nullptr, nullptr);
}

// Updates 3D cursor position with control over when to actually calculate
void CursorManager::updateCursorPosition(GLFWwindow *window,
                                         const glm::mat4 &projection,
                                         const glm::mat4 &view,
                                         Engine::Shader *shader,
                                         bool forceRecalculate) {
  updateCursorPosition(window, projection, view, shader, forceRecalculate,
                       false, nullptr, nullptr, nullptr, nullptr);
}

// Updates 3D cursor position with stereo support (checks both eye buffers)
void CursorManager::updateCursorPosition(
    GLFWwindow *window, const glm::mat4 &projection, const glm::mat4 &view,
    Engine::Shader *shader, bool forceRecalculate, bool isStereo,
    const glm::mat4 *leftProjection, const glm::mat4 *leftView,
    const glm::mat4 *rightProjection, const glm::mat4 *rightView) {
  // If we already calculated this frame and not forcing recalculation, return
  if (m_cursorPositionCalculatedThisFrame && !forceRecalculate) {
    return;
  }
  // Skip if ImGui wants mouse input
  if (ImGui::GetIO().WantCaptureMouse) {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    return;
  }
  // Skip if cursor is outside the window to prevent edge rendering
  if (!m_cursorInsideWindow) {
    return;
  }

  // If position is locked, don't update (keeps cursor stationary during
  // parameter adjustments)
  if (m_positionLocked) {
    m_cursorPositionCalculatedThisFrame = true;
    return;
  }

  // During orbiting, maintain cursor at the captured position
  if (camera.IsOrbiting) {
    // The cursor position should already be set via setCapturedCursorPosition
    // Just update the individual cursor types with the current position
    if (m_cursorPositionValid) {
      m_sphereCursor->setPosition(m_cursorPosition);
      m_sphereCursor->setPositionValid(true);
      m_fragmentCursor->setPosition(m_cursorPosition);
      m_fragmentCursor->setPositionValid(true);
      m_planeCursor->setPosition(m_cursorPosition);
      m_planeCursor->setPositionValid(true);

      // Update sphere radius based on camera distance
      m_sphereCursor->calculateRadius(camera.Position);
    }
    m_cursorPositionCalculatedThisFrame = true;
    return;
  }

  // Only update if not animating
  if (camera.IsAnimating) {
    return;
  }

  double xpos, ypos;
  glfwGetCursorPos(window, &xpos, &ypos);
  m_lastX = static_cast<float>(xpos);
  m_lastY = static_cast<float>(ypos);

  m_windowWidth = windowWidth;
  m_windowHeight = windowHeight;

  // Additional bounds check: ensure cursor is actually within window dimensions
  // This prevents edge rendering if callback order causes position update
  // before enter callback
  if (m_lastX < 0.0f || m_lastX >= static_cast<float>(m_windowWidth) ||
      m_lastY < 0.0f || m_lastY >= static_cast<float>(m_windowHeight)) {
    // Cursor is outside window bounds - invalidate and return
    m_cursorPositionValid = false;
    m_sphereCursor->setPositionValid(false);
    m_fragmentCursor->setPositionValid(false);
    m_planeCursor->setPositionValid(false);
    m_cursorPositionCalculatedThisFrame = true;
    return;
  }

  // Depth threshold for detecting skybox/background (anything at or very close
  // to far plane)
  const float DEPTH_THRESHOLD = 0.9999f;

  // Async PBO depth readback — eliminates the per-frame synchronous glReadPixels
  // stall. Both stereo and mono paths read the same depth attachment on the same
  // currently-bound FBO at the same cursor pixel, so a single read suffices.
  //
  // Pattern:
  //   1. Poll the previous frame's fence (non-blocking). If done, map the PBO
  //      and update m_cachedDepth.
  //   2. Kick off this frame's async read into the PBO (GPU-to-PBO, no CPU wait).
  //   3. Place a new fence so we can poll completion next frame.
  //   4. Use m_cachedDepth (0 or 1 frame of latency — imperceptible at ≥60 FPS).
  if (m_depthFence) {
    GLenum res = glClientWaitSync(m_depthFence, GL_SYNC_FLUSH_COMMANDS_BIT, 0);
    if (res != GL_TIMEOUT_EXPIRED) {
      glBindBuffer(GL_PIXEL_PACK_BUFFER, m_depthPBO);
      float *ptr = (float *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
      if (ptr) {
        m_cachedDepth = *ptr;
        glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
      }
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      glDeleteSync(m_depthFence);
      m_depthFence = nullptr;
    }
  }

  // Kick off this frame's async read (nullptr → GPU writes directly to PBO).
  // main.cpp already bound the correct read FBO before calling this function.
  glBindBuffer(GL_PIXEL_PACK_BUFFER, m_depthPBO);
  glReadPixels(m_lastX, (float)m_windowHeight - m_lastY, 1, 1,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
  if (m_depthFence) glDeleteSync(m_depthFence);
  m_depthFence = glFenceSync(GL_SYNC_GPU_COMMANDS_COMPLETE, 0);

  float depth = m_cachedDepth; // use cached value (0-or-1-frame latency)

  // Convert cursor position to world space using center projection/view
  // Always use the center matrices to avoid jumps when eye selection changes
  glm::mat4 vpInv = glm::inverse(projection * view);
  glm::vec4 ndc = glm::vec4((m_lastX / (float)m_windowWidth) * 2.0 - 1.0,
                            1.0 - (m_lastY / (float)m_windowHeight) * 2.0,
                            depth * 2.0 - 1.0, 1.0);
  auto worldPosH = vpInv * ndc;
  auto worldPos = worldPosH / worldPosH.w;
  auto isHit = depth < DEPTH_THRESHOLD;

  // Update cursor properties based on whether it hit geometry
  if (isHit && (m_sphereCursor->isVisible() || m_fragmentCursor->isVisible() ||
                m_planeCursor->isVisible())) {
    m_cursorPositionValid = true;
    m_cursorPosition = glm::vec3(worldPos);

    m_sphereCursor->setPosition(m_cursorPosition);
    m_sphereCursor->setPositionValid(true);
    m_fragmentCursor->setPosition(m_cursorPosition);
    m_fragmentCursor->setPositionValid(true);
    m_planeCursor->setPosition(m_cursorPosition);
    m_planeCursor->setPositionValid(true);

    // Update radius for sphere cursor based on camera distance
    m_sphereCursor->calculateRadius(camera.Position);

    // Clear background cursor when we have a valid 3D cursor
    m_hasBackgroundCursorPosition = false;

    if (camera.IsPanning ||
        (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)) {
      m_cursorPositionCalculatedThisFrame = true;
      return;
    }
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
  } else {
    m_cursorPositionValid = false;

    m_sphereCursor->setPositionValid(false);
    m_fragmentCursor->setPositionValid(false);
    m_planeCursor->setPositionValid(false);

    // Calculate background cursor position when cursor is over empty space
    m_backgroundCursorPosition =
        calculateBackgroundCursorPosition(window, projection, view);
    m_hasBackgroundCursorPosition = true;

    if (camera.IsPanning ||
        (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)) {
      m_cursorPositionCalculatedThisFrame = true;
      return;
    }
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
  }

  // Mark that cursor position has been calculated this frame
  m_cursorPositionCalculatedThisFrame = true;
}

// Reset frame calculation flag (call at start of each frame)
void CursorManager::resetFrameCalculationFlag() {
  m_cursorPositionCalculatedThisFrame = false;
}

// Render visible 3D cursors in the scene
void CursorManager::renderCursors(const glm::mat4 &projection,
                                  const glm::mat4 &view) {
  // Don't render 3D cursor if mouse cursor has exited the window
  if (!m_cursorInsideWindow) {
    return;
  }

  if (m_sphereCursor->isVisible()) {
    m_sphereCursor->render(projection, view, camera.Position);
  }

  if (m_planeCursor->isVisible()) {
    m_planeCursor->render(projection, view, camera.Position);
  }

  // Fragment cursor is rendered in the fragment shader via updateShaderUniforms
}

// Update shader uniforms for cursor visualization in fragment shaders
void CursorManager::updateShaderUniforms(Engine::Shader *shader) {
  if (!shader)
    return;

  // Set cursor position for fragment shader
  shader->setVec4(
      "cursorPos",
      camera.IsOrbiting ? glm::vec4(m_cursorPosition, true)
                        : // Always valid during orbiting
          glm::vec4(m_cursorPosition, m_cursorPositionValid ? 1.0f : 0.0f));

  // Update fragment cursor uniforms if visible
  if (m_fragmentCursor->isVisible()) {
    m_fragmentCursor->updateShaderUniforms(shader);
  } else {
    shader->setFloat("baseOuterRadius", 0.0f);
    shader->setFloat("baseOuterBorderThickness", 0.0f);
    shader->setFloat("baseInnerRadius", 0.0f);
    shader->setFloat("baseInnerBorderThickness", 0.0f);
    shader->setVec4("outerCursorColor", glm::vec4(0.0f));
    shader->setVec4("innerCursorColor", glm::vec4(0.0f));
    shader->setBool("showFragmentCursor", false);
  }
}

// Renders a sphere at the orbit center point for visual reference
void CursorManager::renderOrbitCenter(const glm::mat4 &projection,
                                      const glm::mat4 &view,
                                      const glm::vec3 &orbitPoint) {
  if (!m_showOrbitCenter)
    return;

  // Use the sphere cursor's shader to render the orbit center
  auto sphereShader = m_sphereCursor->getShader();
  if (!sphereShader)
    return;

  // Enable depth testing and blending
  glEnable(GL_DEPTH_TEST);
  glDepthFunc(GL_LESS);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  sphereShader->use();
  sphereShader->setMat4("projection", projection);
  sphereShader->setMat4("view", view);

  // Create model matrix for orbit center
  glm::mat4 model = glm::translate(glm::mat4(1.0f), orbitPoint);
  model = glm::scale(model, glm::vec3(m_orbitCenterSphereRadius));

  sphereShader->setMat4("model", model);
  sphereShader->setVec3("viewPos", camera.Position);
  sphereShader->setVec4("sphereColor", m_orbitCenterColor);
  sphereShader->setFloat("transparency", 1.0f);
  sphereShader->setFloat("edgeSoftness", 0.0f);
  sphereShader->setFloat("centerTransparencyFactor", 0.0f);

  // Render orbit center sphere using two-pass approach for correct face ordering
  glBindVertexArray(m_sphereCursor->getVAO());
  glEnable(GL_CULL_FACE);

  // First pass: Render back faces with depth write enabled
  glDepthMask(GL_TRUE);
  glCullFace(GL_FRONT);
  glDrawElements(GL_TRIANGLES, m_sphereCursor->getIndices().size(),
                 GL_UNSIGNED_INT, 0);

  // Second pass: Render front faces with depth write disabled
  glDepthMask(GL_FALSE);
  glCullFace(GL_BACK);
  glDrawElements(GL_TRIANGLES, m_sphereCursor->getIndices().size(),
                 GL_UNSIGNED_INT, 0);

  // Reset OpenGL state
  glDepthMask(GL_TRUE);
  glCullFace(GL_BACK);
  glBindVertexArray(0);

  glDisable(GL_BLEND);
}

// Release resources for all cursor types
void CursorManager::cleanup() {
  // Release async depth-readback resources before cursor cleanup.
  if (m_depthFence) { glDeleteSync(m_depthFence); m_depthFence = nullptr; }
  if (m_depthPBO)   { glDeleteBuffers(1, &m_depthPBO); m_depthPBO = 0; }

  m_sphereCursor->cleanup();
  m_fragmentCursor->cleanup();
  m_planeCursor->cleanup();
}

// Calculate background cursor position when cursor is over empty space
glm::vec3 CursorManager::calculateBackgroundCursorPosition(
    GLFWwindow *window, const glm::mat4 &projection, const glm::mat4 &view) {
  // Use already stored mouse position from updateCursorPosition
  float mouseX = m_lastX;
  float mouseY = m_lastY;

  // Convert to normalized device coordinates
  float x = (2.0f * mouseX) / (float)m_windowWidth - 1.0f;
  float y = 1.0f - (2.0f * mouseY) / (float)m_windowHeight;

  // Project to a reasonable distance from camera (middle of view frustum)
  float targetDepth = 0.5f; // NDC depth between near (0) and far (1)
  glm::vec4 nearPoint = glm::vec4(x, y, -1.0f, 1.0f); // Near plane
  glm::vec4 farPoint = glm::vec4(x, y, 1.0f, 1.0f);   // Far plane

  // Transform to world space
  glm::mat4 invViewProj = glm::inverse(projection * view);
  glm::vec4 nearWorld = invViewProj * nearPoint;
  glm::vec4 farWorld = invViewProj * farPoint;

  nearWorld /= nearWorld.w;
  farWorld /= farWorld.w;

  // Interpolate between near and far points at the target depth
  glm::vec3 worldPos =
      glm::mix(glm::vec3(nearWorld), glm::vec3(farWorld), targetDepth);

  return worldPos;
}
} // namespace Cursor