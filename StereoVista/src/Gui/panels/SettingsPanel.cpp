#include "Gui/Panels.h"
#include "Gui/Services.h"
#include "Gui/SettingsIndex.h"
#include "Gui/UiKit.h"

#include "Core/CommandRegistry.h"
#include "Core/Shortcuts.h"
#include "Engine/XRRuntimeInfo.h" // Engine::XRDiagnostics / XRRuntimeInfo (plain data)
#include "Scene/Scene.h"          // scene::PointLight editing

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

// ============================================================================
// Settings (UI redesign Pass 6 §12): one searchable, resettable window.
//
//   * Sidebar categories (UiKit::NavItem) + a search field that filters rows
//     ACROSS categories and highlights the matches.
//   * `static const Settings kDefaults{}` IS the defaults framework — the
//     struct's initializers are the single source of truth. Every plain row gets
//     a modified Dot + a per-row ResetGlyph; every category gets a reset footer;
//     "Reset all..." confirms and takes a safety snapshot first (Pass 3).
//   * Rows whose value lives OUTSIDE Gui::Settings (theme, GUI scale, tonemap,
//     present mode, stereo mode, VR) are driven through their Services setters,
//     because a reset must re-apply the SIDE EFFECT, not just poke a field.
//   * The settings index (Gui/SettingsIndex.h) is shared with the Pass-4
//     palette's ':' prefix, which deep-links straight into a row.
//   * 3D Cursor is the CursorPanel's content, re-grouped here (§12) by calling
//     the same drawCursorSettings() the standalone window uses.
// ============================================================================

