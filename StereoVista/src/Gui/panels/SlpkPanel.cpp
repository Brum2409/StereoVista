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
        if (!info.geometryDefs.empty()) {
            for (size_t g = 0; g < info.geometryDefs.size(); ++g) {
                const i3s::GeometryDefinition& def = info.geometryDefs[g];
                ImGui::Text("geometry %zu:%s%s%s%s%s%s%s", g,
                            def.hasCompressed ? " draco" : "",
                            def.hasRaw ? " raw" : "",
                            def.hasNormal ? " normal" : "",
                            def.hasUv0 ? " uv0" : "",
                            def.hasColor ? " color" : "",
                            def.hasUvRegion ? " uvRegion" : "",
                            def.hasFeatureId ? " featureId" : "");
            }
        } else if (!info.version.empty() && info.versionMajor == 1 &&
                   info.versionMinor <= 6) {
            ImGui::TextUnformatted("geometry: 1.6 defaultGeometrySchema");
        }
        for (size_t t = 0; t < info.textureSets.size(); ++t) {
            const i3s::TextureSetDefinition& set = info.textureSets[t];
            std::string formats;
            for (const std::string& f : set.formats) {
                if (!formats.empty())
                    formats += ", ";
                formats += f;
            }
            ImGui::Text("textureSet %zu: %s%s", t, formats.c_str(),
                        set.atlas ? " (atlas)" : "");
        }
        if (!info.textureEncodingSummary.empty())
            ImGui::Text("textures (1.6): %s", info.textureEncodingSummary.c_str());
        ImGui::Text("%zu material definition(s)%s", info.materialCount,
                    info.hasStatistics ? ", statistics present" : "");
        ImGui::TreePop();
    }

    ImGui::TextDisabled("Geometry rendering lands with milestone M1.");
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
