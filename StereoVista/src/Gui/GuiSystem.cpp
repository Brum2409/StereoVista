#include "Gui/GuiSystem.h"

#include "Gui/Panels.h"
#include "Gui/Services.h"
#include "Plugins/PluginContext.h"
#include "Plugins/PluginManager.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h" // DockBuilder* / DockBuilderGetNode

namespace Gui {

namespace {

// A "?" hint that shows help text on hover — used to mark controls whose
// backing feature is not ported yet without cluttering the menu.
void menuHint(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

} // namespace

void GuiSystem::draw(Services& services) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Menu bar first — BeginMainMenuBar reserves the top of the viewport work
    // area, so the dockspace below fits under it.
    drawMenuBar(services);

    // Dockspace over the remaining work area. The 3D scene lives in the
    // Viewport window docked into the central node. PassthruCentralNode keeps
    // an EMPTY central node transparent and click-through — the fallback for
    // the classic fullscreen path (XR mirror), where the scene still renders
    // under the GUI on the backbuffer.
    const ImGuiID dockId = ImGui::GetID("StereoVistaDockspace");
    const bool freshProfile = (ImGui::DockBuilderGetNode(dockId) == nullptr);
    ImGui::DockSpaceOverViewport(dockId, viewport,
                                 ImGuiDockNodeFlags_PassthruCentralNode);

    // Seed a default layout on a fresh profile (no imgui.ini docking yet) or on
    // an explicit reset; a layout restored from imgui.ini is left untouched.
    if (resetLayout_ || (freshProfile && !layoutInitialized_)) {
        buildDefaultLayout(dockId, viewport->WorkSize.x, viewport->WorkSize.y);
        resetLayout_ = false;
    }
    layoutInitialized_ = true;

    // The 3D viewport windows. Drawn BEFORE the panels so same-frame consumers
    // (the scene-layer hover pick) see fresh input state, and NOT gated by the
    // F1 master toggle — hiding the side panels collapses their dock nodes and
    // the viewports grow to (nearly) the whole window. Skipped on the classic
    // fullscreen path, where the scene shows through the passthru node.
    const unsigned int viewportCount = services.viewportPanelCount();
    if (services.viewportDisplay(0).active) {
        for (unsigned int i = 0; i < viewportCount; ++i)
            drawViewportPanel(services, i);
    } else {
        for (unsigned int i = 0; i < viewportCount; ++i)
            services.onViewportPanel(i, ViewportPanelState{});
    }

