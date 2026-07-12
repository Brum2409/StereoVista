#include "App/Application.h"

#include "Core/Profiling.h"
#include "Gui/Panels.h"      // Gui::Windows titles (panel-toggle command labels)
#include "Gui/Preferences.h" // Gui::Settings ⇄ preferences.json (Pass 0)
#include "Gui/Services.h" // abstract facade implemented by MainGuiServices below

#include "Engine/Screenshot.h"
#include "Engine/XRSession.h" // OpenXR (Vulkan binding); Windows-only, guarded inside
#include "Loaders/PointCloudLoader.h"
#include "Platform/Paths.h"
#include "Renderer/PointCloudGpu.h"
#include "Renderer/Projection.h"

#include <GLFW/glfw3.h>

#include <glm/gtc/matrix_transform.hpp>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include "imgui/imgui_sytle.h"

#include <portable-file-dialogs.h>
#include <stb_image.h>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace app {

namespace {

constexpr float kClickThresholdPx = 6.0f; // max mouse travel that still counts
                                          // as a click (vs. an orbit drag)

// Runtime configuration files, cwd-relative like the GL app (and like
// pipeline_cache.bin / imgui.ini).
constexpr const char* kPreferencesFile = "preferences.json";
constexpr const char* kShortcutsFile = "shortcuts.json";

void checkImGuiVkResult(VkResult result) {
    if (result != VK_SUCCESS)
        std::cerr << "[imgui][vulkan] VkResult " << static_cast<int>(result) << "\n";
}

// GLFW key code -> ImGuiKey for the shortcut dispatch (the backend's own
// translator is file-static, so it can't be reused here). Covers the
// bindable keyboard; unknown keys return ImGuiKey_None and never match.
ImGuiKey imGuiKeyFromGlfw(int key) {
    if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
        return static_cast<ImGuiKey>(ImGuiKey_A + (key - GLFW_KEY_A));
    if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
        return static_cast<ImGuiKey>(ImGuiKey_0 + (key - GLFW_KEY_0));
    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
        return static_cast<ImGuiKey>(ImGuiKey_F1 + (key - GLFW_KEY_F1));
    if (key >= GLFW_KEY_KP_0 && key <= GLFW_KEY_KP_9)
        return static_cast<ImGuiKey>(ImGuiKey_Keypad0 + (key - GLFW_KEY_KP_0));
    switch (key) {
    case GLFW_KEY_SPACE:         return ImGuiKey_Space;
    case GLFW_KEY_APOSTROPHE:    return ImGuiKey_Apostrophe;
    case GLFW_KEY_COMMA:         return ImGuiKey_Comma;
    case GLFW_KEY_MINUS:         return ImGuiKey_Minus;
    case GLFW_KEY_PERIOD:        return ImGuiKey_Period;
    case GLFW_KEY_SLASH:         return ImGuiKey_Slash;
    case GLFW_KEY_SEMICOLON:     return ImGuiKey_Semicolon;
    case GLFW_KEY_EQUAL:         return ImGuiKey_Equal;
    case GLFW_KEY_LEFT_BRACKET:  return ImGuiKey_LeftBracket;
    case GLFW_KEY_BACKSLASH:     return ImGuiKey_Backslash;
    case GLFW_KEY_RIGHT_BRACKET: return ImGuiKey_RightBracket;
    case GLFW_KEY_GRAVE_ACCENT:  return ImGuiKey_GraveAccent;
    case GLFW_KEY_ESCAPE:        return ImGuiKey_Escape;
    case GLFW_KEY_ENTER:         return ImGuiKey_Enter;
    case GLFW_KEY_TAB:           return ImGuiKey_Tab;
    case GLFW_KEY_BACKSPACE:     return ImGuiKey_Backspace;
    case GLFW_KEY_INSERT:        return ImGuiKey_Insert;
    case GLFW_KEY_DELETE:        return ImGuiKey_Delete;
    case GLFW_KEY_RIGHT:         return ImGuiKey_RightArrow;
    case GLFW_KEY_LEFT:          return ImGuiKey_LeftArrow;
    case GLFW_KEY_DOWN:          return ImGuiKey_DownArrow;
    case GLFW_KEY_UP:            return ImGuiKey_UpArrow;
    case GLFW_KEY_PAGE_UP:       return ImGuiKey_PageUp;
    case GLFW_KEY_PAGE_DOWN:     return ImGuiKey_PageDown;
    case GLFW_KEY_HOME:          return ImGuiKey_Home;
    case GLFW_KEY_END:           return ImGuiKey_End;
    case GLFW_KEY_PAUSE:         return ImGuiKey_Pause;
    case GLFW_KEY_KP_DECIMAL:    return ImGuiKey_KeypadDecimal;
    case GLFW_KEY_KP_DIVIDE:     return ImGuiKey_KeypadDivide;
    case GLFW_KEY_KP_MULTIPLY:   return ImGuiKey_KeypadMultiply;
    case GLFW_KEY_KP_SUBTRACT:   return ImGuiKey_KeypadSubtract;
    case GLFW_KEY_KP_ADD:        return ImGuiKey_KeypadAdd;
    case GLFW_KEY_KP_ENTER:      return ImGuiKey_KeypadEnter;
    case GLFW_KEY_KP_EQUAL:      return ImGuiKey_KeypadEqual;
    default:                     return ImGuiKey_None;
    }
}

// Reverse translation for the Pass-6 shortcut-capture UI. Done by searching the
// forward map rather than hand-writing a second table, so the switch above stays
// the SINGLE source of truth (a second table would silently drift).
int glfwKeyFromImGui(ImGuiKey key) {
    if (key == ImGuiKey_None)
        return 0;
    for (int glfwKey = GLFW_KEY_SPACE; glfwKey <= GLFW_KEY_LAST; ++glfwKey)
        if (imGuiKeyFromGlfw(glfwKey) == key)
            return glfwKey;
    return 0;
}

} // namespace

// ============================================================================
// MainPluginContext — bridges the static plugins to this Application's state.
// A friend of Application, it forwards each PluginContext service to the
// matching member or helper. Constructed once in init(); one instance serves
// every plugin. Picking reuses the GPU depth pick (exact surface point) plus a
// cheap AABB object test — no per-triangle work, no retained geometry.
// ============================================================================
class MainPluginContext : public Plugins::PluginContext {
public:
    explicit MainPluginContext(Application& app) : app_(app) {}

    scene::Scene&      scene() override           { return app_.scene_; }
    // Camera of the ACTIVE viewport — plugin picking/interaction follows the
    // viewport the mouse is over, like every other input path.
    const Camera&      camera() const override    { return app_.activeCamera(); }
    glm::vec3          cameraPosition() const override { return app_.activeCamera().Position; }
    core::UndoManager& undo() override            { return app_.undo_; }

    // The persisted user settings (preferences.json persistence is app-owned).
    Gui::Settings& preferences() override { return app_.settings_; }

    renderer::OverlayDrawList& overlay() override { return app_.overlay_; }

    Plugins::PickRay mouseRay() const override { return app_.mouseRayCurrent(); }

    bool cursorWorldPos(glm::vec3& out) const override {
        if (!app_.cursorManager_.isCursorPositionValid())
            return false;
        out = app_.cursorManager_.getCursorPosition();
        return true;
    }

    bool raycastModels(scene::RayHit& out) const override {
        glm::vec3 point;
        if (!cursorWorldPos(point)) {
            out = scene::RayHit{};
            return false;
        }
        return scene::pickModelAtPoint(app_.scene_, point, out);
    }

    // Plugin selection API stays index-based (its public contract); it reads
    // and writes the PRIMARY item of the app-wide multi-selection.
    int  selectedModel() const override { return app_.selectedModelIndex(); }
    int  selectedMesh() const override  { return app_.selectedMeshIndex(); }
    void setSelection(int model, int mesh) override {
        app_.setSelectionIndices(model, mesh);
    }

    // 3D-viewport-local input (docked viewport image or the full framebuffer
    // on the classic fullscreen path) — see Application::updateSceneInput.
    glm::vec2 mousePos() const override { return app_.sceneInput_.mousePx; }
    int keyMods() const override { return app_.currentMods(); }
    glm::vec2 viewportSize() const override { return app_.sceneInput_.sizePx; }
    glm::vec2 viewportScreenPos() const override { return app_.sceneInput_.screenPos; }
    glm::vec2 viewportScreenSize() const override { return app_.sceneInput_.screenSize; }
    glm::mat4 viewProj() const override {
        glm::mat4 view(1.0f), proj(1.0f);
        app_.cameraMatrices(view, proj);
        return proj * view;
    }

    void toast(const std::string& message, Plugins::ToastLevel level) override {
        app_.pushToast(message, level);
    }

private:
    Application& app_;
};

// ============================================================================
// MainGuiServices — the concrete Gui::Services the GUI panels talk through.
// A friend of Application (like MainPluginContext), it forwards each service to
// the matching member/helper and does all the Vulkan-touching work (present
// modes, tonemap, diagnostics, screenshots) so the Gui/ module stays pure ImGui
// over plain data. Constructed once in init(); one instance serves every panel.
// ============================================================================
class MainGuiServices : public Gui::Services {
public:
    explicit MainGuiServices(Application& app) : app_(app) {}

    Gui::Settings& settings() override { return app_.settings_; }

    Camera&                 camera() override        { return app_.camera_; }
    scene::Scene&           scene() override         { return app_.scene_; }
    Cursor::CursorManager&  cursors() override       { return app_.cursorManager_; }
    core::UndoManager&      undo() override           { return app_.undo_; }
    Tools::ClipPlaneTool&   clipTool() override      { return app_.clipPlaneTool_; }
    Plugins::PluginManager& plugins() override       { return app_.pluginManager_; }
    Plugins::PluginContext& pluginContext() override { return *app_.pluginContext_; }

    core::CommandRegistry& commands() override  { return app_.commands_; }
    core::ShortcutMap&     shortcuts() override { return app_.shortcuts_; }
    core::ToolManager&     tools() override { return app_.toolManager_; }

    // ── Per-viewport cameras (Pass 8 §5.2) ───────────────────────────────────
    uint32_t activeViewport() const override { return app_.activeViewport_; }
    Camera& viewportCamera(uint32_t index) override {
        return app_.viewportCamera(index);
    }
    void applyStandardView(uint32_t index, int view) override {
        app_.applyStandardView(index, view);
    }
    void frameItemsIn(uint32_t index,
                      const std::vector<scene::SceneItemRef>& refs) override {
        app_.frameItemsIn(index, refs);
    }

    // ── Scene stats (Pass 8): cheap counters for the status bar + hints ──────
    Gui::SceneStats sceneStats() const override {
        Gui::SceneStats stats;
        const scene::Scene& scene = app_.scene_;
        stats.models = uint32_t(scene.models.size());
        stats.lights = uint32_t(scene.pointLights.size());
        stats.groups = uint32_t(scene.groups.size());
        stats.layers = uint32_t(scene.i3sLayers.size());
        stats.clouds = uint32_t(scene.pointClouds.size());
        for (const scene::Model& model : scene.models) {
            stats.meshes += uint32_t(model.meshes.size());
            for (const scene::ModelMesh& mesh : model.meshes)
                stats.triangles += mesh.buffer.indexCount() / 3;
            if (model.groupId == 0)
                ++stats.ungrouped;
        }
        for (const Engine::PointCloud& cloud : scene.pointClouds) {
            stats.points += cloud.totalPointCount;
            if (cloud.groupId == 0)
                ++stats.ungrouped;
            if (cloud.gpu && cloud.gpu->valid())
                stats.cloudVramMB +=
                    double(cloud.gpu->storage.size()) / (1024.0 * 1024.0);
        }
        const rhi::Device::MemoryBudget budget = app_.device_.deviceLocalBudget();
        stats.vramUsedMB = double(budget.usageBytes) / (1024.0 * 1024.0);
        stats.vramBudgetMB = double(budget.budgetBytes) / (1024.0 * 1024.0);
        return stats;
    }

    // Is the chord bound to `commandId` held right now? (F1 hold-to-show.) The
    // GLFW->ImGui key mapping stays app-side, so the Gui layer never sees GLFW.
    bool shortcutHeld(const std::string& commandId) const override {
        const ImGuiIO& io = ImGui::GetIO();
        for (const core::ShortcutBinding& binding :
             app_.shortcuts_.bindings(commandId)) {
            if (!binding.valid())
                continue;
            if (binding.ctrl != io.KeyCtrl || binding.alt != io.KeyAlt ||
                binding.shift != io.KeyShift)
                continue;
            const ImGuiKey key = imGuiKeyFromGlfw(binding.keyCode);
            if (key != ImGuiKey_None && ImGui::IsKeyDown(key))
                return true;
        }
        return false;
    }

    // Shortcut capture (Pass 6): the GLFW code of the non-modifier key pressed
    // this frame, so the Gui-layer binding editor never includes GLFW.
    int capturePressedKey() const override {
        for (int k = ImGuiKey_NamedKey_BEGIN; k < ImGuiKey_NamedKey_END; ++k) {
            const ImGuiKey key = static_cast<ImGuiKey>(k);
            if (!ImGui::IsKeyPressed(key, false))
                continue;
            if (key == ImGuiKey_LeftCtrl || key == ImGuiKey_RightCtrl ||
                key == ImGuiKey_LeftShift || key == ImGuiKey_RightShift ||
                key == ImGuiKey_LeftAlt || key == ImGuiKey_RightAlt ||
                key == ImGuiKey_LeftSuper || key == ImGuiKey_RightSuper)
                continue; // a bare modifier is never a binding
            if (const int glfwKey = glfwKeyFromImGui(key))
                return glfwKey;
        }
        return 0;
    }

    // ---- Docked 3D viewports ----
    uint32_t viewportPanelCount() const override {
        return static_cast<uint32_t>(app_.viewports_.size());
    }
    const char* viewportPanelName(uint32_t index) const override {
        return index < app_.viewports_.size() ? app_.viewports_[index].name.c_str()
                                              : "Viewport";
    }
    Gui::ViewportDisplay viewportDisplay(uint32_t index) const override {
        Gui::ViewportDisplay d;
        d.active = app_.renderer_.viewportOutputActive();
        if (index < app_.renderer_.viewportOutputCount()) {
            d.textureId = app_.renderer_.viewportTextureId(index);
            const VkExtent2D extent = app_.renderer_.viewportExtent(index);
            d.width = extent.width;
            d.height = extent.height;
        }
        return d;
    }
    void onViewportPanel(uint32_t index, const Gui::ViewportPanelState& state) override {
        if (index < app_.viewports_.size())
            app_.viewports_[index].ui = state;
    }
    bool canAddViewport() const override {
        return app_.viewports_.size() < renderer::kMaxViewports &&
               app_.renderer_.viewportOutputActive();
    }
    void addViewport() override { app_.addViewport(); }
    void closeViewport(uint32_t index) override {
        // Never the primary; the entry is erased at the next frame boundary
        // (reconcileViewportOutput), where the renderer reconfigures device-idle.
        if (index > 0 && index < app_.viewports_.size())
            app_.viewports_[index].open = false;
    }
    bool viewportHovered() const override {
        if (app_.renderer_.viewportOutputActive()) {
            for (const Application::AppViewport& vp : app_.viewports_)
                if (vp.ui.shown && vp.ui.hovered)
                    return true;
            return false;
        }
        return !ImGui::GetIO().WantCaptureMouse;
    }

    Gui::FrameDiagnostics diagnostics() const override {
        Gui::FrameDiagnostics d;
        d.gpuName = app_.device_.deviceName();
        const uint32_t api = app_.device_.properties().apiVersion;
        d.apiMajor = VK_API_VERSION_MAJOR(api);
        d.apiMinor = VK_API_VERSION_MINOR(api);
        d.apiPatch = VK_API_VERSION_PATCH(api);
        d.validation = app_.device_.validationActive();
        d.swapchainWidth = app_.swapchain_.extent().width;
        d.swapchainHeight = app_.swapchain_.extent().height;
        d.swapchainImages = app_.swapchain_.imageCount();
        d.viewCount = app_.renderer_.viewCount();
        d.stereoPresentCapable = app_.swapchain_.stereoPresentSupported();
        const float fps = ImGui::GetIO().Framerate;
        d.fps = fps;
        d.frameMs = fps > 0.0f ? 1000.0f / fps : 0.0f;
        const renderer::Renderer::FrameStats& s = app_.renderer_.frameStats();
        d.slotWaitMs = s.slotWaitMs;
        d.acquireMs = s.acquireMs;
        d.presentMs = s.presentMs;
        d.swapchainRecreations = app_.swapchainRecreations_;
        d.asyncComputeSupported = app_.device_.asyncComputeAvailable();
        d.asyncComputeDedicatedFamily = app_.device_.computeQueueDedicatedFamily();
        d.asyncComputeEnabled = app_.renderer_.asyncComputeEnabled();
        d.asyncComputeActive = app_.renderer_.asyncComputeActive();
        return d;
    }

    int presentModeCount() const override {
        return static_cast<int>(filteredPresentModes().size());
    }
    const char* presentModeName(int index) const override {
        const std::vector<VkPresentModeKHR> modes = filteredPresentModes();
        if (index < 0 || index >= static_cast<int>(modes.size()))
            return "?";
        return rhi::presentModeName(modes[index]);
    }
    int currentPresentMode() const override {
        const std::vector<VkPresentModeKHR> modes = filteredPresentModes();
        const VkPresentModeKHR current = app_.swapchain_.presentMode();
        for (int i = 0; i < static_cast<int>(modes.size()); ++i)
            if (modes[i] == current)
                return i;
        return 0;
    }
    void requestPresentMode(int index) override {
        const std::vector<VkPresentModeKHR> modes = filteredPresentModes();
        if (index >= 0 && index < static_cast<int>(modes.size()))
            app_.pendingPresentMode_ = modes[index];
    }

    float tonemapExposure() const override {
        return app_.renderer_.tonemapSettings().exposure;
    }
    void setTonemapExposure(float value) override {
        app_.renderer_.tonemapSettings().exposure = value;
    }
    int tonemapOp() const override {
        return static_cast<int>(app_.renderer_.tonemapSettings().op);
    }
    void setTonemapOp(int op) override {
        app_.renderer_.tonemapSettings().op = static_cast<renderer::TonemapOperator>(op);
    }
    int tonemapOpCount() const override { return 5; } // Reinhard..KhronosPBRNeutral
    const char* tonemapOpName(int op) const override {
        return renderer::tonemapOperatorName(static_cast<renderer::TonemapOperator>(op));
    }

    bool skyHasCubemap() const override { return app_.renderer_.skybox().hasCubemap(); }
    bool skyHasEquirect() const override { return app_.renderer_.skybox().hasEquirect(); }

    bool screenshotPending() const override { return app_.renderer_.screenshotPending(); }
    const char* screenshotStatus() const override {
        return app_.renderer_.screenshotStatus().c_str();
    }
    void requestScreenshot() override {
        app_.renderer_.requestScreenshot(
            Engine::Screenshot::makeTimestampedPath("screenshots"));
    }

    double uploadRingUsedMB() const override {
        return double(app_.renderer_.uploadRing().usedBytes()) / (1024.0 * 1024.0);
    }
    double uploadRingCapacityMB() const override {
        return double(app_.renderer_.uploadRing().capacity()) / (1024.0 * 1024.0);
    }

    bool wireframeSupported() const override {
        return app_.device_.fillModeNonSolidEnabled();
    }

    int stereoMode() const override { return static_cast<int>(app_.stereoMode_); }
    void requestStereoMode(int mode) override {
        const StereoMode requested = static_cast<StereoMode>(mode);
        if (requested != app_.stereoMode_) {
            app_.stereoModeRequested_ = requested;
            app_.stereoModePending_ = true;
        }
    }
    bool stereoPresentSupported() const override {
        return app_.swapchain_.stereoPresentSupported();
    }
    uint32_t swapchainLayers() const override { return app_.swapchain_.layers(); }

