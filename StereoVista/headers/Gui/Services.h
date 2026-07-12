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
#include "Scene/SceneItems.h"      // scene::SceneItemRef / scene::Selection

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

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

// Snapshot aspects (UI redesign Pass 3 §9). Bitmask so a snapshot / restore may
// cover any combination of camera and scene.
enum SnapshotAspect : uint32_t {
    kSnapshotCamera = 1u << 0,
    kSnapshotScene  = 1u << 1,
};

// One named snapshot as the Snapshots panel sees it (plain data — no app types).
struct SnapshotView {
    std::string name;
    std::string timestamp;             // human "2026-07-12 14:02"
    uint32_t    aspects = 0;           // SnapshotAspect bits captured
    std::vector<std::string> tags;
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
    // Shortcut-capture helper for the Pass-6 binding editor: the GLFW key code
    // pressed this frame (0 = none; modifier keys are never reported). The app
    // owns the ImGui<->GLFW key translation, so the Gui layer never includes
    // GLFW just to rebind a key.
    virtual int capturePressedKey() const = 0;

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

    // ── Selection (UI redesign Pass 1: the application-wide multi-select) ───
    // The ordered ObjectId-based selection every panel reads and writes
    // (contract C3). The legacy index-based accessors below map onto its
    // PRIMARY item and stay for simple consumers (Inspector until Pass 2).
    virtual scene::Selection& selection() = 0;
    virtual int  selectedModel() const = 0;
    virtual int  selectedMesh() const = 0;
    virtual void setSelection(int model, int mesh = -1) = 0;
    virtual void clearSelection() = 0;

    // ── Outliner item operations (Pass 1) ────────────────────────────────────
    // All mutations are one undoable step per call (contract C4), applied to
    // the given refs (typically the selection, or one row). Unsupported refs
    // in a batch are skipped with a toast where that matters.
    virtual void deleteItems(const std::vector<scene::SceneItemRef>& refs) = 0;
    virtual void duplicateItems(const std::vector<scene::SceneItemRef>& refs) = 0;
    virtual void setItemsVisible(const std::vector<scene::SceneItemRef>& refs,
                                 bool visible) = 0;
    virtual void setItemsLocked(const std::vector<scene::SceneItemRef>& refs,
                                bool locked) = 0;
    // Groups the refs into a new group (returns its id, 0 on failure).
    virtual uint64_t groupItems(const std::vector<scene::SceneItemRef>& refs) = 0;
    virtual void ungroupItems(const std::vector<scene::SceneItemRef>& refs) = 0;
    // Move refs into an existing group (0 = scene root). Grouping a Group ref
    // reparents it.
    virtual void moveItemsToGroup(const std::vector<scene::SceneItemRef>& refs,
                                  uint64_t groupId) = 0;
    virtual void renameItem(const scene::SceneItemRef& ref,
                            const std::string& name) = 0;
    // Fly the active viewport's camera to frame the refs' union bounds.
    virtual void frameItems(const std::vector<scene::SceneItemRef>& refs) = 0;
    // Isolate: hide everything except refs (one undo step); the status bar
    // shows an exit chip while active.
    virtual bool isolateActive() const = 0;
    virtual void isolateItems(const std::vector<scene::SceneItemRef>& refs) = 0;
    virtual void exitIsolate() = 0;

    // ── Scene document (Pass 1: new/open/save/save-as/merge + recents) ──────
    virtual void newScene() = 0;
    virtual void openSceneDialog() = 0;
    // Open `path`, honoring the replace-or-merge-or-ask preference; the ask
    // flow parks the path in pendingSceneOpenPath() for the GuiSystem modal.
    virtual void openSceneFile(const std::string& path) = 0;
    virtual void mergeSceneDialog() = 0;
    virtual bool saveScene() = 0;   // current path, else save-as dialog
    virtual bool saveSceneAs() = 0;
    virtual const std::string& currentScenePath() const = 0; // "" = untitled
    virtual const std::string& pendingSceneOpenPath() const = 0; // "" = none
    // action: 0 = cancel, 1 = replace, 2 = merge; remember persists the choice.
    virtual void resolvePendingSceneOpen(int action, bool remember) = 0;

