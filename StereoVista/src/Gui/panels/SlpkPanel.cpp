#include "Gui/Panels.h"
#include "Gui/Services.h"

#include "Loaders/Slpk/SlpkArchive.h"
#include "Loaders/Slpk/SolarPosition.h"
#include "Scene/Scene.h"
#include "Tools/ClipPlaneTool.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <vector>

namespace Gui {

namespace {

// Days per month, Gregorian (leap Februaries clamp at use).
int daysInMonth(int year, int month) {
    static const int kDays[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    if (month < 1 || month > 12)
        return 31;
    const bool leap = (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
    return month == 2 && leap ? 29 : kDays[month - 1];
}

// ---- M4: daylight — drive the app sun from the layer's geolocation ---------
void drawDaylightSection(Services& services, scene::I3SSceneLayer& layer) {
    if (!layer.anchor.isGeodetic())
        return; // projected/local layers carry no usable geolocation

    ImGui::SeparatorText("Daylight");
    scene::I3SSceneLayer::Daylight& dl = layer.daylight;
    bool changed = false;

    changed |= ImGui::Checkbox("Drive sun from date/time", &dl.driveSun);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Solar position (NOAA) at this layer's location.\n"
                          "While on, the date/time below controls the scene sun.");
    if (!dl.driveSun)
        return;

    int date[3] = { dl.year, dl.month, dl.day };
    ImGui::SetNextItemWidth(160.0f);
    if (ImGui::DragInt3("Y / M / D", date, 0.2f)) {
        dl.year = std::min(std::max(date[0], 1900), 2100);
        dl.month = std::min(std::max(date[1], 1), 12);
        dl.day = std::min(std::max(date[2], 1), daysInMonth(dl.year, dl.month));
        changed = true;
    }
    ImGui::SameLine();
    if (ImGui::SmallButton("Now")) {
        const std::time_t now = std::time(nullptr);
        if (const std::tm* utc = std::gmtime(&now)) {
            dl.year = utc->tm_year + 1900;
            dl.month = utc->tm_mon + 1;
            dl.day = utc->tm_mday;
            dl.localHour = float(utc->tm_hour) + float(utc->tm_min) / 60.0f +
                           dl.utcOffsetHours;
            while (dl.localHour < 0.0f)
                dl.localHour += 24.0f;
            while (dl.localHour >= 24.0f)
                dl.localHour -= 24.0f;
            changed = true;
        }
    }
    ImGui::SetNextItemWidth(160.0f);
    changed |= ImGui::SliderFloat("Local time", &dl.localHour, 0.0f, 24.0f,
                                  "%.2f h");
    ImGui::SetNextItemWidth(160.0f);
    changed |= ImGui::DragFloat("UTC offset", &dl.utcOffsetHours, 0.25f, -12.0f,
                                14.0f, "%+.2f h");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Site timezone (seeded from the longitude; adjust "
                          "for civil/DST time).");

    // Recompute every frame while driving — the cost is a handful of
    // trigonometric calls, and it keeps the sun honest after undo/preset
    // edits elsewhere.
    (void)changed;
    const glm::dvec3 origin = layer.anchor.originGeodetic(); // lon/lat/h
    double utcHours = double(dl.localHour) - double(dl.utcOffsetHours);
    int day = dl.day;
    int month = dl.month;
    int year = dl.year;
    // Fold the time into [0,24) shifting the date so midnight edges behave.
    while (utcHours < 0.0) {
        utcHours += 24.0;
        if (--day < 1) {
            if (--month < 1) {
                month = 12;
                --year;
            }
            day = daysInMonth(year, month);
        }
    }
    while (utcHours >= 24.0) {
        utcHours -= 24.0;
        if (++day > daysInMonth(year, month)) {
            day = 1;
            if (++month > 12) {
                month = 1;
                ++year;
            }
        }
    }
    const glm::dvec3 toSunEnu =
        i3s::solarDirectionEnu(origin.x, origin.y, year, month, day, utcHours);
    const double elevationDeg = glm::degrees(std::asin(glm::clamp(toSunEnu.z, -1.0, 1.0)));
    const double azimuthDeg =
        glm::degrees(std::atan2(toSunEnu.x, toSunEnu.y)); // from north, cw

    renderer::SunState& sun = services.settings().lighting.sun;
    sun.direction =
        -scene::I3SSceneLayer::appDirectionFromEnu(toSunEnu); // travel direction
    sun.enabled = toSunEnu.z > 0.0;

    ImGui::Text("Sun: azimuth %.1f deg, elevation %.1f deg%s",
                azimuthDeg < 0.0 ? azimuthDeg + 360.0 : azimuthDeg, elevationDeg,
                sun.enabled ? "" : "  (below horizon - sun off)");
}

// ---- M4: picked feature + attribute row -------------------------------------
void drawPickedFeature(const scene::I3SSceneLayer& layer) {
    const scene::I3SSceneLayer::PickedFeature& picked = layer.pickedFeature;
    if (!picked.valid)
        return;
    if (picked.hasFeatureId)
        ImGui::Text("feature %d (OID %llu), %.2f m from surface",
                    picked.featureIndex,
                    static_cast<unsigned long long>(picked.featureId),
                    double(picked.distance));
    else if (picked.featureIndex >= 0)
        ImGui::Text("feature %d, %.2f m from surface", picked.featureIndex,
                    double(picked.distance));
    if (!picked.warning.empty())
        ImGui::TextDisabled("(%s)", picked.warning.c_str());
    if (picked.attributes.empty())
        return;
    if (ImGui::BeginTable("##attrs", 2,
                          ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp |
                              ImGuiTableFlags_BordersInnerV)) {
        for (const auto& entry : picked.attributes) {
            ImGui::TableNextRow();
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.first.c_str());
            ImGui::TableNextColumn();
            ImGui::TextUnformatted(entry.second.c_str());
        }
        ImGui::EndTable();
    }
}

// ---- M4: inspector v1 — hover readout + debug displays ----------------------
void drawInspectorSection(Services& services, scene::I3SSceneLayer& layer) {
    ImGui::SeparatorText("Inspector");
    if (layer.rendersGeometry()) {
        ImGui::Checkbox("Tint by LOD level", &layer.tintByLevel);
        ImGui::SameLine();
        const bool wireSupported = services.wireframeSupported();
        ImGui::BeginDisabled(!wireSupported);
        ImGui::Checkbox("Wireframe", &layer.layerWireframe);
        ImGui::EndDisabled();
        if (!wireSupported && ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
            ImGui::SetTooltip("This GPU lacks the fillModeNonSolid feature.");
        ImGui::Checkbox("Highlight picked node", &layer.highlightPicked);
    }
    ImGui::Checkbox("Hover info", &layer.hoverInfo);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Show the node under the mouse (and outline its "
                          "bounding box) while the cursor is on the layer.");

    // Hover pick: the depth cursor under the mouse -> deepest drawn node.
    layer.hoverNode = -1;
    if (layer.hoverInfo && !ImGui::GetIO().WantCaptureMouse) {
        glm::vec3 world;
        if (services.pluginContext().cursorWorldPos(world))
            layer.hoverNode = layer.pickNodeAt(world);
        if (layer.hoverNode >= 0 &&
            layer.hoverNode < int(layer.tree.nodes.size())) {
            const i3s::NodeInfo& n = layer.tree.nodes[layer.hoverNode];
            const ImVec2 mouse = ImGui::GetMousePos();
            ImGui::SetNextWindowPos(ImVec2(mouse.x + 16.0f, mouse.y + 16.0f));
            ImGui::SetNextWindowBgAlpha(0.75f);
            if (ImGui::Begin("##i3sHover", nullptr,
                             ImGuiWindowFlags_NoDecoration |
                                 ImGuiWindowFlags_AlwaysAutoResize |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoInputs |
                                 ImGuiWindowFlags_NoFocusOnAppearing |
                                 ImGuiWindowFlags_NoNav)) {
                ImGui::Text("%s: node %d, level %u", layer.name.c_str(),
                            layer.hoverNode, unsigned(n.level));
                if (layer.rendersPoints())
                    ImGui::Text("%llu points",
                                static_cast<unsigned long long>(n.mesh.vertexCount));
                else
                    ImGui::Text("%llu vertices, %llu features",
                                static_cast<unsigned long long>(n.mesh.vertexCount),
                                static_cast<unsigned long long>(n.mesh.featureCount));
            }
            ImGui::End();
        }
    }
}

// ---- M4: slice preset — seed the clip tool from the layer bounds ------------
void drawSliceSection(Services& services, scene::I3SSceneLayer& layer) {
    ImGui::SeparatorText("Tools");
    ImGui::TextUnformatted("Slice layer:");
    ImGui::SameLine();
    const glm::vec3 center = (layer.boundsMin + layer.boundsMax) * 0.5f;
    const glm::vec3 extent = layer.boundsMax - layer.boundsMin;
    int axis = -1;
    if (ImGui::SmallButton("X"))
        axis = 0;
    ImGui::SameLine();
    if (ImGui::SmallButton("Y"))
        axis = 1;
    ImGui::SameLine();
    if (ImGui::SmallButton("Z"))
        axis = 2;
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Add a section plane through the layer center "
                          "(edit it in the Clip Planes panel; clips meshes "
                          "AND point clouds).");
    if (axis >= 0) {
        Tools::ClipPlaneTool& tool = services.clipTool();
        const int index = tool.addAxisAlignedPlane(axis, center);
        if (index >= 0) {
            tool.setEnabled(true);
            const float halfSpan =
                std::max({ extent.x, extent.y, extent.z, 4.0f }) * 0.55f;
            tool.displaySize = std::max(tool.displaySize, halfSpan);
            services.toast("Section plane added through " + layer.name);
        } else {
            services.toast("Clip plane budget is full",
                           Plugins::ToastLevel::Warning);
        }
    }
}

const char* lodMetricName(i3s::LodMetric metric) {
    switch (metric) {
    case i3s::LodMetric::MaxScreenThresholdSQ: return "maxScreenThresholdSQ";
    case i3s::LodMetric::MaxScreenThreshold: return "maxScreenThreshold";
    case i3s::LodMetric::DistanceRangeFromDefaultCamera:
        return "distanceRangeFromDefaultCamera";
    case i3s::LodMetric::DensityThreshold: return "density-threshold";
    default: return "unknown";
    }
}

void drawLayerInspector(Services& services, scene::I3SSceneLayer& layer,
                        size_t index) {
    const i3s::LayerInfo& info = layer.info;
    const i3s::I3SNodeTree& tree = layer.tree;

    ImGui::Checkbox("##visible", &layer.visible);
    ImGui::SameLine();
    const bool open = ImGui::CollapsingHeader(layer.name.c_str(),
                                              ImGuiTreeNodeFlags_DefaultOpen);
    if (!open)
        return;

    // ---- summary ----
    ImGui::Text("%s  v%s  (%s)", info.typeString.c_str(), info.version.c_str(),
                info.profile.empty() ? "-" : info.profile.c_str());
    if (info.sr.isGeographic())
        ImGui::Text("CRS: WGS84 (wkid %d), geographic", info.sr.wkid);
    else
        ImGui::Text("CRS: wkid %d%s", info.sr.wkid,
                    info.sr.wkid == 0 ? " (unknown - local space)" : " (projected)");
    ImGui::Text("%zu nodes, %u levels  |  LOD metric: %s", tree.nodes.size(),
                tree.levelCount, lodMetricName(info.lodMetric));
    ImGui::Text("Archive: %llu entries%s",
                static_cast<unsigned long long>(layer.archive().entryCount()),
                layer.archive().usedHashIndex() ? ", O(1) hash index" : "");
    if (info.sr.isGeographic()) {
        ImGui::Text("Anchor: %.6f, %.6f, %.1f m",
                    layer.anchor.originGeodetic().x, layer.anchor.originGeodetic().y,
                    layer.anchor.originGeodetic().z);
    }
    const glm::vec3 extent = layer.boundsMax - layer.boundsMin;
    ImGui::Text("Extent: %.0f x %.0f x %.0f m", extent.x, extent.y, extent.z);
    if (!tree.nodes.empty() && tree.nodes[0].obbSynthesizedFromMbs)
        ImGui::TextDisabled("(bounds synthesized from 1.6 bounding spheres)");

    if (ImGui::SmallButton("Frame"))
        services.frameI3SLayer(index);
    ImGui::SameLine();
    if (ImGui::SmallButton("Unload")) {
        services.unloadI3SLayer(index);
        return; // layer reference is dead
    }

    // ---- point-cloud rendering (M3) ----
    if (layer.rendersPoints()) {
        ImGui::SeparatorText("Point cloud");
        ImGui::Checkbox("Render points", &layer.showGeometry);
        if (layer.showGeometry) {
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("Quality", &layer.lodScale, 0.1f, 4.0f, "%.2fx",
                               ImGuiSliderFlags_Logarithmic);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("Density (pt/px^2)", &layer.densityTarget, 0.02f,
                               2.0f, "%.2f", ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Screen density the LOD traversal refines "
                                  "toward (spec suggests ~0.1).");
            int poolM = layer.budgetPoolPoints / 1000000;
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::SliderInt("Pool (M points)", &poolM, 1, 64, "%d",
                                 ImGuiSliderFlags_Logarithmic))
                layer.budgetPoolPoints = poolM * 1000000;
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("GPU pool capacity (~16 B/point). Changing "
                                  "it drops residency and re-streams.");

            // Colorization: modes without data are disabled.
            static const char* kModeNames[] = { "RGB", "Intensity ramp",
                                                "Classification", "Elevation" };
            const bool modeAvailable[4] = { layer.hasRgbColumn,
                                            layer.hasIntensityColumn,
                                            layer.hasClassColumn, true };
            int mode = static_cast<int>(layer.pointColorMode);
            ImGui::SetNextItemWidth(160.0f);
            if (ImGui::BeginCombo("Color by", kModeNames[mode])) {
                for (int m = 0; m < 4; ++m) {
                    if (!modeAvailable[m])
                        continue;
                    if (ImGui::Selectable(kModeNames[m], m == mode) && m != mode) {
                        layer.pointColorMode =
                            static_cast<scene::I3SSceneLayer::PointColorMode>(m);
                        layer.markPointColorsDirty();
                    }
                }
                ImGui::EndCombo();
            }
            using Mode = scene::I3SSceneLayer::PointColorMode;
            if (layer.pointColorMode == Mode::Intensity) {
                float bounds[2] = { layer.intensityRampMin,
                                    layer.intensityRampMax };
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::DragFloat2("Intensity range", bounds, 1.0f)) {
                    layer.intensityRampMin = bounds[0];
                    layer.intensityRampMax = std::max(bounds[1], bounds[0] + 1.0f);
                    layer.markPointColorsDirty();
                }
            }
            if (layer.pointColorMode == Mode::Elevation) {
                float bounds[2] = { layer.elevationRampMin,
                                    layer.elevationRampMax };
                ImGui::SetNextItemWidth(160.0f);
                if (ImGui::DragFloat2("Elevation range (m)", bounds, 0.5f)) {
                    layer.elevationRampMin = bounds[0];
                    layer.elevationRampMax = std::max(bounds[1], bounds[0] + 0.1f);
                    layer.markPointColorsDirty();
                }
            }
            if (layer.pointColorMode == Mode::Classification &&
                ImGui::TreeNode("Palette")) {
                // Classes actually present (statistics) first; fall back to
                // the common 0..18 LAS range when no statistics shipped.
                std::vector<int> values = layer.classStats.presentValues;
                if (values.empty())
                    for (int v = 0; v <= 18; ++v)
                        values.push_back(v);
                for (int value : values) {
                    if (value < 0 || value > 255)
                        continue;
                    ImGui::PushID(value);
                    if (ImGui::ColorEdit3("##class",
                                          &layer.classPalette[value].x,
                                          ImGuiColorEditFlags_NoInputs))
                        layer.markPointColorsDirty();
                    ImGui::SameLine();
                    const char* label = nullptr;
                    for (const auto& entry : layer.classStats.classLabels)
                        if (entry.first == value)
                            label = entry.second.c_str();
                    ImGui::Text("%d%s%s", value, label ? " - " : "",
                                label ? label : "");
                    ImGui::PopID();
                }
                if (ImGui::SmallButton("Reset to defaults")) {
                    std::memcpy(layer.classPalette,
                                scene::I3SSceneLayer::defaultClassPalette(),
                                sizeof(layer.classPalette));
                    layer.markPointColorsDirty();
                }
                ImGui::TreePop();
            }

            const scene::I3SSceneLayer::Stats stats = layer.stats();
            ImGui::Text("%u drawn | %u resident | %u staging | %u ready",
                        stats.drawnLastFrame, stats.resident, stats.staging,
                        stats.ready);
            ImGui::Text("%u queued | %u decoding | %u failed", stats.queued,
                        stats.decoding, stats.failed);
            ImGui::Text("pool %.1f / %.0f M pts (%.0f MB VRAM) | %.1f M resident | %u batches",
                        double(stats.poolPointsUsed) / 1e6,
                        double(stats.poolPointsCapacity) / 1e6,
                        double(stats.poolPointsCapacity) * 16.0 / (1024.0 * 1024.0),
                        double(stats.residentPoints) / 1e6, stats.drawnBatches);
            ImGui::Text("decode %.1f MB/s | upload %.1f MB/s | CPU cache %.1f MB",
                        double(stats.decodeRateMBs), double(stats.uploadRateMBs),
                        double(stats.cpuCacheBytes) / (1024.0 * 1024.0));
            ImGui::Text("evicted %u | cancelled %u | recolor pending %u",
                        stats.evicted, stats.cancelled, stats.recolorPending);
            ImGui::Text("ring %.1f / %.0f MB | VRAM %llu / %llu MB",
                        services.uploadRingUsedMB(), services.uploadRingCapacityMB(),
                        static_cast<unsigned long long>(stats.deviceUsageMB),
                        static_cast<unsigned long long>(stats.deviceBudgetMB));
        }
        if (layer.pickedNode >= 0 &&
            layer.pickedNode < int(layer.tree.nodes.size())) {
            const i3s::NodeInfo& picked = layer.tree.nodes[layer.pickedNode];
            ImGui::SeparatorText("Picked node");
            ImGui::Text("node %d, level %u, %llu points", layer.pickedNode,
                        unsigned(picked.level),
                        static_cast<unsigned long long>(picked.mesh.vertexCount));
        }
    }

