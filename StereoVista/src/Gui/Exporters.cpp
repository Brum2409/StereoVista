#include "Gui/Exporters.h"

#include "Gui/Services.h"
#include "Gui/UiKit.h"
#include "Scene/Scene.h"

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui.h"

#include <string>
#include <vector>

// The export registry + its built-ins (see Gui/Exporters.h). Each exporter is a
// title, an applicability test and a call into the Services executor that
// already exists — which is exactly what makes adding the next one (a mesh
// format, a stereo screenshot mode, a web package) a single registration.

namespace Gui {
namespace Exporters {

namespace {

struct Registry {
    std::vector<Exporter> exporters;
    bool builtinsRegistered = false;
};

Registry& registry() {
    static Registry r;
    return r;
}

using Kind = scene::SceneItemRef::Kind;

// Index of the first selected point cloud, or -1.
int firstPointCloud(Services& services,
                    const std::vector<scene::SceneItemRef>& refs) {
    scene::Scene& scene = services.scene();
    for (const scene::SceneItemRef& ref : refs)
        if (ref.kind == Kind::PointCloud) {
            const int index = scene.pointCloudIndexOf(ref.id);
            if (index >= 0)
                return index;
        }
    return -1;
}

void registerBuiltins() {
    {
        Exporter e;
        e.id = "export.screenshot";
        e.title = "Screenshot";
        e.description = "Save the current view as a PNG.";
        e.icon = ICON_FA_CAMERA;
        e.applies = [](Services&, const std::vector<scene::SceneItemRef>&) {
            return true; // never needs a selection
        };
        e.run = [](Services& services, const std::vector<scene::SceneItemRef>&) {
            services.requestScreenshot();
        };
        registerExporter(std::move(e));
    }
    {
        Exporter e;
        e.id = "export.scene";
        e.title = "Scene (.scene)\xE2\x80\xA6";
        e.description = "Save the whole scene as a portable v3 document.";
        e.icon = ICON_FA_SAVE;
        e.applies = [](Services&, const std::vector<scene::SceneItemRef>&) {
            return true;
        };
        e.run = [](Services& services, const std::vector<scene::SceneItemRef>&) {
            services.saveSceneAs();
        };
        registerExporter(std::move(e));
    }
    {
        Exporter e;
        e.id = "export.pointcloud";
        e.title = "Point cloud\xE2\x80\xA6";
        e.description = "Write the selected cloud as XYZ, PCB, HDF5 or PLY.";
        e.icon = ICON_FA_CLOUD;
        e.applies = [](Services& services,
                       const std::vector<scene::SceneItemRef>& refs) {
            return firstPointCloud(services, refs) >= 0;
        };
        e.run = [](Services& services, const std::vector<scene::SceneItemRef>& refs) {
            const int index = firstPointCloud(services, refs);
            if (index < 0)
                return;
            // Format/options live in the Inspector's Export section; the dialog
            // here uses the interchange default (binary PLY, transform baked).
            services.exportPointCloud(size_t(index), /*format=*/3,
                                      /*applyTransform=*/true, /*plyBinary=*/true);
        };
        registerExporter(std::move(e));
    }
}

} // namespace

void registerExporter(Exporter exporter) {
    if (exporter.id.empty() || !exporter.run)
        return;
    registry().exporters.push_back(std::move(exporter));
}

void collect(Services& services, const std::vector<scene::SceneItemRef>& refs,
             std::vector<const Exporter*>& out) {
    Registry& r = registry();
    if (!r.builtinsRegistered) {
        r.builtinsRegistered = true; // set FIRST: registerBuiltins re-enters
        registerBuiltins();
    }
    for (const Exporter& exporter : r.exporters)
        if (!exporter.applies || exporter.applies(services, refs))
            out.push_back(&exporter);
}

void drawExportDialog(Services& services, bool* open) {
    if (!open || !*open)
        return;

    ImGui::OpenPopup("Export");
    ImGui::SetNextWindowSize(ImVec2(420.0f * UiKit::Scale(), 0.0f), ImGuiCond_Always);
    if (!ImGui::BeginPopupModal("Export", open, ImGuiWindowFlags_AlwaysAutoResize))
        return;

    const std::vector<scene::SceneItemRef> refs = services.selection().items();
    std::vector<const Exporter*> available;
    collect(services, refs, available);

    if (refs.empty())
        ImGui::TextDisabled("Nothing selected — showing exporters that don't need "
                            "a selection.");
    else
        ImGui::TextDisabled("%zu object(s) selected.", refs.size());
    ImGui::Separator();

    for (const Exporter* exporter : available) {
        ImGui::PushID(exporter->id.c_str());
        UiKit::InlineIcon(exporter->icon ? exporter->icon : ICON_FA_FILE_EXPORT,
                          UiKit::Color(UiKit::Semantic::Accent));
        ImGui::SameLine();
        if (ImGui::Selectable(exporter->title.c_str())) {
            exporter->run(services, refs);
            *open = false;
            ImGui::CloseCurrentPopup();
        }
        if (!exporter->description.empty())
            ImGui::TextDisabled("    %s", exporter->description.c_str());
        ImGui::PopID();
    }

    ImGui::Separator();
    if (ImGui::Button("Cancel", ImVec2(120.0f, 0.0f))) {
        *open = false;
        ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
}

} // namespace Exporters
} // namespace Gui