    // The About window and plugin windows are gated by the master toggle too.
    if (guiVisible_) {
        if (showScene_)       drawScenePanel(services, &showScene_);
        if (showInspector_)   drawInspectorPanel(services, &showInspector_);
        if (showSettings_)    drawSettingsPanel(services, &showSettings_);
        if (showCursor_)      drawCursorPanel(services, &showCursor_);
        if (showPointClouds_) drawPointCloudPanel(services, &showPointClouds_);
        if (showClipPlanes_)  drawClipPlanePanel(services, &showClipPlanes_);
        if (showDiagnostics_) drawDiagnosticsPanel(services, &showDiagnostics_);
        if (showSlpk_)        drawSlpkPanel(services, &showSlpk_);
        if (showAbout_)       drawAboutWindow(services);

        // Plugin (tool) windows render inside the same ImGui frame.
        services.plugins().renderUI(services.pluginContext());
    }
}

void GuiSystem::drawMenuBar(Services& services) {
    if (!ImGui::BeginMainMenuBar())
        return;

    if (ImGui::BeginMenu("File")) {
        if (ImGui::MenuItem("Import Model..."))
            services.importModelDialog();
        if (ImGui::MenuItem("Import Point Cloud..."))
            services.openPointCloudDialog();
        if (ImGui::MenuItem("Open Scene Layer (.slpk)..."))
            services.openSlpkDialog();

        ImGui::Separator();
        // Scene load/save/merge is gated on the not-yet-ported SceneManager.
        const bool sceneOps = services.sceneSaveAvailable();
        ImGui::BeginDisabled(!sceneOps);
        ImGui::MenuItem("Open Scene...");
        ImGui::MenuItem("Save Scene...");
        ImGui::MenuItem("Merge Scene...");
        ImGui::EndDisabled();
        if (!sceneOps)
            menuHint("Scene load / save / merge returns with the SceneManager "
                     "port (docs/TODO.md E).");

        ImGui::Separator();
        ImGui::BeginDisabled(services.screenshotPending());
        if (ImGui::MenuItem("Save Screenshot"))
            services.requestScreenshot();
        ImGui::EndDisabled();

        ImGui::Separator();
        if (ImGui::MenuItem("Exit"))
            services.requestQuit();
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("View")) {
        ImGui::MenuItem(Windows::Scene, nullptr, &showScene_);
        ImGui::MenuItem(Windows::Inspector, nullptr, &showInspector_);
        ImGui::MenuItem(Windows::Settings, nullptr, &showSettings_);
        ImGui::MenuItem(Windows::Cursor, nullptr, &showCursor_);
        ImGui::MenuItem(Windows::PointClouds, nullptr, &showPointClouds_);
        ImGui::MenuItem(Windows::ClipPlanes, nullptr, &showClipPlanes_);
        ImGui::MenuItem(Windows::Diagnostics, nullptr, &showDiagnostics_);
        ImGui::MenuItem(Windows::Slpk, nullptr, &showSlpk_);

        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            const int current = services.currentTheme();
            for (int i = 0; i < services.themeCount(); ++i) {
                if (ImGui::MenuItem(services.themeName(i), nullptr, current == i))
                    services.setTheme(i);
            }
            ImGui::EndMenu();
        }
        {
            bool wireframe = services.settings().render.wireframe;
            if (ImGui::MenuItem("Wireframe", nullptr, &wireframe))
                services.settings().render.wireframe = wireframe;
            menuHint("Stored for later — the Vulkan forward pass has no polygon-"
                     "mode toggle yet (docs/TODO.md).");
        }
        ImGui::MenuItem("Show GUI panels", "F1", &guiVisible_);

        ImGui::Separator();
        // Extra 3D viewports, each with its own camera (stereo-capable like
        // the primary). New ones float — dock them wherever they belong.
        ImGui::BeginDisabled(!services.canAddViewport());
        if (ImGui::MenuItem("Add Viewport"))
            services.addViewport();
        ImGui::EndDisabled();
        if (!services.canAddViewport())
            menuHint("Maximum number of viewports reached.");

        ImGui::Separator();
        if (ImGui::MenuItem("Reset Layout"))
            resetLayout_ = true;
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        // Plugin (tool) menu entries; each plugin appends its own item(s).
        services.plugins().renderMenu(services.pluginContext());
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        ImGui::MenuItem("About StereoVista", nullptr, &showAbout_);
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void GuiSystem::drawViewportPanel(Services& services, unsigned int index) {
    const ViewportDisplay display = services.viewportDisplay(index);
    ViewportPanelState state{};

    // Zero padding: the scene image IS the window. The PRIMARY viewport has
    // no close button (it is the application content, not an optional panel);
    // secondary viewports close back into it.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));
    if (index == 0) {
        ImGui::SetNextWindowSize(ImVec2(1280.0f, 720.0f), ImGuiCond_FirstUseEver);
    } else {
        // Extra viewports first appear floating (offset per index so they
        // don't stack exactly); the user docks them wherever they belong.
        const ImGuiViewport* main = ImGui::GetMainViewport();
        ImGui::SetNextWindowSize(ImVec2(960.0f, 540.0f), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowPos(ImVec2(main->WorkPos.x + 120.0f + 40.0f * index,
                                       main->WorkPos.y + 120.0f + 40.0f * index),
                                ImGuiCond_FirstUseEver);
    }
    bool keepOpen = true;
    const bool open = ImGui::Begin(services.viewportPanelName(index),
                                   index > 0 ? &keepOpen : nullptr,
                                   ImGuiWindowFlags_NoScrollbar |
                                       ImGuiWindowFlags_NoScrollWithMouse |
                                       ImGuiWindowFlags_NoCollapse);
    if (open && display.textureId && display.width > 0 && display.height > 0) {
        const ImVec2 avail = ImGui::GetContentRegionAvail();
        if (avail.x >= 1.0f && avail.y >= 1.0f) {
            const ImVec2 imagePos = ImGui::GetCursorScreenPos();
            // Stretch the current texture into the whole content region. While
            // a resize drag is in flight this shows the old size stretched for
            // a frame or two, until the size settles and the app rebuilds the
            // render target at the new resolution.
            ImGui::Image(reinterpret_cast<ImTextureID>(display.textureId), avail);

            state.shown = true;
            state.sizeX = avail.x;
            state.sizeY = avail.y;
            state.screenX = imagePos.x;
            state.screenY = imagePos.y;
            state.screenW = avail.x;
            state.screenH = avail.y;
            // Mouse in TEXTURE pixels, scaled through the display rect so
            // picking stays aligned with what is actually rendered even while
            // the displayed image is a stretched old size.
            const ImVec2 mouse = ImGui::GetIO().MousePos;
            state.mouseX = (mouse.x - imagePos.x) * (float(display.width) / avail.x);
            state.mouseY = (mouse.y - imagePos.y) * (float(display.height) / avail.y);
            state.hovered = ImGui::IsItemHovered();
            state.focused = ImGui::IsWindowFocused();
            // The GLFW window hosting this panel (the main window, or the
            // backend-owned OS window when the panel is dragged out) — camera
            // capture and OS-cursor show/hide must target it.
            state.hostWindow = ImGui::GetWindowViewport()->PlatformHandle;
        }
    }
    ImGui::End();
    ImGui::PopStyleVar();
    services.onViewportPanel(index, state);
    if (!keepOpen)
        services.closeViewport(index);
}

void GuiSystem::drawAboutWindow(Services& services) {
    ImGui::SetNextWindowSize(ImVec2(420.0f, 0.0f), ImGuiCond_FirstUseEver);
    if (ImGui::Begin("About StereoVista", &showAbout_,
                     ImGuiWindowFlags_NoDocking)) {
        const FrameDiagnostics diag = services.diagnostics();
        ImGui::TextUnformatted("StereoVista");
        ImGui::TextDisabled("Native Vulkan 1.3 stereo 3D visualization");
        ImGui::Separator();
        ImGui::Text("GPU: %s", diag.gpuName);
        ImGui::Text("Vulkan %u.%u.%u", diag.apiMajor, diag.apiMinor, diag.apiPatch);
        ImGui::Text("Validation layers: %s", diag.validation ? "on" : "off");
        ImGui::Spacing();
        ImGui::TextWrapped("Physically based forward rendering, shadow mapping, a "
                           "compute point-cloud rasterizer, and single-pass "
                           "multiview stereo (quad-buffer / side-by-side / OpenXR).");
    }
    ImGui::End();
}

void GuiSystem::buildDefaultLayout(unsigned int dockspaceId, float sizeX,
                                   float sizeY) {
    const ImGuiID dockId = static_cast<ImGuiID>(dockspaceId);

    ImGui::DockBuilderRemoveNode(dockId); // clear any previous layout
    ImGui::DockBuilderAddNode(dockId, ImGuiDockNodeFlags_DockSpace);
    ImGui::DockBuilderSetNodeSize(dockId, ImVec2(sizeX, sizeY));

    // Reserve a left column (hierarchy over inspector) and a right column
    // (tabbed settings/cursor/clouds/clip over a diagnostics strip); the
    // remaining centre hosts the Viewport window (or stays the passthru scene
    // view on the classic fullscreen path).
    ImGuiID center = dockId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f,
                                               nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f,
                                                nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f,
                                                     nullptr, &left);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.35f,
                                                      nullptr, &right);

    // The viewport owns the central node; panels dock around it.
    ImGui::DockBuilderDockWindow(Windows::Viewport, center);
    ImGui::DockBuilderDockWindow(Windows::Scene, left);
    ImGui::DockBuilderDockWindow(Windows::Inspector, leftBottom);
    ImGui::DockBuilderDockWindow(Windows::Settings, right);
    ImGui::DockBuilderDockWindow(Windows::Cursor, right);
    ImGui::DockBuilderDockWindow(Windows::PointClouds, right);
    ImGui::DockBuilderDockWindow(Windows::ClipPlanes, right);
    ImGui::DockBuilderDockWindow(Windows::Slpk, right);
    ImGui::DockBuilderDockWindow(Windows::Diagnostics, rightBottom);

    ImGui::DockBuilderFinish(dockId);
}

} // namespace Gui
