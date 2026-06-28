#pragma once

// OpenXR support is Windows-only (matches the rest of the project).
#ifdef _WIN32

// Must precede openxr_platform.h so it knows to include the Win32/WGL bindings.
#define XR_USE_PLATFORM_WIN32
#define XR_USE_GRAPHICS_API_OPENGL

#include <Windows.h>
#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "Engine/XRRuntimeInfo.h"

namespace Engine {

// ---------------------------------------------------------------------------
// System OpenXR runtime probing (independent of any XRSession instance).
//
// These let the app explain *why* a session failed (e.g. the active runtime is
// Monado but its background service isn't running) and let the user switch to a
// different installed runtime — all without admin rights or system-wide changes.
// ---------------------------------------------------------------------------

// Inspect the system: which runtime is active, where that choice comes from, and
// which runtimes are installed. Safe to call any time, even with no session.
XRDiagnostics probeXRRuntimes();

// Force THIS process to use a specific runtime by setting the XR_RUNTIME_JSON
// environment variable, which the OpenXR loader honours ahead of the system
// default. Per-process only — nothing system-wide is modified, no admin needed.
// Pass an empty string to clear the override and fall back to the system default.
// (Note: the loader ignores this when the process runs elevated/as admin.)
void setXRRuntimeOverride(const std::string &manifestPath);

// The XR_RUNTIME_JSON override currently in effect for this process ("" = none).
std::string getXRRuntimeOverride();

// Manages a single OpenXR session bound to the application's OpenGL context.
// All public methods are safe to call regardless of init state; they early-out
// when the session is not initialized or not in the expected state.
class XRSession {
public:
    XRSession()  = default;
    ~XRSession() { destroy(); }

    // Not copyable – owns XR handles and GL objects.
    XRSession(const XRSession &)            = delete;
    XRSession &operator=(const XRSession &) = delete;

    // Attempt to initialize OpenXR. hdc / hglrc must be the current OpenGL
    // context handles (obtained from the GLFW window on Windows).
    // Returns true on success; on failure statusMessage() contains the reason.
    bool init(HDC hdc, HGLRC hglrc);

    // Release all OpenXR and GL resources.  Safe to call even if init() failed.
    void destroy();

    bool isInitialized()    const { return initialized_; }
    bool isRunning()        const { return running_; }
    bool isSessionVisible() const { return sessionVisible_; }

    // ---- Per-frame protocol (call in this order each frame) ----------------

    // Process pending XR events and advance the session state machine.
    // Returns false when the session has ended and destroy() should be called.
    bool pollEvents();

    // Predict the display time and begin the XR frame.
    // Returns false when the session is not yet visible (skip rendering this
    // frame but still call endFrame(false)).
    bool beginFrame();

    // Populate per-eye projection and view matrices for the predicted display
    // time. worldScale maps scene units to metres (1.0 = scene is in metres).
    // Returns false if the view state is invalid (don't render, call
    // endFrame(false)).
    bool getEyePoses(glm::mat4 outProj[2], glm::mat4 outRefView[2],
                     float nearZ, float farZ, float worldScale);

    // Acquire the next swapchain image for one eye (0 = left, 1 = right).
    // Returns the OpenGL texture name for the acquired image, or 0 on error.
    // The caller must call releaseSwapchainImage() after rendering into it.
    GLuint acquireSwapchainImage(int eye);
    void   releaseSwapchainImage(int eye);

    // Submit the frame to the runtime.  Pass rendered = false when the session
    // was not in a visible state this frame (no layers are submitted).
    void endFrame(bool rendered);

    // ---- Accessors ---------------------------------------------------------
    uint32_t    eyeWidth()  const { return eyeWidth_;  }
    uint32_t    eyeHeight() const { return eyeHeight_; }
    // Shared FBO used for per-eye rendering. Attach a swapchain texture to
    // GL_COLOR_ATTACHMENT0 before rendering each eye.
    GLuint      fbo()       const { return fbo_;       }
    GLuint      depthRbo()  const { return depthRbo_;  }

    // Human-readable strings for the GUI.
    const std::string &statusMessage() const { return statusMsg_;     }
    const std::string &runtimeName()   const { return runtimeName_;   }

private:
    // Initialisation stages (each returns false and sets statusMsg_ on error).
    bool createInstance();
    bool createSystem();
    bool createSession(HDC hdc, HGLRC hglrc);
    bool createSwapchains();
    bool createGLObjects();

    void destroyGLObjects();

    // Convert an XrFovf to an OpenGL-style asymmetric frustum projection.
    static glm::mat4 fovToProjection(const XrFovf &fov, float nearZ, float farZ);
    // Convert an XrPosef to a view matrix (world→eye).
    static glm::mat4 poseToView(const XrPosef &pose, float worldScale);

    // ---- XR handles --------------------------------------------------------
    XrInstance      instance_       = XR_NULL_HANDLE;
    XrSystemId      systemId_       = XR_NULL_SYSTEM_ID;
    XrSession       session_        = XR_NULL_HANDLE;
    XrSpace         referenceSpace_ = XR_NULL_HANDLE;
    XrSessionState  sessionState_   = XR_SESSION_STATE_UNKNOWN;

    struct EyeSwapchain {
        XrSwapchain                            handle = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageOpenGLKHR> images;
        uint32_t                               acquiredIndex = 0;
    } eyes_[2];

    uint32_t     eyeWidth_  = 0;
    uint32_t     eyeHeight_ = 0;
    XrFrameState frameState_  = { XR_TYPE_FRAME_STATE };
    XrView       views_[2]    = { {XR_TYPE_VIEW}, {XR_TYPE_VIEW} };

    // ---- GL objects --------------------------------------------------------
    GLuint fbo_      = 0;
    GLuint depthRbo_ = 0;

    // ---- State flags -------------------------------------------------------
    bool initialized_    = false;
    bool running_        = false;
    bool sessionVisible_ = false;

    std::string statusMsg_;
    std::string runtimeName_;
};

} // namespace Engine

#endif // _WIN32
