#pragma once

// ============================================================================
//  Gui::HintEngine — contextual hints that teach without nagging
//  (UI redesign Pass 8; docs/UI_REDESIGN.md §14, principle 7 "alive, quietly").
// ----------------------------------------------------------------------------
//  A hint fires when its CONTEXT is true, at most one at a time, never twice in
//  quick succession, and never again once dismissed. It suggests — it never
//  changes a setting behind the user's back (the performance guardian offers
//  "[Lower it] [Keep]"; it does not lower anything itself).
//
//  All of it is opt-out: `Settings::Ui::Hints::enabled` is the master switch and
//  every dismissal is persisted in `Settings::Ui::Hints::dismissed` (contract C8).
//
//  Timing uses ImGui::GetTime() — no CRT time API (MSVC compiles the deprecated
//  ones as errors here).
// ============================================================================

#include <string>
#include <vector>

namespace Gui {

class Services;

class HintEngine {
public:
    // Evaluate triggers and draw the active hint (bottom-centre over the primary
    // viewport rect, which the GuiSystem passes in). Call once per frame.
    void draw(Services& services, float viewportX, float viewportY, float viewportW,
              float viewportH);

private:
    bool dismissed(Services& services, const char* id) const;
    void dismiss(Services& services, const char* id);

    std::string activeId_;    // the hint on screen ("" = none)
    double lastShownTime_ = -1000.0; // ImGui::GetTime() of the last hint
    double shownAt_ = 0.0;
    // Session state the triggers need (0->N edges, not absolute values).
    bool sawFirstObject_ = false;
    bool sawFirstMeasurement_ = false;
    double lowFpsSince_ = -1.0;
};

} // namespace Gui
