#pragma once

#include "Core/Camera.h"
#include "Core/CommandRegistry.h"
#include "Core/Shortcuts.h"
#include "Core/UndoManager.h"
#include "Cursors/Base/CursorManager.h"
#include "Engine/Data.h"
#include "Engine/XRRuntimeInfo.h" // plain-data OpenXR runtime picker snapshot
#include "Gui/GuiSystem.h"
#include "Gui/Services.h" // Gui::ViewportPanelState (the Viewport window report)
#include "Gui/Settings.h"
#include "Platform/Window.h"
#include "Plugins/PluginManager.h"
#include "RHI/Device.h"
#include "RHI/ShaderCompiler.h"
#include "RHI/Swapchain.h"
#include "Renderer/OverlayDrawList.h"
#include "Renderer/Renderer.h"
#include "Scene/Scene.h"
#include "Tools/ClipPlaneTool.h"
#include "Tools/TransformGizmo.h"

#include <glm/glm.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

// OpenXR session (Phase 7b). Held by unique_ptr + forward-declared so the
// OpenXR/Vulkan-XR headers stay out of Application.h; only Application.cpp
// includes Engine/XRSession.h.
namespace Engine {
class XRSession;
}

// Abstract GUI services facade (Gui/Services.h); the concrete MainGuiServices
// below implements it in Application.cpp, exactly like MainPluginContext.
namespace Gui {
class Services;
}

namespace app {

// Concrete PluginContext / GUI services over the application state (both defined
// in Application.cpp).
class MainPluginContext;
class MainGuiServices;

// Stereo output mode (Phase 7). Off = mono. QuadBuffer = native quad-buffer
// present to a 3D display (2-layer swapchain; falls back to SideBySide when the
// surface can't present stereo). SideBySide = two half-width eyes in one mono
// window (works anywhere; a preview on non-stereo displays).
enum class StereoMode { Off, QuadBuffer, SideBySide };

// Owns the application: window, RHI, renderer, ImGui — and the main loop
// (poll → update → render → present). This replaces the old main.cpp
// orchestration; systems (scene, tools, GUI panels) mount here as their
// migration phases land.
class Application {
public:
    // Constructor + destructor are defined out-of-line in Application.cpp so the
    // unique_ptr<Engine::XRSession> member (forward-declared here) is destroyed
    // where the type is complete — an inline =default would need it in the ctor's
    // exception-cleanup path and fail to compile against the forward declaration.
    Application();
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void init();
    void run();
    void shutdown();

private:
    void initImGui();
    void shutdownImGui();
    void buildUi();
    void openModelDialog();      // File -> Import Model (Assimp import into scene_)
    void openPointCloudDialog();
    // Shared open paths (dialogs + window drag-drop route through these).
    void importModelFiles(const std::vector<std::string>& files);
    void loadPointCloudFiles(const std::vector<std::string>& files);

