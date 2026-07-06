#include "Gui/Panels.h"
#include "Gui/Services.h"
#include "Renderer/GpuTypes.h" // renderer::gpu::MaterialData (glm-only, no Vulkan)
#include "Scene/Scene.h"

#include "imgui/imgui.h"

#include <glm/glm.hpp>

#include <string>

namespace Gui {

// Object / material inspector for the current selection. Transform widgets bind
// DIRECTLY to the live scene::Model fields (the same ones the gizmo edits), so
// there is no stale-cache Euler bug like the GL panel had — dragging the gizmo
// updates these numbers and vice-versa.
void drawInspectorPanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::Inspector, open)) {
        ImGui::End();
        return;
    }

    scene::Scene& scene = services.scene();
    const int modelIdx = services.selectedModel();
    if (modelIdx < 0 || modelIdx >= static_cast<int>(scene.models.size())) {
        ImGui::TextDisabled("No selection.");
        ImGui::TextWrapped("Click a model in the viewport or the Scene panel. "
                           "Click again to drill into a sub-mesh.");
        ImGui::End();
        return;
    }

    scene::Model& model = scene.models[modelIdx];
    const int meshIdx = services.selectedMesh();

    ImGui::TextUnformatted(model.name.empty() ? "(model)" : model.name.c_str());
    if (meshIdx >= 0 && meshIdx < static_cast<int>(model.meshes.size()))
        ImGui::TextDisabled("Sub-mesh: %s",
                            model.meshes[meshIdx].name.empty()
                                ? "(unnamed)"
                                : model.meshes[meshIdx].name.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Focus"))
        services.focusCameraOn(modelIdx);

    ImGui::Checkbox("Visible", &model.visible);

    // ── Transform (live-bound; edits also move the gizmo) ───────────────────
    ImGui::SeparatorText("Transform");
    ImGui::DragFloat3("Position", &model.position.x, 0.01f);
    ImGui::DragFloat3("Rotation", &model.rotationDeg.x, 0.5f, 0.0f, 0.0f, "%.1f deg");
    ImGui::DragFloat3("Scale", &model.scale.x, 0.01f, 0.0001f, 10000.0f);
    if (ImGui::SmallButton("Reset transform")) {
        model.position = glm::vec3(0.0f);
        model.rotationDeg = glm::vec3(0.0f);
        model.scale = glm::vec3(1.0f);
    }

    // ── Gizmo controls (mirrors the 1/2/3 hotkeys) ──────────────────────────
    ImGui::SeparatorText("Gizmo");
    bool gizmoOn = services.gizmoEnabled();
    if (ImGui::Checkbox("Enabled", &gizmoOn))
        services.setGizmoEnabled(gizmoOn);
    if (gizmoOn) {
        int mode = services.gizmoMode();
        const char* modes[] = { "Translate (1)", "Rotate (2)", "Scale (3)" };
        ImGui::SetNextItemWidth(160.0f);
        if (ImGui::Combo("Mode", &mode, modes, 3))
            services.setGizmoMode(mode);
        bool localSpace = services.gizmoLocalSpace();
        if (ImGui::Checkbox("Local space", &localSpace))
            services.setGizmoLocalSpace(localSpace);
        ImGui::SameLine();
        bool snap = services.gizmoSnap();
        if (ImGui::Checkbox("Snap (Shift)", &snap))
            services.setGizmoSnap(snap);
    }

    // ── Material(s) ─────────────────────────────────────────────────────────
    ImGui::SeparatorText("Material");
    auto editMaterial = [&](int mesh) {
        renderer::gpu::MaterialData* mat = services.materialForMesh(modelIdx, mesh);
        if (!mat) {
            ImGui::TextDisabled("(no material)");
            return;
        }
        if (mesh >= 0 && mesh < static_cast<int>(model.meshes.size()))
            ImGui::TextDisabled("bindless material #%u",
                                model.meshes[mesh].materialIndex);
        ImGui::ColorEdit3("Base color", &mat->baseColor.x);
        ImGui::SliderFloat("Metallic", &mat->metallic, 0.0f, 1.0f);
        ImGui::SliderFloat("Roughness", &mat->roughness, 0.0f, 1.0f);
        ImGui::SliderFloat("Emissive", &mat->emissive, 0.0f, 8.0f);
        ImGui::SliderFloat("Normal scale", &mat->normalScale, 0.0f, 2.0f);
    };

    if (meshIdx >= 0 && meshIdx < static_cast<int>(model.meshes.size())) {
        editMaterial(meshIdx);
    } else if (model.meshes.size() == 1) {
        editMaterial(0);
    } else if (model.meshes.empty()) {
        ImGui::TextDisabled("(no meshes)");
    } else {
        for (int j = 0; j < static_cast<int>(model.meshes.size()); ++j) {
            ImGui::PushID(j);
            const std::string& nm = model.meshes[j].name;
            const std::string header =
                nm.empty() ? ("mesh " + std::to_string(j)) : nm;
            if (ImGui::TreeNode(header.c_str()))  {
                editMaterial(j);
                ImGui::TreePop();
            }
            ImGui::PopID();
        }
        ImGui::TextDisabled("Materials are shared by index; editing one affects "
                            "every mesh that uses it.");
    }

    ImGui::End();
}

} // namespace Gui
