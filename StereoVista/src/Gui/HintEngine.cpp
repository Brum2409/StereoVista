#include "Gui/HintEngine.h"

#include "Core/CommandRegistry.h"
#include "Gui/Services.h"
#include "Gui/UiKit.h"
#include "Scene/Scene.h"

#include "imgui/imgui.h"

#include <algorithm>
#include <string>

// The hint engine (see Gui/HintEngine.h). One hint at a time, a cooldown between
// them, dismiss-forever, and a master pref. Every action is a SUGGESTION — the
// performance guardian offers to lower a setting, it never lowers one itself.

namespace Gui {

namespace {

// Don't stack hints, and don't chatter: a minimum gap between two hints, and a
// minimum on-screen time before one can be replaced.
constexpr double kCooldownSeconds = 45.0;
constexpr double kMinVisibleSeconds = 4.0;
// The performance guardian only fires after the frame rate has been bad for a
// while — a single hitch (loading, a resize) must never trigger it.
constexpr double kLowFpsSeconds = 6.0;

} // namespace

bool HintEngine::dismissed(Services& services, const char* id) const {
    const std::vector<std::string>& list = services.settings().ui.hints.dismissed;
    return std::find(list.begin(), list.end(), id) != list.end();
}

void HintEngine::dismiss(Services& services, const char* id) {
    std::vector<std::string>& list = services.settings().ui.hints.dismissed;
    if (std::find(list.begin(), list.end(), id) == list.end())
        list.push_back(id); // persisted by Gui::Preferences (debounced autosave)
}

void HintEngine::draw(Services& services, float viewportX, float viewportY,
                      float viewportW, float viewportH) {
    Settings& settings = services.settings();
    if (!settings.ui.hints.enabled)
        return;
    if (viewportW < 320.0f * UiKit::Scale())
        return;

    const double now = ImGui::GetTime();
    const SceneStats stats = services.sceneStats();

    // ── Trigger evaluation (only when nothing is on screen) ─────────────────
    if (activeId_.empty() && now - lastShownTime_ > kCooldownSeconds) {
        const char* pick = nullptr;

        // 1. The scene just got its first object — teach framing.
        const bool hasObjects = (stats.models + stats.clouds + stats.layers) > 0;
        if (!sawFirstObject_ && hasObjects) {
            sawFirstObject_ = true;
            if (!dismissed(services, "hint.frame_selected"))
                pick = "hint.frame_selected";
        }

        // 2. The first measurement landed — say where it went.
        const bool hasMeasurement = !services.scene().measurements.empty();
        if (!pick && !sawFirstMeasurement_ && hasMeasurement) {
            sawFirstMeasurement_ = true;
            if (!dismissed(services, "hint.measurements"))
                pick = "hint.measurements";
        }

        // 3. Enough loose objects that grouping would help.
        if (!pick && stats.ungrouped >= 5 && stats.groups == 0 &&
            !dismissed(services, "hint.grouping"))
            pick = "hint.grouping";

        // 4. Performance guardian: sustained low FPS AND an expensive setting on.
        const float fps = services.diagnostics().fps;
        const bool expensive = settings.pointCloud.hqs || settings.render.wireframe ||
                               settings.lighting.softShadows;
        if (fps > 0.0f && fps < 25.0f && expensive) {
            if (lowFpsSince_ < 0.0)
                lowFpsSince_ = now;
        } else {
            lowFpsSince_ = -1.0;
        }
        if (!pick && lowFpsSince_ > 0.0 && now - lowFpsSince_ > kLowFpsSeconds &&
            !dismissed(services, "hint.performance"))
            pick = "hint.performance";

        if (pick) {
            activeId_ = pick;
            shownAt_ = now;
            lastShownTime_ = now;
        }
    }

    if (activeId_.empty())
        return;

    // ── Render (bottom-centre over the primary viewport) ─────────────────────
    const char* text = "";
    const char* actions[2] = { nullptr, nullptr };
    int actionCount = 0;
    if (activeId_ == "hint.frame_selected") {
        text = "Press F to frame the selection — or double-click it in the Scene "
               "panel.";
        actions[0] = "Frame now";
        actionCount = 1;
    } else if (activeId_ == "hint.measurements") {
        text = "Measurements live in the Scene panel, like every other object — "
               "rename, hide or delete them there.";
    } else if (activeId_ == "hint.grouping") {
        text = "You have a few loose objects. Select some and press Ctrl+G to "
               "group them.";
        actions[0] = "Group selection";
        actionCount = 1;
    } else if (activeId_ == "hint.performance") {
        text = "The frame rate is low and an expensive setting is on. Want to "
               "lower it?";
        actions[0] = "Lower it";
        actions[1] = "Keep";
        actionCount = 2;
    }

    ImGui::SetNextWindowPos(
        ImVec2(viewportX + viewportW * 0.5f, viewportY + viewportH - UiKit::Space(7)),
        ImGuiCond_Always, ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.94f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, UiKit::RadiusCard());
    if (ImGui::Begin("##hint", nullptr,
                     ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                         ImGuiWindowFlags_NoSavedSettings |
                         ImGuiWindowFlags_AlwaysAutoResize |
                         ImGuiWindowFlags_NoFocusOnAppearing)) {
        const int result =
            UiKit::HintToast(activeId_.c_str(), text, actions, actionCount);

        if (result == UiKit::kHintDismissed) {
            dismiss(services, activeId_.c_str()); // never again
            activeId_.clear();
        } else if (result >= 0) {
            if (activeId_ == "hint.frame_selected" && result == 0) {
                services.commands().run("view.frame_selected");
            } else if (activeId_ == "hint.grouping" && result == 0) {
                services.commands().run("edit.group");
            } else if (activeId_ == "hint.performance" && result == 0) {
                // Suggest, never auto-change: flip exactly ONE expensive flag,
                // the one most likely to be the cost, and say what happened.
                if (settings.render.wireframe) {
                    settings.render.wireframe = false;
                    services.toast("Wireframe off", Plugins::ToastLevel::Info);
                } else if (settings.pointCloud.hqs) {
                    settings.pointCloud.hqs = false;
                    services.toast("High-quality point shading off",
                                   Plugins::ToastLevel::Info);
                } else if (settings.lighting.softShadows) {
                    settings.lighting.softShadows = false;
                    services.toast("Soft shadows off", Plugins::ToastLevel::Info);
                }
                lowFpsSince_ = -1.0;
            }
            activeId_.clear(); // acted on — done
        } else if (ImGui::GetTime() - shownAt_ > 20.0) {
            activeId_.clear(); // it had its moment; don't linger
        }
        (void)kMinVisibleSeconds;
    }
    ImGui::End();
    ImGui::PopStyleVar();
}

} // namespace Gui