    // ── Snapshots (Pass 3 §9: named checkpoints) ────────────────────────────
    virtual size_t       snapshotCount() const = 0;
    virtual SnapshotView snapshot(size_t i) const = 0;
    // Capture the requested aspects (SnapshotAspect bits) of the live scene.
    virtual void createSnapshot(const std::string& name, uint32_t aspects) = 0;
    // Re-apply the snapshot's saved aspects, filtered by `restoreAspects`
    // (scene restore replaces the current scene — one coarse checkpoint op).
    virtual void restoreSnapshot(size_t i, uint32_t restoreAspects) = 0;
    virtual void deleteSnapshot(size_t i) = 0;
    virtual void renameSnapshot(size_t i, const std::string& name) = 0;
    virtual void setSnapshotTags(size_t i, const std::vector<std::string>& tags) = 0;

    // ── Transform gizmo (behind the facade so panels avoid the Tools header) ─
    virtual bool gizmoEnabled() const = 0;
    virtual void setGizmoEnabled(bool on) = 0;
    virtual int  gizmoMode() const = 0; // 0=Translate 1=Rotate 2=Scale
    virtual void setGizmoMode(int mode) = 0;
    virtual bool gizmoLocalSpace() const = 0;
    virtual void setGizmoLocalSpace(bool local) = 0;
    virtual bool gizmoSnap() const = 0;
    virtual void setGizmoSnap(bool on) = 0;

    // ── Smart import (Pass 5 §11: one entry point for every source) ─────────
    // Plans + executes any mix of paths (models, clouds, .slpk, .scene), skips
    // duplicates, prettifies names, auto-groups a multi-file batch, selects it
    // and frames when the scene was empty. Drag-drop, the menu, the Welcome Hub
    // and the palette all funnel through this.
    virtual void importFiles(const std::vector<std::string>& paths) = 0;
    virtual void importFilesDialog() = 0; // one combined-filter dialog (Ctrl+I)
    virtual bool sceneEmpty() const = 0;   // drives the Welcome Hub
    virtual void addPrimitive(int type) = 0; // scene::PrimitiveType index

    // ── Autosave + crash recovery (Pass 5) ──────────────────────────────────
    virtual std::string autosaveStatus() const = 0; // "" or "Autosaved - 14:02"
    virtual bool        recoveryAvailable() const = 0;
    virtual std::string recoveryTimestamp() const = 0;
    virtual void        restoreLastSession() = 0;
    virtual void        discardRecovery() = 0;

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

    // ── Textures (Inspector Pass 2: load + assign a material texture slot) ───
    // Open an image file dialog; returns the chosen path ("" = cancelled).
    virtual std::string pickTextureFile() = 0;
    // Load `path` (Vulkan upload) and assign it to a material slot of the
    // (model, mesh)'s material — mesh = -1 targets every mesh of the model.
    // slot: 0 albedo (sRGB) · 1 normal · 2 metallic · 3 roughness · 4 ao.
    // Returns false on load failure (and toasts). Not undoable (a load; revert
    // with the per-slot Clear, which is a plain index write).
    virtual bool applyMaterialTexture(int model, int mesh, int slot,
                                      const std::string& path) = 0;

    // ── Point clouds ────────────────────────────────────────────────────────
    virtual size_t             pointCloudCount() const = 0;
    virtual const std::string& pointCloudName(size_t i) const = 0;
    virtual bool&              pointCloudVisible(size_t i) = 0;
    virtual uint32_t           pointCloudBatches(size_t i) const = 0;
    virtual uint32_t           pointCloudPoints(size_t i) const = 0;
    virtual double             pointCloudVramMB(size_t i) const = 0;
    virtual PointCloudProgress pointCloudProgress(size_t i) const = 0;
    virtual void               unloadPointCloud(size_t i) = 0;
    // Export cloud `i` (Inspector Pass 2 Export section; ExportService lands in
    // Pass 7). Opens a save dialog and writes the format (0 XYZ · 1 PCB · 2 HDF5
    // · 3 PLY), baking the transform when applyTransform; toasts the result.
    virtual void               exportPointCloud(size_t i, int format,
                                                bool applyTransform, bool plyBinary) = 0;

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