namespace Gui {

namespace {

const Settings kDefaults{}; // the one defaults source (§12)

// ── Deep-link state (consumed once, on the next frame) ──────────────────────
SettingsCategory g_pendingCategory = SettingsCategory::Count;
std::string g_pendingLabel;
SettingsCategory g_activeCategory = SettingsCategory::Interface;
std::string g_flashLabel;
float g_flashTimer = 0.0f;

// ── Row plumbing ────────────────────────────────────────────────────────────

struct Ctx {
    Services& services;
    SettingsCategory category = SettingsCategory::Interface;
    const char* needle = ""; // "" = not searching
    bool searching = false;
    int drawn = 0;
};

// Should this row be drawn? While searching, only matching rows appear (from
// EVERY category — search is global), and the category header is printed by the
// caller when its first row lands.
bool visible(Ctx& ctx, const char* label, const char* keywords) {
    if (!ctx.searching)
        return true;
    int score = 0;
    if (UiKit::FuzzyMatch(ctx.needle, label, &score))
        return true;
    return keywords && *keywords && UiKit::FuzzyMatch(ctx.needle, keywords, &score);
}

// Flash-highlight a deep-linked row for a moment after the palette jumps to it.
void beginFlash(const char* label) {
    if (g_flashTimer > 0.0f && g_flashLabel == label) {
        ImGui::PushStyleColor(ImGuiCol_Text, UiKit::Color(UiKit::Semantic::Accent));
        ImGui::SetScrollHereY(0.5f);
    }
}
void endFlash(const char* label) {
    if (g_flashTimer > 0.0f && g_flashLabel == label)
        ImGui::PopStyleColor();
}

// A plain Settings row: modified dot + label + widget + reset glyph.
// `widget(value)` returns true when it edited (the return is unused today, but
// keeps the shape ready for side-effect rows).
template <class T, class W>
void row(Ctx& ctx, const char* label, const char* keywords, T& value, const T& def,
         W widget) {
    if (!visible(ctx, label, keywords))
        return;
    ++ctx.drawn;
    ImGui::PushID(label);
    const bool modified = !(value == def);
    beginFlash(label);
    UiKit::Dot(modified);
    ImGui::SameLine();
    UiKit::PropertyRow(label);
    widget(value);
    ImGui::SameLine();
    if (UiKit::ResetGlyph("##reset", modified))
        value = def;
    endFlash(label);
    ImGui::PopID();
}

// A row whose value lives outside Settings (a Services setter owns the side
// effect). The caller supplies read/write/modified explicitly.
template <class Draw>
void sideRow(Ctx& ctx, const char* label, const char* keywords, bool modified,
             Draw draw) {
    if (!visible(ctx, label, keywords))
        return;
    ++ctx.drawn;
    ImGui::PushID(label);
    beginFlash(label);
    UiKit::Dot(modified);
    ImGui::SameLine();
    UiKit::PropertyRow(label);
    draw();
    endFlash(label);
    ImGui::PopID();
}

void help(const char* text) {
    ImGui::SameLine();
    UiKit::HelpMarker(text);
}

// Section header that disappears while searching (matches are shown flat).
void section(Ctx& ctx, const char* label) {
    if (!ctx.searching)
        ImGui::SeparatorText(label);
}

// ── Categories ──────────────────────────────────────────────────────────────

void drawInterface(Ctx& ctx) {
    Services& services = ctx.services;
    Settings::Ui& ui = services.settings().ui;

    section(ctx, "Appearance");
    // Theme + scale own SIDE EFFECTS — never write the fields directly.
    sideRow(ctx, "Theme", "color dark light appearance skin",
            services.currentTheme() != kDefaults.ui.theme, [&] {
                if (ImGui::BeginCombo("##theme",
                                      services.themeName(services.currentTheme()))) {
                    for (int i = 0; i < services.themeCount(); ++i) {
                        const bool selected = (i == services.currentTheme());
                        if (ImGui::Selectable(services.themeName(i), selected))
                            services.setTheme(i);
                        if (selected)
                            ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (UiKit::ResetGlyph("##rtheme",
                                      services.currentTheme() != kDefaults.ui.theme))
                    services.setTheme(kDefaults.ui.theme);
            });

    sideRow(ctx, "GUI scale", "size dpi zoom text bigger smaller",
            services.guiScaleFactor() != kDefaults.ui.guiScale, [&] {
                // Applies on release: each change restyles + rebuilds the font
                // atlas (device-idle), so live-dragging would hitch every tick.
                static float pending = -1.0f;
                float scale = pending >= 0.0f ? pending : services.guiScaleFactor();
                if (ImGui::SliderFloat("##scale", &scale, 0.5f, 2.0f, "%.2fx"))
                    pending = scale;
                if (ImGui::IsItemDeactivatedAfterEdit()) {
                    services.setGuiScaleFactor(scale);
                    pending = -1.0f;
                }
                ImGui::SameLine();
                if (UiKit::ResetGlyph("##rscale", services.guiScaleFactor() !=
                                                      kDefaults.ui.guiScale))
                    services.setGuiScaleFactor(kDefaults.ui.guiScale);
            });

    section(ctx, "Interface");
    row(ctx, "Status bar", "footer stats fps", ui.showStatusBar,
        kDefaults.ui.showStatusBar,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Reduce motion", "animation accessibility disable",
        ui.reduceMotion, kDefaults.ui.reduceMotion,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    if (visible(ctx, "Reduce motion", ""))
        help("Disables interface micro-animations (toggles, hover fades).");
}

void drawCamera(Ctx& ctx) {
    Settings::Camera& c = ctx.services.settings().camera;
    const Settings::Camera& d = kDefaults.camera;

    section(ctx, "Projection");
    row(ctx, "Field of view", "fov lens angle projection", c.fovDeg, d.fovDeg,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 30.0f, 100.0f, "%.0f deg"); });
    row(ctx, "Near plane", "clip projection", c.nearPlane, d.nearPlane,
        [](float& v) { return ImGui::DragFloat("##v", &v, 0.005f, 0.01f, 10.0f, "%.3f"); });
    row(ctx, "Far plane", "clip projection distance", c.farPlane, d.farPlane,
        [](float& v) { return ImGui::DragFloat("##v", &v, 1.0f, 5.0f, 5000.0f, "%.0f"); });
    if (c.farPlane < c.nearPlane * 2.0f)
        c.farPlane = c.nearPlane * 2.0f;

    section(ctx, "Navigation");
    row(ctx, "Distance-adaptive speed", "adaptive fly speed auto", c.adaptiveSpeed,
        d.adaptiveSpeed, [](bool& v) { return ImGui::Checkbox("##v", &v); });
    if (visible(ctx, "Distance-adaptive speed", ""))
        help("Scales fly/zoom speed by the distance to the surface at the screen "
             "centre: fast in open space, gentle near geometry.");
    if (!ctx.searching)
        ImGui::BeginDisabled(c.adaptiveSpeed);
    row(ctx, "Fly speed", "wasd move navigation", c.speed, d.speed,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.5f, 30.0f, "%.1f"); });
    if (!ctx.searching)
        ImGui::EndDisabled();
    row(ctx, "Speed factor", "multiplier fly zoom", c.speedFactor, d.speedFactor,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.1f, 5.0f, "%.2f"); });
    row(ctx, "Look sensitivity", "mouse look", c.sensitivity, d.sensitivity,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.01f, 0.5f, "%.3f"); });
    row(ctx, "Zoom to cursor", "scroll zoom", c.zoomToCursor, d.zoomToCursor,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Orbit around cursor", "rotate pivot", c.orbitAroundCursor,
        d.orbitAroundCursor, [](bool& v) { return ImGui::Checkbox("##v", &v); });

    section(ctx, "Scrolling");
    row(ctx, "Smooth scrolling", "momentum scroll wheel", c.useSmoothScrolling,
        d.useSmoothScrolling, [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Momentum", "scroll smooth", c.scrollMomentum, d.scrollMomentum,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.05f, 2.0f, "%.2f"); });
    row(ctx, "Deceleration", "scroll smooth", c.scrollDeceleration,
        d.scrollDeceleration,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 1.0f, 20.0f, "%.1f"); });
    row(ctx, "Max velocity", "scroll smooth clamp", c.maxScrollVelocity,
        d.maxScrollVelocity,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.5f, 10.0f, "%.1f"); });
}

