#include "Gui/Panels.h"

#include "Gui/Services.h"
#include "Gui/UiKit.h"

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

// Snapshots panel (UI redesign Pass 3 §9): named checkpoints of the camera and/
// or scene. Capture row + fuzzy search + per-snapshot cards (rename, tags,
// selective restore, delete). The native port of the GL SnapshotManager UI onto
// Gui::Services. Thumbnails/color are deferred (need renderer readback-to-ImGui)
// — cards show kind badges + metadata; note in the Status Board.

namespace Gui {

namespace {

// Split a comma-separated string into trimmed, non-empty tags.
std::vector<std::string> splitTags(const char* text) {
    std::vector<std::string> out;
    std::string cur;
    for (const char* p = text; ; ++p) {
        if (*p == ',' || *p == '\0') {
            size_t a = cur.find_first_not_of(" \t");
            size_t b = cur.find_last_not_of(" \t");
            if (a != std::string::npos)
                out.push_back(cur.substr(a, b - a + 1));
            cur.clear();
            if (*p == '\0')
                break;
        } else {
            cur.push_back(*p);
        }
    }
    return out;
}

std::string joinTags(const std::vector<std::string>& tags) {
    std::string s;
    for (size_t i = 0; i < tags.size(); ++i) {
        if (i)
            s += ", ";
        s += tags[i];
    }
    return s;
}

} // namespace

void drawSnapshotsPanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::Snapshots, open)) {
        ImGui::End();
        return;
    }

    // ── Capture row ─────────────────────────────────────────────────────────
    static char nameBuf[128] = "";
    static bool capCamera = true;
    static bool capScene = true;
    ImGui::SetNextItemWidth(-1.0f);
    ImGui::InputTextWithHint("##snapName", "Snapshot name (optional)", nameBuf,
                             sizeof(nameBuf));
    ImGui::Checkbox("Camera", &capCamera);
    ImGui::SameLine();
    ImGui::Checkbox("Scene", &capScene);
    ImGui::SameLine();
    ImGui::BeginDisabled(!capCamera && !capScene);
    if (ImGui::Button(ICON_FA_BOOKMARK "  Capture")) {
        uint32_t aspects = 0;
        if (capCamera)
            aspects |= kSnapshotCamera;
        if (capScene)
            aspects |= kSnapshotScene;
        services.createSnapshot(nameBuf, aspects);
        nameBuf[0] = '\0';
    }
    ImGui::EndDisabled();
    ImGui::Separator();

    static char search[128] = "";
    UiKit::SearchInput("snapSearch", search, sizeof(search));
    const bool filtering = search[0] != '\0';
    auto matches = [&](const SnapshotView& v) {
        if (!filtering)
            return true;
        int score = 0;
        if (UiKit::FuzzyMatch(search, v.name.c_str(), &score))
            return true;
        for (const std::string& t : v.tags)
            if (UiKit::FuzzyMatch(search, t.c_str(), &score))
                return true;
        return false;
    };

    const size_t count = services.snapshotCount();
    if (count == 0) {
        UiKit::EmptyState(ICON_FA_BOOKMARK, "No snapshots",
                          "Capture a checkpoint of the camera and scene you can "
                          "return to at any time.");
        ImGui::End();
        return;
    }

    static int editIndex = -1;      // snapshot whose name/tags are being edited
    static char editName[128] = "";
    static char editTags[256] = "";

    ImGui::BeginChild("snapList", ImVec2(0, 0), false);
    for (size_t i = 0; i < count; ++i) {
        const SnapshotView v = services.snapshot(i);
        if (!matches(v))
            continue;
        ImGui::PushID(int(i));
        UiKit::BeginCard("snapCard");

        if (editIndex == int(i)) {
            ImGui::SetNextItemWidth(-1.0f);
            const bool commit =
                ImGui::InputText("##editName", editName, sizeof(editName),
                                 ImGuiInputTextFlags_EnterReturnsTrue);
            ImGui::SetNextItemWidth(-1.0f);
            ImGui::InputTextWithHint("##editTags", "tags, comma, separated", editTags,
                                     sizeof(editTags));
            if (ImGui::Button("Save") || commit) {
                services.renameSnapshot(i, editName);
                services.setSnapshotTags(i, splitTags(editTags));
                editIndex = -1;
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel"))
                editIndex = -1;
        } else {
            ImGui::TextUnformatted(v.name.c_str());
            ImGui::SameLine();
            ImGui::TextDisabled("%s", v.timestamp.c_str());

            if (v.aspects & kSnapshotCamera) {
                UiKit::Badge(ICON_FA_CAMERA " cam", UiKit::Color(UiKit::Semantic::Info));
                ImGui::SameLine();
            }
            if (v.aspects & kSnapshotScene) {
                UiKit::Badge(ICON_FA_CUBE " scene",
                             UiKit::Color(UiKit::Semantic::Primary));
                ImGui::SameLine();
            }
            for (const std::string& t : v.tags) {
                UiKit::Chip(t.c_str(), false);
                ImGui::SameLine();
            }
            ImGui::NewLine();

            if (ImGui::Button(ICON_FA_UNDO "  Restore"))
                services.restoreSnapshot(i, v.aspects);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("Restore all captured aspects");
            // Selective restore.
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_ELLIPSIS_H))
                ImGui::OpenPopup("restoreSel");
            if (ImGui::BeginPopup("restoreSel")) {
                if ((v.aspects & kSnapshotCamera) &&
                    ImGui::MenuItem("Restore camera only"))
                    services.restoreSnapshot(i, kSnapshotCamera);
                if ((v.aspects & kSnapshotScene) &&
                    ImGui::MenuItem("Restore scene only"))
                    services.restoreSnapshot(i, kSnapshotScene);
                ImGui::EndPopup();
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_PEN)) {
                editIndex = int(i);
                std::snprintf(editName, sizeof(editName), "%s", v.name.c_str());
                std::snprintf(editTags, sizeof(editTags), "%s",
                              joinTags(v.tags).c_str());
            }
            ImGui::SameLine();
            if (ImGui::SmallButton(ICON_FA_TRASH)) {
                services.deleteSnapshot(i);
                UiKit::EndCard();
                ImGui::PopID();
                break; // indices shifted; redraw next frame
            }
        }

        UiKit::EndCard();
        ImGui::PopID();
    }
    ImGui::EndChild();
    ImGui::End();
}

} // namespace Gui
