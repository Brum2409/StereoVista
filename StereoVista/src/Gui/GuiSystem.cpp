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

    // Dockspace over the remaining work area. PassthruCentralNode keeps the
    // central (undocked) region transparent and click-through, so the 3D scene
    // is visible and camera navigation still works there.
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
    // remaining centre stays the passthru scene view.
    ImGuiID center = dockId;
    ImGuiID left = ImGui::DockBuilderSplitNode(center, ImGuiDir_Left, 0.20f,
                                               nullptr, &center);
    ImGuiID right = ImGui::DockBuilderSplitNode(center, ImGuiDir_Right, 0.26f,
                                                nullptr, &center);
    ImGuiID leftBottom = ImGui::DockBuilderSplitNode(left, ImGuiDir_Down, 0.45f,
                                                     nullptr, &left);
    ImGuiID rightBottom = ImGui::DockBuilderSplitNode(right, ImGuiDir_Down, 0.35f,
                                                      nullptr, &right);

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
