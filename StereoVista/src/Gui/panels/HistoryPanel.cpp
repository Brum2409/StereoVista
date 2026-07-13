#include "Gui/Panels.h"

#include "Gui/Services.h"
#include "Gui/UiKit.h"
#include "Core/UndoManager.h"

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// History panel (UI redesign Pass 3 §9): the undo stack as a labeled, clickable
// timeline. Past (applied) edits above the current-state marker, future (undone)
// edits below; clicking a row jumps there by replaying undo()/redo(). A search
// filter and a last-saved divider complete it. Reads core::UndoManager through
// Gui::Services — no app internals.

namespace Gui {

void drawHistoryPanel(Services& services, bool* open) {
    if (!ImGui::Begin(Windows::History, open)) {
        ImGui::End();
        return;
    }

    core::UndoManager& undo = services.undo();
    const std::vector<std::string> past = undo.undoDescriptions();   // oldest→newest
    const std::vector<std::string> future = undo.redoDescriptions(); // next→furthest
    const size_t appliedCount = past.size();
    const bool hasSaved = undo.hasSavedMark();
    const size_t savedDepth = undo.savedDepth();

    static char search[128] = "";
    UiKit::SearchInput("historySearch", search, sizeof(search));
    const bool filtering = search[0] != '\0';
    auto matches = [&](const std::string& s) {
        if (!filtering)
            return true;
        int score = 0;
        return UiKit::FuzzyMatch(search, s.c_str(), &score);
    };

    const ImVec4 accent = UiKit::Color(UiKit::Semantic::Accent);
    const ImVec4 dim = ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled);
    const float scale = UiKit::Scale();

    auto savedDivider = [&]() {
        ImGui::PushStyleColor(ImGuiCol_Text, UiKit::Color(UiKit::Semantic::Success));
        ImGui::TextUnformatted(ICON_FA_BOOKMARK "  saved");
        ImGui::PopStyleColor();
    };

    ImGui::BeginChild("historyList", ImVec2(0, 0), false);
    ImDrawList* dl = ImGui::GetWindowDrawList();

    // A continuous rail runs down the left of the timeline; every entry hangs a
    // node off it, and the current state is a playhead dot that SPRINGS along the
    // rail when you roll back or forward — the position in history becomes a thing
    // that visibly moves rather than a label that jumps. railX/top/bottom are
    // gathered as the list lays out and the rail + playhead are drawn at the end,
    // over everything.
    const float railX = ImGui::GetCursorScreenPos().x + 6.0f * scale;
    float railTop = ImGui::GetCursorScreenPos().y;
    float railBottom = railTop;
    float currentY = railTop;    // screen-Y of the "Current state" marker
    const float indent = 18.0f * scale;

    // One timeline row: a node dot on the rail + the label, with an eased hover
    // wash and a press ripple. Returns true when clicked. `tone` colours node+text.
    auto timelineRow = [&](const char* id, const char* icon, const std::string& text,
                           const ImVec4& tone, bool bright) -> bool {
        ImGui::PushID(id);
        const ImVec2 p0 = ImGui::GetCursorScreenPos();
        ImGui::SetCursorScreenPos(ImVec2(p0.x + indent, p0.y));
        const std::string label = std::string(icon) + "  " + text;
        const bool clicked = ImGui::Selectable(label.c_str());
        const ImGuiID rowId = ImGui::GetItemID();
        const ImVec2 mn = ImGui::GetItemRectMin();
        const ImVec2 mx = ImGui::GetItemRectMax();
        // Hover wash + press ripple straight from UiKit (the row's own rect).
        UiKit::ItemFxAt(rowId, mn, mx, UiKit::ItemFx_Hover | UiKit::ItemFx_Press,
                        UiKit::RadiusInner(), ImGui::IsItemHovered(),
                        ImGui::IsItemActivated(), ImGui::IsItemActive());
        // The node dot on the rail, brightening slightly under the pointer.
        const float nodeY = (mn.y + mx.y) * 0.5f;
        const float hov = UiKit::Anim01(rowId ^ 0x7E1u, ImGui::IsItemHovered()
                                                            ? 1.0f : 0.0f, 0.12f);
        const float r = (bright ? 3.5f : 2.5f) * scale * (1.0f + 0.25f * hov);
        dl->AddCircleFilled(ImVec2(railX, nodeY), r, ImGui::GetColorU32(tone));
        railBottom = mx.y;
        ImGui::PopID();
        return clicked;
    };

