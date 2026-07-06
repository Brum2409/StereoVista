#include "Gui/Panels.h"
#include "Gui/Services.h"

#include "Core/Camera.h"
#include "Cursors/Base/CursorManager.h"
#include "Engine/Data.h" // Engine::ClipPlane
#include "Scene/Scene.h"
#include "Tools/ClipPlaneTool.h"

#include "imgui/imgui.h"

#include <glm/glm.hpp>

#include <vector>

namespace Gui {

// Section / clip planes. These clip both meshes and point clouds whenever
// enabled planes exist; the "editing" toggle drives the overlay + gizmo
// takeover. The active plane is edited with the transform gizmo (translate =
// slide, rotate = steer the normal).
void drawClipPlanePanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::ClipPlanes, open)) {
        ImGui::End();
        return;
    }

    Tools::ClipPlaneTool& tool = services.clipTool();

    bool enabled = tool.isEnabled();
    if (ImGui::Checkbox("Enable editing", &enabled))
        tool.setEnabled(enabled);
    ImGui::SameLine();
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Clipping applies whenever enabled planes exist; this "
                          "toggle only drives the editing overlay + gizmo takeover.");

    const glm::vec3 center =
        (services.scene().worldBoundsMin + services.scene().worldBoundsMax) * 0.5f;

    ImGui::SeparatorText("Add plane");
    if (ImGui::Button("At cursor")) {
        Camera& cam = services.camera();
        const glm::vec3 pos = services.cursors().isCursorPositionValid()
                                  ? services.cursors().getCursorPosition()
                                  : cam.Position + cam.Front * 5.0f;
        tool.addPlane(pos, -cam.Front); // faces the camera
    }
    ImGui::SameLine();
    if (ImGui::Button("+X")) tool.addAxisAlignedPlane(0, center);
    ImGui::SameLine();
    if (ImGui::Button("+Y")) tool.addAxisAlignedPlane(1, center);
    ImGui::SameLine();
    if (ImGui::Button("+Z")) tool.addAxisAlignedPlane(2, center);

    ImGui::SetNextItemWidth(140.0f);
    ImGui::DragFloat("Display size", &tool.displaySize, 0.1f, 0.2f, 50.0f, "%.1f");
    ImGui::TextDisabled("Active plane edits with the gizmo (1 move / 2 rotate).");

    ImGui::SeparatorText("Planes");
    std::vector<Engine::ClipPlane>& planes = tool.planes();
    if (planes.empty())
        ImGui::TextDisabled("(none)");
    int deleteIdx = -1;
    for (int i = 0; i < static_cast<int>(planes.size()); ++i) {
        ImGui::PushID(i);
        ImGui::Checkbox("##enabled", &planes[i].enabled);
        ImGui::SameLine();
        if (ImGui::RadioButton(planes[i].name.c_str(), tool.activeIndex() == i))
            tool.setActiveIndex(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("Flip")) tool.flipNormal(i);
        ImGui::SameLine();
        if (ImGui::SmallButton("X")) deleteIdx = i;
        ImGui::PopID();
    }
    if (deleteIdx >= 0)
        tool.deletePlane(deleteIdx);

    ImGui::End();
}

} // namespace Gui
