#ifndef NOMINMAX
#define NOMINMAX
#endif

#include "Plugins/MeasurementPlugin.h"
#include "Plugins/PluginRegistry.h"
#include "Tools/MeasurementTool.h"
#include "Gui/Gui.h"

// Legacy globals owned by main.cpp / GUI.cpp that this adapter bridges.
extern Tools::MeasurementTool measurementTool;
extern bool showMeasurementToolWindow;

namespace Plugins {

PluginInfo MeasurementPlugin::info() const {
    PluginInfo meta;
    meta.id          = "stereovista.measurement";
    meta.name        = "Measure";
    meta.description = "Measure distances, angles and coordinates in the scene.";
    meta.version     = "1.0.0";
    meta.category    = PluginCategory::Tool;
    meta.shortcut    = "M";
    return meta;
}

void MeasurementPlugin::onInitializeGL(PluginContext& /*ctx*/) {
    // Create the overlay GL resources up front (also done lazily by render()).
    measurementTool.initialize();
}

void MeasurementPlugin::onRenderViewport(PluginContext& ctx, const glm::mat4& projection,
                                         const glm::mat4& view,
                                         const glm::vec3& /*cameraPos*/) {
    // Committed measurements always draw; the live rubber-band preview only
    // while the tool is enabled and the 3D cursor is over geometry.
    glm::vec3 cursor;
    const glm::vec3* preview = nullptr;
    if (measurementTool.isEnabled() && ctx.cursorWorldPos(cursor))
        preview = &cursor;
    measurementTool.render(projection, view, preview);
}

void MeasurementPlugin::onRenderMenu(PluginContext& /*ctx*/) {
    // Mirrors the former static Tools-menu entry; keeps using the existing
    // window-visibility flag so the "M" shortcut (which sets it) still works.
    ImGui::MenuItem("Measure", "M", &showMeasurementToolWindow);
    if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Measure distances, angles and coordinates in the scene");
}

void MeasurementPlugin::onRenderUI(PluginContext& /*ctx*/) {
    if (showMeasurementToolWindow)
        renderMeasurementToolWindow();
}

bool MeasurementPlugin::onMouseButton(PluginContext& ctx, int button, int action,
                                      int mods) {
    if (action != GLFW_PRESS) return false;

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        // Ctrl/Alt clicks fall through to object selection / free-move.
        if (mods & (GLFW_MOD_CONTROL | GLFW_MOD_ALT)) return false;
        if (!measurementTool.isEnabled()) return false;
        glm::vec3 cursor;
        if (ctx.cursorWorldPos(cursor))
            measurementTool.addPoint(cursor);
        return true; // consume: no orbiting/selection while measuring
    }

    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        // Right-click finishes the in-progress polyline instead of rotating.
        if (measurementTool.isEnabled() && measurementTool.hasActive()) {
            measurementTool.finishActive();
            return true;
        }
    }
    return false;
}

bool MeasurementPlugin::onKey(PluginContext& /*ctx*/, int key, int /*scancode*/,
                              int action, int mods) {
    if (action != GLFW_PRESS) return false;
    if (!(measurementTool.isEnabled() && measurementTool.hasActive())) return false;
    (void)mods;

    if (key == GLFW_KEY_ENTER || key == GLFW_KEY_KP_ENTER) {
        measurementTool.finishActive();
        return true;
    }
    if (key == GLFW_KEY_DELETE) {
        measurementTool.cancelActive();
        return true;
    }
    if (key == GLFW_KEY_BACKSPACE) {
        measurementTool.undoLastPoint();
        return true;
    }
    return false;
}

} // namespace Plugins

// Self-register so the PluginManager instantiates the adapter at startup.
REGISTER_PLUGIN(Plugins::MeasurementPlugin);