    bool vrEnabled() const override { return app_.vrEnabled_; }
    void setVrEnabled(bool on) override { app_.vrEnabled_ = on; }
    bool xrRunning() const override { return app_.xrRunning(); }
    std::string xrRuntimeName() const override {
        return app_.xrSession_ ? app_.xrSession_->runtimeName() : std::string();
    }
    uint32_t xrEyeWidth() const override {
        return app_.xrSession_ ? app_.xrSession_->eyeWidth() : 0u;
    }
    uint32_t xrEyeHeight() const override {
        return app_.xrSession_ ? app_.xrSession_->eyeHeight() : 0u;
    }
    std::string vrStatus() const override { return app_.vrStatus_; }
    void refreshXRDiagnostics() override { app_.refreshXRDiagnostics(); }
    const Engine::XRDiagnostics& xrDiagnostics() const override { return app_.xrDiag_; }
    bool xrDiagnosticsValid() const override { return app_.xrDiagValid_; }
    std::string xrRuntimeOverride() const override {
        return Engine::getXRRuntimeOverride();
    }
    void setXRRuntimeOverride(const std::string& manifestPath) override {
        Engine::setXRRuntimeOverride(manifestPath);
    }

    scene::Selection& selection() override { return app_.selection_; }
    int selectedModel() const override { return app_.selectedModelIndex(); }
    int selectedMesh() const override { return app_.selectedMeshIndex(); }
    void setSelection(int model, int mesh) override {
        app_.setSelectionIndices(model, mesh);
    }
    void clearSelection() override { app_.selection_.clear(); }

    // ---- Outliner item operations (Pass 1; Application methods in SceneOps.cpp) ----
    void deleteItems(const std::vector<scene::SceneItemRef>& refs) override {
        app_.deleteItems(refs);
    }
    void duplicateItems(const std::vector<scene::SceneItemRef>& refs) override {
        app_.duplicateItems(refs);
    }
    void setItemsVisible(const std::vector<scene::SceneItemRef>& refs,
                         bool visible) override {
        app_.setItemsVisible(refs, visible);
    }
    void setItemsLocked(const std::vector<scene::SceneItemRef>& refs,
                        bool locked) override {
        app_.setItemsLocked(refs, locked);
    }
    uint64_t groupItems(const std::vector<scene::SceneItemRef>& refs) override {
        return app_.groupItems(refs);
    }
    void ungroupItems(const std::vector<scene::SceneItemRef>& refs) override {
        app_.ungroupItems(refs);
    }
    void moveItemsToGroup(const std::vector<scene::SceneItemRef>& refs,
                          uint64_t groupId) override {
        app_.moveItemsToGroup(refs, groupId);
    }
    void renameItem(const scene::SceneItemRef& ref,
                    const std::string& name) override {
        app_.renameItem(ref, name);
    }
    void frameItems(const std::vector<scene::SceneItemRef>& refs) override {
        app_.frameItems(refs);
    }
    bool isolateActive() const override { return app_.isolate_.active; }
    void isolateItems(const std::vector<scene::SceneItemRef>& refs) override {
        app_.isolateItems(refs);
    }
    void exitIsolate() override { app_.exitIsolate(); }

    // ---- Scene document (Pass 1) ----
    void newScene() override { app_.newScene(); }
    void openSceneDialog() override { app_.openSceneDialog(); }
    void openSceneFile(const std::string& path) override {
        app_.openSceneFile(path);
    }
    void mergeSceneDialog() override { app_.mergeSceneDialog(); }
    bool saveScene() override { return app_.saveScene(); }
    bool saveSceneAs() override { return app_.saveSceneAs(); }
    const std::string& currentScenePath() const override {
        return app_.scenePath_;
    }
    const std::string& pendingSceneOpenPath() const override {
        return app_.pendingOpenPath_;
    }
    void resolvePendingSceneOpen(int action, bool remember) override {
        app_.resolvePendingSceneOpen(action, remember);
    }

    // ── Snapshots (Pass 3 §9) ────────────────────────────────────────────────
    size_t snapshotCount() const override { return app_.snapshots().size(); }
    Gui::SnapshotView snapshot(size_t i) const override {
        Gui::SnapshotView v;
        if (i < app_.snapshots().size()) {
            const Application::SnapshotEntry& e = app_.snapshots()[i];
            v.name = e.name;
            v.timestamp = e.timestamp;
            v.aspects = e.aspects;
            v.tags = e.tags;
        }
        return v;
    }
    void createSnapshot(const std::string& name, uint32_t aspects) override {
        app_.createSnapshot(name, aspects);
    }
    void restoreSnapshot(size_t i, uint32_t restoreAspects) override {
        app_.restoreSnapshot(i, restoreAspects);
    }
    void deleteSnapshot(size_t i) override { app_.deleteSnapshot(i); }
    void renameSnapshot(size_t i, const std::string& name) override {
        if (i < app_.snapshots().size() && !name.empty())
            app_.snapshots()[i].name = name;
    }
    void setSnapshotTags(size_t i, const std::vector<std::string>& tags) override {
        if (i < app_.snapshots().size())
            app_.snapshots()[i].tags = tags;
    }

    bool gizmoEnabled() const override { return app_.gizmo_.enabled; }
    void setGizmoEnabled(bool on) override { app_.gizmo_.enabled = on; }
    int gizmoMode() const override { return static_cast<int>(app_.gizmo_.mode()); }
    void setGizmoMode(int mode) override {
        app_.gizmo_.setMode(static_cast<Tools::TransformGizmo::Mode>(mode));
    }
    bool gizmoLocalSpace() const override {
        return app_.gizmo_.space == Tools::TransformGizmo::Space::Local;
    }
    void setGizmoLocalSpace(bool local) override {
        app_.gizmo_.space = local ? Tools::TransformGizmo::Space::Local
                                  : Tools::TransformGizmo::Space::World;
    }
    bool gizmoSnap() const override { return app_.gizmo_.snapEnabled; }
    void setGizmoSnap(bool on) override { app_.gizmo_.snapEnabled = on; }

    void importModelDialog() override { app_.openModelDialog(); }
    void openPointCloudDialog() override { app_.openPointCloudDialog(); }

    // ── Smart import + autosave/recovery (Pass 5) ────────────────────────────
    void importFiles(const std::vector<std::string>& paths) override {
        app_.importFiles(paths);
    }
    void importFilesDialog() override { app_.importFilesDialog(); }
    bool sceneEmpty() const override { return app_.sceneEmpty(); }
    void addPrimitive(int type) override { app_.addPrimitive(type); }
    std::string autosaveStatus() const override { return app_.autosaveStatus(); }
    bool recoveryAvailable() const override { return app_.recoveryAvailable(); }
    std::string recoveryTimestamp() const override { return app_.recoveryTimestamp(); }
    void restoreLastSession() override { app_.restoreLastSession(); }
    void discardRecovery() override { app_.discardRecovery(); }
    bool sceneSaveAvailable() const override { return true; } // Pass 1 landed
    void deleteModel(int model) override {
        // Undoable path shared with the Outliner (one step, by ObjectId).
        if (model < 0 || model >= static_cast<int>(app_.scene_.models.size()))
            return;
        scene::SceneItemRef ref;
        ref.kind = scene::SceneItemRef::Kind::Model;
        ref.id = app_.scene_.models[model].id;
        ref.index = model;
        app_.deleteItems({ ref });
    }
    void openSlpkDialog() override { app_.openSlpkDialog(); }
    size_t slpkLoadsInFlight() const override { return app_.slpkJobs_.size(); }
    void frameI3SLayer(size_t index) override { app_.frameI3SLayer(index); }
    void unloadI3SLayer(size_t index) override {
        // Undoable path shared with the Outliner (undo re-opens the package).
        if (index >= app_.scene_.i3sLayers.size() ||
            !app_.scene_.i3sLayers[index])
            return;
        scene::SceneItemRef ref;
        ref.kind = scene::SceneItemRef::Kind::SceneLayer;
        ref.id = app_.scene_.i3sLayers[index]->id;
        ref.index = static_cast<int>(index);
        app_.deleteItems({ ref });
    }

    void focusCameraOn(int model) override {
        if (model < 0 || model >= static_cast<int>(app_.scene_.models.size()))
            return;
        const scene::Model& m = app_.scene_.models[model];
        glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
        for (const scene::ModelMesh& mesh : m.meshes) {
            if (mesh.boundsMin.x > mesh.boundsMax.x)
                continue;
            lo = glm::min(lo, mesh.boundsMin);
            hi = glm::max(hi, mesh.boundsMax);
        }
        const glm::vec3 local = (lo.x > hi.x) ? glm::vec3(0.0f) : (lo + hi) * 0.5f;
        const glm::vec3 world = glm::vec3(m.modelMatrix() * glm::vec4(local, 1.0f));
        app_.activeCamera().StartCenteringAnimation(world);
    }

    renderer::gpu::MaterialData* materialForMesh(int model, int mesh) override {
        if (model < 0 || model >= static_cast<int>(app_.scene_.models.size()))
            return nullptr;
        const scene::Model& m = app_.scene_.models[model];
        if (mesh < 0 || mesh >= static_cast<int>(m.meshes.size()))
            return nullptr;
        const uint32_t idx = m.meshes[mesh].materialIndex;
        if (idx >= app_.renderer_.materials().materials().size())
            return nullptr;
        return &app_.renderer_.materials().material(idx);
    }

    std::string pickTextureFile() override {
        const std::vector<std::string> sel =
            pfd::open_file("Select texture", "",
                           { "Images", "*.png *.jpg *.jpeg *.bmp *.tga *.hdr",
                             "All files", "*" })
                .result();
        return sel.empty() ? std::string() : sel[0];
    }

    bool applyMaterialTexture(int model, int mesh, int slot,
                              const std::string& path) override {
        if (model < 0 || model >= static_cast<int>(app_.scene_.models.size()))
            return false;
        const scene::Model& m = app_.scene_.models[model];
        if (m.meshes.empty())
            return false;
        if (!app_.renderer_.materials().textureSlotAvailable()) {
            app_.pushToast("Texture capacity is full", Plugins::ToastLevel::Warning);
            return false;
        }
        // Load once (stb), assign the shared bindless index to every target slot.
        stbi_set_flip_vertically_on_load(false);
        int w = 0, h = 0, comp = 0;
        uint8_t* data = stbi_load(path.c_str(), &w, &h, &comp, 4);
        if (!data) {
            app_.pushToast("Failed to load texture: " + path,
                           Plugins::ToastLevel::Warning);
            return false;
        }
        const bool srgb = (slot == 0); // albedo is sRGB; data maps are UNORM
        rhi::TextureDesc desc{};
        desc.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
        desc.extent = { uint32_t(w), uint32_t(h) };
        desc.mipLevels = rhi::computeMipCount(uint32_t(w), uint32_t(h));
        desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
        desc.debugName = "inspector_texture";
        rhi::Texture tex;
        tex.create(app_.device_, desc);
        tex.upload(data, size_t(w) * size_t(h) * 4);
        stbi_image_free(data);
        const uint32_t index = app_.renderer_.materials().addTexture(std::move(tex));

        auto assign = [&](int meshIdx) {
            renderer::gpu::MaterialData* mat = materialForMesh(model, meshIdx);
            if (!mat)
                return;
            switch (slot) {
            case 0: mat->albedoTexture = index; break;
            case 1: mat->normalTexture = index; break;
            case 2: mat->metallicTexture = index; break;
            case 3: mat->roughnessTexture = index; break;
            case 4: mat->aoTexture = index; break;
            default: break;
            }
        };
        if (mesh >= 0)
            assign(mesh);
        else
            for (int j = 0; j < static_cast<int>(m.meshes.size()); ++j)
                assign(j);
        return true;
    }

    size_t pointCloudCount() const override {
        return app_.scene_.pointClouds.size();
    }
    const std::string& pointCloudName(size_t i) const override {
        return app_.scene_.pointClouds[i].name;
    }
    bool& pointCloudVisible(size_t i) override {
        return app_.scene_.pointClouds[i].visible;
    }
    uint32_t pointCloudBatches(size_t i) const override {
        return app_.scene_.pointClouds[i].numBatches;
    }
    uint32_t pointCloudPoints(size_t i) const override {
        return app_.scene_.pointClouds[i].totalPointCount;
    }
    double pointCloudVramMB(size_t i) const override {
        const Engine::PointCloud& pc = app_.scene_.pointClouds[i];
        if (pc.gpu && pc.gpu->valid())
            return double(pc.gpu->storage.size()) / (1024.0 * 1024.0);
        return 0.0;
    }
    Gui::PointCloudProgress pointCloudProgress(size_t i) const override {
        const Engine::PointCloudLoader::StreamProgress p =
            Engine::PointCloudLoader::getStreamProgress(
                app_.scene_.pointClouds[i]);
        Gui::PointCloudProgress out;
        out.active = p.active;
        out.resorting = p.resorting;
        out.pointsLoaded = p.pointsLoaded;
        out.pointsTotal = p.pointsTotal;
        out.fraction = p.fraction;
        out.pointsPerSecond = p.pointsPerSecond;
        return out;
    }
    void unloadPointCloud(size_t i) override {
        // Undoable path shared with the Outliner.
        if (i >= app_.scene_.pointClouds.size())
            return;
        scene::SceneItemRef ref;
        ref.kind = scene::SceneItemRef::Kind::PointCloud;
        ref.id = app_.scene_.pointClouds[i].id;
        ref.index = static_cast<int>(i);
        app_.deleteItems({ ref });
    }
    void exportPointCloud(size_t i, int format, bool applyTransform,
                          bool plyBinary) override {
        if (i >= app_.scene_.pointClouds.size())
            return;
        const Engine::PointCloud& pc = app_.scene_.pointClouds[i];
        const char* ext = format == 0 ? "xyz"
                          : format == 1 ? "pcb"
                          : format == 2 ? "h5"
                                        : "ply";
        const std::string base = pc.name.empty() ? std::string("pointcloud") : pc.name;
        const std::string dest =
            pfd::save_file("Export point cloud", base + "." + ext,
                           { std::string("Point cloud"), std::string("*.") + ext,
                             std::string("All files"), std::string("*") })
                .result();
        if (dest.empty())
            return;
        bool ok = false;
        switch (format) {
        case 0: ok = Engine::PointCloudLoader::exportToXYZ(pc, dest, applyTransform); break;
        case 1: ok = Engine::PointCloudLoader::exportToBinary(pc, dest, applyTransform); break;
        case 2: ok = Engine::PointCloudLoader::exportToHDF5(pc, dest, applyTransform); break;
        case 3:
            ok = Engine::PointCloudLoader::exportToPLY(pc, dest, applyTransform, plyBinary);
            break;
        default: break;
        }
        app_.pushToast(ok ? ("Exported to " + dest) : std::string("Export failed (see console)"),
                       ok ? Plugins::ToastLevel::Info : Plugins::ToastLevel::Warning);
    }

    int themeCount() const override { return GetGuiThemeCount(); }
    const char* themeName(int theme) const override { return GetGuiThemeName(theme); }
    int currentTheme() const override { return g_currentTheme; }
    void setTheme(int theme) override {
        ApplyGuiTheme(theme, 1.0f);
        // g_currentTheme is what ApplyGuiTheme actually accepted; recording it
        // into the settings persists the choice (preferences.json).
        app_.settings_.ui.theme = g_currentTheme;
    }

    float guiScaleFactor() const override { return app_.settings_.ui.guiScale; }
    void setGuiScaleFactor(float factor) override {
        factor = std::clamp(factor, GuiScaleSettings::MIN_USER_FACTOR,
                            GuiScaleSettings::MAX_USER_FACTOR);
        app_.settings_.ui.guiScale = factor;
        g_GuiScale.userScaleFactor = factor;
        // Defeat UpdateGuiScale's resize hysteresis so the new factor applies
        // without a window resize; the restyle + font rebuild run at the top
        // of the next frame (device-idle), like a window-driven rescale.
        g_GuiScale.lastWindowWidth = 0;
        g_GuiScale.lastWindowHeight = 0;
        int fbWidth = 0, fbHeight = 0;
        app_.window_.framebufferSize(fbWidth, fbHeight);
        UpdateGuiScale(fbWidth, fbHeight);
    }

    void toast(const std::string& message, Plugins::ToastLevel level) override {
        app_.pushToast(message, level);
    }

    void requestQuit() override {
        glfwSetWindowShouldClose(app_.window_.handle(), GLFW_TRUE);
    }

private:
    // Present modes offered to the UI: skip the shared-present modes (they need a
    // different frame path), mirroring the interim debug panel's filter.
    std::vector<VkPresentModeKHR> filteredPresentModes() const {
        std::vector<VkPresentModeKHR> out;
        for (VkPresentModeKHR mode : app_.swapchain_.availablePresentModes())
            if (mode <= VK_PRESENT_MODE_FIFO_RELAXED_KHR)
                out.push_back(mode);
        return out;
    }

    Application& app_;
};

Application::Application() = default;

Application::~Application() {
    shutdown();
}

void Application::init() {
    // Preferences first: everything below (theme, GUI scale, sun/sky
    // defaults, panel visibility, navigation feel) respects the persisted
    // values. A missing file or missing keys keep the Settings defaults; a
    // GL-era preferences.json migrates its overlapping subset (Preferences.h).
    prefsLoaded_ = Gui::Preferences::load(kPreferencesFile, settings_, &commands_);

    // Crash recovery (Pass 5): a session.lock left behind by the previous run
    // means it did not exit cleanly — the Welcome Hub then leads with a
    // "Restore last session" card. Re-arms the lock for this run.
    initSession();

    window_.init(1920, 1080, "StereoVista");

#ifdef SV_VULKAN_VALIDATION
    const bool enableValidation = true;
#else
    const bool enableValidation = false;
#endif
    device_.init(window_.handle(), enableValidation);

    int fbWidth = 0, fbHeight = 0;
    window_.framebufferSize(fbWidth, fbHeight);
    swapchain_.init(device_, static_cast<uint32_t>(fbWidth), static_cast<uint32_t>(fbHeight));

    renderer_.init(device_, swapchain_, shaderCompiler_);

    // Point-cloud loaders upload through the device + the renderer's ring.
    Engine::PointCloudLoader::initGpu(&device_, &renderer_.uploadRing());

    loadScene();

    // 3D cursor system (Phase 6). Generates the sphere mesh; the active type
    // starts on the sphere cursor (classic look) and is switched from the UI.
    cursorManager_.initialize();
    cursorManager_.getSphereCursor()->setVisible(settings_.cursor.type == 0);
    cursorManager_.getPlaneCursor()->setVisible(settings_.cursor.type == 1);
    cursorManager_.getFragmentCursor()->setVisible(settings_.cursor.type == 2);

    // Static plugins (CrosshairPlugin, ...) mount on the app via the context.
    pluginContext_ = std::make_unique<MainPluginContext>(*this);
    pluginManager_.loadRegisteredPlugins(*pluginContext_);

    // GUI services facade the panels talk through (created once, like the plugin
    // context above).
    guiServices_ = std::make_unique<MainGuiServices>(*this);

    // Commands + shortcuts (UI redesign Pass 0): register every action and
    // its default binding, then overlay the user's shortcuts.json (a GL-era
    // profile file is migrated on load). From here on saving is safe.
    registerCommands();
    registerTools(); // Pass 7: tools + their commands (plugins are loaded above)
    shortcuts_.loadFromFile(kShortcutsFile);
    prefsReady_ = true;
    prefsSnapshot_ = Gui::Preferences::snapshot(settings_, &commands_);

    // The primary viewport entry (always present; never closable). Its camera
    // is camera_ — see viewportCamera(). Extra viewports come from the View menu.
    AppViewport primary;
    primary.id = 0;
    primary.name = "Viewport";
    viewports_.push_back(std::move(primary));

    // Double-click centering: when the glide completes, the centred point sits
    // at the viewport centre and the orbit pivot has re-anchored on it
    // (UpdateAnimation). Warp the OS mouse there too so the depth pick / a
    // following orbit continue from that point — but only while the mouse is
    // still over the 3D view (the GL app yanked it out of panels
    // unconditionally). Secondary viewport cameras inherit the callback by
    // copy in addViewport().
    camera_.centeringCompletedCallback = [this]() {
        if (sceneInput_.hovered)
            warpMouseToViewportCenter();
    };

    // Tools record through the app-owned undo stack and edit scene-owned
    // storage ("one heart": scene_.clipPlanes is what the Outliner lists and
    // the scene document serializes; scene_ is a member, so the binding
    // outlives every load — loads replace the vector's CONTENTS, not the
    // vector). The measurement tool binds itself in MeasurementPlugin::
    // onRegister via the plugin context.
    clipPlaneTool_.setUndoManager(&undo_);
    clipPlaneTool_.bindStorage(&scene_.clipPlanes);

    // Sun defaults follow the GL app (dimmed warm directional); the repo
    // skybox becomes the background when its faces resolve. Loaded
    // preferences win over these first-run defaults — but a persisted sky
    // mode whose source isn't available falls back to the gradient.
    // (The sun's shipped "on" default now lives in Gui::Settings itself —
    // Gui::detail::defaultSun — so kDefaults is honest and a Pass-6 reset
    // restores the sun instead of switching it off.)
    const bool hasCubemap = renderer_.skybox().loadCubemap("skybox");
    if (hasCubemap && !prefsLoaded_)
        settings_.sky.mode = renderer::SkyMode::Cubemap;
    if ((settings_.sky.mode == renderer::SkyMode::Cubemap && !hasCubemap) ||
        (settings_.sky.mode == renderer::SkyMode::Equirect &&
         !renderer_.skybox().hasEquirect()))
        settings_.sky.mode = renderer::SkyMode::Gradient;

    initImGui();
}

