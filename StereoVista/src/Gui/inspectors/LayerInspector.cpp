#include "Gui/Inspector.h"

#include "Loaders/Slpk/SolarPosition.h"
#include "Scene/Scene.h" // scene::I3SSceneLayer
#include "Tools/ClipPlaneTool.h"

#include "imgui/IconsFontAwesome5.h"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ctime>
#include <string>

// The I3S / SLPK SceneLayer Inspector editor (UI redesign Pass 2 §8): the
// interactive per-layer controls ported from the SlpkPanel — streaming/LOD
// budgets, point colourization, daylight/solar, section slice, bounding-volume
// overlay — onto the live scene::I3SSceneLayer. These are transient streaming
// CONFIG, not scene-object properties, so they edit the layer directly (not via
// the undo stack, contract C4). SlpkPanel keeps the package-open entry point and
// the exhaustive read-only diagnostics until it fully dissolves (may trail, §7.3).

namespace Gui {
namespace Inspector {

namespace {

int daysInMonth(int year, int month) {
    static const int base[] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12)
        return 31;
    if (month == 2) {
        const bool leap = (year % 4 == 0 && year % 100 != 0) || (year % 400 == 0);
        return leap ? 29 : 28;
    }
    return base[month - 1];
}

void drawDaylight(EditContext& ctx, scene::I3SSceneLayer& layer) {
    if (!layer.anchor.isGeodetic())
        return;
    ImGui::SeparatorText("Daylight");
    scene::I3SSceneLayer::Daylight& dl = layer.daylight;
    ImGui::Checkbox("Drive sun from date / time", &dl.driveSun);
    UiKit::HelpMarker("Solar position (NOAA) at this layer's geolocation drives "
                      "the app sun direction.");
    if (!dl.driveSun)
        return;

    int date[3] = { dl.year, dl.month, dl.day };
    ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
    if (ImGui::DragInt3("Y / M / D", date, 0.2f)) {
        dl.year = std::max(1900, std::min(2100, date[0]));
        dl.month = std::max(1, std::min(12, date[1]));
        dl.day = std::max(1, std::min(daysInMonth(dl.year, dl.month), date[2]));
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Now")) {
        const std::time_t t = std::time(nullptr);
        std::tm gtm{};
        const std::tm* g = nullptr;
#ifdef _MSC_VER
        if (gmtime_s(&gtm, &t) == 0)
            g = &gtm;
#else
        g = std::gmtime(&t);
#endif
        if (g) {
            dl.year = g->tm_year + 1900;
            dl.month = g->tm_mon + 1;
            dl.day = g->tm_mday;
            float h = float(g->tm_hour) + float(g->tm_min) / 60.0f + dl.utcOffsetHours;
            while (h >= 24.0f) h -= 24.0f;
            while (h < 0.0f) h += 24.0f;
            dl.localHour = h;
        }
    }
    ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
    ImGui::SliderFloat("Local time", &dl.localHour, 0.0f, 24.0f, "%.2f h");
    ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
    ImGui::DragFloat("UTC offset", &dl.utcOffsetHours, 0.25f, -12.0f, 14.0f, "%+.2f h");

    const glm::dvec3 origin = layer.anchor.originGeodetic();
    int y = dl.year, mo = dl.month, d = dl.day;
    double utcHours = double(dl.localHour) - double(dl.utcOffsetHours);
    while (utcHours >= 24.0) { utcHours -= 24.0; if (++d > daysInMonth(y, mo)) { d = 1; if (++mo > 12) { mo = 1; ++y; } } }
    while (utcHours < 0.0) { utcHours += 24.0; if (--d < 1) { if (--mo < 1) { mo = 12; --y; } d = daysInMonth(y, mo); } }
    const glm::dvec3 toSun = i3s::solarDirectionEnu(origin.x, origin.y, y, mo, d, utcHours);
    renderer::SunState& sun = ctx.services.settings().lighting.sun;
    sun.direction = -scene::I3SSceneLayer::appDirectionFromEnu(toSun);
    sun.enabled = toSun.z > 0.0;
    const double elev = glm::degrees(std::asin(std::max(-1.0, std::min(1.0, toSun.z))));
    double az = glm::degrees(std::atan2(toSun.x, toSun.y));
    if (az < 0.0) az += 360.0;
    ImGui::Text("Sun: azimuth %.1f\xC2\xB0, elevation %.1f\xC2\xB0%s", az, elev,
                sun.enabled ? "" : " (below horizon - sun off)");
}

void drawStatsHud(scene::I3SSceneLayer& layer) {
    const scene::I3SSceneLayer::Stats s = layer.stats();
    ImGui::Text("drawn %u | resident %u | staging %u | ready %u", s.drawnLastFrame,
                s.resident, s.staging, s.ready);
    ImGui::Text("queued %u | decoding %u | failed %u", s.queued, s.decoding, s.failed);
    ImGui::Text("GPU %.1f MB | CPU cache %.1f MB", double(s.gpuBytes) / (1024.0 * 1024.0),
                double(s.cpuCacheBytes) / (1024.0 * 1024.0));
    ImGui::Text("decode %.1f MB/s | upload %.1f MB/s", double(s.decodeRateMBs),
                double(s.uploadRateMBs));
    if (layer.rendersPoints())
        ImGui::Text("points %.2fM / %.2fM | batches %u", double(s.residentPoints) / 1e6,
                    double(s.poolPointsCapacity) / 1e6, s.drawnBatches);
}

void drawPointColorControls(scene::I3SSceneLayer& layer) {
    using PCM = scene::I3SSceneLayer::PointColorMode;
    const char* names[4] = { "RGB", "Intensity ramp", "Classification", "Elevation" };
    const bool avail[4] = { layer.hasRgbColumn, layer.hasIntensityColumn,
                            layer.hasClassColumn, true };
    UiKit::PropertyRow("Color by");
    if (ImGui::BeginCombo("##colorby", names[int(layer.pointColorMode)])) {
        for (int m = 0; m < 4; ++m) {
            if (!avail[m])
                continue;
            if (ImGui::Selectable(names[m], int(layer.pointColorMode) == m)) {
                layer.pointColorMode = PCM(m);
                layer.markPointColorsDirty();
            }
        }
        ImGui::EndCombo();
    }
    if (layer.pointColorMode == PCM::Intensity) {
        float b[2] = { layer.intensityRampMin, layer.intensityRampMax };
        UiKit::PropertyRow("Intensity range");
        if (ImGui::DragFloat2("##irange", b, 1.0f)) {
            layer.intensityRampMin = b[0];
            layer.intensityRampMax = std::max(b[1], b[0] + 1.0f);
            layer.markPointColorsDirty();
        }
    } else if (layer.pointColorMode == PCM::Elevation) {
        float b[2] = { layer.elevationRampMin, layer.elevationRampMax };
        UiKit::PropertyRow("Elevation range (m)");
        if (ImGui::DragFloat2("##erange", b, 0.5f)) {
            layer.elevationRampMin = b[0];
            layer.elevationRampMax = std::max(b[1], b[0] + 0.1f);
            layer.markPointColorsDirty();
        }
    } else if (layer.pointColorMode == PCM::Classification) {
        if (ImGui::TreeNode("Palette")) {
            const std::vector<int>& values = layer.classStats.presentValues;
            const int count = values.empty() ? 19 : int(values.size());
            for (int k = 0; k < count; ++k) {
                const int value = values.empty() ? k : values[size_t(k)];
                if (value < 0 || value > 255)
                    continue;
                ImGui::PushID(value);
                if (ImGui::ColorEdit3("##class", &layer.classPalette[value].x,
                                      ImGuiColorEditFlags_NoInputs))
                    layer.markPointColorsDirty();
                ImGui::SameLine();
                const char* label = "";
                for (const std::pair<int, std::string>& cl : layer.classStats.classLabels)
                    if (cl.first == value) { label = cl.second.c_str(); break; }
                ImGui::Text("%d%s%s", value, label[0] ? " - " : "", label);
                ImGui::PopID();
            }
            if (ImGui::SmallButton("Reset to defaults")) {
                const glm::vec3* def = scene::I3SSceneLayer::defaultClassPalette();
                for (int k = 0; k < 256; ++k)
                    layer.classPalette[k] = def[k];
                layer.markPointColorsDirty();
            }
            ImGui::TreePop();
        }
    }
}

void layerEditor(EditContext& ctx) {
    Services& services = ctx.services;
    scene::Scene& scene = services.scene();
    const scene::SceneItemRef prim = ctx.primary();
    const int li = scene.layerIndexOf(prim.id);
    if (li < 0 || !scene.i3sLayers[size_t(li)]) {
        ImGui::TextDisabled("(scene layer unavailable)");
        return;
    }
    scene::I3SSceneLayer& layer = *scene.i3sLayers[size_t(li)];
    if (ctx.multi())
        ImGui::TextDisabled("Editing the primary layer of the selection.");

    // Summary
    if (!layer.rendersAnything())
        ImGui::TextColored(ImVec4(1.0f, 0.75f, 0.3f, 1.0f),
                           "This layer type is not renderable yet (inspect only).");
    const glm::vec3 ext = layer.boundsMax - layer.boundsMin;
    ImGui::Text("%zu nodes, %u levels  |  %.0f x %.0f x %.0f m", layer.tree.nodes.size(),
                unsigned(layer.tree.levelCount), ext.x, ext.y, ext.z);
    if (layer.anchor.isGeodetic()) {
        const glm::dvec3 g = layer.anchor.originGeodetic();
        ImGui::Text("Anchor: %.6f, %.6f, %.1f m", g.x, g.y, g.z);
    }
    if (ImGui::SmallButton("Frame"))
        services.frameI3SLayer(size_t(li));
    ImGui::SameLine();
    if (ImGui::SmallButton("Unload")) {
        services.unloadI3SLayer(size_t(li));
        return; // layer ref is dead
    }

    // Streaming + LOD
    if (layer.rendersPoints()) {
        ImGui::SeparatorText("Point cloud");
        ImGui::Checkbox("Render points", &layer.showGeometry);
        if (layer.showGeometry) {
            ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
            ImGui::SliderFloat("Quality", &layer.lodScale, 0.1f, 4.0f, "%.2fx",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
            ImGui::SliderFloat("Density (pt/px\xC2\xB2)", &layer.densityTarget, 0.02f, 2.0f,
                               "%.2f", ImGuiSliderFlags_Logarithmic);
            int poolM = layer.budgetPoolPoints / 1000000;
            ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
            if (ImGui::SliderInt("Pool (M points)", &poolM, 1, 64, "%d",
                                 ImGuiSliderFlags_Logarithmic))
                layer.budgetPoolPoints = poolM * 1000000;
            drawPointColorControls(layer);
        }
    } else if (layer.rendersGeometry()) {
        ImGui::SeparatorText("Geometry");
        ImGui::Checkbox("Render geometry", &layer.showGeometry);
        if (layer.showGeometry) {
            ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
            ImGui::SliderFloat("Quality", &layer.lodScale, 0.1f, 4.0f, "%.2fx",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
            ImGui::SliderInt("Node budget", &layer.budgetMaxNodes, 64, 32768, "%d",
                             ImGuiSliderFlags_Logarithmic);
            ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
            ImGui::SliderInt("VRAM budget (MB)", &layer.budgetGpuMB, 128, 8192, "%d",
                             ImGuiSliderFlags_Logarithmic);
            ImGui::SetNextItemWidth(180.0f * UiKit::Scale());
            ImGui::SliderInt("CPU cache (MB)", &layer.budgetCpuMB, 64, 4096, "%d",
                             ImGuiSliderFlags_Logarithmic);
            ImGui::Checkbox("Prefetch", &layer.prefetch);
        }
    }

    // Inspector overlay toggles
    ImGui::SeparatorText("Inspector");
    if (layer.rendersGeometry()) {
        ImGui::Checkbox("Tint by LOD level", &layer.tintByLevel);
        ImGui::SameLine();
        const bool wire = services.wireframeSupported();
        ImGui::BeginDisabled(!wire);
        ImGui::Checkbox("Wireframe", &layer.layerWireframe);
        ImGui::EndDisabled();
        if (!wire && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("This GPU lacks the fillModeNonSolid feature.");
        ImGui::Checkbox("Highlight picked node", &layer.highlightPicked);
    }
    ImGui::Checkbox("Hover info", &layer.hoverInfo);

    // Section slice
    ImGui::SeparatorText("Tools");
    const glm::vec3 center = (layer.boundsMin + layer.boundsMax) * 0.5f;
    ImGui::TextUnformatted("Add section plane:");
    ImGui::SameLine();
    const char* axisLabel[3] = { "X", "Y", "Z" };
    for (int axis = 0; axis < 3; ++axis) {
        ImGui::PushID(axis);
        if (ImGui::SmallButton(axisLabel[axis])) {
            Tools::ClipPlaneTool& tool = services.clipTool();
            const int idx = tool.addAxisAlignedPlane(axis, center);
            if (idx >= 0) {
                tool.setEnabled(true);
                tool.displaySize = std::max(tool.displaySize,
                                            std::max({ ext.x, ext.y, ext.z, 4.0f }) * 0.55f);
                services.toast("Section plane added through " + layer.name);
            } else {
                services.toast("Clip plane budget is full", Plugins::ToastLevel::Warning);
            }
        }
        ImGui::SameLine();
        ImGui::PopID();
    }
    ImGui::NewLine();

    // Daylight / solar
    drawDaylight(ctx, layer);

    // Bounding volumes
    ImGui::SeparatorText("Bounding volumes");
    ImGui::Checkbox("Show OBBs", &layer.showObbs);
    if (layer.showObbs) {
        const int maxLevel = std::max(0, int(layer.tree.levelCount) - 1);
        ImGui::SliderInt("Tree level", &layer.obbLevel, 0, maxLevel);
        layer.obbLevel = std::max(0, std::min(maxLevel, layer.obbLevel));
        ImGui::SameLine();
        ImGui::Checkbox("Cumulative", &layer.obbUpToLevel);
        ImGui::SliderInt("Box cap", &layer.obbMaxBoxes, 256, 20000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        ImGui::Text("%zu boxes pass the filter", layer.countFilteredBoxes());
    }

    // Statistics HUD (collapsed state persisted)
    Settings::Ui::Inspector& s = services.settings().ui.inspector;
    ImGui::SetNextItemOpen(s.layer, ImGuiCond_Once);
    const bool open = ImGui::CollapsingHeader("Statistics");
    s.layer = open;
    if (open)
        drawStatsHud(layer);
}

} // namespace

namespace builtins {
void registerLayerEditor() { registerEditor(Kind::SceneLayer, layerEditor); }
} // namespace builtins

} // namespace Inspector
} // namespace Gui
