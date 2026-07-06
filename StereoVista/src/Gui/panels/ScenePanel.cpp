#include "Gui/Panels.h"
#include "Gui/Services.h"
#include "Scene/Scene.h"

#include "imgui/imgui.h"

#include <cstddef>
#include <string>

namespace Gui {

// Scene hierarchy: models (with sub-mesh drill-down) + point clouds. Selection
// is synced with Application's selection_ through Services, which the transform
// gizmo binds to automatically each frame — so clicking here moves the gizmo.
void drawScenePanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::Scene, open)) {
        ImGui::End();
        return;
    }

    scene::Scene& scene = services.scene();

    if (ImGui::SmallButton("Import Model"))
        services.importModelDialog();
    ImGui::SameLine();
    if (ImGui::SmallButton("Import Cloud"))
        services.openPointCloudDialog();
    ImGui::SameLine();
    if (ImGui::SmallButton("Clear Selection"))
        services.clearSelection();

    // ── Models ──────────────────────────────────────────────────────────────
    ImGui::SeparatorText("Models");
    if (scene.models.empty())
        ImGui::TextDisabled("(no models)");

    const int selModel = services.selectedModel();
    const int selMesh  = services.selectedMesh();
    int deleteModelIdx = -1;

    for (int i = 0; i < static_cast<int>(scene.models.size()); ++i) {
        scene::Model& model = scene.models[i];
        ImGui::PushID(i);

        ImGui::Checkbox("##visible", &model.visible);
        ImGui::SameLine();

        const bool isSelected = (selModel == i);
        const bool leaf = model.meshes.size() <= 1;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow |
                                   ImGuiTreeNodeFlags_SpanAvailWidth;
        if (isSelected && selMesh < 0)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (leaf)
            flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;

        const std::string label = model.name.empty()
                                      ? ("model " + std::to_string(i))
                                      : model.name;
        const bool opened = ImGui::TreeNodeEx(label.c_str(), flags);
        // Select on a label click (not on the expand arrow).
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            services.setSelection(i, -1);
        if (ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left))
            services.focusCameraOn(i);
        if (ImGui::BeginPopupContextItem()) {
            if (ImGui::MenuItem("Select"))       services.setSelection(i, -1);
            if (ImGui::MenuItem("Focus camera")) services.focusCameraOn(i);
            ImGui::Separator();
            if (ImGui::MenuItem("Delete"))       deleteModelIdx = i;
            ImGui::EndPopup();
        }

        if (opened && !leaf) {
            for (int mi = 0; mi < static_cast<int>(model.meshes.size()); ++mi) {
                ImGui::PushID(mi);
                ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf |
                                               ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                               ImGuiTreeNodeFlags_SpanAvailWidth;
                if (isSelected && selMesh == mi)
                    meshFlags |= ImGuiTreeNodeFlags_Selected;
                const std::string& nm = model.meshes[mi].name;
                const std::string meshLabel =
                    nm.empty() ? ("mesh " + std::to_string(mi)) : nm;
                ImGui::TreeNodeEx(meshLabel.c_str(), meshFlags);
                if (ImGui::IsItemClicked())
                    services.setSelection(i, mi);
                ImGui::PopID();
            }
            ImGui::TreePop();
        }
        ImGui::PopID();
    }
    if (deleteModelIdx >= 0)
        services.deleteModel(deleteModelIdx);

    // ── Point clouds ────────────────────────────────────────────────────────
    ImGui::SeparatorText("Point Clouds");
    if (services.pointCloudCount() == 0)
        ImGui::TextDisabled("(none) - File > Import Point Cloud");

    size_t unloadIdx = SIZE_MAX;
    for (size_t i = 0; i < services.pointCloudCount(); ++i) {
        ImGui::PushID(static_cast<int>(1000 + i));
        ImGui::Checkbox("##pcvisible", &services.pointCloudVisible(i));
        ImGui::SameLine();
        ImGui::TextUnformatted(services.pointCloudName(i).c_str());
        if (ImGui::BeginPopupContextItem("pcctx")) {
            if (ImGui::MenuItem("Unload"))
                unloadIdx = i;
            ImGui::EndPopup();
        }
        ImGui::PopID();
    }
    if (unloadIdx != SIZE_MAX)
        services.unloadPointCloud(unloadIdx);

    ImGui::End();
}

} // namespace Gui
