#include "Core/ToolManager.h"

// The tool registry (see Core/ToolManager.h). Behaviour only — the tools
// themselves live wherever they already lived (a Plugin, the Application), and
// this just gives every UI surface one place to ask about them.

namespace core {

void ToolManager::add(Tool tool) {
    if (tool.id.empty() || !tool.isActive || !tool.setActive)
        return;
    for (Tool& existing : tools_)
        if (existing.id == tool.id) {
            existing = std::move(tool); // replace in place, keep the order
            return;
        }
    tools_.push_back(std::move(tool));
}

const Tool* ToolManager::find(const std::string& id) const {
    for (const Tool& tool : tools_)
        if (tool.id == id)
            return &tool;
    return nullptr;
}

const Tool* ToolManager::active() const {
    // Ask the tools rather than caching: a plugin the user toggled from its own
    // window (or a tool a script enabled) must still read as active everywhere.
    for (const Tool& tool : tools_)
        if (tool.isActive && tool.isActive())
            return &tool;
    return nullptr;
}

void ToolManager::activate(const std::string& id) {
    for (const Tool& tool : tools_)
        if (tool.id != id && tool.isActive && tool.isActive())
            tool.setActive(false); // one mode at a time
    if (const Tool* tool = find(id))
        tool->setActive(true);
}

void ToolManager::toggle(const std::string& id) {
    const Tool* tool = find(id);
    if (!tool)
        return;
    if (tool->isActive())
        tool->setActive(false);
    else
        activate(id);
}

bool ToolManager::deactivateAll() {
    bool exited = false;
    for (const Tool& tool : tools_)
        if (tool.isActive && tool.isActive()) {
            tool.setActive(false);
            exited = true;
        }
    return exited;
}

} // namespace core