void Application::loadScene() {
    // Startup scene: office.scene next to the executable (a GL-era v1 file —
    // it loads through the same SceneDocument path as every other version).
    const std::filesystem::path scenePath = Platform::resolveAssetPath("office.scene");
    bool loaded = false;
    if (!scenePath.empty()) {
        try {
            scene::SceneLoadResult result = scene::loadSceneDocument(
                scenePath.string(), device_, renderer_.materials(),
                settings_.pointCloud.downsample, settings_.pointCloud.mortonResort);
            scene_ = std::move(result.scene);
            applyLoadedCamera(result.camera);
            applyLoadedEnvironment(result.environment);
            for (const scene::PendingLayerState& layer : result.layers) {
                pendingLayerStates_.push_back(layer);
                openSlpk(layer.sourcePath);
            }
            reportSceneLoad(result.report);
            scenePath_ = scenePath.string();
            loaded = true;
        } catch (const std::exception& e) {
            std::cerr << "ERROR: " << e.what() << "\n";
        }
    }
    if (!loaded && scene_.models.empty())
        scene_ = scene::createDefaultScene(device_, renderer_.materials());

    // Recorded ids/selection no longer match a freshly loaded scene.
    selection_.clear();
    gizmo_.clearTarget();
    gizmoDragging_ = false;
    clipPlaneTool_.notifyStorageChanged();
    undo_.clear();
}

// Seed the quaternion camera from a loaded pose: the saved orientation wins
// when present (v2/v3 files); otherwise build a lookAt basis from the front
// vector (the Camera's Euler convention has a 90° yaw offset, so this is the
// robust construction — same one the Camera uses for centering animations).
void Application::applyLoadedCamera(const scene::SceneCameraState& cam) {
    if (!cam.valid)
        return;
    Camera::CameraState st = camera_.GetState();
    st.position = cam.position;
    if (cam.hasOrientation) {
        st.orientation = glm::normalize(cam.orientation);
        st.front = cam.front;
        st.up = cam.up;
        st.yaw = cam.yaw;
        st.pitch = cam.pitch;
        st.zoom = cam.zoom;
    } else {
        const glm::vec3 f = glm::normalize(cam.front);
        const glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3(0.0f, 1.0f, 0.0f)));
        const glm::vec3 u = glm::normalize(glm::cross(r, f));
        st.orientation = glm::normalize(glm::quat_cast(glm::mat3(r, u, -f)));
    }
    camera_.SetState(st);
    camera_.SetOrbitPoint(camera_.OrbitDistance);
}

// Scene-authored sun/sky override the session's settings (the file is the
// author's intent); files without the blocks leave the user's settings alone.
void Application::applyLoadedEnvironment(const scene::SceneEnvironmentState& env) {
    if (env.hasSun)
        settings_.lighting.sun = env.sun;
    if (env.hasSky) {
        settings_.sky = env.sky;
        // A persisted mode whose source isn't available falls back (same
        // guard init() applies to preferences).
        if ((settings_.sky.mode == renderer::SkyMode::Cubemap &&
             !renderer_.skybox().hasCubemap()) ||
            (settings_.sky.mode == renderer::SkyMode::Equirect &&
             !renderer_.skybox().hasEquirect()))
            settings_.sky.mode = renderer::SkyMode::Gradient;
    }
}

