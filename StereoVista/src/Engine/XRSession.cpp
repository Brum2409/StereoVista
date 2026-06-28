// OpenXR session management for StereoVista.
// Compiled only on Windows; the rest of the project is also Windows-only.
#ifdef _WIN32

#include "Engine/XRSession.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <cmath>
#include <iostream>
#include <cstring>

#include <algorithm>
#include <cctype>
#include <cstdlib>     // _wputenv_s
#include <filesystem>

// RegGetValueW / registry access lives in Advapi32; make sure it is linked even
// if it is not part of the project's default library set.
#pragma comment(lib, "Advapi32.lib")

namespace Engine {

// ===========================================================================
// System OpenXR runtime probing
// ===========================================================================
namespace {

std::string wideToUtf8(const std::wstring &w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                                nullptr, 0, nullptr, nullptr);
    std::string s(n, '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(),
                        s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring utf8ToWide(const std::string &s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), w.data(), n);
    return w;
}

std::string toLower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return (char)std::tolower(c); });
    return s;
}

// Read an environment variable as a wide string ("" if unset).
std::wstring getEnvW(const wchar_t *name) {
    DWORD n = GetEnvironmentVariableW(name, nullptr, 0);
    if (n == 0) return {};
    std::wstring v(n, L'\0');
    DWORD got = GetEnvironmentVariableW(name, v.data(), n);
    v.resize(got);
    return v;
}

// Read a REG_SZ value from the 64-bit registry view ("" if missing).
std::wstring regReadSZ(HKEY root, const wchar_t *subkey, const wchar_t *value) {
    wchar_t buf[1024];
    DWORD   cb = sizeof(buf);
    LSTATUS st = RegGetValueW(root, subkey, value,
                              RRF_RT_REG_SZ | RRF_SUBKEY_WOW6464KEY,
                              nullptr, buf, &cb);
    if (st != ERROR_SUCCESS) return {};
    return std::wstring(buf);
}

// Map a runtime manifest path to a friendly name + whether it is service-based
// (needs a separate background process running to work).
void classifyRuntime(const std::string &pathUtf8,
                     std::string &nameOut, bool &serviceBasedOut) {
    const std::string p = toLower(pathUtf8);
    auto has = [&](const char *needle) { return p.find(needle) != std::string::npos; };

    serviceBasedOut = false;
    if (has("monado")) {
        serviceBasedOut = true;
        nameOut = has("immersa") ? "Monado (from ImmersaStudio)" : "Monado";
    } else if (has("steamxr") || has("steamvr")) {
        nameOut = "SteamVR";
    } else if (has("oculus") || has("meta")) {
        nameOut = "Oculus / Meta";
    } else if (has("mixedrealityruntime") || has("windowsmr") || has("\\wmr")) {
        nameOut = "Windows Mixed Reality";
    } else if (has("varjo")) {
        nameOut = "Varjo";
    } else if (has("vive") || has("htc")) {
        nameOut = "VIVE OpenXR";
    } else {
        nameOut = "OpenXR runtime";
    }
}

} // namespace