void drawStereo(Ctx& ctx) {
    Services& services = ctx.services;
    Settings::Stereo& st = services.settings().stereo;
    const Settings::Stereo& d = kDefaults.stereo;

    if (!ctx.searching)
        ImGui::BeginDisabled(services.xrRunning());
    sideRow(ctx, "Stereo mode", "3d quad buffer side by side mono", services.stereoMode() != 0,
            [&] {
                const char* names[] = { "Off (mono)", "Quad-buffer 3D", "Side-by-side" };
                int mode = services.stereoMode();
                if (ImGui::Combo("##mode", &mode, names, 3))
                    services.requestStereoMode(mode);
                ImGui::SameLine();
                if (UiKit::ResetGlyph("##rmode", services.stereoMode() != 0))
                    services.requestStereoMode(0);
            });
    row(ctx, "Eye separation", "stereo ipd", st.separation, d.separation,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 2.0f, "%.3f"); });
    row(ctx, "Convergence", "stereo zero parallax", st.convergence, d.convergence,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.1f, 40.0f, "%.2f"); });
    row(ctx, "Auto convergence", "stereo focus", st.autoConvergence, d.autoConvergence,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Focus factor", "stereo convergence", st.convergenceFactor,
        d.convergenceFactor,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.1f, 3.0f, "%.2f"); });
    row(ctx, "Convergence smoothing", "stereo", st.convergenceSmoothing,
        d.convergenceSmoothing,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.5f, 20.0f, "%.1f"); });
    row(ctx, "Flip eyes", "stereo swap left right", st.flipEyes, d.flipEyes,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    if (!ctx.searching) {
        ImGui::EndDisabled();
        ImGui::Separator();
        ImGui::TextDisabled("Quad-buffer present: %s (swapchain layers %u)",
                            services.stereoPresentSupported() ? "available"
                                                              : "unavailable",
                            services.swapchainLayers());
    }
}

void drawVr(Ctx& ctx) {
    Services& services = ctx.services;
    Settings::Vr& vr = services.settings().vr;
    const Settings::Vr& d = kDefaults.vr;
    const bool running = services.xrRunning();

    sideRow(ctx, "Enable VR (OpenXR)", "vr xr headset hmd", services.vrEnabled(), [&] {
        bool enabled = services.vrEnabled();
        if (ImGui::Checkbox("##vr", &enabled)) {
            services.setVrEnabled(enabled);
            if (enabled && !services.xrDiagnosticsValid())
                services.refreshXRDiagnostics();
        }
        ImGui::SameLine();
        ImGui::TextDisabled(running ? "(active)" : "(off)");
    });

    if (!ctx.searching) {
        if (running) {
            ImGui::Text("Runtime: %s", services.xrRuntimeName().c_str());
            ImGui::Text("Eye buffer: %u x %u", services.xrEyeWidth(),
                        services.xrEyeHeight());
        }
        const std::string status = services.vrStatus();
        if (!status.empty())
            ImGui::TextWrapped("%s", status.c_str());
    }

    row(ctx, "Mirror left eye to window", "vr desktop preview", vr.mirrorToWindow,
        d.mirrorToWindow, [](bool& v) { return ImGui::Checkbox("##v", &v); });

    section(ctx, "Comfort");
    row(ctx, "World scale", "vr metres per unit", vr.worldScale, d.worldScale,
        [](float& v) {
            return ImGui::SliderFloat("##v", &v, 0.01f, 10.0f, "%.3f",
                                      ImGuiSliderFlags_Logarithmic);
        });
    row(ctx, "VR uses desktop near/far", "vr planes", vr.useScenePlanes,
        d.useScenePlanes, [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "VR near plane", "vr clip", vr.nearPlane, d.nearPlane,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.01f, 1.0f, "%.3f"); });
    row(ctx, "VR far plane", "vr clip", vr.farPlane, d.farPlane,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 5.0f, 1000.0f, "%.0f"); });

    if (ctx.searching)
        return;
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