void Application::beginNavCapture() {
    // Capture on the OS window hosting the 3D view (the main window, or the
    // ImGui-backend window when the Viewport panel is dragged out). Disabled
    // cursor = unbounded relative motion; record the baseline AFTER disabling
    // so the first frame's delta is zero (no view snap).
    navWindow_ = sceneInput_.hostWindow ? sceneInput_.hostWindow : window_.handle();
    glfwSetInputMode(navWindow_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    glfwGetCursorPos(navWindow_, &lastMouseX_, &lastMouseY_);
}

void Application::endNavCapture() {
    if (navWindow_)
        glfwSetInputMode(navWindow_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    navWindow_ = nullptr;
}

void Application::warpMouseToViewportCenter() {
    // sceneInput_.screenPos/screenSize are the 3D-view image rect in ImGui
    // screen space (desktop coords with multi-viewport); glfwSetCursorPos
    // wants client-area coords of the OS window hosting the view.
    GLFWwindow* host =
        sceneInput_.hostWindow ? sceneInput_.hostWindow : window_.handle();
    int wx = 0, wy = 0;
    glfwGetWindowPos(host, &wx, &wy);
    const glm::vec2 center =
        sceneInput_.screenPos + sceneInput_.screenSize * 0.5f;
    glfwSetCursorPos(host, double(center.x - wx), double(center.y - wy));
}

void Application::startOrbit() {
    // The viewport under the mouse owns the interaction (activeViewport_ is
    // frozen by updateSceneInput for the whole drag).
    Camera& cam = activeCamera();
    if (cam.IsAnimating)
        return;
    orbiting_ = true;
    if (cursorManager_.isCursorPositionValid()) {
        const glm::vec3 cursor = cursorManager_.getCursorPosition();
        if (settings_.camera.orbitAroundCursor) {
            cam.UpdateCursorInfo(cursor, true);
            cam.StartOrbiting(true); // orbit about the 3D cursor point
        } else {
            // Standard orbit: pivot at the cursor's depth in front of the eye.
            const float depth = glm::length(cursor - cam.Position);
            cam.SetOrbitPointDirectly(cam.Position + cam.Front * depth);
            cam.StartOrbiting();
        }
        cursorManager_.setCapturedCursorPosition(cursor);
    } else {
        cam.SetOrbitPointDirectly(cam.Position + cam.Front * cam.OrbitDistance);
        cam.StartOrbiting();
    }
    beginNavCapture();
}

void Application::updateCamera(float dt) {
    ImGuiIO& io = ImGui::GetIO();

    // World size of the scene content — scales the adaptive-speed and scroll
    // distance curves. Models + rendered I3S layers come from the precomputed
    // scene world bounds; still-streaming point clouds may have grown since
    // the last computeWorldBounds, so union their (scaled) boxes in. The GL
    // version only measured the FIRST model's untransformed vertices.
    float sceneSize = 0.0f;
    {
        const glm::vec3 sceneDim = scene_.worldBoundsMax - scene_.worldBoundsMin;
        sceneSize = glm::max(sceneDim.x, glm::max(sceneDim.y, sceneDim.z));
        for (const Engine::PointCloud& pc : scene_.pointClouds) {
            if (!pc.visible || !pc.hasBounds())
                continue;
            const glm::vec3 dim = (pc.boundsMax - pc.boundsMin) * glm::abs(pc.scale);
            sceneSize = glm::max(sceneSize, glm::max(dim.x, glm::max(dim.y, dim.z)));
        }
        sceneSize = glm::max(sceneSize, 1.0f);
    }

    // Push the tunable navigation feel onto EVERY viewport camera each frame
    // (the panels edit settings_, not the Cameras directly).
    for (size_t v = 0; v < viewports_.size(); ++v) {
        Camera& cam = viewportCamera(v);
        cam.zoomToCursor = settings_.camera.zoomToCursor;
        cam.orbitAroundCursor = settings_.camera.orbitAroundCursor;
        cam.MouseSensitivity = settings_.camera.sensitivity;
        cam.speedFactor = settings_.camera.speedFactor;
        cam.sceneSize = sceneSize;
        cam.useSmoothScrolling = settings_.camera.useSmoothScrolling;
        cam.scrollMomentum = settings_.camera.scrollMomentum;
        cam.scrollDeceleration = settings_.camera.scrollDeceleration;
        cam.maxScrollVelocity = settings_.camera.maxScrollVelocity;
    }

    // Interactions target the viewport under the mouse (frozen while a drag
    // runs — updateSceneInput never retargets mid-drag).
    Camera& camera = activeCamera();

    // Buttons through ImGui's aggregated io (fed by callbacks on the main
    // window AND every backend-owned OS window) so interaction keeps working
    // when the Viewport panel is dragged into its own window. `hovered` is
    // this frame's "pointer over the 3D view" — the gate for STARTING any
    // scene interaction; active drags run to release regardless.
    const bool lmb = io.MouseDown[ImGuiMouseButton_Left];
    const bool rmb = io.MouseDown[ImGuiMouseButton_Right];
    const bool mmb = io.MouseDown[ImGuiMouseButton_Middle];
    const bool hovered = sceneInput_.hovered;
    const int mods = currentMods();
    const bool shiftHeld = (mods & GLFW_MOD_SHIFT) != 0;
    const bool lmbPress = lmb && !prevLmb_;

    // ---- Transform gizmo: bound to the selected model; grabs the LMB press
    // when it lands on a handle (priority over orbit/selection). The gizmo drag
    // keeps the OS cursor visible (it tracks the real mouse), unlike orbit. ----
    if (!gizmoDragging_)
        updateGizmoBinding();
    bool gizmoTookLmb = false;
    if (gizmoDragging_) {
        const Plugins::PickRay ray = mouseRayCurrent();
        if (lmb) {
            gizmo_.updateDrag(ray.origin, ray.direction, camera.Position, shiftHeld);
            if (gizmoTargetPlane_)
                clipPlaneTool_.syncActiveNormalFromGizmo(); // rotate -> plane normal
            else
                applyGizmoDeltaToSelection(); // multi-select: mirror the delta
        } else {
            gizmo_.endDrag();
            if (gizmoTargetPlane_) clipPlaneTool_.recordGizmoUndo();
            else finishGizmoUndo(); // record the whole drag as one undo entry
            gizmoDragging_ = false;
        }
        gizmoTookLmb = true;
    } else if (gizmo_.hasTarget() && hovered && !navActive()) {
        const Plugins::PickRay ray = mouseRayCurrent();
        if (lmbPress) {
            const Tools::TransformGizmo::Handle h =
                gizmo_.hitTest(ray.origin, ray.direction, camera.Position);
            if (h != Tools::TransformGizmo::Handle::None) {
                if (gizmoTargetPlane_) clipPlaneTool_.captureGizmoUndo();
                else beginGizmoUndo();
                gizmo_.beginDrag(h, ray.origin, ray.direction, camera.Position);
                gizmoDragging_ = true;
                gizmoTookLmb = true;
            }
        }
        if (!gizmoDragging_)
            gizmo_.updateHover(ray.origin, ray.direction, camera.Position);
    } else if (!gizmo_.hasTarget()) {
        gizmo_.clearInteractionPoint();
    }

    // ---- Plugin mouse-button edges (press/release). The LMB press is
    // suppressed when the gizmo grabbed it; a plugin that consumes a press owns
    // that button until release (the "*Owned_" latches). ----
    if (pluginContext_) {
        auto dispatchEdge = [&](bool now, bool prev, bool& owned, int button,
                                bool suppressPress) {
            if (now == prev)
                return;
            if (now) { // press
                owned = !suppressPress && !navActive() && hovered &&
                        pluginManager_.dispatchMouseButton(*pluginContext_, button,
                                                           GLFW_PRESS, mods);
            } else {   // release
                if (owned || (!navActive() && hovered))
                    pluginManager_.dispatchMouseButton(*pluginContext_, button,
                                                       GLFW_RELEASE, mods);
                owned = false;
            }
        };
        dispatchEdge(lmb, prevLmb_, lmbOwned_, GLFW_MOUSE_BUTTON_LEFT, gizmoTookLmb);
        dispatchEdge(mmb, prevMmb_, mmbOwned_, GLFW_MOUSE_BUTTON_MIDDLE, false);
        dispatchEdge(rmb, prevRmb_, rmbOwned_, GLFW_MOUSE_BUTTON_RIGHT, false);
    }

    // ---- Camera nav + click-to-select (skips the LMB the gizmo/plugin took) ----
    if (!navActive() && !gizmoDragging_ && hovered) {
        if (lmb && !lmbOwned_ && !gizmoTookLmb) {
            // Double LEFT click -> glide-centre the view on the point under
            // the mouse (the orbit pivot re-anchors there on completion, and
            // the init() callback warps the mouse to the view centre).
            // Detection is ImGui's (0.3s + spatial slop, aggregated across OS
            // windows) — the GL check was time-only, so two clicks in opposite
            // corners counted. Further GL fixes: empty space centres on the
            // background-plane point the user actually aimed at (not a
            // roll-levelling no-op), Ctrl stays a pure selection gesture, and
            // XR is excluded (the desktop depth pick is frozen there).
            if (ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left) &&
                !(mods & GLFW_MOD_CONTROL) && !camera.IsAnimating &&
                !xrRunning()) {
                glm::vec3 target;
                if (cursorManager_.isCursorPositionValid())
                    target = cursorManager_.getCursorPosition();
                else if (cursorManager_.hasBackgroundCursorPosition())
                    target = cursorManager_.getBackgroundCursorPosition();
                else // nothing under the mouse at all: level toward ahead
                    target = camera.Position + camera.Front * camera.OrbitDistance;
                camera.StartCenteringAnimation(target);
                lmbClickCandidate_ = false;
            } else {
                // Capture a click candidate: a CTRL+left press that releases
                // without dragging becomes a selection at the 3D cursor's
                // world point (GL parity — a plain left press only orbits).
                lmbPressPos_ = sceneInput_.mousePx;
                lmbDragDist_ = 0.0f;
                lmbClickCandidate_ = (mods & GLFW_MOD_CONTROL) != 0;
                clickCursorValid_ = cursorManager_.isCursorPositionValid();
                clickCursorWorld_ = clickCursorValid_
                                        ? cursorManager_.getCursorPosition()
                                        : glm::vec3(0.0f);
                startOrbit();
            }
        } else if (mmb && !mmbOwned_) {
            camera.StartPanning();
            panning_ = true;
            beginNavCapture();
        } else if (rmb && !rmbOwned_) {
            rmbLooking_ = true;
            beginNavCapture();
        }
    } else if (navActive()) {
        // End the active drag when its button releases; otherwise apply the
        // relative motion to the camera.
        if (orbiting_ && !lmb) {
            camera.StopOrbiting();
            orbiting_ = false;
            endNavCapture();
            // A Ctrl+left click that barely moved selects the object under it
            // — the press started on the 3D view, so no GUI re-check on
            // release. (While measuring, the MeasurementPlugin consumes the
            // press, so no orbit candidate is started and this never runs.)
            if (lmbClickCandidate_ && lmbDragDist_ < kClickThresholdPx)
                performSelectionClick();
            lmbClickCandidate_ = false;
        } else if (panning_ && !mmb) {
            camera.StopPanning();
            panning_ = false;
            endNavCapture();
        } else if (rmbLooking_ && !rmb) {
            rmbLooking_ = false;
            endNavCapture();
        } else {
            // Relative motion from the window that owns the capture.
            double cx = 0.0, cy = 0.0;
            glfwGetCursorPos(navWindow_ ? navWindow_ : window_.handle(), &cx, &cy);
            const float dx = float(cx - lastMouseX_);
            const float dy = float(cy - lastMouseY_);
            lastMouseX_ = cx;
            lastMouseY_ = cy;
            if (orbiting_)
                lmbDragDist_ += std::abs(dx) + std::abs(dy);
            // Camera yoffset is +up; screen dy is +down.
            camera.ProcessMouseMovement(dx, -dy);
        }
    }

    // Button edge state for next frame (gizmo + plugin dispatch read these).
    prevLmb_ = lmb;
    prevMmb_ = mmb;
    prevRmb_ = rmb;

    // Scroll zoom (toward the 3D/background cursor when enabled). The wheel is
    // wheelThisFrame_, captured in run() before ImGui::Render — EndFrame zeroes
    // io.MouseWheel, so reading io here always saw 0. Plugins get first refusal
    // (a tool may scrub a value); the camera zooms otherwise. Gated on the 3D
    // view being hovered — panels keep their own scrolling.
    if (hovered && wheelThisFrame_ != 0.0f) {
        const bool consumed =
            pluginContext_ && pluginManager_.dispatchScroll(*pluginContext_, 0.0,
                                                            double(wheelThisFrame_));
        if (!consumed) {
            // Refresh the camera's 3D-cursor info from the CURRENT pick before
            // zooming (the GL scroll callback did the same). Without this,
            // zoom-to-cursor aimed at whatever startOrbit() last captured —
            // stale, or never set at all.
            camera.UpdateCursorInfo(cursorManager_.getCursorPosition(),
                                    cursorManager_.isCursorPositionValid());
            camera.ProcessMouseScroll(wheelThisFrame_,
                                      cursorManager_.getBackgroundCursorPosition(),
                                      cursorManager_.hasBackgroundCursorPosition());
        }
    }
    // Every camera keeps integrating: scroll momentum finishes after the mouse
    // leaves a viewport, and a fly-to animation keeps running in a viewport
    // that is no longer the active one.
    for (size_t v = 0; v < viewports_.size(); ++v) {
        viewportCamera(v).UpdateScrolling(dt);
        viewportCamera(v).UpdateAnimation(dt);
    }

    // Keyboard fly: WASD + Space/E up + Shift/Q down (GL parity — Shift is
    // vertical movement, NOT a speed boost; speed comes from the speed-factor
    // slider / the adaptive feed). Suppressed while a text field owns the
    // keyboard. Keys come from ImGui's aggregated state (fed by every OS
    // window the backend owns), so shortcuts keep working when the keyboard
    // focus sits on a dragged-out Viewport window.
    if (!io.WantCaptureKeyboard) {
        // Fly the ACTIVE viewport's camera (the one the mouse is over).
        // Manual mode pins the speed each frame; adaptive mode lets
        // updateCameraDepth evolve MovementSpeed from the depth feed instead.
        if (!settings_.camera.adaptiveSpeed)
            camera.MovementSpeed =
                settings_.camera.speed * settings_.camera.speedFactor;

        // Ctrl chords (undo / Ctrl+click select / …) never fly the camera, and
        // a gizmo drag owns Shift for its snapping modifier.
        const bool canFly = !io.KeyCtrl && !gizmoDragging_;
        bool flying = false;
        auto fly = [&](bool down, Camera_Movement dir) {
            if (down && canFly) {
                camera.ProcessKeyboard(dir, dt);
                flying = true;
            }
        };
        fly(ImGui::IsKeyDown(ImGuiKey_W), FORWARD);
        fly(ImGui::IsKeyDown(ImGuiKey_S), BACKWARD);
        fly(ImGui::IsKeyDown(ImGuiKey_A), LEFT);
        fly(ImGui::IsKeyDown(ImGuiKey_D), RIGHT);
        fly(ImGui::IsKeyDown(ImGuiKey_Space) || ImGui::IsKeyDown(ImGuiKey_E), UP);
        fly(io.KeyShift || ImGui::IsKeyDown(ImGuiKey_Q), DOWN);
        // Drive the adaptive-speed gate from THIS frame's keys — ProcessKeyboard
        // only sets isMoving when called, so it went stale-true once every fly
        // key was released (the GL app had the same latent bug).
        camera.isMoving = flying;

        // Dispatch action keys to plugins (edge-triggered): the MeasurementPlugin
        // uses Enter (finish), Delete (cancel), Backspace (undo last point),
        // Escape (exit the tool). Plugins keep receiving GLFW keycodes (their
        // public contract) and keep first refusal on these keys: a key a
        // plugin CONSUMES is withheld from this frame's shortcut dispatch, so
        // Esc exits the measurement tool without also clearing the selection
        // (§7.1: tool first, selection second).
        pluginConsumedKeys_.clear();
        if (pluginContext_) {
            auto keyEdge = [&](ImGuiKey key, int glfwKey, bool& prev) {
                const bool down = ImGui::IsKeyDown(key);
                if (down && !prev &&
                    pluginManager_.dispatchKey(*pluginContext_, glfwKey, 0,
                                               GLFW_PRESS, mods))
                    pluginConsumedKeys_.push_back(glfwKey);
                prev = down;
            };
            keyEdge(ImGuiKey_Enter, GLFW_KEY_ENTER, prevEnter_);
            keyEdge(ImGuiKey_KeypadEnter, GLFW_KEY_KP_ENTER, prevKpEnter_);
            keyEdge(ImGuiKey_Delete, GLFW_KEY_DELETE, prevDelete_);
            keyEdge(ImGuiKey_Backspace, GLFW_KEY_BACKSPACE, prevBackspace_);
            keyEdge(ImGuiKey_Escape, GLFW_KEY_ESCAPE, prevEscape_);
        }

        // Rebindable shortcuts -> commands (UI redesign Pass 0). Replaces the
        // hardcoded Escape / F1 / gizmo-mode / Ctrl+Z/Y handling: the same
        // actions are commands now (select.clear, view.toggle_gui — moved to
        // G, its GL key — gizmo.*, edit.undo/redo), dispatched by exact
        // modifier match on this frame's key press edges.
        dispatchShortcuts();
    } else {
        camera.isMoving = false;
    }

    // Distance-adaptive fly/zoom speed from the centre-depth feed. Runs after
    // the fly keys so isMoving reflects this frame; the adapted speed applies
    // from the next frame on (one-frame lag, same as the GL PBO read).
    updateCameraDepth(dt);
}

void Application::updateCameraDepth(float dt) {
    // The GL app drove this with a stalling glReadPixels of the centre depth
    // every frame; here the same feed rides the async depth-pick readback (the
    // 1x1 centre rect queued in updateCursorAndOverlay, read one frame later —
    // no stall). No depth queries run while XR owns the frame.
    if (xrRunning())
        return;
    const renderer::DepthReadback& rb = renderer_.depthSamples();
    if (!rb.valid || rb.extent.width == 0 || rb.extent.height == 0)
        return;
    // Right after the mouse crosses into another viewport, the published
    // readback still belongs to the previous one for ~frames-in-flight frames
    // — feeding that distance into THIS viewport's camera would poison its
    // speed/zoom until fresh samples land. Pause the feed instead.
    if (rb.viewport != activeViewport_)
        return;

    const glm::ivec2 center(int(rb.extent.width) / 2, int(rb.extent.height) / 2);
    float depth = 0.0f;
    if (!rb.sample(center, depth))
        return; // centre wasn't queried last frame (startup / mode switch)

    // Reverse-Z: depth 0 = far plane (background/sky). A miss counts as
    // "looking at empty space" at the far plane, like the GL path.
    const float farPlane = settings_.camera.farPlane;
    float distance = farPlane;
    if (depth > 1e-6f) {
        const float ndcX = (center.x + 0.5f) / float(rb.extent.width) * 2.0f - 1.0f;
        const float ndcY = (center.y + 0.5f) / float(rb.extent.height) * 2.0f - 1.0f;
        const glm::vec4 h = rb.invViewProj * glm::vec4(ndcX, ndcY, depth, 1.0f);
        const glm::vec3 world = glm::vec3(h) / h.w;
        distance = glm::length(world - rb.cameraPos);
    }

    // The distance feed also anchors the zoom reference distance, so scroll
    // zoom stays distance-proportional even when adaptive fly speed is
    // toggled off.
    Camera& camera = activeCamera();
    camera.UpdateDistanceToObject(distance);
    if (settings_.camera.adaptiveSpeed)
        camera.AdjustMovementSpeed(distance, farPlane, dt);
}

uint32_t Application::viewCameras(renderer::ViewCamera out[renderer::kMaxViews],
                                  size_t viewportIndex) const {
    // Aspect of THIS viewport's render target: its docked panel, the full
    // window on the classic path, or the HMD eye in XR (where the eye
    // projections overwrite these anyway). Each viewport renders through its
    // own camera; the stereo tunables are shared.
    const VkExtent2D extent =
        renderer_.sceneExtent(static_cast<uint32_t>(viewportIndex));
    const float aspect =
        extent.height ? float(extent.width) / float(extent.height) : 1.0f;
    const Camera& cam = viewportCamera(viewportIndex);

    const float fovDeg = settings_.camera.fovDeg;
    const float nearPlane = settings_.camera.nearPlane;
    const float farPlane = settings_.camera.farPlane;

    if (stereoMode_ == StereoMode::Off) {
        out[0].position = cam.Position;
        out[0].view = cam.GetViewMatrix();
        out[0].proj = renderer::perspective(glm::radians(fovDeg), aspect,
                                            nearPlane, farPlane);
        out[1] = out[0];
        return 1;
    }

    // Off-axis (parallel-axis) stereo, ported from the GL PerspectiveProjection:
    // both eyes look along Front, offset by +/- separation/2 * Right; each
    // frustum is sheared so the zero-parallax plane sits at `convergence`.
    const float hHalf = std::tan(glm::radians(fovDeg) * 0.5f);
    const float wHalf = hHalf * aspect;
    const float halfSep = settings_.stereo.separation * 0.5f;
    const float conv = std::max(settings_.stereo.convergence, 1e-3f);
    const glm::vec3 pos = cam.Position;
    const glm::vec3 right = cam.Right;
    const glm::vec3 front = cam.Front;
    const glm::vec3 up = cam.Up;

    auto buildEye = [&](float dir) {
        renderer::ViewCamera cam;
        cam.position = pos + right * (halfSep * dir);
        cam.view = glm::lookAt(cam.position, cam.position + front, up);
        const float l = (-wHalf * conv - halfSep * dir) / conv * nearPlane;
        const float r = (wHalf * conv - halfSep * dir) / conv * nearPlane;
        const float b = -hHalf * nearPlane;
        const float t = hHalf * nearPlane;
        cam.proj = renderer::frustumAsymmetric(l, r, b, t, nearPlane, farPlane);
        return cam;
    };
    // out[0] -> left display buffer, out[1] -> right; flipEyes swaps eyes.
    const bool flip = settings_.stereo.flipEyes;
    out[0] = buildEye(flip ? +1.0f : -1.0f);
    out[1] = buildEye(flip ? -1.0f : +1.0f);
    return 2;
}

void Application::cameraMatrices(glm::mat4& view, glm::mat4& proj) const {
    // Interaction matrices follow the ACTIVE viewport (mouse rays, cursor
    // reconstruction, plugin viewProj all line up with what that panel shows).
    renderer::ViewCamera cams[renderer::kMaxViews];
    viewCameras(cams, activeViewport_);
    view = cams[0].view;
    proj = cams[0].proj;
}

Camera& Application::viewportCamera(size_t index) {
    if (index == 0 || index >= viewports_.size())
        return camera_;
    return viewports_[index].camera;
}

const Camera& Application::viewportCamera(size_t index) const {
    if (index == 0 || index >= viewports_.size())
        return camera_;
    return viewports_[index].camera;
}

void Application::addViewport() {
    if (viewports_.size() >= renderer::kMaxViewports)
        return;
    // Smallest unused id -> a stable window name ("Viewport 2".."Viewport 4")
    // that never collides with a still-open sibling.
    int id = 1;
    for (bool taken = true; taken; ++id) {
        taken = false;
        for (const AppViewport& vp : viewports_)
            taken = taken || vp.id == id;
        if (!taken)
            break;
    }
    AppViewport vp;
    vp.id = id;
    vp.name = "Viewport " + std::to_string(id + 1);
    vp.camera = activeCamera(); // start where the user is looking
    viewports_.push_back(std::move(vp));
}

void Application::applyStereoMode(StereoMode mode) {
    // Auto-downgrade quad-buffer to side-by-side when the surface can't present
    // stereo (consumer GPUs / the dev laptop expose maxImageArrayLayers = 1).
    if (mode == StereoMode::QuadBuffer && !swapchain_.stereoPresentSupported()) {
        pushToast("No quad-buffer stereo display; using side-by-side.",
                  Plugins::ToastLevel::Warning);
        mode = StereoMode::SideBySide;
    }
    stereoMode_ = mode;
    // QuadBuffer needs a 2-layer swapchain (applied by the recreate); the renderer
    // then switches view count (rebuilds the layered scene target + passes).
    swapchain_.setStereo(mode == StereoMode::QuadBuffer);
    handleResize(); // recreate swapchain (new layer count) + renderer scene target
    renderer_.setViewCount(mode == StereoMode::Off ? 1u : 2u);
}

void Application::updateStereoConvergence(float dt) {
    if (stereoMode_ == StereoMode::Off)
        return;
    if (!settings_.stereo.autoConvergence) {
        // Keep the target synced so switching auto ON starts from the current value.
        targetConvergence_ = settings_.stereo.convergence;
        return;
    }

    // Focus distance = scene depth at the screen centre, from the async depth
    // readback (view 0; one frame late — fine for a smoothed value, and no stall
    // unlike the GL glReadPixels). depth ~0 = background/sky -> keep the target.
    const renderer::DepthReadback& rb = renderer_.depthSamples();
    if (rb.valid) {
        const glm::ivec2 center(int(rb.extent.width) / 2, int(rb.extent.height) / 2);
        float depth = 0.0f;
        if (rb.sample(center, depth) && depth > 1e-6f) {
            const float ndcX =
                (center.x + 0.5f) / float(rb.extent.width) * 2.0f - 1.0f;
            const float ndcY =
                (center.y + 0.5f) / float(rb.extent.height) * 2.0f - 1.0f;
            const glm::vec4 h = rb.invViewProj * glm::vec4(ndcX, ndcY, depth, 1.0f);
            const glm::vec3 world = glm::vec3(h) / h.w;
            const float focusDist = glm::length(world - rb.cameraPos);
            // Always clamp to a sane range so the off-axis frustum never
            // degenerates (an improvement over GL's optional-only cap).
            targetConvergence_ =
                glm::clamp(focusDist * settings_.stereo.convergenceFactor,
                           settings_.camera.nearPlane * 4.0f, settings_.camera.farPlane);
        }
    }

    // Frame-rate-independent exponential smoothing (same form as GL).
    const float t =
        1.0f - std::exp(-std::max(settings_.stereo.convergenceSmoothing, 0.0f) * dt);
    settings_.stereo.convergence =
        glm::mix(settings_.stereo.convergence, targetConvergence_, t);
}

// ============================================================================
// OpenXR / VR (Phase 7b). A live GUI toggle: init() creates the session and
// binds it to the already-created Vulkan device; nothing here runs until the
// user turns VR on, so a desktop launch never contacts a runtime.
// ============================================================================

bool Application::xrRunning() const {
    return xrSession_ && xrSession_->isInitialized();
}

void Application::refreshXRDiagnostics() {
    xrDiag_ = Engine::probeXRRuntimes();
    xrDiagValid_ = true;
}

void Application::enterXR() {
    xrSession_ = std::make_unique<Engine::XRSession>();
    if (!xrSession_->init(device_)) {
        // A failed enter must reset the toggle so the loop doesn't retry every
        // frame (which would repeatedly poke the runtime / spin SteamVR up).
        vrStatus_ = xrSession_->statusMessage();
        pushToast("VR: " + vrStatus_, Plugins::ToastLevel::Error);
        xrSession_.reset();
        vrEnabled_ = false;
        refreshXRDiagnostics(); // surface why in the picker (service not running, …)
        return;
    }
    vrStatus_ = xrSession_->statusMessage();

    // Save the desktop presentation state to restore on leave.
    vrSavedStereoMode_ = stereoMode_;
    vrSavedPresentMode_ = swapchain_.presentMode();

    // The window becomes a mirror/GUI surface: force it mono (the HMD is the
    // stereo output now).
    if (stereoMode_ != StereoMode::Off)
        applyStereoMode(StereoMode::Off);

    // Stop the desktop vsync from capping the HMD frame loop: prefer a
    // non-blocking present mode (Mailbox, else Immediate) so xrWaitFrame paces
    // us, not the monitor. Applied by the swapchain recreate below.
    auto offers = [&](VkPresentModeKHR m) {
        const auto& a = swapchain_.availablePresentModes();
        return std::find(a.begin(), a.end(), m) != a.end();
    };
    VkPresentModeKHR nonBlocking = swapchain_.presentMode();
    if (offers(VK_PRESENT_MODE_MAILBOX_KHR))
        nonBlocking = VK_PRESENT_MODE_MAILBOX_KHR;
    else if (offers(VK_PRESENT_MODE_IMMEDIATE_KHR))
        nonBlocking = VK_PRESENT_MODE_IMMEDIATE_KHR;
    if (nonBlocking != swapchain_.presentMode()) {
        swapchain_.setPreferredPresentMode(nonBlocking);
        handleResize();
    }

    // Retarget the renderer's multiview scene target to the HMD eye resolution
    // (both eyes = 2 views). Device-idle rebuild inside.
    renderer_.beginXR(xrSession_->eyeExtent());

    pushToast("VR on - " + xrSession_->runtimeName(), Plugins::ToastLevel::Success);
}

void Application::leaveXR() {
    // Restore the window-sized mono scene target (waits for GPU idle inside, so
    // the session's eye image views are safe to destroy afterwards).
    renderer_.endXR();

    if (xrSession_) {
        xrSession_->destroy();
        xrSession_.reset();
    }

    // Restore the saved desktop present mode + stereo mode. Set the present-mode
    // preference first so applyStereoMode's swapchain recreate applies it in one
    // shot (applyStereoMode always recreates).
    swapchain_.setPreferredPresentMode(vrSavedPresentMode_);
    applyStereoMode(vrSavedStereoMode_);

    vrStatus_ = "VR off.";
}

rhi::PresentResult Application::renderXRFrame(renderer::FrameSubmission& submission) {
    Engine::XRSession& xr = *xrSession_;

    // Event pump / state machine. false => the runtime ended the session (headset
    // removed, runtime exit) — drop back to the desktop cleanly.
    if (!xr.pollEvents()) {
        leaveXR();
        vrEnabled_ = false;
        return rhi::PresentResult::Success;
    }

    const bool shouldRender = xr.beginFrame();

    // Stream point clouds regardless of XR visibility.
    for (Engine::PointCloud& pc : scene_.pointClouds)
        Engine::PointCloudLoader::updateStreaming(pc);

    // Scene draws / lights / sky / clip-planes (fills mono cameras; the eye
    // cameras overwrite views[0..1] below when the poses are valid).
    buildFrameSubmission(submission);

    // Overlays that make sense in the HMD without a desktop mouse ray: selection
    // outline, clip-plane quads, plugin annotations. The 3D cursor and gizmo are
    // desktop-mouse tools, so they're omitted; no depth pick in XR.
    overlay_.clear();
    appendSelectionOverlay();
    clipPlaneTool_.appendTo(overlay_);
    appendI3SOverlays(); // SLPK inspector bounding volumes (visible in VR too)
    if (pluginContext_)
        pluginManager_.buildOverlay(*pluginContext_);
    submission.overlay = &overlay_;
    submission.fragmentCursor = renderer::FragmentCursorState{};
    submission.depthQueries.clear();

    // Per-eye HMD cameras: world->eye = eyeFromRef * (desktop camera view). The
    // desktop camera is the reference-space anchor — WASD/mouse still fly it,
    // moving the whole play space through the scene.
    Engine::XREyePose eyePoses[2];
    const float nearZ = settings_.vr.useScenePlanes ? settings_.camera.nearPlane
                                                     : settings_.vr.nearPlane;
    const float farZ = settings_.vr.useScenePlanes ? settings_.camera.farPlane
                                                    : settings_.vr.farPlane;
    const bool posesValid =
        shouldRender && xr.getEyePoses(eyePoses, nearZ, farZ, settings_.vr.worldScale);
    if (posesValid) {
        const glm::mat4 sceneView = camera_.GetViewMatrix();
        for (int i = 0; i < 2; ++i) {
            const glm::mat4 combined = eyePoses[i].view * sceneView;
            submission.views[i].view = combined;
            submission.views[i].proj = eyePoses[i].proj;
            submission.views[i].position = glm::vec3(glm::inverse(combined)[3]);
        }
    }

    // Acquire both eye images. Only valid while the session is running (past
    // xrBeginSession); during warmup or a parked/idle session (headset removed)
    // it's not, and acquire returns null.
    renderer::Renderer::XrEyeTarget eyes[2]{};
    bool acquired[2] = { false, false };
    if (xr.isRunning()) {
        for (int i = 0; i < 2; ++i) {
            eyes[i].view = xr.acquireSwapchainImage(i);
            eyes[i].image = xr.acquiredColorImage(i);
            acquired[i] =
                (eyes[i].view != VK_NULL_HANDLE && eyes[i].image != VK_NULL_HANDLE);
        }
    }
    const bool haveEyes = acquired[0] && acquired[1];

    rhi::PresentResult present;
    if (haveEyes) {
        // Render both HMD eyes + the desktop window (ImGui + optional mirror).
        present = renderer_.renderFrameXR(submission, eyes, xr.swapchainIsSrgb(),
                                          settings_.vr.mirrorToWindow,
                                          ImGui::GetDrawData());
    } else {
        // Session warming up / parked (headset idle or removed): no eye images to
        // render, but the desktop window MUST stay live so the user can still turn
        // VR off. Present a normal window frame — the renderer is in XR eye-res
        // 2-view mode, so this resolves the scene as a side-by-side into the
        // window, with ImGui on top. No HMD layers are submitted this frame.
        present = renderer_.renderFrame(submission, ImGui::GetDrawData());
    }

    // Release whatever was acquired (acquire<->release must pair), then close the
    // XR frame if the session is running (pairs xrBeginFrame from beginFrame()) —
    // submit HMD layers only when we actually rendered valid eyes with valid poses.
    for (int i = 0; i < 2; ++i)
        if (acquired[i])
            xr.releaseSwapchainImage(i);
    if (xr.isRunning())
        xr.endFrame(haveEyes && posesValid);

    return present;
}

void Application::updateCursorAndOverlay(renderer::FrameSubmission& submission,
                                         const glm::mat4& view,
                                         const glm::mat4& proj) {
    // Everything picks in render-target pixels via sceneInput_ (the hovered
    // docked viewport image, or the full framebuffer on the classic path).
    const VkExtent2D extent = renderer_.sceneExtent(activeViewport_);

    cursorManager_.resetFrameCalculationFlag();
    submission.depthQueries.clear();

    // While a camera drag owns the mouse (OS cursor disabled) or a gizmo drag is
    // active, freeze the 3D cursor — the pick would fight the gizmo grab point.
    // Side-by-side squishes each eye into a half-target, so the full-target
    // depth pick can't line up; freeze it there too (quad-buffer / mono are
    // full-target and pick normally).
    if (!navActive() && !gizmoDragging_ && stereoMode_ != StereoMode::SideBySide) {
        const int px = int(sceneInput_.mousePx.x);
        const int py = int(sceneInput_.mousePx.y);
        if (sceneInput_.hovered && px >= 0 && py >= 0 &&
            px < int(extent.width) && py < int(extent.height)) {
            renderer::DepthQueryRect rect;
            rect.origin = glm::ivec2(px, py);
            rect.size = glm::ivec2(1, 1);
            submission.depthQueries.push_back(rect);
        }
        cursorManager_.updateCursorPosition(
            sceneInput_.hostWindow ? sceneInput_.hostWindow : window_.handle(),
            sceneInput_.mousePx, sceneInput_.sizePx, sceneInput_.hovered,
            proj, view, activeCamera(), renderer_.depthSamples(),
            /*forceRecalculate=*/true);
    }

    // The scene depth at the screen centre feeds the distance-adaptive
    // fly/zoom speed (updateCameraDepth) and stereo auto-convergence. Queried
    // every frame regardless of nav (both consumers keep tracking while
    // orbiting) and read back one frame later; a 1x1 rect costs nothing.
    {
        renderer::DepthQueryRect rect;
        rect.origin = glm::ivec2(int(extent.width) / 2, int(extent.height) / 2);
        rect.size = glm::ivec2(1, 1);
        submission.depthQueries.push_back(rect);
    }

    // Apply the selected cursor type (the panel edits settings_.cursor.type; the
    // three cursor objects' visibility is derived from it each frame).
    cursorManager_.getSphereCursor()->setVisible(settings_.cursor.type == 0);
    cursorManager_.getPlaneCursor()->setVisible(settings_.cursor.type == 1);
    cursorManager_.getFragmentCursor()->setVisible(settings_.cursor.type == 2);

    // Rebuild the overlay geometry (cursors + orbit centre) for this frame.
    overlay_.clear();
    if (settings_.cursor.show)
        cursorManager_.renderCursors(overlay_, activeCamera());
    // The orbit-centre marker shows during an orbit (or always, if the user
    // pinned it on).
    cursorManager_.setShowOrbitCenter(orbiting_ ||
                                      cursorManager_.isAlwaysShowOrbitCenter());
    cursorManager_.renderOrbitCenter(overlay_, activeCamera().OrbitPoint);

    // Selection outline + transform gizmo + clip planes + plugin annotations
    // (incl. the MeasurementPlugin) share the same overlay list.
    appendSelectionOverlay();
    if (gizmo_.hasTarget())
        gizmo_.appendTo(overlay_, proj, activeCamera().Position);
    clipPlaneTool_.appendTo(overlay_); // section-plane quads + normal arrows
    appendI3SOverlays();               // SLPK inspector bounding volumes
    if (pluginContext_)
        pluginManager_.buildOverlay(*pluginContext_);

    submission.overlay = &overlay_;
    if (settings_.cursor.show)
        cursorManager_.fillFragmentCursorState(submission.fragmentCursor,
                                               activeCamera());
    else
        submission.fragmentCursor = renderer::FragmentCursorState{};
}

void Application::buildFrameSubmission(renderer::FrameSubmission& submission) const {
    // Mono fills view 0 (view 1 duplicated); stereo builds the two eye cameras.
    // Every docked viewport contributes its own camera set; a viewport whose
    // panel wasn't drawn this GUI frame (hidden tab) is flagged so the
    // renderer skips its scene passes entirely.
    viewCameras(submission.views, 0);
    submission.viewportCount =
        std::min(static_cast<uint32_t>(viewports_.size()), renderer::kMaxViewports);
    const bool docked = renderer_.viewportOutputActive();
    for (uint32_t v = 0; v < submission.viewportCount; ++v) {
        if (v > 0)
            viewCameras(submission.extraViewports[v - 1].views, v);
        submission.viewportHidden[v] = docked && !viewports_[v].ui.shown;
    }
    submission.depthPickViewport = activeViewport_;

    submission.draws.clear();
    submission.draws.reserve(scene_.models.size() * 2);
    for (const scene::Model& model : scene_.models) {
        // Effective visibility: own flag AND the group chain (Pass 1 groups).
        if (!model.visible || !scene_.groupChainVisible(model.groupId))
            continue;
        const glm::mat4 modelMatrix = model.modelMatrix();
        const glm::mat3 normalMatrix = model.normalMatrix(modelMatrix);
        for (const scene::ModelMesh& mesh : model.meshes) {
            if (!mesh.buffer.valid())
                continue;
            renderer::DrawItem draw;
            draw.mesh = &mesh.buffer;
            draw.model = modelMatrix;
            draw.normalMatrix = normalMatrix;
            draw.materialIndex = mesh.materialIndex;
            // GL parity: the GL renderer never enabled GL_CULL_FACE for the
            // scene, so imported files with open surfaces or inconsistent
            // winding rendered both faces. Keep that for models; mesh.frag
            // flips the normal on back faces so both sides light correctly.
            draw.twoSided = true;

            // World bounding sphere from the local AABB corners (feeds the sun
            // shadow frustum fit).
            glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local(
                    (corner & 1) ? mesh.boundsMax.x : mesh.boundsMin.x,
                    (corner & 2) ? mesh.boundsMax.y : mesh.boundsMin.y,
                    (corner & 4) ? mesh.boundsMax.z : mesh.boundsMin.z);
                const glm::vec3 world = glm::vec3(modelMatrix * glm::vec4(local, 1.0f));
                minB = glm::min(minB, world);
                maxB = glm::max(maxB, world);
            }
            draw.worldBoundsCenter = (minB + maxB) * 0.5f;
            draw.worldBoundsRadius = glm::length(maxB - minB) * 0.5f;
            submission.draws.push_back(draw);
        }
    }

    submission.pointLights.clear();
    submission.pointLights.reserve(scene_.pointLights.size());
    for (const scene::PointLight& light : scene_.pointLights) {
        if (!light.visible || !scene_.groupChainVisible(light.groupId))
            continue;
        renderer::PointLightState state;
        state.position = light.position;
        state.color = light.color;
        state.intensity = light.intensity;
        state.attenLinear = light.attenLinear;
        state.attenQuadratic = light.attenQuadratic;
        state.castsShadows = light.castsShadows;
        state.radius = light.radius;
        submission.pointLights.push_back(state);
    }

    submission.sun = settings_.lighting.sun;
    submission.sky = settings_.sky;
    submission.shadowsEnabled = settings_.lighting.shadows;
    submission.softShadows = settings_.lighting.softShadows;
    submission.pointShadowRange = settings_.lighting.pointShadowRange;
    submission.ambient = settings_.lighting.ambient;

    // ---- Point clouds (Phase 5: Schütz compute rasterizer) ----
    submission.pointClouds.clear();
    for (const Engine::PointCloud& pc : scene_.pointClouds) {
        if (!pc.visible || !scene_.groupChainVisible(pc.groupId) || !pc.gpu ||
            !pc.gpu->valid() || pc.numBatches == 0)
            continue;
        renderer::PointCloudDrawItem item;
        item.addresses = pc.gpu->addresses;
        item.numBatches = pc.numBatches;
        item.pointsPerThread = pc.computePointsPerThread;
        // Streaming clouds keep full density: phase-1 batches are file-order
        // (scan strips, degenerate AABBs — the spacing estimate over-thins
        // them) and seeing every arriving point is the load feedback anyway.
        // LOD kicks in when the stream (incl. the Morton resort) completes.
        item.densityLod = !pc.isStreaming();
        // GL parity: translate * rotX * rotY * rotZ * scale.
        glm::mat4 model(1.0f);
        model = glm::translate(model, pc.position);
        model = glm::rotate(model, glm::radians(pc.rotation.x), glm::vec3(1, 0, 0));
        model = glm::rotate(model, glm::radians(pc.rotation.y), glm::vec3(0, 1, 0));
        model = glm::rotate(model, glm::radians(pc.rotation.z), glm::vec3(0, 0, 1));
        model = glm::scale(model, pc.scale);
        item.model = model;
        submission.pointClouds.push_back(item);
    }
    submission.pointCloudSettings.hqs = settings_.pointCloud.hqs;
    submission.pointCloudSettings.hqsThreshold = settings_.pointCloud.hqsThreshold;
    submission.pointCloudSettings.splatMaxRadius =
        settings_.pointCloud.splatEnabled ? settings_.pointCloud.splatMaxRadius : 0;
    submission.pointCloudSettings.lodPointsPerPixel =
        settings_.pointCloud.lodEnabled ? settings_.pointCloud.lodPointsPerPixel : 0.0f;

    // ---- SLPK/I3S layers: SSE traversal -> DrawItems (M1) and pool-backed
    // PointCloudDrawItems (M3). Runs AFTER the draws/pointClouds vectors are
    // (re)built above — the layers append to both. The traversal mutates
    // per-layer selection state (hysteresis, load requests) — reached through
    // the unique_ptr, which is why this stays legal in a const method. XR
    // reuses the desktop camera for selection. The traversal runs once per
    // frame against the PRIMARY camera; SSE uses the TALLEST render target so
    // node detail never degrades in the biggest viewport showing the layer.
    uint32_t sseHeight = 0;
    for (uint32_t v = 0; v < std::max(renderer_.viewportOutputCount(), 1u); ++v)
        sseHeight = std::max(sseHeight, renderer_.sceneExtent(v).height);
    for (const std::unique_ptr<scene::I3SSceneLayer>& layer : scene_.i3sLayers)
        if (layer && scene_.groupChainVisible(layer->groupId))
            layer->submitDraws(submission, sseHeight); // checks its own `visible`

    // Global wireframe toggle (M4): flag every draw — scene models and the
    // I3S layers alike (per-layer wireframe is set in the layers' emitDraw).
    // Draws render filled anyway when the device lacks fillModeNonSolid.
    if (settings_.render.wireframe)
        for (renderer::DrawItem& draw : submission.draws)
            draw.wireframe = true;

    // Section/clip planes from the tool (applied whenever enabled planes exist,
    // regardless of the tool's editing mode). Kept side: dot(n,p)+d >= 0.
    submission.clipPlanes.clear();
    glm::vec4 packedPlanes[Engine::MAX_CLIP_PLANES];
    const int planeCount = clipPlaneTool_.collectEnabledPlanes(packedPlanes);
    for (int i = 0; i < planeCount; ++i)
        submission.clipPlanes.push_back(packedPlanes[i]);
}