XRDiagnostics probeXRRuntimes() {
    XRDiagnostics d;

    // Is the loader itself present? (DONT_RESOLVE keeps this side-effect free.)
    if (HMODULE h = LoadLibraryExW(L"openxr_loader.dll", nullptr,
                                   DONT_RESOLVE_DLL_REFERENCES)) {
        d.loaderPresent = true;
        FreeLibrary(h);
    }

    // The OS-wide default comes from the registry (per-user key wins over the
    // machine-wide one). This is independent of any per-process override, so it
    // stays fixed while the user switches runtimes in the picker.
    std::string defaultSource;
    if (std::wstring hkcu = regReadSZ(HKEY_CURRENT_USER,
            L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime"); !hkcu.empty()) {
        d.systemDefaultPath = wideToUtf8(hkcu);
        defaultSource       = "HKCU registry";
    } else if (std::wstring hklm = regReadSZ(HKEY_LOCAL_MACHINE,
            L"SOFTWARE\\Khronos\\OpenXR\\1", L"ActiveRuntime"); !hklm.empty()) {
        d.systemDefaultPath = wideToUtf8(hklm);
        defaultSource       = "HKLM registry";
    }
    bool dummyService = false;
    if (!d.systemDefaultPath.empty())
        classifyRuntime(d.systemDefaultPath, d.systemDefaultName, dummyService);

    // The *effective* active runtime is what the loader will actually use right
    // now: an explicit XR_RUNTIME_JSON override (set by our picker, or the
    // environment) takes precedence over the system default.
    if (std::wstring env = getEnvW(L"XR_RUNTIME_JSON"); !env.empty()) {
        d.activeRuntimePath   = wideToUtf8(env);
        d.activeRuntimeSource = "XR_RUNTIME_JSON";
    } else if (!d.systemDefaultPath.empty()) {
        d.activeRuntimePath   = d.systemDefaultPath;
        d.activeRuntimeSource = defaultSource;
    }
    if (!d.activeRuntimePath.empty())
        classifyRuntime(d.activeRuntimePath, d.activeRuntimeName, d.activeServiceBased);

    // Build the STABLE list of installed runtimes: the standard install
    // locations plus the system default. The transient override is deliberately
    // NOT added here, so switching runtimes never adds/removes list entries.
    std::vector<std::string> candidates;
    auto addUnder = [&](const wchar_t *envName, const wchar_t *tail) {
        std::wstring base = getEnvW(envName);
        if (!base.empty()) candidates.push_back(wideToUtf8(base + tail));
    };
    addUnder(L"ProgramFiles(x86)", L"\\Steam\\steamapps\\common\\SteamVR\\steamxr_win64.json");
    addUnder(L"ProgramW6432",      L"\\Oculus\\Support\\oculus-runtime\\oculus_openxr_64.json");
    addUnder(L"ProgramFiles",      L"\\Oculus\\Support\\oculus-runtime\\oculus_openxr_64.json");
    addUnder(L"SystemRoot",        L"\\System32\\MixedRealityRuntime.json");
    addUnder(L"ProgramData",       L"\\Varjo\\MRRuntime\\varjo-openxr\\VarjoOpenXR.json");
    // Always include the system default (e.g. Monado) so it stays listed even
    // while a different runtime is active.
    if (!d.systemDefaultPath.empty()) candidates.push_back(d.systemDefaultPath);

    const std::string activeLower  = toLower(d.activeRuntimePath);
    const std::string defaultLower = toLower(d.systemDefaultPath);
    for (const std::string &c : candidates) {
        std::error_code ec;
        if (!std::filesystem::exists(std::filesystem::u8path(c), ec)) continue;

        const std::string cl = toLower(c);
        bool dup = false;
        for (const XRRuntimeInfo &r : d.runtimes)
            if (toLower(r.manifestPath) == cl) { dup = true; break; }
        if (dup) continue;

        XRRuntimeInfo info;
        info.manifestPath   = c;
        info.present        = true;
        info.isActive       = (cl == activeLower);
        info.isSystemDefault = (cl == defaultLower);
        classifyRuntime(c, info.name, info.serviceBased);
        d.runtimes.push_back(std::move(info));
    }
    return d;
}

void setXRRuntimeOverride(const std::string &manifestPath) {
    if (manifestPath.empty()) {
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr); // remove from process env
        _wputenv_s(L"XR_RUNTIME_JSON", L"");                  // keep CRT copy in sync
    } else {
        std::wstring w = utf8ToWide(manifestPath);
        // The loader reads this via GetEnvironmentVariableW; set both the Win32
        // block and the CRT copy so they cannot disagree.
        SetEnvironmentVariableW(L"XR_RUNTIME_JSON", w.c_str());
        _wputenv_s(L"XR_RUNTIME_JSON", w.c_str());
    }
}