void drawRendering(Ctx& ctx) {
    Services& services = ctx.services;
    Settings::Render& r = services.settings().render;
    const Settings::Render& d = kDefaults.render;

    section(ctx, "Tone mapping");
    // Tonemap + present mode live on the Renderer/Swapchain, not in Settings —
    // they are driven (and reset) through their Services setters. The reset
    // targets are the renderer's own shipped values.
    constexpr float kDefaultExposure = 1.0f;
    constexpr int kDefaultTonemapOp = 0;
    sideRow(ctx, "Exposure", "tonemap brightness hdr",
            services.tonemapExposure() != kDefaultExposure, [&] {
                float exposure = services.tonemapExposure();
                if (ImGui::SliderFloat("##v", &exposure, 0.1f, 8.0f, "%.2f",
                                       ImGuiSliderFlags_Logarithmic))
                    services.setTonemapExposure(exposure);
                ImGui::SameLine();
                if (UiKit::ResetGlyph("##rexp",
                                      services.tonemapExposure() != kDefaultExposure))
                    services.setTonemapExposure(kDefaultExposure);
            });
    sideRow(ctx, "Tonemap operator", "aces reinhard filmic curve",
            services.tonemapOp() != kDefaultTonemapOp, [&] {
                const int op = services.tonemapOp();
                if (ImGui::BeginCombo("##op", services.tonemapOpName(op))) {
                    for (int i = 0; i < services.tonemapOpCount(); ++i)
                        if (ImGui::Selectable(services.tonemapOpName(i), i == op))
                            services.setTonemapOp(i);
                    ImGui::EndCombo();
                }
                ImGui::SameLine();
                if (UiKit::ResetGlyph("##rop",
                                      services.tonemapOp() != kDefaultTonemapOp))
                    services.setTonemapOp(kDefaultTonemapOp);
            });

    section(ctx, "Presentation");
    sideRow(ctx, "Present mode", "vsync fifo mailbox immediate tearing", false, [&] {
        const int present = services.currentPresentMode();
        if (ImGui::BeginCombo("##present", services.presentModeName(present))) {
            for (int i = 0; i < services.presentModeCount(); ++i)
                if (ImGui::Selectable(services.presentModeName(i), i == present) &&
                    i != present)
                    services.requestPresentMode(i);
            ImGui::EndCombo();
        }
    });
    if (visible(ctx, "Present mode", ""))
        help("Applied at the top of the next frame (never mid-frame).");

    section(ctx, "Advanced");
    const bool wireSupported = services.wireframeSupported();
    if (!ctx.searching)
        ImGui::BeginDisabled(!wireSupported);
    row(ctx, "Wireframe", "debug lines mesh", r.wireframe, d.wireframe,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    if (!ctx.searching)
        ImGui::EndDisabled();
    row(ctx, "Async compute", "queue overlap performance point cloud",
        r.asyncCompute, d.asyncCompute,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    if (visible(ctx, "Async compute", ""))
        help("Runs the compute passes on the GPU's async queue, overlapped with "
             "graphics work. No-op when the GPU exposes no second queue.");
    if (!ctx.searching)
        ImGui::TextDisabled("Global illumination and post-processing (VCT, DDGI, "
                            "bloom, SSAO) return here in a later phase.");
}

void drawEnvironment(Ctx& ctx) {
    Services& services = ctx.services;
    Settings::Lighting& l = services.settings().lighting;
    const Settings::Lighting& dl = kDefaults.lighting;
    renderer::SkyState& sky = services.settings().sky;
    const renderer::SkyState& ds = kDefaults.sky;

    section(ctx, "Shadows");
    row(ctx, "Shadows", "shadow map", l.shadows, dl.shadows,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Soft shadows (PCSS)", "shadow contact hardening", l.softShadows,
        dl.softShadows, [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Point shadow range", "shadow cube far", l.pointShadowRange,
        dl.pointShadowRange, [](float& v) {
            return ImGui::SliderFloat("##v", &v, 5.0f, 500.0f, "%.0f m",
                                      ImGuiSliderFlags_Logarithmic);
        });

    section(ctx, "Sun");
    row(ctx, "Sun enabled", "sun directional light", l.sun.enabled, dl.sun.enabled,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Sun direction", "sun angle", l.sun.direction, dl.sun.direction,
        [](glm::vec3& v) {
            const bool changed = ImGui::SliderFloat3("##v", &v.x, -1.0f, 1.0f);
            if (changed && glm::dot(v, v) < 1e-6f)
                v = glm::vec3(0.0f, -1.0f, 0.0f);
            return changed;
        });
    row(ctx, "Sun color", "sun tint warm", l.sun.color, dl.sun.color,
        [](glm::vec3& v) { return ImGui::ColorEdit3("##v", &v.x); });
    row(ctx, "Sun intensity", "sun brightness", l.sun.intensity, dl.sun.intensity,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 5.0f, "%.2f"); });
    row(ctx, "Sun angular size", "sun penumbra softness", l.sun.angularSizeDeg,
        dl.sun.angularSizeDeg,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 10.0f, "%.1f deg"); });

    section(ctx, "Ambient");
    row(ctx, "Ambient", "fill light", l.ambient, dl.ambient,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 0.3f, "%.3f"); });

    section(ctx, "Sky");
    sideRow(ctx, "Sky type", "background cubemap gradient hdr", sky.mode != ds.mode, [&] {
        const char* names[] = { "Cubemap", "Equirect HDR", "Solid color", "Gradient" };
        const int current = int(sky.mode);
        if (ImGui::BeginCombo("##sky", names[current])) {
            for (int i = 0; i < 4; ++i) {
                const bool available = (i != 0 || services.skyHasCubemap()) &&
                                       (i != 1 || services.skyHasEquirect());
                if (!available)
                    continue;
                if (ImGui::Selectable(names[i], i == current))
                    sky.mode = renderer::SkyMode(i);
            }
            ImGui::EndCombo();
        }
        ImGui::SameLine();
        if (UiKit::ResetGlyph("##rsky", sky.mode != ds.mode))
            sky.mode = ds.mode;
    });
    row(ctx, "Sky intensity", "background brightness", sky.intensity, ds.intensity,
        [](float& v) { return ImGui::SliderFloat("##v", &v, 0.0f, 4.0f, "%.2f"); });
    row(ctx, "Sky solid color", "background", sky.solidColor, ds.solidColor,
        [](glm::vec3& v) { return ImGui::ColorEdit3("##v", &v.x); });
    row(ctx, "Sky gradient top", "background", sky.gradientTop, ds.gradientTop,
        [](glm::vec3& v) { return ImGui::ColorEdit3("##v", &v.x); });
    row(ctx, "Sky gradient bottom", "background", sky.gradientBottom,
        ds.gradientBottom,
        [](glm::vec3& v) { return ImGui::ColorEdit3("##v", &v.x); });

    // Point lights are SCENE data, not settings — they live in the Outliner /
    // Inspector (Pass 1/2) and must never be touched by a settings reset.
    if (!ctx.searching)
        ImGui::TextDisabled("Point lights are scene objects — edit them in the "
                            "Scene panel / Inspector.");
}