    // ---- geometry rendering + streaming HUD (M1/M2) ----
    if (layer.rendersGeometry()) {
        ImGui::SeparatorText("Geometry");
        ImGui::Checkbox("Render geometry", &layer.showGeometry);
        if (layer.showGeometry) {
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderFloat("Quality", &layer.lodScale, 0.1f, 4.0f, "%.2fx",
                               ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Screen-space-error scale: higher = finer LOD "
                                  "cut, more nodes resident.");
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderInt("Node budget", &layer.budgetMaxNodes, 64, 32768, "%d",
                             ImGuiSliderFlags_Logarithmic);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderInt("VRAM budget (MB)", &layer.budgetGpuMB, 128, 8192,
                             "%d", ImGuiSliderFlags_Logarithmic);
            ImGui::SetNextItemWidth(160.0f);
            ImGui::SliderInt("CPU cache (MB)", &layer.budgetCpuMB, 64, 4096, "%d",
                             ImGuiSliderFlags_Logarithmic);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Decoded payloads kept for re-upload without "
                                  "re-decoding (cancelled/evicted nodes).");
            ImGui::Checkbox("Prefetch", &layer.prefetch);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Warm one LOD past the cut along the camera "
                                  "velocity (low priority).");

            const scene::I3SSceneLayer::Stats stats = layer.stats();
            ImGui::Text("%u drawn | %u resident | %u staging | %u ready",
                        stats.drawnLastFrame, stats.resident, stats.staging,
                        stats.ready);
            ImGui::Text("%u queued | %u decoding | %u failed", stats.queued,
                        stats.decoding, stats.failed);
            ImGui::Text("GPU %.1f MB | CPU cache %.1f MB",
                        double(stats.gpuBytes) / (1024.0 * 1024.0),
                        double(stats.cpuCacheBytes) / (1024.0 * 1024.0));
            ImGui::Text("decode %.1f MB/s | upload %.1f MB/s",
                        double(stats.decodeRateMBs), double(stats.uploadRateMBs));
            ImGui::Text("evicted %u | cancelled %u | graveyard %u", stats.evicted,
                        stats.cancelled, stats.graveyard);
            ImGui::Text("ring %.1f / %.0f MB | VRAM %llu / %llu MB",
                        services.uploadRingUsedMB(), services.uploadRingCapacityMB(),
                        static_cast<unsigned long long>(stats.deviceUsageMB),
                        static_cast<unsigned long long>(stats.deviceBudgetMB));
        }
        if (layer.pickedNode >= 0 &&
            layer.pickedNode < int(layer.tree.nodes.size())) {
            const i3s::NodeInfo& picked = layer.tree.nodes[layer.pickedNode];
            ImGui::SeparatorText("Picked node");
            ImGui::Text("node %d, level %u", layer.pickedNode, unsigned(picked.level));
            ImGui::Text("%llu vertices, %llu features",
                        static_cast<unsigned long long>(picked.mesh.vertexCount),
                        static_cast<unsigned long long>(picked.mesh.featureCount));
            if (!picked.v16Id.empty())
                ImGui::Text("id: %s", picked.v16Id.c_str());
            drawPickedFeature(layer); // M4: feature + attribute row
        }
    }

    // ---- M4 tool sections (inspector, slice preset, daylight) ----
    drawInspectorSection(services, layer);
    drawSliceSection(services, layer);
    drawDaylightSection(services, layer);

    // ---- bounding-volume display (the M0 inspector view) ----
    ImGui::SeparatorText("Bounding volumes");
    ImGui::Checkbox("Show OBBs", &layer.showObbs);
    if (layer.showObbs) {
        const int maxLevel = std::max(0, int(tree.levelCount) - 1);
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderInt("Tree level", &layer.obbLevel, 0, maxLevel);
        layer.obbLevel = std::min(std::max(layer.obbLevel, 0), maxLevel);
        ImGui::SameLine();
        ImGui::Checkbox("Cumulative", &layer.obbUpToLevel);
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("On: draw levels 0..N (colored by level).\n"
                              "Off: draw only level N.");
        ImGui::SetNextItemWidth(160.0f);
        ImGui::SliderInt("Box cap", &layer.obbMaxBoxes, 256, 20000, "%d",
                         ImGuiSliderFlags_Logarithmic);
        const size_t filtered = layer.countFilteredBoxes();
        if (filtered > static_cast<size_t>(layer.obbMaxBoxes))
            ImGui::Text("%zu boxes pass the filter, drawing %d (cap)", filtered,
                        layer.obbMaxBoxes);
        else
            ImGui::Text("%zu boxes drawn", filtered);
    }

    // ---- node distribution ----
    if (ImGui::TreeNode("Nodes per level")) {
        for (size_t level = 0; level < tree.levelNodeCounts.size(); ++level)
            ImGui::Text("level %zu: %u nodes", level, tree.levelNodeCounts[level]);
        size_t withGeometry = 0;
        for (const i3s::NodeInfo& n : tree.nodes)
            if (n.mesh.hasGeometry)
                ++withGeometry;
        ImGui::Text("%zu nodes carry geometry", withGeometry);
        ImGui::TreePop();
    }

    // ---- definitions ----
    if (!info.attributeFields.empty() &&
        ImGui::TreeNode("Attributes", "Attributes (%zu)",
                        info.attributeFields.size())) {
        for (const i3s::AttributeField& f : info.attributeFields)
            ImGui::Text("%-4s %s  (%s)", f.key.c_str(), f.name.c_str(),
                        f.valueType.empty() ? "?" : f.valueType.c_str());
        ImGui::TreePop();
    }
    if (ImGui::TreeNode("Resources")) {
        for (size_t g = 0; g < info.geometryDefs.size(); ++g) {
            const i3s::GeometryDefinition& def = info.geometryDefs[g];
            std::string buffers;
            for (const i3s::GeometryBufferDesc& b : def.buffers) {
                if (!buffers.empty())
                    buffers += " | ";
                if (b.compressed) {
                    buffers += "draco(";
                    for (size_t a = 0; a < b.compressedAttributes.size(); ++a) {
                        if (a)
                            buffers += ",";
                        buffers += b.compressedAttributes[a];
                    }
                    buffers += ")";
                } else {
                    buffers += "raw(";
                    bool first = true;
                    for (const i3s::GeometryStream& s : b.streams) {
                        if (!first)
                            buffers += ",";
                        first = false;
                        switch (s.semantic) {
                        case i3s::VertexSemantic::Position: buffers += "pos"; break;
                        case i3s::VertexSemantic::Normal: buffers += "normal"; break;
                        case i3s::VertexSemantic::Uv0: buffers += "uv0"; break;
                        case i3s::VertexSemantic::Color: buffers += "color"; break;
                        case i3s::VertexSemantic::UvRegion: buffers += "uvRegion"; break;
                        case i3s::VertexSemantic::FeatureId: buffers += "featureId"; break;
                        case i3s::VertexSemantic::FaceRange: buffers += "faceRange"; break;
                        default: buffers += "?"; break;
                        }
                    }
                    buffers += ")";
                }
            }
            ImGui::Text("geometry %zu: %s", g, buffers.c_str());
        }
        for (size_t t = 0; t < info.textureSets.size(); ++t) {
            const i3s::TextureSetDefinition& set = info.textureSets[t];
            std::string formats;
            for (const i3s::TextureFormat& f : set.formats) {
                if (!formats.empty())
                    formats += ", ";
                formats += f.name.empty() ? f.format : (f.name + "." + f.format);
            }
            ImGui::Text("textureSet %zu: %s%s", t, formats.c_str(),
                        set.atlas ? " (atlas)" : "");
        }
        if (!info.textureEncodingSummary.empty())
            ImGui::Text("textures (1.6): %s", info.textureEncodingSummary.c_str());
        ImGui::Text("%zu material definition(s)%s", info.materials.size(),
                    info.hasStatistics ? ", statistics present" : "");
        ImGui::TreePop();
    }

}

} // namespace