std::string getXRRuntimeOverride() {
    return wideToUtf8(getEnvW(L"XR_RUNTIME_JSON"));
}

// ---------------------------------------------------------------------------
// Public: init / destroy
// ---------------------------------------------------------------------------

bool XRSession::init(HDC hdc, HGLRC hglrc) {
    // Quick DLL presence check so we report a friendly error rather than
    // triggering the delay-load failure handler.
    HMODULE hCheck = LoadLibraryExW(L"openxr_loader.dll", NULL,
                                    DONT_RESOLVE_DLL_REFERENCES);
    if (!hCheck) {
        statusMsg_ = "openxr_loader.dll not found. Install an OpenXR runtime "
                     "(e.g., SteamVR, Meta Quest Link, Windows Mixed Reality).";
        return false;
    }
    FreeLibrary(hCheck);

    if (!createInstance())         return false;
    if (!createSystem())         { destroy(); return false; }
    if (!createSession(hdc, hglrc)) { destroy(); return false; }
    if (!createSwapchains())     { destroy(); return false; }
    if (!createGLObjects())      { destroy(); return false; }

    initialized_ = true;
    statusMsg_   = "OpenXR session active – " + runtimeName_;
    std::cout << "[XR] Session initialised: " << runtimeName_ << "  "
              << eyeWidth_ << "x" << eyeHeight_ << " per eye\n";
    return true;
}

void XRSession::destroy() {
    destroyGLObjects();

    for (int i = 0; i < 2; ++i) {
        if (eyes_[i].handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(eyes_[i].handle);
            eyes_[i].handle = XR_NULL_HANDLE;
        }
        eyes_[i].images.clear();
    }

    // XrSpace is a child of XrSession — must be destroyed before the session.
    if (referenceSpace_ != XR_NULL_HANDLE) {
        xrDestroySpace(referenceSpace_);
        referenceSpace_ = XR_NULL_HANDLE;
    }

    if (session_ != XR_NULL_HANDLE) {
        if (running_) {
            xrRequestExitSession(session_);
        }
        xrDestroySession(session_);
        session_ = XR_NULL_HANDLE;
    }

    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
    }

    systemId_      = XR_NULL_SYSTEM_ID;
    sessionState_  = XR_SESSION_STATE_UNKNOWN;
    initialized_   = false;
    running_       = false;
    sessionVisible_= false;
    eyeWidth_      = 0;
    eyeHeight_     = 0;

    statusMsg_     = "OpenXR session destroyed.";
}

// ---------------------------------------------------------------------------
// Public: per-frame protocol
// ---------------------------------------------------------------------------

bool XRSession::pollEvents() {
    if (!initialized_) return false;

    XrEventDataBuffer event{ XR_TYPE_EVENT_DATA_BUFFER };
    while (xrPollEvent(instance_, &event) == XR_SUCCESS) {
        switch (event.type) {
        case XR_TYPE_EVENT_DATA_SESSION_STATE_CHANGED: {
            auto &e = *reinterpret_cast<XrEventDataSessionStateChanged *>(&event);
            sessionState_ = e.state;
            std::cout << "[XR] Session state → " << e.state << "\n";

            switch (e.state) {
            case XR_SESSION_STATE_READY:
                if (!running_) {
                    XrSessionBeginInfo bi{ XR_TYPE_SESSION_BEGIN_INFO };
                    bi.primaryViewConfigurationType =
                        XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
                    if (xrBeginSession(session_, &bi) == XR_SUCCESS) {
                        running_ = true;
                        statusMsg_ = "OpenXR running – " + runtimeName_;
                        std::cout << "[XR] xrBeginSession OK\n";
                    }
                }
                break;

            case XR_SESSION_STATE_SYNCHRONIZED:
            case XR_SESSION_STATE_VISIBLE:
            case XR_SESSION_STATE_FOCUSED:
                sessionVisible_ = (e.state >= XR_SESSION_STATE_VISIBLE);
                break;

            case XR_SESSION_STATE_STOPPING:
                if (running_) {
                    xrEndSession(session_);
                    running_        = false;
                    sessionVisible_ = false;
                    statusMsg_      = "OpenXR session stopped.";
                }
                break;

            case XR_SESSION_STATE_EXITING:
            case XR_SESSION_STATE_LOSS_PENDING:
                running_        = false;
                sessionVisible_ = false;
                statusMsg_      = "OpenXR session ended.";
                return false; // signal caller to destroy the session

            default:
                break;
            }
            break;
        }

        case XR_TYPE_EVENT_DATA_INSTANCE_LOSS_PENDING:
            running_        = false;
            sessionVisible_ = false;
            statusMsg_      = "OpenXR instance loss pending.";
            return false;

        default:
            break;
        }

        event = { XR_TYPE_EVENT_DATA_BUFFER };
    }
    return true;
}

