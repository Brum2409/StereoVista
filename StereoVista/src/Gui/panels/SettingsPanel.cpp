#include "Gui/Panels.h"
#include "Gui/Services.h"
#include "Gui/UiKit.h"

#include "Engine/XRRuntimeInfo.h" // Engine::XRDiagnostics / XRRuntimeInfo (plain data)
#include "Scene/Scene.h"          // scene::PointLight editing

#include "imgui/imgui.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace Gui {
namespace {

void helpMarker(const char* text) {
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", text);
}

// ── Camera & navigation ─────────────────────────────────────────────────────
void drawCameraTab(Services& services) {
    Settings::Camera& c = services.settings().camera;
    ImGui::SeparatorText("Projection");
    ImGui::SliderFloat("FOV", &c.fovDeg, 30.0f, 100.0f, "%.0f deg");
    ImGui::DragFloat("Near plane", &c.nearPlane, 0.005f, 0.01f, 10.0f, "%.3f");
    ImGui::DragFloat("Far plane", &c.farPlane, 1.0f, 5.0f, 5000.0f, "%.0f");
    if (c.farPlane < c.nearPlane * 2.0f)
        c.farPlane = c.nearPlane * 2.0f;

    ImGui::SeparatorText("Navigation");
    ImGui::BeginDisabled(c.adaptiveSpeed); // adaptive mode derives the speed itself
    ImGui::SliderFloat("Fly speed", &c.speed, 0.5f, 30.0f, "%.1f");
    ImGui::EndDisabled();
    ImGui::SliderFloat("Look sensitivity", &c.sensitivity, 0.01f, 0.5f, "%.3f");
    ImGui::Checkbox("Zoom to cursor", &c.zoomToCursor);
    ImGui::SameLine();
    ImGui::Checkbox("Orbit around cursor", &c.orbitAroundCursor);

    ImGui::SeparatorText("Adaptive speed");
    ImGui::Checkbox("Distance-adaptive speed", &c.adaptiveSpeed);
    helpMarker("Scales fly/zoom speed by the distance to the surface at the "
               "screen centre (async depth feed, no stall): fast in open space, "
               "gentle near geometry. Off = the fixed fly speed above.");
    ImGui::SliderFloat("Speed factor", &c.speedFactor, 0.1f, 5.0f, "%.2f");
    helpMarker("Multiplies the fly speed (both modes) and the scroll zoom.");

    ImGui::SeparatorText("Scrolling");
    ImGui::Checkbox("Smooth scrolling", &c.useSmoothScrolling);
    if (c.useSmoothScrolling) {
        ImGui::SliderFloat("Momentum", &c.scrollMomentum, 0.05f, 2.0f, "%.2f");
        ImGui::SliderFloat("Deceleration", &c.scrollDeceleration, 1.0f, 20.0f, "%.1f");
        ImGui::SliderFloat("Max velocity", &c.maxScrollVelocity, 0.5f, 10.0f, "%.1f");
    }
}

// ── Stereo ───────────────────────────────────────────────────────────────────
void drawStereoTab(Services& services) {
    Settings::Stereo& st = services.settings().stereo;

    // Mode changes rebuild the swapchain/renderer; disabled while VR owns the
    // window (the desktop is forced mono then).
    ImGui::BeginDisabled(services.xrRunning());
    const char* modeNames[] = { "Off (mono)", "Quad-buffer 3D", "Side-by-side" };
    int mode = services.stereoMode();
    if (ImGui::Combo("Stereo mode", &mode, modeNames, 3))
        services.requestStereoMode(mode);

    if (mode != 0) {
        ImGui::SliderFloat("Eye separation", &st.separation, 0.0f, 2.0f, "%.3f");
        ImGui::BeginDisabled(st.autoConvergence);
        ImGui::SliderFloat("Convergence", &st.convergence, 0.1f, 40.0f, "%.2f");
        ImGui::EndDisabled();
        ImGui::Checkbox("Auto convergence", &st.autoConvergence);
        helpMarker("Eases the zero-parallax plane toward the scene depth at the "
                   "screen centre (from the async depth pick, no stall).");
        if (st.autoConvergence) {
            ImGui::SliderFloat("Focus factor", &st.convergenceFactor, 0.1f, 3.0f, "%.2f");
            ImGui::SliderFloat("Smoothing", &st.convergenceSmoothing, 0.5f, 20.0f, "%.1f");
        }
        ImGui::Checkbox("Flip eyes", &st.flipEyes);
    }
    ImGui::EndDisabled();

    ImGui::Separator();
    ImGui::TextDisabled("Quad-buffer present: %s (swapchain layers %u)",
                        services.stereoPresentSupported() ? "available"
                                                          : "unavailable",
                        services.swapchainLayers());
}

// ── VR / OpenXR ──────────────────────────────────────────────────────────────
void drawVrTab(Services& services) {
    Settings::Vr& vr = services.settings().vr;
    const bool running = services.xrRunning();

    bool enabled = services.vrEnabled();
    if (ImGui::Checkbox("Enable VR (OpenXR)", &enabled)) {
        services.setVrEnabled(enabled);
        if (enabled && !services.xrDiagnosticsValid())
            services.refreshXRDiagnostics();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(running ? "(active)" : "(off)");

    if (running) {
        ImGui::Text("Runtime: %s", services.xrRuntimeName().c_str());
        ImGui::Text("Eye buffer: %u x %u", services.xrEyeWidth(),
                    services.xrEyeHeight());
        ImGui::Checkbox("Mirror left eye to window", &vr.mirrorToWindow);
    }
    const std::string status = services.vrStatus();
    if (!status.empty())
        ImGui::TextWrapped("%s", status.c_str());

    ImGui::SeparatorText("Comfort");
    ImGui::SliderFloat("World scale (m per unit)", &vr.worldScale, 0.01f, 10.0f,
                       "%.3f", ImGuiSliderFlags_Logarithmic);
    ImGui::Checkbox("VR uses desktop near/far", &vr.useScenePlanes);
    if (!vr.useScenePlanes) {
        ImGui::SliderFloat("VR near", &vr.nearPlane, 0.01f, 1.0f, "%.3f");
        ImGui::SliderFloat("VR far", &vr.farPlane, 5.0f, 1000.0f, "%.0f");
    }

    // Per-process runtime override (no admin, nothing system-wide). Only affects
    // a session started afterwards, so it is disabled while one is live.
    if (ImGui::TreeNode("OpenXR runtime")) {
        if (!services.xrDiagnosticsValid())
            services.refreshXRDiagnostics();
        if (ImGui::SmallButton("Re-scan"))
            services.refreshXRDiagnostics();
        const Engine::XRDiagnostics& diag = services.xrDiagnostics();
        ImGui::SameLine();
        ImGui::TextDisabled("loader: %s", diag.loaderPresent ? "present" : "MISSING");
        if (!diag.activeRuntimeName.empty())
            ImGui::TextWrapped("Active: %s (via %s)", diag.activeRuntimeName.c_str(),
                               diag.activeRuntimeSource.c_str());

        ImGui::BeginDisabled(running);
        const std::string activeOverride = services.xrRuntimeOverride();
        if (ImGui::RadioButton("System default", activeOverride.empty()) &&
            !activeOverride.empty())
            services.setXRRuntimeOverride("");
        if (!diag.systemDefaultName.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("(%s)", diag.systemDefaultName.c_str());
        }
        for (const Engine::XRRuntimeInfo& rt : diag.runtimes) {
            ImGui::PushID(rt.manifestPath.c_str());
            std::string label = rt.name;
            if (rt.isSystemDefault) label += " [default]";
            if (rt.serviceBased)    label += " - needs its service running";
            const bool selected = (activeOverride == rt.manifestPath);
            if (ImGui::RadioButton(label.c_str(), selected) && !selected)
                services.setXRRuntimeOverride(rt.manifestPath);
            ImGui::PopID();
        }
        ImGui::EndDisabled();
        ImGui::TreePop();
    }
}

// ── Rendering ────────────────────────────────────────────────────────────────
void drawRenderingTab(Services& services) {
    ImGui::SeparatorText("Tone mapping");
    float exposure = services.tonemapExposure();
    if (ImGui::SliderFloat("Exposure", &exposure, 0.1f, 8.0f, "%.2f",
                           ImGuiSliderFlags_Logarithmic))
        services.setTonemapExposure(exposure);

    const int op = services.tonemapOp();
    if (ImGui::BeginCombo("Operator", services.tonemapOpName(op))) {
        for (int i = 0; i < services.tonemapOpCount(); ++i) {
            const bool selected = (i == op);
            if (ImGui::Selectable(services.tonemapOpName(i), selected))
                services.setTonemapOp(i);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    ImGui::SeparatorText("Presentation");
    const int present = services.currentPresentMode();
    if (ImGui::BeginCombo("Present mode", services.presentModeName(present))) {
        for (int i = 0; i < services.presentModeCount(); ++i) {
            const bool selected = (i == present);
            if (ImGui::Selectable(services.presentModeName(i), selected) && !selected)
                services.requestPresentMode(i);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    helpMarker("Applied at the top of the next frame (never mid-frame).");

    const bool wireSupported = services.wireframeSupported();
    ImGui::BeginDisabled(!wireSupported);
    bool wireframe = services.settings().render.wireframe;
    if (ImGui::Checkbox("Wireframe", &wireframe))
        services.settings().render.wireframe = wireframe;
    ImGui::EndDisabled();
    helpMarker(wireSupported
                   ? "Draw all scene geometry (models + scene layers) with the "
                     "line-mode debug pipeline."
                   : "Unavailable: this GPU lacks the fillModeNonSolid feature.");
}

// ── Lighting ─────────────────────────────────────────────────────────────────
void drawLightingTab(Services& services) {
    Settings::Lighting& l = services.settings().lighting;

    ImGui::Checkbox("Shadows", &l.shadows);
    if (l.shadows) {
        ImGui::SameLine();
        ImGui::Checkbox("Soft (PCSS)", &l.softShadows);
        ImGui::SliderFloat("Point shadow range", &l.pointShadowRange, 5.0f,
                           500.0f, "%.0f m", ImGuiSliderFlags_Logarithmic);
        helpMarker("How far point-light shadows reach (cube-map far plane). "
                   "Depth precision spreads over this range - keep it as "
                   "tight as the scene allows.");
    }

    ImGui::SeparatorText("Sun");
    ImGui::Checkbox("Enabled##sun", &l.sun.enabled);
    if (l.sun.enabled) {
        if (ImGui::SliderFloat3("Direction", &l.sun.direction.x, -1.0f, 1.0f)) {
            if (glm::dot(l.sun.direction, l.sun.direction) < 1e-6f)
                l.sun.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        }
        ImGui::ColorEdit3("Color##sun", &l.sun.color.x);
        ImGui::SliderFloat("Intensity##sun", &l.sun.intensity, 0.0f, 5.0f, "%.2f");
        ImGui::SliderFloat("Angular size", &l.sun.angularSizeDeg, 0.0f, 10.0f, "%.1f deg");
    }

    ImGui::SeparatorText("Ambient");
    ImGui::SliderFloat("Ambient", &l.ambient, 0.0f, 0.3f, "%.3f");

    ImGui::SeparatorText("Point lights");
    std::vector<scene::PointLight>& lights = services.scene().pointLights;
    if (lights.empty())
        ImGui::TextDisabled("(none)");
    for (size_t i = 0; i < lights.size(); ++i) {
        scene::PointLight& light = lights[i];
        ImGui::PushID(static_cast<int>(i));
        if (ImGui::TreeNode((void*)(intptr_t)i, "Light %zu", i)) {
            ImGui::ColorEdit3("Color", &light.color.x);
            ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 10.0f, "%.2f");
            ImGui::Checkbox("Casts shadows", &light.castsShadows);
            ImGui::SliderFloat("Radius", &light.radius, 0.0f, 0.5f, "%.3f m");
            ImGui::DragFloat3("Position", &light.position.x, 0.02f);
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
}

// ── Sky ──────────────────────────────────────────────────────────────────────
void drawSkyTab(Services& services) {
    renderer::SkyState& sky = services.settings().sky;

    const char* skyNames[] = { "Cubemap", "Equirect HDR", "Solid color", "Gradient" };
    const int current = static_cast<int>(sky.mode);
    if (ImGui::BeginCombo("Sky type", skyNames[current])) {
        for (int i = 0; i < 4; ++i) {
            const bool available = (i != 0 || services.skyHasCubemap()) &&
                                   (i != 1 || services.skyHasEquirect());
            if (!available)
                continue;
            const bool selected = (i == current);
            if (ImGui::Selectable(skyNames[i], selected))
                sky.mode = static_cast<renderer::SkyMode>(i);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    if (!services.skyHasCubemap() && !services.skyHasEquirect())
        ImGui::TextDisabled("No cubemap/HDR loaded; procedural modes only.");

    ImGui::SliderFloat("Intensity", &sky.intensity, 0.0f, 4.0f, "%.2f");
    if (sky.mode == renderer::SkyMode::SolidColor)
        ImGui::ColorEdit3("Color##sky", &sky.solidColor.x);
    if (sky.mode == renderer::SkyMode::Gradient) {
        ImGui::ColorEdit3("Bottom", &sky.gradientBottom.x);
        ImGui::ColorEdit3("Top", &sky.gradientTop.x);
    }
}

// ── Interface (theme / scale / status bar — UI redesign Pass 0) ─────────────
void drawInterfaceTab(Services& services) {
    Settings::Ui& ui = services.settings().ui;

    ImGui::SeparatorText("Appearance");
    const int current = services.currentTheme();
    if (ImGui::BeginCombo("Theme", services.themeName(current))) {
        for (int i = 0; i < services.themeCount(); ++i) {
            const bool selected = (i == current);
            if (ImGui::Selectable(services.themeName(i), selected))
                services.setTheme(i);
            if (selected)
                ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }

    // The scale applies on release: every change restyles + rebuilds the font
    // atlas (device-idle), so live-dragging would hitch each tick.
    static float pendingScale = -1.0f;
    float scale = pendingScale >= 0.0f ? pendingScale : services.guiScaleFactor();
    if (ImGui::SliderFloat("GUI scale", &scale, 0.5f, 2.0f, "%.2fx"))
        pendingScale = scale;
    if (ImGui::IsItemDeactivatedAfterEdit()) {
        services.setGuiScaleFactor(scale);
        pendingScale = -1.0f;
    }
    helpMarker("Multiplies the window-derived interface scale (applies when "
               "the slider is released).");

    ImGui::SeparatorText("Interface");
    if (UiKit::ToggleSwitch("Status bar", &ui.showStatusBar)) {}
    if (UiKit::ToggleSwitch("Reduce motion", &ui.reduceMotion)) {}
    helpMarker("Disables interface micro-animations (toggles, hover fades).");

    ImGui::SeparatorText("Shortcuts");
    ImGui::TextDisabled("Shortcuts are rebindable in shortcuts.json;");
    ImGui::TextDisabled("the editor arrives with the Settings overhaul pass.");
}

} // namespace

void drawSettingsPanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::Settings, open)) {
        ImGui::End();
        return;
    }

    if (ImGui::BeginTabBar("##settingsTabs")) {
        if (ImGui::BeginTabItem("Camera"))   { drawCameraTab(services);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Stereo"))   { drawStereoTab(services);    ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("VR"))       { drawVrTab(services);        ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Rendering")){ drawRenderingTab(services); ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Lighting")) { drawLightingTab(services);  ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Sky"))      { drawSkyTab(services);       ImGui::EndTabItem(); }
        if (ImGui::BeginTabItem("Interface")){ drawInterfaceTab(services); ImGui::EndTabItem(); }
        ImGui::EndTabBar();
    }

    ImGui::End();
}

} // namespace Gui
