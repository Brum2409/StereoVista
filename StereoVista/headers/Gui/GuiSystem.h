#pragma once

// ============================================================================
//  Gui::GuiSystem  —  the dockspace host + main menu bar that replaces the
//  interim debug panel's buildUi().
// ----------------------------------------------------------------------------
//  Application owns one GuiSystem and calls draw() once per ImGui frame. The
//  system:
//    * lays out a dockspace over the main viewport with a passthru central
//      node (the 3D scene shows through where nothing is docked),
//    * draws the main menu bar (File / View / Tools / Help + panel toggles),
//    * establishes a sensible default dock layout on a fresh profile and on an
//      explicit "Reset Layout", while respecting a layout restored from
//      imgui.ini,
//    * draws each enabled panel (Gui/panels/*), and hosts the plugin windows.
//
//  It owns nothing but its own visibility flags — all app state flows through
//  the Gui::Services facade passed to draw().
// ============================================================================

namespace Gui {

class Services;

class GuiSystem {
public:
    // One call per ImGui frame (between ImGui::NewFrame and ImGui::Render).
    void draw(Services& services);

    // Master GUI visibility (menu bar stays so it can be toggled back). Bound to
    // the View menu item and the F1 shortcut.
    void toggleGuiVisible() { guiVisible_ = !guiVisible_; }
    bool guiVisible() const { return guiVisible_; }

    // Force the default dock layout on the next frame.
    void requestResetLayout() { resetLayout_ = true; }

private:
    void drawMenuBar(Services& services);
    void drawAboutWindow(Services& services);
    // Build the default dock layout into the given dockspace node.
    void buildDefaultLayout(unsigned int dockspaceId, float sizeX, float sizeY);

    // Panel visibility (also toggled from the View menu).
    bool showScene_       = true;
    bool showInspector_   = true;
    bool showSettings_    = true;
    bool showCursor_      = false;
    bool showPointClouds_ = true;
    bool showClipPlanes_  = false;
    bool showDiagnostics_ = false;
    bool showSlpk_        = true;

    bool showAbout_ = false;

    bool guiVisible_       = true;
    bool resetLayout_      = false;
    bool layoutInitialized_ = false;
};

} // namespace Gui