    // ---- SLPK / I3S scene layers (M0: open + inspect) ----
    // Parsing runs on a worker thread per package (pure CPU — archive mmap,
    // JSON, node tree, anchor math); pumpSlpkLoads() adopts finished layers
    // into scene_ on the main thread. No Vulkan anywhere in the M0 path.
    void openSlpkDialog();
    void openSlpk(const std::string& path);
    void pumpSlpkLoads();
    // M1: per-frame GPU-create pump for every layer's decoded payloads
    // (MeshBuffer/Texture creation under a time budget) + warning toasts.
    void pumpI3SLayers();
    void appendI3SOverlays();          // inspector OBBs -> overlay_
    void frameI3SLayer(size_t index);  // fly the camera to a layer's bounds
    void handleDroppedFiles();         // route window drag-drop by extension
    void handleResize();
    void loadScene();
    // ---- Docked viewports (GUI rework; up to renderer::kMaxViewports) ----
    // Reconcile the renderer's offscreen viewport outputs with the desired
    // config at a frame boundary (before ImGui::NewFrame — the last frame's
    // draw data must never reference a destroyed viewport texture). Docked
    // whenever the desktop owns the window (any stereo mode); XR keeps the
    // classic fullscreen path. Closed viewports are erased here; a panel
    // resize is applied only once the reported size has settled (equal on two
    // consecutive frames), so a splitter drag stretches the image instead of
    // rebuilding render targets every frame.
    void reconcileViewportOutput();
    // View menu: append a viewport (camera seeded from the active one).
    void addViewport();
    // The camera of viewport `index` (0 = the primary camera_) / of the
    // viewport the mouse is over (nav, picking and rays route to it).
    Camera& viewportCamera(size_t index);
    const Camera& viewportCamera(size_t index) const;
    Camera& activeCamera() { return viewportCamera(activeViewport_); }
    const Camera& activeCamera() const { return viewportCamera(activeViewport_); }
    // Derive this frame's effective 3D-view input (sceneInput_) after the GUI
    // built: from the hovered Viewport panel's report when docked, from the
    // raw window mouse + swapchain extent on the classic fullscreen path.
    // Also retargets activeViewport_ to the hovered viewport (never during an
    // active drag).
    void updateSceneInput();
    void updateCamera(float dt);
    // Distance-adaptive fly/zoom speed feed: samples the scene depth at the
    // screen centre from the async depth-pick readback (one frame late, no
    // stall — the GL app used a stalling glReadPixels for this), reconstructs
    // the world-space distance and drives Camera::AdjustMovementSpeed and the
    // zoom reference distance via UpdateDistanceToObject.
    void updateCameraDepth(float dt);
    // Per-view cameras of one viewport for this frame: mono fills view 0
    // (view 1 duplicated); stereo builds off-axis left/right eyes from
    // stereoSeparation_/Convergence_ (port of the GL PerspectiveProjection +
    // offset lookAt). Returns the active view count (1 or 2).
    uint32_t viewCameras(renderer::ViewCamera out[renderer::kMaxViews],
                         size_t viewportIndex = 0) const;
    // View + projection of the interaction eye (view 0 = left eye in stereo,
    // mono otherwise) of the ACTIVE viewport. Shared by the cursor
    // depth-picking reconstruction and picking rays so all use identical
    // matrices (the render submission builds every viewport's via viewCameras).
    void cameraMatrices(glm::mat4& view, glm::mat4& proj) const;
    // Applies a stereo-mode change (swapchain layers + renderer view count +
    // swapchain recreate); auto-downgrades QuadBuffer to SideBySide when the
    // surface can't present stereo. Called from the loop, never mid-frame.
    void applyStereoMode(StereoMode mode);
    // Eases stereoConvergence_ toward the scene depth at the screen centre when
    // auto-convergence is on (depth from the async pick readback, one frame late).
    void updateStereoConvergence(float dt);

    // ---- OpenXR / VR (Phase 7b) ----
    // Live GUI toggle exactly like the GL app: the session is created on enter
    // and destroyed on leave, so a desktop run never touches an OpenXR runtime
    // (no SteamVR spin-up, zero cost when off). Enter/leave run at a frame
    // boundary (device-idle rebuild of the scene target); never mid-frame.
    void enterXR();
    void leaveXR();
    bool xrRunning() const; // a session exists and initialized OK
    // One full OpenXR frame: pollEvents -> beginFrame -> per-eye HMD poses ->
    // acquire eye images -> renderer.renderFrameXR -> release -> endFrame. The
    // desktop window always gets ImGui (+ optional left-eye mirror) so the VR
    // toggle stays reachable. Returns the window present result.
    rhi::PresentResult renderXRFrame(renderer::FrameSubmission& submission);
    // Re-probe the system for installed OpenXR runtimes (registry + standard
    // install paths) to drive the picker; cached so it isn't run every frame.
    void refreshXRDiagnostics();
    // 3D cursor + overlay geometry for this frame (depth pick -> cursor world
    // pos -> overlay draw list + fragment-cursor state). No-ops while a camera
    // drag owns the mouse.
    void updateCursorAndOverlay(renderer::FrameSubmission& submission,
                                const glm::mat4& view, const glm::mat4& proj);
    void startOrbit();
    void beginNavCapture();
    void endNavCapture();
    // Move the OS mouse to the centre of the active viewport's 3D-view image
    // (host-window client coords; works for docked and dragged-out panels).
    // Fired by the double-click centering callback so the depth pick — and a
    // following orbit — continue from the freshly centred point.
    void warpMouseToViewportCenter();
    bool navActive() const { return orbiting_ || panning_ || rmbLooking_; }
    void buildFrameSubmission(renderer::FrameSubmission& submission) const;