void Application::reconcileViewportOutput() {
    // Docked whenever the desktop owns the window — every stereo mode
    // included (quad-buffer resolves per-eye into the layered viewport
    // textures; the renderer remaps the GUI images per swapchain layer). XR
    // keeps the classic fullscreen path (the window is a mirror/GUI surface).
    const bool want = !xrRunning();
    if (!want) {
        renderer_.setViewportOutputs(nullptr, 0);
        for (AppViewport& vp : viewports_)
            vp.sizeWant = { 0, 0 };
        return;
    }

    // Erase viewports whose close button was clicked last GUI frame (never
    // the primary). Safe here: setViewportOutputs waits for the device before
    // reconfiguring, and ImGui has not started the next frame yet.
    viewports_.erase(std::remove_if(viewports_.begin() + 1, viewports_.end(),
                                    [](const AppViewport& vp) { return !vp.open; }),
                     viewports_.end());
    if (activeViewport_ >= viewports_.size())
        activeViewport_ = 0;

    // Per-viewport desired texture size: the panel's content region as
    // reported by the last GUI frame. Before a viewport's first report (or
    // while its tab is hidden) keep the current target; a brand-new viewport
    // falls back to a default until its window reports real bounds.
    // Apply a config change (count / first-enable) immediately; apply a pure
    // resize only when the size has settled (same report on two consecutive
    // frames) — a splitter drag thus stretches the displayed image and costs
    // ONE device-idle rebuild when it ends, not one per frame.
    VkExtent2D apply[renderer::kMaxViewports]{};
    const uint32_t count = std::min(static_cast<uint32_t>(viewports_.size()),
                                    renderer::kMaxViewports);
    for (uint32_t i = 0; i < count; ++i) {
        AppViewport& vp = viewports_[i];
        const bool configured = i < renderer_.viewportOutputCount();
        const VkExtent2D current =
            configured ? renderer_.viewportExtent(i) : VkExtent2D{ 0, 0 };

        VkExtent2D target = current;
        if (vp.ui.shown)
            target = { uint32_t(std::max(vp.ui.sizeX, 1.0f)),
                       uint32_t(std::max(vp.ui.sizeY, 1.0f)) };
        else if (!configured)
            target = (i == 0) ? swapchain_.extent() : VkExtent2D{ 960, 540 };

        const bool settled = target.width == vp.sizeWant.width &&
                             target.height == vp.sizeWant.height;
        vp.sizeWant = target;
        apply[i] = (!configured || settled) ? target : current;
    }
    renderer_.setViewportOutputs(apply, count); // no-ops when nothing changed
}

void Application::updateSceneInput() {
    if (renderer_.viewportOutputActive()) {
        // The hovered viewport becomes the interaction target. Never retarget
        // while a drag owns the mouse — the drag finishes against the camera
        // and pick space it started in.
        if (!navActive() && !gizmoDragging_) {
            for (size_t i = 0; i < viewports_.size(); ++i) {
                if (viewports_[i].ui.shown && viewports_[i].ui.hovered) {
                    activeViewport_ = static_cast<uint32_t>(i);
                    break;
                }
            }
        }
        if (activeViewport_ >= viewports_.size())
            activeViewport_ = 0;

        const Gui::ViewportPanelState& ui = viewports_[activeViewport_].ui;
        const VkExtent2D extent = renderer_.viewportExtent(activeViewport_);
        sceneInput_.hovered = ui.shown && ui.hovered;
        sceneInput_.mousePx = glm::vec2(ui.mouseX, ui.mouseY);
        sceneInput_.sizePx =
            glm::vec2(float(std::max(extent.width, 1u)),
                      float(std::max(extent.height, 1u)));
        sceneInput_.screenPos = glm::vec2(ui.screenX, ui.screenY);
        sceneInput_.screenSize = glm::vec2(std::max(ui.screenW, 1.0f),
                                           std::max(ui.screenH, 1.0f));
        sceneInput_.hostWindow =
            ui.hostWindow ? static_cast<GLFWwindow*>(ui.hostWindow)
                          : window_.handle();
        return;
    }
    activeViewport_ = 0;

    // Classic fullscreen path (XR mirror): the scene fills the framebuffer
    // under the GUI; the pointer owns the 3D view wherever no GUI window
    // captures the mouse.
    ImGuiIO& io = ImGui::GetIO();
    const VkExtent2D extent = swapchain_.extent();
    double cx = 0.0, cy = 0.0;
    glfwGetCursorPos(window_.handle(), &cx, &cy);
    // Window coords -> framebuffer pixels (HiDPI content scaling).
    int winW = 0, winH = 0;
    glfwGetWindowSize(window_.handle(), &winW, &winH);
    const double sx = winW > 0 ? double(extent.width) / winW : 1.0;
    const double sy = winH > 0 ? double(extent.height) / winH : 1.0;
    sceneInput_.hovered = !io.WantCaptureMouse;
    sceneInput_.mousePx = glm::vec2(float(cx * sx), float(cy * sy));
    sceneInput_.sizePx = glm::vec2(float(std::max(extent.width, 1u)),
                                   float(std::max(extent.height, 1u)));
    const ImGuiViewport* mainVp = ImGui::GetMainViewport();
    sceneInput_.screenPos = glm::vec2(mainVp->Pos.x, mainVp->Pos.y);
    sceneInput_.screenSize = glm::vec2(mainVp->Size.x, mainVp->Size.y);
    sceneInput_.hostWindow = window_.handle();
}

void Application::initImGui() {
    // Pool for the backend's font/user textures. FREE_DESCRIPTOR_SET is a
    // backend requirement (it frees per-texture sets individually).
    VkDescriptorPoolSize poolSize{};
    poolSize.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    poolSize.descriptorCount = 128;
    VkDescriptorPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    poolInfo.flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT;
    poolInfo.maxSets = 128;
    poolInfo.poolSizeCount = 1;
    poolInfo.pPoolSizes = &poolSize;
    VK_CHECK(vkCreateDescriptorPool(device_.device(), &poolInfo, nullptr, &imguiDescriptorPool_));

    // Context + docking/viewport flags + GLFW platform backend + theme +
    // fonts (project-local imgui_style.cpp, preserved from the GL build).
    // The persisted user scale factor feeds the very first scale computation
    // so the font atlas builds at the right size immediately; the persisted
    // theme (clamped against a file from a newer build) applies right after
    // the context exists.
    int fbWidth = 0, fbHeight = 0;
    window_.framebufferSize(fbWidth, fbHeight);
    g_GuiScale.userScaleFactor = settings_.ui.guiScale;
    UpdateGuiScale(fbWidth, fbHeight);
    settings_.ui.theme =
        std::clamp(settings_.ui.theme, 0, GetGuiThemeCount() - 1);
    InitializeImGuiWithFonts(window_.handle(),
                             IsGuiThemeDark(settings_.ui.theme));
    ApplyGuiTheme(settings_.ui.theme, 1.0f);

    // A floating (undocked) Viewport window must not be dragged around by a
    // camera orbit that starts on its body — windows move by title bar only.
    ImGui::GetIO().ConfigWindowsMoveFromTitleBarOnly = true;

    // The backend copies InitInfo by value but keeps pColorAttachmentFormats
    // as a raw pointer and dereferences it again whenever a dragged-out
    // viewport creates its pipeline — a stack local here means that pipeline
    // gets a garbage color format and the OS window renders black.
    imguiColorFormat_ = swapchain_.format();
    ImGui_ImplVulkan_InitInfo info{};
    info.Instance = device_.instance();
    info.PhysicalDevice = device_.physicalDevice();
    info.Device = device_.device();
    info.QueueFamily = device_.graphicsQueueFamily();
    info.Queue = device_.graphicsQueue();
    info.DescriptorPool = imguiDescriptorPool_;
    info.MinImageCount = 2;
    // Quad-buffer stereo renders the UI once per swapchain layer (2 draws/frame),
    // and the backend advances its UI vertex-buffer ring per RenderDrawData call.
    // Size the ring for 2 draws across the frames in flight so a slot is never
    // overwritten while still being read (harmless extra slots in mono).
    info.ImageCount = std::max(swapchain_.imageCount(),
                               2u * renderer::Renderer::kFramesInFlight + 1u);
    info.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.UseDynamicRendering = true;
    info.PipelineRenderingCreateInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO_KHR;
    info.PipelineRenderingCreateInfo.colorAttachmentCount = 1;
    info.PipelineRenderingCreateInfo.pColorAttachmentFormats = &imguiColorFormat_;
    info.CheckVkResultFn = checkImGuiVkResult;
    if (!ImGui_ImplVulkan_Init(&info))
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    imguiInitialized_ = true;
}