void drawPointClouds(Ctx& ctx) {
    Settings::PointCloud& pc = ctx.services.settings().pointCloud;
    const Settings::PointCloud& d = kDefaults.pointCloud;

    section(ctx, "Loading");
    row(ctx, "Downsample", "stride decimate load", pc.downsample, d.downsample,
        [](int& v) {
            const bool changed = ImGui::InputInt("##v", &v);
            if (v < 1)
                v = 1;
            return changed;
        });
    row(ctx, "Morton resort", "las laz order streaming", pc.mortonResort,
        d.mortonResort, [](bool& v) { return ImGui::Checkbox("##v", &v); });

    section(ctx, "Rendering");
    row(ctx, "High-quality shading", "hqs averaging", pc.hqs, d.hqs,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "HQS depth window", "hqs threshold", pc.hqsThreshold, d.hqsThreshold,
        [](float& v) {
            return ImGui::SliderFloat("##v", &v, 0.001f, 0.1f, "%.3f",
                                      ImGuiSliderFlags_Logarithmic);
        });
    row(ctx, "Adaptive splats", "splat hole filling", pc.splatEnabled, d.splatEnabled,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Splat max radius", "splat pixels", pc.splatMaxRadius, d.splatMaxRadius,
        [](int& v) { return ImGui::SliderInt("##v", &v, 1, 8); });
    row(ctx, "Density LOD", "lod thinning budget", pc.lodEnabled, d.lodEnabled,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Points per pixel", "lod density budget", pc.lodPointsPerPixel,
        d.lodPointsPerPixel, [](float& v) {
            return ImGui::SliderFloat("##v", &v, 0.25f, 8.0f, "%.2f",
                                      ImGuiSliderFlags_Logarithmic);
        });
}

void drawFiles(Ctx& ctx) {
    Services& services = ctx.services;
    Settings::Files& f = services.settings().files;
    const Settings::Files& d = kDefaults.files;

    section(ctx, "Opening scenes");
    row(ctx, "Open scene behaviour", "replace merge ask remember", f.openSceneMode,
        d.openSceneMode, [](int& v) {
            const char* names[] = { "Ask every time", "Replace", "Merge" };
            return ImGui::Combo("##v", &v, names, 3);
        });
    row(ctx, "Safety snapshot before replace", "backup checkpoint destructive",
        f.safetySnapshotBeforeReplace, d.safetySnapshotBeforeReplace,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });

    section(ctx, "Autosave");
    row(ctx, "Autosave", "backup recovery crash", f.autosaveEnabled, d.autosaveEnabled,
        [](bool& v) { return ImGui::Checkbox("##v", &v); });
    row(ctx, "Autosave interval", "minutes backup", f.autosaveMinutes,
        d.autosaveMinutes,
        [](int& v) { return ImGui::SliderInt("##v", &v, 1, 60, "%d min"); });
    row(ctx, "Autosave slots", "rotate backup files", f.autosaveSlots, d.autosaveSlots,
        [](int& v) { return ImGui::SliderInt("##v", &v, 1, 10); });

    section(ctx, "Welcome");
    row(ctx, "Show Welcome Hub", "start empty scene hub", f.showWelcomeHub,
        d.showWelcomeHub, [](bool& v) { return ImGui::Checkbox("##v", &v); });

    if (ctx.searching)
        return;
    ImGui::SeparatorText("Recent scenes");
    ImGui::TextDisabled("%zu remembered", f.recentScenes.size());
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear recents"))
        f.recentScenes.clear();
}

// ── Shortcuts editor ────────────────────────────────────────────────────────

