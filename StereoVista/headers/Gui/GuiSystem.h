#pragma once

// ============================================================================
//  Gui::GuiSystem  —  the App Shell: dockspace host, menu bar, status bar
//  (UI redesign Pass 0; docs/UI_REDESIGN.md §5).
// ----------------------------------------------------------------------------
//  Application owns one GuiSystem and calls draw() once per ImGui frame. The
//  system:
//    * lays out a dockspace over the main viewport with a passthru central
//      node (the 3D scene shows through where nothing is docked),
//    * draws the main menu bar — every item is a CommandRegistry command
//      (contract C5); GuiSystem curates the grouping, the registry owns the
//      behaviour, and shortcut labels come live from the ShortcutMap,
//    * draws the thin status bar along the bottom of the main window
//      (scene stats, selection, background progress, FPS — Pass 8 completes
//      it). It is window chrome like the menu bar, not a dockable panel,
//    * establishes a sensible default dock layout on a fresh profile and on
//      an explicit "Reset Layout", while respecting a layout restored from
//      imgui.ini,
//    * draws each enabled panel (Gui/panels/*; visibility lives in
//      Settings::Ui::Panels so it persists), and hosts the plugin windows.
//
//  All app state flows through the Gui::Services facade passed to draw().
// ============================================================================

namespace Gui {

class Services;

class GuiSystem {
public:
    // One call per ImGui frame (between ImGui::NewFrame and ImGui::Render).
    void draw(Services& services);

    // Master GUI visibility (menu bar + viewports stay so it can be toggled
    // back). Bound to the "view.toggle_gui" command (default shortcut: G).
    void toggleGuiVisible() { guiVisible_ = !guiVisible_; }
    bool guiVisible() const { return guiVisible_; }

    // Force the default dock layout on the next frame ("view.reset_layout").
    void requestResetLayout() { resetLayout_ = true; }

    // The About window toggle behind the "help.about" command.
    void toggleAbout() { showAbout_ = !showAbout_; }
    bool aboutVisible() const { return showAbout_; }

private:
    void drawMenuBar(Services& services);
    void drawStatusBar(Services& services);
    // One 3D viewport as a real dockable window (the primary owns the central
    // node by default; extra viewports float until docked): displays the
    // renderer's offscreen viewport texture and reports size + input state
    // back through Services::onViewportPanel(index, ...). Always drawn (not
    // gated by the master GUI toggle — it IS the application content).
    void drawViewportPanel(Services& services, unsigned int index);
    void drawAboutWindow(Services& services);
    // Build the default dock layout into the given dockspace node.
    void buildDefaultLayout(unsigned int dockspaceId, float sizeX, float sizeY);

    bool showAbout_ = false;

    bool guiVisible_        = true;
    bool resetLayout_       = false;
    bool layoutInitialized_ = false;
};

} // namespace Gui
