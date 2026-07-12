#pragma once

// ============================================================================
//  Gui panels  —  one dockable window per file (src/Gui/panels/*.cpp)
// ----------------------------------------------------------------------------
//  Every panel is a free function that opens its own ImGui window (Begin/End)
//  and edits app state through Gui::Services. The GuiSystem host owns the
//  per-panel visibility bool it passes as `open` (so the window's close button
//  and the View menu stay in sync) and docks the windows by the titles in
//  Gui::Windows below — keep the Begin() title and the DockBuilder name in
//  lockstep by using these constants on both sides.
// ============================================================================

namespace Gui {

class Services;

// Window titles — shared by the panels (Begin) and the dock-layout builder.
namespace Windows {
inline constexpr const char* Viewport    = "Viewport";
inline constexpr const char* Scene       = "Scene";
inline constexpr const char* Inspector   = "Inspector";
inline constexpr const char* Settings    = "Settings";
inline constexpr const char* Cursor      = "3D Cursor";
inline constexpr const char* PointClouds = "Point Clouds";
inline constexpr const char* ClipPlanes  = "Clip Planes";
inline constexpr const char* Diagnostics = "Performance";
inline constexpr const char* Slpk        = "Scene Layers";
inline constexpr const char* History     = "History";
inline constexpr const char* Snapshots   = "Snapshots";
} // namespace Windows

void drawScenePanel(Services& services, bool* open);
void drawInspectorPanel(Services& services, bool* open);
void drawSettingsPanel(Services& services, bool* open);
void drawCursorPanel(Services& services, bool* open);
void drawPointCloudPanel(Services& services, bool* open);
void drawClipPlanePanel(Services& services, bool* open);
void drawDiagnosticsPanel(Services& services, bool* open);
// SLPK / I3S scene layers (open + inspector; docs/SLPK_IMPLEMENTATION_PLAN.md).
void drawSlpkPanel(Services& services, bool* open);
// History timeline + named Snapshots (UI redesign Pass 3 §9).
void drawHistoryPanel(Services& services, bool* open);
void drawSnapshotsPanel(Services& services, bool* open);

} // namespace Gui