    // ---- Commands, shortcuts, preferences (UI redesign Pass 0) ----
    // Every user-facing action is a Command run through commands_ (contract
    // C5); shortcuts_ maps rebindable key chords to command ids and replaces
    // the old hardcoded key handling. Both are fed by registerCommands()
    // (one place: command + menu grouping + default binding).
    void registerCommands();
    // Per-frame key-edge dispatch: bindings whose chord was pressed run their
    // command. Called from the keyboard block of updateCamera (same
    // WantCaptureKeyboard gate the hardcoded keys used).
    void dispatchShortcuts();
    // "view.center" (GL CenterView port): glide-center on the 3D cursor
    // point, else the selection, else the scene bounds.
    void centerViewOnCursor();

    // ---- Plugins / selection / picking (Phase 6) ----
    // MainPluginContext reaches these internals to service plugins/tools.
    friend class MainPluginContext;
    friend class MainGuiServices;
    // Model+mesh pick under the cursor on a non-drag Ctrl+left click (plain
    // LMB is orbit-only, like the GL app); drills into a sub-mesh when the
    // already-selected model is clicked again.
    void performSelectionClick();
    // The selected model (or sub-mesh) outline, appended to the overlay.
    void appendSelectionOverlay();
    // Bottom-right transient toast overlay (ImGui), decayed by frame dt.
    void drawToasts();
    void pushToast(const std::string& message, Plugins::ToastLevel level);
    // Bind the transform gizmo to the selected model's transform (or clear it).
    void bindGizmoToSelection();
    // Decide the gizmo's target this frame: the active clip plane (when the clip
    // tool is editing) takes precedence over the selected model.
    void updateGizmoBinding();
    // Capture the selected model's transform at gizmo drag start / record one
    // undo entry for the whole drag at drag end (no-op if nothing changed).
    void beginGizmoUndo();
    void finishGizmoUndo();
    // World-space ray under the mouse / through a 3D-viewport pixel (render-
    // target pixels, top-left origin), using this frame's view + projection.
    Plugins::PickRay mouseRayCurrent() const;
    Plugins::PickRay rayThroughPixel(glm::vec2 viewportPixel) const;
    int currentMods() const; // GLFW_MOD_* bitmask from ImGui's modifier state

    Platform::Window window_;
    rhi::Device device_;
    rhi::Swapchain swapchain_;
    rhi::ShaderCompiler shaderCompiler_;
    renderer::Renderer renderer_;

    // Interim scene host (full SceneManager returns in a later phase).
    scene::Scene scene_;

    // Consolidated, serialization-ready UI settings the GUI panels edit and the
    // render loop reads (camera / stereo / VR / lighting / sky / point-cloud /
    // cursor). Replaces the scattered per-setting members the debug panel used.
    Gui::Settings settings_;

    // Production GUI: dockspace host + panels (replaces the interim buildUi debug
    // panel) and the concrete services facade the panels talk through.
    Gui::GuiSystem guiSystem_;
    std::unique_ptr<Gui::Services> guiServices_;

    // Commands + rebindable shortcuts (UI redesign Pass 0). Registered once in
    // registerCommands(); menus/status bar render from commands_ via Services,
    // dispatchShortcuts() runs bindings, frecency rides preferences.json.
    core::CommandRegistry commands_;
    core::ShortcutMap shortcuts_;

    // Preferences persistence (Gui::Settings ⇄ preferences.json + shortcuts ⇄
    // shortcuts.json, both cwd-relative like the GL app). prefsReady_ gates
    // saving so a failed init can never clobber a user's file with defaults;
    // the snapshot string + timer drive the debounced save-on-change.
    bool prefsLoaded_ = false;
    bool prefsReady_ = false;
    std::string prefsSnapshot_;
    double prefsNextCheckTime_ = 0.0;

    // Loaded point clouds (Phase 4: parsers + GPU upload). Owned here until the
    // full SceneManager returns; streaming clouds are pumped every frame before
    // renderFrame. Render/load options live in settings_.pointCloud.
    std::vector<Engine::PointCloud> pointClouds_;

    // In-flight SLPK opens: one worker thread each, joined + adopted by
    // pumpSlpkLoads() (or shutdown()). unique_ptr keeps the atomic in place.
    struct SlpkLoadJob {
        std::thread thread;
        std::atomic<bool> done{ false };
        std::unique_ptr<scene::I3SSceneLayer> layer;
    };
    std::vector<std::unique_ptr<SlpkLoadJob>> slpkJobs_;

    // Phase 6: the real quaternion Camera (headers/Core/Camera.h), reused
    // almost verbatim from the GL app. LMB orbit / MMB pan / RMB free-look /
    // scroll zoom (to cursor) / WASDQE fly.
    Camera camera_{ glm::vec3(3.0f, 3.0f, 7.0f) };
    // FOV / speed / near / far live in settings_.camera.

