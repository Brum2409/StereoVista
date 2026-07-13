#ifndef CAMERA_H
#define CAMERA_H

// GPU-API-free since Phase 6 of the Vulkan migration: the GL era's two touch
// points are gone — the glReadPixels centre-depth sampler moved to the
// application (app::Application::updateCameraDepth, fed by the renderer's
// depth-picking readback) and the glfwGetTime scroll timestamps were dead
// code (the computed delta was never used).

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtx/rotate_vector.hpp>


enum Camera_Movement { FORWARD, BACKWARD, LEFT, RIGHT, UP, DOWN };

// Default camera values
const float YAW = -90.0f;
const float PITCH = 0.0f;
const float SPEED = 2.0f;
const float SENSITIVITY = 0.06f;
const float ZOOM = 45.0f;

class Camera {
public:
  // Camera state structure
  struct CameraState {
    glm::vec3 position;
    glm::vec3 front;
    glm::vec3 up;
    float yaw;
    float pitch;
    float zoom;
    glm::quat orientation; // Quaternion representation
  };

  // Camera attributes
  glm::vec3 Position;
  glm::vec3 Front;
  glm::vec3 Up;
  glm::vec3 Right;
  glm::vec3 WorldUp;
  float Yaw;
  float Pitch;

  // Quaternion-based orientation (eliminates gimbal lock)
  glm::quat Orientation;
  float MovementSpeed;
  float MouseSensitivity;
  float Zoom;
  bool isLookingAtEmptySpace;
  bool isMoving;

  // Speed parameters
  float minSpeed = 0.2f;
  float maxSpeed = 3.0f;
  float speedFactor = 1.0f;
  // World-space size of the scene content (largest bounds dimension), fed by
  // the application each frame. Scales the adaptive-speed and scroll-factor
  // distance curves — the GL code hardcoded 1.0 in the scroll path.
  float sceneSize = 1.0f;

  // Scroll parameters. scrollVelocity is in wheel-notch units; the zoom
  // constants below convert it into a FRACTION of the current distance to the
  // zoom target per second (distance-proportional zoom — see UpdateScrolling).
  float scrollMomentum = 0.5f;
  float maxScrollVelocity = 3.0f;
  float scrollDeceleration = 5.0f;
  bool useSmoothScrolling = true;
  float scrollVelocity = 0.0f;
  // Tuned for a calm glide: one notch peaks at ~0.6x the reference distance
  // per second and eases out exponentially (~0.12 of the reference in total);
  // spinning the wheel stacks velocity up to the cap for deliberate fast travel.
  static constexpr float kZoomPerNotch = 0.12f; // fraction of the reference distance per notch
  static constexpr float kZoomRate = 1.2f;      // fraction/s per notch of velocity
  static constexpr float kMaxZoomPerSec = 2.0f; // fraction/s cap (wheel spam stays sane)

  // Orbit parameters
  glm::vec3 OrbitPoint;
  float OrbitDistance;
  bool IsOrbiting;
  bool IsPanning;
  // World units the camera travels per PIXEL of mouse motion while panning —
  // frozen for the whole drag by StartPanning from the distance to the point
  // under the cursor (see there). Panning translates in the view plane, which
  // leaves that distance unchanged, so one factor stays exact for the drag.
  float PanWorldPerPixel = 0.01f;

  // Animation parameters
  bool IsAnimating;
  glm::vec3 AnimationStartPosition;
  glm::vec3 AnimationEndPosition;
  glm::quat AnimationStartOrientation;
  glm::quat AnimationEndOrientation;
  float AnimationStartZoom = ZOOM;
  float AnimationEndZoom = ZOOM;
  float AnimationProgress;
  float AnimationDuration;
  // Whether to fire centeringCompletedCallback when the current animation
  // finishes. Centering animations want it; snapshot-restore animations do not
  // (they must not warp the cursor or start orbiting).
  bool AnimationInvokeCallback = true;

  bool useNewMethod = true;
  bool wireframe = false;

  // Cursor-based navigation
  bool zoomToCursor = false;
  glm::vec3 cursorPosition = glm::vec3(0.0f);
  bool cursorValid = false;
  glm::vec3 scrollTargetPos = glm::vec3(0.0f);
  bool isScrollingToCursor = false;

