#include "Core/Shortcuts.h"

#include <GLFW/glfw3.h>
#include <json.h>

#include <algorithm>
#include <cctype>
#include <fstream>

namespace core {

namespace {

// GL-era shortcuts.json migration: the old ShortcutAction names that still
// have an equivalent action on the Vulkan branch, mapped to command ids.
// Actions whose feature does not exist here (VCT/HDR/radar/lighting cycles,
// standard views before the viewport-toolbar pass) are skipped — their
// features return via later passes/ports and will claim ids then.
struct LegacyAction {
    const char* glName;
    const char* commandId;
};
constexpr LegacyAction kLegacyActions[] = {
    { "ToggleGUI",          "view.toggle_gui" },
    { "TakeScreenshot",     "file.screenshot" },
    { "Undo",               "edit.undo" },
    { "Redo",               "edit.redo" },
    { "ImportModel",        "file.import_model" },
    { "ImportPointCloud",   "file.import_pointcloud" },
    { "DeleteObject",       "edit.delete_selected" },
    { "CenterView",         "view.center" },
    { "ToggleWireframe",    "view.wireframe" },
    { "GizmoModeMove",      "gizmo.translate" },
    { "GizmoModeRotate",    "gizmo.rotate" },
    { "GizmoModeScale",     "gizmo.scale" },
    { "GizmoToggleSpace",   "gizmo.toggle_space" },
    { "OpenSettings",       "view.panel.settings" },
    { "OpenCursorSettings", "view.panel.cursor" },
};

const std::vector<ShortcutBinding>& emptyBindings() {
    static const std::vector<ShortcutBinding> kEmpty;
    return kEmpty;
}

nlohmann::json bindingToJson(const ShortcutBinding& b) {
    // Field names match the GL KeyBinding serialization (familiarity; easy
    // hand-editing), minus the never-used-here mouseButton.
    nlohmann::json j;
    j["keyCode"] = b.keyCode;
    j["ctrl"] = b.ctrl;
    j["alt"] = b.alt;
    j["shift"] = b.shift;
    return j;
}

ShortcutBinding bindingFromJson(const nlohmann::json& j) {
    ShortcutBinding b;
    b.keyCode = j.value("keyCode", -1);
    b.ctrl = j.value("ctrl", false);
    b.alt = j.value("alt", false);
    b.shift = j.value("shift", false);
    return b;
}

} // namespace

// ── ShortcutBinding ─────────────────────────────────────────────────────────

std::string ShortcutBinding::toString() const {
    if (!valid())
        return std::string();
    std::string result;
    if (ctrl)  result += "Ctrl+";
    if (alt)   result += "Alt+";
    if (shift) result += "Shift+";
    result += ShortcutMap::keyName(keyCode);
    return result;
}

// ── ShortcutMap ─────────────────────────────────────────────────────────────

void ShortcutMap::registerDefault(const std::string& commandId,
                                  const ShortcutBinding& primary,
                                  const ShortcutBinding& secondary) {
    std::vector<ShortcutBinding> list;
    if (primary.valid())
        list.push_back(primary);
    if (secondary.valid())
        list.push_back(secondary);
    defaults_[commandId] = list;
    // Seed the live table unless something (a loaded file, the user) already
    // configured this command — loadFromFile may legitimately run first.
    if (bindings_.find(commandId) == bindings_.end())
        bindings_[commandId] = std::move(list);
}

const std::vector<ShortcutBinding>&
ShortcutMap::bindings(const std::string& commandId) const {
    const auto it = bindings_.find(commandId);
    return it == bindings_.end() ? emptyBindings() : it->second;
}

const std::vector<ShortcutBinding>&
ShortcutMap::defaults(const std::string& commandId) const {
    const auto it = defaults_.find(commandId);
    return it == defaults_.end() ? emptyBindings() : it->second;
}

void ShortcutMap::setBinding(const std::string& commandId, int slot,
                             const ShortcutBinding& binding) {
    if (slot < 0 || slot >= kMaxSlots)
        return;
    std::vector<ShortcutBinding>& list = bindings_[commandId];
    if (static_cast<int>(list.size()) <= slot)
        list.resize(slot + 1);
    list[slot] = binding;
    // Trim trailing unbound slots so label()/save stay tidy.
    while (!list.empty() && !list.back().valid())
        list.pop_back();
}

void ShortcutMap::clearBinding(const std::string& commandId, int slot) {
    setBinding(commandId, slot, ShortcutBinding{});
}

std::string ShortcutMap::label(const std::string& commandId) const {
    for (const ShortcutBinding& b : bindings(commandId))
        if (b.valid())
            return b.toString();
    return std::string();
}

const std::string* ShortcutMap::commandForKey(int keyCode, bool ctrl, bool alt,
                                              bool shift) const {
    const ShortcutBinding probe{ keyCode, ctrl, alt, shift };
    for (const auto& [commandId, list] : bindings_)
        for (const ShortcutBinding& b : list)
            if (b == probe)
                return &commandId;
    return nullptr;
}

const std::string*
ShortcutMap::findConflict(const ShortcutBinding& binding,
                          const std::string& excludeCommandId) const {
    if (!binding.valid())
        return nullptr;
    for (const auto& [commandId, list] : bindings_) {
        if (commandId == excludeCommandId)
            continue;
        for (const ShortcutBinding& b : list)
            if (b == binding)
                return &commandId;
    }
    return nullptr;
}

void ShortcutMap::forEach(
    const std::function<void(const std::string&, const ShortcutBinding&)>& fn)
    const {
    for (const auto& [commandId, list] : bindings_)
        for (const ShortcutBinding& b : list)
            if (b.valid())
                fn(commandId, b);
}

void ShortcutMap::resetToDefaults() {
    bindings_ = defaults_;
}

bool ShortcutMap::loadFromFile(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open())
        return false;
    nlohmann::json doc;
    try {
        file >> doc;
    } catch (const std::exception&) {
        return false; // corrupt file: keep defaults, will be rewritten on save
    }

