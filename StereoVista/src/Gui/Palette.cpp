#include "Gui/Palette.h"

#include "Core/CommandRegistry.h"
#include "Core/Shortcuts.h"
#include "Gui/Outliner.h"
#include "Gui/Panels.h"
#include "Gui/Services.h"

#include "imgui/IconsFontAwesome5.h"

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

// The Ctrl+K palette (see Gui/Palette.h): provider registry + the built-in
// providers + the overlay. Commands always execute through
// core::CommandRegistry::run (C5); objects/snapshots/recents act through
// Gui::Services. Ranking = UiKit::FuzzyMatch score blended with command
// frecency (recorded by run(), persisted in preferences.json since Pass 0).

namespace Gui {
namespace Palette {

namespace {

struct Registry {
    std::vector<Provider> providers;
    bool builtinsRegistered = false;
};

Registry& registry() {
    static Registry r;
    return r;
}

// ── Built-in providers ───────────────────────────────────────────────────────

void provideCommands(Services& services, std::vector<Item>& out) {
    Services* svc = &services;
    core::CommandRegistry& registry = services.commands();
    for (const core::Command& command : registry.commands()) {
        Item item;
        item.source = Source::Command;
        item.title = command.title;
        item.subtitle = command.category;
        item.keywords = command.keywords;
        item.shortcut = services.shortcuts().label(command.id);
        item.kind = UiKit::ObjectKind::Command;
        item.enabled = registry.isEnabled(command);
        item.boost = registry.frecency(command.id);
        const std::string id = command.id;
        item.activate = [svc, id] { svc->commands().run(id); };
        out.push_back(std::move(item));
    }
}

// Scene objects come straight from the Pass-1 Outliner provider registry, so a
// future tool that registers Outliner rows becomes searchable here for free.
void provideObjects(Services& services, std::vector<Item>& out) {
    Services* svc = &services;
    const Outliner::Section sections[] = { Outliner::Section::Environment,
                                           Outliner::Section::SceneObjects,
                                           Outliner::Section::Annotations };
    for (Outliner::Section section : sections) {
        std::vector<Outliner::Item> rows;
        Outliner::collect(services, section, rows);
        for (const Outliner::Item& row : rows) {
            Item item;
            item.source = Source::Object;
            item.title = row.name;
            item.subtitle = UiKit::StyleFor(row.kind).noun;
            item.kind = row.kind;
            const scene::SceneItemRef ref = row.ref;
            item.activate = [svc, ref] { svc->selection().selectOne(ref); };
            if (row.canFrame)
                item.alternate = [svc, ref] {
                    svc->selection().selectOne(ref);
                    svc->frameItems({ ref });
                };
            out.push_back(std::move(item));
        }
    }
}

void provideSnapshots(Services& services, std::vector<Item>& out) {
    Services* svc = &services;
    const size_t count = services.snapshotCount();
    for (size_t i = 0; i < count; ++i) {
        const SnapshotView view = services.snapshot(i);
        Item item;
        item.source = Source::Snapshot;
        item.title = view.name;
        item.subtitle = "Snapshot \xC2\xB7 " + view.timestamp;
        for (const std::string& tag : view.tags)
            item.keywords += tag + " ";
        item.kind = UiKit::ObjectKind::Snapshot;
        const uint32_t aspects = view.aspects;
        item.activate = [svc, i, aspects] { svc->restoreSnapshot(i, aspects); };
        out.push_back(std::move(item));
    }
}

void provideRecents(Services& services, std::vector<Item>& out) {
    Services* svc = &services;
    for (const std::string& path : services.settings().files.recentScenes) {
        Item item;
        item.source = Source::Recent;
        // Show the file stem; keep the full path searchable + as the subtitle.
        const size_t slash = path.find_last_of("/\\");
        const std::string stem =
            (slash == std::string::npos) ? path : path.substr(slash + 1);
        item.title = stem;
        item.subtitle = path;
        item.keywords = path;
        item.kind = UiKit::ObjectKind::File;
        const std::string target = path;
        item.activate = [svc, target] { svc->openSceneFile(target); };
        out.push_back(std::move(item));
    }
}

void registerBuiltins() {
    registerProvider(provideCommands);
    registerProvider(provideObjects);
    registerProvider(provideSnapshots);
    registerProvider(provideRecents);
}

// ── Ranking ──────────────────────────────────────────────────────────────────

struct Scored {
    const Item* item = nullptr;
    int score = 0;
    int matches[32] = {};
    int matchCount = 0;
};

// Frecency is a decayed use-count; weight it so a heavily-used command
// outranks an equal-scoring stranger without ever burying an exact match.
constexpr double kFrecencyWeight = 12.0;

} // namespace

void registerProvider(Provider provider) {
    if (provider)
        registry().providers.push_back(std::move(provider));
}

void collect(Services& services, std::vector<Item>& out) {
    Registry& r = registry();
    if (!r.builtinsRegistered) {
        r.builtinsRegistered = true; // set FIRST: registerBuiltins re-enters
        registerBuiltins();
    }
    for (const Provider& provider : r.providers)
        provider(services, out);
}

void draw(Services& services, bool* open) {
    if (!open || !*open)
        return;

    static char query[192] = "";
    static int selected = 0;
    static bool wasOpen = false;
    const bool justOpened = !wasOpen;
    wasOpen = true;
    if (justOpened) {
        query[0] = '\0';
        selected = 0;
    }

    ImGuiViewport* viewport = ImGui::GetMainViewport();

    // Dimmed scrim (its own window so the palette, focused, draws above it).
    ImGui::SetNextWindowPos(viewport->Pos);
    ImGui::SetNextWindowSize(viewport->Size);
    ImGui::SetNextWindowBgAlpha(0.45f);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.02f, 0.02f, 0.04f, 1.0f));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
    if (ImGui::Begin("##paletteScrim", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_NoBringToFrontOnFocus |
                         ImGuiWindowFlags_NoNavFocus |
                         ImGuiWindowFlags_NoFocusOnAppearing)) {
        if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(ImGuiMouseButton_Left))
            *open = false;
    }
    ImGui::End();
    ImGui::PopStyleVar();
    ImGui::PopStyleColor();

    // ── Gather + rank ───────────────────────────────────────────────────────
    std::vector<Item> items;
    collect(services, items);

    // Prefix scoping: '>' commands, '#' objects, ':' settings (Pass 6 fills it).
    const char* needle = query;
    Source only = Source::Count; // Count = no scope
    if (query[0] == '>') {
        only = Source::Command;
        needle = query + 1;
    } else if (query[0] == '#') {
        only = Source::Object;
        needle = query + 1;
    } else if (query[0] == ':') {
        only = Source::Setting;
        needle = query + 1;
    }
    while (*needle == ' ')
        ++needle;
    const bool hasQuery = *needle != '\0';

    std::vector<Scored> hits;
    hits.reserve(items.size());
    for (const Item& item : items) {
        if (only != Source::Count && item.source != only)
            continue;
        Scored scored;
        scored.item = &item;
        if (hasQuery) {
            int titleScore = 0;
            const bool titleHit =
                UiKit::FuzzyMatch(needle, item.title.c_str(), &titleScore,
                                  scored.matches, 32, &scored.matchCount);
            int keyScore = 0;
            const bool keyHit =
                !item.keywords.empty() &&
                UiKit::FuzzyMatch(needle, item.keywords.c_str(), &keyScore);
            if (!titleHit && !keyHit)
                continue;
            // A keyword-only hit shouldn't highlight the title.
            if (!titleHit)
                scored.matchCount = 0;
            scored.score = titleHit ? titleScore : (keyScore / 2);
        }
        scored.score += int(item.boost * kFrecencyWeight);
        hits.push_back(scored);
    }

    std::stable_sort(hits.begin(), hits.end(), [](const Scored& a, const Scored& b) {
        if (a.score != b.score)
            return a.score > b.score;
        return a.item->title < b.item->title;
    });
    constexpr size_t kMaxRows = 60;
    if (hits.size() > kMaxRows)
        hits.resize(kMaxRows);

    if (selected >= int(hits.size()))
        selected = hits.empty() ? 0 : int(hits.size()) - 1;
    if (selected < 0)
        selected = 0;

    // ── The palette window ──────────────────────────────────────────────────
    const float width = 640.0f * UiKit::Scale();
    ImGui::SetNextWindowPos(
        ImVec2(viewport->Pos.x + viewport->Size.x * 0.5f,
               viewport->Pos.y + viewport->Size.y * 0.18f),
        ImGuiCond_Always, ImVec2(0.5f, 0.0f));
    ImGui::SetNextWindowSize(ImVec2(width, 0.0f), ImGuiCond_Always);
    if (justOpened)
        ImGui::SetNextWindowFocus();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiKit::RadiusCard());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(UiKit::Space(5), UiKit::Space(5)));
    if (!ImGui::Begin("##palette", nullptr,
                      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
                          ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    if (justOpened)
        ImGui::SetKeyboardFocusHere();
    ImGui::SetNextItemWidth(-1.0f);
    const bool submitted =
        ImGui::InputTextWithHint("##paletteQuery",
                                 "Search commands, objects, snapshots\xE2\x80\xA6  "
                                 "(> commands, # objects, : settings)",
                                 query, sizeof(query),
                                 ImGuiInputTextFlags_EnterReturnsTrue);

    // Keyboard navigation (a single-line InputText leaves Up/Down free).
    if (ImGui::IsKeyPressed(ImGuiKey_DownArrow, true))
        ++selected;
    if (ImGui::IsKeyPressed(ImGuiKey_UpArrow, true))
        --selected;
    if (!hits.empty()) {
        if (selected >= int(hits.size()))
            selected = 0; // wrap
        if (selected < 0)
            selected = int(hits.size()) - 1;
    }

    bool activate = submitted;
    const bool alternate = ImGui::GetIO().KeyShift;

    ImGui::Separator();

    const float rowHeight = ImGui::GetTextLineHeightWithSpacing() + UiKit::Space(3);
    const float listHeight = std::min(float(hits.size()), 9.0f) * rowHeight;
    ImGui::BeginChild("paletteList", ImVec2(0.0f, listHeight), false);
    for (size_t i = 0; i < hits.size(); ++i) {
        const Item& item = *hits[i].item;
        const UiKit::KindStyle style = UiKit::StyleFor(item.kind);
        ImGui::PushID(int(i));

        const bool isSelected = int(i) == selected;
        if (ImGui::Selectable("##row", isSelected,
                              ImGuiSelectableFlags_AllowItemOverlap,
                              ImVec2(0.0f, rowHeight - UiKit::Space(2)))) {
            selected = int(i);
            activate = true;
        }
        if (isSelected && (justOpened || ImGui::IsKeyPressed(ImGuiKey_DownArrow) ||
                           ImGui::IsKeyPressed(ImGuiKey_UpArrow)))
            ImGui::SetScrollHereY(0.5f);

        ImGui::SameLine(UiKit::Space(3));
        if (!item.enabled)
            ImGui::PushStyleColor(ImGuiCol_Text,
                                  ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        UiKit::InlineIcon(style.icon, item.enabled
                                          ? style.color
                                          : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        ImGui::SameLine();
        if (hits[i].matchCount > 0)
            UiKit::TextFuzzyHighlighted(item.title.c_str(), hits[i].matches,
                                        hits[i].matchCount);
        else
            ImGui::TextUnformatted(item.title.c_str());
        if (!item.enabled)
            ImGui::PopStyleColor();

        if (!item.subtitle.empty()) {
            ImGui::SameLine();
            ImGui::TextDisabled("\xC2\xB7 %s", item.subtitle.c_str());
        }
        if (!item.shortcut.empty()) {
            const ImVec2 size = ImGui::CalcTextSize(item.shortcut.c_str());
            ImGui::SameLine(ImGui::GetContentRegionMax().x - size.x - UiKit::Space(3));
            ImGui::TextDisabled("%s", item.shortcut.c_str());
        }
        ImGui::PopID();
    }
    ImGui::EndChild();

    if (hits.empty()) {
        ImGui::TextDisabled("No matches.");
    } else {
        ImGui::Separator();
        ImGui::TextDisabled("Enter run  \xC2\xB7  Shift+Enter frame  \xC2\xB7  "
                            "\xE2\x86\x91\xE2\x86\x93 navigate  \xC2\xB7  Esc close");
    }

    // ── Activate / dismiss ──────────────────────────────────────────────────
    if (activate && !hits.empty()) {
        const Item& item = *hits[size_t(selected)].item;
        if (item.enabled) {
            // Close FIRST: an action may open a modal or rebuild the scene.
            *open = false;
            if (alternate && item.alternate)
                item.alternate();
            else if (item.activate)
                item.activate();
        }
    }
    if (ImGui::IsKeyPressed(ImGuiKey_Escape))
        *open = false;

    ImGui::End();
    ImGui::PopStyleVar(2);

    if (!*open)
        wasOpen = false; // re-arm the just-opened seeding
}

} // namespace Palette
} // namespace Gui
