#pragma once

// ============================================================================
//  MeasurementPlugin  —  adapter that drives the legacy MeasurementTool
//                        through the plugin system
// ----------------------------------------------------------------------------
//  This is the worked migration example referenced by docs/PLUGINS.md. Rather
//  than rewrite Tools::MeasurementTool, this thin adapter wraps the existing
//  global instance (declared in main.cpp) and exposes it through the Plugin
//  hooks, so all of its render / UI / menu / input now flow through the unified
//  PluginManager pipeline:
//
//    onInitializeGL  → MeasurementTool::initialize()
//    onRenderViewport→ MeasurementTool::render() (with the 3D-cursor preview)
//    onRenderMenu    → the "Measure" Tools-menu entry
//    onRenderUI      → the existing renderMeasurementToolWindow()
//    onMouseButton   → left-click places a point, right-click finishes
//    onKey           → Enter finishes, Delete cancels, Backspace undoes a point
//
//  The tool's editable data still lives in Engine::Scene::measurements, so
//  scene save/load and SnapshotManager are completely unaffected.
//
//  This "adapter wrapping a legacy global" is the recommended low-risk pattern
//  for migrating the remaining tools (BrushTool, ClipPlaneTool, ...).
// ============================================================================

#include "Plugins/Plugin.h"

namespace Plugins {

class MeasurementPlugin : public Plugin {
public:
    PluginInfo info() const override;

    void onInitializeGL(PluginContext& ctx) override;

    void onRenderViewport(PluginContext& ctx, const glm::mat4& projection,
                          const glm::mat4& view, const glm::vec3& cameraPos) override;
    void onRenderUI(PluginContext& ctx) override;
    void onRenderMenu(PluginContext& ctx) override;

    bool onMouseButton(PluginContext& ctx, int button, int action, int mods) override;
    bool onKey(PluginContext& ctx, int key, int scancode, int action, int mods) override;
};

} // namespace Plugins