    if (hasSaved && savedDepth == 0) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        savedDivider();
    }
    if (!filtering) {
        ImGui::SetCursorScreenPos(
            ImVec2(ImGui::GetCursorScreenPos().x + indent, ImGui::GetCursorScreenPos().y));
        ImGui::TextDisabled(ICON_FA_FLAG "  Initial state");
        const ImVec2 mx = ImGui::GetItemRectMax();
        dl->AddCircleFilled(ImVec2(railX, (ImGui::GetItemRectMin().y + mx.y) * 0.5f),
                            2.5f * scale, ImGui::GetColorU32(dim));
        railBottom = mx.y;
    }

    // Past edits (oldest → newest); click jumps to just after that edit.
    for (size_t i = 0; i < appliedCount; ++i) {
        if (hasSaved && savedDepth == i && savedDepth != 0) {
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
            savedDivider();
        }
        if (!matches(past[i]))
            continue;
        char id[24];
        std::snprintf(id, sizeof(id), "p%zu", i);
        if (timelineRow(id, ICON_FA_UNDO, past[i],
                        ImGui::GetStyleColorVec4(ImGuiCol_Text), false)) {
            for (size_t k = i + 1; k < appliedCount; ++k)
                undo.undo();
        }
    }

    // Current-state marker (+ saved marker when saved == current). Its Y is what
    // the playhead springs to.
    if (hasSaved && savedDepth == appliedCount && appliedCount != 0) {
        ImGui::SetCursorPosX(ImGui::GetCursorPosX() + indent);
        savedDivider();
    }
    {
        ImGui::SetCursorScreenPos(
            ImVec2(ImGui::GetCursorScreenPos().x + indent, ImGui::GetCursorScreenPos().y));
        ImGui::PushStyleColor(ImGuiCol_Text, accent);
        ImGui::TextUnformatted(ICON_FA_CIRCLE "  Current state");
        ImGui::PopStyleColor();
        currentY = (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5f;
        railBottom = ImGui::GetItemRectMax().y;
    }

    // Future edits (next redo → furthest); click replays redo up to there.
    for (size_t j = 0; j < future.size(); ++j) {
        if (!matches(future[j]))
            continue;
        char id[24];
        std::snprintf(id, sizeof(id), "f%zu", j);
        if (timelineRow(id, ICON_FA_REDO, future[j], dim, false))
            for (size_t k = 0; k <= j; ++k)
                undo.redo();
    }

    // ── Rail + playhead, drawn over the nodes ───────────────────────────────
    if (appliedCount == 0 && future.empty()) {
        UiKit::EmptyState(ICON_FA_HISTORY, "No edits yet",
                          "Every change you make appears here. Click any point to "
                          "roll the whole scene back to it.");
    } else {
        ImVec4 rail = dim;
        rail.w *= 0.5f;
        dl->AddLine(ImVec2(railX, railTop + 2.0f * scale),
                    ImVec2(railX, railBottom - 2.0f * scale),
                    ImGui::GetColorU32(rail), 2.0f * scale);

        // The playhead springs toward the current-state marker. When appliedCount
        // changes (an undo/redo/rollback landed) it also gets a kick, so it
        // pulses as it arrives — the panel reacts even to an undo triggered by a
        // keyboard shortcut with the mouse nowhere near it.
        const ImGuiID phId = ImGui::GetID("##playhead");
        static size_t s_lastApplied = size_t(-1);
        if (s_lastApplied != size_t(-1) && s_lastApplied != appliedCount)
            UiKit::Kick(phId ^ 0x99u, 1.0f);
        s_lastApplied = appliedCount;

        const float y = UiKit::ReduceMotion()
                            ? currentY
                            : UiKit::Spring(phId, currentY, 5.0f, 0.6f);
        const float pulse = UiKit::Kicked(phId ^ 0x99u, 0.5f);
        // A soft halo that blooms on arrival, then the solid playhead dot.
        if (pulse > 0.01f) {
            ImVec4 halo = accent;
            halo.w = 0.4f * pulse;
            dl->AddCircleFilled(ImVec2(railX, y),
                                (5.0f + 6.0f * pulse) * scale,
                                ImGui::GetColorU32(halo));
        }
        dl->AddCircleFilled(ImVec2(railX, y), (4.5f + 1.0f * pulse) * scale,
                            ImGui::GetColorU32(accent));
        dl->AddCircleFilled(ImVec2(railX, y), 1.8f * scale,
                            ImGui::GetColorU32(ImVec4(1, 1, 1, 1)));
    }

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Gui