void Application::run() {
    lastFrameTime_ = glfwGetTime();
    renderer::FrameSubmission submission;
    while (!window_.shouldClose()) {
        window_.pollEvents();

        SV_FRAME_MARK();

        // Drag-dropped files + finished SLPK worker parses (both main-thread,
        // CPU-only; new layers are adopted into the scene here). The I3S pump
        // then turns decoded node payloads into GPU residency under a budget.
        handleDroppedFiles();
        pumpSlpkLoads();
        pumpI3SLayers();

        // Identity sweep (contract C3): anything created this frame without
        // an ObjectId — tool-committed measurements, clip planes, imports —
        // gets one. A handful of integer compares per object.
        scene_.ensureIds();

        const double now = glfwGetTime();
        const float dt = std::min(float(now - lastFrameTime_), 0.1f);
        lastFrameTime_ = now;

        if (window_.isMinimized()) {
            glfwWaitEventsTimeout(0.1);
            continue;
        }

        // Debounced preferences save: every ~2.5 s serialize the settings (+
        // command frecency) and write only when something changed. Cheap —
        // the struct is small — and catches every edit path without hooks.
        if (prefsReady_ && now >= prefsNextCheckTime_) {
            prefsNextCheckTime_ = now + 2.5;
            std::string snap = Gui::Preferences::snapshot(settings_, &commands_);
            if (snap != prefsSnapshot_) {
                prefsSnapshot_ = std::move(snap);
                Gui::Preferences::save(kPreferencesFile, settings_, &commands_);
            }
        }

        // Autosave (Pass 5): rotating slots into autosave/, skipped while
        // anything streams. Ids exist by now (ensureIds ran above).
        maybeAutosave(now);

        if (window_.consumeResizeFlag())
            handleResize();

        if (pendingPresentMode_ != VK_PRESENT_MODE_MAX_ENUM_KHR) {
            swapchain_.setPreferredPresentMode(pendingPresentMode_);
            pendingPresentMode_ = VK_PRESENT_MODE_MAX_ENUM_KHR;
            handleResize();
        }

        // Apply a queued stereo-mode change at a frame boundary (never mid-frame).
        // Suppressed while VR owns the window (the desktop is forced mono then).
        if (stereoModePending_ && !xrRunning()) {
            stereoModePending_ = false;
            applyStereoMode(stereoModeRequested_);
        }

        // Reconcile the live VR toggle at a frame boundary — enter/leave rebuild
        // the scene target with the device idle, so this must not run mid-frame.
        // enterXR() resets vrEnabled_ itself if the session fails to start.
        if (vrEnabled_ && !xrRunning())
            enterXR();
        else if (!vrEnabled_ && xrRunning())
            leaveXR();

        // Docked-viewport reconcile AFTER the stereo/XR reconciles (it derives
        // from their final state) and BEFORE ImGui::NewFrame — the previous
        // frame's draw data must never reference a destroyed viewport texture.
        reconcileViewportOutput();

        // Font atlas rebuilds must happen outside NewFrame/Render, with the
        // GPU idle (the texture may still be bound by an in-flight frame).
        if (g_GuiScale.needsRescale || g_GuiScale.needsFontRebuild) {
            device_.waitIdle();
            RescaleImGuiFonts(window_.handle(), IsGuiThemeDark(g_currentTheme));
        }

        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplGlfw_NewFrame();
        ImGui::NewFrame();
        buildUi();
        // Capture this frame's wheel BEFORE Render: ImGui::EndFrame (inside
        // Render) zeroes io.MouseWheel, and updateCamera runs after the GUI
        // pass — it would only ever see 0 there (scroll zoom was dead).
        wheelThisFrame_ = ImGui::GetIO().MouseWheel;
        ImGui::Render();

        // Effective 3D-view input for this frame — after the GUI built (the
        // Viewport panel just reported its rect/hover), before any interaction.
        updateSceneInput();

        updateCamera(dt);
        if (pluginContext_)
            pluginManager_.update(*pluginContext_, dt);

        // Async compute is a per-frame decision in the renderer, so the panel
        // toggle applies instantly (and safely mid-run) on both render paths.
        renderer_.setAsyncCompute(settings_.render.asyncCompute);

        rhi::PresentResult presentResult = rhi::PresentResult::Success;
        if (xrRunning()) {
            // VR owns the scene submission + HMD present. The desktop window
            // still receives ImGui (+ optional left-eye mirror) inside
            // renderXRFrame, so the VR toggle stays reachable.
            presentResult = renderXRFrame(submission);
        } else {
            // Depth-driven stereo convergence (before the eye matrices are built).
            updateStereoConvergence(dt);

            // 3D cursor depth-pick + overlay geometry for this frame (uses the
            // same view/proj the scene renders with). Must run before the
            // submission is built so the overlay/fragment-cursor/depth-query
            // fields are populated.
            glm::mat4 view(1.0f), proj(1.0f);
            cameraMatrices(view, proj);
            updateCursorAndOverlay(submission, view, proj);

            // Pump streaming point clouds: stages this frame's chunk copies into
            // the upload ring; renderFrame records them at the top of the frame.
            for (Engine::PointCloud& pc : scene_.pointClouds)
                Engine::PointCloudLoader::updateStreaming(pc);

            buildFrameSubmission(submission);
            presentResult = renderer_.renderFrame(submission, ImGui::GetDrawData());
        }

        // Secondary OS windows (dragged-out panels): the Vulkan backend owns
        // their swapchains and presents them itself. Runs in both paths.
        if (ImGui::GetIO().ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
            ImGui::UpdatePlatformWindows();
            ImGui::RenderPlatformWindowsDefault();
        }

        if (presentResult == rhi::PresentResult::OutOfDate) {
            handleResize();
        } else if (presentResult == rhi::PresentResult::Suboptimal) {
            // Recreate only when the size really changed. A driver that
            // reports Suboptimal persistently must not trap the loop in a
            // waitIdle+recreate cycle (which shows up as a hard fps cap and
            // laggy input).
            int width = 0, height = 0;
            window_.framebufferSize(width, height);
            if (width > 0 && height > 0 &&
                (static_cast<uint32_t>(width) != swapchain_.extent().width ||
                 static_cast<uint32_t>(height) != swapchain_.extent().height))
                handleResize();
        }
    }
}

void Application::buildUi() {
    // The hover readout is panel-driven (the Scene Layers panel re-picks the
    // node under the mouse while it is visible). Clear it first so a hidden/
    // closed panel cannot leave a stale hover highlight behind.
    for (const std::unique_ptr<scene::I3SSceneLayer>& layer : scene_.i3sLayers)
        if (layer)
            layer->hoverNode = -1;

    // The production GUI: the dockspace host + panels (Gui/) replace the interim
    // debug panel. GuiSystem::draw also hosts the plugin (tool) windows; the
    // toast overlay stays app-owned and renders on top.
    guiSystem_.draw(*guiServices_);
    drawToasts();
}

// File -> Import Model: Assimp import into the interim scene::Scene. (Scene
// save/merge is gated on the not-yet-ported SceneManager; see docs/TODO.md E.)
void Application::openModelDialog() {
    const std::vector<std::string> modelFiles =
        pfd::open_file("Import model", "",
                       { "Model files",
                         "*.obj *.fbx *.gltf *.glb *.dae *.ply *.stl *.3ds *.blend",
                         "All files", "*" },
                       pfd::opt::multiselect)
            .result();
    importModelFiles(modelFiles);
}

void Application::importModelFiles(const std::vector<std::string>& files) {
    if (files.empty())
        return;

    int added = 0;
    for (const std::string& path : files) {
        scene::Model model;
        if (scene::importModelFile(path, device_, renderer_.materials(), model)) {
            model.id = scene_.allocateId();
            scene_.models.push_back(std::move(model));
            ++added;
        } else {
            pushToast("Failed to import " +
                          std::filesystem::path(path).filename().string(),
                      Plugins::ToastLevel::Error);
        }
    }
    if (added > 0) {
        scene_.computeWorldBounds();
        pushToast(std::to_string(added) + " model(s) imported",
                  Plugins::ToastLevel::Success);
    }
}

void Application::openPointCloudDialog() {
    const std::vector<std::string> files =
        pfd::open_file("Load point cloud", "",
                       { "Point clouds",
                         "*.las *.laz *.xyz *.txt *.ply *.pcb *.h5 *.hdf5 *.f5",
                         "All files", "*" },
                       pfd::opt::multiselect)
            .result();
    loadPointCloudFiles(files);
}

void Application::loadPointCloudFiles(const std::vector<std::string>& files) {
    if (files.empty())
        return;

    const size_t downsample = static_cast<size_t>(settings_.pointCloud.downsample);

    // LAS/LAZ ride the progressive streaming path (a multi-selection shares
    // one centre so tiles stay aligned); everything else loads synchronously
    // through the format-dispatching text/binary loaders.
    std::vector<std::string> lasFiles;
    auto adopt = [this](Engine::PointCloud&& pc) {
        pc.id = scene_.allocateId();
        scene_.pointClouds.push_back(std::move(pc));
    };
    for (const std::string& path : files) {
        std::string ext = std::filesystem::path(path).extension().string();
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        if (ext == ".las" || ext == ".laz") {
            lasFiles.push_back(path);
            continue;
        }
        Engine::PointCloud pc =
            Engine::PointCloudLoader::loadPointCloudFile(path, downsample);
        if (pc.isLoaded())
            adopt(std::move(pc));
        else
            std::cerr << "[app] failed to load point cloud: " << path << "\n";
    }
    if (lasFiles.size() == 1) {
        Engine::PointCloud pc = Engine::PointCloudLoader::beginLoadLASProgressive(
            lasFiles[0], downsample, nullptr, settings_.pointCloud.mortonResort);
        if (pc.isLoaded())
            adopt(std::move(pc));
        else
            std::cerr << "[app] failed to start streaming: " << lasFiles[0] << "\n";
    } else if (lasFiles.size() > 1) {
        std::vector<Engine::PointCloud> clouds =
            Engine::PointCloudLoader::beginLoadLASMultipleProgressive(
                lasFiles, downsample, settings_.pointCloud.mortonResort);
        for (Engine::PointCloud& pc : clouds)
            adopt(std::move(pc));
    }
    scene_.computeWorldBounds();
}

// ---- SLPK / I3S scene layers (M0: open + inspect) ----

void Application::openSlpkDialog() {
    const std::vector<std::string> files =
        pfd::open_file("Open scene layer package", "",
                       { "Scene layer packages (*.slpk)", "*.slpk",
                         "All files", "*" },
                       pfd::opt::multiselect)
            .result();
    for (const std::string& path : files)
        openSlpk(path);
}

void Application::openSlpk(const std::string& path) {
    auto job = std::make_unique<SlpkLoadJob>();
    job->layer = std::make_unique<scene::I3SSceneLayer>();
    scene::I3SSceneLayer* layer = job->layer.get();
    std::atomic<bool>* done = &job->done;
    // Pure CPU work (mmap + gunzip + JSON + geodetic math) — no Vulkan on the
    // worker, mirroring the PointCloudLoader progressive pattern.
    job->thread = std::thread([layer, done, path]() {
        layer->load(path);
        done->store(true, std::memory_order_release);
    });
    slpkJobs_.push_back(std::move(job));
    pushToast("Opening " + std::filesystem::path(path).filename().string() + "...",
              Plugins::ToastLevel::Info);
}

void Application::pumpSlpkLoads() {
    for (size_t i = 0; i < slpkJobs_.size();) {
        SlpkLoadJob& job = *slpkJobs_[i];
        if (!job.done.load(std::memory_order_acquire)) {
            ++i;
            continue;
        }
        job.thread.join();
        std::unique_ptr<scene::I3SSceneLayer> layer = std::move(job.layer);
        slpkJobs_.erase(slpkJobs_.begin() + static_cast<std::ptrdiff_t>(i));

        if (!layer->error().empty()) {
            pushToast("SLPK open failed: " + layer->error(),
                      Plugins::ToastLevel::Error);
            continue;
        }
        // A redo re-deleted this layer while its (undo-triggered) re-open was
        // still parsing: drop the result silently (pure CPU state — no GPU
        // residency exists before startStreaming).
        {
            const auto cancelled =
                std::find(cancelledLayerOpens_.begin(),
                          cancelledLayerOpens_.end(), layer->sourcePath);
            if (cancelled != cancelledLayerOpens_.end()) {
                cancelledLayerOpens_.erase(cancelled);
                continue;
            }
        }
        if (!layer->info.sr.isGeographic() && layer->info.sr.wkid == 0)
            pushToast("Unknown CRS — layer loads in its own local space",
                      Plugins::ToastLevel::Warning);
        // Inspector-only layer types (feature-symbol "Point", BSL "Building")
        // draw nothing: warn instead of celebrating, and never auto-frame —
        // framing bounds no draw will ever fill flings the camera into space
        // (a global Point layer spans the planet).
        const bool renderable = layer->rendersAnything();
        if (renderable)
            pushToast(layer->name + ": " + std::to_string(layer->tree.nodes.size()) +
                          " nodes, " + std::to_string(layer->tree.levelCount) +
                          " levels (v" + layer->info.version + ")",
                      Plugins::ToastLevel::Success);
        else
            pushToast(layer->name + ": layerType \"" + layer->info.typeString +
                          "\" has no renderer yet — node tree/bounds inspector only",
                      Plugins::ToastLevel::Warning);

        // Scene-document re-open (load / merge / delete-undo): re-apply the
        // saved identity + display state parked for this source path, so the
        // layer keeps its ObjectId, group, name and visibility across the
        // async round trip. Fresh user opens get a fresh id.
        bool restored = false;
        for (size_t p = 0; p < pendingLayerStates_.size(); ++p) {
            if (pendingLayerStates_[p].sourcePath != layer->sourcePath)
                continue;
            const scene::PendingLayerState& st = pendingLayerStates_[p];
            layer->id = st.id;
            layer->groupId = st.groupId;
            layer->visible = st.visible;
            layer->locked = st.locked;
            layer->showGeometry = st.showGeometry;
            if (!st.name.empty())
                layer->name = st.name;
            pendingLayerStates_.erase(pendingLayerStates_.begin() +
                                      static_cast<std::ptrdiff_t>(p));
            restored = true;
            break;
        }
        if (layer->id == 0)
            layer->id = scene_.allocateId();

        scene_.i3sLayers.push_back(std::move(layer));
        scene_.i3sLayers.back()->startStreaming(); // M1: spawn decode workers
        scene_.computeWorldBounds();
        // Restored layers keep the user's current view; fresh opens frame.
        if (renderable && !restored)
            frameI3SLayer(scene_.i3sLayers.size() - 1);
    }
}

void Application::pumpI3SLayers() {
    // ONE per-frame budget pair shared across all layers (panel-editable,
    // plan §6.5): pump CPU time + bytes staged through the upload ring. Each
    // layer's pump decrements what it consumed; the byte budget is post-paid
    // so a single oversized node can overshoot once instead of never loading.
    double budgetMs = std::max(double(scene::I3SSceneLayer::sPumpBudgetMs), 0.5);
    int64_t budgetStageBytes =
        int64_t(std::max(scene::I3SSceneLayer::sPumpStageBudgetMB, 4)) * 1024 * 1024;
    for (const std::unique_ptr<scene::I3SSceneLayer>& layer : scene_.i3sLayers) {
        if (!layer)
            continue;
        if (budgetMs > 0.0)
            layer->pump(device_, renderer_.materials(), renderer_.uploadRing(),
                        renderer_.frameRetireValue(), renderer_.completedFrameValue(),
                        budgetMs, budgetStageBytes);
        for (const std::string& warning : layer->drainWarnings())
            pushToast(warning, Plugins::ToastLevel::Warning);
    }
}

void Application::appendI3SOverlays() {
    for (const std::unique_ptr<scene::I3SSceneLayer>& layer : scene_.i3sLayers)
        if (layer && layer->visible && layer->wantsObbOverlay())
            layer->appendObbOverlay(overlay_);
}

void Application::frameI3SLayer(size_t index) {
    if (index >= scene_.i3sLayers.size() || !scene_.i3sLayers[index])
        return;
    const scene::I3SSceneLayer& layer = *scene_.i3sLayers[index];
    if (layer.nodeBoxes.empty())
        return;

    const glm::vec3 center = (layer.boundsMin + layer.boundsMax) * 0.5f;
    const float radius =
        std::max(glm::length(layer.boundsMax - layer.boundsMin) * 0.5f, 1.0f);

    // Fit the bounding sphere into the vertical FOV from a pleasant 3/4 view.
    const float fovRad = glm::radians(std::max(settings_.camera.fovDeg, 10.0f));
    const float distance = radius / std::tan(fovRad * 0.5f) * 1.15f;
    const glm::vec3 viewDir = glm::normalize(glm::vec3(0.55f, 0.45f, 0.9f));

    Camera& cam = activeCamera(); // frame the layer in the viewport in use
    Camera::CameraState state = cam.GetState();
    state.position = center + viewDir * distance;
    const glm::vec3 f = glm::normalize(center - state.position);
    const glm::vec3 r = glm::normalize(glm::cross(f, glm::vec3(0.0f, 1.0f, 0.0f)));
    const glm::vec3 u = glm::normalize(glm::cross(r, f));
    state.orientation = glm::normalize(glm::quat_cast(glm::mat3(r, u, -f)));
    cam.SetState(state);
    cam.SetOrbitPointDirectly(center);

    // City-scale layers outgrow the default clip range and fly speed; widen
    // them (never shrink a user's larger setting).
    settings_.camera.farPlane =
        std::max(settings_.camera.farPlane, distance + radius * 4.0f);
    settings_.camera.speed = std::max(settings_.camera.speed, radius * 0.05f);
}

void Application::handleDroppedFiles() {
    // Drag-drop is just another import source since Pass 5: one entry point
    // (importFiles) plans the paths, sniffs the ambiguous ones, skips dupes,
    // groups + names + selects + frames. The old body classified by extension
    // inline and hard-wired .ply to point clouds — a dropped PLY *mesh* silently
    // became a vertex cloud. core::ImportService reads the header instead.
    const std::vector<std::string> dropped = window_.consumeDroppedFiles();
    if (!dropped.empty())
        importFiles(dropped);
}

void Application::handleResize() {
    int width = 0, height = 0;
    window_.framebufferSize(width, height);
    if (width == 0 || height == 0)
        return;
    swapchain_.recreate(static_cast<uint32_t>(width), static_cast<uint32_t>(height));
    renderer_.onSwapchainRecreated();
    ++swapchainRecreations_;
    // The surface format selection is deterministic, so this stays stable; if
    // it ever changed, the ImGui main pipeline would need a reinit as well.
    imguiColorFormat_ = swapchain_.format();
}

void Application::shutdownImGui() {
    if (imguiInitialized_) {
        device_.waitIdle();
        ImGui_ImplVulkan_Shutdown();
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        imguiInitialized_ = false;
    }
    if (imguiDescriptorPool_ != VK_NULL_HANDLE) {
        vkDestroyDescriptorPool(device_.device(), imguiDescriptorPool_, nullptr);
        imguiDescriptorPool_ = VK_NULL_HANDLE;
    }
}

void Application::shutdown() {
    // Persist user state first. prefsReady_ is only set once init() got past
    // loading + command registration, so an early init failure can never
    // clobber a user's files with defaults. (shutdown may run twice — the
    // explicit call and the destructor — the second write is identical.)
    if (prefsReady_) {
        Gui::Preferences::save(kPreferencesFile, settings_, &commands_);
        shortcuts_.saveToFile(kShortcutsFile);
    }
    // Clean exit: drop the session lock. A crash leaves it behind — that IS the
    // staleness signal the next launch recovers from (Pass 5).
    endSession();

    if (device_.device() != VK_NULL_HANDLE)
        device_.waitIdle();
    // The VR session owns XR handles + Vulkan image views on device_; release it
    // (device idle) before the renderer/device it borrows from are torn down.
    if (xrSession_) {
        xrSession_->destroy();
        xrSession_.reset();
    }
    shutdownImGui();
    // Join in-flight SLPK parses (CPU-only workers; their layers are dropped).
    for (std::unique_ptr<SlpkLoadJob>& job : slpkJobs_)
        if (job && job->thread.joinable())
            job->thread.join();
    slpkJobs_.clear();
    // Undo closures can own deleted objects (their GPU residency included —
    // see SceneOps.cpp deleteItems), so the stack clears here, while the
    // device is idle and still alive. Then the scene's GPU buffers (models,
    // point clouds, layers) go before the renderer/device they live on
    // (cloud destruction also joins any still-streaming worker).
    undo_.clear();
    scene_ = scene::Scene{};
    renderer_.shutdown();
    swapchain_.shutdown();
    device_.shutdown();
    window_.shutdown();
}

// ---- Selection, picking, toasts (Phase 6) ----

void Application::performSelectionClick() {
    // clickCursorWorld_ is the exact surface point the GPU depth pick reported
    // under the press (empty space -> clear the selection).
    for (const std::unique_ptr<scene::I3SSceneLayer>& layer : scene_.i3sLayers) {
        if (!layer)
            continue;
        layer->pickedNode = -1;
        layer->pickedFeature = scene::I3SSceneLayer::PickedFeature{};
    }
    const bool additive = (currentMods() & GLFW_MOD_SHIFT) != 0; // Ctrl+Shift
    if (!clickCursorValid_) {
        if (!additive)
            selection_.clear();
        return;
    }
    scene::RayHit hit;
    if (!scene::pickModelAtPoint(scene_, clickCursorWorld_, hit)) {
        // M1 I3S picking: the depth-picked surface point resolves to the
        // deepest node drawn this frame whose OBB contains it. Node-level
        // info shows in the Scene Layers panel (exact per-feature picking is
        // the M4 attribute work).
        for (const std::unique_ptr<scene::I3SSceneLayer>& layer : scene_.i3sLayers) {
            if (!layer || !layer->visible || !layer->showGeometry)
                continue;
            const int node = layer->pickNodeAt(clickCursorWorld_);
            if (node >= 0) {
                layer->pickedNode = node;
                // M4: resolve the feature under the point + its attribute
                // row (synchronous decode — click-rate only).
                layer->pickFeatureAt(clickCursorWorld_, node);
                break;
            }
        }
        if (!additive)
            selection_.clear();
        return;
    }

    scene::Model& model = scene_.models[hit.modelIndex];
    // Locked objects are not viewport-selectable (outliner lock).
    if (model.locked || scene_.groupChainLocked(model.groupId))
        return;

    scene::SceneItemRef modelRef;
    modelRef.kind = scene::SceneItemRef::Kind::Model;
    modelRef.id = model.id;
    modelRef.index = hit.modelIndex;

    // Ctrl+Shift+click toggles membership in the multi-selection; a plain
    // Ctrl+click keeps the GL single-select feel: first click selects the
    // whole model, clicking the SAME model again drills to the sub-mesh.
    if (additive) {
        selection_.toggle(modelRef);
        return;
    }
    const scene::SceneItemRef primary = selection_.primary();
    const bool sameModel =
        (primary.kind == scene::SceneItemRef::Kind::Model ||
         primary.kind == scene::SceneItemRef::Kind::Mesh) &&
        primary.id == model.id;
    const bool hasSubMeshes = model.meshes.size() > 1;
    if (sameModel && hasSubMeshes && hit.meshIndex >= 0) {
        scene::SceneItemRef meshRef = modelRef;
        meshRef.kind = scene::SceneItemRef::Kind::Mesh;
        meshRef.sub = hit.meshIndex;
        selection_.selectOne(meshRef);
    } else {
        selection_.selectOne(modelRef);
    }
}