  // Distance tracking
  float distanceToNearestObject = 0.0f;
  bool distanceUpdated = false;
  bool orbitAroundCursor = false;
  // Last scene distance sampled at the viewport centre that actually HIT
  // geometry. When the centre feed misses (skimming a hole in a sparse point
  // cloud, or pointing at open sky) the app holds this value instead of
  // snapping the reference to the far plane, so the adaptive fly/zoom speed no
  // longer spikes. Per-viewport (each viewport owns its Camera). See
  // Application::updateCameraDepth.
  float lastValidSceneDistance = 0.0f;
  bool hasLastValidSceneDistance = false;

  std::function<void()> centeringCompletedCallback;

  // Constructor with default values
  Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f),
         glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW,
         float pitch = PITCH)
      : isMoving(false), isLookingAtEmptySpace(false),
        Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED),
        MouseSensitivity(SENSITIVITY), Zoom(ZOOM), IsOrbiting(false),
        IsPanning(false), IsAnimating(false), AnimationProgress(0.0f),
        AnimationDuration(0.5f), centeringCompletedCallback(nullptr) {
    Position = position;
    WorldUp = up;
    Yaw = yaw;
    Pitch = pitch;
    OrbitPoint = Position + Front;
    OrbitDistance = 1.0f;

    // Initialize quaternion from Euler angles
    initializeQuaternionFromEuler();
    updateCameraVectorsFromQuaternion();
  }

  // Returns the view matrix calculated using Euler Angles and the LookAt Matrix
  glm::mat4 GetViewMatrix() const {
    return glm::lookAt(Position, Position + Front, Up);
  }

  // Returns the projection matrix for the camera
  glm::mat4 GetProjectionMatrix(float aspectRatio, float nearPlane,
                                float farPlane) const {
    return glm::perspective(glm::radians(Zoom), aspectRatio, nearPlane,
                            farPlane);
  }

  // Gets the current camera state
  CameraState GetState() const {
    CameraState state;
    state.position = Position;
    state.front = Front;
    state.up = Up;
    state.yaw = Yaw;
    state.pitch = Pitch;
    state.zoom = Zoom;
    state.orientation = Orientation;
    return state;
  }

  // Sets the camera state
  void SetState(const CameraState &state) {
    Position = state.position;
    Front = state.front;
    Up = state.up;
    Yaw = state.yaw;
    Pitch = state.pitch;
    Zoom = state.zoom;
    Orientation = state.orientation;
    updateCameraVectorsFromQuaternion();
  }

  // Updates the cursor information for cursor-based navigation
  void UpdateCursorInfo(const glm::vec3 &pos, bool valid) {
    cursorPosition = pos;
    cursorValid = valid;
  }

  // Updates the distance to the nearest visible object
  void UpdateDistanceToObject(float distance) {
    distanceToNearestObject = distance;
    distanceUpdated = true;
  }

  // Synchronize quaternion with current Euler angles (SpaceMouse -> Normal
  // transition)
  void SynchronizeQuaternionFromEuler() {
    initializeQuaternionFromEuler();
    updateCameraVectorsFromQuaternion();
  }

  // Synchronize Euler angles with current quaternion (Normal -> SpaceMouse
  // transition)
  void SynchronizeEulerFromQuaternion() { updateEulerFromQuaternion(); }

  // Extract the 6 normalized frustum planes from a P*V matrix.
  // Call this once per renderModels invocation, then reuse the result per model.
  static std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &vp) {
    std::array<glm::vec4, 6> planes;
    for (int i = 0; i < 6; ++i) {
      glm::vec4 p(vp[0][3] + (i % 2 == 0 ? vp[0][i / 2] : -vp[0][i / 2]),
                  vp[1][3] + (i % 2 == 0 ? vp[1][i / 2] : -vp[1][i / 2]),
                  vp[2][3] + (i % 2 == 0 ? vp[2][i / 2] : -vp[2][i / 2]),
                  vp[3][3] + (i % 2 == 0 ? vp[3][i / 2] : -vp[3][i / 2]));
      p /= glm::length(glm::vec3(p));
      planes[i] = p;
    }
    return planes;
  }

  // Test a sphere (center + radius) against pre-extracted frustum planes.
  static bool isInFrustumPlanes(const glm::vec3 &center, float radius,
                                 const std::array<glm::vec4, 6> &planes) {
    for (const auto &plane : planes) {
      if (glm::dot(center, glm::vec3(plane)) + plane.w <= -radius)
        return false;
    }
    return true;
  }

  // Frustum culling check (extracts planes every call — prefer the two-step
  // extractFrustumPlanes / isInFrustumPlanes API when testing many objects).
  bool isInFrustum(const glm::vec3 &point, float radius,
                   const glm::mat4 &viewProj) const {
    auto planes = extractFrustumPlanes(viewProj);
    return isInFrustumPlanes(point, radius, planes);
  }

  // Processes keyboard input and moves the camera accordingly
  void ProcessKeyboard(Camera_Movement direction, float deltaTime) {
    if (IsAnimating)
      return;

    float velocity = MovementSpeed * deltaTime;
    glm::vec3 oldPosition = Position;

    switch (direction) {
    case FORWARD:
      Position += Front * velocity;
      break;
    case BACKWARD:
      Position -= Front * velocity;
      break;
    case LEFT:
      Position -= Right * velocity;
      break;
    case RIGHT:
      Position += Right * velocity;
      break;
    case UP:
      Position += Up * velocity;
      break;
    case DOWN:
      Position -= Up * velocity;
      break;
    }

    glm::vec3 actualMovement = Position - oldPosition;
    isMoving = glm::length(actualMovement) > 0.0001f;

    OrbitPoint = Position + Front * OrbitDistance;
  }

  // Distance-proportional adaptive fly speed — the SAME scale-free idea as the
  // scroll zoom, which is why both now behave across wildly different scales.
  // Speed tracks the distance to whatever the view centre rests on, so you
  // cover that gap in ~1/kSpeedFraction seconds REGARDLESS of scene size: a
  // hair's-crawl beside a flower pot and a fast glide over a city — correct for
  // BOTH in one scene, because it keys off the LOCAL distance, not the global
  // scene bounds.
  //
  // The old curve keyed min/max speed AND the distance band off `sceneSize`, so
  // a huge city set a huge speed FLOOR (minSpeed = sceneSize*0.1) that a nearby
  // small object could never escape — WASD "broke" (ran away) in large / mixed
  // scenes. Here sceneSize is only a far-away SANITY ceiling; the local
  // distance drives everything. The caller HOLDS the last valid distance when
  // the centre sees nothing (Application::updateCameraDepth), so staring into
  // open sky keeps the last local speed instead of accelerating into the void.
  void AdjustMovementSpeed(float distanceToNearest, float nearPlane,
                           float farPlane, float dt) {
    if (!isMoving)
      return;

    const float kSpeedFraction = 1.0f; // gap covered per second at speedFactor 1

    // Reference distance, clamped so speed never crawls to nothing when hugging
    // a surface (floor at the near plane — scale-independent, user-set) nor runs
    // away if the centre genuinely sees far geometry (scene-scale ceiling; with
    // the last-distance hold this rarely bites). NOT keyed to sceneSize on the
    // low end — that was the mixed-scene bug.
    const float lo = glm::max(nearPlane, 1e-4f);
    const float hi = glm::max(sceneSize * 2.0f, lo);
    const float ref = glm::clamp(distanceToNearest, lo, hi);

    isLookingAtEmptySpace = distanceToNearest >= farPlane * 0.999f;

    const float target = ref * kSpeedFraction * speedFactor;
    // Bookkeeping (nothing reads these today, kept meaningful for future UI).
    minSpeed = lo * kSpeedFraction * speedFactor;
    maxSpeed = hi * kSpeedFraction * speedFactor;

    // Gentle exponential spin-up (~63% of the gap every 1/3 s) so pulling away
    // from geometry ramps smoothly; immediate braking so diving toward geometry
    // sheds speed without lag (no overshoot into surfaces).
    if (target > MovementSpeed)
      MovementSpeed = glm::mix(MovementSpeed, target, 1.0f - std::exp(-3.0f * dt));
    else
      MovementSpeed = target;
  }

  // Reference distance for the distance-proportional zoom: how far away the
  // thing being zoomed at is — the explicit target when zooming to the
  // cursor, else the centre-depth feed, else the orbit anchor. Clamped to a
  // sceneSize band so the zoom never crawls into a surface nor blasts off
  // over the background, at ANY scene scale.
  float zoomReferenceDistance(const glm::vec3 *target) const {
    float ref;
    if (target)
      ref = glm::length(*target - Position);
    else if (distanceUpdated)
      ref = distanceToNearestObject;
    else
      ref = OrbitDistance;
    return glm::clamp(ref, sceneSize * 0.01f, sceneSize * 2.0f);
  }

  // Processes mouse movement for rotation, orbiting, and panning
  void ProcessMouseMovement(float xoffset, float yoffset,
                            bool constrainPitch = true) {
    if (IsAnimating)
      return;

    // Rotation/orbit are angular, so they scale with MouseSensitivity. Panning
    // is NOT: it is locked to the pixels the mouse actually travelled (the grab
    // point must stay under the pointer), so it keeps the raw offsets.
    const float rawX = xoffset;
    const float rawY = yoffset;

    xoffset *= MouseSensitivity;
    yoffset *= MouseSensitivity;

    if (IsOrbiting) {
      if (orbitAroundCursor) {
        // Orbit around cursor position using pure quaternion rotation
        glm::vec3 orbitToCamera = Position - OrbitPoint;
        float distance = glm::length(orbitToCamera);

        // Create rotation quaternions for orbit movement
        glm::quat yawRotation = glm::angleAxis(-glm::radians(xoffset), WorldUp);
        glm::vec3 rightAxis =
            glm::normalize(glm::cross(orbitToCamera, WorldUp));
        glm::quat pitchRotation =
            glm::angleAxis(-glm::radians(yoffset), rightAxis);

        // Apply orbit rotation to position
        orbitToCamera = pitchRotation * (yawRotation * orbitToCamera);
        orbitToCamera = glm::normalize(orbitToCamera) * distance;
        Position = OrbitPoint + orbitToCamera;

        // Apply same rotation to camera orientation
        Orientation = pitchRotation * yawRotation * Orientation;
        Orientation = glm::normalize(Orientation);
        updateCameraVectorsFromQuaternion();
      } else {
        // Standard orbit mode using quaternions
        glm::vec3 toCamera = Position - OrbitPoint;
        float distance = glm::length(toCamera);

        // Create rotation quaternions
        glm::quat yawRotation = glm::angleAxis(-glm::radians(xoffset), WorldUp);
        glm::vec3 rightAxis = glm::normalize(glm::cross(toCamera, WorldUp));
        glm::quat pitchRotation =
            glm::angleAxis(-glm::radians(yoffset), rightAxis);

        // Apply rotation to camera position
        toCamera = pitchRotation * (yawRotation * toCamera);
        Position = OrbitPoint + toCamera;

        // Calculate orientation to look at orbit point
        glm::vec3 lookDirection = glm::normalize(OrbitPoint - Position);
        glm::vec3 rightDir = glm::normalize(glm::cross(lookDirection, WorldUp));
        glm::vec3 upDir = glm::normalize(glm::cross(rightDir, lookDirection));

        // Create rotation matrix and convert to quaternion
        glm::mat3 rotMatrix = glm::mat3(rightDir, upDir, -lookDirection);
        Orientation = glm::normalize(glm::quat_cast(rotMatrix));
        updateCameraVectorsFromQuaternion();
      }
    } else if (IsPanning) {
      // Screen-space-locked pan: the world point the drag grabbed stays glued to
      // the pointer, so dragging feels identical whether you are inches from a
      // surface or a kilometre out. PanWorldPerPixel carries the distance to
      // that point (StartPanning); the old OrbitDistance*0.01 factor ignored
      // both what you were looking at and the FOV/viewport, so panning crawled
      // up close and flew far away.
      Position -= Right * rawX * PanWorldPerPixel;
      Position -= Up * rawY * PanWorldPerPixel;

      OrbitPoint = Position + Front * OrbitDistance;
    } else {
      // Standard free camera rotation using quaternions (eliminates gimbal
      // lock) Create rotation quaternions for yaw and pitch
      glm::quat yawRotation = glm::angleAxis(glm::radians(-xoffset), WorldUp);
      // Use local X axis for pitch to maintain consistent up/down movement
      glm::vec3 localRight = glm::vec3(1.0f, 0.0f, 0.0f);
      glm::quat pitchRotation =
          glm::angleAxis(glm::radians(yoffset), localRight);

      // Apply yaw rotation first
      Orientation = yawRotation * Orientation;

      // For pitch, check constraint and apply partial rotation if needed
      if (constrainPitch) {
        // Get current pitch
        glm::mat4 currentRotationMatrix = glm::mat4_cast(Orientation);
        glm::vec3 currentFront = -glm::vec3(currentRotationMatrix[2]);
        float currentPitch = glm::degrees(asin(-currentFront.y));

        // Test the full pitch rotation
        glm::quat testOrientation = Orientation * pitchRotation;
        glm::mat4 testRotationMatrix = glm::mat4_cast(testOrientation);
        glm::vec3 testFront = -glm::vec3(testRotationMatrix[2]);
        float testPitch = glm::degrees(asin(-testFront.y));

        if (testPitch > 88.5f || testPitch < -88.5f) {
          // Calculate how much rotation we can apply to reach exactly 88.5
          // degrees
          float targetPitch = (testPitch > 88.5f) ? 88.5f : -88.5f;
          float remainingPitch = targetPitch - currentPitch;

          if (abs(remainingPitch) >
              0.01f) { // Only apply if there's meaningful rotation left
            glm::quat constrainedPitchRotation =
                glm::angleAxis(glm::radians(remainingPitch), localRight);
            Orientation = Orientation * constrainedPitchRotation;
          }
        } else {
          // Safe to apply full pitch rotation
          Orientation = Orientation * pitchRotation;
        }
      } else {
        // No constraint, apply pitch rotation normally
        Orientation = Orientation * pitchRotation;
      }

      // Normalize to prevent drift
      Orientation = glm::normalize(Orientation);

      // Update camera vectors from quaternion
      updateCameraVectorsFromQuaternion();

      OrbitPoint = Position + Front * OrbitDistance;
    }
  }

  // Processes mouse scroll for zooming.
  // Distance-proportional rewrite of the GL version: every step covers a
  // FRACTION of the live distance to what is being zoomed at, so the feel is
  // identical at any scene scale and any distance — brisk over far/background,
  // automatically gentle near surfaces, and independent of the (adaptive,
  // possibly stale per-viewport) fly MovementSpeed. The GL code scaled a fixed
  // step by a distance factor applied TWICE (once accumulating velocity, once
  // integrating it) and by MovementSpeed — crawling near geometry (0.01x) and
  // blasting over background (up to 20x).
  void
  ProcessMouseScroll(float yoffset,
                     const glm::vec3 &backgroundCursorPos = glm::vec3(0.0f),
                     bool hasBackgroundCursor = false) {
    if (IsAnimating)
      return;

    // Resolve the zoom target for this event: the 3D cursor point on
    // geometry, else the background-plane point, else free zoom along Front.
    bool hasTarget = false;
    glm::vec3 targetPos(0.0f);
    if (zoomToCursor && cursorValid) {
      targetPos = cursorPosition;
      hasTarget = true;
    } else if (zoomToCursor && hasBackgroundCursor) {
      targetPos = backgroundCursorPos;
      hasTarget = true;
    }

    if (!useSmoothScrolling) {
      // Direct movement without momentum: one fixed fraction per notch.
      const float ref = zoomReferenceDistance(hasTarget ? &targetPos : nullptr);
      glm::vec3 dir = Front;
      if (hasTarget) {
        const glm::vec3 toTarget = targetPos - Position;
        const float distance = glm::length(toTarget);
        if (distance > 1e-4f)
          dir = toTarget / distance;
      }
      Position += dir * (yoffset * kZoomPerNotch * ref * speedFactor);
      if (IsOrbiting)
        OrbitPoint = Position + Front * OrbitDistance;
      return;
    }

    // Smooth scrolling: accumulate momentum in wheel-notch units (the
    // distance scaling happens at integration time, every frame, against the
    // CURRENT distance — that is what makes the approach ease in).
    scrollVelocity += yoffset * scrollMomentum;
    scrollVelocity =
        glm::clamp(scrollVelocity, -maxScrollVelocity, maxScrollVelocity);
    scrollTargetPos = targetPos;
    isScrollingToCursor = hasTarget;
  }

  // Updates smooth scrolling movement over time
  void UpdateScrolling(float deltaTime) {
    if (scrollVelocity == 0.0f)
      return;

    // Fraction of the reference distance covered this frame. The reference is
    // recomputed EVERY frame: zooming in on a target shrinks it, easing the
    // approach exponentially; pulling back grows it, accelerating out — the
    // classic map-app zoom, scale-free. The cap keeps wheel spam from
    // covering more than ~2.5x the reference per second.
    const float ref = zoomReferenceDistance(
        isScrollingToCursor ? &scrollTargetPos : nullptr);
    const float fractionPerSec =
        glm::clamp(scrollVelocity * kZoomRate, -kMaxZoomPerSec, kMaxZoomPerSec);
    const float step = fractionPerSec * deltaTime * ref * speedFactor;

    glm::vec3 dir = Front;
    if (isScrollingToCursor) {
      const glm::vec3 toTarget = scrollTargetPos - Position;
      const float distance = glm::length(toTarget);
      if (distance > 1e-4f)
        dir = toTarget / distance;
      else
        isScrollingToCursor = false; // reached the point: continue along Front
    }
    Position += dir * step;

    // Exponential ease-out (scrollDeceleration is the decay rate, 1/s): the
    // velocity bleeds off asymptotically instead of hitting a hard stop, which
    // is what makes the glide feel smooth. (The GL version decelerated
    // linearly — an abrupt end — and scaled the tail by the distance factor,
    // so its length depended on where you looked.)
    scrollVelocity *= std::exp(-scrollDeceleration * deltaTime);
    if (std::abs(scrollVelocity) < 0.01f)
      scrollVelocity = 0.0f;

    if (IsOrbiting)
      OrbitPoint = Position + Front * OrbitDistance;
  }

  // Sets orbit distance and updates orbit point
  void SetOrbitPoint(float distance) {
    OrbitDistance = distance;
    OrbitPoint = Position + Front * OrbitDistance;
  }

  // Sets orbit point directly and calculates distance
  void SetOrbitPointDirectly(const glm::vec3 &point) {
    OrbitPoint = point;
    OrbitDistance = glm::length(Position - OrbitPoint);
  }

  // Starts animation to center camera on target point
  void StartCenteringAnimation(const glm::vec3 &targetPoint) {
    IsAnimating = true;
    AnimationInvokeCallback = true;
    AnimationStartPosition = Position;
    AnimationStartOrientation = Orientation;
    // Zoom (FOV) is unchanged by centering.
    AnimationStartZoom = Zoom;
    AnimationEndZoom = Zoom;

    float initialDistance = glm::length(Position - targetPoint);
    glm::vec3 directionToCamera = glm::normalize(Position - targetPoint);
    AnimationEndPosition = targetPoint + directionToCamera * initialDistance;

    // Calculate target orientation to look at the target point
    glm::vec3 targetFront = glm::normalize(targetPoint - AnimationEndPosition);
    glm::vec3 targetRight = glm::normalize(glm::cross(targetFront, WorldUp));
    glm::vec3 targetUp = glm::normalize(glm::cross(targetRight, targetFront));

    // Create rotation matrix and convert to quaternion
    glm::mat3 targetRotMatrix = glm::mat3(targetRight, targetUp, -targetFront);
    AnimationEndOrientation = glm::normalize(glm::quat_cast(targetRotMatrix));

    AnimationProgress = 0.0f;
    AnimationDuration = 0.5f;
    OrbitDistance = initialDistance;
  }

  // Smoothly animate every camera parameter (position, orientation and zoom)
  // toward a saved state with easing. Used when restoring a snapshot's camera
  // so the view glides into place instead of jumping. OrbitDistance is left
  // untouched and re-anchored along the new front on completion.
  void StartStateAnimation(const CameraState &target, float duration = 0.6f) {
    IsAnimating = true;
    // Snapshot restores must not trigger the centering callback (cursor warp /
    // auto-orbit); that behavior is specific to double-click centering.
    AnimationInvokeCallback = false;
    AnimationStartPosition = Position;
    AnimationStartOrientation = Orientation;
    AnimationStartZoom = Zoom;

    AnimationEndPosition = target.position;
    glm::quat endOrient = glm::normalize(target.orientation);
    // Take the shortest rotational path so large turns don't spin the long way.
    if (glm::dot(AnimationStartOrientation, endOrient) < 0.0f)
      endOrient = -endOrient;
    AnimationEndOrientation = endOrient;
    AnimationEndZoom = target.zoom;

    AnimationProgress = 0.0f;
    AnimationDuration = (duration > 0.0001f) ? duration : 0.0001f;
  }

  // Updates camera animation with smooth easing
  void UpdateAnimation(float deltaTime) {
    if (!IsAnimating)
      return;

    AnimationProgress += deltaTime / AnimationDuration;

    if (AnimationProgress >= 1.0f) {
      // Animation complete
      Position = AnimationEndPosition;
      Orientation = AnimationEndOrientation;
      Zoom = AnimationEndZoom;
      IsAnimating = false;
      updateCameraVectorsFromQuaternion();

      OrbitPoint = Position + Front * OrbitDistance;

      if (AnimationInvokeCallback && centeringCompletedCallback) {
        centeringCompletedCallback();
      }
    } else {
      // Apply cubic easing function
      float t = easeOutCubic(AnimationProgress);

      // Interpolate position, orientation (SLERP) and zoom for a smooth glide.
      Position = glm::mix(AnimationStartPosition, AnimationEndPosition, t);
      Orientation = glm::normalize(
          glm::slerp(AnimationStartOrientation, AnimationEndOrientation, t));
      Zoom = glm::mix(AnimationStartZoom, AnimationEndZoom, t);

      // Update camera vectors from interpolated quaternion
      updateCameraVectorsFromQuaternion();
    }
  }

  // Starts orbit mode, optionally around cursor
  void StartOrbiting(bool useCurrentCursorPosition = false) {
    if (useCurrentCursorPosition && cursorValid) {
      OrbitPoint = cursorPosition;
      OrbitDistance = glm::length(Position - OrbitPoint);
    }
    IsOrbiting = true;
  }

  void StopOrbiting() { IsOrbiting = false; }

  // Begin a pan. grabDistance is the distance from the eye to the point the
  // drag grabbed (the 3D cursor's, so it is what you SEE under the pointer);
  // viewportHeightPx is the height of the 3D view ON SCREEN, in the same pixels
  // the mouse deltas come in. Together with the vertical FOV they give the world
  // extent of one pixel at that depth — pan by exactly that per pixel and the
  // grabbed point tracks the mouse 1:1 at any distance, FOV or window size.
  void StartPanning(float grabDistance, float viewportHeightPx) {
    IsPanning = true;
    const float h = glm::max(viewportHeightPx, 1.0f);
    // The clamp is the same band the zoom reference uses: a grab point on a
    // hair-thin near surface must not freeze the pan, and one on the far
    // background must not launch the camera across the scene.
    const float d = glm::clamp(grabDistance, sceneSize * 0.01f, sceneSize * 2.0f);
    PanWorldPerPixel = 2.0f * d * glm::tan(glm::radians(Zoom) * 0.5f) / h;
  }
  void StopPanning() { IsPanning = false; }

  // Calculates the Front, Right and Up vectors from the Yaw and Pitch angles
  // Legacy function - now uses quaternion-based approach
  // Deprecated: Use updateCameraVectorsFromQuaternion() instead
  void updateCameraVectors() {
    // For backward compatibility, update quaternion from Euler angles
    initializeQuaternionFromEuler();
    // Use quaternion-based vector calculation
    updateCameraVectorsFromQuaternion();
  }

