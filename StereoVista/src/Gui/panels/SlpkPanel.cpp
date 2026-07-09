#include "Gui/Panels.h"
#include "Gui/Services.h"

#include "Loaders/Slpk/SlpkArchive.h"
#include "Scene/Scene.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <cstddef>
#include <cstdio>

namespace Gui {

namespace {

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
        }
    }

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

    if (info.type == i3s::LayerType::PointCloud)
        ImGui::TextDisabled("Point-cloud rendering lands with milestone M3.");
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