bool XRSession::beginFrame() {
    if (!running_) return false;

    frameState_ = { XR_TYPE_FRAME_STATE };
    XrFrameWaitInfo wi{ XR_TYPE_FRAME_WAIT_INFO };
    xrWaitFrame(session_, &wi, &frameState_);

    XrFrameBeginInfo bi{ XR_TYPE_FRAME_BEGIN_INFO };
    xrBeginFrame(session_, &bi);

    return frameState_.shouldRender == XR_TRUE;
}

bool XRSession::getEyePoses(glm::mat4 outProj[2], glm::mat4 outRefView[2],
                            float nearZ, float farZ, float worldScale) {
    if (!running_) return false;

    XrViewLocateInfo locateInfo{ XR_TYPE_VIEW_LOCATE_INFO };
    locateInfo.viewConfigurationType = XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO;
    locateInfo.displayTime           = frameState_.predictedDisplayTime;
    locateInfo.space                 = referenceSpace_;

    XrViewState viewState{ XR_TYPE_VIEW_STATE };
    uint32_t    viewCount = 2;
    views_[0] = { XR_TYPE_VIEW };
    views_[1] = { XR_TYPE_VIEW };
    XrResult res = xrLocateViews(session_, &locateInfo, &viewState,
                                  viewCount, &viewCount, views_);
    if (res != XR_SUCCESS) return false;

    bool valid = (viewState.viewStateFlags &
                  (XR_VIEW_STATE_POSITION_VALID_BIT |
                   XR_VIEW_STATE_ORIENTATION_VALID_BIT)) ==
                 (XR_VIEW_STATE_POSITION_VALID_BIT |
                  XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    if (!valid) return false;

    for (int i = 0; i < 2; ++i) {
        outProj[i]    = fovToProjection(views_[i].fov, nearZ, farZ);
        outRefView[i] = poseToView(views_[i].pose, worldScale);
    }
    return true;
}

GLuint XRSession::acquireSwapchainImage(int eye) {
    if (!running_ || eye < 0 || eye > 1) return 0;

    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    xrAcquireSwapchainImage(eyes_[eye].handle, &ai, &eyes_[eye].acquiredIndex);

    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    xrWaitSwapchainImage(eyes_[eye].handle, &wi);

    return eyes_[eye].images[eyes_[eye].acquiredIndex].image;
}

void XRSession::releaseSwapchainImage(int eye) {
    if (!running_ || eye < 0 || eye > 1) return;

    XrSwapchainImageReleaseInfo ri{ XR_TYPE_SWAPCHAIN_IMAGE_RELEASE_INFO };
    xrReleaseSwapchainImage(eyes_[eye].handle, &ri);
}

void XRSession::endFrame(bool rendered) {
    if (!running_) return;

    XrCompositionLayerProjectionView projViews[2]{};
    XrCompositionLayerProjection     projLayer{ XR_TYPE_COMPOSITION_LAYER_PROJECTION };

    if (rendered) {
        for (int i = 0; i < 2; ++i) {
            projViews[i].type = XR_TYPE_COMPOSITION_LAYER_PROJECTION_VIEW;
            projViews[i].pose = views_[i].pose;
            projViews[i].fov  = views_[i].fov;
            projViews[i].subImage.swapchain             = eyes_[i].handle;
            projViews[i].subImage.imageRect.offset      = { 0, 0 };
            projViews[i].subImage.imageRect.extent      = { static_cast<int32_t>(eyeWidth_),
                                                            static_cast<int32_t>(eyeHeight_) };
            projViews[i].subImage.imageArrayIndex       = 0;
        }
        projLayer.space      = referenceSpace_;
        projLayer.viewCount  = 2;
        projLayer.views      = projViews;
    }

    const XrCompositionLayerBaseHeader *layers[] = {
        reinterpret_cast<XrCompositionLayerBaseHeader *>(&projLayer)
    };

    XrFrameEndInfo ei{ XR_TYPE_FRAME_END_INFO };
    ei.displayTime          = frameState_.predictedDisplayTime;
    ei.environmentBlendMode = XR_ENVIRONMENT_BLEND_MODE_OPAQUE;
    ei.layerCount           = rendered ? 1 : 0;
    ei.layers               = rendered ? layers : nullptr;
    xrEndFrame(session_, &ei);
}

// ---------------------------------------------------------------------------
// Private: initialisation stages
// ---------------------------------------------------------------------------

bool XRSession::createInstance() {
    const char *extensions[] = { XR_KHR_OPENGL_ENABLE_EXTENSION_NAME };

    XrApplicationInfo appInfo{};
    strncpy_s(appInfo.applicationName, "StereoVista", XR_MAX_APPLICATION_NAME_SIZE - 1);
    appInfo.applicationVersion = 1;
    strncpy_s(appInfo.engineName, "StereoVistaEngine", XR_MAX_ENGINE_NAME_SIZE - 1);
    appInfo.engineVersion      = 1;
    appInfo.apiVersion         = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO };
    ci.applicationInfo         = appInfo;
    ci.enabledExtensionCount   = 1;
    ci.enabledExtensionNames   = extensions;

    XrResult res = xrCreateInstance(&ci, &instance_);
    if (res != XR_SUCCESS) {
        // xrCreateInstance can fail for very different reasons. Probe the system
        // so the GUI can tell the user *which* one and how to fix it, rather than
        // always blaming a missing runtime.
        XRDiagnostics diag = probeXRRuntimes();
        if (diag.activeServiceBased) {
            // e.g. Monado (bundled with ImmersaStudio): a runtime is selected but
            // its background service isn't running, so the loader can't connect.
            statusMsg_ = "OpenXR runtime \"" + diag.activeRuntimeName +
                         "\" is selected, but its background service is not "
                         "running, so it can't be used. Start that program first, "
                         "or choose another runtime below.";
        } else if (diag.activeRuntimeName.empty()) {
            statusMsg_ = "No OpenXR runtime is selected on this PC. Install or "
                         "enable one (SteamVR, Oculus/Meta, or Windows Mixed "
                         "Reality), then choose it below.";
        } else {
            statusMsg_ = "Couldn't start OpenXR runtime \"" +
                         diag.activeRuntimeName + "\" (xrCreateInstance error " +
                         std::to_string(res) + "). Make sure your headset is "
                         "connected and its software is running, or choose "
                         "another runtime below.";
        }
        return false;
    }

    // Read runtime name for display in the GUI.
    XrInstanceProperties props{ XR_TYPE_INSTANCE_PROPERTIES };
    if (xrGetInstanceProperties(instance_, &props) == XR_SUCCESS) {
        runtimeName_ = props.runtimeName;
    }
    return true;
}

