#include "Gui/Panels.h"

#include "Gui/Services.h"
#include "Gui/UiKit.h"
#include "Core/UndoManager.h"

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui.h"

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

    auto savedDivider = [&]() {
        ImGui::PushStyleColor(ImGuiCol_Text, UiKit::Color(UiKit::Semantic::Success));
        ImGui::TextUnformatted(ICON_FA_BOOKMARK "  saved");
        ImGui::PopStyleColor();
    };

    ImGui::BeginChild("historyList", ImVec2(0, 0), false);

    if (hasSaved && savedDepth == 0)
        savedDivider();
    if (!filtering)
        ImGui::TextDisabled(ICON_FA_FLAG "  Initial state");

    // Past edits (oldest → newest); click jumps to just after that edit.
    for (size_t i = 0; i < appliedCount; ++i) {
        if (hasSaved && savedDepth == i && savedDepth != 0)
            savedDivider();
        if (!matches(past[i]))
            continue;
        ImGui::PushID(int(i));
        const std::string label = std::string(ICON_FA_UNDO "  ") + past[i];
        if (ImGui::Selectable(label.c_str())) {
            for (size_t k = i + 1; k < appliedCount; ++k)
                undo.undo();
        }
        ImGui::PopID();
    }

    // Current-state marker (+ saved marker when saved == current).
    if (hasSaved && savedDepth == appliedCount && appliedCount != 0)
        savedDivider();
    ImGui::PushStyleColor(ImGuiCol_Text, accent);
    ImGui::TextUnformatted(ICON_FA_CIRCLE "  Current state");
    ImGui::PopStyleColor();

    // Future edits (next redo → furthest); click replays redo up to there.
    for (size_t j = 0; j < future.size(); ++j) {
        if (!matches(future[j]))
            continue;
        ImGui::PushID(10000 + int(j));
        ImGui::PushStyleColor(ImGuiCol_Text, dim);
        const std::string label = std::string(ICON_FA_REDO "  ") + future[j];
        const bool clicked = ImGui::Selectable(label.c_str());
        ImGui::PopStyleColor();
        if (clicked)
            for (size_t k = 0; k <= j; ++k)
                undo.redo();
        ImGui::PopID();
    }

    if (appliedCount == 0 && future.empty())
        ImGui::TextDisabled("No edits yet. Every change appears here.");

    ImGui::EndChild();
    ImGui::End();
}

} // namespace Gui