void Application::appendSelectionOverlay() {
    // Outline every selected item; the PRIMARY draws brighter/wider so the
    // gizmo target reads at a glance. Measurements/clip planes draw themselves.
    const std::vector<scene::SceneItemRef>& items = selection_.items();
    if (items.empty())
        return;
    const scene::SceneItemRef primaryRef = selection_.primary();

    auto drawBox = [&](const glm::mat4& m, glm::vec3 lo, glm::vec3 hi,
                       const glm::vec4& color, float width) {
        if (lo.x > hi.x)
            return;
        glm::vec3 corner[8];
        for (int i = 0; i < 8; ++i) {
            const glm::vec3 local((i & 1) ? hi.x : lo.x, (i & 2) ? hi.y : lo.y,
                                  (i & 4) ? hi.z : lo.z);
            corner[i] = glm::vec3(m * glm::vec4(local, 1.0f));
        }
        auto edge = [&](int a, int b) {
            overlay_.line(corner[a], corner[b], color, width,
                          renderer::OverlayDepth::Always);
        };
        edge(0, 1); edge(2, 3); edge(4, 5); edge(6, 7); // X
        edge(0, 2); edge(1, 3); edge(4, 6); edge(5, 7); // Y
        edge(0, 4); edge(1, 5); edge(2, 6); edge(3, 7); // Z
    };

    for (scene::SceneItemRef ref : items) {
        if (!scene_.resolve(ref))
            continue;
        const bool isPrimary = (ref == primaryRef);
        const float width = isPrimary ? 2.0f : 1.5f;
        using Kind = scene::SceneItemRef::Kind;
        switch (ref.kind) {
        case Kind::Model:
        case Kind::Mesh: {
            const scene::Model& model = scene_.models[ref.index];
            if (!model.visible || model.meshes.empty())
                break;
            glm::vec3 lo(FLT_MAX), hi(-FLT_MAX);
            if (ref.kind == Kind::Mesh) {
                lo = model.meshes[ref.sub].boundsMin;
                hi = model.meshes[ref.sub].boundsMax;
            } else {
                for (const scene::ModelMesh& mesh : model.meshes) {
                    if (mesh.boundsMin.x > mesh.boundsMax.x)
                        continue;
                    lo = glm::min(lo, mesh.boundsMin);
                    hi = glm::max(hi, mesh.boundsMax);
                }
            }
            // Sub-mesh outline is cyan; whole-model is orange (secondary
            // selection dimmed toward grey).
            glm::vec4 color = (ref.kind == Kind::Mesh)
                                  ? glm::vec4(0.20f, 0.90f, 1.00f, 1.0f)
                                  : glm::vec4(1.00f, 0.60f, 0.10f, 1.0f);
            if (!isPrimary)
                color = glm::vec4(glm::mix(glm::vec3(color), glm::vec3(0.85f), 0.35f),
                                  0.9f);
            drawBox(model.modelMatrix(), lo, hi, color, width);
            break;
        }
        case Kind::PointCloud: {
            const Engine::PointCloud& pc = scene_.pointClouds[ref.index];
            if (!pc.visible || !pc.hasBounds())
                break;
            glm::mat4 m(1.0f);
            m = glm::translate(m, pc.position);
            m = glm::rotate(m, glm::radians(pc.rotation.x), glm::vec3(1, 0, 0));
            m = glm::rotate(m, glm::radians(pc.rotation.y), glm::vec3(0, 1, 0));
            m = glm::rotate(m, glm::radians(pc.rotation.z), glm::vec3(0, 0, 1));
            m = glm::scale(m, pc.scale);
            const glm::vec4 color(0.45f, 0.75f, 1.00f, isPrimary ? 1.0f : 0.85f);
            drawBox(m, pc.boundsMin, pc.boundsMax, color, width);
            break;
        }
        case Kind::SceneLayer: {
            const scene::I3SSceneLayer& layer = *scene_.i3sLayers[ref.index];
            if (!layer.visible || layer.nodeBoxes.empty())
                break;
            drawBox(glm::mat4(1.0f), layer.boundsMin, layer.boundsMax,
                    glm::vec4(0.35f, 0.85f, 0.75f, isPrimary ? 1.0f : 0.85f),
                    width);
            break;
        }
        case Kind::PointLight: {
            // Small world-space cross marker at the emitter.
            const glm::vec3 p = scene_.pointLights[ref.index].position;
            const float s = 0.25f;
            const glm::vec4 color(1.00f, 0.78f, 0.40f, 1.0f);
            overlay_.line(p - glm::vec3(s, 0, 0), p + glm::vec3(s, 0, 0), color,
                          width, renderer::OverlayDepth::Always);
            overlay_.line(p - glm::vec3(0, s, 0), p + glm::vec3(0, s, 0), color,
                          width, renderer::OverlayDepth::Always);
            overlay_.line(p - glm::vec3(0, 0, s), p + glm::vec3(0, 0, s), color,
                          width, renderer::OverlayDepth::Always);
            break;
        }
        default:
            break; // Sun/Environment/Group/annotations: no box outline
        }
    }
}

void Application::pushToast(const std::string& message, Plugins::ToastLevel level) {
    toasts_.push_back({ message, level, 3.5f });
    if (toasts_.size() > 6)
        toasts_.erase(toasts_.begin());
}