bool XRSession::createSystem() {
    XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult res = xrGetSystem(instance_, &sgi, &systemId_);
    if (res != XR_SUCCESS) {
        statusMsg_ = "No HMD detected (xrGetSystem failed, error "
                     + std::to_string(res) + "). Is a headset connected?";
        return false;
    }

    // Check that the runtime actually supports OpenGL.
    PFN_xrGetOpenGLGraphicsRequirementsKHR pfnGetReqs = nullptr;
    xrGetInstanceProcAddr(instance_,
                          "xrGetOpenGLGraphicsRequirementsKHR",
                          reinterpret_cast<PFN_xrVoidFunction *>(&pfnGetReqs));
    if (!pfnGetReqs) {
        statusMsg_ = "xrGetOpenGLGraphicsRequirementsKHR not available.";
        return false;
    }

    XrGraphicsRequirementsOpenGLKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_OPENGL_KHR };
    pfnGetReqs(instance_, systemId_, &reqs);
    // We don't abort on version mismatch – the runtime will reject the session
    // if needed, which gives a more informative error message.
    return true;
}

bool XRSession::createSession(HDC hdc, HGLRC hglrc) {
    XrGraphicsBindingOpenGLWin32KHR glBinding{ XR_TYPE_GRAPHICS_BINDING_OPENGL_WIN32_KHR };
    glBinding.hDC   = hdc;
    glBinding.hGLRC = hglrc;

    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    sci.next     = &glBinding;
    sci.systemId = systemId_;

    XrResult res = xrCreateSession(instance_, &sci, &session_);
    if (res != XR_SUCCESS) {
        statusMsg_ = "xrCreateSession failed (error " + std::to_string(res) + ").";
        return false;
    }

    // Create a LOCAL reference space centred on the user's head at session start.
    XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.referenceSpaceType        = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f }; // identity
    rsci.poseInReferenceSpace.position    = { 0.0f, 0.0f, 0.0f };

    res = xrCreateReferenceSpace(session_, &rsci, &referenceSpace_);
    if (res != XR_SUCCESS) {
        statusMsg_ = "xrCreateReferenceSpace failed (error " + std::to_string(res) + ").";
        return false;
    }

    statusMsg_ = "OpenXR session created.";
    return true;
}

