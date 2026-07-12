#include "App/Application.h"

#include "Plugins/Plugin.h"
#include "Plugins/PluginManager.h"
#include "Tools/ClipPlaneTool.h"

#include "imgui/IconsFontAwesome5.h"

#include <string>

// ============================================================================
// Tool registration (UI redesign Pass 7 §13).
//
// Every interactive MODE lands in core::ToolManager, and every surface that
// shows tools renders from it: the Tools menu, the command palette, the
// status-bar chip, the Inspector's Tool card and the viewport toolbar. Adding a
// tool = one registration here (or, for a plugin, none at all — a plugin that
// declares PluginCategory::Tool is picked up automatically below).
//
// The registry stores CLOSURES, not tool objects, because the tools are
// heterogeneous: measurement lives inside a Plugin (which owns its enabled
// state), the clip-plane tool is app-owned, and a future tool may be neither.
// ToolManager::active() asks each tool whether it is enabled rather than caching
// a pointer, so a plugin the user toggles from its own window still reads as the
// active tool everywhere.
// ============================================================================

namespace app {

void Application::registerTools() {
    using core::Tool;

    // ── Plugin-backed tools ─────────────────────────────────────────────────
    // Any loaded plugin in the Tool category becomes a mode automatically — the
    // measurement plugin included. No per-plugin code here (§16.1 recipe).
    for (Plugins::Plugin* plugin : pluginManager_.all()) {
        const Plugins::PluginInfo info = plugin->info();
        if (info.category != Plugins::PluginCategory::Tool)
            continue;
        Tool tool;
        tool.id = "tool." + info.id;
        tool.title = info.name;
        tool.description = info.description;
        tool.icon = ICON_FA_RULER_COMBINED;
        tool.isActive = [plugin] { return plugin->isEnabled(); };
        tool.setActive = [plugin](bool on) { plugin->setEnabled(on); };
        toolManager_.add(std::move(tool));
    }

    // ── App-owned tools ─────────────────────────────────────────────────────
    {
        Tool tool;
        tool.id = "tool.clipplane";
        tool.title = "Section plane";
        tool.description =
            "Add and edit clipping planes. Clipping applies whenever enabled "
            "planes exist; this mode drives the editing overlay and the gizmo.";
        tool.icon = ICON_FA_CUT;
        tool.isActive = [this] { return clipPlaneTool_.isEnabled(); };
        tool.setActive = [this](bool on) { clipPlaneTool_.setEnabled(on); };
        toolManager_.add(std::move(tool));
    }

    // ── One command per tool (C5): the menu, the palette, the toolbar and any
    //    shortcut all run the SAME toggle. ──────────────────────────────────
    for (const core::Tool& tool : toolManager_.tools()) {
        core::Command command;
        command.id = tool.id;
        command.title = tool.title;
        command.category = "Tools";
        command.keywords = "tool mode " + tool.title;
        command.tooltip = tool.description;
        command.icon = tool.icon;
        const std::string id = tool.id;
        command.checked = [this, id] {
            const core::Tool* t = toolManager_.find(id);
            return t && t->isActive();
        };
        command.action = [this, id] { toolManager_.toggle(id); };
        commands_.add(std::move(command));
    }
}

} // namespace app