void drawShortcuts(Ctx& ctx) {
    Services& services = ctx.services;
    core::CommandRegistry& commands = services.commands();
    core::ShortcutMap& shortcuts = services.shortcuts();

    static std::string capturingId;
    static int capturingSlot = 0;

    if (!ctx.searching) {
        ImGui::TextDisabled("Click a shortcut to rebind it. Esc cancels.");
        ImGui::SameLine();
        if (ImGui::SmallButton("Reset all shortcuts"))
            shortcuts.resetToDefaults();
        ImGui::Separator();
    }

    // Capture: while armed, swallow the keyboard so the chord being bound does
    // not ALSO fire its current command (dispatchShortcuts only runs when ImGui
    // is not capturing the keyboard).
    if (!capturingId.empty()) {
        ImGui::SetNextFrameWantCaptureKeyboard(true);
        if (ImGui::IsKeyPressed(ImGuiKey_Escape, false)) {
            capturingId.clear();
        } else if (const int glfwKey = services.capturePressedKey()) {
            const ImGuiIO& io = ImGui::GetIO();
            core::ShortcutBinding binding;
            binding.keyCode = glfwKey;
            binding.ctrl = io.KeyCtrl;
            binding.alt = io.KeyAlt;
            binding.shift = io.KeyShift;
            // Clear the chord from whoever held it, then take it.
            if (const std::string* other =
                    shortcuts.findConflict(binding, capturingId)) {
                const std::string victim = *other;
                const std::vector<core::ShortcutBinding> theirs =
                    shortcuts.bindings(victim);
                for (int slot = 0; slot < int(theirs.size()); ++slot)
                    if (theirs[size_t(slot)] == binding)
                        shortcuts.clearBinding(victim, slot);
                const core::Command* cmd = commands.find(victim);
                services.toast("Took " + binding.toString() + " from \"" +
                                   (cmd ? cmd->title : victim) + "\"",
                               Plugins::ToastLevel::Warning);
            }
            shortcuts.setBinding(capturingId, capturingSlot, binding);
            capturingId.clear();
        }
    }

    for (const core::Command& command : commands.commands()) {
        if (!visible(ctx, command.title.c_str(), command.keywords.c_str()))
            continue;
        ++ctx.drawn;
        ImGui::PushID(command.id.c_str());
        UiKit::PropertyRow(command.title.c_str());

        const std::vector<core::ShortcutBinding> live = shortcuts.bindings(command.id);
        for (int slot = 0; slot < core::ShortcutMap::kMaxSlots; ++slot) {
            ImGui::PushID(slot);
            const bool capturing =
                (capturingId == command.id && capturingSlot == slot);
            std::string label;
            if (capturing)
                label = "Press a key...";
            else if (slot < int(live.size()) && live[size_t(slot)].valid())
                label = live[size_t(slot)].toString();
            else
                label = "--";
            if (ImGui::Button(label.c_str(), ImVec2(140.0f * UiKit::Scale(), 0.0f))) {
                capturingId = command.id;
                capturingSlot = slot;
            }
            ImGui::SameLine();
            ImGui::PopID();
        }

        // Per-row reset to the registered default.
        const std::vector<core::ShortcutBinding>& defaults =
            shortcuts.defaults(command.id);
        bool modified = live.size() != defaults.size();
        for (size_t i = 0; !modified && i < live.size(); ++i)
            modified = !(live[i] == defaults[i]);
        if (UiKit::ResetGlyph("##rsc", modified)) {
            for (int slot = 0; slot < core::ShortcutMap::kMaxSlots; ++slot)
                shortcuts.clearBinding(command.id, slot);
            for (int slot = 0; slot < int(defaults.size()); ++slot)
                shortcuts.setBinding(command.id, slot, defaults[size_t(slot)]);
        }
        ImGui::PopID();
    }
}

// ── Reset ───────────────────────────────────────────────────────────────────

// Reset one category (plain Settings fields + the side-effect setters it owns).
void resetCategory(Services& services, SettingsCategory category) {
    Settings& s = services.settings();
    switch (category) {
    case SettingsCategory::Interface:
        s.ui.showStatusBar = kDefaults.ui.showStatusBar;
        s.ui.reduceMotion = kDefaults.ui.reduceMotion;
        services.setTheme(kDefaults.ui.theme);
        services.setGuiScaleFactor(kDefaults.ui.guiScale);
        break;
    case SettingsCategory::Camera: s.camera = kDefaults.camera; break;
    case SettingsCategory::Cursor: s.cursor = kDefaults.cursor; break;
    case SettingsCategory::Stereo:
        s.stereo = kDefaults.stereo;
        services.requestStereoMode(0);
        break;
    case SettingsCategory::Vr:
        s.vr = kDefaults.vr;
        services.setVrEnabled(false);
        services.setXRRuntimeOverride("");
        break;
    case SettingsCategory::Rendering:
        s.render = kDefaults.render;
        services.setTonemapExposure(1.0f);
        services.setTonemapOp(0);
        break;
    case SettingsCategory::Environment:
        s.lighting = kDefaults.lighting; // sun default is honestly ON (§12)
        s.sky = kDefaults.sky;
        break;
    case SettingsCategory::PointClouds: s.pointCloud = kDefaults.pointCloud; break;
    case SettingsCategory::Files: {
        // Recents are user history, not a setting — never wiped by a reset.
        std::vector<std::string> keep = std::move(s.files.recentScenes);
        s.files = kDefaults.files;
        s.files.recentScenes = std::move(keep);
        break;
    }
    case SettingsCategory::Shortcuts: services.shortcuts().resetToDefaults(); break;
    default: break;
    }
}

void resetAll(Services& services) {
    // Safety snapshot first (Pass 3) — a reset touches the sun/sky, which is
    // scene-visible state the user may want back.
    services.createSnapshot("Before settings reset",
                            kSnapshotCamera | kSnapshotScene);
    for (int i = 0; i < int(SettingsCategory::Count); ++i)
        resetCategory(services, SettingsCategory(i));
    // Layout + panel visibility + Inspector collapse state are NOT settings —
    // they're the user's workspace. "Reset layout" is its own command.
    services.toast("Settings reset to defaults", Plugins::ToastLevel::Success);
}

} // namespace

// ── Index + deep-link (shared with the Pass-4 palette) ───────────────────────

