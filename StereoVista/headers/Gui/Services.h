#pragma once

// ============================================================================
//  Gui::Services  —  the services facade handed to every GUI panel
// ----------------------------------------------------------------------------
//  Panels never touch Application internals or Vulkan directly. Every panel is
//  passed a Gui::Services& (mirroring how tools get a Plugins::PluginContext&)
//  and reaches the host through it:
//
//      • the Settings struct they edit
//      • the "plain enough" subsystems (camera, scene, cursors, undo, clip
//        tool, plugins) by reference
//      • plain-typed queries/actions for everything that would otherwise drag
//        a Vulkan or renderer header into the GUI layer (frame diagnostics,
//        present mode, tonemap, stereo/VR, screenshots, selection, the gizmo,
//        file dialogs, per-cloud stats, theming, toasts)
//
//  Keeping the Vulkan-coupled work behind this interface is what lets the GUI
//  stay "pure ImGui over system APIs" — the concrete MainGuiServices (a friend
//  of Application, defined in App/Application.cpp) does the Vulkan-touching
//  work, exactly like MainPluginContext.
// ============================================================================

#include "Gui/Settings.h"
#include "Plugins/PluginContext.h" // Plugins::ToastLevel

#include <cstddef>
#include <cstdint>
#include <string>

// Forward declarations keep this header light.
class Camera;
namespace scene { struct Scene; }
namespace Cursor { class CursorManager; }
namespace core { class UndoManager; class CommandRegistry; class ShortcutMap; }
namespace Tools { class ClipPlaneTool; }
namespace Plugins { class PluginManager; class PluginContext; }
namespace Engine { struct XRDiagnostics; }
namespace renderer { namespace gpu { struct MaterialData; } }

namespace Gui {

// One-shot snapshot of per-frame render/WSI diagnostics (folded out of the old
// debug panel). Plain data so the Performance panel never sees a Vulkan type.
struct FrameDiagnostics {
    const char* gpuName = "";
    uint32_t apiMajor = 0, apiMinor = 0, apiPatch = 0;
    bool     validation = false;

    uint32_t swapchainWidth = 0, swapchainHeight = 0, swapchainImages = 0;
    uint32_t viewCount = 1;
    bool     stereoPresentCapable = false;

    float fps = 0.0f, frameMs = 0.0f;
    float slotWaitMs = 0.0f, acquireMs = 0.0f, presentMs = 0.0f;
    uint32_t swapchainRecreations = 0;

    // Async compute queue: whether the device has one, whether it rides its
    // own queue family (vs a second graphics-family queue), and whether the
    // last frame actually submitted compute work on it.
    bool asyncComputeSupported = false;
    bool asyncComputeDedicatedFamily = false;
    bool asyncComputeEnabled = false;
    bool asyncComputeActive = false;
};

// What one Viewport panel displays: the renderer's offscreen viewport texture
// as an ImGui image. Plain data (textureId is the ImTextureID); active=false
// means the classic fullscreen path is in use (XR) and the panels are skipped
// — the scene then shows through the dockspace's passthru central node.
struct ViewportDisplay {
    void* textureId = nullptr;      // ImTextureID; null until the target exists
    unsigned width = 0, height = 0; // texture size in pixels
    bool active = false;            // docked-viewport mode (draw the panel)
};

// What one Viewport panel reports back each GUI frame: desired size + input
// state, all in coordinates the app can act on without knowing ImGui layout.
struct ViewportPanelState {
    bool  shown = false;            // panel visible with a >=1px content region
    float sizeX = 0.0f, sizeY = 0.0f;   // content region = desired texture size
    float mouseX = 0.0f, mouseY = 0.0f; // mouse in TEXTURE pixels (may be out of
                                        // range — consumers bound-check)
    float screenX = 0.0f, screenY = 0.0f; // image top-left, ImGui screen coords
    float screenW = 0.0f, screenH = 0.0f; // image display size on screen
    bool  hovered = false;          // image hovered, no other window on top
    bool  focused = false;
    void* hostWindow = nullptr;     // GLFWwindow* of the OS window hosting it
};

// Live streaming progress for one point cloud (mirror of
// Engine::PointCloudLoader::StreamProgress, minus the loader header).
struct PointCloudProgress {
    bool     active = false;
    bool     resorting = false;
    uint32_t pointsLoaded = 0;
    uint32_t pointsTotal = 0;
    float    fraction = 0.0f;
    double   pointsPerSecond = 0.0;
};

class Services {
public:
    virtual ~Services() = default;

