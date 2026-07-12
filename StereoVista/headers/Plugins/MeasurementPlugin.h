#pragma once

// ============================================================================
//  MeasurementPlugin  —  migrates Tools::MeasurementTool onto the plugin system
// ----------------------------------------------------------------------------
//  The worked "tool as a plugin" example. Unlike the GL adapter (which wrapped a
//  global tool + a GUI window function), this plugin OWNS the MeasurementTool and
//  drives all of its overlay / UI / menu / input through the plugin hooks, so the
//  measurement tool needs no special-casing in the Application:
//
//    onBuildOverlay → tool.appendTo() with the live 3D-cursor preview
//    onRenderUI     → the settings window + world-space value labels
//    onRenderMenu   → the "Measure" Tools-menu entry
//    onMouseButton  → LMB places a point, RMB finishes the polyline
//    onKey          → Enter finishes, Delete cancels, Backspace undoes a point
// ============================================================================

#include "Plugins/Plugin.h"
#include "Tools/MeasurementTool.h"

namespace Plugins {

class MeasurementPlugin : public Plugin {
public:
    PluginInfo info() const override;

    // Binds the tool's measurement storage to scene::Scene::measurements
    // ("one heart", UI redesign Pass 1) so the Outliner and the scene document
    // see exactly what the tool creates.
    void onRegister(PluginContext& ctx) override;

    void onBuildOverlay(PluginContext& ctx) override;
    void onRenderUI(PluginContext& ctx) override;
    void onRenderMenu(PluginContext& ctx) override;
    bool onMouseButton(PluginContext& ctx, int button, int action, int mods) override;
    bool onKey(PluginContext& ctx, int key, int scancode, int action, int mods) override;

private:
    Tools::MeasurementTool m_tool;
};

} // namespace Plugins
