#include "Gui/GuiSystem.h"

#include "Core/CommandRegistry.h"
#include "Core/Shortcuts.h"
#include "Gui/Panels.h"
#include "Gui/Services.h"
#include "Gui/UiKit.h"
#include "Plugins/PluginContext.h"
#include "Plugins/PluginManager.h"
#include "Scene/Scene.h"

#include "imgui/imgui.h"
#include "imgui/imgui_internal.h" // DockBuilder* / BeginViewportSideBar

#include <cstdio>
#include <string>

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

// One registry command as a menu item: title from the registration, shortcut
// label live from the ShortcutMap, checkmark/enabled from the command's
// closures — and the click runs through CommandRegistry::run (contract C5).
void commandMenuItem(Services& services, const core::Command& command) {
    if (command.separatorBefore)
        ImGui::Separator();
    core::CommandRegistry& commands = services.commands();
    const std::string shortcut = services.shortcuts().label(command.id);
    const bool enabled = commands.isEnabled(command);
    const bool checked = command.checked && command.checked();
    if (ImGui::MenuItem(command.title.c_str(),
                        shortcut.empty() ? nullptr : shortcut.c_str(), checked,
                        enabled))
        commands.run(command.id);
    if (!command.tooltip.empty() &&
        ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", command.tooltip.c_str());
}

void commandMenuItem(Services& services, const char* id) {
    if (const core::Command* command = services.commands().find(id))
        commandMenuItem(services, *command);
}

// Render every command of a category in registration order — the generic
// path for menus without hand-curated dynamic content.
void commandMenu(Services& services, const char* category) {
    services.commands().forEachInCategory(
        category,
        [&](const core::Command& command) { commandMenuItem(services, command); });
}

// "12,4M" style compact count for the status bar.
std::string compactCount(unsigned long long n) {
    char buf[32];
    if (n >= 1000000000ull)
        std::snprintf(buf, sizeof(buf), "%.1fB", double(n) / 1e9);
    else if (n >= 1000000ull)
        std::snprintf(buf, sizeof(buf), "%.1fM", double(n) / 1e6);
    else if (n >= 10000ull)
        std::snprintf(buf, sizeof(buf), "%.0fk", double(n) / 1e3);
    else
        std::snprintf(buf, sizeof(buf), "%llu", n);
    return buf;
}

} // namespace

void GuiSystem::draw(Services& services) {
    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // UiKit reads the motion preference once per frame (§15 reduceMotion).
    UiKit::SetReduceMotion(services.settings().ui.reduceMotion);

    // Menu bar, then status bar — both reserve their strip of the viewport
    // work area, so the dockspace below fits between them.
    drawMenuBar(services);
    drawStatusBar(services);

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
    // master GUI toggle — hiding the side panels collapses their dock nodes and
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

    // Panels (visibility lives in Settings::Ui::Panels so the View menu, the
    // commands and preferences persistence all share one source of truth),
    // the About window and the plugin windows — all gated by the master toggle.
    if (guiVisible_) {
        Settings::Ui::Panels& panels = services.settings().ui.panels;
        if (panels.scene)       drawScenePanel(services, &panels.scene);
        if (panels.inspector)   drawInspectorPanel(services, &panels.inspector);
        if (panels.settings)    drawSettingsPanel(services, &panels.settings);
        if (panels.cursor)      drawCursorPanel(services, &panels.cursor);
        if (panels.pointClouds) drawPointCloudPanel(services, &panels.pointClouds);
        if (panels.clipPlanes)  drawClipPlanePanel(services, &panels.clipPlanes);
        if (panels.diagnostics) drawDiagnosticsPanel(services, &panels.diagnostics);
        if (panels.slpk)        drawSlpkPanel(services, &panels.slpk);
        if (showAbout_)         drawAboutWindow(services);

        // Plugin (tool) windows render inside the same ImGui frame.
        services.plugins().renderUI(services.pluginContext());
    }
}

void GuiSystem::drawMenuBar(Services& services) {
    if (!ImGui::BeginMainMenuBar())
        return;

    // File and Edit render generically from the registry (registration order
    // + separatorBefore define the grouping).
    if (ImGui::BeginMenu("File")) {
        commandMenu(services, "File");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Edit")) {
        commandMenu(services, "Edit");
        ImGui::EndMenu();
    }

    // View interleaves registry commands with dynamic content (the theme
    // picker and the viewport lifecycle) — every actionable item is still a
    // command.
    if (ImGui::BeginMenu("View")) {
        commandMenuItem(services, "view.panel.scene");
        commandMenuItem(services, "view.panel.inspector");
        commandMenuItem(services, "view.panel.settings");
        commandMenuItem(services, "view.panel.cursor");
        commandMenuItem(services, "view.panel.pointclouds");
        commandMenuItem(services, "view.panel.clipplanes");
        commandMenuItem(services, "view.panel.diagnostics");
        commandMenuItem(services, "view.panel.slpk");
        commandMenuItem(services, "view.status_bar");

        ImGui::Separator();
        if (ImGui::BeginMenu("Theme")) {
            const int current = services.currentTheme();
            for (int i = 0; i < services.themeCount(); ++i) {
                if (ImGui::MenuItem(services.themeName(i), nullptr, current == i))
                    services.setTheme(i);
            }
            ImGui::EndMenu();
        }
        commandMenuItem(services, "view.wireframe");
        commandMenuItem(services, "view.toggle_gui");

        ImGui::Separator();
        commandMenuItem(services, "view.center");
        // Extra 3D viewports, each with its own camera (stereo-capable like
        // the primary). New ones float — dock them wherever they belong.
        commandMenuItem(services, "view.add_viewport");
        if (!services.canAddViewport())
            menuHint("Maximum number of viewports reached.");

        ImGui::Separator();
        commandMenuItem(services, "view.reset_layout");
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Tools")) {
        // Registry commands first (empty today; the Pass-7 ToolManager fills
        // this), then each plugin appends its own item(s).
        commandMenu(services, "Tools");
        services.plugins().renderMenu(services.pluginContext());
        ImGui::EndMenu();
    }

    if (ImGui::BeginMenu("Help")) {
        commandMenu(services, "Help");
        ImGui::EndMenu();
    }

    ImGui::EndMainMenuBar();
}