    // ---- Stereo (Phase 7) ----
    StereoMode stereoMode_ = StereoMode::Off;          // currently applied
    bool stereoModePending_ = false;                   // a change is queued
    StereoMode stereoModeRequested_ = StereoMode::Off; // applied at the loop top
    // Per-mode stereo tunables (separation / convergence / flip / auto) live in
    // settings_.stereo; the applied mode + its pending/requested lifecycle stay
    // here because they drive swapchain/renderer rebuilds.

    // Dynamic (depth-driven) convergence: the zero-parallax plane follows the
    // scene depth at the screen centre, smoothed. Unlike the GL app (which read
    // the centre depth with a stalling glReadPixels every frame), this reuses the
    // async depth-pick readback — no pipeline stall.
    float targetConvergence_ = 2.6f; // settings_.stereo.convergence eases toward this

    // ---- OpenXR / VR (Phase 7b) ----
    // null unless VR is on. vrEnabled_ is the GUI toggle (the requested state);
    // it is reconciled against the live session at the loop top. A failed enter
    // resets vrEnabled_ so we never re-poke the runtime every frame.
    std::unique_ptr<Engine::XRSession> xrSession_;
    bool vrEnabled_ = false;
    // VR comfort/projection tunables (world scale, mirror, near/far, use-scene-
    // planes) live in settings_.vr.
    std::string vrStatus_;             // last session status / failure reason for the GUI
    Engine::XRDiagnostics xrDiag_;     // cached runtime-picker snapshot
    bool xrDiagValid_ = false;         // has xrDiag_ been probed yet
    // Desktop presentation state saved on enter, restored on leave: the window is
    // forced mono while the HMD is the stereo output, and switched to a
    // non-blocking present mode so the desktop vsync can't cap the HMD frame loop.
    StereoMode vrSavedStereoMode_ = StereoMode::Off;
    VkPresentModeKHR vrSavedPresentMode_ = VK_PRESENT_MODE_FIFO_KHR;

    // ---- Docked viewports (scene rendered offscreen, shown as GUI windows) ----
    // One entry per viewport window. Index 0 is the primary (its camera is
    // camera_ below); secondary viewports own their cameras. `name` is the
    // stable ImGui window identity (imgui.ini docking keys on it), derived
    // from `id` — a persistent per-viewport slot that survives siblings
    // closing (vector indices shift, ids do not).
    struct AppViewport {
        int id = 0;
        std::string name;
        Camera camera{ glm::vec3(3.0f, 3.0f, 7.0f) }; // unused for index 0
        // What this Viewport panel reported this GUI frame (via MainGuiServices).
        Gui::ViewportPanelState ui;
        // Last frame's desired texture size — a resize is applied only when the
        // report repeats (settle test), so splitter drags don't rebuild per frame.
        VkExtent2D sizeWant{ 0, 0 };
        bool open = true; // false = close requested; erased at reconcile
    };
    std::vector<AppViewport> viewports_;
    // The viewport the mouse is over (last one hovered; frozen during drags):
    // navigation, picking rays, the 3D cursor and depth queries target it.
    uint32_t activeViewport_ = 0;
    // This frame's effective 3D-view input, derived in updateSceneInput():
    // every interaction consumer (camera nav, picking rays, depth queries,
    // cursor, plugins) reads the 3D view through this — never raw window
    // coordinates — so docked and classic fullscreen paths behave identically.
    struct SceneInput {
        bool hovered = false;         // pointer over the 3D view; nav may start
        glm::vec2 mousePx{ 0.0f };    // mouse in render-target pixels
        glm::vec2 sizePx{ 1.0f };     // render-target size
        glm::vec2 screenPos{ 0.0f };  // view rect on screen (HUD/label space)
        glm::vec2 screenSize{ 1.0f };
        GLFWwindow* hostWindow = nullptr; // OS window hosting the 3D view
    };
    SceneInput sceneInput_;

    // Navigation drag state (edge-detected from polled mouse buttons). While
    // any is active the OS cursor is disabled for unbounded travel and the 3D
    // cursor / depth pick is frozen.
    bool orbiting_ = false;
    bool panning_ = false;
    bool rmbLooking_ = false;
    GLFWwindow* navWindow_ = nullptr; // window that owns the cursor capture
    double lastMouseX_ = 0.0;
    double lastMouseY_ = 0.0;
    double lastFrameTime_ = 0.0;