    // ── The settings the panels edit ────────────────────────────────────────
    virtual Settings& settings() = 0;

    // ── Subsystems exposed directly (no Vulkan API surface of their own) ─────
    virtual Camera&                 camera() = 0;
    virtual scene::Scene&           scene() = 0;
    virtual Cursor::CursorManager&  cursors() = 0;
    virtual core::UndoManager&      undo() = 0;
    virtual Tools::ClipPlaneTool&   clipTool() = 0;
    virtual Plugins::PluginManager& plugins() = 0;
    virtual Plugins::PluginContext& pluginContext() = 0;

    // ── Commands + shortcuts (UI redesign Pass 0) ────────────────────────────
    // Every menu item / toolbar button / palette row runs through
    // commands().run(id) — contract C5. shortcuts() supplies the live key
    // labels (menus) and the binding editor (Settings, Pass 6).
    virtual core::CommandRegistry& commands() = 0;
    virtual core::ShortcutMap&     shortcuts() = 0;

    // ── Docked 3D viewports ─────────────────────────────────────────────────
    // The GuiSystem draws one Viewport window per index in
    // [0, viewportPanelCount()) from viewportDisplay(i) and reports each
    // panel's geometry/input back through onViewportPanel(i, ...) every frame
    // (a default-constructed state when a panel was not drawn). Window titles
    // come from viewportPanelName(i) and are STABLE per viewport (they are the
    // ImGui window identity, which imgui.ini docking keys on).
    virtual uint32_t        viewportPanelCount() const = 0;
    virtual const char*     viewportPanelName(uint32_t index) const = 0;
    virtual ViewportDisplay viewportDisplay(uint32_t index) const = 0;
    virtual void onViewportPanel(uint32_t index, const ViewportPanelState& state) = 0;
    // View menu: add a viewport (up to the renderer cap) / close a secondary
    // one (index 0 — the primary — is never closable). The close is deferred
    // to the next frame boundary, where the renderer reconfigures device-idle.
    virtual bool canAddViewport() const = 0;
    virtual void addViewport() = 0;
    virtual void closeViewport(uint32_t index) = 0;
    // Mouse is over any 3D view (docked: a viewport image; classic
    // fullscreen: anywhere no GUI window owns the mouse). For hover-driven
    // panel features like the scene-layer hover pick.
    virtual bool viewportHovered() const = 0;

    // ── Diagnostics / device info ────────────────────────────────────────────
    virtual FrameDiagnostics diagnostics() const = 0;

    // ── Present mode (index-based; keeps VkPresentModeKHR out of the GUI) ────
    virtual int         presentModeCount() const = 0;
    virtual const char* presentModeName(int index) const = 0;
    virtual int         currentPresentMode() const = 0; // index in [0,count)
    virtual void        requestPresentMode(int index) = 0;

    // ── Tonemap (op is an index; names via tonemapOpName) ────────────────────
    virtual float       tonemapExposure() const = 0;
    virtual void        setTonemapExposure(float value) = 0;
    virtual int         tonemapOp() const = 0;
    virtual void        setTonemapOp(int op) = 0;
    virtual int         tonemapOpCount() const = 0;
    virtual const char* tonemapOpName(int op) const = 0;

    // ── Sky availability (gate the sky-mode combo) ──────────────────────────
    virtual bool skyHasCubemap() const = 0;
    virtual bool skyHasEquirect() const = 0;

    // ── Screenshot ──────────────────────────────────────────────────────────
    virtual bool        screenshotPending() const = 0;
    virtual const char* screenshotStatus() const = 0;
    virtual void        requestScreenshot() = 0;

    // ── Upload ring (point-cloud streaming budget) ──────────────────────────
    virtual double uploadRingUsedMB() const = 0;
    virtual double uploadRingCapacityMB() const = 0;

    // ── Wireframe (line-mode debug pipelines need optional GPU support) ─────
    virtual bool wireframeSupported() const = 0;

    // ── Stereo (the applied mode lives on the app; changes are queued) ──────
    virtual int      stereoMode() const = 0; // 0=Off 1=QuadBuffer 2=SideBySide
    virtual void     requestStereoMode(int mode) = 0;
    virtual bool     stereoPresentSupported() const = 0;
    virtual uint32_t swapchainLayers() const = 0;