private:
  // Cubic easing function for smooth animation
  float easeOutCubic(float t) { return 1 - pow(1 - t, 3); }

  // Initialize quaternion from current Euler angles
  void initializeQuaternionFromEuler() {
    glm::quat yawQuat =
        glm::angleAxis(glm::radians(Yaw), glm::vec3(0.0f, 1.0f, 0.0f));
    glm::quat pitchQuat =
        glm::angleAxis(glm::radians(Pitch), glm::vec3(1.0f, 0.0f, 0.0f));
    Orientation = yawQuat * pitchQuat;
  }

  // Update camera vectors from quaternion orientation
  void updateCameraVectorsFromQuaternion() {
    // Convert quaternion to direction vectors
    glm::mat4 rotationMatrix = glm::mat4_cast(Orientation);
    Front = -glm::vec3(rotationMatrix[2]); // Negative Z axis
    Right = glm::vec3(rotationMatrix[0]);  // X axis
    Up = glm::vec3(rotationMatrix[1]);     // Y axis

    // Update Euler angles for backward compatibility
    updateEulerFromQuaternion();
  }

  // Update Euler angles from quaternion (for backward compatibility)
  void updateEulerFromQuaternion() {
    // Extract Euler angles from quaternion
    glm::vec3 euler = glm::eulerAngles(Orientation);
    Yaw = glm::degrees(euler.y);
    Pitch = glm::degrees(euler.x);

    // Normalize yaw to -180 to 180 range
    while (Yaw > 180.0f)
      Yaw -= 360.0f;
    while (Yaw < -180.0f)
      Yaw += 360.0f;
  }
};

#endif