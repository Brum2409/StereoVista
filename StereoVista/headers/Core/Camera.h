#ifndef CAMERA_H
#define CAMERA_H

#include "Engine/Core.h"
#include <functional>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

enum Camera_Movement {
    FORWARD,
    BACKWARD,
    LEFT,
    RIGHT,
    UP,
    DOWN
};

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
        glm::quat orientation;  // Quaternion representation
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

    // Scroll parameters
    float scrollMomentum = 0.5f;
    float maxScrollVelocity = 3.0f;
    float scrollDeceleration = 5.0f;
    bool useSmoothScrolling = true;
    float scrollVelocity = 0.0f;

    // Orbit parameters
    glm::vec3 OrbitPoint;
    float OrbitDistance;
    bool IsOrbiting;
    bool IsPanning;

    // Animation parameters
    bool IsAnimating;
    glm::vec3 AnimationStartPosition;
    glm::vec3 AnimationEndPosition;
    glm::quat AnimationStartOrientation;
    glm::quat AnimationEndOrientation;
    float AnimationProgress;
    float AnimationDuration;

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

    std::function<void()> centeringCompletedCallback;

    // Constructor with default values
    Camera(glm::vec3 position = glm::vec3(0.0f, 0.0f, 3.0f), glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f), float yaw = YAW, float pitch = PITCH)
        : isMoving(false), isLookingAtEmptySpace(false), Front(glm::vec3(0.0f, 0.0f, -1.0f)), MovementSpeed(SPEED), MouseSensitivity(SENSITIVITY), Zoom(ZOOM),
        IsOrbiting(false), IsPanning(false), IsAnimating(false), AnimationProgress(0.0f), AnimationDuration(0.5f), centeringCompletedCallback(nullptr)
    {
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
    glm::mat4 GetProjectionMatrix(float aspectRatio, float nearPlane, float farPlane) const {
        return glm::perspective(glm::radians(Zoom), aspectRatio, nearPlane, farPlane);
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
        return state;
    }

    // Sets the camera state
    void SetState(const CameraState& state) {
        Position = state.position;
        Front = state.front;
        Up = state.up;
        Yaw = state.yaw;
        Pitch = state.pitch;
        Zoom = state.zoom;
        initializeQuaternionFromEuler();
        updateCameraVectorsFromQuaternion();
    }

    // Updates the cursor information for cursor-based navigation
    void UpdateCursorInfo(const glm::vec3& pos, bool valid) {
        cursorPosition = pos;
        cursorValid = valid;
    }

    // Updates the distance to the nearest visible object
    void UpdateDistanceToObject(float distance) {
        distanceToNearestObject = distance;
        distanceUpdated = true;
    }

    // Synchronize quaternion with current Euler angles (SpaceMouse -> Normal transition)
    void SynchronizeQuaternionFromEuler() {
        initializeQuaternionFromEuler();
        updateCameraVectorsFromQuaternion();
    }
    
    // Synchronize Euler angles with current quaternion (Normal -> SpaceMouse transition)  
    void SynchronizeEulerFromQuaternion() {
        updateEulerFromQuaternion();
    }

    // Creates an offset projection matrix for stereo rendering (This version of stereo is not correct. AsymFrustrum should be used instead)
    glm::mat4 offsetProjection(glm::mat4& centerProjection, float separation, float convergence) const {
        glm::mat4 o = glm::mat4(centerProjection);
        o[2][0] = o[2][0] - separation;
        o[3][0] = o[3][0] - separation * convergence;
        return o;
    }

    // Frustum culling check
    bool isInFrustum(const glm::vec3& point, float radius, glm::mat4 viewProj) const {
        for (int i = 0; i < 6; ++i) {
            glm::vec4 plane(
                viewProj[0][3] + (i % 2 == 0 ? viewProj[0][i / 2] : -viewProj[0][i / 2]),
                viewProj[1][3] + (i % 2 == 0 ? viewProj[1][i / 2] : -viewProj[1][i / 2]),
                viewProj[2][3] + (i % 2 == 0 ? viewProj[2][i / 2] : -viewProj[2][i / 2]),
                viewProj[3][3] + (i % 2 == 0 ? viewProj[3][i / 2] : -viewProj[3][i / 2])
            );
            plane /= glm::length(glm::vec3(plane));

            if (glm::dot(point, glm::vec3(plane)) + plane.w <= -radius) {
                return false;
            }
        }
        return true;
    }

    // Processes keyboard input and moves the camera accordingly
    void ProcessKeyboard(Camera_Movement direction, float deltaTime) {
        if (IsAnimating) return;

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

    // Adjusts camera movement speed based on distance to nearest object
    // Uses logarithmic scaling to create natural acceleration/deceleration
    void AdjustMovementSpeed(float distanceToNearestObject, float modelSize, float farPlane) {
        if (!isMoving) return;

        float minDistance = modelSize * 0.1f;
        float maxDistance = modelSize * 10.0f;

        maxSpeed = modelSize * 1.5 * speedFactor;
        minSpeed = modelSize * 0.1 * speedFactor;

        minDistance = glm::max(minDistance, 0.01f);
        maxDistance = glm::max(maxDistance, minDistance * 10.0f);

        // Normalize distance into 0-1 range
        float normalizedDistance = (distanceToNearestObject - minDistance) / (maxDistance - minDistance);
        normalizedDistance = glm::clamp(normalizedDistance, 0.0f, 1.0f);

        // Apply logarithmic scaling for smooth speed transitions
        float logFactor = 4.0f;
        float t = glm::log(1 + normalizedDistance * (exp(logFactor) - 1)) / logFactor;

        float newTargetSpeed = minSpeed + t * (maxSpeed - minSpeed);
        newTargetSpeed = glm::clamp(newTargetSpeed, minSpeed, maxSpeed);

        isLookingAtEmptySpace = (distanceToNearestObject == farPlane);

        if (isLookingAtEmptySpace) {
            // Gradually increase speed when looking at empty space
            float newSpeed = MovementSpeed += MovementSpeed / 50.0f;
            newSpeed = glm::clamp(newSpeed, minSpeed, maxSpeed);
            MovementSpeed = newSpeed;
        }
        else {
            if (newTargetSpeed > MovementSpeed) {
                // Gradually accelerate
                MovementSpeed += MovementSpeed / 50;
            }
            else {
                // Immediately decelerate when needed
                MovementSpeed = newTargetSpeed;
            }
        }
    }

    // Calculates scroll factor based on distance, similar to movement speed
    float CalculateScrollFactor(float modelSize) {
        if (!distanceUpdated) {
            return 1.0f;
        }

        float minDistance = modelSize * 0.1f;
        float maxDistance = modelSize * 10.0f;

        minDistance = glm::max(minDistance, 0.01f);
        maxDistance = glm::max(maxDistance, minDistance * 10.0f);

        float normalizedDistance = (distanceToNearestObject - minDistance) / (maxDistance - minDistance);
        normalizedDistance = glm::clamp(normalizedDistance, 0.0f, 1.0f);

        // Same logarithmic function as movement speed for consistency
        float logFactor = 4.0f;
        float t = glm::log(1 + normalizedDistance * (exp(logFactor) - 1)) / logFactor;

        float minScrollFactor = 0.1f;
        float maxScrollFactor = 3.0f;

        float scrollFactor = minScrollFactor + t * (maxScrollFactor - minScrollFactor);

        if (isLookingAtEmptySpace) {
            scrollFactor *= 1.5f;
        }

        return scrollFactor;
    }

    // Processes mouse movement for rotation, orbiting, and panning
    void ProcessMouseMovement(float xoffset, float yoffset, bool constrainPitch = true) {
        if (IsAnimating) return;

        xoffset *= MouseSensitivity;
        yoffset *= MouseSensitivity;

        if (IsOrbiting) {
            if (orbitAroundCursor) {
                // Orbit around cursor position using pure quaternion rotation
                glm::vec3 orbitToCamera = Position - OrbitPoint;
                float distance = glm::length(orbitToCamera);

                // Create rotation quaternions for orbit movement
                glm::quat yawRotation = glm::angleAxis(-glm::radians(xoffset), WorldUp);
                glm::vec3 rightAxis = glm::normalize(glm::cross(orbitToCamera, WorldUp));
                glm::quat pitchRotation = glm::angleAxis(-glm::radians(yoffset), rightAxis);

                // Apply orbit rotation to position
                orbitToCamera = pitchRotation * (yawRotation * orbitToCamera);
                orbitToCamera = glm::normalize(orbitToCamera) * distance;
                Position = OrbitPoint + orbitToCamera;

                // Apply same rotation to camera orientation
                Orientation = pitchRotation * yawRotation * Orientation;
                Orientation = glm::normalize(Orientation);
                updateCameraVectorsFromQuaternion();
            }
            else {
                // Standard orbit mode using quaternions
                glm::vec3 toCamera = Position - OrbitPoint;
                float distance = glm::length(toCamera);

                // Create rotation quaternions
                glm::quat yawRotation = glm::angleAxis(-glm::radians(xoffset), WorldUp);
                glm::vec3 rightAxis = glm::normalize(glm::cross(toCamera, WorldUp));
                glm::quat pitchRotation = glm::angleAxis(-glm::radians(yoffset), rightAxis);

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
        }
        else if (IsPanning) {
            // Pan camera in view plane using quaternion-derived vectors
            float panFactor = OrbitDistance * 0.01f;
            panFactor = glm::max(panFactor, 0.001f);

            // Use Right and Up vectors directly from quaternion
            Position -= Right * xoffset * panFactor;
            Position -= Up * yoffset * panFactor;

            OrbitPoint = Position + Front * OrbitDistance;
        }
        else {
            // Standard free camera rotation using quaternions (eliminates gimbal lock)
            // Create rotation quaternions for yaw and pitch
            glm::quat yawRotation = glm::angleAxis(glm::radians(-xoffset), WorldUp);
            // Use local X axis for pitch to maintain consistent up/down movement
            glm::vec3 localRight = glm::vec3(1.0f, 0.0f, 0.0f);
            glm::quat pitchRotation = glm::angleAxis(glm::radians(yoffset), localRight);
            
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
                    // Calculate how much rotation we can apply to reach exactly 88.5 degrees
                    float targetPitch = (testPitch > 88.5f) ? 88.5f : -88.5f;
                    float remainingPitch = targetPitch - currentPitch;
                    
                    if (abs(remainingPitch) > 0.01f) { // Only apply if there's meaningful rotation left
                        glm::quat constrainedPitchRotation = glm::angleAxis(glm::radians(remainingPitch), localRight);
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

    // Processes mouse scroll for zooming
    void ProcessMouseScroll(float yoffset, const glm::vec3& backgroundCursorPos = glm::vec3(0.0f), bool hasBackgroundCursor = false) {
        if (IsAnimating) return;

        float modelSize = 1.0f;
        float scrollFactor = CalculateScrollFactor(modelSize);
        float scaledYOffset = yoffset * scrollFactor;

        if (!useSmoothScrolling) {
            // Direct movement without momentum
            glm::vec3 targetPosition;
            bool hasValidTarget = false;

            if (zoomToCursor && cursorValid) {
                // Use valid 3D cursor position
                targetPosition = cursorPosition;
                hasValidTarget = true;
            }
            else if (zoomToCursor && !cursorValid && hasBackgroundCursor) {
                // Use background cursor position from cursor manager when over empty space
                targetPosition = backgroundCursorPos;
                hasValidTarget = true;
            }

            if (hasValidTarget) {
                // Zoom toward/away from target point
                glm::vec3 dirToTarget = targetPosition - Position;
                float distance = glm::length(dirToTarget);

                if (distance > 0.01f) {
                    dirToTarget = glm::normalize(dirToTarget);
                    Position += dirToTarget * scaledYOffset * MovementSpeed * 0.1f;
                }
                else {
                    Position += Front * scaledYOffset * MovementSpeed * 0.1f;
                }
            }
            else {
                // Standard zoom along Front vector
                Position += Front * scaledYOffset * MovementSpeed * 0.1f;
            }

            if (IsOrbiting) {
                OrbitPoint = Position + Front * OrbitDistance;
            }
            return;
        }

        // Setup for smooth scrolling with momentum
        float currentTime = glfwGetTime();
        float deltaTime = currentTime - lastScrollTime;
        lastScrollTime = currentTime;

        scrollVelocity += scaledYOffset * scrollMomentum;
        scrollVelocity = glm::clamp(scrollVelocity, -maxScrollVelocity, maxScrollVelocity);

        if (zoomToCursor && cursorValid) {
            scrollTargetPos = cursorPosition;
            isScrollingToCursor = true;
        }
        else if (zoomToCursor && !cursorValid && hasBackgroundCursor) {
            // Use background cursor position from cursor manager when over empty space
            scrollTargetPos = backgroundCursorPos;
            isScrollingToCursor = true;
        }
        else {
            isScrollingToCursor = false;
        }
    }

    // Updates smooth scrolling movement over time
    void UpdateScrolling(float deltaTime) {
        if (scrollVelocity != 0.0f) {
            float modelSize = 1.0f;
            float scrollFactor = CalculateScrollFactor(modelSize);
            float adjustedVelocity = scrollVelocity * scrollFactor;

            if (isScrollingToCursor) {
                // Zoom toward cursor position
                glm::vec3 dirToCursor = scrollTargetPos - Position;
                float distance = glm::length(dirToCursor);

                if (distance > 0.01f) {
                    dirToCursor = glm::normalize(dirToCursor);
                    Position += dirToCursor * adjustedVelocity * MovementSpeed * deltaTime;
                }
                else {
                    Position += Front * adjustedVelocity * MovementSpeed * deltaTime;
                    isScrollingToCursor = false;
                }
            }
            else {
                // Standard zoom along Front vector
                Position += Front * adjustedVelocity * MovementSpeed * deltaTime;
            }

            // Apply deceleration
            float deceleration = scrollDeceleration * deltaTime * scrollFactor;
            if (abs(scrollVelocity) <= deceleration) {
                scrollVelocity = 0.0f;
            }
            else {
                scrollVelocity -= glm::sign(scrollVelocity) * deceleration;
            }

            if (IsOrbiting) {
                OrbitPoint = Position + Front * OrbitDistance;
            }
        }
    }

    // Sets orbit distance and updates orbit point
    void SetOrbitPoint(float distance) {
        OrbitDistance = distance;
        OrbitPoint = Position + Front * OrbitDistance;
    }

    // Sets orbit point directly and calculates distance
    void SetOrbitPointDirectly(const glm::vec3& point) {
        OrbitPoint = point;
        OrbitDistance = glm::length(Position - OrbitPoint);
    }

    // Starts animation to center camera on target point
    void StartCenteringAnimation(const glm::vec3& targetPoint) {
        IsAnimating = true;
        AnimationStartPosition = Position;
        AnimationStartOrientation = Orientation;

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
        OrbitDistance = initialDistance;
    }

    // Updates camera animation with smooth easing
    void UpdateAnimation(float deltaTime) {
        if (!IsAnimating) return;

        AnimationProgress += deltaTime / AnimationDuration;

        if (AnimationProgress >= 1.0f) {
            // Animation complete
            Position = AnimationEndPosition;
            Orientation = AnimationEndOrientation;
            IsAnimating = false;
            updateCameraVectorsFromQuaternion();

            OrbitPoint = Position + Front * OrbitDistance;

            if (centeringCompletedCallback) {
                centeringCompletedCallback();
            }
        }
        else {
            // Apply cubic easing function
            float t = easeOutCubic(AnimationProgress);

            // Interpolate position and orientation using SLERP for smooth rotation
            Position = glm::mix(AnimationStartPosition, AnimationEndPosition, t);
            Orientation = glm::normalize(glm::slerp(AnimationStartOrientation, AnimationEndOrientation, t));
            
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
    void StartPanning() { IsPanning = true; }
    void StopPanning() { IsPanning = false; }

    // Calculates distance to the nearest visible object using depth buffer sampling
    float getDistanceToNearestObject(const Camera& camera, const glm::mat4& projection, const glm::mat4& view,
        const float farPlane, const int windowWidth, const int windowHeight) const {
        // Validate OpenGL context and window dimensions
        if (windowWidth <= 0 || windowHeight <= 0) {
            return farPlane;
        }

        // Check if OpenGL context is current (basic validation)
        GLint currentFBO;
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFBO);
        GLenum error = glGetError();
        if (error != GL_NO_ERROR) {
            return farPlane; // OpenGL context issue
        }

        const int numSamples = 9; // 3x3 sampling grid
        const int sampleOffset = 100; // Pixel offset from center
        float minDepth = 1.0f;

        // Sample depth buffer at multiple points around screen center
        for (int i = -1; i <= 1; i++) {
            for (int j = -1; j <= 1; j++) {
                int x = windowWidth / 2 + i * sampleOffset;
                int y = windowHeight / 2 + j * sampleOffset;
                
                // Bounds checking for pixel coordinates
                if (x < 0 || x >= windowWidth || y < 0 || y >= windowHeight) {
                    continue; // Skip out-of-bounds pixels
                }

                float depth = 1.0f; // Default to far plane
                glReadPixels(x, y, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);
                
                // Check for OpenGL errors after glReadPixels
                error = glGetError();
                if (error != GL_NO_ERROR) {
                    continue; // Skip this sample on error
                }
                
                minDepth = std::min(minDepth, depth);
            }
        }

        if (minDepth == 1.0f) {
            return farPlane; // No object detected
        }

        // Convert depth to world space distance
        glm::vec4 ndc = glm::vec4(0.0f, 0.0f, minDepth * 2.0f - 1.0f, 1.0f);
        glm::mat4 invPV = glm::inverse(projection * view);
        glm::vec4 worldPos = invPV * ndc;
        worldPos /= worldPos.w;

        float distance = glm::distance(camera.Position, glm::vec3(worldPos));

        return distance;
    }

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
    float lastScrollTime = 0.0f;

    // Cubic easing function for smooth animation
    float easeOutCubic(float t) {
        return 1 - pow(1 - t, 3);
    }

    // Initialize quaternion from current Euler angles
    void initializeQuaternionFromEuler() {
        glm::quat yawQuat = glm::angleAxis(glm::radians(Yaw), glm::vec3(0.0f, 1.0f, 0.0f));
        glm::quat pitchQuat = glm::angleAxis(glm::radians(Pitch), glm::vec3(1.0f, 0.0f, 0.0f));
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
        while (Yaw > 180.0f) Yaw -= 360.0f;
        while (Yaw < -180.0f) Yaw += 360.0f;
    }
};

#endif