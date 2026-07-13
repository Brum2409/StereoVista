#pragma once

#include "Core/Camera.h"
#include "Core/CommandRegistry.h"
#include "Core/Shortcuts.h"
#include "Core/ToolManager.h"
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
#include "Scene/SceneDocument.h" // scene::PendingLayerState / load results
#include "Scene/SceneItems.h"    // scene::SceneItemRef / scene::Selection
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

    // ---- Scene document (UI redesign Pass 1; implemented in SceneOps.cpp) ----
    // new / open / save / save-as / merge over scene::SceneDocument, with the
    // replace-or-merge-or-ask flow (remembered via Settings::Files, C8), a
    // recents list, and async SLPK re-open with saved identity/display state.
    void newScene();
    void openSceneDialog();
    void openSceneFile(const std::string& path); // honors openSceneMode
    void mergeSceneDialog();
    bool saveScene();       // current path, else save-as dialog
    bool saveSceneAs();
    void resolvePendingSceneOpen(int action, bool remember); // 0/1/2 = cancel/replace/merge
    void replaceSceneFromFile(const std::string& path);
    void mergeSceneFromFile(const std::string& path);
    void addRecentScene(const std::string& path);
    // Drop every object from scene_ safely (I3S GPU release + device idle) —
    // shared by New and Replace.
    void clearSceneContent();
    // Toast the load report's warnings (asset misses are never silent).
    void reportSceneLoad(const scene::SceneLoadReport& report);
    // Apply a loaded camera pose / scene-authored sun+sky (Application.cpp).
    void applyLoadedCamera(const scene::SceneCameraState& cam);
    void applyLoadedEnvironment(const scene::SceneEnvironmentState& env);

    // ---- Snapshots (Pass 3 §9; src/App/Snapshots.cpp) ----
    // A named checkpoint: camera pose and/or a scene serialized to a snapshots/
    // file (scene state can't be copied in-memory — MeshBuffers are GPU-backed
    // move-only). Session-scoped for now (cross-restart persistence lands with
    // autosave, Pass 5).
    struct SnapshotEntry {
        std::string name;
        std::string timestamp;
        std::string filePath;    // saved .scene ("" when no scene aspect)
        uint32_t    aspects = 0; // Gui::SnapshotAspect bits
        std::vector<std::string> tags;
        Camera::CameraState camera{};
    };
    // ---- Smart import + autosave + recovery (Pass 5 §11; src/App/ImportOps.cpp) ----
    // ONE entry point for menu / drag-drop / hub / palette imports: plans every
    // path (core::ImportService), skips duplicates, runs the existing loaders,
    // then prettifies names, auto-groups a multi-file batch, selects it and
    // frames when the scene was empty.
    void importFiles(const std::vector<std::string>& paths);
    void importFilesDialog(); // one combined-filter dialog (Ctrl+I)
    bool sceneEmpty() const;
    void addPrimitive(int type); // scene::PrimitiveType index; selects the result

    void maybeAutosave(double now); // called from the frame loop
    void initSession();             // session.lock: detect an unclean shutdown
    void endSession();              // remove the lock (clean exit)
    bool recoveryAvailable() const { return recoveryAvailable_; }
    const std::string& recoveryTimestamp() const { return recoveryStamp_; }
    void restoreLastSession();
    void discardRecovery();
    const std::string& autosaveStatus() const { return autosaveStatus_; }
    // The camera + environment block every v3 save writes (was duplicated in
    // saveScene and Snapshots; factored here).
    scene::SceneSaveState currentSaveState() const;

    void createSnapshot(const std::string& name, uint32_t aspects);
    void restoreSnapshot(size_t index, uint32_t restoreAspects);
    void deleteSnapshot(size_t index);
    // Auto-capture a scene safety snapshot before a destructive Replace, when
    // the pref is on and the current scene is non-empty (§9). No-op otherwise.
    void maybeSafetySnapshotBeforeReplace();
    const std::vector<SnapshotEntry>& snapshots() const { return snapshots_; }
    std::vector<SnapshotEntry>& snapshots() { return snapshots_; }

    // ---- Outliner item operations (Pass 1; SceneOps.cpp) ----
    // Each call = one undoable step (C4); refs resolve by ObjectId (C3).
    void deleteItems(const std::vector<scene::SceneItemRef>& refs);
    void duplicateItems(const std::vector<scene::SceneItemRef>& refs);
    void setItemsVisible(const std::vector<scene::SceneItemRef>& refs, bool visible);
    void setItemsLocked(const std::vector<scene::SceneItemRef>& refs, bool locked);
    uint64_t groupItems(const std::vector<scene::SceneItemRef>& refs);
    void ungroupItems(const std::vector<scene::SceneItemRef>& refs);
    void moveItemsToGroup(const std::vector<scene::SceneItemRef>& refs,
                          uint64_t groupId);
    void renameItem(const scene::SceneItemRef& ref, const std::string& name);
    void frameItems(const std::vector<scene::SceneItemRef>& refs);
    // Frame in a SPECIFIC viewport (the Pass-8 toolbar passes its own index, so
    // it can never drive viewport 0 by accident — §5.2).
    void frameItemsIn(uint32_t viewport, const std::vector<scene::SceneItemRef>& refs);
    // Standard views (Pass 8 §14): 0 top · 1 bottom · 2 front · 3 back ·
    // 4 right · 5 left · 6 iso. Fits the selection, else the whole scene.
    void applyStandardView(uint32_t viewport, int view);
    // Union world bounds of refs (groups expand). False when nothing contributed.
    // Not const: expandGroups resolves refs against the scene (it refreshes the
    // cached indices), which needs a mutable Scene&.
    bool itemsBounds(const std::vector<scene::SceneItemRef>& refs, glm::vec3& outLo,
                     glm::vec3& outHi);
    // Fit a bounding sphere into the vertical FOV along viewDir and fly there
    // (animated unless reduce-motion is on — the old frame always snapped).
    void flyCameraTo(uint32_t viewport, const glm::vec3& center, float radius,
                     const glm::vec3& viewDir);
    void isolateItems(const std::vector<scene::SceneItemRef>& refs);
    void exitIsolate();
    // Esc cascade (select.clear command): exit the active tool first (clip
    // editing; the measurement plugin consumes Esc itself), then clear the
    // selection. Returns false when there was nothing to do.
    bool escapeAction();
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
    // THE single owner of OS-cursor visibility over the 3D view. Records the
    // intent (osCursorHidden_) and applies it to the window hosting the view —
    // unless a nav capture owns the mode (GLFW_CURSOR_DISABLED), which is
    // stronger than either. Everything that wants the Windows cursor gone over
    // the 3D cursor goes through here; nothing else calls glfwSetInputMode with
    // GLFW_CURSOR_HIDDEN/NORMAL. See pushOsCursorToImGui for why that matters.
    void setOsCursorHidden(bool hidden);
    // Re-assert osCursorHidden_ to ImGui right before ImGui_ImplGlfw_NewFrame.
    // The ImGui GLFW backend rewrites the GLFW cursor mode from
    // ImGui::GetMouseCursor() on every NewFrame (imgui_impl_glfw.cpp), and it
    // only stands down for GLFW_CURSOR_DISABLED — so a plain HIDDEN set by us
    // was flipped back to NORMAL at the top of the next frame and re-hidden
    // later in it, flashing the Windows cursor for a frame (most visibly on the
    // press/release edges of an orbit, where the mode leaves/returns to
    // DISABLED). Asking for ImGuiMouseCursor_None instead makes the backend hide
    // it FOR us: one owner, no fight, no flash.
    void pushOsCursorToImGui() const;
    // Move the OS mouse onto the screen projection of a world point in the
    // active viewport. The GL CursorSynchronizer::worldToScreen + warp, made
    // Vulkan-native (the projection already bakes the Y-flip). Returns false and
    // does NOT move the mouse when the point is behind the eye or off-screen —
    // the anchor is gone, and faking an edge-clamped one (as GL did) would pin
    // the cursor to the wrong geometry.
    bool warpMouseToWorldPoint(const glm::vec3& world);
    // Pin the 3D cursor to a world point, re-anchor the OS mouse onto it, hide
    // the OS cursor, and hold that state for a few frames to bridge the async
    // depth-readback latency. The single choke-point for cursor continuity after
    // orbit/pan/look, centering animations, and gizmo drags.
    void syncCursorToWorld(const glm::vec3& world);
    // Called at the end of an orbit/pan/look drag: sync to the pinned cursor
    // point, or (nothing was under the cursor when the drag began) let the OS
    // cursor show over the background — mirrors the GL release path.
    void finishCameraOpCursor();
    bool navActive() const { return orbiting_ || panning_ || rmbLooking_; }
    void buildFrameSubmission(renderer::FrameSubmission& submission) const;

    // ---- Commands, shortcuts, preferences (UI redesign Pass 0) ----
    // Every user-facing action is a Command run through commands_ (contract
    // C5); shortcuts_ maps rebindable key chords to command ids and replaces
    // the old hardcoded key handling. Both are fed by registerCommands()
    // (one place: command + menu grouping + default binding).
    void registerCommands();
    // Register the interactive tools (modes) + their commands (Pass 7 §13).
    // Called after the plugins load, since measurement lives in one.
    void registerTools();
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
    // Ctrl+drag grab-to-move. beginModelDrag() latches the grab point + depth
    // and snapshots the selection for undo; updateModelDrag() follows the cursor
    // (and integrates the wheel's depth push) each frame; endModelDrag() commits
    // the drag as one undo entry. Returns false from begin if nothing draggable
    // is under the press (caller falls back to orbit).
    bool beginModelDrag();
    void updateModelDrag(float dt);
    void endModelDrag();
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
    core::ToolManager toolManager_; // Pass 7: the one tool (mode) registry

    // Preferences persistence (Gui::Settings ⇄ preferences.json + shortcuts ⇄
    // shortcuts.json, both cwd-relative like the GL app). prefsReady_ gates
    // saving so a failed init can never clobber a user's file with defaults;
    // the snapshot string + timer drive the debounced save-on-change.
    bool prefsLoaded_ = false;
    bool prefsReady_ = false;
    std::string prefsSnapshot_;
    double prefsNextCheckTime_ = 0.0;

    // Point clouds live on scene_.pointClouds since UI redesign Pass 1 ("one
    // heart"); streaming clouds are still pumped every frame before
    // renderFrame. Render/load options live in settings_.pointCloud.

    // ---- Scene document state (Pass 1) ----
    std::string scenePath_;       // current document ("" = untitled)
    std::string pendingOpenPath_; // parked while the ask-modal is up (C8)
    // ---- Snapshots (Pass 3) ----
    std::vector<SnapshotEntry> snapshots_;
    int snapshotCounter_ = 0; // unique snapshot scene-file names this session
    // ---- Autosave + crash recovery (Pass 5) ----
    double autosaveNextTime_ = 0.0; // glfwGetTime() of the next autosave check
    int autosaveSlot_ = 0;          // rotates over settings_.files.autosaveSlots
    std::string autosaveStatus_;    // "Autosaved - 14:02" (status-bar whisper)
    bool recoveryAvailable_ = false;
    std::string recoveryPath_;      // newest autosave from the crashed session
    std::string recoveryStamp_;     // its clock stamp (read from session.lock)
    // Saved identity/display state for layers whose async SLPK re-open is in
    // flight (scene load + delete-undo); consumed by pumpSlpkLoads on adopt.
    std::vector<scene::PendingLayerState> pendingLayerStates_;
    // Open jobs whose result should be dropped on adoption (a redo re-deleted
    // a layer whose undo re-open was still parsing). Matched by source path.
    std::vector<std::string> cancelledLayerOpens_;
    // Isolate (Outliner): saved visibility to restore on exit.
    struct IsolateEntry { scene::SceneItemRef ref; bool visible; };
    struct IsolateState {
        bool active = false;
        std::vector<IsolateEntry> saved;
    };
    IsolateState isolate_;

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

    // ---- 3D-cursor continuity (port of the GL CursorSynchronizer) ----
    // After any view change that ends with the 3D cursor pinned to a world
    // point (orbit/pan/look end, centering-animation end, gizmo-drag end), the
    // OS mouse is re-anchored onto that point's screen projection and the cursor
    // is HELD there for a few frames. The hold bridges the async depth
    // readback's latency (the mouse-driven pick is ~kFramesInFlight frames
    // stale) so the cursor never flashes the Windows cursor or jumps to whatever
    // the stale sample reports before a fresh pick at the settled position
    // lands. See syncCursorToWorld / updateCursorAndOverlay.
    glm::vec3 cursorSyncWorld_{ 0.0f };
    int cursorSyncHold_ = 0;
    // Whether the Windows cursor should currently be invisible over the 3D view.
    // Sticky across frames (a frame that resolves nothing keeps the last state)
    // and re-asserted to ImGui every frame — see setOsCursorHidden /
    // pushOsCursorToImGui.
    bool osCursorHidden_ = false;
    // World point a double-click centering glide is centring on; the completion
    // callback syncs the cursor onto it (it ends up at the viewport centre).
    glm::vec3 centeringTarget_{ 0.0f };

    // ---- 3D cursor + overlays (Phase 6) ----
    Cursor::CursorManager cursorManager_;
    renderer::OverlayDrawList overlay_; // rebuilt each frame; alive across renderFrame
    // 3D-cursor show/type live in settings_.cursor; zoom/orbit-around-cursor in
    // settings_.camera.

    // ---- Selection, undo, plugins (Phase 6 → Pass 1 multi-select) ----
    // The application-wide, ObjectId-based, ordered multi-selection (grown
    // from the old {int model; int mesh;} seed — contract C3). Primary = last
    // added; the gizmo binds to it. Legacy index-based consumers go through
    // selectedModelIndex()/selectedMeshIndex().
    scene::Selection selection_;
    // Primary-selection index shims (resolve by id each call; -1 = none).
    int selectedModelIndex() const;
    int selectedMeshIndex() const;
    void setSelectionIndices(int model, int mesh); // build a ref from indices

    // Undo/redo owned here and handed to tools/plugins through the context.
    core::UndoManager undo_;

    // Transform gizmo: bound to the PRIMARY selected object (model or point
    // cloud) each frame, edits its transform in place; the primary's per-drag
    // delta is mirrored onto every other selected transformable each frame
    // (multi-select transforms, §7.1). Drag lifecycle + undo owned by the app:
    // the snapshot below captures every selected transform at drag start —
    // entry 0 is the primary — and the whole drag records as ONE undo step by
    // ObjectId (C4).
    Tools::TransformGizmo gizmo_;
    bool gizmoDragging_ = false;
    struct GizmoSnapshot {
        scene::SceneItemRef ref;
        glm::vec3 pos{ 0.0f };
        glm::vec3 rot{ 0.0f };
        glm::vec3 scale{ 1.0f };
    };
    std::vector<GizmoSnapshot> gizmoUndo_;
    // Mirror the primary's delta since drag start onto the other snapshot
    // entries (translate: add; rotate: add Euler degrees about each object's
    // own pivot; scale: multiply component-wise).
    void applyGizmoDeltaToSelection();
    // (The gizmo mode keys and Ctrl+Z/Y are rebindable commands now — see
    // registerCommands/dispatchShortcuts; the old per-key edge flags are gone.)

    // Action-key edges dispatched to plugins (Enter / KP-Enter / Delete /
    // Backspace / Escape) — e.g. the MeasurementPlugin's finish / cancel /
    // undo-point / exit. A key a plugin consumed is suppressed for this
    // frame's shortcut dispatch (Esc must exit the measurement tool WITHOUT
    // also clearing the selection).
    bool prevEnter_ = false, prevKpEnter_ = false;
    bool prevDelete_ = false, prevBackspace_ = false;
    bool prevEscape_ = false;
    std::vector<int> pluginConsumedKeys_; // GLFW keys eaten this frame

    // Clip / section planes. Editing reuses the gizmo (translate = slide the
    // plane, rotate = steer the normal); the packed planes feed
    // FrameSubmission::clipPlanes. gizmoTargetPlane_ = the gizmo currently edits
    // a plane (vs. a model) so the drag sync + undo route to the tool.
    Tools::ClipPlaneTool clipPlaneTool_;
    bool gizmoTargetPlane_ = false;

    // Static plugins + the context that bridges them to this Application.
    Plugins::PluginManager pluginManager_;
    std::unique_ptr<Plugins::PluginContext> pluginContext_;

    // Transient toast notifications (bottom-right overlay). Each toast is its own
    // ImGui window so it can slide in on its own and the stack can re-settle when
    // one below it expires; `age` drives the entrance, `ttl` the exit, and `id`
    // keeps the animation state attached to THIS toast as the vector shifts.
    struct Toast {
        std::string text;
        Plugins::ToastLevel level;
        float ttl = 0.0f;
        float age = 0.0f;
        unsigned long long id = 0;
    };
    std::vector<Toast> toasts_;
    unsigned long long nextToastId_ = 0;

    // This frame's mouse-wheel delta, captured in run() BEFORE ImGui::Render():
    // ImGui::EndFrame() zeroes io.MouseWheel, and updateCamera runs after the
    // GUI pass — reading io.MouseWheel there always saw 0 (dead scroll zoom).
    float wheelThisFrame_ = 0.0f;

    // Mouse-button edge state for plugin input dispatch. A Ctrl+left press
    // selects immediately (see performSelectionClick); "*Owned_" latches a
    // button a plugin consumed until release.
    bool prevLmb_ = false, prevMmb_ = false, prevRmb_ = false;
    bool lmbOwned_ = false, mmbOwned_ = false, rmbOwned_ = false;
    bool clickCursorValid_ = false;  // the 3D cursor was over geometry at press
    glm::vec3 clickCursorWorld_{ 0.0f };

    // ---- Ctrl+drag "grab-to-move" (GL-parity: hold Ctrl to slide the selected
    // object under the cursor; the wheel pushes it in depth). Reuses the gizmo
    // undo machinery, so a whole drag is ONE undo entry and mirrors onto every
    // selected transformable. Vulkan-native: the OS cursor stays visible and the
    // object tracks it via a ray∩view-plane solve (no hidden-cursor warp hack,
    // and depth stays under the cursor instead of drifting along camera-front as
    // the GL build did). ----
    bool draggingModel_ = false;      // a Ctrl+drag is actively moving the selection
    float dragViewDepth_ = 0.0f;      // view-space depth of the drag plane (dist along Front)
    glm::vec3 dragGrabOffset_{ 0.0f };// primary.position − grab point, held constant
    float dragDepthVel_ = 0.0f;       // wheel-driven depth velocity (smooth push/pull)
    // The grabbed spot ON the mesh, in world space, re-solved every frame — it
    // rides the object (mouse AND wheel). The 3D cursor is pinned here for the
    // whole drag, and held here on release, so it never jumps. Under the mouse
    // by construction, which is why the Windows cursor can simply hide.
    glm::vec3 dragGrabWorld_{ 0.0f };

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
