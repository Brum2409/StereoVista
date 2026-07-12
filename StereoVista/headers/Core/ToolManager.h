#pragma once

// ============================================================================
//  core::ToolManager — the one registry of interactive tools (modes)
//  (UI redesign Pass 7; docs/UI_REDESIGN.md §13, coherence contract C6).
// ----------------------------------------------------------------------------
//  A "tool" is a MODE: at most one is active at a time, activating one exits the
//  others, and Esc exits the active one. Tools self-describe; every surface that
//  shows tools — the Tools menu, the command palette, the status-bar chip, the
//  Inspector's Tool card and (Pass 8) the viewport toolbar — renders from THIS
//  registry, so a new tool appears everywhere by registering, not by editing UI
//  files (§16.1 recipe).
//
//  The registry stores behaviour (closures), never the tool object: measurement
//  lives in a Plugin, the clip-plane tool is app-owned, and a future tool may be
//  neither. Each descriptor just says how to ask "are you active?" and "become
//  active / inactive".
//
//  Pure std — no ImGui, no Vulkan, no scene. Options UI is drawn by the Gui layer
//  (the Inspector Tool card), which looks the active tool up by id.
// ============================================================================

#include <functional>
#include <string>
#include <vector>

namespace core {

struct Tool {
    std::string id;          // stable: "tool.measure" (also the command id)
    std::string title;       // "Measure"
    std::string description; // one-line tooltip
    const char* icon = nullptr; // ICON_FA_* literal (static storage) or null

    std::function<bool()> isActive;      // required
    std::function<void(bool)> setActive; // required
};

class ToolManager {
public:
    // Append-only registry (C6). A second registration of the same id replaces
    // the first in place.
    void add(Tool tool);

    const std::vector<Tool>& tools() const { return tools_; }
    const Tool* find(const std::string& id) const;

    // The active tool, or nullptr. Derived by ASKING the tools (their enabled
    // state is owned by the tool itself — a plugin toggled from its own window
    // must still show up as the active tool everywhere).
    const Tool* active() const;

    // Exclusive activation: deactivates every other tool first (one mode).
    void activate(const std::string& id);
    // Toggle: activates when inactive, exits when already active.
    void toggle(const std::string& id);
    // Esc: exit whatever is active. Returns true when something was exited.
    bool deactivateAll();

private:
    std::vector<Tool> tools_;
};

} // namespace core
