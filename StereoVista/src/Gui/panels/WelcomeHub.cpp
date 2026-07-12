#include "Gui/Panels.h"

#include "Core/CommandRegistry.h"
#include "Core/Shortcuts.h"
#include "Gui/Services.h"
#include "Gui/UiKit.h"

#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui.h"

#include <string>
#include <vector>

// Welcome Hub (UI redesign Pass 5 §11): the calm landing card that replaces the
// empty-scene gap. Non-modal, centered over the PRIMARY viewport, only while the
// scene is empty. After an unclean shutdown it LEADS with a "Restore last
// session" card (crash recovery, §11).
//
// Everything here runs registered commands (C5) — the primitives row and the
// import button share the exact paths the menus and the palette use, so there is
// no second code path to keep in sync.

namespace Gui {

namespace {

// Short label for a recent-scene card: the file stem.
std::string stemOf(const std::string& path) {
    const size_t slash = path.find_last_of("/\\");
    std::string name = (slash == std::string::npos) ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    if (dot != std::string::npos)
        name = name.substr(0, dot);
    return name;
}

} // namespace

void drawWelcomeHub(Services& services, float screenX, float screenY, float screenW,
                    float screenH) {
    if (!services.settings().files.showWelcomeHub || !services.sceneEmpty())
        return;
    if (screenW < 320.0f * UiKit::Scale() || screenH < 240.0f * UiKit::Scale())
        return; // too small to be anything but noise (C9)

    ImGui::SetNextWindowPos(ImVec2(screenX + screenW * 0.5f, screenY + screenH * 0.5f),
                            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiKit::RadiusCard());
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(UiKit::Space(7), UiKit::Space(6)));
    if (!ImGui::Begin("##welcomeHub", nullptr,
                      ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                          ImGuiWindowFlags_NoSavedSettings |
                          ImGuiWindowFlags_AlwaysAutoResize |
                          ImGuiWindowFlags_NoFocusOnAppearing)) {
        ImGui::End();
        ImGui::PopStyleVar(2);
        return;
    }

    core::CommandRegistry& commands = services.commands();

    // ── Recovery card first: the most valuable thing we can offer (§11) ──────
    if (services.recoveryAvailable()) {
        UiKit::BeginCard("recoveryCard");
        UiKit::InlineIcon(ICON_FA_LIFE_RING, UiKit::Color(UiKit::Semantic::Warning));
        ImGui::SameLine();
        ImGui::TextUnformatted("StereoVista closed unexpectedly");
        const std::string stamp = services.recoveryTimestamp();
        ImGui::TextDisabled("%s", stamp.empty()
                                      ? "An autosave from that session is available."
                                      : ("Autosaved at " + stamp).c_str());
        if (ImGui::Button(ICON_FA_UNDO "  Restore last session"))
            services.restoreLastSession();
        ImGui::SameLine();
        if (ImGui::Button("Discard"))
            services.discardRecovery();
        UiKit::EndCard();
        ImGui::Spacing();
    }

    UiKit::PanelTitle(ICON_FA_CUBES, "StereoVista");
    ImGui::TextDisabled("Drop files anywhere, or start from here.");
    ImGui::Spacing();

    // ── Import ──────────────────────────────────────────────────────────────
    if (ImGui::Button(ICON_FA_FILE_IMPORT "  Import files\xE2\x80\xA6",
                      ImVec2(-FLT_MIN, 0.0f)))
        commands.run("file.import_files");
    const std::string importKey = services.shortcuts().label("file.import_files");
    ImGui::TextDisabled("Models, point clouds, SLPK layers and scenes%s%s",
                        importKey.empty() ? "" : "  \xC2\xB7  ", importKey.c_str());

    // ── Primitives row (same commands the palette runs) ──────────────────────
    ImGui::Spacing();
    UiKit::SectionHeader("Start with a primitive");
    struct Primitive {
        const char* id;
        const char* label;
    };
    static const Primitive kPrimitives[] = {
        { "create.cube", "Cube" },     { "create.sphere", "Sphere" },
        { "create.cylinder", "Cylinder" }, { "create.plane", "Plane" },
        { "create.torus", "Torus" },
    };
    for (int i = 0; i < 5; ++i) {
        if (i)
            ImGui::SameLine();
        if (ImGui::Button(kPrimitives[i].label))
            commands.run(kPrimitives[i].id);
    }

    // ── Recent scenes ───────────────────────────────────────────────────────
    const std::vector<std::string>& recents = services.settings().files.recentScenes;
    if (!recents.empty()) {
        ImGui::Spacing();
        UiKit::SectionHeader("Recent scenes");
        const size_t shown = recents.size() < 5 ? recents.size() : size_t(5);
        for (size_t i = 0; i < shown; ++i) {
            ImGui::PushID(int(i));
            const std::string& path = recents[i];
            UiKit::InlineIcon(ICON_FA_FILE, UiKit::StyleFor(UiKit::ObjectKind::File).color);
            ImGui::SameLine();
            if (ImGui::Selectable(stemOf(path).c_str()))
                services.openSceneFile(path);
            if (ImGui::IsItemHovered())
                ImGui::SetTooltip("%s", path.c_str()); // truncated text always tooltips
            ImGui::PopID();
        }
    }

    // ── Footer: the one shortcut worth teaching on day one ──────────────────
    ImGui::Spacing();
    ImGui::Separator();
    const std::string paletteKey = services.shortcuts().label("palette.open");
    ImGui::TextDisabled("%s  Search everything",
                        paletteKey.empty() ? "Ctrl+K" : paletteKey.c_str());
    ImGui::SameLine();
    if (ImGui::SmallButton("Don't show this again"))
        services.settings().files.showWelcomeHub = false;

    ImGui::End();
    ImGui::PopStyleVar(2);
}

} // namespace Gui