    if (doc.contains("bindings") && doc["bindings"].is_object()) {
        // v2 native: entries present in the file REPLACE that command's
        // bindings entirely (an empty array = deliberately unbound); ids the
        // file doesn't mention keep their registered defaults.
        for (const auto& [commandId, arr] : doc["bindings"].items()) {
            std::vector<ShortcutBinding> list;
            if (arr.is_array())
                for (const nlohmann::json& jb : arr) {
                    const ShortcutBinding b = bindingFromJson(jb);
                    if (b.valid() &&
                        static_cast<int>(list.size()) < kMaxSlots)
                        list.push_back(b);
                }
            bindings_[commandId] = std::move(list);
        }
        return true;
    }

    if (doc.contains("profiles") && doc["profiles"].is_array()) {
        // GL-era v1 profile file: migrate the active profile's bindings for
        // every action that still exists (see kLegacyActions). Keyboard
        // bindings only — the GL format's mouse-button slots don't apply.
        const std::string active = doc.value("activeProfile", "Default");
        const nlohmann::json* profile = nullptr;
        for (const nlohmann::json& p : doc["profiles"]) {
            if (p.value("name", "") == active) {
                profile = &p;
                break;
            }
        }
        if (!profile && !doc["profiles"].empty())
            profile = &doc["profiles"].front();
        if (!profile || !profile->contains("bindings"))
            return true;

        for (const LegacyAction& legacy : kLegacyActions) {
            const auto it = (*profile)["bindings"].find(legacy.glName);
            if (it == (*profile)["bindings"].end() || !it->is_array())
                continue;
            std::vector<ShortcutBinding> list;
            for (const nlohmann::json& jb : *it) {
                if (jb.value("mouseButton", -1) != -1)
                    continue; // keyboard-only in the new table
                const ShortcutBinding b = bindingFromJson(jb);
                if (b.valid() && static_cast<int>(list.size()) < kMaxSlots)
                    list.push_back(b);
            }
            if (!list.empty())
                bindings_[legacy.commandId] = std::move(list);
        }
        return true;
    }

    return true; // unknown shape: keep defaults
}