    // ---- 3D cursor + overlays (Phase 6) ----
    Cursor::CursorManager cursorManager_;
    renderer::OverlayDrawList overlay_; // rebuilt each frame; alive across renderFrame
    // 3D-cursor show/type live in settings_.cursor; zoom/orbit-around-cursor in
    // settings_.camera.

    // ---- Selection, undo, plugins (Phase 6) ----
    // Selection into scene_.models; mesh -1 = whole model, else a specific
    // sub-mesh. Picked with the depth-point + AABB test (no per-triangle work).
    struct Selection { int model = -1; int mesh = -1; };
    Selection selection_;

    // Undo/redo owned here and handed to tools/plugins through the context.
    core::UndoManager undo_;

    // Transform gizmo: bound to the selected model each frame, edits its
    // position/rotation/scale in place. Drag lifecycle + undo owned by the app.
    Tools::TransformGizmo gizmo_;
    bool gizmoDragging_ = false;
    int gizmoUndoModel_ = -1;              // model captured at drag start
    glm::vec3 gizmoUndoPos_{ 0.0f };
    glm::vec3 gizmoUndoRot_{ 0.0f };
    glm::vec3 gizmoUndoScale_{ 1.0f };
    // (The gizmo mode keys and Ctrl+Z/Y are rebindable commands now — see
    // registerCommands/dispatchShortcuts; the old per-key edge flags are gone.)

    // Action-key edges dispatched to plugins (Enter / KP-Enter / Delete /
    // Backspace) — e.g. the MeasurementPlugin's finish / cancel / undo-point.
    bool prevEnter_ = false, prevKpEnter_ = false;
    bool prevDelete_ = false, prevBackspace_ = false;

    // Clip / section planes. Editing reuses the gizmo (translate = slide the
    // plane, rotate = steer the normal); the packed planes feed
    // FrameSubmission::clipPlanes. gizmoTargetPlane_ = the gizmo currently edits
    // a plane (vs. a model) so the drag sync + undo route to the tool.
    Tools::ClipPlaneTool clipPlaneTool_;
    bool gizmoTargetPlane_ = false;

    // Static plugins + the context that bridges them to this Application.
    Plugins::PluginManager pluginManager_;
    std::unique_ptr<Plugins::PluginContext> pluginContext_;

    // Transient toast notifications (bottom-right overlay).
    struct Toast { std::string text; Plugins::ToastLevel level; float ttl; };
    std::vector<Toast> toasts_;

    // This frame's mouse-wheel delta, captured in run() BEFORE ImGui::Render():
    // ImGui::EndFrame() zeroes io.MouseWheel, and updateCamera runs after the
    // GUI pass — reading io.MouseWheel there always saw 0 (dead scroll zoom).
    float wheelThisFrame_ = 0.0f;

    // Mouse-button edge state for plugin input dispatch + click-vs-drag
    // selection. "*Owned_" latches a button a plugin consumed until release.
    bool prevLmb_ = false, prevMmb_ = false, prevRmb_ = false;
    bool lmbOwned_ = false, mmbOwned_ = false, rmbOwned_ = false;
    glm::vec2 lmbPressPos_{ 0.0f };
    float lmbDragDist_ = 0.0f;       // accumulated |delta| during an LMB drag
    bool lmbClickCandidate_ = false; // press started a potential (non-drag) click
    bool clickCursorValid_ = false;  // the 3D cursor was over geometry at press
    glm::vec3 clickCursorWorld_{ 0.0f };

    VkDescriptorPool imguiDescriptorPool_ = VK_NULL_HANDLE;
    // Backing store for ImGui_ImplVulkan_InitInfo::PipelineRenderingCreateInfo
    // ::pColorAttachmentFormats. The backend keeps that POINTER for its whole
    // lifetime (secondary-viewport pipelines are created lazily on first
    // drag-out), so it must not point at a stack local.
    VkFormat imguiColorFormat_ = VK_FORMAT_UNDEFINED;
    bool imguiInitialized_ = false;

    // Debug-panel state: deferred present-mode switch (applied at the top of
    // the next loop iteration, never mid-frame) and a recreate counter that
    // makes a recreate storm visible at a glance.
    VkPresentModeKHR pendingPresentMode_ = VK_PRESENT_MODE_MAX_ENUM_KHR;
    uint32_t swapchainRecreations_ = 0;
};

} // namespace app