bool XRSession::createSwapchains() {
    // Query the recommended eye resolution.
    uint32_t viewCount = 0;
    xrEnumerateViewConfigurationViews(instance_, systemId_,
                                      XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                      0, &viewCount, nullptr);
    if (viewCount != 2) {
        statusMsg_ = "Unexpected view count from xrEnumerateViewConfigurationViews.";
        return false;
    }

    std::vector<XrViewConfigurationView> viewConfs(2, { XR_TYPE_VIEW_CONFIGURATION_VIEW });
    xrEnumerateViewConfigurationViews(instance_, systemId_,
                                      XR_VIEW_CONFIGURATION_TYPE_PRIMARY_STEREO,
                                      2, &viewCount, viewConfs.data());

    eyeWidth_  = viewConfs[0].recommendedImageRectWidth;
    eyeHeight_ = viewConfs[0].recommendedImageRectHeight;

    // Choose a swapchain format. Prefer sRGB so compositing looks right.
    uint32_t fmtCount = 0;
    xrEnumerateSwapchainFormats(session_, 0, &fmtCount, nullptr);
    std::vector<int64_t> formats(fmtCount);
    xrEnumerateSwapchainFormats(session_, fmtCount, &fmtCount, formats.data());

    int64_t chosenFmt = GL_RGBA8; // fallback
    for (int64_t f : formats) {
        if (f == GL_SRGB8_ALPHA8) { chosenFmt = f; break; }
    }
    if (chosenFmt == GL_RGBA8) {
        for (int64_t f : formats) {
            if (f == GL_RGBA8) { chosenFmt = f; break; }
        }
    }

    for (int i = 0; i < 2; ++i) {
        XrSwapchainCreateInfo sci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                          XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.format      = chosenFmt;
        sci.sampleCount = 1;
        sci.width       = eyeWidth_;
        sci.height      = eyeHeight_;
        sci.faceCount   = 1;
        sci.arraySize   = 1;
        sci.mipCount    = 1;

        XrResult res = xrCreateSwapchain(session_, &sci, &eyes_[i].handle);
        if (res != XR_SUCCESS) {
            statusMsg_ = "xrCreateSwapchain failed for eye " + std::to_string(i)
                       + " (error " + std::to_string(res) + ").";
            return false;
        }

        uint32_t imgCount = 0;
        xrEnumerateSwapchainImages(eyes_[i].handle, 0, &imgCount, nullptr);
        eyes_[i].images.resize(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_OPENGL_KHR });
        xrEnumerateSwapchainImages(eyes_[i].handle, imgCount, &imgCount,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(eyes_[i].images.data()));
    }
    return true;
}

