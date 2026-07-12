#include "Gui/Panels.h"

#include "Gui/Inspector.h"
#include "Gui/Services.h"
#include "Scene/Scene.h"

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui.h"

#include <glm/glm.hpp>

#include <cfloat>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

// The Inspector panel shell (UI redesign Pass 2 §8): drives the current
// selection into the per-kind editor registry (Gui/Inspector.h). Layout is
// header card -> tool card (stub until Pass 7) -> per-kind sections -> global
// cards -> empty state — the editors themselves come from the registry, so a
// new object kind plugs in without touching this file (contract C6).

namespace Gui {

namespace {

using Kind = scene::SceneItemRef::Kind;

UiKit::ObjectKind toObjectKind(Kind k) {
    switch (k) {
    case Kind::Model: return UiKit::ObjectKind::Model;
    case Kind::Mesh: return UiKit::ObjectKind::Mesh;
    case Kind::PointCloud: return UiKit::ObjectKind::PointCloud;
    case Kind::SceneLayer: return UiKit::ObjectKind::SceneLayer;
    case Kind::Sun: return UiKit::ObjectKind::Sun;
    case Kind::PointLight: return UiKit::ObjectKind::PointLight;
    case Kind::SpotLight: return UiKit::ObjectKind::SpotLight;
    case Kind::Measurement: return UiKit::ObjectKind::Measurement;
    case Kind::ClipPlane: return UiKit::ObjectKind::ClipPlane;
    case Kind::Group: return UiKit::ObjectKind::Group;
    case Kind::Environment: return UiKit::ObjectKind::Environment;
    default: return UiKit::ObjectKind::Model;
    }
}

bool renameable(Kind k) {
    switch (k) {
    case Kind::Model:
    case Kind::PointCloud:
    case Kind::PointLight:
    case Kind::SceneLayer:
    case Kind::Measurement:
    case Kind::ClipPlane:
    case Kind::Group:
        return true;
    default:
        return false;
    }
}

std::string displayName(scene::Scene& s, const scene::SceneItemRef& r) {
    switch (r.kind) {
    case Kind::Model: {
        const int i = s.modelIndexOf(r.id);
        return (i >= 0 && !s.models[size_t(i)].name.empty()) ? s.models[size_t(i)].name
                                                             : "Model";
    }
    case Kind::Mesh: {
        const int i = s.modelIndexOf(r.id);
        std::string base = (i >= 0) ? s.models[size_t(i)].name : "Model";
        return base + "  \xC2\xB7  mesh " + std::to_string(r.sub);
    }
    case Kind::PointCloud: {
        const int i = s.pointCloudIndexOf(r.id);
        return (i >= 0 && !s.pointClouds[size_t(i)].name.empty())
                   ? s.pointClouds[size_t(i)].name
                   : "Point cloud";
    }
    case Kind::PointLight: {
        const int i = s.lightIndexOf(r.id);
        return (i >= 0 && !s.pointLights[size_t(i)].name.empty())
                   ? s.pointLights[size_t(i)].name
                   : "Point light";
    }
    case Kind::SceneLayer: {
        const int i = s.layerIndexOf(r.id);
        return (i >= 0 && s.i3sLayers[size_t(i)] && !s.i3sLayers[size_t(i)]->name.empty())
                   ? s.i3sLayers[size_t(i)]->name
                   : "Scene layer";
    }
    case Kind::Measurement: {
        const int i = s.measurementIndexOf(r.id);
        return (i >= 0 && !s.measurements[size_t(i)].name.empty())
                   ? s.measurements[size_t(i)].name
                   : "Measurement";
    }
    case Kind::ClipPlane: {
        const int i = s.clipPlaneIndexOf(r.id);
        return (i >= 0 && !s.clipPlanes[size_t(i)].name.empty())
                   ? s.clipPlanes[size_t(i)].name
                   : "Clip plane";
    }
    case Kind::Group: {
        const scene::Group* g = s.findGroup(r.id);
        return (g && !g->name.empty()) ? g->name : "Group";
    }
    case Kind::Sun: return "Sun";
    case Kind::Environment: return "Sky";
    default: return "Object";
    }
}

// Deep-link into the Settings window (Pass 6 grows it; today: reveal + focus).
void openInSettings(Services& services) {
    services.settings().ui.panels.settings = true;
    ImGui::SetWindowFocus(Windows::Settings);
}

// Header card: kind icon, editable name (single selection), kind badge, mixed
// summary and quick actions over the whole selection.
void drawHeader(Services& services, scene::Selection& selection) {
    scene::Scene& scene = services.scene();
    const std::vector<scene::SceneItemRef>& items = selection.items();
    const scene::SceneItemRef primary = selection.primary();
    const UiKit::KindStyle style = UiKit::StyleFor(toObjectKind(primary.kind));

    UiKit::BeginCard("inspectorHeader");
    UiKit::InlineIcon(style.icon, style.color);
    ImGui::SameLine();

    if (items.size() == 1 && renameable(primary.kind)) {
        static char nameBuf[256] = "";
        static uint64_t nameFor = 0;
        static Kind nameKind = Kind::None;
        if (nameFor != primary.id || nameKind != primary.kind) {
            const std::string cur = displayName(scene, primary);
            size_t n = cur.size();
            if (n > sizeof(nameBuf) - 1)
                n = sizeof(nameBuf) - 1;
            std::memcpy(nameBuf, cur.c_str(), n);
            nameBuf[n] = '\0';
            nameFor = primary.id;
            nameKind = primary.kind;
        }
        ImGui::SetNextItemWidth(-1.0f);
        if (ImGui::InputText("##name", nameBuf, sizeof(nameBuf),
                             ImGuiInputTextFlags_EnterReturnsTrue))
            services.renameItem(primary, nameBuf);
        if (ImGui::IsItemDeactivatedAfterEdit())
            services.renameItem(primary, nameBuf);
    } else if (items.size() == 1) {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(displayName(scene, primary).c_str());
    } else {
        // "N objects (2 Models, 1 Light)" — count by kind.
        int counts[int(UiKit::ObjectKind::Count)] = { 0 };
        for (const scene::SceneItemRef& r : items)
            ++counts[int(toObjectKind(r.kind))];
        std::string summary = std::to_string(items.size()) + " objects (";
        bool first = true;
        for (int k = 0; k < int(UiKit::ObjectKind::Count); ++k) {
            if (!counts[k])
                continue;
            const UiKit::KindStyle ks = UiKit::StyleFor(UiKit::ObjectKind(k));
            if (!first)
                summary += ", ";
            summary += std::to_string(counts[k]) + " " + ks.noun +
                       (counts[k] > 1 ? "s" : "");
            first = false;
        }
        summary += ")";
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(summary.c_str());
    }

    // Kind badge + quick actions.
    UiKit::Badge(style.noun, style.color);
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(UiKit::Space(4), 0));
    ImGui::SameLine();
    const std::vector<scene::SceneItemRef> refs = items; // batch = whole selection
    if (UiKit::IconButton("frame", ICON_FA_CROSSHAIRS, "Frame (F)"))
        services.frameItems(refs);
    ImGui::SameLine();
    if (UiKit::IconButton("isolate", ICON_FA_EYE, "Isolate"))
        services.isolateItems(refs);
    ImGui::SameLine();
    if (UiKit::IconButton("dup", ICON_FA_CLONE, "Duplicate (Ctrl+D)"))
        services.duplicateItems(refs);
    ImGui::SameLine();
    if (UiKit::IconButton("hide", ICON_FA_EYE_SLASH, "Hide"))
        services.setItemsVisible(refs, false);
    ImGui::SameLine();
    if (UiKit::IconButton("del", ICON_FA_TRASH, "Delete"))
        services.deleteItems(refs);
    UiKit::EndCard();
}

// Mixed-kind selection: header + a relative Position nudge applied to every
// transformable object as one undo step (§8).
glm::vec3* mixedPos(scene::Scene& s, const scene::SceneItemRef& r) {
    switch (r.kind) {
    case Kind::Model: {
        const int i = s.modelIndexOf(r.id);
        return i >= 0 ? &s.models[size_t(i)].position : nullptr;
    }
    case Kind::PointCloud: {
        const int i = s.pointCloudIndexOf(r.id);
        return i >= 0 ? &s.pointClouds[size_t(i)].position : nullptr;
    }
    case Kind::PointLight: {
        const int i = s.lightIndexOf(r.id);
        return i >= 0 ? &s.pointLights[size_t(i)].position : nullptr;
    }
    default:
        return nullptr;
    }
}

void drawMixed(Services& services, scene::Selection& selection) {
    ImGui::TextDisabled("Multiple object types selected.");
    if (!ImGui::CollapsingHeader("Relative transform", ImGuiTreeNodeFlags_DefaultOpen))
        return;
    scene::Scene& scene = services.scene();
    const std::vector<scene::SceneItemRef> refs = selection.items();

    // Per-frame delta nudge; captures the pre-gesture snapshot on the first
    // changed frame and records one undo once nothing is active (DragFloat3's
    // per-item activation flags are unreliable — see EditRow in Inspector.h).
    static std::vector<std::pair<scene::SceneItemRef, glm::vec3>> base;
    static bool gesture = false;
    UiKit::PropertyRow("Move");
    glm::vec3 delta(0.0f);
    const bool changed = ImGui::DragFloat3("##mv", &delta.x, 0.05f);
    if (changed && !gesture) {
        base.clear();
        for (const scene::SceneItemRef& r : refs)
            if (glm::vec3* p = mixedPos(scene, r))
                base.push_back({ r, *p });
        gesture = true;
    }
    if (changed) {
        for (const scene::SceneItemRef& r : refs)
            if (glm::vec3* p = mixedPos(scene, r))
                *p += delta;
        scene.computeWorldBounds();
    }
    if (gesture && !ImGui::IsAnyItemActive()) {
        gesture = false;
        std::vector<std::pair<scene::SceneItemRef, glm::vec3>> after;
        for (const scene::SceneItemRef& r : refs)
            if (glm::vec3* p = mixedPos(scene, r))
                after.push_back({ r, *p });
        Services* svc = &services;
        std::vector<std::pair<scene::SceneItemRef, glm::vec3>> beforeSnap = base;
        bool moved = false;
        for (size_t i = 0; i < beforeSnap.size() && i < after.size(); ++i)
            if (beforeSnap[i].second != after[i].second) {
                moved = true;
                break;
            }
        if (moved)
            services.undo().record(
                "Move objects",
                [svc, beforeSnap]() {
                    for (const std::pair<scene::SceneItemRef, glm::vec3>& c : beforeSnap)
                        if (glm::vec3* p = mixedPos(svc->scene(), c.first))
                            *p = c.second;
                    svc->scene().computeWorldBounds();
                },
                [svc, after]() {
                    for (const std::pair<scene::SceneItemRef, glm::vec3>& c : after)
                        if (glm::vec3* p = mixedPos(svc->scene(), c.first))
                            *p = c.second;
                    svc->scene().computeWorldBounds();
                });
    }
}

// Global (non-per-object) cards shown in per-object contexts (§8): tinted,
// tagged GLOBAL, deep-linking into Settings.
void drawGlobalCards(Services& services, const std::vector<Kind>& kinds) {
    auto hasKind = [&](Kind k) {
        for (Kind x : kinds)
            if (x == k)
                return true;
        return false;
    };

    if (hasKind(Kind::PointCloud)) {
        UiKit::BeginGlobalCard("gcPointCloud");
        UiKit::SectionHeader("Point cloud rendering");
        Settings::PointCloud& pc = services.settings().pointCloud;
        ImGui::Checkbox("High-quality shading", &pc.hqs);
        ImGui::Checkbox("Density LOD", &pc.lodEnabled);
        if (pc.lodEnabled) {
            ImGui::SetNextItemWidth(140.0f * UiKit::Scale());
            ImGui::SliderFloat("Points / pixel", &pc.lodPointsPerPixel, 0.25f, 8.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic);
        }
        if (ImGui::SmallButton("Open in Settings \xE2\x86\x92"))
            openInSettings(services);
        UiKit::EndCard();
    }

    if (hasKind(Kind::Sun)) {
        UiKit::BeginGlobalCard("gcLighting");
        UiKit::SectionHeader("Lighting");
        Settings::Lighting& lt = services.settings().lighting;
        ImGui::Checkbox("Shadows", &lt.shadows);
        if (lt.shadows) {
            ImGui::SameLine();
            ImGui::Checkbox("Soft (PCSS)", &lt.softShadows);
        }
        ImGui::SetNextItemWidth(140.0f * UiKit::Scale());
        ImGui::SliderFloat("Ambient", &lt.ambient, 0.0f, 0.5f, "%.3f");
        if (ImGui::SmallButton("Open in Settings \xE2\x86\x92"))
            openInSettings(services);
        UiKit::EndCard();
    }
}

} // namespace

void drawInspectorPanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::Inspector, open)) {
        ImGui::End();
        return;
    }

    scene::Selection& selection = services.selection();
    const std::vector<scene::SceneItemRef>& items = selection.items();

    if (items.empty()) {
        UiKit::EmptyState(ICON_FA_MOUSE_POINTER, "Nothing selected",
                          "Select an object in the viewport or the Scene panel to "
                          "edit it here.");
        ImGui::Spacing();
        if (ImGui::Button("Import model\xE2\x80\xA6", ImVec2(-FLT_MIN, 0.0f)))
            services.importModelDialog();
        if (ImGui::Button("Open point cloud\xE2\x80\xA6", ImVec2(-FLT_MIN, 0.0f)))
            services.openPointCloudDialog();
        if (ImGui::Button("Open scene layer (SLPK)\xE2\x80\xA6", ImVec2(-FLT_MIN, 0.0f)))
            services.openSlpkDialog();
        ImGui::End();
        return;
    }

    // Header card + tool card stub.
    drawHeader(services, selection);
    // Tool card: options for the active tool land here once Pass 7's ToolManager
    // and the onRenderInspector plugin hook exist. (Intentionally empty stub.)

    // Determine the set of selected kinds.
    std::vector<Kind> kinds;
    bool singleKind = true;
    const Kind first = items.front().kind;
    for (const scene::SceneItemRef& r : items) {
        if (r.kind != first)
            singleKind = false;
        bool seen = false;
        for (Kind k : kinds)
            if (k == r.kind) {
                seen = true;
                break;
            }
        if (!seen)
            kinds.push_back(r.kind);
    }

    if (singleKind) {
        const Kind kind = first;
        if (const Inspector::Editor* editor = Inspector::editorFor(kind)) {
            Inspector::EditContext ctx{ services, kind, items };
            (*editor)(ctx);
        } else {
            ImGui::TextDisabled("No editor for this object type yet.");
        }
    } else {
        drawMixed(services, selection);
    }

    drawGlobalCards(services, kinds);
    ImGui::End();
}

} // namespace Gui