const char* settingsCategoryName(SettingsCategory category) {
    switch (category) {
    case SettingsCategory::Interface: return "Interface";
    case SettingsCategory::Camera: return "Camera & navigation";
    case SettingsCategory::Cursor: return "3D Cursor";
    case SettingsCategory::Stereo: return "Stereo";
    case SettingsCategory::Vr: return "VR / OpenXR";
    case SettingsCategory::Rendering: return "Rendering";
    case SettingsCategory::Environment: return "Environment";
    case SettingsCategory::PointClouds: return "Point clouds";
    case SettingsCategory::Files: return "Files & autosave";
    case SettingsCategory::Shortcuts: return "Shortcuts";
    default: return "";
    }
}

const char* settingsCategoryIcon(SettingsCategory category) {
    switch (category) {
    case SettingsCategory::Interface: return ICON_FA_PALETTE;
    case SettingsCategory::Camera: return ICON_FA_VIDEO;
    case SettingsCategory::Cursor: return ICON_FA_MOUSE_POINTER;
    case SettingsCategory::Stereo: return ICON_FA_GLASSES;
    case SettingsCategory::Vr: return ICON_FA_VR_CARDBOARD;
    case SettingsCategory::Rendering: return ICON_FA_IMAGE;
    case SettingsCategory::Environment: return ICON_FA_SUN;
    case SettingsCategory::PointClouds: return ICON_FA_CLOUD;
    case SettingsCategory::Files: return ICON_FA_SAVE;
    case SettingsCategory::Shortcuts: return ICON_FA_KEYBOARD;
    default: return ICON_FA_COG;
    }
}

const std::vector<SettingsIndexEntry>& settingsIndex() {
    static const std::vector<SettingsIndexEntry> index = {
        { "Theme", "color dark light appearance", SettingsCategory::Interface },
        { "GUI scale", "size dpi zoom text", SettingsCategory::Interface },
        { "Status bar", "footer stats", SettingsCategory::Interface },
        { "Reduce motion", "animation accessibility", SettingsCategory::Interface },
        { "Field of view", "fov lens", SettingsCategory::Camera },
        { "Fly speed", "wasd navigation", SettingsCategory::Camera },
        { "Look sensitivity", "mouse", SettingsCategory::Camera },
        { "Distance-adaptive speed", "adaptive", SettingsCategory::Camera },
        { "Smooth scrolling", "momentum wheel", SettingsCategory::Camera },
        { "Zoom to cursor", "scroll", SettingsCategory::Camera },
        { "Show 3D cursor", "cursor", SettingsCategory::Cursor },
        { "Cursor type", "sphere plane fragment", SettingsCategory::Cursor },
        { "Stereo mode", "3d quad buffer side by side", SettingsCategory::Stereo },
        { "Eye separation", "ipd stereo", SettingsCategory::Stereo },
        { "Convergence", "zero parallax", SettingsCategory::Stereo },
        { "Enable VR (OpenXR)", "vr xr headset", SettingsCategory::Vr },
        { "World scale", "vr metres", SettingsCategory::Vr },
        { "Exposure", "tonemap brightness", SettingsCategory::Rendering },
        { "Tonemap operator", "aces filmic", SettingsCategory::Rendering },
        { "Present mode", "vsync tearing", SettingsCategory::Rendering },
        { "Wireframe", "debug lines", SettingsCategory::Rendering },
        { "Async compute", "queue performance", SettingsCategory::Rendering },
        { "Shadows", "shadow map", SettingsCategory::Environment },
        { "Soft shadows (PCSS)", "contact hardening", SettingsCategory::Environment },
        { "Sun enabled", "directional light", SettingsCategory::Environment },
        { "Sun intensity", "brightness", SettingsCategory::Environment },
        { "Ambient", "fill light", SettingsCategory::Environment },
        { "Sky type", "background gradient cubemap", SettingsCategory::Environment },
        { "High-quality shading", "hqs point cloud", SettingsCategory::PointClouds },
        { "Density LOD", "lod thinning", SettingsCategory::PointClouds },
        { "Points per pixel", "lod budget", SettingsCategory::PointClouds },
        { "Adaptive splats", "splat", SettingsCategory::PointClouds },
        { "Downsample", "stride load", SettingsCategory::PointClouds },
        { "Open scene behaviour", "replace merge ask", SettingsCategory::Files },
        { "Autosave", "backup recovery", SettingsCategory::Files },
        { "Autosave interval", "minutes", SettingsCategory::Files },
        { "Safety snapshot before replace", "backup", SettingsCategory::Files },
        { "Show Welcome Hub", "start", SettingsCategory::Files },
    };
    return index;
}

void focusSetting(SettingsCategory category, const char* label) {
    g_pendingCategory = category;
    g_pendingLabel = label ? label : "";
}

// ── The window ───────────────────────────────────────────────────────────────