void GuiSystem::drawStatusBar(Services& services) {
    // Window chrome, not a panel: a thin strip pinned to the bottom of the
    // main window (BeginViewportSideBar shrinks the work area, so the
    // dockspace and the passthru scene view sit above it). Hidden with the
    // master GUI toggle and by its own View-menu command.
    if (!guiVisible_ || !services.settings().ui.showStatusBar)
        return;

    ImGuiViewport* viewport = ImGui::GetMainViewport();
    const float height = ImGui::GetFrameHeight();
    const ImGuiWindowFlags flags =
        ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings;
    if (ImGui::BeginViewportSideBar("##StatusBar", viewport, ImGuiDir_Down,
                                    height, flags)) {
        const scene::Scene& scene = services.scene();

        // ── Scene stats ────────────────────────────────────────────────────
        const size_t models = scene.models.size();
        const size_t clouds = services.pointCloudCount();
        const size_t layers = scene.i3sLayers.size();
        unsigned long long points = 0;
        bool streaming = false;
        for (size_t i = 0; i < clouds; ++i) {
            points += services.pointCloudPoints(i);
            streaming = streaming || services.pointCloudProgress(i).active;
        }

        ImGui::AlignTextToFramePadding();
        if (models == 0 && clouds == 0 && layers == 0) {
            ImGui::TextDisabled("Empty scene - File > Import to begin");
        } else {
            std::string stats = std::to_string(models) +
                                (models == 1 ? " model" : " models");
            if (clouds > 0)
                stats += "  ·  " + std::to_string(clouds) +
                         (clouds == 1 ? " cloud (" : " clouds (") +
                         compactCount(points) + " pts)";
            if (layers > 0)
                stats += "  ·  " + std::to_string(layers) +
                         (layers == 1 ? " layer" : " layers");
            ImGui::TextUnformatted(stats.c_str());
        }

        // ── Selection summary ──────────────────────────────────────────────
        const int selModel = services.selectedModel();
        if (selModel >= 0 && selModel < static_cast<int>(scene.models.size())) {
            ImGui::SameLine(0.0f, UiKit::Space(7));
            const UiKit::KindStyle style =
                UiKit::StyleFor(services.selectedMesh() >= 0
                                    ? UiKit::ObjectKind::Mesh
                                    : UiKit::ObjectKind::Model);
            UiKit::InlineIcon(style.icon, style.color);
            const scene::Model& model = scene.models[selModel];
            std::string name = model.name.empty()
                                   ? "model " + std::to_string(selModel)
                                   : model.name;
            const int selMesh = services.selectedMesh();
            if (selMesh >= 0 && selMesh < static_cast<int>(model.meshes.size())) {
                const std::string& meshName = model.meshes[selMesh].name;
                name += " / " + (meshName.empty()
                                     ? "mesh " + std::to_string(selMesh)
                                     : meshName);
            }
            ImGui::TextUnformatted(name.c_str());
        }

        // ── Background activity ────────────────────────────────────────────
        if (streaming || services.slpkLoadsInFlight() > 0) {
            ImGui::SameLine(0.0f, UiKit::Space(7));
            ImGui::TextColored(UiKit::Color(UiKit::Semantic::Info), "%s",
                               streaming ? "Streaming points..."
                                         : "Opening scene layer...");
        }

        // ── Right block: FPS (click -> Performance panel) + reserved slot ──
        const FrameDiagnostics diag = services.diagnostics();
        char fpsText[48];
        std::snprintf(fpsText, sizeof(fpsText), "%.0f fps", double(diag.fps));
        // Far-right slot reserved for presence/sync (§16) — keep the gap.
        const float reserved = UiKit::Space(7);
        const float fpsWidth = ImGui::CalcTextSize(fpsText).x;
        ImGui::SameLine(ImGui::GetCursorPosX() +
                        ImGui::GetContentRegionAvail().x - fpsWidth - reserved);
        const UiKit::Semantic level = diag.fps >= 50.0f
                                          ? UiKit::Semantic::Success
                                          : (diag.fps >= 25.0f
                                                 ? UiKit::Semantic::Warning
                                                 : UiKit::Semantic::Danger);
        ImGui::TextColored(UiKit::Color(level), "%s", fpsText);
        if (ImGui::IsItemHovered()) {
            ImGui::SetTooltip("%.2f ms  ·  click for the Performance panel",
                              double(diag.frameMs));
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left))
                services.commands().run("view.panel.diagnostics");
        }
    }
    ImGui::End();
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
