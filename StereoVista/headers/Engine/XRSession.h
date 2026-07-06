#pragma once

// OpenXR support is Windows-only (matches the rest of the project).
#ifdef _WIN32

// Phase 7b: OpenXR bound to the app's EXISTING Vulkan device (XR_KHR_vulkan_enable2),
// no longer the GL context. The session shares rhi::Device's VkInstance /
// VkPhysicalDevice / VkDevice / graphics queue; the runtime hands back per-eye
// swapchain images as VkImage, which the renderer resolves the two multiview
// eyes into (the same path Phase 7a built for the window backbuffer).
//
// Crucially, NO xr* function is called until init() runs, and init() only runs
// when the user turns VR on. A desktop launch therefore never contacts an
// OpenXR runtime (no SteamVR spin-up) and costs nothing — VR stays a live GUI
// toggle exactly like the GL app. The device is made "XR-ready" up front by
// enabling the standard external-memory/semaphore/fence extensions (see
// rhi::Device::createLogicalDevice), so binding an existing device succeeds
// without ever having asked the runtime what it needs.
#define XR_USE_GRAPHICS_API_VULKAN

#include "RHI/VulkanCommon.h" // volk: Vulkan types, BEFORE openxr_platform.h

#include <openxr/openxr.h>
#include <openxr/openxr_platform.h>

#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "Engine/XRRuntimeInfo.h"

namespace rhi {
class Device;
}

namespace Engine {

// ---------------------------------------------------------------------------
// System OpenXR runtime probing (independent of any XRSession instance).
//
// These let the app explain *why* a session failed (e.g. the active runtime is
// Monado but its background service isn't running) and let the user switch to a
// different installed runtime — all without admin rights or system-wide changes.
// (Graphics-API-agnostic; unchanged across the GL->Vulkan port.)
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

// Per-eye camera the renderer consumes. `view` is EYE-FROM-REFERENCE-SPACE (the
// head/eye pose), meant to be composed with the scene camera by the app
// (combined = view * sceneView, i.e. eye-from-world); `proj` is already in the
// house reverse-Z + Y-flip convention (built from the XrFovf via
// renderer::frustumAsymmetric), ready to hand to the renderer as-is.
struct XREyePose {
    glm::mat4 view{ 1.0f };
    glm::mat4 proj{ 1.0f };
};

// Manages a single OpenXR session bound to the application's Vulkan device.
// All public methods are safe to call regardless of init state; they early-out
// when the session is not initialized or not in the expected state.
class XRSession {
public:
    XRSession()  = default;
    ~XRSession() { destroy(); }

    // Not copyable – owns XR handles and Vulkan image views.
    XRSession(const XRSession &)            = delete;
    XRSession &operator=(const XRSession &) = delete;

    // Attempt to initialize OpenXR against the already-created Vulkan device.
    // Returns true on success; on failure statusMessage() contains the reason
    // and the device is left untouched. The device must have been created
    // "XR-ready" (rhi::Device enables the interop extensions unconditionally).
    bool init(rhi::Device &device);

    // Release all OpenXR + Vulkan-view resources. Safe even if init() failed.
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

    // Populate per-eye eye-from-reference view + house projection for the
    // predicted display time. worldScale maps scene units to metres (1.0 = the
    // scene is in metres). Returns false if the view state is invalid (don't
    // render, call endFrame(false)).
    bool getEyePoses(XREyePose out[2], float nearZ, float farZ, float worldScale);

    // Acquire + wait the next swapchain image for one eye (0 = left, 1 = right)
    // and return the VkImageView the renderer draws that eye into (VK_NULL_HANDLE
    // on error). Call releaseSwapchainImage() after the GPU work is submitted.
    VkImageView acquireSwapchainImage(int eye);
    void        releaseSwapchainImage(int eye);

    // The runtime-owned VkImage currently acquired for one eye (valid between
    // acquireSwapchainImage() and releaseSwapchainImage()). The renderer needs
    // it for the UNDEFINED->COLOR_ATTACHMENT layout barrier before the resolve;
    // the VkImageView above is the attachment it renders through.
    VkImage acquiredColorImage(int eye) const {
        if (eye < 0 || eye > 1 || eyes_[eye].images.empty())
            return VK_NULL_HANDLE;
        return eyes_[eye].images[eyes_[eye].acquiredIndex].image;
    }

    // Submit the frame to the runtime. Pass rendered = false when the session
    // was not in a visible state this frame (no layers are submitted).
    void endFrame(bool rendered);

    // ---- Accessors ---------------------------------------------------------
    uint32_t   eyeWidth()  const { return eyeWidth_; }
    uint32_t   eyeHeight() const { return eyeHeight_; }
    VkExtent2D eyeExtent() const { return { eyeWidth_, eyeHeight_ }; }
    // Vulkan color format of the eye swapchains + whether it is an _SRGB format
    // (the renderer outputs LINEAR into an sRGB target so the hardware encodes,
    // else it applies the sRGB OETF itself, matching the window path).
    VkFormat swapchainFormat() const { return colorFormat_; }
    bool      swapchainIsSrgb() const { return colorIsSrgb_; }

    // Human-readable strings for the GUI.
    const std::string &statusMessage() const { return statusMsg_;   }
    const std::string &runtimeName()   const { return runtimeName_; }

private:
    // Initialisation stages (each returns false and sets statusMsg_ on error).
    bool createInstance();
    bool createSystemAndValidateDevice();
    bool createSession();
    bool createSwapchains();
    void destroySwapchains();

    // Convert an XrPosef to an eye-from-reference view matrix.
    static glm::mat4 poseToView(const XrPosef &pose, float worldScale);

    // ---- Vulkan device handles (owned by rhi::Device, not by us) -----------
    rhi::Device *device_   = nullptr;
    VkDevice     vkDevice_ = VK_NULL_HANDLE;

    // ---- XR handles --------------------------------------------------------
    XrInstance     instance_       = XR_NULL_HANDLE;
    XrSystemId     systemId_       = XR_NULL_SYSTEM_ID;
    XrSession      session_        = XR_NULL_HANDLE;
    XrSpace        referenceSpace_ = XR_NULL_HANDLE;
    XrSessionState sessionState_   = XR_SESSION_STATE_UNKNOWN;

    // enable2 entry points (resolved via xrGetInstanceProcAddr).
    PFN_xrGetVulkanGraphicsRequirements2KHR pfnGetVulkanRequirements_ = nullptr;
    PFN_xrGetVulkanGraphicsDevice2KHR       pfnGetVulkanDevice_       = nullptr;

    struct EyeSwapchain {
        XrSwapchain                            handle = XR_NULL_HANDLE;
        std::vector<XrSwapchainImageVulkanKHR> images;
        std::vector<VkImageView>               views; // one per image, on our device
        uint32_t                               acquiredIndex = 0;
    } eyes_[2];

    uint32_t     eyeWidth_   = 0;
    uint32_t     eyeHeight_  = 0;
    VkFormat     colorFormat_ = VK_FORMAT_UNDEFINED;
    bool         colorIsSrgb_ = false;
    XrFrameState frameState_  = { XR_TYPE_FRAME_STATE };
    XrView       views_[2]    = { {XR_TYPE_VIEW}, {XR_TYPE_VIEW} };

    // ---- State flags -------------------------------------------------------
    bool initialized_    = false;
    bool running_        = false;
    bool sessionVisible_ = false;

    std::string statusMsg_;
    std::string runtimeName_;
};

} // namespace Engine

#endif // _WIN32