void drawSettingsPanel(Services& services, bool* open) {
    // Honour a palette deep-link before the window is submitted.
    if (g_pendingCategory != SettingsCategory::Count) {
        g_activeCategory = g_pendingCategory;
        g_flashLabel = g_pendingLabel;
        g_flashTimer = 1.6f;
        g_pendingCategory = SettingsCategory::Count;
        ImGui::SetNextWindowFocus();
    }
    if (g_flashTimer > 0.0f)
        g_flashTimer -= ImGui::GetIO().DeltaTime;

    if (!ImGui::Begin(Windows::Settings, open)) {
        ImGui::End();
        return;
    }

    static char search[128] = "";
    UiKit::SearchInput("settingsSearch", search, sizeof(search),
                       "Search settings...");
    const bool searching = search[0] != '\0';

    ImGui::Separator();

    // ── Sidebar ─────────────────────────────────────────────────────────────
    const float sidebar = 190.0f * UiKit::Scale();
    ImGui::BeginChild("settingsNav", ImVec2(sidebar, -ImGui::GetFrameHeightWithSpacing()),
                      true);
    for (int i = 0; i < int(SettingsCategory::Count); ++i) {
        const SettingsCategory category = SettingsCategory(i);
        // While searching, mark the categories that actually contain a hit.
        int hits = 0;
        if (searching)
            for (const SettingsIndexEntry& entry : settingsIndex())
                if (entry.category == category) {
                    int score = 0;
                    if (UiKit::FuzzyMatch(search, entry.label, &score) ||
                        UiKit::FuzzyMatch(search, entry.keywords, &score))
                        ++hits;
                }
        if (UiKit::NavItem(settingsCategoryIcon(category),
                           settingsCategoryName(category),
                           !searching && g_activeCategory == category))
            g_activeCategory = category;
        if (searching && hits > 0) {
            ImGui::SameLine();
            char badge[8];
            std::snprintf(badge, sizeof(badge), "%d", hits);
            UiKit::Badge(badge, UiKit::Color(UiKit::Semantic::Accent));
        }
    }
    ImGui::EndChild();

    ImGui::SameLine();

    // ── Content ─────────────────────────────────────────────────────────────
    ImGui::BeginChild("settingsContent",
                      ImVec2(0.0f, -ImGui::GetFrameHeightWithSpacing()), true);

    Ctx ctx{ services };
    ctx.needle = search;
    ctx.searching = searching;

    const auto drawCategory = [&](SettingsCategory category) {
        ctx.category = category;
        switch (category) {
        case SettingsCategory::Interface: drawInterface(ctx); break;
        case SettingsCategory::Camera: drawCamera(ctx); break;
        case SettingsCategory::Cursor:
            drawCursorSettings(services); // re-grouped CursorPanel content (§12)
            break;
        case SettingsCategory::Stereo: drawStereo(ctx); break;
        case SettingsCategory::Vr: drawVr(ctx); break;
        case SettingsCategory::Rendering: drawRendering(ctx); break;
        case SettingsCategory::Environment: drawEnvironment(ctx); break;
        case SettingsCategory::PointClouds: drawPointClouds(ctx); break;
        case SettingsCategory::Files: drawFiles(ctx); break;
        case SettingsCategory::Shortcuts: drawShortcuts(ctx); break;
        default: break;
        }
    };

    // Does this category hold a search hit? Asked BEFORE drawing, so an empty
    // category never leaves a dangling header behind.
    const auto categoryHasHit = [&](SettingsCategory category) {
        for (const SettingsIndexEntry& entry : settingsIndex())
            if (entry.category == category) {
                int score = 0;
                if (UiKit::FuzzyMatch(search, entry.label, &score) ||
                    UiKit::FuzzyMatch(search, entry.keywords, &score))
                    return true;
            }
        return false;
    };

    if (searching) {
        // Global search: matching rows from EVERY category, under its header.
        bool any = false;
        for (int i = 0; i < int(SettingsCategory::Count); ++i) {
            const SettingsCategory category = SettingsCategory(i);
            if (!categoryHasHit(category))
                continue;
            ImGui::PushID(1000 + i);
            UiKit::SectionHeader(settingsCategoryName(category));
            drawCategory(category);
            ImGui::PopID();
            any = true;
        }
        if (!any)
            UiKit::EmptyState(ICON_FA_SEARCH, "No settings match",
                              "Try a different word — or clear the search to browse "
                              "by category.");
    } else {
        UiKit::PanelTitle(settingsCategoryIcon(g_activeCategory),
                          settingsCategoryName(g_activeCategory));
        drawCategory(g_activeCategory);

        ImGui::Spacing();
        ImGui::Separator();
        if (ImGui::SmallButton("Reset this category"))
            resetCategory(services, g_activeCategory);
    }

    ImGui::EndChild();

    // ── Footer: Reset all (confirm + safety snapshot) ───────────────────────
    if (ImGui::Button("Reset all\xE2\x80\xA6"))
        ImGui::OpenPopup("Reset all settings?");
    if (ImGui::BeginPopupModal("Reset all settings?", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::TextUnformatted("Restore every setting to its default?");
        ImGui::TextDisabled("Your layout, recent scenes and scene objects are kept.\n"
                            "A safety snapshot is taken first.");
        if (ImGui::Button("Reset all", ImVec2(120.0f, 0.0f))) {
            resetAll(services);
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f)))
            ImGui::CloseCurrentPopup();
        ImGui::EndPopup();
    }

    ImGui::End();
}

} // namespace Gui