void Application::drawToasts() {
    const float dt = ImGui::GetIO().DeltaTime;
    for (Toast& t : toasts_)
        t.ttl -= dt;
    toasts_.erase(std::remove_if(toasts_.begin(), toasts_.end(),
                                 [](const Toast& t) { return t.ttl <= 0.0f; }),
                  toasts_.end());
    if (toasts_.empty())
        return;

    const ImGuiViewport* vp = ImGui::GetMainViewport();
    const ImVec2 anchor(vp->WorkPos.x + vp->WorkSize.x - 16.0f,
                        vp->WorkPos.y + vp->WorkSize.y - 16.0f);
    ImGui::SetNextWindowPos(anchor, ImGuiCond_Always, ImVec2(1.0f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.85f);
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
        ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoMove;
    if (ImGui::Begin("##toasts", nullptr, flags)) {
        for (const Toast& t : toasts_) {
            ImVec4 col;
            switch (t.level) {
            case Plugins::ToastLevel::Success: col = ImVec4(0.40f, 0.90f, 0.45f, 1.0f); break;
            case Plugins::ToastLevel::Warning: col = ImVec4(0.95f, 0.75f, 0.20f, 1.0f); break;
            case Plugins::ToastLevel::Error:   col = ImVec4(0.95f, 0.40f, 0.40f, 1.0f); break;
            default:                           col = ImVec4(0.80f, 0.82f, 0.88f, 1.0f); break;
            }
            ImGui::TextColored(col, "%s", t.text.c_str());
        }
    }
    ImGui::End();
}

// ---- Commands, shortcuts (UI redesign Pass 0) ----

// The ONE place actions, menu grouping (category + separatorBefore) and
// default key bindings are defined. GuiSystem::drawMenuBar renders from this
// registry; dispatchShortcuts() and the future palette/macros run the same
// commands (contract C5). Actions go through guiServices_ where a service
// exists — the same surface the panels use — and through app internals
// otherwise.
void Application::registerCommands() {
    using core::Command;
    using core::ShortcutBinding;
    // Captured as a pointer BY VALUE: these closures outlive this scope,
    // and guiServices_ lives as long as the Application.
    Gui::Services* services = guiServices_.get();

    auto add = [this](Command command) { commands_.add(std::move(command)); };

    // ── File ─────────────────────────────────────────────────────────────
    {
        // The one import entry (Pass 5): any mix of models / clouds / .slpk /
        // .scene in a single combined-filter dialog.
        Command c;
        c.id = "file.import_files";
        c.title = "Import files...";
        c.category = "File";
        c.keywords = "import load open add model point cloud slpk scene drop any";
        c.action = [this] { importFilesDialog(); };
        add(std::move(c));
        shortcuts_.registerDefault("file.import_files",
                                   ShortcutBinding{ GLFW_KEY_I, /*ctrl=*/true });
    }
    {
        Command c;
        c.id = "file.import_model";
        c.title = "Import model...";
        c.category = "File";
        c.keywords = "load mesh obj fbx gltf assimp open";
        c.action = [services] { services->importModelDialog(); };
        add(std::move(c));
    }
    {
        Command c;
        c.id = "file.import_pointcloud";
        c.title = "Import point cloud...";
        c.category = "File";
        c.keywords = "load las laz ply xyz pcb hdf5 open";
        c.action = [services] { services->openPointCloudDialog(); };
        add(std::move(c));
    }
    {
        Command c;
        c.id = "file.open_slpk";
        c.title = "Open scene layer (.slpk)...";
        c.category = "File";
        c.keywords = "i3s esri package layer import";
        c.action = [services] { services->openSlpkDialog(); };
        add(std::move(c));
    }
    // Scene document ops (UI redesign Pass 1: scene::SceneDocument).
    {
        Command c;
        c.id = "file.new_scene";
        c.title = "New scene";
        c.category = "File";
        c.keywords = "clear empty document";
        c.separatorBefore = true;
        c.action = [services] { services->newScene(); };
        add(std::move(c));
        shortcuts_.registerDefault("file.new_scene",
                                   ShortcutBinding{ GLFW_KEY_N, true });
    }
    {
        Command c;
        c.id = "file.open_scene";
        c.title = "Open scene...";
        c.category = "File";
        c.keywords = "load document";
        c.action = [services] { services->openSceneDialog(); };
        add(std::move(c));
        shortcuts_.registerDefault("file.open_scene",
                                   ShortcutBinding{ GLFW_KEY_O, true });
    }
    {
        Command c;
        c.id = "file.save_scene";
        c.title = "Save scene";
        c.category = "File";
        c.keywords = "write document";
        c.action = [services] { services->saveScene(); };
        add(std::move(c));
        shortcuts_.registerDefault("file.save_scene",
                                   ShortcutBinding{ GLFW_KEY_S, true });
    }
    {
        Command c;
        c.id = "file.save_scene_as";
        c.title = "Save scene as...";
        c.category = "File";
        c.keywords = "write copy document";
        c.action = [services] { services->saveSceneAs(); };
        add(std::move(c));
        shortcuts_.registerDefault(
            "file.save_scene_as",
            ShortcutBinding{ GLFW_KEY_S, true, false, true }); // Ctrl+Shift+S
    }
    {
        Command c;
        c.id = "file.merge_scene";
        c.title = "Merge scene...";
        c.category = "File";
        c.keywords = "combine append import document";
        c.tooltip = "Add another scene file's objects to the current scene.";
        c.action = [services] { services->mergeSceneDialog(); };
        add(std::move(c));
    }
    {
        Command c;
        c.id = "file.screenshot";
        c.title = "Save screenshot";
        c.category = "File";
        c.keywords = "capture image png";
        c.separatorBefore = true;
        c.enabled = [services] { return !services->screenshotPending(); };
        c.action = [services] { services->requestScreenshot(); };
        add(std::move(c));
        shortcuts_.registerDefault("file.screenshot",
                                   ShortcutBinding{ GLFW_KEY_F12 });
    }
    {
        Command c;
        c.id = "file.exit";
        c.title = "Exit";
        c.category = "File";
        c.keywords = "quit close";
        c.separatorBefore = true;
        c.action = [services] { services->requestQuit(); };
        add(std::move(c));
    }

    // ── Edit ─────────────────────────────────────────────────────────────
    {
        Command c;
        c.id = "edit.undo";
        c.title = "Undo";
        c.category = "Edit";
        c.enabled = [this] { return undo_.canUndo(); };
        c.action = [this] { undo_.undo(); };
        add(std::move(c));
        shortcuts_.registerDefault("edit.undo",
                                   ShortcutBinding{ GLFW_KEY_Z, true });
    }
    {
        Command c;
        c.id = "edit.redo";
        c.title = "Redo";
        c.category = "Edit";
        c.enabled = [this] { return undo_.canRedo(); };
        c.action = [this] { undo_.redo(); };
        add(std::move(c));
        shortcuts_.registerDefault(
            "edit.redo", ShortcutBinding{ GLFW_KEY_Y, true },
            ShortcutBinding{ GLFW_KEY_Z, true, false, true }); // Ctrl+Shift+Z
    }
    // Gizmo modes (enabled while something is selected — same gate the old
    // hardcoded 1/2/3 keys had).
    const auto gizmoActive = [this] { return gizmo_.hasTarget(); };
    {
        Command c;
        c.id = "gizmo.translate";
        c.title = "Gizmo: translate";
        c.category = "Edit";
        c.separatorBefore = true;
        c.enabled = gizmoActive;
        c.checked = [this] {
            return gizmo_.mode() == Tools::TransformGizmo::Mode::Translate;
        };
        c.action = [this] {
            gizmo_.setMode(Tools::TransformGizmo::Mode::Translate);
        };
        add(std::move(c));
        shortcuts_.registerDefault("gizmo.translate",
                                   ShortcutBinding{ GLFW_KEY_1 });
    }
    {
        Command c;
        c.id = "gizmo.rotate";
        c.title = "Gizmo: rotate";
        c.category = "Edit";
        c.enabled = gizmoActive;
        c.checked = [this] {
            return gizmo_.mode() == Tools::TransformGizmo::Mode::Rotate;
        };
        c.action = [this] {
            gizmo_.setMode(Tools::TransformGizmo::Mode::Rotate);
        };
        add(std::move(c));
        shortcuts_.registerDefault("gizmo.rotate", ShortcutBinding{ GLFW_KEY_2 });
    }
    {
        Command c;
        c.id = "gizmo.scale";
        c.title = "Gizmo: scale";
        c.category = "Edit";
        c.enabled = gizmoActive;
        c.checked = [this] {
            return gizmo_.mode() == Tools::TransformGizmo::Mode::Scale;
        };
        c.action = [this] {
            gizmo_.setMode(Tools::TransformGizmo::Mode::Scale);
        };
        add(std::move(c));
        shortcuts_.registerDefault("gizmo.scale", ShortcutBinding{ GLFW_KEY_3 });
    }
    {
        Command c;
        c.id = "gizmo.toggle_space";
        c.title = "Gizmo: local space";
        c.category = "Edit";
        c.keywords = "world coordinate";
        c.enabled = gizmoActive;
        c.checked = [services] { return services->gizmoLocalSpace(); };
        c.action = [services] {
            services->setGizmoLocalSpace(!services->gizmoLocalSpace());
        };
        add(std::move(c));
        shortcuts_.registerDefault("gizmo.toggle_space",
                                   ShortcutBinding{ GLFW_KEY_4 });
    }
    {
        Command c;
        c.id = "edit.duplicate";
        c.title = "Duplicate";
        c.category = "Edit";
        c.keywords = "copy clone";
        c.separatorBefore = true;
        c.enabled = [this] { return !selection_.empty(); };
        c.action = [this] { duplicateItems(selection_.items()); };
        add(std::move(c));
        shortcuts_.registerDefault("edit.duplicate",
                                   ShortcutBinding{ GLFW_KEY_D, true });
    }
    {
        Command c;
        c.id = "edit.group";
        c.title = "Group selection";
        c.category = "Edit";
        c.keywords = "folder organize";
        c.enabled = [this] { return !selection_.empty(); };
        c.action = [this] { groupItems(selection_.items()); };
        add(std::move(c));
        shortcuts_.registerDefault("edit.group",
                                   ShortcutBinding{ GLFW_KEY_G, true });
    }
    {
        Command c;
        c.id = "edit.ungroup";
        c.title = "Ungroup";
        c.category = "Edit";
        c.keywords = "dissolve folder";
        c.enabled = [this] { return !selection_.empty(); };
        c.action = [this] { ungroupItems(selection_.items()); };
        add(std::move(c));
        shortcuts_.registerDefault(
            "edit.ungroup",
            ShortcutBinding{ GLFW_KEY_G, true, false, true }); // Ctrl+Shift+G
    }
    {
        Command c;
        c.id = "edit.delete_selected";
        c.title = "Delete selection";
        c.category = "Edit";
        c.keywords = "remove";
        // Unbound by default (the GL app used Delete, but plugins own that
        // key here: the MeasurementPlugin's cancel). Bindable by the user;
        // the Outliner's context menu also routes here.
        c.enabled = [this] { return !selection_.empty(); };
        c.action = [this] { deleteItems(selection_.items()); };
        add(std::move(c));
    }

    // ── Select ───────────────────────────────────────────────────────────
    {
        Command c;
        c.id = "select.all";
        c.title = "Select all";
        c.category = "Select";
        c.keywords = "everything";
        c.action = [this] {
            std::vector<scene::SceneItemRef> refs;
            using Kind = scene::SceneItemRef::Kind;
            auto push = [&](Kind kind, uint64_t id, int index) {
                scene::SceneItemRef ref;
                ref.kind = kind;
                ref.id = id;
                ref.index = index;
                refs.push_back(ref);
            };
            for (size_t i = 0; i < scene_.models.size(); ++i)
                push(Kind::Model, scene_.models[i].id, int(i));
            for (size_t i = 0; i < scene_.pointClouds.size(); ++i)
                push(Kind::PointCloud, scene_.pointClouds[i].id, int(i));
            for (size_t i = 0; i < scene_.i3sLayers.size(); ++i)
                if (scene_.i3sLayers[i])
                    push(Kind::SceneLayer, scene_.i3sLayers[i]->id, int(i));
            for (size_t i = 0; i < scene_.pointLights.size(); ++i)
                push(Kind::PointLight, scene_.pointLights[i].id, int(i));
            selection_.set(std::move(refs));
        };
        add(std::move(c));
        shortcuts_.registerDefault("select.all",
                                   ShortcutBinding{ GLFW_KEY_A, true });
    }
    {
        Command c;
        c.id = "select.clear";
        c.title = "Clear selection";
        c.category = "Select";
        c.keywords = "deselect escape cancel exit tool";
        c.tooltip = "Exits the active tool first, then clears the selection.";
        // The Esc cascade (§7.1): active tool first (the measurement plugin
        // consumes Esc itself before this runs), then the selection.
        c.enabled = [this] {
            return clipPlaneTool_.isEnabled() || !selection_.empty();
        };
        c.action = [this] { escapeAction(); };
        add(std::move(c));
        shortcuts_.registerDefault("select.clear",
                                   ShortcutBinding{ GLFW_KEY_ESCAPE });
    }

    // ── View: panel toggles (checkable; state lives in Settings::Ui) ─────
    const auto panelToggle = [&](const char* id, const char* title,
                                 bool Gui::Settings::Ui::Panels::* flag) {
        Command c;
        c.id = id;
        c.title = title;
        c.category = "View";
        c.keywords = "panel window show hide toggle";
        c.checked = [this, flag] { return settings_.ui.panels.*flag; };
        c.action = [this, flag] {
            settings_.ui.panels.*flag = !(settings_.ui.panels.*flag);
        };
        add(std::move(c));
    };
    panelToggle("view.panel.scene", Gui::Windows::Scene,
                &Gui::Settings::Ui::Panels::scene);
    panelToggle("view.panel.inspector", Gui::Windows::Inspector,
                &Gui::Settings::Ui::Panels::inspector);
    panelToggle("view.panel.settings", Gui::Windows::Settings,
                &Gui::Settings::Ui::Panels::settings);
    panelToggle("view.panel.cursor", Gui::Windows::Cursor,
                &Gui::Settings::Ui::Panels::cursor);
    panelToggle("view.panel.pointclouds", Gui::Windows::PointClouds,
                &Gui::Settings::Ui::Panels::pointClouds);
    panelToggle("view.panel.diagnostics", Gui::Windows::Diagnostics,
                &Gui::Settings::Ui::Panels::diagnostics);
    panelToggle("view.panel.slpk", Gui::Windows::Slpk,
                &Gui::Settings::Ui::Panels::slpk);
    panelToggle("view.panel.history", Gui::Windows::History,
                &Gui::Settings::Ui::Panels::history);
    panelToggle("view.panel.snapshots", Gui::Windows::Snapshots,
                &Gui::Settings::Ui::Panels::snapshots);
    // ── Create: primitives (Pass 5; the Welcome Hub row and the palette run
    //    the SAME commands — one path, contract C5) ─────────────────────────
    {
        static const char* kPrimitiveNames[] = { "Cube", "Sphere", "Cylinder",
                                                 "Plane", "Torus" };
        static const char* kPrimitiveIds[] = { "create.cube", "create.sphere",
                                               "create.cylinder", "create.plane",
                                               "create.torus" };
        for (int i = 0; i < 5; ++i) {
            Command c;
            c.id = kPrimitiveIds[i];
            c.title = kPrimitiveNames[i];
            c.category = "Create";
            c.keywords = "primitive add new object mesh shape";
            c.action = [this, i] { addPrimitive(i); };
            add(std::move(c));
        }
    }
    {
        // Hold F1 = keymap cheat sheet (GuiSystem reads shortcutHeld); the
        // COMMAND fires on Shift+F1 and pins it (§14). Registering it means the
        // overlay is rebindable and lists itself.
        Command c;
        c.id = "help.shortcuts";
        c.title = "Keyboard shortcuts (hold F1)";
        c.category = "Help";
        c.keywords = "keys keymap cheat sheet bindings help overlay";
        c.checked = [this] { return guiSystem_.shortcutOverlayPinned(); };
        c.action = [this] { guiSystem_.toggleShortcutOverlayPinned(); };
        add(std::move(c));
        shortcuts_.registerDefault(
            "help.shortcuts",
            ShortcutBinding{ GLFW_KEY_F1, /*ctrl=*/false, /*alt=*/false,
                             /*shift=*/true }, // Shift+F1 pins
            ShortcutBinding{ GLFW_KEY_F1 });   // plain F1: hold-to-show
    }
    {
        // Standard views (Pass 8): registered so the viewport toolbar, the
        // palette and the F1 sheet all drive the SAME code (C5). They act on the
        // ACTIVE viewport (§5.2); the toolbar passes its own index directly.
        static const char* kViewIds[] = { "view.std.top",   "view.std.bottom",
                                          "view.std.front", "view.std.back",
                                          "view.std.right", "view.std.left",
                                          "view.std.iso" };
        static const char* kViewNames[] = { "Top view",   "Bottom view",
                                            "Front view", "Back view",
                                            "Right view", "Left view",
                                            "Isometric view" };
        for (int v = 0; v < 7; ++v) {
            Command c;
            c.id = kViewIds[v];
            c.title = kViewNames[v];
            c.category = "View";
            c.keywords = "camera standard axis orthographic view";
            c.action = [this, v] { applyStandardView(activeViewport_, v); };
            add(std::move(c));
        }
    }
    {
        // File ▸ Export… — renders from the Pass-7 exporter registry.
        Command c;
        c.id = "file.export";
        c.title = "Export...";
        c.category = "File";
        c.keywords = "export save write screenshot point cloud scene output";
        c.action = [this] { guiSystem_.openExportDialog(); };
        add(std::move(c));
    }
    {
        // The palette searches commands, objects, snapshots and recents (Pass 4).
        Command c;
        c.id = "palette.open";
        c.title = "Command palette...";
        c.category = "View";
        c.keywords = "search find command palette goto anything everything";
        c.separatorBefore = true;
        c.action = [this] { guiSystem_.openPalette(); };
        add(std::move(c));
        shortcuts_.registerDefault(
            "palette.open",
            ShortcutBinding{ GLFW_KEY_K, /*ctrl=*/true, /*alt=*/false, /*shift=*/false });
    }
    {
        Command c;
        c.id = "edit.snapshot_now";
        c.title = "Snapshot now";
        c.category = "Edit";
        c.keywords = "snapshot checkpoint capture save state camera scene";
        c.action = [this] {
            createSnapshot("", Gui::kSnapshotCamera | Gui::kSnapshotScene);
            settings_.ui.panels.snapshots = true;
        };
        add(std::move(c));
    }
    {
        Command c;
        c.id = "view.status_bar";
        c.title = "Status bar";
        c.category = "View";
        c.checked = [this] { return settings_.ui.showStatusBar; };
        c.action = [this] {
            settings_.ui.showStatusBar = !settings_.ui.showStatusBar;
        };
        add(std::move(c));
    }
    {
        Command c;
        c.id = "view.wireframe";
        c.title = "Wireframe";
        c.category = "View";
        c.keywords = "line mode debug";
        c.tooltip = "Draw all scene geometry (models + scene layers) with the "
                    "line-mode debug pipeline.";
        c.separatorBefore = true;
        c.enabled = [services] { return services->wireframeSupported(); };
        c.checked = [this] { return settings_.render.wireframe; };
        c.action = [this] {
            settings_.render.wireframe = !settings_.render.wireframe;
        };
        add(std::move(c));
    }
    {
        Command c;
        c.id = "view.toggle_gui";
        c.title = "Show GUI panels";
        c.category = "View";
        c.keywords = "hide interface hud";
        c.tooltip = "Hide the side panels — the menu bar and the 3D view "
                    "stay. (G, the GL app's key; F1 is reserved for the "
                    "upcoming shortcut overlay.)";
        c.checked = [this] { return guiSystem_.guiVisible(); };
        c.action = [this] { guiSystem_.toggleGuiVisible(); };
        add(std::move(c));
        shortcuts_.registerDefault("view.toggle_gui",
                                   ShortcutBinding{ GLFW_KEY_G });
    }
    {
        Command c;
        c.id = "view.center";
        c.title = "Center view";
        c.category = "View";
        c.keywords = "focus frame look at";
        c.separatorBefore = true;
        c.action = [this] { centerViewOnCursor(); };
        add(std::move(c));
        shortcuts_.registerDefault("view.center", ShortcutBinding{ GLFW_KEY_C });
    }
    {
        Command c;
        c.id = "view.frame_selected";
        c.title = "Frame selection";
        c.category = "View";
        c.keywords = "focus zoom fit look at";
        c.enabled = [this] { return !selection_.empty(); };
        c.action = [this] { frameItems(selection_.items()); };
        add(std::move(c));
        shortcuts_.registerDefault("view.frame_selected",
                                   ShortcutBinding{ GLFW_KEY_F });
    }
    {
        Command c;
        c.id = "view.add_viewport";
        c.title = "Add viewport";
        c.category = "View";
        c.keywords = "camera window 3d view";
        c.enabled = [services] { return services->canAddViewport(); };
        c.action = [services] { services->addViewport(); };
        add(std::move(c));
    }
    {
        Command c;
        c.id = "view.reset_layout";
        c.title = "Reset layout";
        c.category = "View";
        c.keywords = "dock default windows arrange";
        c.separatorBefore = true;
        c.action = [this] { guiSystem_.requestResetLayout(); };
        add(std::move(c));
    }

    // ── Help ─────────────────────────────────────────────────────────────
    {
        Command c;
        c.id = "help.about";
        c.title = "About StereoVista";
        c.category = "Help";
        c.checked = [this] { return guiSystem_.aboutVisible(); };
        c.action = [this] { guiSystem_.toggleAbout(); };
        add(std::move(c));
    }
}

void Application::dispatchShortcuts() {
    const ImGuiIO& io = ImGui::GetIO();
    // Collect first, run after: a command could mutate the binding table
    // (the Pass-6 editor) while we iterate it.
    std::vector<std::string> toRun;
    shortcuts_.forEach([&](const std::string& commandId,
                           const core::ShortcutBinding& binding) {
        if (binding.ctrl != io.KeyCtrl || binding.alt != io.KeyAlt ||
            binding.shift != io.KeyShift)
            return; // exact modifier match (Ctrl+Z must not fire Ctrl+Shift+Z)
        // Keys a plugin consumed this frame stay with the plugin.
        for (int consumed : pluginConsumedKeys_)
            if (consumed == binding.keyCode)
                return;
        const ImGuiKey key = imGuiKeyFromGlfw(binding.keyCode);
        if (key == ImGuiKey_None || !ImGui::IsKeyPressed(key, false))
            return;
        toRun.push_back(commandId);
    });
    for (const std::string& commandId : toRun)
        commands_.run(commandId); // no-op when the command is disabled
}

void Application::centerViewOnCursor() {
    // GL CenterView port: glide-center on the 3D cursor point (desktop only —
    // the depth pick is frozen in XR), else the selection, else the scene.
    Camera& cam = activeCamera();
    if (cam.IsAnimating)
        return;
    if (!xrRunning() && cursorManager_.isCursorPositionValid()) {
        cam.StartCenteringAnimation(cursorManager_.getCursorPosition());
        return;
    }
    if (!selection_.empty()) {
        frameItems(selection_.items());
        return;
    }
    if (scene_.worldBoundsMax.x >= scene_.worldBoundsMin.x)
        cam.StartCenteringAnimation(
            (scene_.worldBoundsMin + scene_.worldBoundsMax) * 0.5f);
}

Plugins::PickRay Application::mouseRayCurrent() const {
    return rayThroughPixel(sceneInput_.mousePx);
}

Plugins::PickRay Application::rayThroughPixel(glm::vec2 viewportPixel) const {
    glm::mat4 view(1.0f), proj(1.0f);
    cameraMatrices(view, proj);
    const glm::mat4 invViewProj = glm::inverse(proj * view);

    // ImGui reports (-FLT_MAX,-FLT_MAX) when there is no mouse this frame —
    // fall back to the viewport centre so the ray math never produces NaNs.
    if (!std::isfinite(viewportPixel.x) || !std::isfinite(viewportPixel.y))
        viewportPixel = sceneInput_.sizePx * 0.5f;

    const float u = sceneInput_.sizePx.x > 0.0f
                        ? viewportPixel.x / sceneInput_.sizePx.x : 0.0f;
    const float v = sceneInput_.sizePx.y > 0.0f
                        ? viewportPixel.y / sceneInput_.sizePx.y : 0.0f;
    // Viewport pixels are top-left origin; the Y-flip is baked into proj, so
    // ndc.y = v*2-1 matches the depth-pick reconstruction convention. Reverse-Z
    // puts the near plane at depth 1 and the far plane at 0.
    const float ndcX = u * 2.0f - 1.0f;
    const float ndcY = v * 2.0f - 1.0f;
    const glm::vec4 nearH = invViewProj * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
    const glm::vec4 farH = invViewProj * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
    const glm::vec3 origin = glm::vec3(nearH) / nearH.w;
    const glm::vec3 farPoint = glm::vec3(farH) / farH.w;

    Plugins::PickRay ray;
    ray.origin = origin;
    ray.direction = glm::normalize(farPoint - origin);
    return ray;
}

int Application::currentMods() const {
    // ImGui aggregates modifiers across the main window and every backend-
    // owned OS window (a dragged-out Viewport panel included).
    const ImGuiIO& io = ImGui::GetIO();
    int mods = 0;
    if (io.KeyShift) mods |= GLFW_MOD_SHIFT;
    if (io.KeyCtrl)  mods |= GLFW_MOD_CONTROL;
    if (io.KeyAlt)   mods |= GLFW_MOD_ALT;
    return mods;
}

// ---- Selection shims (index-based consumers over the multi-selection) ----

int Application::selectedModelIndex() const {
    const scene::SceneItemRef ref = selection_.primary();
    if (ref.kind != scene::SceneItemRef::Kind::Model &&
        ref.kind != scene::SceneItemRef::Kind::Mesh)
        return -1;
    return scene_.modelIndexOf(ref.id);
}

int Application::selectedMeshIndex() const {
    scene::SceneItemRef ref = selection_.primary();
    if (ref.kind != scene::SceneItemRef::Kind::Mesh)
        return -1;
    return scene_.resolve(ref) ? ref.sub : -1;
}

void Application::setSelectionIndices(int model, int mesh) {
    if (model < 0 || model >= int(scene_.models.size())) {
        selection_.clear();
        return;
    }
    scene::SceneItemRef ref;
    ref.id = scene_.models[model].id;
    ref.index = model;
    if (mesh >= 0 && mesh < int(scene_.models[model].meshes.size())) {
        ref.kind = scene::SceneItemRef::Kind::Mesh;
        ref.sub = mesh;
    } else {
        ref.kind = scene::SceneItemRef::Kind::Model;
    }
    selection_.selectOne(ref);
}

// ---- Transform gizmo glue (Phase 6b; multi-select since Pass 1) ----

// The transform pointers behind a ref (nullptr pos = not transformable):
// models (a Mesh ref edits its whole model — meshes have no transform of
// their own), point clouds, and point lights (translate only). Locked
// objects return not-transformable.
namespace {
struct TransformPtrs {
    glm::vec3* pos = nullptr;
    glm::vec3* rot = nullptr;
    glm::vec3* scale = nullptr;
    bool valid() const { return pos != nullptr; }
};

TransformPtrs transformPtrsFor(scene::Scene& scene, scene::SceneItemRef ref) {
    TransformPtrs out;
    if (!scene.resolve(ref))
        return out;
    using Kind = scene::SceneItemRef::Kind;
    if (ref.kind == Kind::Model || ref.kind == Kind::Mesh) {
        scene::Model& m = scene.models[ref.index];
        if (m.locked || scene.groupChainLocked(m.groupId))
            return out;
        out.pos = &m.position;
        out.rot = &m.rotationDeg;
        out.scale = &m.scale;
    } else if (ref.kind == Kind::PointCloud) {
        Engine::PointCloud& pc = scene.pointClouds[ref.index];
        if (pc.locked || scene.groupChainLocked(pc.groupId))
            return out;
        out.pos = &pc.position;
        out.rot = &pc.rotation;
        out.scale = &pc.scale;
    } else if (ref.kind == Kind::PointLight) {
        scene::PointLight& l = scene.pointLights[ref.index];
        if (l.locked || scene.groupChainLocked(l.groupId))
            return out;
        out.pos = &l.position; // translate only
    }
    return out;
}
} // namespace

void Application::bindGizmoToSelection() {
    // The gizmo binds to the PRIMARY selected transformable.
    const scene::SceneItemRef ref = selection_.primary();
    const TransformPtrs t = transformPtrsFor(scene_, ref);
    if (t.valid())
        gizmo_.setTarget(t.pos, t.rot, t.scale);
    else
        gizmo_.clearTarget();
}

void Application::applyGizmoDeltaToSelection() {
    // Entry 0 of the drag snapshot is the primary (the object the gizmo edits
    // in place); mirror its delta onto every other snapshot entry. Rotation
    // and scale apply about each object's OWN pivot — simple, predictable,
    // and exactly right for translate (the dominant multi-select edit).
    if (gizmoUndo_.size() < 2)
        return;
    const TransformPtrs primary = transformPtrsFor(scene_, gizmoUndo_[0].ref);
    if (!primary.valid())
        return;
    const glm::vec3 dPos = *primary.pos - gizmoUndo_[0].pos;
    const glm::vec3 dRot =
        primary.rot ? (*primary.rot - gizmoUndo_[0].rot) : glm::vec3(0.0f);
    glm::vec3 scaleRatio(1.0f);
    if (primary.scale) {
        for (int a = 0; a < 3; ++a) {
            const float before = gizmoUndo_[0].scale[a];
            if (std::abs(before) > 1e-8f)
                scaleRatio[a] = (*primary.scale)[a] / before;
        }
    }
    for (size_t i = 1; i < gizmoUndo_.size(); ++i) {
        const TransformPtrs t = transformPtrsFor(scene_, gizmoUndo_[i].ref);
        if (!t.valid())
            continue;
        *t.pos = gizmoUndo_[i].pos + dPos;
        if (t.rot)
            *t.rot = gizmoUndo_[i].rot + dRot;
        if (t.scale)
            *t.scale = gizmoUndo_[i].scale * scaleRatio;
    }
}

void Application::updateGizmoBinding() {
    // The clip tool's active plane takes the gizmo while it's editing; otherwise
    // the gizmo edits the selected model.
    if (clipPlaneTool_.isEnabled() && clipPlaneTool_.hasActivePlane()) {
        clipPlaneTool_.bindGizmo(gizmo_);
        gizmoTargetPlane_ = true;
    } else {
        bindGizmoToSelection();
        gizmoTargetPlane_ = false;
    }
}

void Application::beginGizmoUndo() {
    // Snapshot EVERY selected transformable at drag start; entry 0 is the
    // primary (the object the gizmo edits directly).
    gizmoUndo_.clear();
    auto capture = [&](scene::SceneItemRef ref) {
        // A mesh selection edits its whole model's transform — normalize so a
        // model and one of its meshes dedupe to one snapshot entry.
        if (ref.kind == scene::SceneItemRef::Kind::Mesh) {
            ref.kind = scene::SceneItemRef::Kind::Model;
            ref.sub = -1;
        }
        for (const GizmoSnapshot& snap : gizmoUndo_)
            if (snap.ref == ref)
                return;
        const TransformPtrs t = transformPtrsFor(scene_, ref);
        if (!t.valid())
            return;
        GizmoSnapshot snap;
        snap.ref = ref;
        snap.pos = *t.pos;
        if (t.rot) snap.rot = *t.rot;
        if (t.scale) snap.scale = *t.scale;
        gizmoUndo_.push_back(snap);
    };
    capture(selection_.primary());
    for (const scene::SceneItemRef& ref : selection_.items())
        capture(ref);
}

void Application::finishGizmoUndo() {
    // Whole drag = ONE undo entry covering every selected transformable
    // (contract C4); records resolve by ObjectId so later adds/deletes can't
    // corrupt the replay (C3).
    if (gizmoUndo_.empty())
        return;
    struct Delta {
        scene::SceneItemRef ref;
        glm::vec3 beforePos, beforeRot, beforeScale;
        glm::vec3 afterPos, afterRot, afterScale;
    };
    std::vector<Delta> deltas;
    for (const GizmoSnapshot& snap : gizmoUndo_) {
        const TransformPtrs t = transformPtrsFor(scene_, snap.ref);
        if (!t.valid())
            continue;
        Delta d;
        d.ref = snap.ref;
        d.beforePos = snap.pos;
        d.beforeRot = snap.rot;
        d.beforeScale = snap.scale;
        d.afterPos = *t.pos;
        d.afterRot = t.rot ? *t.rot : snap.rot;
        d.afterScale = t.scale ? *t.scale : snap.scale;
        if (d.beforePos != d.afterPos || d.beforeRot != d.afterRot ||
            d.beforeScale != d.afterScale)
            deltas.push_back(d);
    }
    gizmoUndo_.clear();
    if (deltas.empty())
        return; // a click with no drag — nothing changed, nothing to record

    const std::string label =
        deltas.size() == 1
            ? "Transform object"
            : "Transform " + std::to_string(deltas.size()) + " objects";
    auto apply = [this](const std::vector<Delta>& all, bool before) {
        for (const Delta& d : all) {
            const TransformPtrs t = transformPtrsFor(scene_, d.ref);
            if (!t.valid())
                continue;
            *t.pos = before ? d.beforePos : d.afterPos;
            if (t.rot) *t.rot = before ? d.beforeRot : d.afterRot;
            if (t.scale) *t.scale = before ? d.beforeScale : d.afterScale;
        }
        scene_.computeWorldBounds();
    };
    undo_.record(
        label, [apply, deltas]() { apply(deltas, true); },
        [apply, deltas]() { apply(deltas, false); });
}

} // namespace app
