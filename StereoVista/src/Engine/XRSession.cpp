// OpenXR session management for StereoVista (Phase 7b: Vulkan graphics binding).
// Compiled only on Windows; the rest of the project is also Windows-only.
#ifdef _WIN32

// Keep Windows.h (pulled in for the registry/env probing below) from defining
// the min/max macros — this TU also sees glm/Vulkan headers via XRSession.h.
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN

#include "Engine/XRSession.h"

#include "RHI/Device.h"
#include "Renderer/Projection.h" // house reverse-Z / Y-flip projection factories

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/quaternion.hpp>

#include <Windows.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib> // _wputenv_s
#include <cstring>
#include <filesystem>
#include <iostream>

// RegGetValueW / registry access lives in Advapi32; make sure it is linked even
// if it is not part of the project's default library set.
#pragma comment(lib, "Advapi32.lib")

namespace Engine {

// ===========================================================================
// System OpenXR runtime probing  (graphics-API-agnostic; unchanged in the port)
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

// ===========================================================================
// Small helpers
// ===========================================================================
namespace {

// xrGetInstanceProcAddr wrapper for extension entry points.
template <typename Fn>
bool loadXrFunc(XrInstance instance, const char *name, Fn &out) {
    return xrGetInstanceProcAddr(
               instance, name,
               reinterpret_cast<PFN_xrVoidFunction *>(&out)) == XR_SUCCESS &&
           out != nullptr;
}

} // namespace

// ---------------------------------------------------------------------------
// Public: init / destroy
// ---------------------------------------------------------------------------

bool XRSession::init(rhi::Device &device) {
    device_   = &device;
    vkDevice_ = device.device();

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

    if (!createInstance())                  return false;
    if (!createSystemAndValidateDevice()) { destroy(); return false; }
    if (!createSession())                 { destroy(); return false; }
    if (!createSwapchains())              { destroy(); return false; }

    initialized_ = true;
    statusMsg_   = "OpenXR session active – " + runtimeName_;
    std::cout << "[XR] Session initialised: " << runtimeName_ << "  "
              << eyeWidth_ << "x" << eyeHeight_ << " per eye\n";
    return true;
}

void XRSession::destroy() {
    destroySwapchains();

    // XrSpace is a child of XrSession — must be destroyed before the session.
    if (referenceSpace_ != XR_NULL_HANDLE) {
        xrDestroySpace(referenceSpace_);
        referenceSpace_ = XR_NULL_HANDLE;
    }

    if (session_ != XR_NULL_HANDLE) {
        if (running_)
            xrRequestExitSession(session_);
        xrDestroySession(session_);
        session_ = XR_NULL_HANDLE;
    }

    if (instance_ != XR_NULL_HANDLE) {
        xrDestroyInstance(instance_);
        instance_ = XR_NULL_HANDLE;
    }

    pfnGetVulkanRequirements_ = nullptr;
    pfnGetVulkanDevice_       = nullptr;
    systemId_       = XR_NULL_SYSTEM_ID;
    sessionState_   = XR_SESSION_STATE_UNKNOWN;
    initialized_    = false;
    running_        = false;
    sessionVisible_ = false;
    eyeWidth_       = 0;
    eyeHeight_      = 0;
    colorFormat_    = VK_FORMAT_UNDEFINED;
    colorIsSrgb_    = false;

    statusMsg_ = "OpenXR session destroyed.";
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

bool XRSession::getEyePoses(XREyePose out[2], float nearZ, float farZ,
                            float worldScale) {
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

    const bool valid = (viewState.viewStateFlags &
                        (XR_VIEW_STATE_POSITION_VALID_BIT |
                         XR_VIEW_STATE_ORIENTATION_VALID_BIT)) ==
                       (XR_VIEW_STATE_POSITION_VALID_BIT |
                        XR_VIEW_STATE_ORIENTATION_VALID_BIT);
    if (!valid) return false;

    for (int i = 0; i < 2; ++i) {
        const XrFovf &fov = views_[i].fov;
        // Near-plane extents from the runtime's asymmetric FOV angles, fed to
        // the house reverse-Z + Y-flip factory (NOT the GL-convention matrix the
        // old code built — playbook C.2: build depth the Vulkan way, don't port).
        const float l = std::tan(fov.angleLeft)  * nearZ;
        const float r = std::tan(fov.angleRight) * nearZ;
        const float b = std::tan(fov.angleDown)  * nearZ;
        const float t = std::tan(fov.angleUp)    * nearZ;
        out[i].proj = renderer::frustumAsymmetric(l, r, b, t, nearZ, farZ);
        out[i].view = poseToView(views_[i].pose, worldScale);
    }
    return true;
}

VkImageView XRSession::acquireSwapchainImage(int eye) {
    if (!running_ || eye < 0 || eye > 1) return VK_NULL_HANDLE;

    XrSwapchainImageAcquireInfo ai{ XR_TYPE_SWAPCHAIN_IMAGE_ACQUIRE_INFO };
    if (xrAcquireSwapchainImage(eyes_[eye].handle, &ai,
                                &eyes_[eye].acquiredIndex) != XR_SUCCESS)
        return VK_NULL_HANDLE;

    XrSwapchainImageWaitInfo wi{ XR_TYPE_SWAPCHAIN_IMAGE_WAIT_INFO };
    wi.timeout = XR_INFINITE_DURATION;
    if (xrWaitSwapchainImage(eyes_[eye].handle, &wi) != XR_SUCCESS)
        return VK_NULL_HANDLE;

    return eyes_[eye].views[eyes_[eye].acquiredIndex];
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
            projViews[i].subImage.swapchain        = eyes_[i].handle;
            projViews[i].subImage.imageRect.offset = { 0, 0 };
            projViews[i].subImage.imageRect.extent = {
                static_cast<int32_t>(eyeWidth_), static_cast<int32_t>(eyeHeight_) };
            projViews[i].subImage.imageArrayIndex  = 0;
        }
        projLayer.space     = referenceSpace_;
        projLayer.viewCount = 2;
        projLayer.views     = projViews;
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
    const char *extensions[] = { XR_KHR_VULKAN_ENABLE2_EXTENSION_NAME };

    XrApplicationInfo appInfo{};
    strncpy_s(appInfo.applicationName, "StereoVista", XR_MAX_APPLICATION_NAME_SIZE - 1);
    appInfo.applicationVersion = 1;
    strncpy_s(appInfo.engineName, "StereoVistaEngine", XR_MAX_ENGINE_NAME_SIZE - 1);
    appInfo.engineVersion      = 1;
    appInfo.apiVersion         = XR_CURRENT_API_VERSION;

    XrInstanceCreateInfo ci{ XR_TYPE_INSTANCE_CREATE_INFO };
    ci.applicationInfo       = appInfo;
    ci.enabledExtensionCount = 1;
    ci.enabledExtensionNames = extensions;

    XrResult res = xrCreateInstance(&ci, &instance_);
    if (res != XR_SUCCESS) {
        // xrCreateInstance can fail for very different reasons. Probe the system
        // so the GUI can tell the user *which* one and how to fix it, rather than
        // always blaming a missing runtime.
        XRDiagnostics diag = probeXRRuntimes();
        if (diag.activeServiceBased) {
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
                         std::to_string(res) + "). It may not support the Vulkan "
                         "graphics binding, or your headset isn't connected. Try "
                         "another runtime below.";
        }
        return false;
    }

    // Read runtime name for display in the GUI.
    XrInstanceProperties props{ XR_TYPE_INSTANCE_PROPERTIES };
    if (xrGetInstanceProperties(instance_, &props) == XR_SUCCESS)
        runtimeName_ = props.runtimeName;
    return true;
}

bool XRSession::createSystemAndValidateDevice() {
    XrSystemGetInfo sgi{ XR_TYPE_SYSTEM_GET_INFO };
    sgi.formFactor = XR_FORM_FACTOR_HEAD_MOUNTED_DISPLAY;

    XrResult res = xrGetSystem(instance_, &sgi, &systemId_);
    if (res != XR_SUCCESS) {
        statusMsg_ = "No HMD detected (xrGetSystem failed, error "
                     + std::to_string(res) + "). Is a headset connected and its "
                     "runtime running?";
        return false;
    }

    // Resolve the enable2 entry points (extension functions, not core).
    if (!loadXrFunc(instance_, "xrGetVulkanGraphicsRequirements2KHR",
                    pfnGetVulkanRequirements_) ||
        !loadXrFunc(instance_, "xrGetVulkanGraphicsDevice2KHR",
                    pfnGetVulkanDevice_)) {
        statusMsg_ = "This OpenXR runtime does not expose the Vulkan graphics "
                     "binding (XR_KHR_vulkan_enable2). Choose a Vulkan-capable "
                     "runtime below.";
        return false;
    }

    // Calling the graphics-requirements query is a REQUIRED ritual before
    // xrCreateSession (some runtimes reject the session otherwise). We honour
    // it and log the supported range but, like the GL path, don't hard-abort on
    // a version mismatch — xrCreateSession is the real arbiter.
    XrGraphicsRequirementsVulkanKHR reqs{ XR_TYPE_GRAPHICS_REQUIREMENTS_VULKAN_KHR };
    if (pfnGetVulkanRequirements_(instance_, systemId_, &reqs) == XR_SUCCESS) {
        std::cout << "[XR] Vulkan API range: "
                  << XR_VERSION_MAJOR(reqs.minApiVersionSupported) << "."
                  << XR_VERSION_MINOR(reqs.minApiVersionSupported) << " .. "
                  << XR_VERSION_MAJOR(reqs.maxApiVersionSupported) << "."
                  << XR_VERSION_MINOR(reqs.maxApiVersionSupported) << "\n";
    }

    // The runtime dictates which physical GPU the headset is attached to. We
    // render on the device rhi::Device already picked; if they differ we cannot
    // bind (Vulkan can't share images across physical devices). On single-GPU
    // PCs and typical laptops (HMD on the dGPU we render on) they match.
    XrVulkanGraphicsDeviceGetInfoKHR gi{ XR_TYPE_VULKAN_GRAPHICS_DEVICE_GET_INFO_KHR };
    gi.systemId       = systemId_;
    gi.vulkanInstance = device_->instance();
    VkPhysicalDevice xrGpu = VK_NULL_HANDLE;
    res = pfnGetVulkanDevice_(instance_, &gi, &xrGpu);
    if (res != XR_SUCCESS || xrGpu == VK_NULL_HANDLE) {
        statusMsg_ = "xrGetVulkanGraphicsDevice2KHR failed (error "
                     + std::to_string(res) + ").";
        return false;
    }
    if (xrGpu != device_->physicalDevice()) {
        VkPhysicalDeviceProperties hmdGpu{};
        vkGetPhysicalDeviceProperties(xrGpu, &hmdGpu);
        statusMsg_ = std::string("The headset is driven by a different GPU (") +
                     hmdGpu.deviceName + ") than StereoVista renders on (" +
                     device_->deviceName() +
                     "). Multi-GPU VR isn't supported — connect the headset to "
                     "the render GPU (or set it as the OpenXR/driver GPU).";
        return false;
    }
    return true;
}

bool XRSession::createSession() {
    XrGraphicsBindingVulkanKHR binding{ XR_TYPE_GRAPHICS_BINDING_VULKAN_KHR };
    binding.instance         = device_->instance();
    binding.physicalDevice   = device_->physicalDevice();
    binding.device           = vkDevice_;
    binding.queueFamilyIndex = device_->graphicsQueueFamily();
    binding.queueIndex       = 0; // rhi::Device creates queueCount = 1

    XrSessionCreateInfo sci{ XR_TYPE_SESSION_CREATE_INFO };
    sci.next     = &binding;
    sci.systemId = systemId_;

    XrResult res = xrCreateSession(instance_, &sci, &session_);
    if (res != XR_SUCCESS) {
        statusMsg_ = "xrCreateSession failed (error " + std::to_string(res) +
                     "). The runtime may require a Vulkan version or extensions "
                     "this device wasn't created with.";
        return false;
    }

    // LOCAL reference space centred on the user's head at session start.
    XrReferenceSpaceCreateInfo rsci{ XR_TYPE_REFERENCE_SPACE_CREATE_INFO };
    rsci.referenceSpaceType               = XR_REFERENCE_SPACE_TYPE_LOCAL;
    rsci.poseInReferenceSpace.orientation = { 0.0f, 0.0f, 0.0f, 1.0f };
    rsci.poseInReferenceSpace.position    = { 0.0f, 0.0f, 0.0f };
    res = xrCreateReferenceSpace(session_, &rsci, &referenceSpace_);
    if (res != XR_SUCCESS) {
        statusMsg_ = "xrCreateReferenceSpace failed (error " + std::to_string(res) + ").";
        return false;
    }
    return true;
}

bool XRSession::createSwapchains() {
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

    // Choose a Vulkan color format the runtime offers. Prefer sRGB so the
    // hardware encodes on write (the resolve then outputs LINEAR); fall back to
    // UNORM (the resolve then applies the sRGB OETF itself, like the window).
    uint32_t fmtCount = 0;
    xrEnumerateSwapchainFormats(session_, 0, &fmtCount, nullptr);
    std::vector<int64_t> formats(fmtCount);
    xrEnumerateSwapchainFormats(session_, fmtCount, &fmtCount, formats.data());
    auto offered = [&](int64_t f) {
        return std::find(formats.begin(), formats.end(), f) != formats.end();
    };
    const int64_t preferred[] = {
        VK_FORMAT_R8G8B8A8_SRGB, VK_FORMAT_B8G8R8A8_SRGB,
        VK_FORMAT_R8G8B8A8_UNORM, VK_FORMAT_B8G8R8A8_UNORM,
    };
    int64_t chosen = formats.empty() ? int64_t(VK_FORMAT_R8G8B8A8_SRGB) : formats[0];
    for (int64_t f : preferred)
        if (offered(f)) { chosen = f; break; }
    colorFormat_ = static_cast<VkFormat>(chosen);
    colorIsSrgb_ = (colorFormat_ == VK_FORMAT_R8G8B8A8_SRGB ||
                    colorFormat_ == VK_FORMAT_B8G8R8A8_SRGB);

    for (int i = 0; i < 2; ++i) {
        XrSwapchainCreateInfo sci{ XR_TYPE_SWAPCHAIN_CREATE_INFO };
        sci.usageFlags  = XR_SWAPCHAIN_USAGE_COLOR_ATTACHMENT_BIT |
                          XR_SWAPCHAIN_USAGE_SAMPLED_BIT;
        sci.format      = chosen;
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
        eyes_[i].images.assign(imgCount, { XR_TYPE_SWAPCHAIN_IMAGE_VULKAN_KHR });
        xrEnumerateSwapchainImages(
            eyes_[i].handle, imgCount, &imgCount,
            reinterpret_cast<XrSwapchainImageBaseHeader *>(eyes_[i].images.data()));

        // Our own color views over the runtime-owned VkImages (we destroy only
        // the views; the images belong to the runtime).
        eyes_[i].views.assign(imgCount, VK_NULL_HANDLE);
        for (uint32_t n = 0; n < imgCount; ++n) {
            VkImageViewCreateInfo iv{ VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
            iv.image            = eyes_[i].images[n].image;
            iv.viewType         = VK_IMAGE_VIEW_TYPE_2D;
            iv.format           = colorFormat_;
            iv.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
            VK_CHECK(vkCreateImageView(vkDevice_, &iv, nullptr, &eyes_[i].views[n]));
        }
    }
    return true;
}

void XRSession::destroySwapchains() {
    for (int i = 0; i < 2; ++i) {
        for (VkImageView view : eyes_[i].views)
            if (view != VK_NULL_HANDLE)
                vkDestroyImageView(vkDevice_, view, nullptr);
        eyes_[i].views.clear();
        if (eyes_[i].handle != XR_NULL_HANDLE) {
            xrDestroySwapchain(eyes_[i].handle);
            eyes_[i].handle = XR_NULL_HANDLE;
        }
        eyes_[i].images.clear();
        eyes_[i].acquiredIndex = 0;
    }
}

// ---------------------------------------------------------------------------
// Private: matrix helpers
// ---------------------------------------------------------------------------

glm::mat4 XRSession::poseToView(const XrPosef &pose, float worldScale) {
    glm::quat q(pose.orientation.w, pose.orientation.x,
                pose.orientation.y, pose.orientation.z);
    // XR positions are in metres; worldScale is metres-per-scene-unit, so divide
    // to get scene units (e.g. worldScale 0.01 for a cm scene: 1 m → 100 units).
    const float ws = (worldScale != 0.0f) ? worldScale : 1.0f;
    glm::vec3 p(pose.position.x / ws, pose.position.y / ws, pose.position.z / ws);

    // eye-from-reference = inverse(reference-from-eye) = inverse(T * R).
    glm::mat4 refFromEye = glm::translate(glm::mat4(1.0f), p) * glm::mat4_cast(q);
    return glm::inverse(refFromEye);
}

} // namespace Engine

#endif // _WIN32