bool XRSession::createGLObjects() {
    glGenFramebuffers(1, &fbo_);
    glGenRenderbuffers(1, &depthRbo_);

    // Allocate a depth renderbuffer sized for one eye.  It is shared between
    // both eyes since we render them sequentially (one eye at a time).
    glBindRenderbuffer(GL_RENDERBUFFER, depthRbo_);
    glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24,
                          static_cast<GLsizei>(eyeWidth_),
                          static_cast<GLsizei>(eyeHeight_));
    glBindRenderbuffer(GL_RENDERBUFFER, 0);

    glBindFramebuffer(GL_FRAMEBUFFER, fbo_);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                              GL_RENDERBUFFER, depthRbo_);
    // No colour attachment yet — that is attached per-eye per-frame in the main loop.
    // GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT is expected at this point.
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return true;
}

void XRSession::destroyGLObjects() {
    if (fbo_)     { glDeleteFramebuffers(1, &fbo_);      fbo_     = 0; }
    if (depthRbo_){ glDeleteRenderbuffers(1, &depthRbo_); depthRbo_= 0; }
}

// ---------------------------------------------------------------------------
// Private: matrix helpers
// ---------------------------------------------------------------------------

glm::mat4 XRSession::fovToProjection(const XrFovf &fov, float nearZ, float farZ) {
    float tanL = std::tan(fov.angleLeft);   // negative
    float tanR = std::tan(fov.angleRight);  // positive
    float tanU = std::tan(fov.angleUp);     // positive
    float tanD = std::tan(fov.angleDown);   // negative

    float w = tanR - tanL;
    float h = tanU - tanD;

    glm::mat4 p(0.0f);
    p[0][0] =  2.0f / w;
    p[2][0] = (tanR + tanL) / w;
    p[1][1] =  2.0f / h;
    p[2][1] = (tanU + tanD) / h;
    p[2][2] = -(farZ + nearZ) / (farZ - nearZ);
    p[3][2] = -(2.0f * farZ * nearZ) / (farZ - nearZ);
    p[2][3] = -1.0f;
    return p;
}

glm::mat4 XRSession::poseToView(const XrPosef &pose, float worldScale) {
    glm::quat q(pose.orientation.w,
                pose.orientation.x,
                pose.orientation.y,
                pose.orientation.z);
    // worldScale is in metres-per-scene-unit (e.g. 0.01 for a cm scene, 1.0 for m).
    // XR positions are in metres → divide by worldScale to get scene units.
    // Example: worldScale=0.01 (cm scene), XR pos=1.0m → 100 scene units.
    glm::vec3 p(pose.position.x / worldScale,
                pose.position.y / worldScale,
                pose.position.z / worldScale);

    // world-from-eye = translation * rotation
    glm::mat4 worldFromEye = glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(q);
    // view = inverse(world-from-eye) = eye-from-world
    return glm::inverse(worldFromEye);
}

} // namespace Engine

#endif // _WIN32