    // ── VR / OpenXR ─────────────────────────────────────────────────────────
    virtual bool        vrEnabled() const = 0;
    virtual void        setVrEnabled(bool on) = 0;
    virtual bool        xrRunning() const = 0;
    virtual std::string xrRuntimeName() const = 0;
    virtual uint32_t    xrEyeWidth() const = 0;
    virtual uint32_t    xrEyeHeight() const = 0;
    virtual std::string vrStatus() const = 0;
    virtual void        refreshXRDiagnostics() = 0;
    virtual const Engine::XRDiagnostics& xrDiagnostics() const = 0;
    virtual bool        xrDiagnosticsValid() const = 0;
    virtual std::string xrRuntimeOverride() const = 0;
    virtual void        setXRRuntimeOverride(const std::string& manifestPath) = 0;

    // ── Selection (index into scene().models; mesh -1 = whole model) ─────────
    virtual int  selectedModel() const = 0;
    virtual int  selectedMesh() const = 0;
    virtual void setSelection(int model, int mesh = -1) = 0;
    virtual void clearSelection() = 0;

    // ── Transform gizmo (behind the facade so panels avoid the Tools header) ─
    virtual bool gizmoEnabled() const = 0;
    virtual void setGizmoEnabled(bool on) = 0;
    virtual int  gizmoMode() const = 0; // 0=Translate 1=Rotate 2=Scale
    virtual void setGizmoMode(int mode) = 0;
    virtual bool gizmoLocalSpace() const = 0;
    virtual void setGizmoLocalSpace(bool local) = 0;
    virtual bool gizmoSnap() const = 0;
    virtual void setGizmoSnap(bool on) = 0;

    // ── Scene operations ────────────────────────────────────────────────────
    virtual void importModelDialog() = 0;    // Assimp import -> scene
    virtual void openPointCloudDialog() = 0;  // load a point cloud file
    virtual bool sceneSaveAvailable() const = 0; // false until SceneManager lands
    virtual void deleteModel(int model) = 0;
    virtual void focusCameraOn(int model) = 0; // centre the camera on a model

    // ── SLPK / I3S scene layers ─────────────────────────────────────────────
    // The layers themselves are reachable via scene().i3sLayers (plain data +
    // inspector state); only the operations that touch app internals (worker
    // threads, camera, dialogs) go through the facade.
    virtual void   openSlpkDialog() = 0;             // file dialog -> worker parse
    virtual size_t slpkLoadsInFlight() const = 0;    // parses still running
    virtual void   frameI3SLayer(size_t index) = 0;  // fly camera to the layer
    virtual void   unloadI3SLayer(size_t index) = 0;

    // ── Materials (edit the bindless table entry backing a mesh) ────────────
    // Returns nullptr when the (model, mesh) pair is invalid.
    virtual renderer::gpu::MaterialData* materialForMesh(int model, int mesh) = 0;

    // ── Point clouds ────────────────────────────────────────────────────────
    virtual size_t             pointCloudCount() const = 0;
    virtual const std::string& pointCloudName(size_t i) const = 0;
    virtual bool&              pointCloudVisible(size_t i) = 0;
    virtual uint32_t           pointCloudBatches(size_t i) const = 0;
    virtual uint32_t           pointCloudPoints(size_t i) const = 0;
    virtual double             pointCloudVramMB(size_t i) const = 0;
    virtual PointCloudProgress pointCloudProgress(size_t i) const = 0;
    virtual void               unloadPointCloud(size_t i) = 0;

    // ── Theme ───────────────────────────────────────────────────────────────
    virtual int         themeCount() const = 0;
    virtual const char* themeName(int theme) const = 0;
    virtual int         currentTheme() const = 0;
    virtual void        setTheme(int theme) = 0;

    // ── GUI scale (user factor on the window-derived scale, 0.5–2.0) ────────
    // Applying triggers a style restyle + font rebuild at the next safe frame
    // boundary, so call it from a released slider, not every drag frame.
    virtual float guiScaleFactor() const = 0;
    virtual void  setGuiScaleFactor(float factor) = 0;

    // ── User feedback ───────────────────────────────────────────────────────
    virtual void toast(const std::string& message,
                       Plugins::ToastLevel level = Plugins::ToastLevel::Info) = 0;

    // ── Application ─────────────────────────────────────────────────────────
    virtual void requestQuit() = 0; // File -> Exit (closes the main window)
};

} // namespace Gui