// SLPK / I3S scene layers: open packages, walk the node hierarchy, and show
// the bounding-volume cloud through the overlay renderer (inspector v0 —
// docs/SLPK_IMPLEMENTATION_PLAN.md M0; grows into the full inspector in M4).
void drawSlpkPanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::Slpk, open)) {
        ImGui::End();
        return;
    }

    if (ImGui::Button("Open SLPK..."))
        services.openSlpkDialog();
    ImGui::SameLine();
    ImGui::TextDisabled("(or drop .slpk files on the window)");

    const size_t inFlight = services.slpkLoadsInFlight();
    if (inFlight > 0)
        ImGui::Text("Opening %zu package(s)...", inFlight);

    // Per-frame pump budgets, shared across all layers (plan §6.5).
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderFloat("Pump ms/frame", &scene::I3SSceneLayer::sPumpBudgetMs, 0.5f,
                       10.0f, "%.1f");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(140.0f);
    ImGui::SliderInt("MB/frame", &scene::I3SSceneLayer::sPumpStageBudgetMB, 4, 128,
                     "%d", ImGuiSliderFlags_Logarithmic);

    ImGui::SeparatorText("Layers");
    scene::Scene& scene = services.scene();
    if (scene.i3sLayers.empty() && inFlight == 0)
        ImGui::TextDisabled("(no scene layers loaded)");

    for (size_t i = 0; i < scene.i3sLayers.size(); ++i) {
        if (!scene.i3sLayers[i])
            continue;
        ImGui::PushID(static_cast<int>(i));
        const size_t before = scene.i3sLayers.size();
        drawLayerInspector(services, *scene.i3sLayers[i], i);
        ImGui::PopID();
        if (scene.i3sLayers.size() != before)
            break; // a layer was unloaded; indices are stale this frame
        ImGui::Separator();
    }

    ImGui::End();
}

} // namespace Gui