bool ShortcutMap::saveToFile(const std::string& path) const {
    nlohmann::json doc;
    doc["version"] = 2;
    nlohmann::json& out = doc["bindings"] = nlohmann::json::object();
    for (const auto& [commandId, list] : bindings_) {
        nlohmann::json arr = nlohmann::json::array();
        for (const ShortcutBinding& b : list)
            if (b.valid())
                arr.push_back(bindingToJson(b));
        out[commandId] = std::move(arr);
    }
    std::ofstream file(path);
    if (!file.is_open())
        return false;
    file << doc.dump(4);
    return file.good();
}

int ShortcutMap::normalizeKeyToLayout(int glfwKey) {
    // The keypad has the same physical layout on every keyboard and is a
    // distinct set of keys from the main number row; glfwGetKeyName reports
    // names for it too (e.g. "1" for KP_1), which would fold numpad bindings
    // onto the top-row digits. Leave every keypad key untouched. (GL port.)
    if (glfwKey >= GLFW_KEY_KP_0 && glfwKey <= GLFW_KEY_KP_EQUAL)
        return glfwKey;

    const char* name = glfwGetKeyName(glfwKey, 0);
    if (name && name[0] != '\0' && name[1] == '\0') {
        const char c = name[0];
        if (c >= 'a' && c <= 'z')
            return GLFW_KEY_A + (c - 'a');
        if (c >= 'A' && c <= 'Z')
            return GLFW_KEY_A + (c - 'A');
        if (c >= '0' && c <= '9')
            return GLFW_KEY_0 + (c - '0');
    }
    return glfwKey;
}

std::string ShortcutMap::keyName(int glfwKey) {
    // Ranges first (letters, digits, function keys, keypad), then the named
    // specials; printable punctuation falls back to glfwGetKeyName.
    if (glfwKey >= GLFW_KEY_A && glfwKey <= GLFW_KEY_Z)
        return std::string(1, char('A' + (glfwKey - GLFW_KEY_A)));
    if (glfwKey >= GLFW_KEY_0 && glfwKey <= GLFW_KEY_9)
        return std::string(1, char('0' + (glfwKey - GLFW_KEY_0)));
    if (glfwKey >= GLFW_KEY_F1 && glfwKey <= GLFW_KEY_F25)
        return "F" + std::to_string(glfwKey - GLFW_KEY_F1 + 1);
    if (glfwKey >= GLFW_KEY_KP_0 && glfwKey <= GLFW_KEY_KP_9)
        return "Numpad " + std::to_string(glfwKey - GLFW_KEY_KP_0);

    switch (glfwKey) {
    case GLFW_KEY_SPACE:        return "Space";
    case GLFW_KEY_ESCAPE:       return "Esc";
    case GLFW_KEY_ENTER:        return "Enter";
    case GLFW_KEY_TAB:          return "Tab";
    case GLFW_KEY_BACKSPACE:    return "Backspace";
    case GLFW_KEY_INSERT:       return "Insert";
    case GLFW_KEY_DELETE:       return "Delete";
    case GLFW_KEY_RIGHT:        return "Right";
    case GLFW_KEY_LEFT:         return "Left";
    case GLFW_KEY_DOWN:         return "Down";
    case GLFW_KEY_UP:           return "Up";
    case GLFW_KEY_PAGE_UP:      return "Page Up";
    case GLFW_KEY_PAGE_DOWN:    return "Page Down";
    case GLFW_KEY_HOME:         return "Home";
    case GLFW_KEY_END:          return "End";
    case GLFW_KEY_PAUSE:        return "Pause";
    case GLFW_KEY_KP_DECIMAL:   return "Numpad .";
    case GLFW_KEY_KP_DIVIDE:    return "Numpad /";
    case GLFW_KEY_KP_MULTIPLY:  return "Numpad *";
    case GLFW_KEY_KP_SUBTRACT:  return "Numpad -";
    case GLFW_KEY_KP_ADD:       return "Numpad +";
    case GLFW_KEY_KP_ENTER:     return "Numpad Enter";
    case GLFW_KEY_KP_EQUAL:     return "Numpad =";
    default:
        break;
    }

    const char* name = glfwGetKeyName(glfwKey, 0);
    if (name && name[0] != '\0') {
        std::string s(name);
        if (s.size() == 1)
            s[0] = char(std::toupper(static_cast<unsigned char>(s[0])));
        return s;
    }
    return "Key " + std::to_string(glfwKey);
}

} // namespace core
