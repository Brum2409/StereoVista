#pragma once

// ============================================================================
//  core::ShortcutMap  —  rebindable keyboard shortcuts bound to command ids
//  (UI redesign Pass 0; docs/UI_REDESIGN.md §6.3).
// ----------------------------------------------------------------------------
//  A binding table  shortcut -> command id  that replaces the hard-coded key
//  handling in Application.cpp. Native re-implementation of the excluded GL
//  ShortcutManager, rewritten better (branch rule):
//
//    * bindings target CommandRegistry ids, not a parallel action enum —
//      a new command is bindable the moment it is registered;
//    * defaults live next to the command registrations; the persisted file
//      stores the user's whole table (missing ids keep their defaults);
//    * the GL shortcuts.json (profile format, GLFW keycodes) still loads:
//      the active profile's bindings are migrated onto the new command ids
//      on first load, then saved back in the v2 format (C10 spirit);
//    * up to two bindings per command (GL parity), exact-modifier matching,
//      and the GL keyboard-layout normalization (QWERTZ/AZERTY) is kept.
//
//  Plain data + std only in this header; GLFW/json stay in the .cpp. The
//  actual per-frame key-edge dispatch lives in Application (it owns the input
//  layer); this class is the table, its persistence, and the lookups.
// ============================================================================

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace core {

struct ShortcutBinding {
    int keyCode = -1; // GLFW_KEY_* (layout-normalized); -1 = unbound slot
    bool ctrl = false;
    bool alt = false;
    bool shift = false;

    bool valid() const { return keyCode != -1; }
    bool operator==(const ShortcutBinding& o) const {
        return keyCode == o.keyCode && ctrl == o.ctrl && alt == o.alt &&
               shift == o.shift;
    }
    bool operator!=(const ShortcutBinding& o) const { return !(*this == o); }

    // Human-readable form for menus/tooltips: "Ctrl+Shift+Z", "G", "".
    std::string toString() const;
};

class ShortcutMap {
public:
    static constexpr int kMaxSlots = 2; // bindings per command (GL parity)

    // Register a command's default binding(s). Also seeds the live table for
    // ids the persisted file didn't mention (or before any file is loaded).
    void registerDefault(const std::string& commandId,
                         const ShortcutBinding& primary,
                         const ShortcutBinding& secondary = ShortcutBinding{});

    // The live bindings of a command (empty when it has none).
    const std::vector<ShortcutBinding>& bindings(const std::string& commandId) const;
    // The command's REGISTERED defaults (empty when it has none) — the Pass-6
    // editor offers a per-row "reset to default" next to the global
    // resetToDefaults().
    const std::vector<ShortcutBinding>& defaults(const std::string& commandId) const;
    void setBinding(const std::string& commandId, int slot,
                    const ShortcutBinding& binding);
    void clearBinding(const std::string& commandId, int slot);

    // First valid binding as display text ("" when unbound) — menu shortcut
    // columns and tooltips read this so rebinds show up live everywhere.
    std::string label(const std::string& commandId) const;

    // The command bound to exactly (key, ctrl, alt, shift); nullptr when the
    // chord is free. Pointer valid until the map is next mutated.
    const std::string* commandForKey(int keyCode, bool ctrl, bool alt,
                                     bool shift) const;

    // The command already using `binding` (for the future editor's conflict
    // warning); nullptr when free. `excludeCommandId` skips self-conflicts.
    const std::string* findConflict(const ShortcutBinding& binding,
                                    const std::string& excludeCommandId) const;

    // Visit every (commandId, binding) pair — the dispatch loop and editor.
    void forEach(const std::function<void(const std::string& commandId,
                                          const ShortcutBinding& binding)>& fn) const;

    void resetToDefaults();

    // Load shortcuts.json: v2 native, or a GL-era v1 profile file (migrated
    // via the action-name table). Missing file / unknown ids keep defaults.
    // Returns true when a file was read.
    bool loadFromFile(const std::string& path);
    bool saveToFile(const std::string& path) const;

    // GL port: translate a physical GLFW key code to the key that carries
    // that label in the user's current keyboard layout (QWERTZ 'Z' etc.).
    // Keypad keys pass through untouched.
    static int normalizeKeyToLayout(int glfwKey);
    // Display name for a GLFW key code ("F5", "Space", "Z", ...).
    static std::string keyName(int glfwKey);

private:
    // commandId -> up to kMaxSlots bindings. std::map keeps iteration (and
    // the saved file) deterministically ordered.
    std::map<std::string, std::vector<ShortcutBinding>> bindings_;
    std::map<std::string, std::vector<ShortcutBinding>> defaults_;
};

} // namespace core
