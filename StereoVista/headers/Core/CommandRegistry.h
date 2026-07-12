#pragma once

// ============================================================================
//  core::CommandRegistry  —  every user-facing action, behind one id
//  (UI redesign Pass 0; docs/UI_REDESIGN.md §6.2, coherence contract C5).
// ----------------------------------------------------------------------------
//  Menus, shortcuts, the future command palette, toolbars — and later macros
//  and the AI agent — all trigger actions exclusively through
//  CommandRegistry::run(id). That single choke-point is what makes actions
//  discoverable (palette), rebindable (ShortcutMap maps keys to command ids),
//  rankable (frecency is recorded per run) and eventually recordable.
//
//  The Application registers its commands once at startup
//  (Application::registerCommands); systems that grow new actions in later
//  passes add registrations, never new hard-wired call sites.
//
//  Pure standard C++ — no ImGui, no GLFW, no Vulkan. The registry stores
//  behaviour (closures) and metadata; presentation lives in the callers.
// ============================================================================

#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

namespace core {

struct Command {
    std::string id;       // stable, dot-scoped: "file.import_model"
    std::string title;    // menu / palette label: "Import Model..."
    std::string category; // menu + palette group: "File", "Edit", "View", ...
    std::string keywords; // extra search terms for the palette (space-separated)
    std::string tooltip;  // optional hover help (shown even while disabled)
    const char* icon = nullptr;   // ICON_FA_* literal (static storage) or null
    bool separatorBefore = false; // menus draw a separator before this item

    std::function<void()> action;
    std::function<bool()> enabled; // optional; empty = always enabled
    std::function<bool()> checked; // optional; renders as a checkable item
};

class CommandRegistry {
public:
    // Register a command (replaces an existing registration with the same id
    // in place, keeping its position — registration order is menu order).
    void add(Command command);

    // The one way to execute a command (contract C5). Returns false when the
    // id is unknown or the command is currently disabled; otherwise runs the
    // action and records a frecency use.
    bool run(const std::string& id);

    const Command* find(const std::string& id) const;
    bool isEnabled(const Command& command) const {
        return !command.enabled || command.enabled();
    }

    // Iterate registered commands (registration order).
    const std::vector<Command>& commands() const { return commands_; }
    // Visit every command of one category, in registration order.
    void forEachInCategory(const std::string& category,
                           const std::function<void(const Command&)>& fn) const;

    // ── Frecency (palette ranking; persisted via preferences) ───────────────
    // A decayed use counter per command: each run adds 1 to a score that
    // halves every two weeks. Zero for never-used commands.
    double frecency(const std::string& id) const;

    // Serialization for the preferences file: id -> {score, lastUse (unix s)}.
    struct FrecencyEntry {
        double score = 0.0;
        int64_t lastUse = 0;
    };
    const std::unordered_map<std::string, FrecencyEntry>& frecencyTable() const {
        return frecency_;
    }
    void setFrecencyEntry(const std::string& id, const FrecencyEntry& entry) {
        frecency_[id] = entry;
    }

private:
    std::vector<Command> commands_;
    std::unordered_map<std::string, size_t> index_; // id -> commands_ position
    std::unordered_map<std::string, FrecencyEntry> frecency_;
};

} // namespace core
