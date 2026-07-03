#include "Gui/Gui.h"
#include "Core/Camera.h"
#include "Core/SnapshotManager.h"
#include "Core/UndoManager.h"
#include "Core/Voxalizer.h"
#include "Cursors/Base/CursorManager.h"
#include "Engine/BVHDebug.h"
#include "Engine/Core.h"
#include "Engine/Screenshot.h"
#include "Engine/ShortcutManager.h"
#include "Engine/SpaceMouseInput.h"
#include "Engine/ThreeDConnexionSync.h"
#include "Engine/XRRuntimeInfo.h" // OpenXR diagnostics/runtime-picker data (no XR headers)
#include "Gui/GuiTypes.h"
#include "Tools/BrushTool.h"
#include "Tools/ClipPlaneTool.h"
#include "Tools/MeasurementTool.h"
#include "Tools/TransformGizmo.h"
#include "Plugins/PluginManager.h"
#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui_sytle.h"
#include "libs/portable-file-dialogs.h"
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json.h>
#include <map>
#include <set>
#include <sstream>
#include <utility>

using namespace GUI;

// Forward declarations
void updateSpaceMouseBounds();
void applyStandardView(int viewId); // defined in main.cpp
bool frameSelectedObject();          // defined in main.cpp
void updateSpaceMouseCursorAnchor();
void renderPointLightManipulationPanel();
void renderSpotLightManipulationPanel();
static void drawMeasurementLabels();

// With ImGuiConfigFlags_ViewportsEnable (docking branch) ImGui window positions
// are desktop-absolute, not main-window-relative. The HUD chrome below (corner
// overlays, toasts, the empty-scene hint and the fixed Scene Hierarchy panel)
// is authored in main-window-relative pixels (0,0 = top-left of the app
// window's client area), so it must be offset by the main viewport origin and
// pinned to the main viewport -- otherwise it would jump to the primary
// monitor's origin whenever the application window is moved or lives on a
// second monitor. When the window sits at the desktop origin this is a no-op,
// so it is also correct without viewports enabled.
static void SetNextWindowPosInMainWindow(const ImVec2 &relPos,
                                         ImGuiCond cond = 0,
                                         const ImVec2 &pivot = ImVec2(0, 0)) {
  const ImGuiViewport *mvp = ImGui::GetMainViewport();
  ImGui::SetNextWindowViewport(mvp->ID);
  ImGui::SetNextWindowPos(ImVec2(mvp->Pos.x + relPos.x, mvp->Pos.y + relPos.y),
                          cond, pivot);
}

// Application globals used throughout the GUI system
extern int windowWidth;
extern int windowHeight;
extern Engine::Scene currentScene;
extern Camera camera;
extern bool showGui;
extern bool showFPS;
extern bool g_requestScreenshot;
extern std::string g_screenshotPath;
extern bool isDarkTheme;
extern bool isStereoWindow;
extern bool showInfoWindow;
extern bool showSettingsWindow;
extern bool show3DCursor;
extern bool showCursorSettingsWindow;
extern bool showBrushToolWindow;
extern Cursor::CursorManager cursorManager;
extern Engine::Voxelizer *voxelizer;
extern float ambientStrengthFromSkybox;
extern float mouseSmoothingFactor;
extern bool orbitFollowsCursor;
extern float convergenceSmoothingSpeed;

// SpaceMouse variables
extern SpaceMouseInput spaceMouseInput;
extern bool spaceMouseInitialized;
extern ThreeDConnexionSync tdxSync;

// Cursor variables
extern Cursor::CursorManager cursorManager;

// Brush tool
extern Tools::BrushTool brushTool;

// Measurement tool
extern Tools::MeasurementTool measurementTool;
extern bool showMeasurementToolWindow;

// Section / clip plane tool
extern Tools::ClipPlaneTool clipPlaneTool;
extern bool showClipPlaneToolWindow;

// Plugin system (owned by main.cpp). g_pluginContext is a reference to the
// concrete services API; both are used to dispatch the Tools-menu entries and
// the plugin ImGui windows.
extern Plugins::PluginManager g_pluginManager;
extern Plugins::PluginContext &g_pluginContext;

// Snapshots
extern bool showSnapshotsWindow;

// OpenXR integration (defined in main.cpp; strings come from XRSession)
extern bool        g_xrAvailable;   // true once init() succeeded at least once
extern std::string g_xrStatusMsg;   // current status/error text
extern std::string g_xrRuntimeName; // name of the runtime (e.g. "SteamVR")
extern Engine::XRDiagnostics g_xrDiagnostics; // active + installed runtimes
extern std::string g_xrActiveOverride;        // forced runtime ("" = system default)
// Called by the GUI toggle to create / destroy the XR session.
void xrSessionEnable(bool enable);
// Re-scan the system for installed OpenXR runtimes.
void xrRefreshDiagnostics();
// Force a specific runtime for this process and (re)start XR ("" = system default).
void xrUseRuntime(const std::string &manifestPath);

// Defined in main.cpp: place planes using the 3D cursor / selection context.
void addClipPlaneAtCursor();
void addClipPlaneAxisAligned(int axis);

// Transform gizmo
extern Tools::TransformGizmo transformGizmo;

// Defined later in this file; used by the gizmo controls below.
static void DrawHelpMarker(const char *desc);

// Shared transform-gizmo controls (mode / space / snap), drawn inside the
// Transform panel of any selectable object.
static void DrawTransformGizmoControls(bool canRotateScale) {
  using G = Tools::TransformGizmo;
  ImGui::Spacing();
  ImGui::Checkbox("Show Gizmo", &transformGizmo.enabled);
  if (!transformGizmo.enabled)
    return;

  G::Mode mode = transformGizmo.mode();
  auto modeButton = [&](const char *label, G::Mode m, bool enabledBtn) {
    if (!enabledBtn)
      ImGui::BeginDisabled();
    bool active = (mode == m);
    if (active)
      ImGui::PushStyleColor(ImGuiCol_Button,
                            ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
    if (ImGui::Button(label))
      transformGizmo.setMode(m);
    if (active)
      ImGui::PopStyleColor();
    if (!enabledBtn)
      ImGui::EndDisabled();
  };
  modeButton("Move", G::Mode::Translate, true);
  ImGui::SameLine();
  modeButton("Rotate", G::Mode::Rotate, canRotateScale);
  ImGui::SameLine();
  modeButton("Scale", G::Mode::Scale, canRotateScale);
  ImGui::SameLine();
  bool world = (transformGizmo.space == G::Space::World);
  if (ImGui::Button(world ? "World" : "Local"))
    transformGizmo.toggleSpace();

  ImGui::Checkbox("Snap", &transformGizmo.snapEnabled);
  if (transformGizmo.snapEnabled) {
    ImGui::DragFloat("Move Snap", &transformGizmo.snapTranslate, 0.01f, 0.001f,
                     10.0f, "%.3f");
    ImGui::DragFloat("Rotate Snap", &transformGizmo.snapRotateDeg, 0.5f, 1.0f,
                     90.0f, "%.0f°");
    ImGui::DragFloat("Scale Snap", &transformGizmo.snapScale, 0.01f, 0.001f,
                     10.0f, "%.3f");
  }
  ImGui::DragFloat("Gizmo Size", &transformGizmo.screenSize, 0.005f, 0.08f,
                   0.5f, "%.3f");
  ImGui::SameLine();
  DrawHelpMarker("On-screen gizmo size as a fraction of the viewport height. "
                 "Constant apparent size regardless of zoom, distance or FOV.");
  ImGui::TextDisabled(
      "Default keys: 1 Move  2 Rotate  3 Scale  4 World/Local");
  ImGui::TextDisabled("(rebindable in Settings > Shortcuts)");
  ImGui::TextDisabled("Hold Shift while dragging to snap");
}

// 3D viewport rectangle (window pixels) — used to project measurement labels
extern int g_viewportX;
extern int g_viewportTopInset;
extern int g_viewportWidth;
extern int g_viewportHeight;
extern float aspectRatio;
extern StereoVista::ShortcutManager shortcutManager;

// Floating mode toolbar drawn as a small rounded bubble in the top-right of the
// 3D viewport while an object is selected. Mirrors the gizmo's current mode and
// shows the (rebindable) shortcut keys as tooltips.
static void renderGizmoViewportToolbar() {
  using G = Tools::TransformGizmo;
  using SA = StereoVista::ShortcutAction;
  if (!transformGizmo.enabled || !transformGizmo.hasTarget())
    return;
  if (g_viewportWidth <= 0 || g_viewportHeight <= 0)
    return;

  auto keyFor = [&](SA a) -> std::string {
    const StereoVista::ShortcutProfile *prof = shortcutManager.getActiveProfile();
    if (!prof)
      return "";
    const std::vector<StereoVista::KeyBinding> &b = prof->getBindings(a);
    if (b.empty() || b[0].isEmpty())
      return "";
    return b[0].toString();
  };

  const float scale = g_GuiScale.currentScale;
  const float margin = 12.0f * scale;
  SetNextWindowPosInMainWindow(
      ImVec2(g_viewportX + g_viewportWidth - margin, g_viewportTopInset + margin),
      ImGuiCond_Always, ImVec2(1.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.78f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(6.0f * scale, 6.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * scale);

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoSavedSettings;

  if (ImGui::Begin("##GizmoToolbar", nullptr, flags)) {
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    // Use the natural frame height (fontSize + 2*FramePadding.y). Forcing a
    // shorter fixed height stops ImGui from vertically centering the label,
    // which made the button text sit too low.
    const ImVec2 btn(0.0f, ImGui::GetFrameHeight());

    auto modeBtn = [&](const char *label, G::Mode m, bool enabledBtn, SA act) {
      bool active = (transformGizmo.mode() == m);
      if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
      if (!enabledBtn)
        ImGui::BeginDisabled();
      if (ImGui::Button(label, btn))
        transformGizmo.setMode(m);
      if (!enabledBtn)
        ImGui::EndDisabled();
      if (active)
        ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) {
        std::string k = keyFor(act);
        ImGui::SetTooltip("%s%s%s", label, k.empty() ? "" : "  [",
                          k.empty() ? "" : (k + "]").c_str());
      }
    };

    bool rs = transformGizmo.canRotate(); // rotate/scale availability
    modeBtn("Move", G::Mode::Translate, true, SA::GizmoModeMove);
    ImGui::SameLine();
    modeBtn("Rotate", G::Mode::Rotate, rs, SA::GizmoModeRotate);
    ImGui::SameLine();
    modeBtn("Scale", G::Mode::Scale, transformGizmo.canScale(),
            SA::GizmoModeScale);
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    bool world = (transformGizmo.space == G::Space::World);
    if (ImGui::Button(world ? "World" : "Local", btn))
      transformGizmo.toggleSpace();
    if (ImGui::IsItemHovered()) {
      std::string k = keyFor(SA::GizmoToggleSpace);
      ImGui::SetTooltip("Transform space%s%s%s",
                        k.empty() ? "" : "  [", k.empty() ? "" : k.c_str(),
                        k.empty() ? "" : "]");
    }
    ImGui::SameLine();
    if (ImGui::SmallButton(transformGizmo.snapEnabled ? "Snap:On" : "Snap:Off"))
      transformGizmo.snapEnabled = !transformGizmo.snapEnabled;
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Toggle snapping (hold Shift while dragging for "
                        "momentary snap)");
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
}

extern bool g_unlitMode;

// Floating "View" toolbar in the top-left of the viewport. Always available
// (these are global view-shading toggles, independent of selection). Surfaces
// the rebindable shortcut keys as tooltips.
static void renderViewModeToolbar() {
  using SA = StereoVista::ShortcutAction;
  if (g_viewportWidth <= 0 || g_viewportHeight <= 0)
    return;

  auto keyFor = [&](SA a) -> std::string {
    const StereoVista::ShortcutProfile *prof = shortcutManager.getActiveProfile();
    if (!prof)
      return "";
    const std::vector<StereoVista::KeyBinding> &b = prof->getBindings(a);
    if (b.empty() || b[0].isEmpty())
      return "";
    return b[0].toString();
  };

  const float scale = g_GuiScale.currentScale;
  const float margin = 12.0f * scale;
  SetNextWindowPosInMainWindow(
      ImVec2(g_viewportX + margin, g_viewportTopInset + margin),
      ImGuiCond_Always, ImVec2(0.0f, 0.0f));
  ImGui::SetNextWindowBgAlpha(0.78f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 10.0f * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(6.0f * scale, 6.0f * scale));
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 6.0f * scale);

  ImGuiWindowFlags flags =
      ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoResize |
      ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoScrollbar |
      ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_AlwaysAutoResize |
      ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoFocusOnAppearing |
      ImGuiWindowFlags_NoSavedSettings;

  if (ImGui::Begin("##ViewModeToolbar", nullptr, flags)) {
    const ImVec4 accent = ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive);
    // Use the natural frame height (fontSize + 2*FramePadding.y). Forcing a
    // shorter fixed height stops ImGui from vertically centering the label,
    // which made the button text sit too low.
    const ImVec2 btn(0.0f, ImGui::GetFrameHeight());

    auto toggleBtn = [&](const char *label, bool active, const char *tip,
                         SA act) {
      if (active)
        ImGui::PushStyleColor(ImGuiCol_Button, accent);
      bool clicked = ImGui::Button(label, btn);
      if (active)
        ImGui::PopStyleColor();
      if (ImGui::IsItemHovered()) {
        std::string k = keyFor(act);
        if (k.empty())
          ImGui::SetTooltip("%s", tip);
        else
          ImGui::SetTooltip("%s  [%s]", tip, k.c_str());
      }
      return clicked;
    };

    // Shading: Lit / Unlit (mutually exclusive)
    if (toggleBtn("Lit", !g_unlitMode, "Lit shading", SA::ToggleUnlit))
      g_unlitMode = false;
    ImGui::SameLine();
    if (toggleBtn("Unlit", g_unlitMode, "Unlit (albedo-only) shading",
                  SA::ToggleUnlit))
      g_unlitMode = true;
    ImGui::SameLine();
    ImGui::TextUnformatted("|");
    ImGui::SameLine();
    // Wireframe overlay toggle (independent of shading)
    if (toggleBtn("Wireframe", camera.wireframe, "Wireframe (triangle edges)",
                  SA::ToggleWireframe))
      camera.wireframe = !camera.wireframe;
  }
  ImGui::End();
  ImGui::PopStyleVar(3);
}

extern GUI::LightingMode currentLightingMode;
extern bool enableShadows;
extern GUI::VCTSettings vctSettings;
extern GUI::ApplicationPreferences::RadianceSettings radianceSettings;
extern bool enableBVH;
extern bool enableTwoLevelBVH;
extern bool showBVHDebug;
extern Engine::BVHDebugRenderer bvhDebugRenderer;
extern bool g_ddgiResetRequested; // set by GUI, consumed in the render loop

// Selection state for object interaction
extern enum class SelectedType {
  None,
  Model,
  PointCloud,
  Sun,
  PointLight,
  SpotLight,
  BrushCluster
} currentSelectedType;
extern int currentSelectedIndex;
extern int currentSelectedMeshIndex;

extern Engine::Sun sun;
extern std::vector<Engine::PointLight> pointLights;
extern std::vector<Engine::SpotLight> spotLights;

// Skybox configuration
extern GUI::SkyboxConfig skyboxConfig;
extern std::vector<GUI::CubemapPreset> cubemapPresets;

// User preferences and presets
extern GUI::ApplicationPreferences preferences;
extern std::string currentPresetName;
extern bool isEditingPresetName;
extern char editPresetNameBuffer[256];
extern GUI::CursorPreview3D cursorPreview3D;

// Shortcut manager
extern StereoVista::ShortcutManager shortcutManager;

// Files dropped onto the window (queued by the GLFW drop callback in main.cpp)
extern std::vector<std::string> g_droppedFiles;

// External function declarations
extern void savePreferences();
extern void updateSkybox();

// Constants
extern const int MAX_LIGHTS;

// ===========================================================================
// Redesigned GUI: navigation state + reusable modern widgets
// ===========================================================================

// Categories for the redesigned, sidebar-navigated Settings window. Declared at
// file scope so the main menu bar can deep-link straight to a category.
enum SettingsCategory {
  SETTINGS_CAT_RENDERING = 0,
  SETTINGS_CAT_CAMERA,
  SETTINGS_CAT_ENVIRONMENT,
  SETTINGS_CAT_DISPLAY,
  SETTINGS_CAT_INPUT,
  SETTINGS_CAT_IMPORT,
  SETTINGS_CAT_SHORTCUTS
};
static int g_settingsCategory = SETTINGS_CAT_RENDERING;

// Docked-region insets published to the render loop (see Gui.h). Updated each
// frame in renderGUI so the 3D viewport can be sized to the free area.
float g_dockLeftWidth = 0.0f;
float g_dockTopHeight = 0.0f;

// ===========================================================================
// Undo integration: per-panel edit gesture tracking
// ===========================================================================

// Records one undo entry per ImGui edit gesture (slider drag, color pick,
// checkbox click, text input) instead of one per frame. A manipulation panel
// calls update() after its widgets, passing the object state captured before
// the widgets ran this frame (preFrame) and the state now (current). When a
// gesture ends, record() is invoked once with the state from before the
// gesture began and the final state; the record functions themselves discard
// no-op edits, so spurious gestures (e.g. clicking a button) record nothing.
template <typename State> struct PanelEditTracker {
  bool editing = false;
  int objectIndex = -1;
  State preEdit{};

  template <typename RecordFn>
  void update(int index, const State &preFrame, const State &current,
              RecordFn record) {
    const bool activeNow = ImGui::IsAnyItemActive();
    if (!editing) {
      if (activeNow) {
        editing = true;
        objectIndex = index;
        preEdit = preFrame;
      }
    } else if (!activeNow) {
      if (objectIndex == index) {
        record(index, preEdit, current);
      }
      editing = false;
      objectIndex = -1;
    } else if (objectIndex != index) {
      // Selection changed mid-gesture - restart tracking on the new object.
      objectIndex = index;
      preEdit = preFrame;
    }
  }
};

static PanelEditTracker<Engine::Undo::ModelEditState> s_modelEditTracker;
static PanelEditTracker<Engine::Undo::PointCloudEditState> s_pointCloudEditTracker;
static PanelEditTracker<Engine::PointLight> s_pointLightEditTracker;
static PanelEditTracker<Engine::SpotLight> s_spotLightEditTracker;
static PanelEditTracker<Engine::Sun> s_sunEditTracker;

// Draw a FontAwesome glyph inline using the dedicated icon font (the icon font
// is always guaranteed to contain the glyph, unlike the merged regular font),
// then keep the cursor on the same line so a label can follow.
static void DrawInlineIcon(const char *icon, const ImVec4 &color) {
  ImGui::AlignTextToFramePadding();
  if (g_Fonts.icons) {
    ImGui::PushFont(g_Fonts.icons);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
  }
}

// Modern section header: a colored accent bar followed by a bright title and a
// thin separator. Used everywhere via the existing call sites.
static void DrawSectionHeader(const char *label) {
  float scale = g_GuiScale.currentScale;
  ImGui::Spacing();
  ImVec2 p = ImGui::GetCursorScreenPos();
  float fontSize = ImGui::GetFontSize();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImU32 accent = ImGui::GetColorU32(g_StyleColors.accent);
  dl->AddRectFilled(ImVec2(p.x, p.y + 2.0f * scale),
                    ImVec2(p.x + 3.5f * scale, p.y + fontSize),
                    accent, 2.0f * scale);
  ImGui::Indent(10.0f * scale);
  ImGui::TextUnformatted(label);
  ImGui::Unindent(10.0f * scale);
  ImGui::Spacing();
  ImGui::Separator();
}

// Title row for an object's properties panel: an accent-colored icon (drawn
// through the reliable dedicated icon font) followed by the object name, then a
// separator. Keeps every manipulation panel header visually consistent instead
// of relying on raw emoji glyphs that may be missing from the system fonts.
static void DrawPanelTitle(const char *icon, const std::string &title) {
  DrawInlineIcon(icon, g_StyleColors.accent);
  ImGui::TextUnformatted(title.c_str());
  ImGui::Separator();
}

// Vertical divider for the main menu bar. ImGui::Separator() in a menu bar
// draws a hard, full-height line in the heavy default separator color, which
// reads as a harsh cut across the whole bar. This draws a softer, rounded pill
// inset from the top and bottom edges and tinted from the text color (so it
// adapts to the active theme) at low opacity — gently grouping the menus rather
// than chopping the bar in two.
static void MenuBarSeparator() {
  float scale = g_GuiScale.currentScale;
  float barTop = ImGui::GetWindowPos().y;
  float barHeight = ImGui::GetFrameHeight();
  float thickness = 2.0f * scale;
  float padX = 5.0f * scale;   // horizontal breathing room either side
  float insetY = 6.0f * scale; // shrink in from the top and bottom edges

  ImVec2 cursor = ImGui::GetCursorScreenPos();
  float x = cursor.x + padX + thickness * 0.5f;
  float y0 = barTop + insetY;
  float y1 = barTop + barHeight - insetY;

  ImVec4 col = ImGui::GetStyleColorVec4(ImGuiCol_Text);
  col.w = 0.20f;
  ImGui::GetWindowDrawList()->AddRectFilled(
      ImVec2(x - thickness * 0.5f, y0), ImVec2(x + thickness * 0.5f, y1),
      ImGui::GetColorU32(col), thickness * 0.5f);

  // Reserve the horizontal slot so the following menu flows past the divider.
  ImGui::Dummy(ImVec2(padX * 2.0f + thickness, 0.0f));
}

// Vertical sidebar navigation entry (icon + label). Returns true when clicked.
// The icon is rendered through the dedicated icon font via the draw list so it
// is reliable regardless of whether the merged-font path succeeded.
static bool DrawNavItem(const char *icon, const char *label, bool selected) {
  float scale = g_GuiScale.currentScale;
  float fullWidth = ImGui::GetContentRegionAvail().x;
  float rowH = ImGui::GetFrameHeight() + 8.0f * scale;
  ImVec2 p0 = ImGui::GetCursorScreenPos();

  ImGui::PushID(label);
  ImGui::PushStyleColor(
      ImGuiCol_HeaderHovered,
      ImVec4(g_StyleColors.primary.x, g_StyleColors.primary.y,
             g_StyleColors.primary.z, 0.16f));
  // Pass selected=false so ImGui does not paint its own selection fill; we draw
  // a custom accent pill instead.
  bool clicked = ImGui::Selectable("##navitem", false,
                                   ImGuiSelectableFlags_None,
                                   ImVec2(fullWidth, rowH));
  ImGui::PopStyleColor();
  ImGui::PopID();

  ImDrawList *dl = ImGui::GetWindowDrawList();
  float fontSize = ImGui::GetFontSize();
  ImU32 textCol = ImGui::GetColorU32(selected ? ImGuiCol_Text
                                              : ImGuiCol_TextDisabled);

  if (selected) {
    ImU32 fill = ImGui::GetColorU32(
        ImVec4(g_StyleColors.primary.x, g_StyleColors.primary.y,
               g_StyleColors.primary.z, 0.16f));
    dl->AddRectFilled(p0, ImVec2(p0.x + fullWidth, p0.y + rowH), fill,
                      8.0f * scale);
    dl->AddRectFilled(ImVec2(p0.x, p0.y + rowH * 0.18f),
                      ImVec2(p0.x + 3.0f * scale, p0.y + rowH * 0.82f),
                      ImGui::GetColorU32(g_StyleColors.primary), 2.0f * scale);
    textCol = ImGui::GetColorU32(ImGuiCol_Text);
  }

  float pad = 14.0f * scale;
  float iconSlot = 24.0f * scale;
  ImVec2 iconPos(p0.x + pad, p0.y + (rowH - fontSize) * 0.5f);
  if (g_Fonts.icons)
    dl->AddText(g_Fonts.icons, fontSize, iconPos, textCol, icon);
  ImVec2 labelPos(p0.x + pad + iconSlot, p0.y + (rowH - fontSize) * 0.5f);
  dl->AddText(labelPos, textCol, label);

  return clicked;
}

// A modern on/off toggle switch. Returns true on the frame it changes.
static bool DrawToggleSwitch(const char *label, bool *v) {
  float scale = g_GuiScale.currentScale;
  ImGui::PushID(label);
  float height = ImGui::GetFrameHeight();
  float width = height * 1.85f;
  float radius = height * 0.5f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##toggle", ImVec2(width, height));
  bool changed = false;
  if (ImGui::IsItemClicked()) {
    *v = !*v;
    changed = true;
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();
  bool hovered = ImGui::IsItemHovered();
  ImU32 bg;
  if (*v)
    bg = ImGui::GetColorU32(hovered ? g_StyleColors.primaryHover
                                    : g_StyleColors.primary);
  else
    bg = ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered
                                    : ImGuiCol_FrameBg);
  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), bg, radius);
  float knob = radius - 2.5f * scale;
  float cx = *v ? (p.x + width - radius) : (p.x + radius);
  dl->AddCircleFilled(ImVec2(cx, p.y + radius), knob,
                      IM_COL32(255, 255, 255, 255));

  if (label && label[0] != '\0' &&
      !(label[0] == '#' && label[1] == '#')) {
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
  }
  ImGui::PopID();
  return changed;
}

// Helper function for help markers (tooltips)
static void DrawHelpMarker(const char *desc) {
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(desc);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

// Comprehensive icon test window
static void ShowIconTestWindow() {
  ImGui::Begin("Icon Test Window", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

  ImGui::Text("=== FontAwesome Icon Tests ===");

  // Test 1: Direct icon display with current font
  ImGui::Separator();
  ImGui::Text("Test 1: Current font");
  ImGui::Text("Lightbulb: %s", ICON_FA_LIGHTBULB);
  ImGui::Text("Star: %s", ICON_FA_STAR);
  ImGui::Text("Home: %s", ICON_FA_HOME);
  ImGui::Text("Cog: %s", ICON_FA_COG);
  ImGui::Text("Heart: %s", ICON_FA_HEART);

  // Test 2: With explicit regular font
  ImGui::Separator();
  ImGui::Text("Test 2: With regular font");
  if (g_Fonts.regular) {
    ImGui::PushFont(g_Fonts.regular);
    ImGui::Text("Lightbulb: %s", ICON_FA_LIGHTBULB);
    ImGui::Text("Star: %s", ICON_FA_STAR);
    ImGui::Text("Home: %s", ICON_FA_HOME);
    ImGui::Text("Cog: %s", ICON_FA_COG);
    ImGui::Text("Heart: %s", ICON_FA_HEART);
    ImGui::PopFont();
  } else {
    ImGui::Text("Regular font not available");
  }

  // Test 3: Raw UTF-8 sequences
  ImGui::Separator();
  ImGui::Text("Test 3: Raw UTF-8 sequences");
  ImGui::Text("Lightbulb raw: \uf0eb");
  ImGui::Text("Star raw: \uf005");
  ImGui::Text("Home raw: \uf015");

  // Test 4: Different character codes
  ImGui::Separator();
  ImGui::Text("Test 4: Character codes");
  char lightbulb_utf8[5];
  ImTextCharToUtf8(lightbulb_utf8, 0xf0eb);
  ImGui::Text("Lightbulb from code: %s", lightbulb_utf8);

  // Test 5: Font diagnostic
  ImGui::Separator();
  ImGui::Text("Test 5: Font Diagnostic");
  ImFont *currentFont = ImGui::GetFont();
  ImGui::Text("Current font: %p", currentFont);
  if (currentFont) {
    ImGui::Text("Font size: %.1f", currentFont->FontSize);
    ImGui::Text("Glyph count: %d", currentFont->Glyphs.Size);

    // Check specific glyphs
    const ImFontGlyph *lightbulb_glyph = currentFont->FindGlyph(0xf0eb);
    const ImFontGlyph *star_glyph = currentFont->FindGlyph(0xf005);
    const ImFontGlyph *home_glyph = currentFont->FindGlyph(0xf015);

    ImGui::Text("Lightbulb glyph: %s", lightbulb_glyph ? "FOUND" : "MISSING");
    ImGui::Text("Star glyph: %s", star_glyph ? "FOUND" : "MISSING");
    ImGui::Text("Home glyph: %s", home_glyph ? "FOUND" : "MISSING");

    if (lightbulb_glyph) {
      ImGui::Text("Lightbulb advance: %.2f", lightbulb_glyph->AdvanceX);
      ImGui::Text("Lightbulb visible: %s",
                  lightbulb_glyph->Visible ? "YES" : "NO");
    }
  }

  // Test 6: All available fonts
  ImGui::Separator();
  ImGui::Text("Test 6: All Fonts");
  ImGuiIO &io = ImGui::GetIO();
  ImGui::Text("Total fonts loaded: %d", io.Fonts->Fonts.Size);

  for (int i = 0; i < io.Fonts->Fonts.Size; i++) {
    ImFont *font = io.Fonts->Fonts[i];
    if (ImGui::TreeNode((void *)(intptr_t)i, "Font %d (%p) - Size: %.1f", i,
                        font, font->FontSize)) {
      ImGui::PushFont(font);

      // Test basic glyphs
      const ImFontGlyph *glyph_A = font->FindGlyph('A');
      const ImFontGlyph *glyph_lightbulb = font->FindGlyph(0xf0eb);

      ImGui::Text("Has 'A': %s", glyph_A ? "YES" : "NO");
      ImGui::Text("Has lightbulb: %s", glyph_lightbulb ? "YES" : "NO");
      ImGui::Text("Regular text: Hello World");
      ImGui::Text("Icon test: %s %s %s", ICON_FA_LIGHTBULB, ICON_FA_STAR,
                  ICON_FA_HOME);

      ImGui::PopFont();
      ImGui::TreePop();
    }
  }

  // Test 7: Manual button test
  ImGui::Separator();
  ImGui::Text("Test 7: Interactive Test");
  if (ImGui::Button(ICON_FA_LIGHTBULB " Click Me")) {
    // Test button with icon
  }

  // Test 8: Hex dump of icon strings
  ImGui::Separator();
  ImGui::Text("Test 8: Icon String Analysis");
  const char *lightbulb_str = ICON_FA_LIGHTBULB;
  ImGui::Text("ICON_FA_LIGHTBULB length: %d", strlen(lightbulb_str));
  ImGui::Text("Hex bytes:");
  for (int i = 0; i < strlen(lightbulb_str) && i < 8; i++) {
    ImGui::SameLine();
    ImGui::Text("%02X", (unsigned char)lightbulb_str[i]);
  }

  // Test 9: Try different lightbulb icons
  ImGui::Separator();
  ImGui::Text("Test 9: Alternative Icons");
  ImGui::Text("Try different codes:");

  // Test with manual UTF-8 encoding of 0xF0EB
  char manual_lightbulb[4] = {(char)0xEF, (char)0x83, (char)0xAB,
                              0x00}; // UTF-8 encoding of U+F0EB
  ImGui::Text("Manual UTF-8 lightbulb: %s", manual_lightbulb);

  // Test with other manual codes
  char manual_star[4] = {(char)0xEF, (char)0x80, (char)0x85,
                         0x00}; // UTF-8 encoding of U+F005
  ImGui::Text("Manual UTF-8 star: %s", manual_star);

  // Test with known working characters
  ImGui::Text("ASCII test: ABC123");
  ImGui::Text("Extended ASCII: ÄÖÜ");

  // Test 10: Force use Font 0 (which has the lightbulb)
  ImGui::Separator();
  ImGui::Text("Test 10: Force Font 0");
  io = ImGui::GetIO();
  if (io.Fonts->Fonts.Size > 0) {
    ImFont *font0 = io.Fonts->Fonts[0];
    ImGui::PushFont(font0);
    ImGui::Text("With Font 0: %s Lightbulb Test", ICON_FA_LIGHTBULB);
    ImGui::Text("With Font 0: %s Star Test", ICON_FA_STAR);
    ImGui::Text("With Font 0: %s Home Test", ICON_FA_HOME);
    ImGui::PopFont();
  }

  // Test 11: Manual font switching
  ImGui::Separator();
  ImGui::Text("Test 11: Try All Fonts");
  for (int i = 0; i < io.Fonts->Fonts.Size; i++) {
    ImFont *font = io.Fonts->Fonts[i];
    ImGui::PushFont(font);
    ImGui::Text("Font %d: %s", i, ICON_FA_LIGHTBULB);
    ImGui::PopFont();
  }

  ImGui::End();
}

bool InitializeGUI(GLFWwindow *window, bool isDarkTheme) {
  return InitializeImGuiWithFonts(window, isDarkTheme);
}

void CleanupGUI() {
  // Note: We intentionally skip ImGui cleanup to avoid crashes during static
  // destruction. ImGui has global/static objects that are destroyed after
  // main() returns, and calling shutdown functions can cause double-free or
  // use-after-free errors. The OS will clean up all memory when the process
  // exits anyway. See: https://github.com/ocornut/imgui/issues/586

  // ImGui_ImplOpenGL3_Shutdown();
  // ImGui_ImplGlfw_Shutdown();
  // ImGui::DestroyContext();
}

// ---------------------------------------------------------------------------
// Toast notifications
// ---------------------------------------------------------------------------

namespace {

struct ToastMessage {
  std::string text;
  GUI::ToastType type;
  double createdAt;
};

std::vector<ToastMessage> g_toasts;

constexpr double kToastLifetime = 4.0; // seconds on screen
constexpr double kToastFadeIn = 0.15;
constexpr double kToastFadeOut = 0.4;

} // namespace

void GUI::ShowToast(const std::string &message, GUI::ToastType type) {
  if (ImGui::GetCurrentContext() == nullptr)
    return;
  // Keep the stack short; the oldest message gives way to new ones
  if (g_toasts.size() >= 4)
    g_toasts.erase(g_toasts.begin());
  g_toasts.push_back({message, type, ImGui::GetTime()});
}

void GUI::UpdateWindowTitleForScene(const std::string &scenePath) {
  std::string title = "StereoVista";
  if (!scenePath.empty()) {
    std::string stem = std::filesystem::path(scenePath).stem().string();
    if (!stem.empty()) {
      title = stem + " - StereoVista";
    }
  }
  if (!isStereoWindow) {
    title += " (Monoviewer)";
  }
  if (Engine::Window::nativeWindow) {
    glfwSetWindowTitle(Engine::Window::nativeWindow, title.c_str());
  }
}

// Draws the active toasts stacked above the bottom edge, newest at the
// bottom. Called every frame from renderGUI (in both the visible and the
// hidden-GUI paths).
// Compact human-readable point count: "1.23B" / "350.4M" / "12.0K" / "742".
static std::string formatPointCount(uint64_t n) {
  char buf[32];
  if (n >= 1000000000ull)
    snprintf(buf, sizeof(buf), "%.2fB", static_cast<double>(n) / 1e9);
  else if (n >= 1000000ull)
    snprintf(buf, sizeof(buf), "%.1fM", static_cast<double>(n) / 1e6);
  else if (n >= 1000ull)
    snprintf(buf, sizeof(buf), "%.1fK", static_cast<double>(n) / 1e3);
  else
    snprintf(buf, sizeof(buf), "%llu", static_cast<unsigned long long>(n));
  return buf;
}

// Overlay (bottom-left) showing progressive LAS/LAZ load state + stats while any
// point cloud is still streaming: per-cloud phase, % loaded, points and rate.
// Disappears automatically once everything has finished loading.
static void renderPointCloudStreamingOverlay() {
  struct Item {
    std::string name;
    Engine::PointCloudLoader::StreamProgress p;
  };
  std::vector<Item> items;
  for (const auto &pc : currentScene.pointClouds) {
    if (!pc.isStreaming())
      continue;
    auto prog = Engine::PointCloudLoader::getStreamProgress(pc);
    if (prog.active)
      items.push_back({pc.name, prog});
  }
  if (items.empty())
    return;

  float scale = g_GuiScale.currentScale;
  SetNextWindowPosInMainWindow(ImVec2(16.0f * scale, windowHeight - 16.0f * scale),
                               ImGuiCond_Always, ImVec2(0.0f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.90f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(14.0f * scale, 10.0f * scale));
  ImGui::Begin("##pcloading", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing | ImGuiWindowFlags_NoNav);

  if (g_Fonts.icons) {
    ImGui::PushFont(g_Fonts.icons);
    ImGui::TextColored(g_StyleColors.info, "%s", ICON_FA_CLOUD);
    ImGui::PopFont();
    ImGui::SameLine();
  }
  ImGui::Text("Loading %zu point cloud%s", items.size(),
              items.size() == 1 ? "" : "s");
  ImGui::Separator();

  const float barW = 240.0f * scale;
  for (const auto &it : items) {
    const auto &p = it.p;
    ImGui::TextUnformatted(it.name.c_str());
    if (p.resorting) {
      // Phase 2: background Morton sort — show an animated sweep (no fraction).
      float anim =
          static_cast<float>((static_cast<long long>(ImGui::GetTime() * 1000.0) %
                              1000)) /
          1000.0f;
      ImGui::ProgressBar(anim, ImVec2(barW, 0.0f), "Optimizing (Morton sort)");
      ImGui::TextDisabled("%s pts - sorting for full render speed...",
                          formatPointCount(p.pointsTotal).c_str());
    } else {
      char ov[32];
      snprintf(ov, sizeof(ov), "%.0f%%", p.fraction * 100.0f);
      ImGui::ProgressBar(p.fraction, ImVec2(barW, 0.0f), ov);
      ImGui::TextDisabled("%s / %s pts - %s pts/s",
                          formatPointCount(p.pointsLoaded).c_str(),
                          formatPointCount(p.pointsTotal).c_str(),
                          formatPointCount(static_cast<uint64_t>(p.pointsPerSecond)).c_str());
    }
    ImGui::Spacing();
  }

  ImGui::End();
  ImGui::PopStyleVar(2);
}

static void renderToasts() {
  if (g_toasts.empty())
    return;

  double now = ImGui::GetTime();
  g_toasts.erase(std::remove_if(g_toasts.begin(), g_toasts.end(),
                                [now](const ToastMessage &t) {
                                  return now - t.createdAt > kToastLifetime;
                                }),
                 g_toasts.end());

  float scale = g_GuiScale.currentScale;
  float y = windowHeight - 24.0f * scale;

  for (int i = static_cast<int>(g_toasts.size()) - 1; i >= 0; --i) {
    const ToastMessage &toast = g_toasts[i];
    double age = now - toast.createdAt;

    // Fade in quickly, hold, then fade out
    float alpha = 1.0f;
    if (age < kToastFadeIn) {
      alpha = static_cast<float>(age / kToastFadeIn);
    } else if (age > kToastLifetime - kToastFadeOut) {
      alpha = static_cast<float>((kToastLifetime - age) / kToastFadeOut);
    }
    alpha = std::clamp(alpha, 0.0f, 1.0f);

    const char *icon = ICON_FA_INFO_CIRCLE;
    ImVec4 accent = g_StyleColors.info;
    switch (toast.type) {
    case GUI::ToastType::Success:
      icon = ICON_FA_CHECK_CIRCLE;
      accent = g_StyleColors.success;
      break;
    case GUI::ToastType::Warning:
      icon = ICON_FA_EXCLAMATION_TRIANGLE;
      accent = g_StyleColors.warning;
      break;
    case GUI::ToastType::Error:
      icon = ICON_FA_EXCLAMATION_CIRCLE;
      accent = g_StyleColors.danger;
      break;
    default:
      break;
    }

    SetNextWindowPosInMainWindow(ImVec2(windowWidth * 0.5f, y), ImGuiCond_Always,
                                 ImVec2(0.5f, 1.0f));
    ImGui::SetNextWindowBgAlpha(0.92f);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(14.0f * scale, 10.0f * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, alpha);

    char windowName[32];
    snprintf(windowName, sizeof(windowName), "##toast%d", i);
    ImGui::Begin(windowName, nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoSavedSettings |
                     ImGuiWindowFlags_NoFocusOnAppearing |
                     ImGuiWindowFlags_NoNav);

    if (g_Fonts.icons) {
      ImGui::PushFont(g_Fonts.icons);
      ImGui::TextColored(accent, "%s", icon);
      ImGui::PopFont();
      ImGui::SameLine();
    }
    ImGui::TextUnformatted(toast.text.c_str());

    y -= ImGui::GetWindowSize().y + 8.0f * scale;
    ImGui::End();
    ImGui::PopStyleVar(3);
  }
}

// ---------------------------------------------------------------------------
// Performance overlay: FPS, frame time, a frame-time graph and scene
// statistics in a translucent click-through widget (bottom-right corner).
// ---------------------------------------------------------------------------

// Formats large counts compactly ("950", "12.4K", "1.2M")
static void formatCount(char *buffer, size_t bufferSize, size_t count) {
  if (count >= 1000000) {
    snprintf(buffer, bufferSize, "%.1fM", count / 1000000.0);
  } else if (count >= 10000) {
    snprintf(buffer, bufferSize, "%.1fK", count / 1000.0);
  } else {
    snprintf(buffer, bufferSize, "%zu", count);
  }
}

static void renderPerformanceOverlay() {
  ImGuiIO &io = ImGui::GetIO();
  float scale = g_GuiScale.currentScale;

  // Rolling frame-time history (~2 s at 60 fps)
  static float frameTimes[120] = {};
  static int frameTimeOffset = 0;
  frameTimes[frameTimeOffset] = io.DeltaTime * 1000.0f;
  frameTimeOffset = (frameTimeOffset + 1) % IM_ARRAYSIZE(frameTimes);

  // GPU name (driver-owned string, valid for the lifetime of the GL context)
  static const char *gpuName = nullptr;
  if (gpuName == nullptr) {
    const GLubyte *renderer = glGetString(GL_RENDERER);
    gpuName =
        renderer ? reinterpret_cast<const char *>(renderer) : "Unknown GPU";
  }

  float fps = io.Framerate;
  float frameMs = fps > 0.0f ? 1000.0f / fps : 0.0f;

  SetNextWindowPosInMainWindow(
      ImVec2(windowWidth - 12.0f * scale, windowHeight - 12.0f * scale),
      ImGuiCond_Always, ImVec2(1.0f, 1.0f));
  ImGui::SetNextWindowBgAlpha(0.6f);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 8.0f * scale);
  ImGui::Begin("PerformanceOverlay", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing |
                   ImGuiWindowFlags_NoNav);

  // FPS readout colored by performance budget (green >= 59, yellow >= 29)
  ImVec4 fpsColor = fps >= 59.0f   ? g_StyleColors.success
                    : fps >= 29.0f ? g_StyleColors.warning
                                   : g_StyleColors.danger;
  if (g_Fonts.header)
    ImGui::PushFont(g_Fonts.header);
  ImGui::TextColored(fpsColor, "%.0f", fps);
  if (g_Fonts.header)
    ImGui::PopFont();
  ImGui::SameLine();
  ImGui::Text("FPS");
  ImGui::SameLine();
  ImGui::TextDisabled("%.2f ms", frameMs);

  // Frame-time graph. The scale floors at the 30 fps budget so a steady
  // 60 fps draws as a calm low line and spikes stand out.
  float graphMax = 33.3f;
  for (float t : frameTimes) {
    if (t > graphMax)
      graphMax = t;
  }
  ImGui::PushStyleColor(ImGuiCol_FrameBg, ImVec4(0.0f, 0.0f, 0.0f, 0.25f));
  ImGui::PlotLines("##frametimes", frameTimes, IM_ARRAYSIZE(frameTimes),
                   frameTimeOffset, nullptr, 0.0f, graphMax,
                   ImVec2(180.0f * scale, 40.0f * scale));
  ImGui::PopStyleColor();

  // Scene statistics
  size_t triangles = 0;
  for (const auto &model : currentScene.models) {
    for (const auto &mesh : model.getMeshes()) {
      triangles += mesh.indices.size() / 3;
    }
  }
  size_t points = 0;
  for (const auto &pc : currentScene.pointClouds) {
    points += pc.totalPointCount;
  }

  if (g_Fonts.smallFont)
    ImGui::PushFont(g_Fonts.smallFont);
  char countBuffer[32];
  if (triangles > 0 || !currentScene.models.empty()) {
    formatCount(countBuffer, sizeof(countBuffer), triangles);
    ImGui::TextDisabled("%zu models | %s tris", currentScene.models.size(),
                        countBuffer);
  }
  if (points > 0 || !currentScene.pointClouds.empty()) {
    formatCount(countBuffer, sizeof(countBuffer), points);
    ImGui::TextDisabled("%zu clouds | %s pts",
                        currentScene.pointClouds.size(), countBuffer);
  }
  ImGui::TextDisabled("%s", gpuName);
  if (g_Fonts.smallFont)
    ImGui::PopFont();

  ImGui::End();
  ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// Empty-scene hint: a subtle centered prompt shown while nothing is loaded
// ---------------------------------------------------------------------------
static void renderEmptySceneHint() {
  if (!currentScene.models.empty() || !currentScene.pointClouds.empty())
    return;

  // Center over the 3D viewport (falls back to the window center before the
  // viewport rectangle is first published)
  float centerX = windowWidth * 0.5f;
  float centerY = windowHeight * 0.5f;
  if (g_viewportWidth > 0 && g_viewportHeight > 0) {
    centerX = g_viewportX + g_viewportWidth * 0.5f;
    centerY = g_viewportTopInset + g_viewportHeight * 0.5f;
  }

  auto centeredText = [](const char *text) {
    float textWidth = ImGui::CalcTextSize(text).x;
    float indent = (ImGui::GetWindowSize().x -
                    ImGui::GetStyle().WindowPadding.x * 2.0f - textWidth) *
                   0.5f;
    if (indent > 0.0f) {
      ImGui::SetCursorPosX(ImGui::GetStyle().WindowPadding.x + indent);
    }
    ImGui::TextDisabled("%s", text);
  };

  SetNextWindowPosInMainWindow(ImVec2(centerX, centerY), ImGuiCond_Always,
                               ImVec2(0.5f, 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_Alpha, 0.75f);
  ImGui::Begin("EmptySceneHint", nullptr,
               ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                   ImGuiWindowFlags_AlwaysAutoResize |
                   ImGuiWindowFlags_NoSavedSettings |
                   ImGuiWindowFlags_NoFocusOnAppearing |
                   ImGuiWindowFlags_NoNav | ImGuiWindowFlags_NoBackground);

  if (g_Fonts.icons) {
    ImGui::PushFont(g_Fonts.icons);
    centeredText(ICON_FA_CUBE);
    ImGui::PopFont();
    ImGui::Spacing();
  }
  if (g_Fonts.header)
    ImGui::PushFont(g_Fonts.header);
  centeredText("Scene is empty");
  if (g_Fonts.header)
    ImGui::PopFont();
  ImGui::Spacing();
  centeredText("Drag & drop 3D models, point clouds or scene files here");
  centeredText("or use File > Import");

  ImGui::End();
  ImGui::PopStyleVar();
}

// ---------------------------------------------------------------------------
// File import helpers shared by the File menu and window drag-and-drop
// ---------------------------------------------------------------------------

// Imports a single 3D model file into the scene and selects it.
static void importModelFile(const std::string &filePath) {
  try {
    Engine::Model newModel = *Engine::loadModel(filePath);
    glm::vec3 targetScale = newModel.scale;
    if (preferences.enableSpawnAnimation) {
      newModel.startSpawnAnimation(targetScale, 1.1f);
    }
    currentScene.models.push_back(newModel);
    Engine::Undo::recordModelAdded(
        static_cast<int>(currentScene.models.size()) - 1);
    currentSelectedIndex = currentScene.models.size() - 1;
    currentSelectedType = SelectedType::Model;
    updateSpaceMouseBounds();

    // Mark voxelizer dirty for re-voxelization
    if (voxelizer) {
      voxelizer->markDirty();
    }

    GUI::ShowToast("Model imported: " +
                       std::filesystem::path(filePath).filename().string(),
                   GUI::ToastType::Success);
  } catch (const std::exception &e) {
    std::cerr << "Failed to load model: " << e.what() << std::endl;
    GUI::ShowToast(std::string("Failed to import model: ") + e.what(),
                   GUI::ToastType::Error);
  }
}

// Imports a single point cloud file of any supported format except LAS/LAZ
// (those are loaded in batches via importLASFiles so tiles share a global
// center).
static void importPointCloudFile(const std::string &filePath) {
  std::string extension = std::filesystem::path(filePath).extension().string();
  std::transform(extension.begin(), extension.end(), extension.begin(),
                 ::tolower);

  if (extension == ".txt" || extension == ".xyz" || extension == ".ply") {
    Engine::PointCloud newPointCloud =
        std::move(Engine::PointCloudLoader::loadPointCloudFile(filePath));
    if (newPointCloud.isLoaded()) {
      newPointCloud.filePath = filePath;
      currentScene.pointClouds.emplace_back(std::move(newPointCloud));
      Engine::Undo::recordPointCloudAdded(
          static_cast<int>(currentScene.pointClouds.size()) - 1);
      updateSpaceMouseBounds();
    } else {
      GUI::ShowToast("Failed to load point cloud (no points / unsupported "
                     "format - binary PLY is not supported): " +
                         std::filesystem::path(filePath).filename().string(),
                     GUI::ToastType::Error);
    }
  } else if (extension == ".pcb") {
    Engine::PointCloud newPointCloud =
        std::move(Engine::PointCloudLoader::loadFromBinary(filePath));
    if (newPointCloud.isLoaded()) {
      newPointCloud.filePath = filePath;
      newPointCloud.name = std::filesystem::path(filePath).stem().string();
      currentScene.pointClouds.emplace_back(std::move(newPointCloud));
      Engine::Undo::recordPointCloudAdded(
          static_cast<int>(currentScene.pointClouds.size()) - 1);
      updateSpaceMouseBounds();
    }
  } else if (extension == ".h5" || extension == ".hdf5" ||
             extension == ".f5") {
    Engine::PointCloud newPointCloud =
        std::move(Engine::PointCloudLoader::loadPointCloudFile(filePath));
    if (newPointCloud.isLoaded()) {
      newPointCloud.filePath = filePath;
      newPointCloud.name = std::filesystem::path(filePath).stem().string();
      currentScene.pointClouds.emplace_back(std::move(newPointCloud));
      Engine::Undo::recordPointCloudAdded(
          static_cast<int>(currentScene.pointClouds.size()) - 1);
      updateSpaceMouseBounds();
    }
  }
}

// Imports LAS/LAZ tiles together so they share a global center and remain
// correctly positioned relative to each other.
static void importLASFiles(const std::vector<std::string> &lasFiles) {
  if (lasFiles.empty())
    return;
  // Progressive load: returns immediately with cloud stubs whose SSBOs are
  // pre-sized from the LAS header and whose bounds are already valid.  The
  // points stream in on background threads and are uploaded each frame by
  // PointCloudLoader::updateStreaming() in the main loop, so the cloud appears
  // almost instantly and fills in while remaining tiles keep loading.
  auto clouds = Engine::PointCloudLoader::beginLoadLASMultipleProgressive(
      lasFiles, 1, preferences.pointCloudMortonResort);
  for (auto &pc : clouds) {
    currentScene.pointClouds.emplace_back(std::move(pc));
    Engine::Undo::recordPointCloudAdded(
        static_cast<int>(currentScene.pointClouds.size()) - 1);
  }
  // Bounds come from the LAS headers, so SpaceMouse extents are correct
  // immediately even though points are still streaming.
  updateSpaceMouseBounds();
}

// ===========================================================================
// Scene file operations
//
// Shared by the File menu, the keyboard shortcuts and the Scene Manager window
// so save / load / merge / new behave identically wherever they are invoked.
// They operate on the live application globals (currentScene, lights, camera).
// ===========================================================================

// Live "current scene" bookkeeping owned by main.cpp.
extern std::string g_currentScenePath;
extern bool g_sceneDirty;
extern bool showSceneManagerWindow;
// Apply / capture the per-scene environment (skybox + lighting mode + sun).
void applyLoadedSceneEnvironment(const Engine::Scene &scene);
void captureSceneEnvironment(Engine::Scene &scene);

// Insert `path` at the head of the recent-scenes list (newest first), removing
// any earlier occurrence and capping the list. Persists preferences.
static void SceneAddRecent(const std::string &path) {
  if (path.empty())
    return;
  std::string normalized =
      std::filesystem::path(path).lexically_normal().string();
  auto &recents = preferences.recentScenes;
  recents.erase(std::remove(recents.begin(), recents.end(), normalized),
                recents.end());
  recents.insert(recents.begin(), normalized);
  constexpr size_t kMaxRecent = 12;
  if (recents.size() > kMaxRecent)
    recents.resize(kMaxRecent);
  savePreferences();
}

// Build the engine save options from the user's preferences.
static Engine::SceneSaveOptions SceneOptionsFromPrefs() {
  Engine::SceneSaveOptions opt;
  const auto &s = preferences.sceneSaveSettings;
  opt.includeCamera = s.includeCamera;
  opt.includeLighting = s.includeLighting;
  opt.includeEnvironment = s.includeEnvironment;
  opt.includeMeasurements = s.includeMeasurements;
  opt.includeClipPlanes = s.includeClipPlanes;
  opt.compact = s.compact;
  return opt;
}

// Surface a load report as a toast: success with a count, plus a warning toast
// when assets were missing / objects were dropped.
static void SceneReportToast(const std::string &sceneFile,
                             const Engine::SceneLoadReport &report,
                             const char *verb) {
  std::string name = std::filesystem::path(sceneFile).filename().string();
  GUI::ShowToast(std::string("Scene ") + verb + ": " + name,
                 GUI::ToastType::Success);
  int failed = report.modelsFailed + report.pointCloudsFailed;
  if (failed > 0) {
    GUI::ShowToast(std::to_string(failed) +
                       " object(s) could not be loaded - see log for details",
                   GUI::ToastType::Warning);
  } else if (report.hasWarnings()) {
    GUI::ShowToast("Scene loaded with warnings - see log for details",
                   GUI::ToastType::Warning);
  }
}

// Save the live scene to `destination`. Returns true on success. Updates the
// current-scene path, clears the dirty flag and refreshes the recent list.
static bool SceneSaveTo(const std::string &destination) {
  try {
    currentScene.pointLights = pointLights;
    currentScene.spotLights = spotLights;
    captureSceneEnvironment(currentScene);
    Engine::saveScene(destination, currentScene, camera,
                      SceneOptionsFromPrefs());

    std::filesystem::path p(destination);
    if (p.extension() != ".scene")
      p.replace_extension(".scene");

    if (preferences.sceneSaveSettings.includeSnapshots) {
      Core::SnapshotManager::instance().saveToScene(p.string());
    } else {
      // Snapshot saving is off: drop any snapshot index left in this scene's
      // folder so a later load doesn't restore stale snapshots.
      std::error_code ec;
      std::filesystem::remove(p.parent_path() / p.stem() / "snapshots.json", ec);
    }

    g_currentScenePath = p.string();
    g_sceneDirty = false;
    // Reflect the timestamps the file was actually written with so the Scene
    // Manager panel shows them without needing a reload. Guard on a non-empty
    // modifiedAt so a chunked scene (whose header carries no metadata) doesn't
    // wipe the description/author the user just entered.
    Engine::SceneInfo info = Engine::loadSceneInfo(g_currentScenePath);
    if (info.valid && !info.metadata.modifiedAt.empty())
      currentScene.metadata = info.metadata;
    SceneAddRecent(g_currentScenePath);
    GUI::UpdateWindowTitleForScene(g_currentScenePath);
    GUI::ShowToast("Scene saved: " + p.filename().string(),
                   GUI::ToastType::Success);
    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to save scene: " << e.what() << std::endl;
    GUI::ShowToast(std::string("Failed to save scene: ") + e.what(),
                   GUI::ToastType::Error);
    return false;
  }
}

// Prompt for a destination and save (Save As...). Returns true if saved.
static bool SceneSaveAsDialog() {
  std::string suggested =
      g_currentScenePath.empty()
          ? std::string("scene.scene")
          : std::filesystem::path(g_currentScenePath).filename().string();
  auto destination = pfd::save_file("Save scene as", suggested,
                                    {"Scene Files", "*.scene", "All Files", "*"})
                         .result();
  if (destination.empty())
    return false;
  return SceneSaveTo(destination);
}

// Quick save (Ctrl+S): re-save to the current path, or fall back to Save As
// when the scene has never been saved.
static bool SceneQuickSave() {
  if (g_currentScenePath.empty())
    return SceneSaveAsDialog();
  return SceneSaveTo(g_currentScenePath);
}

// Replace the live scene with the contents of `sceneFile`.
static void SceneReplaceFrom(const std::string &sceneFile) {
  try {
    // Recorded undo entries reference objects by index, which no longer match
    // once a scene file replaces the containers.
    Engine::UndoManager::instance().clear();

    currentScene.models.clear();
    currentScene.pointClouds.clear();
    pointLights.clear();
    spotLights.clear();
    currentSelectedType = SelectedType::None;
    currentSelectedIndex = -1;
    currentSelectedMeshIndex = -1;

    Engine::SceneLoadReport report;
    currentScene = Engine::loadScene(sceneFile, camera, &report);
    pointLights = currentScene.pointLights;
    for (auto &pl : pointLights)
      pl.castShadows = true;
    spotLights = currentScene.spotLights;

    Core::SnapshotManager::instance().loadFromScene(sceneFile);
    applyLoadedSceneEnvironment(currentScene);

    for (auto &model : currentScene.models) {
      glm::vec3 targetScale = model.scale;
      if (preferences.enableSpawnAnimation)
        model.startSpawnAnimation(targetScale, 1.1f);
    }
    currentSelectedIndex = currentScene.models.empty() ? -1 : 0;
    updateSpaceMouseBounds();
    if (voxelizer)
      voxelizer->markDirty();

    g_currentScenePath = sceneFile;
    g_sceneDirty = false;
    SceneAddRecent(sceneFile);
    GUI::UpdateWindowTitleForScene(sceneFile);
    SceneReportToast(sceneFile, report, "loaded");
  } catch (const std::exception &e) {
    std::cerr << "Failed to load scene: " << e.what() << std::endl;
    GUI::ShowToast(std::string("Failed to load scene: ") + e.what(),
                   GUI::ToastType::Error);
  }
}

// Merge the contents of `sceneFile` into the live scene (keep existing).
static void SceneMergeFrom(const std::string &sceneFile) {
  try {
    // Bulk merges are not tracked, so drop the index-based undo history.
    Engine::UndoManager::instance().clear();

    Engine::SceneLoadReport report;
    Engine::Scene newScene = Engine::loadScene(sceneFile, camera, &report);

    for (auto &model : newScene.models) {
      glm::vec3 targetScale = model.scale;
      if (preferences.enableSpawnAnimation)
        model.startSpawnAnimation(targetScale, 1.1f);
      currentScene.models.push_back(model);
    }
    for (auto &pc : newScene.pointClouds)
      currentScene.pointClouds.push_back(std::move(pc));
    for (auto &pl : newScene.pointLights) {
      pl.castShadows = true;
      pointLights.push_back(pl);
    }
    for (auto &sl : newScene.spotLights)
      spotLights.push_back(sl);

    updateSpaceMouseBounds();
    if (voxelizer)
      voxelizer->markDirty();

    g_sceneDirty = true; // merged content is unsaved
    SceneAddRecent(sceneFile);
    SceneReportToast(sceneFile, report, "merged");
  } catch (const std::exception &e) {
    std::cerr << "Failed to merge scene: " << e.what() << std::endl;
    GUI::ShowToast(std::string("Failed to merge scene: ") + e.what(),
                   GUI::ToastType::Error);
  }
}

// Clear the live scene back to an empty, untitled state.
static void SceneNew() {
  Engine::UndoManager::instance().clear();
  currentScene.models.clear();
  currentScene.pointClouds.clear();
  currentScene.measurements.clear();
  currentScene.clipPlanes.clear();
  pointLights.clear();
  spotLights.clear();
  currentScene.metadata = Engine::SceneMetadata{};
  currentScene.environment = Engine::SceneEnvironment{};
  currentSelectedType = SelectedType::None;
  currentSelectedIndex = -1;
  currentSelectedMeshIndex = -1;
  Core::SnapshotManager::instance().clear();
  updateSpaceMouseBounds();
  if (voxelizer)
    voxelizer->markDirty();
  g_currentScenePath.clear();
  g_sceneDirty = false;
  GUI::UpdateWindowTitleForScene("");
  GUI::ShowToast("New (empty) scene", GUI::ToastType::Info);
}

// Shared modal state. The modals themselves are drawn by renderGUI each frame;
// these flags can be raised from anywhere (menu, shortcuts, Scene Manager).
static std::string g_pendingSceneToLoad;
static bool g_showLoadSceneDialog = false;
static bool g_showNewSceneGuard = false;

// Route a scene file through the user's replace / merge / ask preference.
static void SceneRequestLoad(const std::string &sceneFile) {
  bool hasExistingObjects = !currentScene.models.empty() ||
                            !currentScene.pointClouds.empty() ||
                            !pointLights.empty() || !spotLights.empty();
  if (!hasExistingObjects ||
      preferences.sceneLoadingBehavior == GUI::SCENE_LOAD_ALWAYS_REPLACE) {
    SceneReplaceFrom(sceneFile);
  } else if (preferences.sceneLoadingBehavior == GUI::SCENE_LOAD_ALWAYS_MERGE) {
    SceneMergeFrom(sceneFile);
  } else {
    g_pendingSceneToLoad = sceneFile;
    g_showLoadSceneDialog = true;
  }
}

// New Scene, guarded against silently discarding unsaved changes.
static void SceneRequestNew() {
  if (g_sceneDirty)
    g_showNewSceneGuard = true;
  else
    SceneNew();
}

// Open a file dialog and load the chosen scene through the preference router.
static void SceneOpenDialog() {
  auto selection =
      pfd::open_file("Select a scene file to load", ".",
                     {"Scene Files", "*.scene", "All Files", "*"})
          .result();
  if (!selection.empty())
    SceneRequestLoad(selection[0]);
}

// Case-insensitive substring test used by the Scene Hierarchy search filter.
// An empty needle matches everything (so the unfiltered list shows in full).
static bool searchMatches(const std::string &haystack, const char *needle) {
  if (needle == nullptr || needle[0] == '\0')
    return true;
  std::string h = haystack;
  std::string n = needle;
  std::transform(h.begin(), h.end(), h.begin(), ::tolower);
  std::transform(n.begin(), n.end(), n.begin(), ::tolower);
  return h.find(n) != std::string::npos;
}

void renderGUI(bool isLeftEye, ImGuiViewportP *viewport,
               ImGuiWindowFlags windowFlags, Engine::Shader *shader) {
  if (!isLeftEye) {
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return;
  }

  // Check if GUI rescaling is needed
  RescaleImGuiFonts(Engine::Window::nativeWindow, isDarkTheme);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  if (!showGui) {
    // Files dropped while the GUI is hidden: bring the GUI back so the
    // import (and any scene-load dialog) runs on the next frame.
    if (!g_droppedFiles.empty()) {
      showGui = true;
    }

    // GUI hidden: the viewport fills the whole window.
    g_dockLeftWidth = 0.0f;
    g_dockTopHeight = 0.0f;
    renderEmptySceneHint();
    if (showFPS) {
      renderPerformanceOverlay();
    }
    renderPointCloudStreamingOverlay();
    renderToasts();
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return;
  }

  // ========================

  // MAIN MENU BAR
  // ========================


  // Thin wrappers kept for the existing call sites below; they delegate to the
  // shared scene-file operations so every entry point behaves identically.
  auto loadAndReplaceScene = [&](const std::string &sceneFile) {
    SceneReplaceFrom(sceneFile);
  };
  auto loadAndMergeScene = [&](const std::string &sceneFile) {
    SceneMergeFrom(sceneFile);
  };

  // Global keyboard shortcuts for scene file operations. Ignored while a text
  // field has keyboard focus so they don't fire while the user is typing.
  {
    ImGuiIO &io = ImGui::GetIO();
    if (io.KeyCtrl && !io.WantTextInput) {
      if (ImGui::IsKeyPressed(ImGuiKey_S, false)) {
        if (io.KeyShift)
          SceneSaveAsDialog();
        else
          SceneQuickSave();
      } else if (ImGui::IsKeyPressed(ImGuiKey_N, false)) {
        SceneRequestNew();
      } else if (ImGui::IsKeyPressed(ImGuiKey_O, false)) {
        SceneOpenDialog();
      }
    }
  }

  // Import any files dropped onto the window (queued by the GLFW drop
  // callback). Scene files follow the same replace/merge/ask flow as
  // File > Load Scene; models and point clouds import directly.
  if (!g_droppedFiles.empty()) {
    std::vector<std::string> droppedFiles;
    droppedFiles.swap(g_droppedFiles);

    std::vector<std::string> lasFiles;
    size_t cloudCountBefore = currentScene.pointClouds.size();

    for (const auto &filePath : droppedFiles) {
      std::string ext = std::filesystem::path(filePath).extension().string();
      std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);

      if (ext == ".scene") {
        SceneRequestLoad(filePath);
      } else if (ext == ".obj" || ext == ".fbx" || ext == ".3ds" ||
                 ext == ".gltf" || ext == ".glb") {
        importModelFile(filePath);
      } else if (ext == ".las" || ext == ".laz") {
        lasFiles.push_back(filePath);
      } else if (ext == ".txt" || ext == ".xyz" || ext == ".ply" ||
                 ext == ".pcb" || ext == ".h5" || ext == ".hdf5" ||
                 ext == ".f5") {
        importPointCloudFile(filePath);
      } else {
        GUI::ShowToast("Unsupported file type: " +
                           std::filesystem::path(filePath).filename().string(),
                       GUI::ToastType::Warning);
      }
    }

    importLASFiles(lasFiles);

    size_t cloudsImported =
        currentScene.pointClouds.size() - cloudCountBefore;
    if (cloudsImported > 0) {
      GUI::ShowToast("Imported " + std::to_string(cloudsImported) +
                         (cloudsImported == 1 ? " point cloud"
                                              : " point clouds"),
                     GUI::ToastType::Success);
    }
  }

  if (ImGui::BeginMainMenuBar()) {
    // File Menu
    if (ImGui::BeginMenu("File")) {
      if (ImGui::BeginMenu("Import")) {
        // Add icon to 3D Model import
        std::string modelMenuText = "3D Model...";
        if (g_Fonts.icons) {
          ImGui::PushFont(g_Fonts.icons);
          ImGui::Text(ICON_FA_CUBE);
          ImGui::PopFont();
          ImGui::SameLine();
        }
        if (ImGui::MenuItem(modelMenuText.c_str())) {
          auto selection =
              pfd::open_file("Select a 3D model to import", ".",
                             {"3D Models", "*.obj *.fbx *.3ds *.gltf *.glb",
                              "All Files", "*"})
                  .result();

          if (!selection.empty()) {
            importModelFile(selection[0]);
          }
        }

        // Add icon to Point Cloud import
        std::string cloudMenuText = "Point Cloud...";
        if (g_Fonts.icons) {
          ImGui::PushFont(g_Fonts.icons);
          ImGui::Text(ICON_FA_CLOUD);
          ImGui::PopFont();
          ImGui::SameLine();
        }
        if (ImGui::MenuItem(cloudMenuText.c_str())) {
          auto selection =
              pfd::open_file(
                  "Select point cloud file(s) to import", ".",
                  {"Point Cloud Files",
                   "*.txt *.xyz *.ply *.pcb *.h5 *.hdf5 *.f5 *.las *.laz",
                   "All Files", "*"},
                  pfd::opt::multiselect)
                  .result();

          if (!selection.empty()) {
            size_t cloudCountBefore = currentScene.pointClouds.size();

            // Separate LAS/LAZ tiles from other formats.  Multiple LAS/LAZ
            // files are loaded together so they share a global centre and
            // remain correctly positioned relative to each other.
            std::vector<std::string> lasFiles, otherFiles;
            for (const auto& path : selection) {
              std::string ext = std::filesystem::path(path).extension().string();
              std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
              if (ext == ".las" || ext == ".laz") lasFiles.push_back(path);
              else otherFiles.push_back(path);
            }

            importLASFiles(lasFiles);

            for (const auto& filePath : otherFiles) {
              importPointCloudFile(filePath);
            }

            size_t cloudsImported =
                currentScene.pointClouds.size() - cloudCountBefore;
            if (cloudsImported > 0) {
              GUI::ShowToast("Imported " + std::to_string(cloudsImported) +
                                 (cloudsImported == 1 ? " point cloud"
                                                      : " point clouds"),
                             GUI::ToastType::Success);
            } else {
              GUI::ShowToast("No point clouds could be imported",
                             GUI::ToastType::Warning);
            }
          }
        }
        ImGui::EndMenu();
      }

      ImGui::Separator();

      // New Scene (guarded against discarding unsaved changes).
      if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
        SceneRequestNew();
      }

      // Load Scene
      std::string sceneMenuText = "Load Scene...";
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_FOLDER_OPEN);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      if (ImGui::MenuItem(sceneMenuText.c_str(), "Ctrl+O")) {
        SceneOpenDialog();
      }

      // Recent Scenes submenu (most-recently-used first).
      if (ImGui::BeginMenu("Recent Scenes",
                           !preferences.recentScenes.empty())) {
        // Copy so we can mutate the list (remove missing entries) while
        // iterating safely.
        std::vector<std::string> recents = preferences.recentScenes;
        for (const auto &recent : recents) {
          bool exists = std::filesystem::exists(recent);
          std::string label = std::filesystem::path(recent).filename().string();
          if (!exists)
            label += "  (missing)";
          if (!exists)
            ImGui::BeginDisabled();
          if (ImGui::MenuItem(label.c_str())) {
            SceneRequestLoad(recent);
          }
          if (!exists)
            ImGui::EndDisabled();
          if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", recent.c_str());
        }
        ImGui::Separator();
        if (ImGui::MenuItem("Clear Recent")) {
          preferences.recentScenes.clear();
          savePreferences();
        }
        ImGui::EndMenu();
      }

      ImGui::Separator();

      // Save / Save As. Save re-writes the current file (or prompts when the
      // scene has never been saved); Save As always prompts.
      if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
        SceneQuickSave();
      }
      if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
        SceneSaveAsDialog();
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Save Screenshot...")) {
        // Capture happens on the render thread next frame; here we just pick
        // the destination path and arm the request.
        std::string defaultName =
            Engine::Screenshot::makeTimestampedPath("screenshots");
        auto destination =
            pfd::save_file("Save screenshot",
                           std::filesystem::path(defaultName).string(),
                           {"PNG Image", "*.png", "All Files", "*"})
                .result();
        if (!destination.empty()) {
          // Ensure a .png extension so the encoded image matches the file name.
          if (std::filesystem::path(destination).extension() != ".png")
            destination += ".png";
          g_screenshotPath = destination;
          g_requestScreenshot = true;
        }
      }

      if (ImGui::MenuItem("Quick Screenshot", "F12")) {
        // Auto-save to the "screenshots" folder with a timestamped name.
        g_screenshotPath.clear();
        g_requestScreenshot = true;
      }

      ImGui::MenuItem("Include UI in Screenshot", nullptr,
                      &preferences.screenshotIncludeUI);

      // Stereo-3D screenshot layout. On a quad-buffer stereo window the
      // screenshot can capture both eyes and write a Full Side-by-Side,
      // Above/Below, or two separate (_L/_R) images. "Single (Left Eye)" keeps
      // the legacy single-eye capture. Stereo modes always capture the clean 3D
      // viewer (the "Include UI" option is ignored for them).
      if (ImGui::BeginMenu("Stereo-3D Screenshot")) {
        if (ImGui::MenuItem("Single (Left Eye)", nullptr,
                            preferences.stereoScreenshotMode ==
                                GUI::STEREO_SHOT_MONO)) {
          preferences.stereoScreenshotMode = GUI::STEREO_SHOT_MONO;
        }
        if (ImGui::MenuItem("Full Side-by-Side (SBS)", nullptr,
                            preferences.stereoScreenshotMode ==
                                GUI::STEREO_SHOT_FULL_SBS)) {
          preferences.stereoScreenshotMode = GUI::STEREO_SHOT_FULL_SBS;
        }
        if (ImGui::MenuItem("Above/Below", nullptr,
                            preferences.stereoScreenshotMode ==
                                GUI::STEREO_SHOT_ABOVE_BELOW)) {
          preferences.stereoScreenshotMode = GUI::STEREO_SHOT_ABOVE_BELOW;
        }
        if (ImGui::MenuItem("Two Separate Images (_L / _R)", nullptr,
                            preferences.stereoScreenshotMode ==
                                GUI::STEREO_SHOT_SEPARATE)) {
          preferences.stereoScreenshotMode = GUI::STEREO_SHOT_SEPARATE;
        }
        ImGui::Separator();
        ImGui::TextDisabled("Stereo modes require a stereo (quad-buffer)\n"
                            "window and capture both eyes without UI.");
        ImGui::EndMenu();
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Exit", "Esc")) {
        glfwSetWindowShouldClose(Engine::Window::nativeWindow, true);
      }

      ImGui::EndMenu();
    }

    // Load Scene Dialog Modal (must be outside menu scope for popup to work
    // correctly)
    if (g_showLoadSceneDialog) {
      ImGui::OpenPopup("Load Scene");
      g_showLoadSceneDialog = false;
    }

    // Center the popup in the viewport (both horizontally and vertically)
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Load Scene", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove)) {
      ImGui::Text("An existing scene is already loaded.");
      ImGui::Spacing();
      ImGui::Text("How would you like to load the new scene?");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Replace button - clear existing scene
      if (ImGui::Button("Replace (Clear Existing)", ImVec2(200, 0))) {
        loadAndReplaceScene(g_pendingSceneToLoad);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      DrawHelpMarker("Remove all existing objects and load the new scene");

      ImGui::Spacing();

      // Merge button - keep existing scene
      if (ImGui::Button("Merge (Keep Existing)", ImVec2(200, 0))) {
        loadAndMergeScene(g_pendingSceneToLoad);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      DrawHelpMarker("Add new scene objects to the existing scene");

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Cancel button
      if (ImGui::Button("Cancel", ImVec2(-1, 0))) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Hint about settings
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
      ImGui::TextWrapped("Tip: You can set a default behavior in Settings > "
                         "Display > Scene Loading");
      ImGui::PopStyleColor();

      ImGui::EndPopup();
    }

    // New Scene unsaved-changes guard.
    if (g_showNewSceneGuard) {
      ImGui::OpenPopup("Unsaved Changes");
      g_showNewSceneGuard = false;
    }
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    if (ImGui::BeginPopupModal("Unsaved Changes", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove)) {
      ImGui::Text("The current scene has unsaved changes.");
      ImGui::Spacing();
      ImGui::Text("Save before creating a new scene?");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      if (ImGui::Button("Save", ImVec2(120, 0))) {
        if (SceneQuickSave()) {
          SceneNew();
          ImGui::CloseCurrentPopup();
        }
        // If the save was cancelled / failed, keep the dialog open.
      }
      ImGui::SameLine();
      if (ImGui::Button("Don't Save", ImVec2(120, 0))) {
        SceneNew();
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      if (ImGui::Button("Cancel", ImVec2(120, 0))) {
        ImGui::CloseCurrentPopup();
      }
      ImGui::EndPopup();
    }

    // Edit Menu (undo/redo history)
    if (ImGui::BeginMenu("Edit")) {
      Engine::UndoManager &undoManager = Engine::UndoManager::instance();

      // Show the active profile's binding next to the menu entry
      auto bindingLabel = [](StereoVista::ShortcutAction action,
                             const char *fallback) -> std::string {
        const StereoVista::ShortcutProfile *profile =
            shortcutManager.getActiveProfile();
        if (profile) {
          const auto &bindings = profile->getBindings(action);
          if (!bindings.empty() && bindings[0].isValid()) {
            return bindings[0].toString();
          }
        }
        return fallback;
      };

      std::string undoLabel = "Undo";
      if (undoManager.canUndo()) {
        undoLabel += " " + undoManager.undoDescription();
      }
      if (ImGui::MenuItem(
              undoLabel.c_str(),
              bindingLabel(StereoVista::ShortcutAction::Undo, "Ctrl+Z").c_str(),
              false, undoManager.canUndo())) {
        undoManager.undo();
      }

      std::string redoLabel = "Redo";
      if (undoManager.canRedo()) {
        redoLabel += " " + undoManager.redoDescription();
      }
      if (ImGui::MenuItem(
              redoLabel.c_str(),
              bindingLabel(StereoVista::ShortcutAction::Redo, "Ctrl+Y").c_str(),
              false, undoManager.canRedo())) {
        undoManager.redo();
      }

      ImGui::EndMenu();
    }

    // Create Menu
    if (ImGui::BeginMenu("Create")) {
      // Use non-collapsible headers instead of dropdowns
      DrawSectionHeader("Primitives");
      if (ImGui::MenuItem("Cube")) {
        Engine::Model newCube =
            Engine::createCube(glm::vec3(0.8f, 0.8f, 0.8f), 1.0f, 0.0f);
        newCube.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(0.5f);
        if (preferences.enableSpawnAnimation) {
          newCube.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newCube.scale = targetScale;
        }
        currentScene.models.push_back(newCube);
        Engine::Undo::recordModelAdded(
            static_cast<int>(currentScene.models.size()) - 1);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      if (ImGui::MenuItem("Sphere")) {
        Engine::Model newSphere =
            Engine::createSphere(glm::vec3(0.8f, 0.4f, 0.4f), 1.0f, 0.0f);
        newSphere.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(0.5f);
        if (preferences.enableSpawnAnimation) {
          newSphere.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newSphere.scale = targetScale;
        }
        currentScene.models.push_back(newSphere);
        Engine::Undo::recordModelAdded(
            static_cast<int>(currentScene.models.size()) - 1);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      if (ImGui::MenuItem("Cylinder")) {
        Engine::Model newCylinder =
            Engine::createCylinder(glm::vec3(0.4f, 0.8f, 0.4f), 1.0f, 0.0f);
        newCylinder.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(0.5f);
        if (preferences.enableSpawnAnimation) {
          newCylinder.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newCylinder.scale = targetScale;
        }
        currentScene.models.push_back(newCylinder);
        Engine::Undo::recordModelAdded(
            static_cast<int>(currentScene.models.size()) - 1);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      if (ImGui::MenuItem("Plane")) {
        Engine::Model newPlane =
            Engine::createPlane(glm::vec3(0.6f, 0.6f, 0.8f), 1.0f, 0.0f);
        newPlane.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(1.0f);
        if (preferences.enableSpawnAnimation) {
          newPlane.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newPlane.scale = targetScale;
        }
        currentScene.models.push_back(newPlane);
        Engine::Undo::recordModelAdded(
            static_cast<int>(currentScene.models.size()) - 1);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      if (ImGui::MenuItem("Torus")) {
        Engine::Model newTorus =
            Engine::createTorus(glm::vec3(0.8f, 0.6f, 0.2f), 1.0f, 0.0f);
        newTorus.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(0.8f);
        if (preferences.enableSpawnAnimation) {
          newTorus.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newTorus.scale = targetScale;
        }
        currentScene.models.push_back(newTorus);
        Engine::Undo::recordModelAdded(
            static_cast<int>(currentScene.models.size()) - 1);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      ImGui::Separator();

      // Use non-collapsible header for Lights
      DrawSectionHeader("Lights");
      if (ImGui::MenuItem("Point Light")) {
        Engine::PointLight newPointLight;
        newPointLight.position = glm::vec3(0.0f, 2.0f, 0.0f);
        newPointLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
        newPointLight.intensity = 1.0f;
        newPointLight.lightSpaceMatrix = glm::mat4(1.0f);
        newPointLight.castShadows = true; // default: cast shadows
        pointLights.push_back(newPointLight);
        Engine::Undo::recordPointLightAdded(
            static_cast<int>(pointLights.size()) - 1);
        currentSelectedIndex = pointLights.size() - 1;
        currentSelectedType = SelectedType::PointLight;
      }

      if (ImGui::MenuItem("Spot Light")) {
        Engine::SpotLight newSpotLight;
        newSpotLight.position = glm::vec3(0.0f, 3.0f, 0.0f);
        newSpotLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        newSpotLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
        newSpotLight.intensity = 1.0f;
        newSpotLight.innerCutOff = glm::cos(glm::radians(12.5f));
        newSpotLight.outerCutOff = glm::cos(glm::radians(17.5f));
        newSpotLight.lightSpaceMatrix = glm::mat4(1.0f);
        spotLights.push_back(newSpotLight);
        Engine::Undo::recordSpotLightAdded(
            static_cast<int>(spotLights.size()) - 1);
        currentSelectedIndex = spotLights.size() - 1;
        currentSelectedType = SelectedType::SpotLight;
      }

      ImGui::EndMenu();
    }

    // Divider: scene/document menus (File/Edit/Create) | display menus
    // (View/Camera/Cursor).
    MenuBarSeparator();

    // View Menu
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Show GUI", "G", &showGui);
      ImGui::MenuItem("Show Performance Overlay", nullptr, &showFPS);
      ImGui::MenuItem("Wireframe Mode", nullptr, &camera.wireframe);

      ImGui::Separator();

      ImGui::MenuItem("Show Radar", nullptr, &preferences.radarEnabled);
      ImGui::MenuItem("Show Zero Plane", nullptr, &preferences.showZeroPlane);

      ImGui::EndMenu();
    }

    // Camera Menu
    if (ImGui::BeginMenu("Camera")) {
      DrawSectionHeader("Movement");

      if (ImGui::SliderFloat("Speed", &camera.speedFactor, 0.1f, 5.0f,
                             "%.1fx")) {
        preferences.cameraSpeedFactor = camera.speedFactor;
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Multiplies camera movement speed");

      if (ImGui::SliderFloat("Sensitivity", &camera.MouseSensitivity, 0.01f,
                             0.08f, "%.3f")) {
        preferences.mouseSensitivity = camera.MouseSensitivity;
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Mouse rotation sensitivity");

      DrawSectionHeader("Zoom Behavior");

      if (ImGui::MenuItem("Zoom to Cursor", nullptr, &camera.zoomToCursor)) {
        preferences.zoomToCursor = camera.zoomToCursor;
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Scroll zooms toward cursor position");

      DrawSectionHeader("Orbit Mode");

      bool standardOrbit = !camera.orbitAroundCursor && !orbitFollowsCursor;
      if (ImGui::RadioButton("Standard", standardOrbit)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = false;
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Orbit around viewport center");

      bool orbitAroundCursorOption = camera.orbitAroundCursor;
      if (ImGui::RadioButton("Around Cursor", orbitAroundCursorOption)) {
        camera.orbitAroundCursor = true;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = true;
        preferences.orbitFollowsCursor = false;
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Orbit around cursor position");

      bool orbitFollowsCursorOption = orbitFollowsCursor;
      if (ImGui::RadioButton("Follow Cursor", orbitFollowsCursorOption)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = true;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = true;
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Center view on cursor, then orbit");

      ImGui::EndMenu();
    }

    // Cursor Menu
    if (ImGui::BeginMenu("Cursor")) {
      auto *sphereCursor = cursorManager.getSphereCursor();
      auto *fragmentCursor = cursorManager.getFragmentCursor();
      auto *planeCursor = cursorManager.getPlaneCursor();

      DrawSectionHeader("Cursor Types");

      bool showSphereCursor = sphereCursor->isVisible();
      if (ImGui::MenuItem("3D Sphere", nullptr, &showSphereCursor)) {
        sphereCursor->setVisible(showSphereCursor);
      }

      bool showFragmentCursor = fragmentCursor->isVisible();
      if (ImGui::MenuItem("2D Circle", nullptr, &showFragmentCursor)) {
        fragmentCursor->setVisible(showFragmentCursor);
      }

      bool showPlaneCursor = planeCursor->isVisible();
      if (ImGui::MenuItem("Surface Plane", nullptr, &showPlaneCursor)) {
        planeCursor->setVisible(showPlaneCursor);
      }

      ImGui::Separator();

      if (ImGui::BeginMenu("Quick Presets")) {
        std::vector<std::string> presetNames =
            Engine::CursorPresetManager::getPresetNames();
        for (const auto &name : presetNames) {
          if (ImGui::MenuItem(name.c_str(), nullptr,
                              currentPresetName == name)) {
            currentPresetName = name;
            Engine::CursorPreset loadedPreset =
                Engine::CursorPresetManager::applyCursorPreset(name);

            sphereCursor->setVisible(loadedPreset.showSphereCursor);
            sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(
                loadedPreset.sphereScalingMode));
            sphereCursor->setFixedRadius(loadedPreset.sphereFixedRadius);
            sphereCursor->setTransparency(loadedPreset.sphereTransparency);
            sphereCursor->setShowInnerSphere(loadedPreset.showInnerSphere);
            sphereCursor->setColor(loadedPreset.cursorColor);
            sphereCursor->setInnerSphereColor(loadedPreset.innerSphereColor);
            sphereCursor->setInnerSphereFactor(loadedPreset.innerSphereFactor);
            sphereCursor->setEdgeSoftness(loadedPreset.cursorEdgeSoftness);
            sphereCursor->setCenterTransparency(
                loadedPreset.cursorCenterTransparency);

            fragmentCursor->setVisible(loadedPreset.showFragmentCursor);
            fragmentCursor->setBaseInnerRadius(
                loadedPreset.fragmentBaseInnerRadius);

            planeCursor->setVisible(loadedPreset.showPlaneCursor);
            planeCursor->setDiameter(loadedPreset.planeDiameter);
            planeCursor->setColor(loadedPreset.planeColor);

            preferences.currentPresetName = currentPresetName;
            savePreferences();
          }
        }
        ImGui::EndMenu();
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Advanced Settings...")) {
        showCursorSettingsWindow = true;
      }

      ImGui::EndMenu();
    }

    // Divider: display menus | tool windows.
    MenuBarSeparator();

    // Tooltip helper for menu entries that open a tool window.
    auto menuButtonTooltip = [](const char *desc) {
      if (ImGui::IsItemHovered(ImGuiHoveredFlags_AllowWhenDisabled))
        ImGui::SetTooltip("%s", desc);
    };

    // Tools Menu — collapses the tool windows that used to sit as separate bare
    // buttons cluttering the bar. Each entry toggles its window; the check mark
    // reflects whether that window is currently open.
    if (ImGui::BeginMenu("Tools")) {
      ImGui::MenuItem("Brush Tool", nullptr, &showBrushToolWindow);
      menuButtonTooltip("Scatter copies of a model across surfaces with the "
                        "paint brush");

      // "Measure" is now contributed by MeasurementPlugin through the plugin
      // menu dispatch below, alongside any other registered plugins.

      ImGui::MenuItem("Section Planes", nullptr, &showClipPlaneToolWindow);
      menuButtonTooltip("Slice the scene with clipping planes to inspect "
                        "interiors");

      ImGui::MenuItem("Snapshots", nullptr, &showSnapshotsWindow);
      menuButtonTooltip("Save and restore camera / scene / tool snapshots");

      ImGui::MenuItem("Scene Manager", nullptr, &showSceneManagerWindow);
      menuButtonTooltip("Save / load scenes, recent files, scene info and "
                        "save options");

      // Plugin-contributed tool entries (each toggles its own window).
      ImGui::Separator();
      g_pluginManager.renderMenu(g_pluginContext);

      ImGui::EndMenu();
    }

    // Divider before the app-level Settings entry.
    MenuBarSeparator();

    ImGui::MenuItem("Settings", nullptr, &showSettingsWindow);
    menuButtonTooltip("Application settings: rendering, camera, environment, "
                      "input and shortcuts");

    ImGui::EndMainMenuBar();
  }

  // Publish the menu bar height so the render loop can reserve the top strip.
  g_dockTopHeight = ImGui::GetFrameHeight();

  // ========================
  // SCENE HIERARCHY PANEL
  // ========================
  SetNextWindowPosInMainWindow(ImVec2(0, ImGui::GetFrameHeight()));
  ImGui::SetNextWindowSize(ImVec2(320 * g_GuiScale.currentScale,
                                  viewport->Size.y - ImGui::GetFrameHeight()));
  // Ensure only the Scene Hierarchy window itself has square corners (keep
  // inner elements rounded)
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

  ImGui::Begin("Scene Hierarchy", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse);

  // Publish the panel width so the render loop can reserve the left strip.
  g_dockLeftWidth = ImGui::GetWindowWidth();

  // Scene objects list with search filter
  static char searchBuffer[128] = "";
  DrawInlineIcon(ICON_FA_SEARCH,
                 ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  const bool hasSearchText = (searchBuffer[0] != '\0');
  const float clearBtnSize = ImGui::GetFrameHeight();
  ImGui::SetNextItemWidth(
      ImGui::GetContentRegionAvail().x -
      (hasSearchText ? (clearBtnSize + ImGui::GetStyle().ItemSpacing.x) : 0.0f));
  ImGui::InputTextWithHint("##Search", "Search objects...", searchBuffer,
                           sizeof(searchBuffer));
  if (hasSearchText) {
    ImGui::SameLine();
    // Force a square button so the icon font's smaller glyph stays aligned
    // with the input box height.
    if (g_Fonts.icons)
      ImGui::PushFont(g_Fonts.icons);
    bool clearClicked = ImGui::Button(
        g_Fonts.icons ? ICON_FA_TIMES "##clearsearch" : "x##clearsearch",
        ImVec2(clearBtnSize, clearBtnSize));
    if (g_Fonts.icons)
      ImGui::PopFont();
    if (clearClicked)
      searchBuffer[0] = '\0';
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Clear search");
  }
  ImGui::Separator();

  if (ImGui::BeginChild("ObjectList", ImVec2(0, 250 * g_GuiScale.currentScale),
                        true)) {
    ImGui::Columns(2, "ObjectColumns", false);
    ImGui::SetColumnWidth(0, 60 * g_GuiScale.currentScale);

    // Tracks whether the active search matched any object, so the list can
    // show a friendly "no results" hint instead of appearing empty.
    bool anyMatch = false;

    // Sun Object (always at top, ungrouped)
    if (searchMatches("Sun", searchBuffer)) {
      anyMatch = true;
      ImGui::PushID("sun");
      bool sunVisible = sun.enabled;
      if (ImGui::Checkbox("##visible", &sunVisible))
        sun.enabled = sunVisible;
      ImGui::NextColumn();

      bool isSunSelected = (currentSelectedType == SelectedType::Sun);
      ImGui::AlignTextToFramePadding();
      // Render Sun with FontAwesome icon to avoid missing glyphs in fonts
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_SUN);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      if (ImGui::Selectable("Sun", isSunSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        currentSelectedType = SelectedType::Sun;
        currentSelectedIndex = -1;
        currentSelectedMeshIndex = -1;
      }
      ImGui::NextColumn();
      ImGui::PopID();
    }

    // Group objects by sourceScenePath
    std::map<std::string, std::vector<int>> sceneModels;
    std::map<std::string, std::vector<int>> scenePointClouds;
    std::map<std::string, std::vector<int>> scenePointLights;
    std::map<std::string, std::vector<int>> sceneSpotLights;

    // Collect models by scene
    for (int i = 0; i < currentScene.models.size(); i++) {
      sceneModels[currentScene.models[i].sourceScenePath].push_back(i);
    }

    // Collect point clouds by scene
    for (int i = 0; i < currentScene.pointClouds.size(); i++) {
      scenePointClouds[currentScene.pointClouds[i].sourceScenePath].push_back(
          i);
    }

    // Collect point lights by scene
    for (int i = 0; i < pointLights.size(); i++) {
      scenePointLights[pointLights[i].sourceScenePath].push_back(i);
    }

    // Collect spot lights by scene
    for (int i = 0; i < spotLights.size(); i++) {
      sceneSpotLights[spotLights[i].sourceScenePath].push_back(i);
    }

    // Get all unique scene paths
    std::set<std::string> allScenePaths;
    for (const auto &pair : sceneModels)
      allScenePaths.insert(pair.first);
    for (const auto &pair : scenePointClouds)
      allScenePaths.insert(pair.first);
    for (const auto &pair : scenePointLights)
      allScenePaths.insert(pair.first);
    for (const auto &pair : sceneSpotLights)
      allScenePaths.insert(pair.first);

    // Helper lambda to render a model
    auto renderModel = [&](int i) {
      std::string modelName = currentScene.models[i].name;
      if (!searchMatches(modelName, searchBuffer))
        return;
      anyMatch = true;

      ImGui::PushID(i);
      ImGui::AlignTextToFramePadding();

      bool visible = currentScene.models[i].visible;
      if (ImGui::Checkbox("##visible", &visible)) {
        currentScene.models[i].visible = visible;
      }
      ImGui::NextColumn();

      bool isModelSelected = (currentSelectedIndex == i &&
                              currentSelectedType == SelectedType::Model);
      bool hasMeshes = currentScene.models[i].getMeshes().size() > 0;

      ImGui::AlignTextToFramePadding();
      ImGuiTreeNodeFlags flags =
          ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

      if (!hasMeshes)
        flags |= ImGuiTreeNodeFlags_Leaf;
      if (isModelSelected && currentSelectedMeshIndex == -1)
        flags |= ImGuiTreeNodeFlags_Selected;

      // Render model icon with FontAwesome and the model name separately
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_CUBE);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      bool nodeOpen = ImGui::TreeNodeEx((modelName).c_str(), flags);

      if (ImGui::IsItemClicked()) {
        currentSelectedType = SelectedType::Model;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
      }

      ImGui::NextColumn();

      if (nodeOpen && hasMeshes) {
        for (size_t meshIndex = 0;
             meshIndex < currentScene.models[i].getMeshes().size();
             meshIndex++) {
          ImGui::Columns(2, "MeshColumns", false);
          ImGui::SetColumnWidth(0, 60);

          ImGui::PushID(static_cast<int>(meshIndex));
          bool meshVisible =
              currentScene.models[i].getMeshes()[meshIndex].visible;
          if (ImGui::Checkbox("##meshvisible", &meshVisible)) {
            currentScene.models[i].getMeshes()[meshIndex].visible = meshVisible;
          }
          ImGui::PopID();
          ImGui::NextColumn();

          ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf |
                                         ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                         ImGuiTreeNodeFlags_SpanAvailWidth;

          if (isModelSelected &&
              currentSelectedMeshIndex == static_cast<int>(meshIndex)) {
            meshFlags |= ImGuiTreeNodeFlags_Selected;
          }

          ImGui::Indent(20.0f);
          if (g_Fonts.icons) {
            ImGui::PushFont(g_Fonts.icons);
            ImGui::Text(ICON_FA_CUBE);
            ImGui::PopFont();
            ImGui::SameLine();
          }
          ImGui::TreeNodeEx(("Mesh " + std::to_string(meshIndex + 1)).c_str(),
                            meshFlags);

          if (ImGui::IsItemClicked()) {
            currentSelectedType = SelectedType::Model;
            currentSelectedIndex = i;
            currentSelectedMeshIndex = static_cast<int>(meshIndex);
          }

          ImGui::Unindent(20.0f);
          ImGui::NextColumn();
        }
        ImGui::Columns(2, "ObjectColumns", false);
        ImGui::SetColumnWidth(0, 60);
        ImGui::TreePop();
      }
      ImGui::PopID();
    };

    // Helper lambda to render a point cloud
    auto renderPointCloud = [&](int i) {
      std::string pcName = currentScene.pointClouds[i].name;
      if (!searchMatches(pcName, searchBuffer))
        return;
      anyMatch = true;

      ImGui::PushID(i + currentScene.models.size());
      bool isSelected = (currentSelectedIndex == i &&
                         currentSelectedType == SelectedType::PointCloud);

      bool visible = currentScene.pointClouds[i].visible;
      if (ImGui::Checkbox("##visible", &visible)) {
        currentScene.pointClouds[i].visible = visible;
      }
      ImGui::NextColumn();

      ImGui::AlignTextToFramePadding();
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_CLOUD);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      if (ImGui::Selectable((pcName).c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        currentSelectedType = SelectedType::PointCloud;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
      }
      ImGui::NextColumn();
      ImGui::PopID();
    };

    // Helper lambda to render a point light
    auto renderPointLight = [&](int i) {
      std::string lightText = "Point Light " + std::to_string(i + 1);
      if (!searchMatches(lightText, searchBuffer))
        return;
      anyMatch = true;

      ImGui::PushID(i + currentScene.models.size() + 1000);
      bool isSelected = (currentSelectedIndex == i &&
                         currentSelectedType == SelectedType::PointLight);

      ImGui::AlignTextToFramePadding();
      ImGui::Text("");
      ImGui::NextColumn();

      ImGui::AlignTextToFramePadding();
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_LIGHTBULB);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      if (ImGui::Selectable(lightText.c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        currentSelectedType = SelectedType::PointLight;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
      }
      ImGui::NextColumn();
      ImGui::PopID();
    };

    // Helper lambda to render a spot light
    auto renderSpotLight = [&](int i) {
      std::string spotText = "Spot Light " + std::to_string(i + 1);
      if (!searchMatches(spotText, searchBuffer))
        return;
      anyMatch = true;

      ImGui::PushID(i + currentScene.models.size() + 2000);
      bool isSelected = (currentSelectedIndex == i &&
                         currentSelectedType == SelectedType::SpotLight);

      ImGui::AlignTextToFramePadding();
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_BULLSEYE);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      if (ImGui::Selectable(spotText.c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        currentSelectedType = SelectedType::SpotLight;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
      }
      ImGui::NextColumn();
      ImGui::PopID();
    };

    // First, render ungrouped objects (empty sourceScenePath)
    if (sceneModels.count("") > 0) {
      for (int idx : sceneModels[""]) {
        renderModel(idx);
      }
    }
    if (scenePointClouds.count("") > 0) {
      for (int idx : scenePointClouds[""]) {
        renderPointCloud(idx);
      }
    }
    if (scenePointLights.count("") > 0) {
      for (int idx : scenePointLights[""]) {
        renderPointLight(idx);
      }
    }
    if (sceneSpotLights.count("") > 0) {
      for (int idx : sceneSpotLights[""]) {
        renderSpotLight(idx);
      }
    }

    // Then, render grouped scenes
    for (const auto &scenePath : allScenePaths) {
      if (scenePath.empty())
        continue; // Skip empty path (already rendered)

      // Extract scene name from path
      std::filesystem::path path(scenePath);
      std::string sceneName = path.stem().string();

      // Count total objects in this scene
      int totalObjects = 0;
      if (sceneModels.count(scenePath))
        totalObjects += sceneModels[scenePath].size();
      if (scenePointClouds.count(scenePath))
        totalObjects += scenePointClouds[scenePath].size();
      if (scenePointLights.count(scenePath))
        totalObjects += scenePointLights[scenePath].size();
      if (sceneSpotLights.count(scenePath))
        totalObjects += sceneSpotLights[scenePath].size();

      if (totalObjects == 0)
        continue;

      ImGui::PushID(scenePath.c_str());

      // Calculate master visibility state for this scene
      bool anyVisible = false;
      bool allVisible = true;
      int visibleCount = 0;
      int totalVisibleObjects = 0;

      if (sceneModels.count(scenePath)) {
        for (int idx : sceneModels[scenePath]) {
          totalVisibleObjects++;
          if (currentScene.models[idx].visible) {
            anyVisible = true;
            visibleCount++;
          } else {
            allVisible = false;
          }
        }
      }
      if (scenePointClouds.count(scenePath)) {
        for (int idx : scenePointClouds[scenePath]) {
          totalVisibleObjects++;
          if (currentScene.pointClouds[idx].visible) {
            anyVisible = true;
            visibleCount++;
          } else {
            allVisible = false;
          }
        }
      }

      // Master visibility toggle
      bool masterVisible = allVisible;
      if (visibleCount > 0 && visibleCount < totalVisibleObjects) {
        // Mixed state - show as partially checked
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
      }

      if (ImGui::Checkbox("##scenevisible", &masterVisible)) {
        // Toggle all objects in this scene
        if (sceneModels.count(scenePath)) {
          for (int idx : sceneModels[scenePath]) {
            currentScene.models[idx].visible = masterVisible;
          }
        }
        if (scenePointClouds.count(scenePath)) {
          for (int idx : scenePointClouds[scenePath]) {
            currentScene.pointClouds[idx].visible = masterVisible;
          }
        }
      }

      if (visibleCount > 0 && visibleCount < totalVisibleObjects) {
        ImGui::PopItemFlag();
      }

      ImGui::NextColumn();

      // Scene group header
      ImGuiTreeNodeFlags sceneFlags =
          ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_FOLDER);
        ImGui::PopFont();
        ImGui::SameLine();
      }

      bool sceneOpen = ImGui::TreeNodeEx(
          (sceneName + " (" + std::to_string(totalObjects) + ")").c_str(),
          sceneFlags);

      // Context menu for scene group
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete Scene")) {
          // Mark for deletion
          std::vector<int> modelsToDelete;
          std::vector<int> pointCloudsToDelete;
          std::vector<int> pointLightsToDelete;
          std::vector<int> spotLightsToDelete;

          if (sceneModels.count(scenePath)) {
            modelsToDelete = sceneModels[scenePath];
          }
          if (scenePointClouds.count(scenePath)) {
            pointCloudsToDelete = scenePointClouds[scenePath];
          }
          if (scenePointLights.count(scenePath)) {
            pointLightsToDelete = scenePointLights[scenePath];
          }
          if (sceneSpotLights.count(scenePath)) {
            spotLightsToDelete = sceneSpotLights[scenePath];
          }

          // Delete all objects in the group as a single undoable action
          Engine::Undo::deleteSceneGroup(modelsToDelete, pointCloudsToDelete,
                                         pointLightsToDelete,
                                         spotLightsToDelete);

          // Mark voxelizer dirty if models were deleted
          if (!modelsToDelete.empty() && voxelizer) {
            voxelizer->markDirty();
          }

          // Clear selection if needed
          currentSelectedType = SelectedType::None;
          currentSelectedIndex = -1;
          currentSelectedMeshIndex = -1;
        }
        ImGui::EndPopup();
      }

      ImGui::NextColumn();

      if (sceneOpen) {
        // Render all objects in this scene group
        if (sceneModels.count(scenePath)) {
          for (int idx : sceneModels[scenePath]) {
            renderModel(idx);
          }
        }
        if (scenePointClouds.count(scenePath)) {
          for (int idx : scenePointClouds[scenePath]) {
            renderPointCloud(idx);
          }
        }
        if (scenePointLights.count(scenePath)) {
          for (int idx : scenePointLights[scenePath]) {
            renderPointLight(idx);
          }
        }
        if (sceneSpotLights.count(scenePath)) {
          for (int idx : sceneSpotLights[scenePath]) {
            renderSpotLight(idx);
          }
        }

        ImGui::TreePop();
      }

      ImGui::PopID();
    }

    // Brush Clusters List
    extern Tools::BrushTool brushTool;
    int clusterCount = brushTool.getClusterCount();
    for (int i = 0; i < clusterCount; i++) {
      const Tools::BrushCluster *cluster = brushTool.getCluster(i);
      if (!cluster)
        continue;

      std::string clusterName = cluster->name;
      if (!searchMatches(clusterName, searchBuffer))
        continue;
      anyMatch = true;

      ImGui::PushID(i + currentScene.models.size() +
                    currentScene.pointClouds.size() + 5000);
      bool isSelected = (currentSelectedIndex == i &&
                         currentSelectedType == SelectedType::BrushCluster);

      ImGui::AlignTextToFramePadding();
      ImGui::Text(""); // No visibility checkbox for clusters
      ImGui::NextColumn();

      ImGui::AlignTextToFramePadding();
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_PAINT_BRUSH);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      std::string displayName =
          clusterName + " (" + std::to_string(cluster->instances.size()) + ")";
      if (ImGui::Selectable(displayName.c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        currentSelectedType = SelectedType::BrushCluster;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
        brushTool.setActiveCluster(i);
      }
      ImGui::NextColumn();
      ImGui::PopID();
    }

    ImGui::Columns(1);

    // Friendly hint when a search filters everything out.
    if (!anyMatch && hasSearchText) {
      ImGui::Spacing();
      ImGui::PushStyleColor(ImGuiCol_Text,
                            ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
      ImGui::TextWrapped("No objects match \"%s\".", searchBuffer);
      ImGui::PopStyleColor();
    }
    ImGui::EndChild();
  }

  ImGui::Spacing();

  // Properties Panel
  DrawSectionHeader("Properties");

  if (ImGui::BeginChild("PropertiesPanel", ImVec2(0, 0), false)) {
    if (currentSelectedType == SelectedType::Model &&
        currentSelectedIndex >= 0 &&
        currentSelectedIndex < currentScene.models.size()) {

      auto &model = currentScene.models[currentSelectedIndex];

      if (currentSelectedMeshIndex >= 0 &&
          currentSelectedMeshIndex <
              static_cast<int>(model.getMeshes().size())) {
        renderMeshManipulationPanel(model, currentSelectedMeshIndex, shader);
      } else {
        renderModelManipulationPanel(model, shader);
      }
    } else if (currentSelectedType == SelectedType::PointCloud &&
               currentSelectedIndex >= 0 &&
               currentSelectedIndex < currentScene.pointClouds.size()) {
      renderPointCloudManipulationPanel(
          currentScene.pointClouds[currentSelectedIndex]);
    } else if (currentSelectedType == SelectedType::Sun) {
      renderSunManipulationPanel();
    } else if (currentSelectedType == SelectedType::PointLight &&
               currentSelectedIndex >= 0 &&
               currentSelectedIndex < pointLights.size()) {
      renderPointLightManipulationPanel();
    } else if (currentSelectedType == SelectedType::SpotLight &&
               currentSelectedIndex >= 0 &&
               currentSelectedIndex < spotLights.size()) {
      renderSpotLightManipulationPanel();
    } else if (currentSelectedType == SelectedType::BrushCluster &&
               currentSelectedIndex >= 0) {
      extern Tools::BrushTool brushTool;
      int clusterCount = brushTool.getClusterCount();
      if (currentSelectedIndex < clusterCount) {
        Tools::BrushCluster *cluster =
            brushTool.getCluster(currentSelectedIndex);
        if (cluster) {
          DrawPanelTitle(ICON_FA_PAINT_BRUSH, cluster->name);

          if (cluster->sourceModelIndex >= 0 &&
              cluster->sourceModelIndex < currentScene.models.size()) {
            ImGui::Text(
                "Source Model: %s",
                currentScene.models[cluster->sourceModelIndex].name.c_str());
          }
          ImGui::Text("Instances: %d", brushTool.getInstanceCountForCluster(
                                           currentSelectedIndex));

          ImGui::Spacing();

          // Instance Variation
          DrawSectionHeader("Instance Variation");

          ImGui::SliderFloat("Min Scale", &cluster->minScale, 0.1f, 5.0f,
                             "%.2f");
          ImGui::SameLine();
          DrawHelpMarker("Minimum scale multiplier");

          ImGui::SliderFloat("Max Scale", &cluster->maxScale, 0.1f, 5.0f,
                             "%.2f");
          ImGui::SameLine();
          DrawHelpMarker("Maximum scale multiplier");

          if (cluster->maxScale < cluster->minScale) {
            cluster->maxScale = cluster->minScale;
          }

          ImGui::SliderFloat("Rotation", &cluster->rotationRandomization, 0.0f,
                             1.0f, "%.2f");
          ImGui::SameLine();
          DrawHelpMarker("Random rotation");

          ImGui::SliderFloat("Color Variation", &cluster->colorVariation, 0.0f,
                             1.0f, "%.2f");
          ImGui::SameLine();
          DrawHelpMarker("Random color variation");

          ImGui::Checkbox("Align to Surface", &cluster->alignToNormal);

          ImGui::Spacing();

          // Cluster Actions
          DrawSectionHeader("Actions");

          if (ImGui::Button("Set as Active Cluster", ImVec2(-1, 0))) {
            brushTool.setActiveCluster(currentSelectedIndex);
          }

          if (ImGui::Button("Clear Instances", ImVec2(-1, 0))) {
            brushTool.clearInstancesForCluster(currentSelectedIndex);
          }

          if (ImGui::Button("Delete Cluster", ImVec2(-1, 0))) {
            brushTool.deleteCluster(currentSelectedIndex);
            currentSelectedType = SelectedType::None;
            currentSelectedIndex = -1;
          }
        }
      }
    } else {
      ImGui::TextDisabled("No object selected");
      ImGui::Spacing();
      ImGui::TextWrapped("Select an object from the hierarchy above to view "
                         "and edit its properties.");
    }
    ImGui::EndChild();
  }

  ImGui::End();
  ImGui::PopStyleVar(
      1); // Restore rounding only after the Scene Hierarchy window ends

  // Render other windows
  if (showSettingsWindow) {
    renderSettingsWindow();
  }

  if (showCursorSettingsWindow) {
    renderCursorSettingsWindow();
  }

  if (showBrushToolWindow) {
    renderBrushToolWindow();
  }

  // Plugin ImGui windows (MeasurementPlugin draws the measurement window here,
  // gated by showMeasurementToolWindow; other plugins draw their own windows).
  g_pluginManager.renderUI(g_pluginContext);

  if (showClipPlaneToolWindow) {
    renderClipPlaneToolWindow();
  }

  if (showSnapshotsWindow) {
    renderSnapshotsWindow();
  }

  if (showSceneManagerWindow) {
    renderSceneManagerWindow();
  }

  // Screen-space measurement labels (distances/angles/coordinates) projected
  // from the world-space measurement overlay
  drawMeasurementLabels();

  // Floating view-mode toolbar (top-left) and transform-gizmo toolbar
  // (top-right) of the viewport
  renderViewModeToolbar();
  renderGizmoViewportToolbar();

  // Centered hint while nothing is loaded yet
  renderEmptySceneHint();

  // Performance overlay (bottom-right corner)
  if (showFPS) {
    renderPerformanceOverlay();
  }

  // Progressive point-cloud load progress (bottom-left, only while streaming)
  renderPointCloudStreamingOverlay();

  // Transient status notifications (bottom-center)
  renderToasts();

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void renderSettingsWindow() {
  float scale = g_GuiScale.currentScale;
  ImGui::SetNextWindowSize(ImVec2(900 * scale, 720 * scale),
                           ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(640 * scale, 420 * scale),
                                      ImVec2(100000.0f, 100000.0f));
  ImGui::Begin("Settings", &showSettingsWindow);
  bool settingsChanged = false;

  // Shared DDGI tuning controls. The same probe volume drives GI in both
  // Radiance mode ("Enable DDGI") and Shadow Mapping mode ("Enable Indirect
  // Lighting"), so both expose this identical set of sliders. These live in the
  // mutually-exclusive lighting-mode branches of the Rendering tab, so only one
  // copy is ever submitted per frame -- no ImGui ID collision.
  auto drawDDGISettings = [&]() {
    ImGui::Indent();

    if (ImGui::SliderInt3("Probe Counts",
                          &preferences.radianceSettings.ddgiProbeCounts.x, 2,
                          48)) {
      ::radianceSettings.ddgiProbeCounts =
          preferences.radianceSettings.ddgiProbeCounts;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Probes along X/Y/Z. More probes = finer GI, but more cost "
                   "and memory. Changing this rebuilds the volume.");

    if (ImGui::SliderInt("Rays / Probe",
                         &preferences.radianceSettings.ddgiRaysPerProbe, 16,
                         256)) {
      ::radianceSettings.ddgiRaysPerProbe =
          preferences.radianceSettings.ddgiRaysPerProbe;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Rays traced per probe each frame. More = faster "
                   "convergence, less noise, higher cost.");

    if (ImGui::SliderFloat("GI Intensity",
                           &preferences.radianceSettings.ddgiGIIntensity, 0.0f,
                           1.0f, "%.3f")) {
      ::radianceSettings.ddgiGIIntensity =
          preferences.radianceSettings.ddgiGIIntensity;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Multiplier for the indirect diffuse contribution. GI is "
                   "physically multi-bounce, so high-albedo or bright-sky "
                   "scenes need a low value (~0.05-0.3). Ctrl+click to type an "
                   "exact value.");

    if (ImGui::SliderFloat("Visibility (AO)",
                           &preferences.radianceSettings.ddgiVisibilityStrength,
                           0.0f, 1.0f, "%.2f")) {
      ::radianceSettings.ddgiVisibilityStrength =
          preferences.radianceSettings.ddgiVisibilityStrength;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("How strongly probe visibility darkens indirect light near "
                   "geometry. Lower this if contact shadows look like harsh AO; "
                   "raise it if light leaks through walls.");

    if (ImGui::SliderFloat("Hysteresis",
                           &preferences.radianceSettings.ddgiHysteresis, 0.0f,
                           0.99f, "%.3f")) {
      ::radianceSettings.ddgiHysteresis =
          preferences.radianceSettings.ddgiHysteresis;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Temporal blend. Higher = more stable but slower to react "
                   "to lighting/scene changes.");

    if (ImGui::SliderFloat("Normal Bias",
                           &preferences.radianceSettings.ddgiNormalBias, 0.0f,
                           1.0f, "%.2f")) {
      ::radianceSettings.ddgiNormalBias =
          preferences.radianceSettings.ddgiNormalBias;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Surface offset (fraction of probe spacing) used when "
                   "sampling probes. Reduces self-shadowing and leaks.");

    if (ImGui::Button("Reset DDGI")) {
      g_ddgiResetRequested = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Clear all probes; they reconverge over a few frames.");

    ImGui::Unindent();
  };

  // ===========================================================================
  // Sidebar navigation (left) + content area (right)
  // ===========================================================================
  struct SettingsNavEntry {
    int id;
    const char *icon;
    const char *label;
  };
  static const SettingsNavEntry kNavEntries[] = {
      {SETTINGS_CAT_RENDERING, ICON_FA_LIGHTBULB, "Rendering"},
      {SETTINGS_CAT_CAMERA, ICON_FA_VIDEO, "Camera & 3D"},
      {SETTINGS_CAT_ENVIRONMENT, ICON_FA_MOUNTAIN, "Environment"},
      {SETTINGS_CAT_DISPLAY, ICON_FA_DESKTOP, "Display"},
      {SETTINGS_CAT_INPUT, ICON_FA_MOUSE, "Input & Devices"},
      {SETTINGS_CAT_IMPORT, ICON_FA_FILE_IMPORT, "Import"},
      {SETTINGS_CAT_SHORTCUTS, ICON_FA_KEYBOARD, "Shortcuts"},
  };

  float navWidth = 210.0f * scale;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.18f));
  ImGui::BeginChild("##SettingsNav", ImVec2(navWidth, 0), true);
  ImGui::PopStyleColor();
  {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * scale, 6 * scale));
    DrawInlineIcon(ICON_FA_SLIDERS_H, g_StyleColors.accent);
    ImGui::TextUnformatted("Settings");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    for (const auto &entry : kNavEntries) {
      if (DrawNavItem(entry.icon, entry.label, g_settingsCategory == entry.id))
        g_settingsCategory = entry.id;
    }
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("##SettingsContent", ImVec2(0, 0), false);

  // ===========================
  // RENDERING TAB
  // ===========================
  if (g_settingsCategory == SETTINGS_CAT_RENDERING) {
      ImGui::PushID("RenderingTab");
      DrawSectionHeader("Lighting System");

      const char *lightingModes[] = {
          "Shadow Mapping", "Voxel Cone Tracing (GI)", "Radiance Raytracing"};
      int currentMode = static_cast<int>(preferences.lightingMode);
      if (ImGui::Combo("Mode", &currentMode, lightingModes,
                       IM_ARRAYSIZE(lightingModes))) {
        preferences.lightingMode = static_cast<GUI::LightingMode>(currentMode);
        ::currentLightingMode = preferences.lightingMode;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Shadow Mapping: Traditional shadows\nVoxel Cone Tracing: "
                     "Global illumination\nRadiance: Ray-traced lighting");

      // Wireframe mode setting
      if (ImGui::Checkbox("Wireframe Mode", &camera.wireframe)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Renders objects as wireframes instead of solid surfaces");

      if (ImGui::Checkbox("Enable Spawn Animation",
                          &preferences.enableSpawnAnimation)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Animate models when they are created or imported");

      // Mode-specific settings
      if (preferences.lightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
        ImGui::Spacing();
        DrawSectionHeader("Shadow Mapping Settings");

        if (ImGui::Checkbox("Enable Shadows", &preferences.enableShadows)) {
          ::enableShadows = preferences.enableShadows;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Toggle shadow mapping on/off");

        ImGui::Spacing();
        DrawSectionHeader("HDR Rendering");

        if (ImGui::Checkbox("Enable HDR", &preferences.hdrSettings.enabled)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Enable High Dynamic Range rendering for better lighting");

        if (preferences.hdrSettings.enabled) {
          if (ImGui::SliderFloat("Exposure", &preferences.hdrSettings.exposure,
                                 0.1f, 5.0f, "%.2f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Controls overall brightness of the scene");

          const char *toneMapOperators[] = {"Reinhard",
                                            "ACES",
                                            "Uncharted 2",
                                            "AgX",
                                            "Khronos PBR Neutral",
                                            "Tony McMapface"};
          if (ImGui::Combo("Tone Mapping",
                           &preferences.hdrSettings.toneMapOperator,
                           toneMapOperators, IM_ARRAYSIZE(toneMapOperators))) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          const char *toneMapDescriptions[] = {
              "Reinhard: Simple, classic tone mapping",
              "ACES: Industry standard, cinematic look",
              "Uncharted 2: Popular in games, good contrast",
              "AgX: Modern, perceptually accurate",
              "Khronos PBR Neutral: Neutral for PBR workflows",
              "Tony McMapface: Modern, perceptually motivated"};
          DrawHelpMarker(
              toneMapDescriptions[preferences.hdrSettings.toneMapOperator]);

          if (ImGui::Checkbox("Enable Bloom",
                              &preferences.hdrSettings.enableBloom)) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Add glow effect to bright areas");

          if (preferences.hdrSettings.enableBloom) {
            if (ImGui::SliderFloat("Bloom Threshold",
                                   &preferences.hdrSettings.bloomThreshold,
                                   0.01f, 1.0f, "%.2f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Brightness threshold for bloom effect");

            if (ImGui::SliderFloat("Bloom Intensity",
                                   &preferences.hdrSettings.bloomIntensity,
                                   0.0f, 1.0f, "%.3f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Strength of the bloom effect");
          }

          ImGui::Spacing();
          if (ImGui::Checkbox("Enable FXAA",
                              &preferences.hdrSettings.enableFXAA)) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Fast Approximate Anti-Aliasing. A cheap post-process pass that "
              "smooths jagged edges after tone mapping.");

          if (preferences.hdrSettings.enableFXAA) {
            if (ImGui::SliderFloat("FXAA Sub-pixel",
                                   &preferences.hdrSettings.fxaaSubpixel, 0.0f,
                                   1.0f, "%.2f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Amount of sub-pixel aliasing removal. Higher = "
                           "softer, lower = sharper.");

            if (ImGui::SliderFloat("FXAA Edge Threshold",
                                   &preferences.hdrSettings.fxaaEdgeThreshold,
                                   0.063f, 0.333f, "%.3f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Minimum contrast treated as an edge. Lower = more "
                           "edges smoothed (higher quality, slower).");
          }

          ImGui::Spacing();
          DrawSectionHeader("Color Grading");

          if (ImGui::SliderFloat("Contrast",
                                 &preferences.hdrSettings.contrast, 0.5f, 1.5f,
                                 "%.2f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Image contrast around mid-gray. 1.00 = neutral.");

          if (ImGui::SliderFloat("Saturation",
                                 &preferences.hdrSettings.saturation, 0.0f,
                                 2.0f, "%.2f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Color intensity. 1.00 = neutral, 0.00 = grayscale.");

          if (ImGui::Checkbox("Vignette",
                              &preferences.hdrSettings.enableVignette)) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Subtle darkening towards the screen corners that "
                         "draws the eye to the center of the frame.");

          if (preferences.hdrSettings.enableVignette) {
            if (ImGui::SliderFloat("Vignette Intensity",
                                   &preferences.hdrSettings.vignetteIntensity,
                                   0.0f, 1.0f, "%.2f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("How dark the corners get.");

            if (ImGui::SliderFloat("Vignette Radius",
                                   &preferences.hdrSettings.vignetteRadius,
                                   0.0f, 1.0f, "%.2f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Where the darkening starts. 0 = screen center, "
                           "1 = corners only.");

            if (ImGui::SliderFloat("Vignette Softness",
                                   &preferences.hdrSettings.vignetteSoftness,
                                   0.05f, 1.0f, "%.2f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Width of the falloff. Higher = smoother blend.");
          }

          if (ImGui::Button("Reset Color Grading")) {
            preferences.hdrSettings.contrast = 1.0f;
            preferences.hdrSettings.saturation = 1.0f;
            preferences.hdrSettings.enableVignette = false;
            preferences.hdrSettings.vignetteIntensity = 0.35f;
            preferences.hdrSettings.vignetteRadius = 0.55f;
            preferences.hdrSettings.vignetteSoftness = 0.45f;
            settingsChanged = true;
          }
        }

        ImGui::Spacing();
        DrawSectionHeader("Screen Space Ambient Occlusion");

        if (ImGui::Checkbox("Enable SSAO", &preferences.ssaoSettings.enabled)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Screen-Space Ambient Occlusion darkens creases and corners "
            "for more realistic lighting. Requires HDR to be enabled.");

        if (preferences.ssaoSettings.enabled) {
          const char *ssaoSampleCounts[] = {"16 (Fast)", "32 (Balanced)",
                                            "64 (Quality)"};
          int ssaoKernelIndex = 2;
          if (preferences.ssaoSettings.kernelSize <= 16)
            ssaoKernelIndex = 0;
          else if (preferences.ssaoSettings.kernelSize <= 32)
            ssaoKernelIndex = 1;
          if (ImGui::Combo("SSAO Samples", &ssaoKernelIndex, ssaoSampleCounts,
                           IM_ARRAYSIZE(ssaoSampleCounts))) {
            const int kernelSizes[] = {16, 32, 64};
            preferences.ssaoSettings.kernelSize = kernelSizes[ssaoKernelIndex];
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Number of samples per pixel. More samples = better quality "
              "but slower");

          if (ImGui::SliderFloat("SSAO Radius",
                                 &preferences.ssaoSettings.radius, 0.1f, 2.0f,
                                 "%.2f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Radius of the hemisphere sample kernel in view space");

          if (ImGui::SliderFloat("SSAO Bias", &preferences.ssaoSettings.bias,
                                 0.001f, 0.1f, "%.3f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Depth bias to prevent self-occlusion artifacts");

          if (ImGui::SliderFloat("SSAO Power", &preferences.ssaoSettings.power,
                                 0.5f, 5.0f, "%.1f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Power curve to control occlusion intensity. Higher "
                         "values = stronger darkening");
        }

        // ---- Eye-Dome Lighting ----
        ImGui::Spacing();
        DrawSectionHeader("Eye-Dome Lighting");

        if (ImGui::Checkbox("Enable EDL", &preferences.edlSettings.enabled)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Eye-Dome Lighting adds depth cues to point clouds by darkening "
            "depth discontinuities."
            "rendering. "
            "Requires HDR to be enabled.");

        if (preferences.edlSettings.enabled) {
          if (ImGui::SliderFloat("EDL Strength",
                                 &preferences.edlSettings.strength, 0.1f, 5.0f,
                                 "%.2f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Controls how strongly depth edges are darkened. "
                         "1.0 = default Potree strength.");

          if (ImGui::SliderFloat("EDL Radius", &preferences.edlSettings.radius,
                                 0.5f, 5.0f, "%.1f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Neighbourhood sampling radius in pixels. "
                         "Larger values detect wider depth transitions.");
        }

        // ---- High-Quality Shading (point averaging / anti-aliasing) ----
        ImGui::Spacing();
        DrawSectionHeader("High-Quality Shading");

        if (ImGui::Checkbox("Enable HQS",
                            &preferences.pointCloudQuality.highQualityShading)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Schütz high-quality shading: averages every point within a small "
            "depth window of the nearest surface at each pixel, instead of "
            "letting a single nearest point win. Removes aliasing and the "
            "shimmer/flicker you see on dense clouds while moving. Costs a second "
            "geometry pass (~2x the point-cloud render time).");

        if (preferences.pointCloudQuality.highQualityShading) {
          float pct = preferences.pointCloudQuality.hqsDepthThreshold * 100.0f;
          if (ImGui::SliderFloat("Depth Blend Window", &pct, 0.1f, 5.0f,
                                 "%.2f %%")) {
            preferences.pointCloudQuality.hqsDepthThreshold = pct * 0.01f;
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "How close (in relative depth) a point must be to the nearest "
              "surface to be blended in. 1.00% matches the original paper. "
              "Larger = smoother but can blend across thin gaps; smaller = "
              "sharper but may reintroduce flicker.");
        }

        // ---- Point Splatting (close-up / sparse density) ----
        ImGui::Spacing();
        DrawSectionHeader("Point Splatting");

        if (ImGui::Checkbox("Enable Splatting",
                            &preferences.pointSplatSettings.enabled)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Widens each point into a small round splat sized from its on-screen "
            "spacing, filling the gaps that appear when you view the cloud close "
            "up or in a sparse area. Distant / dense views collapse back to "
            "single pixels, so the cost is paid only where it's needed.");

        if (preferences.pointSplatSettings.enabled) {
          if (ImGui::SliderInt("Max Splat Radius",
                               &preferences.pointSplatSettings.maxRadius, 1, 8,
                               "%d px")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Upper limit on the splat radius in pixels. Higher = "
                         "fills larger gaps when extremely close, but costs more "
                         "atomic writes. 3-4 is a good balance.");
        }

        // ---- LAS/LAZ progressive loading ----
        ImGui::Spacing();
        DrawSectionHeader("Point Cloud Loading (LAS/LAZ)");

        if (ImGui::Checkbox("Morton Resort (two-phase)",
                            &preferences.pointCloudMortonResort)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "On (default): LAS/LAZ files appear instantly in file order, then a "
            "background per-file Morton sort is swapped in for full render speed "
            "(tight per-batch culling regardless of the file's ordering). Off: "
            "keep the file's original order as-is (literal Schütz loader) – "
            "faster to load with lower RAM, but render speed depends on the file "
            "already being spatially ordered. Applies to the next file loaded.");

        ImGui::Spacing();
        DrawSectionHeader("Shadow Quality");

        const char *pcfKernelSizes[] = {"3x3 (Fast)", "5x5 (Balanced)",
                                        "7x7 (Quality)", "9x9 (High Quality)"};
        int kernelIndex = (preferences.shadowSettings.pcfKernelSize - 3) / 2;
        kernelIndex = glm::clamp(kernelIndex, 0, 3);
        if (ImGui::Combo("PCF Kernel Size", &kernelIndex, pcfKernelSizes,
                         IM_ARRAYSIZE(pcfKernelSizes))) {
          preferences.shadowSettings.pcfKernelSize = 3 + (kernelIndex * 2);
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Size of the shadow filtering kernel. Larger = softer "
                       "shadows but slower");

        if (ImGui::Checkbox("Enable PCSS",
                            &preferences.shadowSettings.enablePCSS)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Percentage Closer Soft Shadows - realistic variable "
                       "shadow softness");

        if (preferences.shadowSettings.enablePCSS) {
          if (ImGui::SliderFloat("Light Size",
                                 &preferences.shadowSettings.lightSize, 0.01f,
                                 1.0f, "%.3f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Size of the light source for PCSS calculations");
        }

        if (ImGui::SliderFloat("Shadow Softness",
                               &preferences.shadowSettings.shadowSoftness, 0.1f,
                               30.0f, "%.2f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Adjusts the softness of shadow edges (affects both "
                       "standard PCF and PCSS)");

        ImGui::Spacing();
        DrawSectionHeader("Indirect Lighting (Global Illumination)");

        if (ImGui::Checkbox(
                "Enable Indirect Lighting",
                &preferences.shadowSettings.enableIndirectLighting)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Add real-time diffuse global illumination (DDGI light probes) to "
            "shadow mapping mode. Direct lighting stays shadow-mapped; this only "
            "adds the indirect bounce. Shares the same probe volume and tuning "
            "as Radiance mode.");

        if (preferences.shadowSettings.enableIndirectLighting) {
          drawDDGISettings();
        }

        ImGui::Spacing();
        DrawSectionHeader("Material Enhancement");

        if (ImGui::Checkbox("Enable PBR Materials",
                            &preferences.materialSettings.enablePBR)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Physically Based Rendering for realistic materials");

      } else if (preferences.lightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
        ImGui::Spacing();
        DrawSectionHeader("VCT Components");

        if (ImGui::Checkbox("Indirect Diffuse",
                            &preferences.vctSettings.indirectDiffuseLight)) {
          vctSettings.indirectDiffuseLight =
              preferences.vctSettings.indirectDiffuseLight;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Color bleeding and bounce lighting");

        if (ImGui::Checkbox("Indirect Specular",
                            &preferences.vctSettings.indirectSpecularLight)) {
          vctSettings.indirectSpecularLight =
              preferences.vctSettings.indirectSpecularLight;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Glossy reflections from environment");

        if (ImGui::Checkbox("Direct Lighting",
                            &preferences.vctSettings.directLight)) {
          vctSettings.directLight = preferences.vctSettings.directLight;
          settingsChanged = true;
        }

        if (ImGui::Checkbox("Soft Shadows", &preferences.vctSettings.shadows)) {
          vctSettings.shadows = preferences.vctSettings.shadows;
          settingsChanged = true;
        }

        DrawSectionHeader("VCT Quality");

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.3f, 0.8f, 0.4f));
        if (ImGui::Button("Low", ImVec2(100, 0))) {
          preferences.vctSettings.diffuseConeCount = 1;
          preferences.vctSettings.shadowSampleCount = 5;
          preferences.vctSettings.shadowStepMultiplier = 0.3f;
          preferences.vctSettings.tracingMaxDistance = 1.0f;
          vctSettings = preferences.vctSettings;
          settingsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Medium", ImVec2(100, 0))) {
          preferences.vctSettings.diffuseConeCount = 5;
          preferences.vctSettings.shadowSampleCount = 8;
          preferences.vctSettings.shadowStepMultiplier = 0.2f;
          preferences.vctSettings.tracingMaxDistance = 1.5f;
          vctSettings = preferences.vctSettings;
          settingsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("High", ImVec2(100, 0))) {
          preferences.vctSettings.diffuseConeCount = 6;
          preferences.vctSettings.shadowSampleCount = 15;
          preferences.vctSettings.shadowStepMultiplier = 0.1f;
          preferences.vctSettings.tracingMaxDistance = 2.0f;
          vctSettings = preferences.vctSettings;
          settingsChanged = true;
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();

        const char *coneOptions[] = {"1 (Fast)", "5 (Balanced)", "6 (Quality)"};
        int coneIndex = preferences.vctSettings.diffuseConeCount <= 1   ? 0
                        : preferences.vctSettings.diffuseConeCount <= 5 ? 1
                                                                        : 2;
        if (ImGui::Combo("Cone Count", &coneIndex, coneOptions,
                         IM_ARRAYSIZE(coneOptions))) {
          switch (coneIndex) {
          case 0:
            preferences.vctSettings.diffuseConeCount = 1;
            break;
          case 1:
            preferences.vctSettings.diffuseConeCount = 5;
            break;
          case 2:
            preferences.vctSettings.diffuseConeCount = 6;
            break;
          }
          vctSettings.diffuseConeCount =
              preferences.vctSettings.diffuseConeCount;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Controls the number of cones used for indirect diffuse "
            "lighting.\n6 cones with 60° aperture provides optimal hemisphere "
            "coverage.\nMore cones = better quality but slower performance");

        if (ImGui::SliderFloat("Trace Distance",
                               &preferences.vctSettings.tracingMaxDistance,
                               0.5f, 2.5f, "%.1f units")) {
          vctSettings.tracingMaxDistance =
              preferences.vctSettings.tracingMaxDistance;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Maximum distance for tracing cones (in grid units).\nLarger "
            "values capture more distant lighting but reduce performance");

        int shadowSamples = preferences.vctSettings.shadowSampleCount;
        if (ImGui::SliderInt("Shadow Samples", &shadowSamples, 5, 20)) {
          preferences.vctSettings.shadowSampleCount = shadowSamples;
          vctSettings.shadowSampleCount = shadowSamples;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Number of samples taken when tracing shadow cones.\nMore samples "
            "= smoother shadows but slower performance");

        if (ImGui::SliderFloat("Shadow Step Multiplier",
                               &preferences.vctSettings.shadowStepMultiplier,
                               0.05f, 0.5f, "%.3f")) {
          vctSettings.shadowStepMultiplier =
              preferences.vctSettings.shadowStepMultiplier;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Controls how fast shadow cone tracing advances.\nLarger values "
            "are faster but may miss details");

        DrawSectionHeader("Grid Configuration");

        float gridSize = voxelizer->getVoxelGridSize();
        if (ImGui::SliderFloat("Grid Dimensions", &gridSize, 1.0f, 50.0f,
                               "%.1f")) {
          voxelizer->setVoxelGridSize(gridSize);
        }
        ImGui::SameLine();
        DrawHelpMarker("World space area coverage for voxelization (larger = "
                       "more world captured, more voxels)");

        float voxelSize = preferences.vctSettings.voxelSize;
        if (ImGui::SliderFloat("VCT Voxel Resolution", &voxelSize,
                               1.0f / 256.0f, 1.0f / 32.0f, "%.5f")) {
          preferences.vctSettings.voxelSize = voxelSize;
          vctSettings.voxelSize = voxelSize;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Voxel resolution for cone tracing calculations "
                       "(smaller = higher quality but slower)");

        DrawSectionHeader("VCT Debug");

        ImGui::Checkbox("Show Voxels", &voxelizer->showDebugVisualization);
        ImGui::SameLine();
        DrawHelpMarker("Visualize the voxel grid as colored cubes");

        if (voxelizer->showDebugVisualization) {
          // Mipmap level selector
          int mipLevel = voxelizer->getDebugMipLevel();
          int maxMip = voxelizer->getMaxMipLevels() - 1;
          if (ImGui::SliderInt("Mip Level", &mipLevel, 0, maxMip)) {
            voxelizer->setDebugMipLevel(mipLevel);
          }
          ImGui::SameLine();
          {
            int res =
                voxelizer->getResolution() >> voxelizer->getDebugMipLevel();
            char buf[64];
            snprintf(buf, sizeof(buf), "Effective resolution: %dx%dx%d", res,
                     res, res);
            DrawHelpMarker(buf);
          }

          // Visualization mode combo
          {
            const char *vizModes[] = {"Color", "Luminance", "Alpha",
                                      "Emissive"};
            int currentViz = static_cast<int>(voxelizer->visualizationMode);
            if (ImGui::Combo("Viz Mode", &currentViz, vizModes,
                             IM_ARRAYSIZE(vizModes))) {
              voxelizer->visualizationMode =
                  static_cast<Engine::Voxelizer::VisualizationMode>(currentViz);
            }
          }

          ImGui::Separator();

          if (ImGui::SliderFloat("Voxel Size", &voxelizer->debugVoxelSize,
                                 0.001f, 0.1f, "%.4f")) {
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Visual size of debug voxel cubes (scaled by 2^mipLevel)");

          if (ImGui::SliderFloat("Opacity", &voxelizer->voxelOpacity, 0.0f,
                                 1.0f, "%.2f")) {
          }

          if (ImGui::SliderFloat("Color Intensity",
                                 &voxelizer->voxelColorIntensity, 0.0f, 5.0f,
                                 "%.1f")) {
          }

          ImGui::Checkbox("Wireframe", &voxelizer->debugWireframe);
          ImGui::SameLine();
          DrawHelpMarker("Render voxel cubes as wireframe outlines");
        }
      } else if (preferences.lightingMode == GUI::LIGHTING_RADIANCE) {
        ImGui::Spacing();
        DrawSectionHeader("Raytracing Settings");

        if (ImGui::Checkbox("Enable Raytracing",
                            &preferences.radianceSettings.enableRaytracing)) {
          ::radianceSettings.enableRaytracing =
              preferences.radianceSettings.enableRaytracing;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Enable proper raytracing from camera through pixels");

        DrawSectionHeader("Performance");

        if (ImGui::SliderInt("Max Bounces",
                             &preferences.radianceSettings.maxBounces, 1, 4)) {
          ::radianceSettings.maxBounces =
              preferences.radianceSettings.maxBounces;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Maximum number of ray bounces (1=direct lighting only, "
                       "2+=indirect lighting)");

        if (ImGui::SliderInt("Samples/Pixel",
                             &preferences.radianceSettings.samplesPerPixel, 1,
                             100)) {
          ::radianceSettings.samplesPerPixel =
              preferences.radianceSettings.samplesPerPixel;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Number of rays cast per pixel (higher = better "
                       "quality, lower performance)");

        if (ImGui::SliderFloat("Ray Max Distance",
                               &preferences.radianceSettings.rayMaxDistance,
                               10.0f, 100.0f, "%.1f")) {
          ::radianceSettings.rayMaxDistance =
              preferences.radianceSettings.rayMaxDistance;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Maximum distance for ray casting");

        DrawSectionHeader("Lighting Features");

        if (ImGui::Checkbox(
                "Indirect Lighting",
                &preferences.radianceSettings.enableIndirectLighting)) {
          ::radianceSettings.enableIndirectLighting =
              preferences.radianceSettings.enableIndirectLighting;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Enable indirect lighting through ray bounces");

        if (ImGui::Checkbox(
                "Emissive Lighting",
                &preferences.radianceSettings.enableEmissiveLighting)) {
          ::radianceSettings.enableEmissiveLighting =
              preferences.radianceSettings.enableEmissiveLighting;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Use emissive objects as light sources");

        DrawSectionHeader("Intensity Controls");

        if (ImGui::SliderFloat("Indirect Intensity",
                               &preferences.radianceSettings.indirectIntensity,
                               0.0f, 1.0f, "%.2f")) {
          ::radianceSettings.indirectIntensity =
              preferences.radianceSettings.indirectIntensity;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Intensity of indirect lighting contribution");

        if (ImGui::SliderFloat("Sky Intensity",
                               &preferences.radianceSettings.skyIntensity, 0.0f,
                               2.0f, "%.2f")) {
          ::radianceSettings.skyIntensity =
              preferences.radianceSettings.skyIntensity;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Brightness of sky lighting when rays miss geometry");

        if (ImGui::SliderFloat("Emissive Intensity",
                               &preferences.radianceSettings.emissiveIntensity,
                               0.0f, 3.0f, "%.2f")) {
          ::radianceSettings.emissiveIntensity =
              preferences.radianceSettings.emissiveIntensity;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Multiplier for emissive object brightness");

        if (ImGui::SliderFloat("Material Roughness",
                               &preferences.radianceSettings.materialRoughness,
                               0.0f, 1.0f, "%.2f")) {
          ::radianceSettings.materialRoughness =
              preferences.radianceSettings.materialRoughness;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Global material roughness (0=mirror, 1=diffuse)");

        DrawSectionHeader("Soft Shadows");

        if (ImGui::SliderInt("Shadow Samples",
                             &preferences.radianceSettings.shadowSamples, 1,
                             16)) {
          ::radianceSettings.shadowSamples =
              preferences.radianceSettings.shadowSamples;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Shadow rays per light for direct lighting. 1 = hard "
                       "shadows; higher = smoother penumbra (more cost).");

        if (ImGui::SliderFloat("Shadow Softness",
                               &preferences.radianceSettings.shadowSoftness,
                               0.0f, 1.0f, "%.2f")) {
          ::radianceSettings.shadowSoftness =
              preferences.radianceSettings.shadowSoftness;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Penumbra size: angular size of the sun and radius of "
                       "point/spot sources. 0 = hard shadows.");

        DrawSectionHeader("Acceleration");

        if (ImGui::Checkbox("Enable BVH",
                            &preferences.radianceSettings.enableBVH)) {
          ::enableBVH = preferences.radianceSettings.enableBVH;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Bounding Volume Hierarchy for faster ray-triangle tests");

        if (ImGui::Checkbox("Two-Level BVH (TLAS/BLAS)",
                            &preferences.radianceSettings.enableTwoLevelBVH)) {
          ::enableTwoLevelBVH = preferences.radianceSettings.enableTwoLevelBVH;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Per-object BLAS (built once, cached) + a small TLAS rebuilt when "
            "objects move. Avoids rebuilding the whole-scene BVH on every "
            "transform. Mutually exclusive with the single-level BVH above.");

        if (ImGui::Checkbox("Debug BVH",
                            &preferences.radianceSettings.showBVHDebug)) {
          ::showBVHDebug = preferences.radianceSettings.showBVHDebug;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Visualize BVH bounding boxes with color-coded depth "
                       "levels:\nLevel 0: Red, Level 1: Orange, Level 2: "
                       "Yellow, Level 3: Green\nLevel 4: Cyan, Level 5: Blue, "
                       "Level 6: Purple, Level 7: Magenta");

        if (preferences.radianceSettings.showBVHDebug) {
          ImGui::Indent();

          if (ImGui::SliderInt("BVH Max Depth",
                               &preferences.radianceSettings.bvhDebugMaxDepth,
                               1, 8)) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Maximum BVH depth levels to display (1=root only, higher=more "
              "detail)\nEach level has a distinct color: "
              "Red→Orange→Yellow→Green→Cyan→Blue→Purple→Magenta");

          const char *renderModeNames[] = {"Depth Tested", "Always On Top",
                                           "Depth Biased"};
          if (ImGui::Combo("Render Mode",
                           &preferences.radianceSettings.bvhDebugRenderMode,
                           renderModeNames, 3)) {
            Engine::BVHDebugRenderer::RenderMode mode =
                static_cast<Engine::BVHDebugRenderer::RenderMode>(
                    preferences.radianceSettings.bvhDebugRenderMode);
            ::bvhDebugRenderer.setRenderMode(mode);
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Depth Tested: lines behind geometry hidden\nAlways On Top: "
              "lines always visible\nDepth Biased: lines slightly in front");

          ImGui::Unindent();
        }

        // Dynamic Diffuse GI (DDGI) Section
        ImGui::Separator();
        DrawSectionHeader("Dynamic Diffuse GI (DDGI)");

        if (ImGui::Checkbox("Enable DDGI",
                            &preferences.radianceSettings.enableDDGI)) {
          ::radianceSettings.enableDDGI =
              preferences.radianceSettings.enableDDGI;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Real-time diffuse global illumination from a grid of "
                       "light probes (irradiance + visibility). Updated every "
                       "frame via the BVH - no precompute.");

        if (preferences.radianceSettings.enableDDGI) {
          drawDDGISettings();
        }
      }

      ImGui::PopID();
  }

    // ===========================
    // CAMERA TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_CAMERA) {
      ImGui::PushID("CameraTab");
      DrawSectionHeader("View Settings");

      if (ImGui::SliderFloat("Field of View", &camera.Zoom, 1.0f, 120.0f,
                             "%.0f°")) {
        preferences.fov = camera.Zoom;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Controls the camera's field of view. Higher values show "
                     "more of the scene");

      if (ImGui::SliderFloat("Near Clip", &preferences.nearPlane, 0.01f, 10.0f,
                             "%.2f")) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Minimum visible distance from camera. Smaller values can "
                     "cause visual artifacts");

      if (ImGui::SliderFloat("Far Clip", &preferences.farPlane, 10.0f, 1000.0f,
                             "%.0f")) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Maximum visible distance from camera. Higher values may "
                     "impact performance");

      DrawSectionHeader("Standard Views");
      ImGui::TextDisabled("Frame the scene from a fixed angle (numpad keys).");
      {
        const float spacing = ImGui::GetStyle().ItemSpacing.x;
        const float btnW =
            (ImGui::GetContentRegionAvail().x - spacing * 2.0f) / 3.0f;
        const ImVec2 sz(btnW, 0.0f);

        if (ImGui::Button("Top", sz))
          applyStandardView(4);
        ImGui::SameLine();
        if (ImGui::Button("Bottom", sz))
          applyStandardView(5);
        ImGui::SameLine();
        if (ImGui::Button("Isometric", sz))
          applyStandardView(6);

        if (ImGui::Button("Front", sz))
          applyStandardView(0);
        ImGui::SameLine();
        if (ImGui::Button("Back", sz))
          applyStandardView(1);
        ImGui::SameLine();
        if (ImGui::Button("Right", sz))
          applyStandardView(2);

        if (ImGui::Button("Left", sz))
          applyStandardView(3);

        ImGui::Spacing();
        if (ImGui::Button("Frame Selected (F)"))
          frameSelectedObject();
        ImGui::SameLine();
        DrawHelpMarker("Fly the camera in to fill the view with the selected "
                       "object, keeping the current viewing angle.");
      }

      DrawSectionHeader("Stereoscopic 3D");

      if (ImGui::SliderFloat("Eye Separation", &preferences.separation, 0.01f,
                             2.0f, "%.2f")) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Adjusts the distance between stereo views. Higher values "
                     "increase 3D effect");

      if (ImGui::Checkbox("Auto Convergence", &preferences.autoConvergence)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Automatically sets convergence based on distance to nearest object");

      if (preferences.autoConvergence) {
        if (ImGui::SliderFloat("Distance Factor",
                               &preferences.convergenceDistanceFactor, 0.1f,
                               2.0f, "%.1fx")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Multiplier for the distance to nearest object used for "
                       "convergence");

        if (ImGui::SliderFloat("Smoothing Speed", &convergenceSmoothingSpeed,
                               0.5f, 20.0f, "%.1f")) {
          preferences.convergenceSmoothingSpeed = convergenceSmoothingSpeed;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Speed of convergence transition. Higher values = faster "
            "transitions, lower values = smoother but slower");

        if (ImGui::Checkbox("Enable Convergence Cap",
                            &preferences.enableConvergenceCap)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Limit auto convergence to a specific range");

        if (preferences.enableConvergenceCap) {
          if (ImGui::SliderFloat("Min Convergence",
                                 &preferences.convergenceCapMin, 0.1f, 20.0f,
                                 "%.1f")) {
            // Ensure min doesn't exceed max
            if (preferences.convergenceCapMin > preferences.convergenceCapMax) {
              preferences.convergenceCapMin = preferences.convergenceCapMax;
            }
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Minimum convergence distance when auto convergence is active");

          if (ImGui::SliderFloat("Max Convergence",
                                 &preferences.convergenceCapMax, 0.1f, 100.0f,
                                 "%.1f")) {
            // Ensure max doesn't go below min
            if (preferences.convergenceCapMax < preferences.convergenceCapMin) {
              preferences.convergenceCapMax = preferences.convergenceCapMin;
            }
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Maximum convergence distance when auto convergence is active");
        }

        ImGui::Text("Current Convergence: %.2f", preferences.convergence);
        ImGui::SameLine();
        DrawHelpMarker(
            "Auto-calculated convergence distance based on nearest object");
      } else {
        if (ImGui::SliderFloat("Convergence", &preferences.convergence, 0.0f,
                               40.0f, "%.1f")) {
          settingsChanged = true;
        }
        // Lock cursor position while adjusting convergence to prevent it from
        // sliding
        cursorManager.setPositionLocked(ImGui::IsItemActive());

        ImGui::SameLine();
        DrawHelpMarker("Sets the focal point distance where left and right "
                       "views converge");
      }

      if (ImGui::Checkbox("Flip Eyes", &preferences.flipEyes)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Swaps the left and right eye views for stereoscopic display");

      // ---- OpenXR / VR section ----
      DrawSectionHeader("OpenXR / VR");

      // Status indicator dot
      {
        bool active = preferences.openxrSettings.enabled && g_xrAvailable;
        ImVec4 dotCol = active
                          ? ImVec4(0.2f, 0.9f, 0.2f, 1.0f)  // green = running
                          : (preferences.openxrSettings.enabled
                               ? ImVec4(0.9f, 0.5f, 0.1f, 1.0f) // orange = error
                               : ImVec4(0.5f, 0.5f, 0.5f, 1.0f)); // grey = off
        ImGui::PushStyleColor(ImGuiCol_Text, dotCol);
        ImGui::Text(active ? "[VR]" : "[--]");
        ImGui::PopStyleColor();
        ImGui::SameLine();
        ImGui::TextDisabled("%s", g_xrStatusMsg.empty()
                                    ? "OpenXR disabled"
                                    : g_xrStatusMsg.c_str());
      }

      if (ImGui::Checkbox("Enable OpenXR (VR Headset)", &preferences.openxrSettings.enabled)) {
        xrSessionEnable(preferences.openxrSettings.enabled);
        if (preferences.openxrSettings.enabled) xrRefreshDiagnostics();
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Renders to a connected VR headset via OpenXR. Requires an OpenXR "
          "runtime (SteamVR, Meta Quest Link, Windows Mixed Reality, etc.) and "
          "a compatible HMD. The normal desktop window continues to render as a "
          "mirror when 'Mirror to Window' is enabled.");

      // ---- Troubleshooting + runtime picker -------------------------------
      // Shown when OpenXR was requested but no session is running (i.e. startup
      // failed). Explains *why* in plain language and lets the user switch to
      // any runtime installed on this PC. Switching is per-process (via
      // XR_RUNTIME_JSON) so it needs no admin rights and changes nothing
      // system-wide.
      if (preferences.openxrSettings.enabled && !g_xrAvailable) {
        const Engine::XRDiagnostics &diag = g_xrDiagnostics;

        ImGui::Spacing();
        ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.45f, 0.2f, 1.0f));
        ImGui::TextWrapped("OpenXR could not start.");
        ImGui::PopStyleColor();

        // Why it failed.
        if (diag.activeServiceBased) {
          ImGui::TextWrapped(
              "The selected OpenXR runtime is \"%s\". It runs as a background "
              "service that is launched by its own app, and that service is not "
              "running right now - so no program (including this one) can use it.",
              diag.activeRuntimeName.c_str());
        } else if (!diag.activeRuntimeName.empty()) {
          ImGui::TextWrapped("Active runtime: %s%s.",
                             diag.activeRuntimeName.c_str(),
                             diag.activeRuntimeSource == "XR_RUNTIME_JSON"
                                 ? " (selected here)" : " (system default)");
          ImGui::TextWrapped("%s", g_xrStatusMsg.c_str());
        } else {
          ImGui::TextWrapped(
              "No OpenXR runtime is selected on this PC. Install or enable one "
              "(SteamVR, Oculus / Meta, or Windows Mixed Reality).");
        }

        // How to fix it.
        ImGui::Spacing();
        ImGui::TextDisabled("How to fix");
        ImGui::BulletText("Pick an installed runtime below and click \"Use\".");
        ImGui::Bullet();
        ImGui::TextWrapped("Make sure your headset is plugged in and its software "
                           "(SteamVR, Oculus app, Mixed Reality Portal, ...) is "
                           "running.");
        if (diag.activeServiceBased) {
          ImGui::Bullet();
          ImGui::TextWrapped("Or launch the program that provides \"%s\" first, "
                             "then enable OpenXR again.",
                             diag.activeRuntimeName.c_str());
        }

        // Runtime picker. The list is stable (it does not change as you switch);
        // each entry is tagged [in use] / [system default] as appropriate.
        // Clicking a button must NOT mutate g_xrDiagnostics mid-draw (that would
        // invalidate the list we're iterating), so the chosen action is recorded
        // here and applied after the list has been drawn.
        enum class XrAction { None, Refresh, Use };
        XrAction    action = XrAction::None;
        std::string actionPath; // runtime to switch to

        ImGui::Spacing();
        ImGui::TextDisabled("OpenXR runtimes found on this PC");
        ImGui::SameLine();
        if (ImGui::SmallButton("Refresh")) action = XrAction::Refresh;

        ImGui::BeginChild("xr_runtime_list",
                          ImVec2(0.0f, ImGui::GetTextLineHeightWithSpacing() * 5.0f),
                          true);

        if (diag.runtimes.empty()) {
          ImGui::TextDisabled("(no OpenXR runtimes detected on this PC)");
        }
        const ImVec4 kServiceCol(0.95f, 0.6f, 0.2f, 1.0f);
        for (size_t i = 0; i < diag.runtimes.size(); ++i) {
          const Engine::XRRuntimeInfo &rt = diag.runtimes[i];
          ImGui::PushID(static_cast<int>(i));
          ImGui::BeginGroup();

          if (ImGui::Button("Use")) { action = XrAction::Use; actionPath = rt.manifestPath; }
          ImGui::SameLine();

          if (rt.serviceBased) ImGui::PushStyleColor(ImGuiCol_Text, kServiceCol);
          ImGui::TextUnformatted(rt.name.c_str());
          if (rt.serviceBased) ImGui::PopStyleColor();

          if (rt.isActive) { ImGui::SameLine(); ImGui::TextDisabled("[in use]"); }
          if (rt.isSystemDefault) { ImGui::SameLine(); ImGui::TextDisabled("[system default]"); }
          if (rt.serviceBased) {
            ImGui::SameLine();
            ImGui::TextColored(kServiceCol, "- needs its own app running");
          }

          ImGui::EndGroup();
          if (ImGui::IsItemHovered()) ImGui::SetTooltip("%s", rt.manifestPath.c_str());
          ImGui::PopID();
        }
        ImGui::EndChild();

        ImGui::TextDisabled("Tip: if picking a runtime has no effect, run "
                            "StereoVista without administrator rights, or set the "
                            "runtime as default in its own settings.");

        // Apply the deferred action now that we're done reading g_xrDiagnostics.
        if (action == XrAction::Refresh) xrRefreshDiagnostics();
        else if (action == XrAction::Use) xrUseRuntime(actionPath);
      }

      if (preferences.openxrSettings.enabled && g_xrAvailable) {
        if (!g_xrRuntimeName.empty()) {
          ImGui::TextDisabled("Runtime: %s", g_xrRuntimeName.c_str());
        }

        if (ImGui::Checkbox("Mirror to Window", &preferences.openxrSettings.mirrorToWindow)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Blits the left-eye VR image to the desktop window. "
                       "Useful for demos; disable for maximum VR performance.");

        if (ImGui::SliderFloat("World Scale", &preferences.openxrSettings.worldScale,
                               0.01f, 10.0f, "%.3f m/unit")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Metres per scene unit. Set to 1.0 when your scene is in metres, "
            "0.01 when in centimetres, etc. Affects how head-tracking "
            "translates into scene movement.");

        if (ImGui::Checkbox("Use Scene Near/Far Planes", &preferences.openxrSettings.useScenePlanes)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("When enabled, the VR projection uses the same near/far "
                       "planes as the desktop view. Disable to set custom values "
                       "for comfort.");

        if (!preferences.openxrSettings.useScenePlanes) {
          if (ImGui::SliderFloat("VR Near Plane##xr", &preferences.openxrSettings.nearPlane,
                                 0.001f, 1.0f, "%.3f")) {
            settingsChanged = true;
          }
          if (ImGui::SliderFloat("VR Far Plane##xr", &preferences.openxrSettings.farPlane,
                                 1.0f, 1000.0f, "%.0f")) {
            settingsChanged = true;
          }
        }

        ImGui::Spacing();
        ImGui::TextDisabled("Camera movement (WASD / SpaceMouse) works in VR.");
        ImGui::TextDisabled("Head tracking is composited on top of camera orbit.");
      }

      DrawSectionHeader("Movement");

      if (ImGui::SliderFloat("Mouse Sensitivity", &camera.MouseSensitivity,
                             0.01f, 0.08f, "%.3f")) {
        preferences.mouseSensitivity = camera.MouseSensitivity;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Adjusts how quickly the camera rotates in response to "
                     "mouse movement");

      if (ImGui::SliderFloat("Smoothing", &mouseSmoothingFactor, 0.1f, 1.0f,
                             "%.1f")) {
        preferences.mouseSmoothingFactor = mouseSmoothingFactor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Controls smoothness of mouse movement. Lower values = "
                     "smoother, higher values = more responsive");

      if (ImGui::SliderFloat("Speed Multiplier", &camera.speedFactor, 0.1f,
                             5.0f, "%.1fx")) {
        preferences.cameraSpeedFactor = camera.speedFactor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Multiplies base movement speed. Useful for navigating "
                     "larger scenes");

      if (ImGui::Checkbox("Zoom to Cursor", &camera.zoomToCursor)) {
        preferences.zoomToCursor = camera.zoomToCursor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("When enabled, scrolling zooms toward the cursor position "
                     "(including background areas)");

      DrawSectionHeader("Orbit Behavior");

      bool standardOrbit = !camera.orbitAroundCursor && !orbitFollowsCursor;
      bool orbitAroundCursorOption = camera.orbitAroundCursor;
      bool orbitFollowsCursorOption = orbitFollowsCursor;

      if (ImGui::RadioButton("Standard Orbit", standardOrbit)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = false;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Orbits around the viewport center at cursor depth");

      if (ImGui::RadioButton("Orbit Around Cursor", orbitAroundCursorOption)) {
        camera.orbitAroundCursor = true;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = true;
        preferences.orbitFollowsCursor = false;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Orbits around the 3D position of the cursor without "
                     "centering the view");

      if (ImGui::RadioButton("Orbit Follows Cursor (Center)",
                             orbitFollowsCursorOption)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = true;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = true;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Centers the view on cursor position before orbiting");

      DrawSectionHeader("Smooth Scrolling");

      if (ImGui::Checkbox("Enable Smooth Scrolling",
                          &camera.useSmoothScrolling)) {
        preferences.useSmoothScrolling = camera.useSmoothScrolling;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Enable physics-based smooth scrolling");

      if (camera.useSmoothScrolling) {
        ImGui::Indent();
        if (ImGui::SliderFloat("Momentum", &camera.scrollMomentum, 0.0f, 1.0f,
                               "%.2f")) {
          preferences.scrollMomentum = camera.scrollMomentum;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Controls how much scrolling 'carries' (higher = more momentum)");

        if (ImGui::SliderFloat("Max Speed", &camera.maxScrollVelocity, 0.5f,
                               10.0f, "%.1f")) {
          preferences.maxScrollVelocity = camera.maxScrollVelocity;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Maximum scroll speed");

        if (ImGui::SliderFloat("Deceleration", &camera.scrollDeceleration, 1.0f,
                               20.0f, "%.1f")) {
          preferences.scrollDeceleration = camera.scrollDeceleration;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("How quickly scrolling slows down");
        ImGui::Unindent();
      }

      ImGui::PopID();
  }

    // ===========================
    // ENVIRONMENT TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_ENVIRONMENT) {
      ImGui::PushID("EnvironmentTab");
      DrawSectionHeader("Skybox");

      const char *skyboxTypes[] = {"Cubemap", "HDR", "Solid Color", "Gradient"};
      int currentType = static_cast<int>(skyboxConfig.type);
      if (ImGui::Combo("Type", &currentType, skyboxTypes,
                       IM_ARRAYSIZE(skyboxTypes))) {
        skyboxConfig.type = static_cast<GUI::SkyboxType>(currentType);
        updateSkybox();
        preferences.skyboxType = static_cast<int>(skyboxConfig.type);
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Change the type of skybox used in the scene");

      if (skyboxConfig.type == GUI::SKYBOX_CUBEMAP) {
        std::vector<const char *> presetNames;
        for (const auto &preset : cubemapPresets) {
          presetNames.push_back(preset.name.c_str());
        }

        if (ImGui::Combo("Theme", &skyboxConfig.selectedCubemap,
                         presetNames.data(),
                         static_cast<int>(presetNames.size()))) {
          updateSkybox();
          preferences.selectedCubemap = skyboxConfig.selectedCubemap;
          savePreferences();
        }

        if (skyboxConfig.selectedCubemap >= 0 &&
            skyboxConfig.selectedCubemap < cubemapPresets.size()) {
          ImGui::SameLine();
          DrawHelpMarker(
              cubemapPresets[skyboxConfig.selectedCubemap].description.c_str());
        }

        if (ImGui::Button("Browse Custom...", ImVec2(-1, 0))) {
          auto selection =
              pfd::select_folder("Select skybox directory").result();
          if (!selection.empty()) {
            std::string path = selection + "/";
            std::string dirName =
                std::filesystem::path(selection).filename().string();

            GUI::CubemapPreset newPreset;
            newPreset.name = "Custom: " + dirName;
            newPreset.path = path;
            newPreset.description = "Custom skybox";
            cubemapPresets.push_back(newPreset);

            skyboxConfig.selectedCubemap =
                static_cast<int>(cubemapPresets.size()) - 1;
            updateSkybox();
            preferences.selectedCubemap = skyboxConfig.selectedCubemap;
            savePreferences();
          }
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Select a directory containing skybox textures (right.jpg, "
            "left.jpg, etc. OR posx.jpg, negx.jpg, etc.)");
      } else if (skyboxConfig.type == GUI::SKYBOX_HDR) {
        ImGui::Text("HDR File:");
        ImGui::SameLine();

        // Display current HDR path or "None selected"
        std::string displayPath =
            skyboxConfig.hdrPath.empty()
                ? "None selected"
                : skyboxConfig.hdrPath.substr(
                      skyboxConfig.hdrPath.find_last_of("/\\") + 1);
        ImGui::TextDisabled("%s", displayPath.c_str());

        if (ImGui::Button("Browse HDR File...")) {
          auto selection =
              pfd::open_file("Select HDR environment map", ".",
                             {"HDR Images", "*.hdr", "All Files", "*"})
                  .result();
          if (!selection.empty()) {
            skyboxConfig.hdrPath = selection[0];
            updateSkybox();
            preferences.skyboxHdrPath = skyboxConfig.hdrPath;
            savePreferences();
          }
        }
        ImGui::SameLine();
        DrawHelpMarker("Select an HDR (.hdr) equirectangular environment map");

        // Exposure control for HDR skyboxes
        if (ImGui::SliderFloat("HDR Exposure", &preferences.skyboxExposure,
                               0.01f, 2.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic)) {
          savePreferences();
        }
        ImGui::SameLine();
        DrawHelpMarker("Controls the brightness of the HDR skybox. Most HDR "
                       "environment maps need values between 0.1-0.5");
      } else if (skyboxConfig.type == GUI::SKYBOX_SOLID_COLOR) {
        if (ImGui::ColorEdit3("Color",
                              glm::value_ptr(skyboxConfig.solidColor))) {
          updateSkybox();
          preferences.skyboxSolidColor = skyboxConfig.solidColor;
          savePreferences();
        }
        ImGui::SameLine();
        DrawHelpMarker("Set a single color for the entire skybox");
      } else if (skyboxConfig.type == GUI::SKYBOX_GRADIENT) {
        bool changed = false;
        changed |= ImGui::ColorEdit3(
            "Top", glm::value_ptr(skyboxConfig.gradientTopColor));
        ImGui::SameLine();
        DrawHelpMarker("Color of the top portion of the skybox");

        changed |= ImGui::ColorEdit3(
            "Bottom", glm::value_ptr(skyboxConfig.gradientBottomColor));
        ImGui::SameLine();
        DrawHelpMarker("Color of the bottom portion of the skybox");

        if (changed) {
          updateSkybox();
          preferences.skyboxGradientTop = skyboxConfig.gradientTopColor;
          preferences.skyboxGradientBottom = skyboxConfig.gradientBottomColor;
          savePreferences();
        }
      }

      if (ImGui::SliderFloat("Ambient Light", &ambientStrengthFromSkybox, 0.0f,
                             1.0f, "%.2f")) {
        preferences.ambientStrengthFromSkybox = ambientStrengthFromSkybox;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Controls how much the skybox illuminates the scene. "
                     "Higher values create brighter ambient lighting");

      DrawSectionHeader("Sun Light");

      settingsChanged |= ImGui::Checkbox("Enable Sun", &sun.enabled);
      ImGui::SameLine();
      DrawHelpMarker("Toggles sun lighting on/off");

      settingsChanged |=
          ImGui::ColorEdit3("Sun Color", glm::value_ptr(sun.color));
      ImGui::SameLine();
      DrawHelpMarker("Sets the color of sunlight in the scene");

      settingsChanged |= ImGui::SliderFloat("Sun Intensity", &sun.intensity,
                                            0.0f, 1.0f, "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Controls the brightness of sunlight");

      // Edit the direction vector directly and re-normalize. (The previous
      // Euler-angle control used a static cache that never reflected the actual
      // sun.direction, so it desynced after load/undo and snapped on first use.)
      glm::vec3 sunDir = sun.direction;
      if (ImGui::DragFloat3("Sun Direction", glm::value_ptr(sunDir), 0.01f,
                            -1.0f, 1.0f, "%.2f")) {
        if (glm::length(sunDir) > 1e-4f) {
          sun.direction = glm::normalize(sunDir);
          settingsChanged = true;
        }
      }
      ImGui::SameLine();
      DrawHelpMarker("Direction the sunlight travels (points away from the "
                     "sun). Affects shadows and lighting.");

      ImGui::PopID();
  }

    // ===========================
    // DISPLAY TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_DISPLAY) {
      ImGui::PushID("DisplayTab");
      DrawSectionHeader("Interface");

      // Color theme picker
      {
        int themeIndex = preferences.guiTheme;
        if (themeIndex < 0 || themeIndex >= GetGuiThemeCount())
          themeIndex = 0;

        if (ImGui::BeginCombo("Theme", GetGuiThemeName(themeIndex))) {
          for (int i = 0; i < GetGuiThemeCount(); ++i) {
            bool selected = (i == themeIndex);
            if (ImGui::Selectable(GetGuiThemeName(i), selected)) {
              ApplyGuiTheme(i, 1.0f);
              themeIndex = i;
              preferences.guiTheme = i;
              isDarkTheme = IsGuiThemeDark(i);
              preferences.isDarkTheme = isDarkTheme;
              settingsChanged = true;
            }
            if (selected)
              ImGui::SetItemDefaultFocus();
          }
          ImGui::EndCombo();
        }
        ImGui::SameLine();
        DrawHelpMarker("Color theme for the entire interface. \"Schneider "
                       "Digital\" is the white & golden-yellow brand theme; the "
                       "rest are modern, easy-on-the-eyes light and dark palettes.");

        // Live swatch preview of the selected theme's palette
        ImVec4 swatches[4];
        int swatchCount = GetGuiThemeSwatches(themeIndex, swatches, 4);
        for (int i = 0; i < swatchCount; ++i) {
          std::string id = "##themesw" + std::to_string(i);
          ImGui::ColorButton(id.c_str(), swatches[i],
                             ImGuiColorEditFlags_NoTooltip |
                                 ImGuiColorEditFlags_NoPicker |
                                 ImGuiColorEditFlags_NoDragDrop,
                             ImVec2(34 * scale, 16 * scale));
          if (i < swatchCount - 1)
            ImGui::SameLine();
        }
      }

      if (ImGui::Checkbox("Show Performance Overlay", &showFPS)) {
        preferences.showFPS = showFPS;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Shows/hides the performance overlay (FPS, frame-time "
                     "graph and scene statistics) in the bottom-right corner");

      if (ImGui::Checkbox("VSync", &preferences.vsyncEnabled)) {
        glfwSwapInterval(preferences.vsyncEnabled ? 1 : 0);
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Synchronizes rendering with the display refresh rate. "
                     "Eliminates tearing but caps the frame rate.");

      if (ImGui::Checkbox("Show GUI", &showGui)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Toggle the entire GUI interface on/off (also 'G' key)");

      ImGui::Spacing();
      DrawSectionHeader("GUI Scale");

      float userFactor = g_GuiScale.userScaleFactor;
      if (ImGui::SliderFloat("Scale Factor", &userFactor,
                             GuiScaleSettings::MIN_USER_FACTOR,
                             GuiScaleSettings::MAX_USER_FACTOR, "%.2fx")) {
        g_GuiScale.userScaleFactor = userFactor;
        if (g_GuiScale.lastWindowWidth > 0 && g_GuiScale.lastWindowHeight > 0) {
          float baseScale = CalculateGuiScale(g_GuiScale.lastWindowWidth,
                                              g_GuiScale.lastWindowHeight);
          float newScale =
              std::max(0.5f, std::min(2.0f, baseScale * userFactor));
          g_GuiScale.currentScale = newScale;
        }
        g_GuiScale.needsRescale = true;
        g_GuiScale.needsFontRebuild = true;
        preferences.guiScaleFactor = userFactor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Multiplier applied on top of the automatic window-size "
                     "scaling. 1.0x = default, 0.5x = smaller, 2.0x = larger");

      if (ImGui::Button("Reset Scale")) {
        g_GuiScale.userScaleFactor = 1.0f;
        if (g_GuiScale.lastWindowWidth > 0 && g_GuiScale.lastWindowHeight > 0) {
          float baseScale = CalculateGuiScale(g_GuiScale.lastWindowWidth,
                                              g_GuiScale.lastWindowHeight);
          g_GuiScale.currentScale = std::max(0.5f, std::min(2.0f, baseScale));
        }
        g_GuiScale.needsRescale = true;
        g_GuiScale.needsFontRebuild = true;
        preferences.guiScaleFactor = 1.0f;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Reset GUI scale factor to default (1.0x)");

      DrawSectionHeader("Radar Overlay");

      if (ImGui::Checkbox("Show Radar", &preferences.radarEnabled)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Show a radar overlay with the camera frustum");

      if (preferences.radarEnabled) {
        ImGui::Indent();
        if (ImGui::SliderFloat2("Position",
                                glm::value_ptr(preferences.radarPos), -1.0f,
                                1.0f, "%.2f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Horizontal and vertical position of the radar (-1 to 1)");

        if (ImGui::SliderFloat("Size", &preferences.radarRadius, 0.05f, 0.5f,
                               "%.2f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("On-screen radius of the radar scope");

        if (ImGui::Checkbox("Auto-fit to Convergence",
                            &preferences.radarAutoFit)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Automatically scale the radar so the convergence plane (green "
            "line) always sits inside the scope. Turn off for manual zoom.");

        if (!preferences.radarAutoFit) {
          if (ImGui::SliderFloat("Zoom", &preferences.radarScale, 0.001f, 0.5f,
                                 "%.3f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "How much of the scene fits inside the scope (world units to "
              "radar units). Lower zooms out to show more of the scene.");
        }

        if (ImGui::SliderFloat("Frustum Separation",
                               &preferences.radarFrustumSpread, 0.0f, 0.4f,
                               "%.2f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Exaggerate the gap between the left/right eye frustums so they "
            "are distinguishable at comfortable separations. The convergence "
            "crossing stays accurate. 0 = true-to-life.");

        if (ImGui::Checkbox("Show Scene in Radar",
                            &preferences.radarShowScene)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Show the scene models in the radar view");

        if (preferences.radarShowScene) {
          if (ImGui::SliderFloat("Scene Brightness",
                                 &preferences.radarSceneBrightness, 1.0f, 30.0f,
                                 "%.1f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Exposure boost for the top-down scene in the radar. The radar "
              "skips the normal HDR/bloom tone-map, so crank this up to keep "
              "the floor plan bright and readable (not realistic).");

          if (ImGui::Checkbox("Slice Scene (Top-Down)",
                              &preferences.radarSliceEnabled)) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Cut away geometry above the camera so building interiors are "
              "visible from above instead of just the roof.");

          if (preferences.radarSliceEnabled) {
            if (ImGui::SliderFloat("Slice Height", &preferences.radarSliceOffset,
                                   -2.0f, 10.0f, "%.2f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker(
                "Height of the slice plane above the camera (world units). "
                "Lower values cut closer to the floor.");
          }
        }
        ImGui::Unindent();
      }

      if (ImGui::Checkbox("Show Zero Plane", &preferences.showZeroPlane)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Display the zero plane in the visualization");

      DrawSectionHeader("Startup");

      if (ImGui::Checkbox("Load Scene on Start",
                          &preferences.loadStartupScene)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Automatically load the specified scene file when the "
                     "application starts");

      if (preferences.loadStartupScene) {
        static char scenePathBuffer[1024];
        if (strlen(scenePathBuffer) == 0 &&
            !preferences.startupScenePath.empty()) {
          strncpy_s(scenePathBuffer, preferences.startupScenePath.c_str(),
                    sizeof(scenePathBuffer) - 1);
        }

        ImGui::InputText("Scene Path", scenePathBuffer,
                         sizeof(scenePathBuffer));
        ImGui::SameLine();
        DrawHelpMarker("Path to the .scene file to load on startup");

        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
          auto result =
              pfd::open_file("Select Scene", "", {"Scene Files", "*.scene"});
          if (!result.result().empty()) {
            std::string selectedPath = result.result()[0];
            strncpy_s(scenePathBuffer, selectedPath.c_str(),
                      sizeof(scenePathBuffer) - 1);
            preferences.startupScenePath = selectedPath;
            settingsChanged = true;
          }
        }

        if (strcmp(scenePathBuffer, preferences.startupScenePath.c_str()) !=
            0) {
          preferences.startupScenePath = std::string(scenePathBuffer);
          settingsChanged = true;
        }
      }

      ImGui::Spacing();
      DrawSectionHeader("Scene Loading");

      const char *sceneLoadingBehaviors[] = {"Always Ask",
                                             "Always Replace Existing",
                                             "Always Merge (Keep Existing)"};
      int currentBehavior = static_cast<int>(preferences.sceneLoadingBehavior);
      if (ImGui::Combo("When Loading Scene", &currentBehavior,
                       sceneLoadingBehaviors, 3)) {
        preferences.sceneLoadingBehavior =
            static_cast<GUI::SceneLoadingBehavior>(currentBehavior);
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Choose default behavior when loading a scene file while "
                     "another scene is already loaded:\n"
                     "• Always Ask: Show dialog each time\n"
                     "• Always Replace: Clear existing scene\n"
                     "• Always Merge: Keep existing scene and add new objects");

      ImGui::PopID();
  }

    // ===========================
    // INPUT TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_INPUT) {
      ImGui::PushID("InputTab");
      DrawSectionHeader("Mouse Settings");

      if (ImGui::SliderFloat("Mouse Sensitivity", &camera.MouseSensitivity,
                             0.01f, 0.08f, "%.3f")) {
        preferences.mouseSensitivity = camera.MouseSensitivity;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Adjusts how quickly the camera rotates in response to "
                     "mouse movement");

      if (ImGui::SliderFloat("Mouse Smoothing", &mouseSmoothingFactor, 0.1f,
                             1.0f, "%.1f")) {
        preferences.mouseSmoothingFactor = mouseSmoothingFactor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Controls smoothness of mouse movement. Lower values = "
                     "smoother, higher values = more responsive");

      if (ImGui::SliderFloat("Speed Multiplier", &camera.speedFactor, 0.1f,
                             5.0f, "%.1fx")) {
        preferences.cameraSpeedFactor = camera.speedFactor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Multiplies base movement speed. Useful for navigating "
                     "larger scenes");

      if (ImGui::Checkbox("Zoom to Cursor", &camera.zoomToCursor)) {
        preferences.zoomToCursor = camera.zoomToCursor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("When enabled, scrolling zooms toward or away from the 3D "
                     "cursor position");

      DrawSectionHeader("Camera Behavior");

      bool standardOrbit = !camera.orbitAroundCursor && !orbitFollowsCursor;
      bool orbitAroundCursorOption = camera.orbitAroundCursor;
      bool orbitFollowsCursorOption = orbitFollowsCursor;

      if (ImGui::RadioButton("Standard Orbit", standardOrbit)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = false;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Orbits around the viewport center at cursor depth");

      if (ImGui::RadioButton("Orbit Around Cursor", orbitAroundCursorOption)) {
        camera.orbitAroundCursor = true;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = true;
        preferences.orbitFollowsCursor = false;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Orbits around the 3D position of the cursor without "
                     "centering the view");

      if (ImGui::RadioButton("Orbit Follows Cursor (Center)",
                             orbitFollowsCursorOption)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = true;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = true;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Centers the view on cursor position before orbiting");

      if (ImGui::Checkbox("Smooth Scrolling", &camera.useSmoothScrolling)) {
        preferences.useSmoothScrolling = camera.useSmoothScrolling;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Enable physics-based smooth scrolling");

      if (camera.useSmoothScrolling) {
        ImGui::Indent();
        if (ImGui::SliderFloat("Momentum", &camera.scrollMomentum, 0.0f, 1.0f,
                               "%.2f")) {
          preferences.scrollMomentum = camera.scrollMomentum;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Controls how much scrolling 'carries' (higher = more momentum)");

        if (ImGui::SliderFloat("Max Velocity", &camera.maxScrollVelocity, 0.5f,
                               10.0f, "%.1f")) {
          preferences.maxScrollVelocity = camera.maxScrollVelocity;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Maximum scroll speed");

        if (ImGui::SliderFloat("Deceleration", &camera.scrollDeceleration, 1.0f,
                               20.0f, "%.1f")) {
          preferences.scrollDeceleration = camera.scrollDeceleration;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("How quickly scrolling slows down");
        ImGui::Unindent();
      }

      DrawSectionHeader("3DConnexion SpaceMouse");

      if (spaceMouseInitialized) {
        if (ImGui::Checkbox("Enable SpaceMouse",
                            &preferences.spaceMouseEnabled)) {
          spaceMouseInput.SetEnabled(preferences.spaceMouseEnabled);
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Enable or disable 3DConnexion SpaceMouse input");

        if (preferences.spaceMouseEnabled) {
          ImGui::Spacing();
          ImGui::Text("Navigation Mode: CAD (Pivot)");
          ImGui::SameLine();
          DrawHelpMarker("Pivot-based navigation: rotate around a pivot point. "
                         "Best for inspecting 3D models.");

          ImGui::Spacing();
          ImGui::Separator();
          ImGui::Spacing();

          if (ImGui::SliderFloat("Deadzone", &preferences.spaceMouseDeadzone,
                                 0.0f, 0.5f, "%.2f")) {
            spaceMouseInput.SetDeadzone(preferences.spaceMouseDeadzone);
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Movement threshold below which SpaceMouse input is ignored");

          if (ImGui::SliderFloat("Translation",
                                 &preferences.spaceMouseTranslationSensitivity,
                                 0.1f, 3.0f, "%.1fx")) {
            spaceMouseInput.SetSensitivity(
                preferences.spaceMouseTranslationSensitivity,
                preferences.spaceMouseRotationSensitivity);
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Controls how sensitive translation movements are");

          if (ImGui::SliderFloat("Rotation",
                                 &preferences.spaceMouseRotationSensitivity,
                                 0.1f, 3.0f, "%.1fx")) {
            spaceMouseInput.SetSensitivity(
                preferences.spaceMouseTranslationSensitivity,
                preferences.spaceMouseRotationSensitivity);
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Controls how sensitive rotation movements are");

          // Only show anchor mode settings in CAD mode
          if (preferences.spaceMouseNavigationMode == GUI::SPACEMOUSE_NAV_CAD) {
            ImGui::Spacing();
            ImGui::Text("Anchor Point Mode:");

            int currentMode =
                static_cast<int>(preferences.spaceMouseAnchorMode);
            bool modeChanged = false;

            if (ImGui::RadioButton(
                    "Scene Center", &currentMode,
                    static_cast<int>(GUI::SPACEMOUSE_ANCHOR_DISABLED))) {
              modeChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker(
                "Use the scene center as the SpaceMouse pivot point");

            if (ImGui::RadioButton(
                    "Cursor on Start", &currentMode,
                    static_cast<int>(GUI::SPACEMOUSE_ANCHOR_ON_START))) {
              modeChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Set anchor to cursor position when SpaceMouse "
                           "navigation starts, then keep it fixed");

            if (ImGui::RadioButton(
                    "Follow Cursor", &currentMode,
                    static_cast<int>(GUI::SPACEMOUSE_ANCHOR_CONTINUOUS))) {
              modeChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker(
                "Continuously update anchor to follow the cursor position");

            if (ImGui::RadioButton(
                    "Click to Set", &currentMode,
                    static_cast<int>(GUI::SPACEMOUSE_ANCHOR_CLICK))) {
              modeChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker(
                "Left-click in the viewport to fix the SpaceMouse pivot at "
                "that point. Click again anywhere to move the pivot.");

            if (modeChanged) {
              preferences.spaceMouseAnchorMode =
                  static_cast<GUI::SpaceMouseAnchorMode>(currentMode);
              settingsChanged = true;
              updateSpaceMouseCursorAnchor();
              spaceMouseInput.RefreshPivotPosition();
            }

            if (ImGui::Checkbox("Center Cursor During Navigation",
                                &preferences.spaceMouseCenterCursor)) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Keep the mouse cursor fixed at the screen center "
                           "while using SpaceMouse");
          }
        }

        // ---- 3DConnexion App Settings (synced to/from 3DxWare XML) ----
        if (tdxSync.IsConnected()) {
          DrawSectionHeader("3DConnexion App Settings");

          // Helper lambda: assemble TdxSettings from preferences + pivot state
          auto buildTdxSettings = [&]() -> ThreeDConnexionSync::TdxSettings {
            ThreeDConnexionSync::TdxSettings s;
            s.motionModel = preferences.tdxSettings.motionModel;
            s.autoPivot = preferences.tdxSettings.autoPivot;
            s.lockHorizon = preferences.tdxSettings.lockHorizon;
            s.suspendInput = preferences.tdxSettings.suspendInput;
            s.lockTo3dViews = preferences.tdxSettings.lockTo3dViews;
            s.moveObjects = preferences.tdxSettings.moveObjects;
            s.autokeyAnimation = preferences.tdxSettings.autokeyAnimation;
            s.selectionFollower = preferences.tdxSettings.selectionFollower;
            s.firstPersonEaseOut = preferences.tdxSettings.firstPersonEaseOut;
            s.floorQueryRate = preferences.tdxSettings.floorQueryRate;
            s.lockSketchPlane = preferences.tdxSettings.lockSketchPlane;
            // Derive pivotVisibility from orbit center prefs
            if (preferences.showOrbitCenter &&
                preferences.alwaysShowOrbitCenter)
              s.pivotVisibility = "ShowPivot";
            else if (preferences.showOrbitCenter)
              s.pivotVisibility = "ShowMovingPivot";
            else
              s.pivotVisibility = "HidePivot";
            return s;
          };

          // Motion Model combo
          {
            const char *motionModels[] = {"Helicopter", "Object", "Fly",
                                          "Walk",       "Orbit",  "Target",
                                          "Drive"};
            int currentModel = 0;
            for (int i = 0; i < 7; ++i) {
              if (preferences.tdxSettings.motionModel == motionModels[i]) {
                currentModel = i;
                break;
              }
            }
            if (ImGui::Combo("Motion Model", &currentModel, motionModels, 7)) {
              preferences.tdxSettings.motionModel = motionModels[currentModel];
              tdxSync.WriteSettings(buildTdxSettings());
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Navigation style used by the SpaceMouse driver");
          }

          if (ImGui::Checkbox("Auto Pivot",
                              &preferences.tdxSettings.autoPivot)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Automatically choose the pivot point based on geometry");

          if (ImGui::Checkbox("Lock Horizon",
                              &preferences.tdxSettings.lockHorizon)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Keep the horizon level while navigating");

          if (ImGui::Checkbox("Suspend Input",
                              &preferences.tdxSettings.suspendInput)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Temporarily disable all SpaceMouse motion input");

          if (ImGui::Checkbox("Lock to 3D Views",
                              &preferences.tdxSettings.lockTo3dViews)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Restrict SpaceMouse input to 3D viewports only");

          if (ImGui::Checkbox("Move Objects",
                              &preferences.tdxSettings.moveObjects)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Move selected objects instead of the camera");

          if (ImGui::Checkbox("Autokey Animation",
                              &preferences.tdxSettings.autokeyAnimation)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Automatically create keyframes during animation");

          if (ImGui::Checkbox("Selection Follower",
                              &preferences.tdxSettings.selectionFollower)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Keep the selection centered in the view");

          if (ImGui::SliderInt("First Person Ease Out",
                               &preferences.tdxSettings.firstPersonEaseOut, 0,
                               1000)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Deceleration ramp length in first-person mode (0-1000)");

          if (ImGui::SliderInt("Floor Query Rate",
                               &preferences.tdxSettings.floorQueryRate, 1,
                               100)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "How often (per second) the floor height is queried (1-100)");

          if (ImGui::Checkbox("Lock Sketch Plane",
                              &preferences.tdxSettings.lockSketchPlane)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Lock navigation to the active sketch plane");
        }
      } else {
        ImGui::TextDisabled("SpaceMouse not detected");
        ImGui::TextWrapped(
            "Connect a 3Dconnexion SpaceMouse device to enable 3D navigation.");
      }

      DrawSectionHeader("Keyboard Shortcuts");

      if (ImGui::CollapsingHeader("Keybind Reference")) {
        ImGui::Columns(2, "keybinds");
        ImGui::SetColumnWidth(0, 150);

        ImGui::Text("Camera Controls");
        ImGui::Separator();

        ImGui::Text("W/S");
        ImGui::NextColumn();
        ImGui::Text("Move forward/backward");
        ImGui::NextColumn();

        ImGui::Text("A/D");
        ImGui::NextColumn();
        ImGui::Text("Move left/right");
        ImGui::NextColumn();

        ImGui::Text("Space/Shift");
        ImGui::NextColumn();
        ImGui::Text("Move up/down");
        ImGui::NextColumn();

        ImGui::Text("Left Mouse + Drag");
        ImGui::NextColumn();
        ImGui::Text("Orbit around the viewport center at cursor depth");
        ImGui::NextColumn();

        ImGui::Text("Right Mouse + Drag");
        ImGui::NextColumn();
        ImGui::Text("Rotate the camera");
        ImGui::NextColumn();

        ImGui::Text("Middle Mouse + Drag");
        ImGui::NextColumn();
        ImGui::Text("Pan camera");
        ImGui::NextColumn();

        ImGui::Text("Mouse Wheel");
        ImGui::NextColumn();
        ImGui::Text("Zoom in/out");
        ImGui::NextColumn();

        ImGui::Text("Double Click");
        ImGui::NextColumn();
        ImGui::Text("Center on cursor");
        ImGui::NextColumn();

        ImGui::Spacing();
        ImGui::NextColumn();
        ImGui::Spacing();
        ImGui::NextColumn();

        ImGui::Text("Other Controls");
        ImGui::Separator();

        // Dynamically display shortcuts from shortcut manager
        const StereoVista::ShortcutProfile *activeProfile =
            shortcutManager.getActiveProfile();
        if (activeProfile) {
          // Helper lambda to display a shortcut action
          auto displayAction = [&](StereoVista::ShortcutAction action) {
            const std::vector<StereoVista::KeyBinding> &bindings =
                activeProfile->getBindings(action);
            std::string bindingText = "Unbound";

            if (!bindings.empty() && bindings[0].isValid()) {
              bindingText = bindings[0].toString();
              // If there's a secondary binding, add it
              if (bindings.size() > 1 && bindings[1].isValid()) {
                bindingText += " / " + bindings[1].toString();
              }
            }

            ImGui::Text("%s", bindingText.c_str());
            ImGui::NextColumn();
            ImGui::Text(
                "%s", StereoVista::ShortcutManager::getActionDescription(action)
                          .c_str());
            ImGui::NextColumn();
          };

          // Display all customizable shortcuts
          displayAction(StereoVista::ShortcutAction::ToggleGUI);

          ImGui::Text("Ctrl + Click");
          ImGui::NextColumn();
          ImGui::Text("Select object");
          ImGui::NextColumn();

          ImGui::Text("Ctrl + Click + Drag");
          ImGui::NextColumn();
          ImGui::Text("Move selected object");
          ImGui::NextColumn();

          displayAction(StereoVista::ShortcutAction::DeleteObject);
          displayAction(StereoVista::ShortcutAction::Undo);
          displayAction(StereoVista::ShortcutAction::Redo);
          displayAction(StereoVista::ShortcutAction::CenterView);
          displayAction(StereoVista::ShortcutAction::CycleLighting);
          displayAction(StereoVista::ShortcutAction::ToggleShadows);
          displayAction(StereoVista::ShortcutAction::ToggleVoxelViz);
        }

        // Hardcoded non-customizable shortcut
        ImGui::Text("Esc");
        ImGui::NextColumn();
        ImGui::Text("Exit application");
        ImGui::NextColumn();

        ImGui::Columns(1);
      }

      ImGui::PopID();
  }

    // ===========================
    // IMPORT TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_IMPORT) {
      ImGui::PushID("ImportTab");
      DrawSectionHeader("Model Import Options");

      if (ImGui::Checkbox("Flip UV Coordinates",
                          &preferences.modelImportSettings.flipUVs)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Flips the V (Y) coordinate of UV maps. Enable if "
                     "textures appear upside down.");

      if (ImGui::Checkbox("Flip Normals",
                          &preferences.modelImportSettings.flipNormals)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Inverts the direction of all normals. Enable if lighting "
                     "appears inverted (dark faces should be bright).");

      if (ImGui::Checkbox("Generate Normals",
                          &preferences.modelImportSettings.generateNormals)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Automatically generate vertex normals for models that "
                     "don't have them.");

      ImGui::Indent();
      if (ImGui::Checkbox(
              "Use Smooth Normals",
              &preferences.modelImportSettings.generateSmoothNormals)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Generate smooth normals for better shading (only when "
                     "Generate Normals is enabled).");
      ImGui::Unindent();

      if (ImGui::Checkbox(
              "Calculate Tangents",
              &preferences.modelImportSettings.calculateTangentSpace)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Calculate tangent and bitangent vectors for normal mapping.");

      if (ImGui::Checkbox(
              "Merge Vertices",
              &preferences.modelImportSettings.joinIdenticalVertices)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Merge vertices that share the same position, normal, and "
                     "UV coordinates.");

      DrawSectionHeader("Optimization");

      if (ImGui::Checkbox(
              "Sort by Primitive Type",
              &preferences.modelImportSettings.sortByPrimitiveType)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Sort faces by primitive type for better rendering performance.");

      if (ImGui::Checkbox(
              "Fix Inverted Normals",
              &preferences.modelImportSettings.fixInfacingNormals)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Automatically fix normals that face inward instead of outward.");

      if (ImGui::Checkbox(
              "Remove Redundant Materials",
              &preferences.modelImportSettings.removeRedundantMaterials)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Remove duplicate materials to reduce memory usage.");

      if (ImGui::Checkbox("Optimize Meshes",
                          &preferences.modelImportSettings.optimizeMeshes)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Optimize mesh data for better rendering performance.");

      if (ImGui::Checkbox(
              "Pre-transform Vertices",
              &preferences.modelImportSettings.pretransformVertices)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Apply node transformations directly to vertices. Use for "
                     "static models only.");

      DrawSectionHeader("Auto-Scaling");

      if (ImGui::Checkbox(
              "Auto-scale Large Models",
              &preferences.modelImportSettings.autoScaleLargeModels)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Automatically scale down models that are too large to "
                     "fit comfortably in the scene.");

      if (preferences.modelImportSettings.autoScaleLargeModels) {
        ImGui::Indent();
        if (ImGui::SliderFloat("Max Model Radius",
                               &preferences.modelImportSettings.maxModelRadius,
                               1.0f, 20.0f, "%.1f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Maximum bounding sphere radius before auto-scaling is "
                       "applied. Default: 5.0 units.");
        ImGui::Unindent();
      }

      ImGui::Spacing();
      if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) {
        preferences.modelImportSettings =
            GUI::ApplicationPreferences::ModelImportSettings{};
        settingsChanged = true;
      }

      ImGui::PopID();
  }

    // ===========================
    // SHORTCUTS TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_SHORTCUTS) {
      ImGui::PushID("ShortcutsTab");
      DrawSectionHeader("Shortcut Profiles");

      // State variables for shortcut management (static to persist across
      // frames)
      static char profileNameBuffer[256] = "";
      static bool isCreatingProfile = false;
      static bool isRenamingProfile = false;
      static std::string profileToRename = "";
      static int captureActionIndex = -1;
      static int captureSlot = -1;
      static std::string conflictMessage = "";
      static bool waitingForKeyPress = false;

      // Profile selector
      StereoVista::ShortcutProfile *activeProfile =
          shortcutManager.getActiveProfile();
      if (activeProfile) {
        const std::vector<std::string> &profileNames =
            shortcutManager.getProfileNames();

        if (ImGui::BeginCombo("Active Profile",
                              shortcutManager.getActiveProfileName().c_str())) {
          for (const auto &name : profileNames) {
            bool isSelected = (shortcutManager.getActiveProfileName() == name);
            if (ImGui::Selectable(name.c_str(), isSelected)) {
              shortcutManager.setActiveProfile(name);
              settingsChanged = true;
            }
          }
          ImGui::EndCombo();
        }

        // Profile management buttons
        ImGui::SameLine();
        if (ImGui::Button("New")) {
          isCreatingProfile = true;
          strcpy_s(profileNameBuffer, "New Profile");
        }
        ImGui::SameLine();
        if (ImGui::Button("Rename")) {
          isRenamingProfile = true;
          profileToRename = shortcutManager.getActiveProfileName();
          strcpy_s(profileNameBuffer, profileToRename.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete") && profileNames.size() > 1) {
          shortcutManager.deleteProfile(shortcutManager.getActiveProfileName());
          settingsChanged = true;
        }
        if (profileNames.size() <= 1 && ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::Text("Cannot delete the last profile");
          ImGui::EndTooltip();
        }

        // Import/Export buttons
        ImGui::SameLine();
        if (ImGui::Button("Import")) {
          auto result = pfd::open_file("Import Shortcut Profile", ".",
                                       {"JSON Files", "*.json"})
                            .result();
          if (!result.empty()) {
            if (shortcutManager.importProfileFromFile(result[0])) {
              std::cout << "Profile imported successfully" << std::endl;
              settingsChanged = true;
            } else {
              std::cout << "Failed to import profile" << std::endl;
            }
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Export")) {
          auto result =
              pfd::save_file("Export Shortcut Profile",
                             shortcutManager.getActiveProfileName() + ".json",
                             {"JSON Files", "*.json"})
                  .result();
          if (!result.empty()) {
            if (shortcutManager.exportProfileToFile(
                    shortcutManager.getActiveProfileName(), result)) {
              std::cout << "Profile exported successfully" << std::endl;
            } else {
              std::cout << "Failed to export profile" << std::endl;
            }
          }
        }

        // Profile creation popup
        if (isCreatingProfile) {
          ImGui::OpenPopup("Create Profile");
          isCreatingProfile = false;
        }
        if (ImGui::BeginPopupModal("Create Profile", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::Text("Enter profile name:");
          ImGui::InputText("##profilename", profileNameBuffer, 256);
          if (ImGui::Button("Create", ImVec2(120, 0))) {
            StereoVista::ShortcutProfile newProfile(profileNameBuffer);
            newProfile.bindings =
                activeProfile->bindings; // Copy current bindings
            shortcutManager.addProfile(newProfile);
            shortcutManager.setActiveProfile(profileNameBuffer);
            settingsChanged = true;
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }

        // Profile rename popup
        if (isRenamingProfile) {
          ImGui::OpenPopup("Rename Profile");
          isRenamingProfile = false;
        }
        if (ImGui::BeginPopupModal("Rename Profile", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::Text("Enter new profile name:");
          ImGui::InputText("##renameprofile", profileNameBuffer, 256);
          if (ImGui::Button("Rename", ImVec2(120, 0))) {
            shortcutManager.renameProfile(profileToRename, profileNameBuffer);
            settingsChanged = true;
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }

        DrawSectionHeader("Key Bindings");

        // Show conflict message if any
        if (!conflictMessage.empty()) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
          ImGui::TextWrapped("%s", conflictMessage.c_str());
          ImGui::PopStyleColor();
        }

        // Shortcut list in a scrollable region
        ImGui::BeginChild("ShortcutList", ImVec2(0, 400), true);

        // Table with columns: Action | Primary Binding | Secondary Binding
        ImGui::Columns(3, "shortcutcolumns");
        ImGui::SetColumnWidth(0, 250);
        ImGui::SetColumnWidth(1, 200);
        ImGui::SetColumnWidth(2, 200);

        ImGui::Text("Action");
        ImGui::NextColumn();
        ImGui::Text("Primary Binding");
        ImGui::NextColumn();
        ImGui::Text("Secondary Binding");
        ImGui::NextColumn();
        ImGui::Separator();
        ImGui::Columns(1); // Reset columns after header

        // Helper lambda to render an action with its bindings
        auto renderAction = [&](StereoVista::ShortcutAction action) {
          int i = static_cast<int>(action);
          std::string actionDesc =
              StereoVista::ShortcutManager::getActionDescription(action);
          const std::vector<StereoVista::KeyBinding> &bindings =
              activeProfile->getBindings(action);

          // Action name
          ImGui::Text("%s", actionDesc.c_str());
          ImGui::NextColumn();

          // Primary binding (slot 0)
          std::string primaryBinding =
              (bindings.size() > 0 && bindings[0].isValid())
                  ? bindings[0].toString()
                  : "Unbound";

          if (waitingForKeyPress && captureActionIndex == i &&
              captureSlot == 0) {
            ImGui::Text("[Press any key...]");
          } else {
            if (ImGui::Button(
                    (primaryBinding + "##primary" + std::to_string(i)).c_str(),
                    ImVec2(180, 0))) {
              captureActionIndex = i;
              captureSlot = 0;
              waitingForKeyPress = true;
              conflictMessage = "";
            }
          }
          ImGui::NextColumn();

          // Secondary binding (slot 1)
          std::string secondaryBinding =
              (bindings.size() > 1 && bindings[1].isValid())
                  ? bindings[1].toString()
                  : "Unbound";

          if (waitingForKeyPress && captureActionIndex == i &&
              captureSlot == 1) {
            ImGui::Text("[Press any key...]");
          } else {
            if (ImGui::Button(
                    (secondaryBinding + "##secondary" + std::to_string(i))
                        .c_str(),
                    ImVec2(180, 0))) {
              captureActionIndex = i;
              captureSlot = 1;
              waitingForKeyPress = true;
              conflictMessage = "";
            }
          }
          ImGui::NextColumn();
        };

        // GROUP: View/Display Controls
        if (ImGui::CollapsingHeader("View/Display Controls",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ToggleGUI);
          renderAction(StereoVista::ShortcutAction::ToggleFPS);
          renderAction(StereoVista::ShortcutAction::ToggleWireframe);
          renderAction(StereoVista::ShortcutAction::ToggleRadar);
          renderAction(StereoVista::ShortcutAction::ToggleZeroPlane);
          renderAction(StereoVista::ShortcutAction::TakeScreenshot);
        }

        // GROUP: Camera Controls
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Camera Controls",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::CenterView);
          renderAction(StereoVista::ShortcutAction::ResetCamera);
          renderAction(StereoVista::ShortcutAction::FrameSelected);
          renderAction(StereoVista::ShortcutAction::ToggleZoomToCursor);
          renderAction(StereoVista::ShortcutAction::ToggleOrbitAroundCursor);
          renderAction(StereoVista::ShortcutAction::ViewFront);
          renderAction(StereoVista::ShortcutAction::ViewBack);
          renderAction(StereoVista::ShortcutAction::ViewRight);
          renderAction(StereoVista::ShortcutAction::ViewLeft);
          renderAction(StereoVista::ShortcutAction::ViewTop);
          renderAction(StereoVista::ShortcutAction::ViewBottom);
          renderAction(StereoVista::ShortcutAction::ViewIso);
        }

        // GROUP: Lighting
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Lighting",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::CycleLighting);
          renderAction(StereoVista::ShortcutAction::ToggleShadows);
          renderAction(StereoVista::ShortcutAction::ToggleHDR);
          renderAction(StereoVista::ShortcutAction::ToggleBloom);
          renderAction(StereoVista::ShortcutAction::TogglePCSS);
          renderAction(StereoVista::ShortcutAction::ToggleSunLight);
        }

        // GROUP: Materials & Rendering
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Materials & Rendering")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::TogglePBR);
        }

        // GROUP: VCT (Voxel Cone Tracing)
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("VCT (Voxel Cone Tracing)")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ToggleVoxelViz);
          renderAction(StereoVista::ShortcutAction::ToggleVCTIndirectDiffuse);
          renderAction(StereoVista::ShortcutAction::ToggleVCTIndirectSpecular);
          renderAction(StereoVista::ShortcutAction::ToggleVCTDirectLight);
          renderAction(StereoVista::ShortcutAction::ToggleVCTSoftShadows);
        }

        // GROUP: Raytracing
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Raytracing")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ToggleRaytracing);
          renderAction(StereoVista::ShortcutAction::ToggleIndirectLighting);
          renderAction(StereoVista::ShortcutAction::ToggleEmissiveLighting);
          renderAction(StereoVista::ShortcutAction::ToggleBVH);
        }

        // GROUP: 3D Cursor
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("3D Cursor")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ToggleSphereCursor);
          renderAction(StereoVista::ShortcutAction::ToggleCircleCursor);
          renderAction(StereoVista::ShortcutAction::TogglePlaneCursor);
        }

        // GROUP: Window Management
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Window Management")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::OpenSettings);
          renderAction(StereoVista::ShortcutAction::OpenCursorSettings);
          renderAction(StereoVista::ShortcutAction::OpenBrushTool);
          renderAction(StereoVista::ShortcutAction::OpenMeasurementTool);
        }

        // GROUP: File Operations
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("File Operations")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ImportModel);
          renderAction(StereoVista::ShortcutAction::ImportPointCloud);
          renderAction(StereoVista::ShortcutAction::SaveScene);
          renderAction(StereoVista::ShortcutAction::LoadScene);
        }

        // GROUP: Object Manipulation
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Object Manipulation")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::DeleteObject);
        }

        // GROUP: Edit History
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Edit History")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::Undo);
          renderAction(StereoVista::ShortcutAction::Redo);
        }

        ImGui::Columns(1);
        ImGui::EndChild();

        // Key capture logic
        if (waitingForKeyPress && captureActionIndex != -1) {
          ImGuiIO &io = ImGui::GetIO();

          // Check for key presses
          for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
            if (ImGui::IsKeyPressed((ImGuiKey)key, false)) {
              // Get modifiers
              bool ctrl = io.KeyCtrl;
              bool alt = io.KeyAlt;
              bool shift = io.KeyShift;

              // Don't capture modifier keys alone
              if (key == GLFW_KEY_LEFT_CONTROL ||
                  key == GLFW_KEY_RIGHT_CONTROL || key == GLFW_KEY_LEFT_ALT ||
                  key == GLFW_KEY_RIGHT_ALT || key == GLFW_KEY_LEFT_SHIFT ||
                  key == GLFW_KEY_RIGHT_SHIFT) {
                continue;
              }

              // Record the binding by its layout label rather than the physical
              // US-layout key position, so it matches at runtime on any keyboard
              // layout (see ShortcutManager::normalizeKeyToLayout).
              int labelKey =
                  StereoVista::ShortcutManager::normalizeKeyToLayout(key);
              StereoVista::KeyBinding newBinding(labelKey, ctrl, alt, shift);
              StereoVista::ShortcutAction targetAction =
                  static_cast<StereoVista::ShortcutAction>(captureActionIndex);

              // Check for conflicts
              auto conflict =
                  activeProfile->findConflict(newBinding, targetAction);
              if (conflict.has_value()) {
                conflictMessage =
                    "Conflict! This key is already bound to: " +
                    StereoVista::ShortcutManager::getActionDescription(
                        conflict.value());
              } else {
                // Set the binding
                activeProfile->setBinding(targetAction, newBinding,
                                          captureSlot);
                settingsChanged = true;
                conflictMessage = "";
              }

              waitingForKeyPress = false;
              captureActionIndex = -1;
              captureSlot = -1;
              break;
            }
          }

          // Allow ESC to cancel capture
          if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            waitingForKeyPress = false;
            captureActionIndex = -1;
            captureSlot = -1;
            conflictMessage = "";
          }

          // Allow right-click to clear binding
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            StereoVista::ShortcutAction targetAction =
                static_cast<StereoVista::ShortcutAction>(captureActionIndex);
            activeProfile->clearBinding(targetAction, captureSlot);
            settingsChanged = true;
            waitingForKeyPress = false;
            captureActionIndex = -1;
            captureSlot = -1;
            conflictMessage = "";
          }
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Click a binding to change it. Press ESC to cancel. "
                           "Right-click to clear a binding.");
        ImGui::Spacing();

        // Reset to defaults button
        if (ImGui::Button("Reset All to Defaults", ImVec2(-1, 0))) {
          shortcutManager.resetActiveProfileToDefaults();
          settingsChanged = true;
        }
      }

      ImGui::PopID();
  }

  ImGui::EndChild(); // ##SettingsContent

  if (settingsChanged) {
    savePreferences();
    // Save shortcuts when settings change
    shortcutManager.saveToFile("shortcuts.json");
  }

  ImGui::End();
}

void renderCursorSettingsWindow() {
  auto *sphereCursor = cursorManager.getSphereCursor();
  auto *fragmentCursor = cursorManager.getFragmentCursor();
  auto *planeCursor = cursorManager.getPlaneCursor();

  ImGui::SetNextWindowSize(ImVec2(540, 680), ImGuiCond_FirstUseEver);
  ImGui::Begin("Cursor Settings", &showCursorSettingsWindow);

  // Preset Management Section
  DrawSectionHeader("Cursor Presets");

  if (ImGui::BeginCombo("Current Preset", currentPresetName.c_str())) {
    if (ImGui::Selectable("Create New...")) {
      currentPresetName = "New Preset";
      isEditingPresetName = true;
      strcpy_s(editPresetNameBuffer, currentPresetName.c_str());
    }

    std::vector<std::string> presetNames =
        Engine::CursorPresetManager::getPresetNames();
    for (const auto &name : presetNames) {
      bool isSelected = (currentPresetName == name);
      if (ImGui::Selectable(name.c_str(), isSelected)) {
        currentPresetName = name;
        try {
          Engine::CursorPreset loadedPreset =
              Engine::CursorPresetManager::applyCursorPreset(name);

          sphereCursor->setVisible(loadedPreset.showSphereCursor);
          sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(
              loadedPreset.sphereScalingMode));
          sphereCursor->setFixedRadius(loadedPreset.sphereFixedRadius);
          sphereCursor->setTransparency(loadedPreset.sphereTransparency);
          sphereCursor->setShowInnerSphere(loadedPreset.showInnerSphere);
          sphereCursor->setColor(loadedPreset.cursorColor);
          sphereCursor->setInnerSphereColor(loadedPreset.innerSphereColor);
          sphereCursor->setInnerSphereFactor(loadedPreset.innerSphereFactor);
          sphereCursor->setEdgeSoftness(loadedPreset.cursorEdgeSoftness);
          sphereCursor->setCenterTransparency(
              loadedPreset.cursorCenterTransparency);

          fragmentCursor->setVisible(loadedPreset.showFragmentCursor);
          fragmentCursor->setBaseInnerRadius(
              loadedPreset.fragmentBaseInnerRadius);

          planeCursor->setVisible(loadedPreset.showPlaneCursor);
          planeCursor->setDiameter(loadedPreset.planeDiameter);
          planeCursor->setColor(loadedPreset.planeColor);

          preferences.currentPresetName = currentPresetName;
          savePreferences();
        } catch (const std::exception &e) {
          std::cerr << "Error loading preset: " << e.what() << std::endl;
        }
      }
    }
    ImGui::EndCombo();
  }

  if (isEditingPresetName) {
    ImGui::InputText("Name", editPresetNameBuffer,
                     IM_ARRAYSIZE(editPresetNameBuffer));

    if (ImGui::Button("Save", ImVec2(100, 0))) {
      std::string newName = editPresetNameBuffer;
      if (!newName.empty()) {
        Engine::CursorPreset newPreset;
        newPreset.name = newName;
        newPreset.showSphereCursor = sphereCursor->isVisible();
        newPreset.showFragmentCursor = fragmentCursor->isVisible();
        newPreset.fragmentBaseInnerRadius =
            fragmentCursor->getBaseInnerRadius();
        newPreset.sphereScalingMode =
            static_cast<int>(sphereCursor->getScalingMode());
        newPreset.sphereFixedRadius = sphereCursor->getFixedRadius();
        newPreset.sphereTransparency = sphereCursor->getTransparency();
        newPreset.showInnerSphere = sphereCursor->getShowInnerSphere();
        newPreset.cursorColor = sphereCursor->getColor();
        newPreset.innerSphereColor = sphereCursor->getInnerSphereColor();
        newPreset.innerSphereFactor = sphereCursor->getInnerSphereFactor();
        newPreset.cursorEdgeSoftness = sphereCursor->getEdgeSoftness();
        newPreset.cursorCenterTransparency =
            sphereCursor->getCenterTransparency();
        newPreset.showPlaneCursor = planeCursor->isVisible();
        newPreset.planeDiameter = planeCursor->getDiameter();
        newPreset.planeColor = planeCursor->getColor();

        Engine::CursorPresetManager::savePreset(newName, newPreset);
        currentPresetName = newName;
        isEditingPresetName = false;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
      isEditingPresetName = false;
    }
  } else {
    if (ImGui::Button("Update", ImVec2(100, 0))) {
      Engine::CursorPreset updatedPreset;
      updatedPreset.name = currentPresetName;
      updatedPreset.showSphereCursor = sphereCursor->isVisible();
      updatedPreset.showFragmentCursor = fragmentCursor->isVisible();
      updatedPreset.fragmentBaseInnerRadius =
          fragmentCursor->getBaseInnerRadius();
      updatedPreset.sphereScalingMode =
          static_cast<int>(sphereCursor->getScalingMode());
      updatedPreset.sphereFixedRadius = sphereCursor->getFixedRadius();
      updatedPreset.sphereTransparency = sphereCursor->getTransparency();
      updatedPreset.showInnerSphere = sphereCursor->getShowInnerSphere();
      updatedPreset.cursorColor = sphereCursor->getColor();
      updatedPreset.innerSphereColor = sphereCursor->getInnerSphereColor();
      updatedPreset.innerSphereFactor = sphereCursor->getInnerSphereFactor();
      updatedPreset.cursorEdgeSoftness = sphereCursor->getEdgeSoftness();
      updatedPreset.cursorCenterTransparency =
          sphereCursor->getCenterTransparency();
      updatedPreset.showPlaneCursor = planeCursor->isVisible();
      updatedPreset.planeDiameter = planeCursor->getDiameter();
      updatedPreset.planeColor = planeCursor->getColor();

      Engine::CursorPresetManager::savePreset(currentPresetName, updatedPreset);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename", ImVec2(100, 0))) {
      isEditingPresetName = true;
      strcpy_s(editPresetNameBuffer, currentPresetName.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(100, 0))) {
      if (currentPresetName != "Default") {
        Engine::CursorPresetManager::deletePreset(currentPresetName);
        currentPresetName = "Default";
      }
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Behavior Section
  DrawSectionHeader("Behavior");

  bool keepLastDepth = cursorManager.isKeepLastDepthOnBackground();
  if (ImGui::Checkbox("Keep Cursor At Last Depth Over Background",
                      &keepLastDepth)) {
    cursorManager.setKeepLastDepthOnBackground(keepLastDepth);
    preferences.cursorKeepLastDepthOnBackground = keepLastDepth;
    savePreferences();
  }
  if (ImGui::IsItemHovered()) {
    ImGui::SetTooltip(
        "Instead of switching to the Windows cursor over the background,\n"
        "the 3D cursor stays at the last valid depth and follows the mouse\n"
        "at that distance until it hits geometry again.\n"
        "Useful for sparse models and point clouds.");
  }

  // Cache-expiry options for the behavior above. Only relevant while it's on.
  if (keepLastDepth) {
    ImGui::Indent();

    const char *cacheModes[] = {"Indefinite", "Timed", "Distance"};
    int cacheMode = cursorManager.getBackgroundCacheMode();
    ImGui::SetNextItemWidth(220.0f);
    if (ImGui::Combo("Cache Duration", &cacheMode, cacheModes,
                     IM_ARRAYSIZE(cacheModes))) {
      cursorManager.setBackgroundCacheMode(cacheMode);
      preferences.cursorBackgroundCacheMode = cacheMode;
      savePreferences();
    }
    if (ImGui::IsItemHovered()) {
      ImGui::SetTooltip(
          "How long the cursor stays at the cached depth over the background:\n"
          "  Indefinite - never give up; hold the depth until geometry is hit\n"
          "  Timed      - revert to the Windows cursor after a time limit\n"
          "  Distance   - revert once the mouse travels too far from the\n"
          "               point where it left the geometry");
    }

    if (cacheMode == GUI::CURSOR_CACHE_TIMED) {
      float cacheTime = cursorManager.getBackgroundCacheTime();
      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::SliderFloat("Cache Time (s)", &cacheTime, 0.1f, 10.0f,
                             "%.1f")) {
        cursorManager.setBackgroundCacheTime(cacheTime);
        preferences.cursorBackgroundCacheTime = cacheTime;
        savePreferences();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "Seconds the cursor keeps the cached depth after leaving geometry\n"
            "before reverting to the Windows cursor.");
      }
    } else if (cacheMode == GUI::CURSOR_CACHE_DISTANCE) {
      float cacheDist = cursorManager.getBackgroundCacheDistance();
      ImGui::SetNextItemWidth(220.0f);
      if (ImGui::SliderFloat("Cache Distance (px)", &cacheDist, 20.0f, 2000.0f,
                             "%.0f")) {
        cursorManager.setBackgroundCacheDistance(cacheDist);
        preferences.cursorBackgroundCacheDistance = cacheDist;
        savePreferences();
      }
      if (ImGui::IsItemHovered()) {
        ImGui::SetTooltip(
            "How far (in screen pixels) the mouse may travel over the\n"
            "background from where it left the geometry before the cursor\n"
            "reverts to the Windows cursor.");
      }
    }

    ImGui::Unindent();
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // 3D Preview Section
  DrawSectionHeader("3D Preview");

  // Initialize the preview if not already done
  static bool previewInitialized = false;
  if (!previewInitialized) {
    cursorPreview3D.initialize(256, 256);
    previewInitialized = true;
  }

  // Get the currently active cursor based on visibility
  Cursor::BaseCursor *activeCursor = nullptr;
  if (sphereCursor->isVisible()) {
    activeCursor = sphereCursor;
  } else if (fragmentCursor->isVisible()) {
    activeCursor = fragmentCursor;
  } else if (planeCursor->isVisible()) {
    activeCursor = planeCursor;
  }

  if (activeCursor) {
    // Render the independent preview
    cursorPreview3D.render(activeCursor);

    // Display the preview image
    ImGui::Text("Current Cursor on Cube:");
    ImVec2 previewSize(200, 200);
    ImGui::Image((void *)(intptr_t)cursorPreview3D.getTextureID(), previewSize);

    // Handle mouse interaction for rotation
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      ImVec2 mouseDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
      cursorPreview3D.updateRotation(mouseDelta.x, mouseDelta.y);
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }

    ImGui::Text("Drag to rotate the preview");
  } else {
    ImGui::Text("No cursor is currently visible.");
    ImGui::Text("Enable a cursor type below to see the preview.");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Unified Scaling Section (applies to all cursor types)
  DrawSectionHeader("Universal Scaling");
  ImGui::Text("These settings apply to all enabled cursor types:");

  // Get scaling mode from any active cursor (they should all be the same)
  Cursor::BaseCursor *referenceCursor = nullptr;
  if (sphereCursor->isVisible())
    referenceCursor = sphereCursor;
  else if (fragmentCursor->isVisible())
    referenceCursor = fragmentCursor;
  else if (planeCursor->isVisible())
    referenceCursor = planeCursor;

  if (referenceCursor) {
    const char *scalingModes[] = {"Normal", "Fixed", "Constrained Dynamic",
                                  "Logarithmic"};
    int currentMode = static_cast<int>(referenceCursor->getScalingMode());
    if (ImGui::Combo("Scaling Mode", &currentMode, scalingModes,
                     IM_ARRAYSIZE(scalingModes))) {
      // Apply to all cursor types
      GUI::CursorScalingMode mode =
          static_cast<GUI::CursorScalingMode>(currentMode);
      sphereCursor->setScalingMode(mode);
      fragmentCursor->setScalingMode(mode);
      planeCursor->setScalingMode(mode);
    }

    if (currentMode == static_cast<int>(GUI::CursorScalingMode::CURSOR_FIXED)) {
      float baseSize = referenceCursor->getBaseSize();
      if (ImGui::SliderFloat("Base Size", &baseSize, 0.01f, 3.0f, "%.2f")) {
        // Apply to all cursor types
        sphereCursor->setBaseSize(baseSize);
        fragmentCursor->setBaseSize(baseSize);
        planeCursor->setBaseSize(baseSize);
      }
    } else {
      float minDiff = referenceCursor->getMinDiff();
      if (ImGui::SliderFloat("Min Size Difference", &minDiff, 0.01f, 2.0f,
                             "%.2f")) {
        // Apply to all cursor types
        sphereCursor->setMinDiff(minDiff);
        fragmentCursor->setMinDiff(minDiff);
        planeCursor->setMinDiff(minDiff);
      }

      float maxDiff = referenceCursor->getMaxDiff();
      if (ImGui::SliderFloat("Max Size Difference", &maxDiff, 0.02f, 5.0f,
                             "%.2f")) {
        // Apply to all cursor types
        sphereCursor->setMaxDiff(maxDiff);
        fragmentCursor->setMaxDiff(maxDiff);
        planeCursor->setMaxDiff(maxDiff);
      }
    }
  } else {
    ImGui::Text("Enable a cursor type to configure scaling settings.");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Cursor Type Tabs
  if (ImGui::BeginTabBar("CursorTypes")) {

    // 3D Sphere Tab
    if (ImGui::BeginTabItem("3D Sphere")) {
      ImGui::PushID("3DSphereTab");
      bool sphereVisible = sphereCursor->isVisible();
      if (ImGui::Checkbox("Enable 3D Sphere", &sphereVisible)) {
        sphereCursor->setVisible(sphereVisible);
      }

      if (sphereVisible) {
        DrawSectionHeader("3D Sphere Settings");

        glm::vec4 cursorColor = sphereCursor->getColor();
        if (ImGui::ColorEdit3("Color", glm::value_ptr(cursorColor))) {
          sphereCursor->setColor(cursorColor);
        }

        float transparency = sphereCursor->getTransparency();
        if (ImGui::SliderFloat("Opacity", &transparency, 0.0f, 1.0f, "%.2f")) {
          sphereCursor->setTransparency(transparency);
        }

        float edgeSoftness = sphereCursor->getEdgeSoftness();
        if (ImGui::SliderFloat("Edge Softness", &edgeSoftness, 0.0f, 1.0f,
                               "%.2f")) {
          sphereCursor->setEdgeSoftness(edgeSoftness);
        }

        float centerTransparency = sphereCursor->getCenterTransparency();
        if (ImGui::SliderFloat("Center Fade", &centerTransparency, 0.0f, 1.0f,
                               "%.2f")) {
          sphereCursor->setCenterTransparency(centerTransparency);
        }

        DrawSectionHeader("Inner Sphere");

        bool showInnerSphere = sphereCursor->getShowInnerSphere();
        if (ImGui::Checkbox("Show Inner Core", &showInnerSphere)) {
          sphereCursor->setShowInnerSphere(showInnerSphere);
        }

        if (showInnerSphere) {
          glm::vec4 innerColor = sphereCursor->getInnerSphereColor();
          if (ImGui::ColorEdit3("Inner Color", glm::value_ptr(innerColor))) {
            sphereCursor->setInnerSphereColor(innerColor);
          }

          float innerFactor = sphereCursor->getInnerSphereFactor();
          if (ImGui::SliderFloat("Inner Size", &innerFactor, 0.1f, 0.9f,
                                 "%.2f")) {
            sphereCursor->setInnerSphereFactor(innerFactor);
          }
        }
      }
      ImGui::PopID();
      ImGui::EndTabItem();
    }

    // 2D Circle Tab
    if (ImGui::BeginTabItem("2D Circle")) {
      ImGui::PushID("2DCircleTab");
      bool fragmentVisible = fragmentCursor->isVisible();
      if (ImGui::Checkbox("Enable 2D Circle", &fragmentVisible)) {
        fragmentCursor->setVisible(fragmentVisible);
      }

      if (fragmentVisible) {
        DrawSectionHeader("Ring Dimensions");

        float outerRadius = fragmentCursor->getBaseOuterRadius();
        if (ImGui::SliderFloat("Outer Radius", &outerRadius, 0.0f, 0.3f,
                               "%.3f")) {
          fragmentCursor->setBaseOuterRadius(outerRadius);
        }

        float outerBorder = fragmentCursor->getBaseOuterBorderThickness();
        if (ImGui::SliderFloat("Outer Thickness", &outerBorder, 0.0f, 0.08f,
                               "%.3f")) {
          fragmentCursor->setBaseOuterBorderThickness(outerBorder);
        }

        float innerRadius = fragmentCursor->getBaseInnerRadius();
        if (ImGui::SliderFloat("Inner Radius", &innerRadius, 0.0f, 0.2f,
                               "%.3f")) {
          fragmentCursor->setBaseInnerRadius(innerRadius);
        }

        float innerBorder = fragmentCursor->getBaseInnerBorderThickness();
        if (ImGui::SliderFloat("Inner Thickness", &innerBorder, 0.0f, 0.08f,
                               "%.3f")) {
          fragmentCursor->setBaseInnerBorderThickness(innerBorder);
        }

        DrawSectionHeader("Colors");

        glm::vec4 outerColor = fragmentCursor->getOuterColor();
        if (ImGui::ColorEdit4("Outer Ring", glm::value_ptr(outerColor))) {
          fragmentCursor->setOuterColor(outerColor);
        }

        glm::vec4 innerColor = fragmentCursor->getInnerColor();
        if (ImGui::ColorEdit4("Inner Ring", glm::value_ptr(innerColor))) {
          fragmentCursor->setInnerColor(innerColor);
        }
      }
      ImGui::PopID();
      ImGui::EndTabItem();
    }

    // Surface Plane Tab
    if (ImGui::BeginTabItem("Surface Plane")) {
      ImGui::PushID("SurfacePlaneTab");
      bool planeVisible = planeCursor->isVisible();
      if (ImGui::Checkbox("Enable Surface Plane", &planeVisible)) {
        planeCursor->setVisible(planeVisible);
      }

      if (planeVisible) {
        DrawSectionHeader("Plane Settings");

        glm::vec4 planeColor = planeCursor->getColor();
        if (ImGui::ColorEdit3("Color", glm::value_ptr(planeColor))) {
          planeCursor->setColor(planeColor);
        }

        float planeDiameter = planeCursor->getDiameter();
        if (ImGui::SliderFloat("Size", &planeDiameter, 0.1f, 5.0f, "%.1f")) {
          planeCursor->setDiameter(planeDiameter);
        }

        ImGui::TextWrapped("The plane cursor aligns to surface normals and "
                           "shows the tangent plane at the cursor position.");
      }
      ImGui::PopID();
      ImGui::EndTabItem();
    }

    // Orbit Visualization Tab
    if (ImGui::BeginTabItem("Orbit Center")) {
      ImGui::PushID("OrbitCenterTab");
      DrawSectionHeader("Orbit Visualization");

      bool showOrbitCenter = cursorManager.isShowOrbitCenter();
      if (ImGui::Checkbox("Show Orbit Center", &showOrbitCenter)) {
        cursorManager.setShowOrbitCenter(showOrbitCenter);
        preferences.showOrbitCenter = showOrbitCenter;
        savePreferences();
        // Sync pivot visibility to 3DConnexion XML
        if (tdxSync.IsConnected()) {
          auto s = tdxSync.GetSettings();
          ThreeDConnexionSync::TdxSettings ws = s;
          ws.motionModel = preferences.tdxSettings.motionModel;
          ws.autoPivot = preferences.tdxSettings.autoPivot;
          ws.lockHorizon = preferences.tdxSettings.lockHorizon;
          ws.suspendInput = preferences.tdxSettings.suspendInput;
          ws.lockTo3dViews = preferences.tdxSettings.lockTo3dViews;
          ws.moveObjects = preferences.tdxSettings.moveObjects;
          ws.autokeyAnimation = preferences.tdxSettings.autokeyAnimation;
          ws.selectionFollower = preferences.tdxSettings.selectionFollower;
          ws.firstPersonEaseOut = preferences.tdxSettings.firstPersonEaseOut;
          ws.floorQueryRate = preferences.tdxSettings.floorQueryRate;
          ws.lockSketchPlane = preferences.tdxSettings.lockSketchPlane;
          ws.pivotVisibility =
              showOrbitCenter
                  ? (preferences.alwaysShowOrbitCenter ? "ShowPivot"
                                                       : "ShowMovingPivot")
                  : "HidePivot";
          tdxSync.WriteSettings(ws);
        }
      }
      ImGui::SameLine();
      DrawHelpMarker("Display a marker at the camera's orbit pivot point");

      if (showOrbitCenter) {
        bool alwaysShowOrbitCenter = cursorManager.isAlwaysShowOrbitCenter();
        if (ImGui::Checkbox("Always Show Orbit Center",
                            &alwaysShowOrbitCenter)) {
          cursorManager.setAlwaysShowOrbitCenter(alwaysShowOrbitCenter);
          preferences.alwaysShowOrbitCenter = alwaysShowOrbitCenter;
          savePreferences();
          // Sync pivot visibility to 3DConnexion XML
          if (tdxSync.IsConnected()) {
            ThreeDConnexionSync::TdxSettings ws = tdxSync.GetSettings();
            ws.motionModel = preferences.tdxSettings.motionModel;
            ws.autoPivot = preferences.tdxSettings.autoPivot;
            ws.lockHorizon = preferences.tdxSettings.lockHorizon;
            ws.suspendInput = preferences.tdxSettings.suspendInput;
            ws.lockTo3dViews = preferences.tdxSettings.lockTo3dViews;
            ws.moveObjects = preferences.tdxSettings.moveObjects;
            ws.autokeyAnimation = preferences.tdxSettings.autokeyAnimation;
            ws.selectionFollower = preferences.tdxSettings.selectionFollower;
            ws.firstPersonEaseOut = preferences.tdxSettings.firstPersonEaseOut;
            ws.floorQueryRate = preferences.tdxSettings.floorQueryRate;
            ws.lockSketchPlane = preferences.tdxSettings.lockSketchPlane;
            ws.pivotVisibility =
                alwaysShowOrbitCenter ? "ShowPivot" : "ShowMovingPivot";
            tdxSync.WriteSettings(ws);
          }
        }
        ImGui::SameLine();
        DrawHelpMarker("Keep the orbit center visible even when not orbiting");
      }

      if (showOrbitCenter) {
        glm::vec4 orbitColor = cursorManager.getOrbitCenterColor();
        if (ImGui::ColorEdit3("Marker Color", glm::value_ptr(orbitColor))) {
          cursorManager.setOrbitCenterColor(orbitColor);
          preferences.orbitCenterColor = orbitColor;
          savePreferences();
        }

        float orbitSize = cursorManager.getOrbitCenterSphereRadius();
        if (ImGui::SliderFloat("Marker Size", &orbitSize, 0.01f, 1.0f,
                               "%.2f")) {
          cursorManager.setOrbitCenterSphereRadius(orbitSize);
          preferences.orbitCenterSphereRadius = orbitSize;
          savePreferences();
        }
      }

      DrawSectionHeader("Orbit Behavior");

      ImGui::Text("Camera orbit mode affects where the orbit center appears:");
      ImGui::Spacing();

      ImGui::TextWrapped("• Standard: Center of viewport at cursor depth");
      ImGui::TextWrapped("• Around Cursor: At the 3D cursor position");
      ImGui::TextWrapped("• Follow Cursor: View centers on cursor first");

      ImGui::PopID();
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::End();
}

void renderBrushToolWindow() {
  extern Tools::BrushTool brushTool;

  ImGui::SetNextWindowSize(ImVec2(450, 650), ImGuiCond_FirstUseEver);
  ImGui::Begin("Brush Tool", &showBrushToolWindow);

  DrawSectionHeader("Brush Tool Settings");

  // Enable/Disable brush tool
  if (ImGui::Checkbox("Enable Brush Tool",
                      &preferences.brushToolSettings.enabled)) {
    // Enabling/disabling is handled in main.cpp; just keep the measurement
    // tool exclusive since both consume left clicks.
    if (preferences.brushToolSettings.enabled) {
      measurementTool.setEnabled(false);
    }
  }

  if (!preferences.brushToolSettings.enabled) {
    ImGui::TextDisabled("Enable the brush tool to start painting");
    ImGui::End();
    return;
  }

  ImGui::Spacing();

  // Cluster Management
  DrawSectionHeader("Brush Clusters");

  // Create new cluster section
  static char newClusterName[128] = "New Cluster";
  ImGui::InputText("Cluster Name", newClusterName,
                   IM_ARRAYSIZE(newClusterName));

  static std::vector<std::string> modelNames;
  modelNames.clear();
  modelNames.push_back("None");
  for (size_t i = 0; i < currentScene.models.size(); i++) {
    modelNames.push_back(currentScene.models[i].name);
  }

  int newClusterModelIndex =
      preferences.brushToolSettings.selectedModelIndex + 1;
  ImGui::Combo(
      "Model", &newClusterModelIndex,
      [](void *data, int idx, const char **out_text) {
        std::vector<std::string> *names =
            static_cast<std::vector<std::string> *>(data);
        *out_text = (*names)[idx].c_str();
        return true;
      },
      &modelNames, modelNames.size());
  preferences.brushToolSettings.selectedModelIndex = newClusterModelIndex - 1;

  if (ImGui::Button("Create New Cluster", ImVec2(-1, 0))) {
    if (preferences.brushToolSettings.selectedModelIndex >= 0) {
      brushTool.createCluster(std::string(newClusterName),
                              preferences.brushToolSettings.selectedModelIndex);
      strcpy_s(newClusterName, "New Cluster");
    }
  }

  ImGui::Spacing();

  // List of existing clusters
  DrawSectionHeader("Existing Clusters");

  int activeCluster = brushTool.getActiveCluster();
  int clusterCount = brushTool.getClusterCount();

  if (clusterCount == 0) {
    ImGui::TextDisabled("No clusters created yet");
  } else {
    for (int i = 0; i < clusterCount; i++) {
      const Tools::BrushCluster *cluster = brushTool.getCluster(i);
      if (!cluster)
        continue;

      ImGui::PushID(i);

      bool isActive = (i == activeCluster);
      if (ImGui::Selectable(cluster->name.c_str(), isActive)) {
        brushTool.setActiveCluster(i);
      }

      // Show instance count
      ImGui::SameLine();
      ImGui::TextDisabled("(%d instances)", cluster->instances.size());

      ImGui::PopID();
    }
  }

  ImGui::Spacing();

  // Active cluster settings
  if (activeCluster >= 0 && activeCluster < clusterCount) {
    Tools::BrushCluster *cluster = brushTool.getCluster(activeCluster);
    if (cluster) {
      DrawSectionHeader("Active Cluster Settings");

      ImGui::Text("Cluster: %s", cluster->name.c_str());
      if (cluster->sourceModelIndex >= 0 &&
          cluster->sourceModelIndex < currentScene.models.size()) {
        ImGui::Text(
            "Model: %s",
            currentScene.models[cluster->sourceModelIndex].name.c_str());
      }
      ImGui::Text("Instances: %d",
                  brushTool.getInstanceCountForCluster(activeCluster));

      ImGui::Spacing();

      // Instance Variation
      DrawSectionHeader("Instance Variation");

      ImGui::SliderFloat("Min Scale", &cluster->minScale, 0.1f, 5.0f, "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Minimum scale multiplier (1.0 = original size)");

      ImGui::SliderFloat("Max Scale", &cluster->maxScale, 0.1f, 5.0f, "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Maximum scale multiplier (1.0 = original size)");

      if (cluster->maxScale < cluster->minScale) {
        cluster->maxScale = cluster->minScale;
      }

      ImGui::SliderFloat("Rotation", &cluster->rotationRandomization, 0.0f,
                         1.0f, "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Random rotation amount (0 = no rotation, 1 = full 360°)");

      ImGui::SliderFloat("Color Variation", &cluster->colorVariation, 0.0f,
                         1.0f, "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Random color variation for instances");

      ImGui::Checkbox("Align to Surface", &cluster->alignToNormal);
      ImGui::SameLine();
      DrawHelpMarker("Align painted instances to surface normal");

      ImGui::Spacing();

      // Cluster Actions
      DrawSectionHeader("Cluster Actions");

      if (ImGui::Button("Clear Instances", ImVec2(-1, 0))) {
        brushTool.clearInstancesForCluster(activeCluster);
      }

      if (ImGui::Button("Delete Cluster", ImVec2(-1, 0))) {
        brushTool.deleteCluster(activeCluster);
      }
    }
  }

  ImGui::Spacing();

  // Global Brush Settings
  DrawSectionHeader("Global Brush Settings");

  ImGui::SliderFloat("Brush Radius", &preferences.brushToolSettings.brushRadius,
                     0.1f, 5.0f, "%.2f");
  ImGui::SameLine();
  DrawHelpMarker("Size of the brush area");

  ImGui::SliderFloat("Min Spacing", &preferences.brushToolSettings.minSpacing,
                     0.0f, 2.0f, "%.2f");
  ImGui::SameLine();
  DrawHelpMarker("Minimum distance between painted instances");

  ImGui::SliderFloat("Density", &preferences.brushToolSettings.density, 0.1f,
                     5.0f, "%.2f");
  ImGui::SameLine();
  DrawHelpMarker("Number of instances per paint stroke");

  ImGui::Checkbox("Show Brush Cursor",
                  &preferences.brushToolSettings.showBrushCursor);

  ImGui::Spacing();

  // Global Statistics
  DrawSectionHeader("Statistics");
  ImGui::Text("Total Clusters: %d", clusterCount);
  ImGui::Text("Total Instances: %d", brushTool.getTotalInstanceCount());

  if (ImGui::Button("Clear All Clusters", ImVec2(-1, 0))) {
    brushTool.clearAllInstances();
  }

  ImGui::Spacing();

  // Usage instructions
  DrawSectionHeader("Usage");
  ImGui::TextWrapped("1. Create a new cluster with a model");
  ImGui::TextWrapped("2. Select cluster from the list to edit");
  ImGui::TextWrapped("3. Adjust cluster settings");
  ImGui::TextWrapped(
      "4. Hold LEFT MOUSE BUTTON and drag over surfaces to paint");

  ImGui::End();
}

void renderMeasurementToolWindow() {
  ImGui::SetNextWindowSize(ImVec2(430, 580), ImGuiCond_FirstUseEver);
  ImGui::Begin("Measurement Tool", &showMeasurementToolWindow);

  DrawSectionHeader("Measurement Tool");

  bool enabled = measurementTool.isEnabled();
  if (ImGui::Checkbox("Enable Measuring", &enabled)) {
    measurementTool.setEnabled(enabled);
    if (enabled) {
      // Measuring and brush painting both consume left clicks — keep them
      // mutually exclusive.
      preferences.brushToolSettings.enabled = false;
    }
  }
  ImGui::SameLine();
  DrawHelpMarker("While enabled, LEFT CLICK places a measurement point at the "
                 "3D cursor. RIGHT CLICK or ENTER finishes a polyline/polygon, "
                 "BACKSPACE removes the last point, DELETE cancels.");

  int mode = static_cast<int>(measurementTool.getMode());
  const char *modeNames[] = {"Distance (polyline)", "Angle (3 points)",
                             "Point (coordinates)", "Area (polygon)"};
  if (ImGui::Combo("Mode", &mode, modeNames, IM_ARRAYSIZE(modeNames))) {
    measurementTool.setMode(static_cast<Engine::Measurement::Type>(mode));
  }

  ImGui::ColorEdit3("New Measurement Color",
                    glm::value_ptr(measurementTool.nextColor));

  // In-progress measurement status + controls
  if (measurementTool.hasActive()) {
    ImGui::Spacing();
    DrawSectionHeader("In Progress");
    const auto &active = measurementTool.getActive();
    ImGui::Text("%s - %d point(s)", active.name.c_str(),
                static_cast<int>(active.points.size()));
    if (active.type == Engine::Measurement::Type::Distance &&
        active.points.size() >= 2) {
      ImGui::Text("Length so far: %s",
                  measurementTool.formatLength(active.totalLength()).c_str());
    }
    if (active.type == Engine::Measurement::Type::Area &&
        active.points.size() >= 3) {
      ImGui::Text("Area so far: %s",
                  measurementTool.formatArea(active.area()).c_str());
      ImGui::Text("Perimeter: %s",
                  measurementTool.formatLength(active.perimeter()).c_str());
    }
    if (ImGui::Button("Finish", ImVec2(100, 0))) {
      measurementTool.finishActive();
    }
    ImGui::SameLine();
    if (ImGui::Button("Undo Point", ImVec2(100, 0))) {
      measurementTool.undoLastPoint();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
      measurementTool.cancelActive();
    }
  }

  ImGui::Spacing();
  DrawSectionHeader("Display");

  ImGui::Checkbox("Show Labels", &measurementTool.showLabels);
  ImGui::SameLine();
  ImGui::Checkbox("Segment Labels", &measurementTool.showSegmentLabels);
  ImGui::SameLine();
  DrawHelpMarker("Show a length label on every segment of a polyline, not "
                 "just the total");
  ImGui::Checkbox("X-Ray", &measurementTool.xRay);
  ImGui::SameLine();
  DrawHelpMarker("Ghost-render measurement lines that are hidden behind "
                 "geometry");
  ImGui::SliderFloat("Line Width", &measurementTool.lineWidth, 1.0f, 8.0f,
                     "%.1f");

  // Unit selection: world units are treated as meters
  static const char *unitNames[] = {"m", "dm", "cm", "mm", "km", "ft", "in"};
  static const float unitScales[] = {1.0f,    10.0f,    100.0f, 1000.0f,
                                     0.001f,  3.28084f, 39.3701f};
  static int unitIndex = 0;
  if (ImGui::Combo("Units", &unitIndex, unitNames, IM_ARRAYSIZE(unitNames))) {
    measurementTool.unitScale = unitScales[unitIndex];
    measurementTool.unitSuffix = unitNames[unitIndex];
  }

  ImGui::Spacing();
  DrawSectionHeader("Measurements");

  auto *measurements = measurementTool.getMeasurements();
  if (!measurements || measurements->empty()) {
    ImGui::TextDisabled("No measurements yet");
  } else {
    int deleteIndex = -1;
    for (int i = 0; i < static_cast<int>(measurements->size()); i++) {
      auto &m = (*measurements)[i];
      ImGui::PushID(i);

      ImGui::Checkbox("##visible", &m.visible);
      ImGui::SameLine();
      ImGui::ColorEdit3("##color", glm::value_ptr(m.color),
                        ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_NoLabel);
      ImGui::SameLine();

      std::string value;
      switch (m.type) {
      case Engine::Measurement::Type::Angle:
        value = std::to_string(m.angleDegrees());
        value = value.substr(0, value.find('.') + 2) + " deg";
        break;
      case Engine::Measurement::Type::Point:
        if (!m.points.empty()) {
          char buf[96];
          snprintf(buf, sizeof(buf), "(%.2f, %.2f, %.2f)", m.points[0].x,
                   m.points[0].y, m.points[0].z);
          value = buf;
        }
        break;
      case Engine::Measurement::Type::Area:
        value = measurementTool.formatArea(m.area());
        break;
      case Engine::Measurement::Type::Distance:
      default:
        value = measurementTool.formatLength(m.totalLength());
        break;
      }
      ImGui::Text("%s: %s", m.name.c_str(), value.c_str());

      ImGui::SameLine(ImGui::GetWindowWidth() - 40 * g_GuiScale.currentScale);
      if (ImGui::SmallButton("X")) {
        deleteIndex = i;
      }

      ImGui::PopID();
    }
    if (deleteIndex >= 0) {
      measurementTool.deleteMeasurement(deleteIndex);
    }

    ImGui::Spacing();
    if (ImGui::Button("Export to CSV...", ImVec2(-1, 0))) {
      auto destination =
          pfd::save_file("Export measurements", "measurements.csv",
                         {"CSV File", "*.csv", "All Files", "*"})
              .result();
      if (!destination.empty()) {
        if (std::filesystem::path(destination).extension() != ".csv")
          destination += ".csv";
        if (!measurementTool.exportToCSV(destination))
          std::cerr << "Failed to export measurements to " << destination
                    << std::endl;
      }
    }
    DrawHelpMarker("Write all measurements (coordinates + lengths/angles) to a "
                   "CSV file for use in reports or spreadsheets.");

    if (ImGui::Button("Clear All Measurements", ImVec2(-1, 0))) {
      measurementTool.clearAll();
    }
  }

  ImGui::Spacing();
  ImGui::TextDisabled("Measurements are saved with the scene file.");

  ImGui::End();
}

void renderClipPlaneToolWindow() {
  ImGui::SetNextWindowSize(ImVec2(440, 560), ImGuiCond_FirstUseEver);
  ImGui::Begin("Section Planes", &showClipPlaneToolWindow);

  // The tool's editing UX (overlay, gizmo takeover, scroll scrub) is active
  // while this panel is open.
  clipPlaneTool.setEnabled(true);

  DrawSectionHeader("Section / Clip Planes");
  ImGui::TextWrapped(
      "Section planes hide scene geometry on the back side of each plane. Add a "
      "plane at the 3D cursor or an axis-aligned plane, then move / rotate it "
      "with the transform gizmo, or scroll to slide the active plane along its "
      "normal.");

  auto *planes = clipPlaneTool.getPlanes();
  const int planeCount = planes ? static_cast<int>(planes->size()) : 0;
  const bool full = planeCount >= Engine::MAX_CLIP_PLANES;

  ImGui::Spacing();
  DrawSectionHeader("Add Plane");
  if (full)
    ImGui::BeginDisabled();
  if (ImGui::Button("At Cursor", ImVec2(120 * g_GuiScale.currentScale, 0)))
    addClipPlaneAtCursor();
  ImGui::SameLine();
  DrawHelpMarker("Places a plane at the 3D cursor, oriented by the surface "
                 "normal under it (camera-facing fallback). Hover geometry "
                 "first so the cursor has a valid position.");
  ImGui::SameLine();
  if (ImGui::Button("X##addAxisX", ImVec2(34 * g_GuiScale.currentScale, 0)))
    addClipPlaneAxisAligned(0);
  ImGui::SameLine();
  if (ImGui::Button("Y##addAxisY", ImVec2(34 * g_GuiScale.currentScale, 0)))
    addClipPlaneAxisAligned(1);
  ImGui::SameLine();
  if (ImGui::Button("Z##addAxisZ", ImVec2(34 * g_GuiScale.currentScale, 0)))
    addClipPlaneAxisAligned(2);
  ImGui::SameLine();
  DrawHelpMarker("Add an axis-aligned plane through the selection centre, else "
                 "the 3D cursor, else the world origin.");
  if (full) {
    ImGui::EndDisabled();
    ImGui::TextDisabled("Plane budget full (%d).", Engine::MAX_CLIP_PLANES);
  }

  ImGui::Spacing();
  DrawSectionHeader("Display");
  ImGui::SliderFloat("Overlay Size", &clipPlaneTool.displaySize, 0.25f, 25.0f,
                     "%.2f");
  ImGui::SliderFloat("Scroll Step", &clipPlaneTool.nudgeStep, 0.01f, 2.0f,
                     "%.3f");

  ImGui::Spacing();
  DrawSectionHeader("Planes");
  if (planeCount == 0) {
    ImGui::TextDisabled("No section planes yet.");
  } else {
    const int activeIdx = clipPlaneTool.activeIndex();
    int deleteIndex = -1;
    for (int i = 0; i < planeCount; ++i) {
      auto &p = (*planes)[i];
      ImGui::PushID(i);

      ImGui::Checkbox("##enabled", &p.enabled);
      ImGui::SameLine();
      ImGui::ColorEdit3("##color", glm::value_ptr(p.color),
                        ImGuiColorEditFlags_NoInputs |
                            ImGuiColorEditFlags_NoLabel);
      ImGui::SameLine();
      const bool selected = (i == activeIdx);
      if (ImGui::RadioButton(p.name.c_str(), selected))
        clipPlaneTool.setActiveIndex(selected ? -1 : i);

      ImGui::SameLine(ImGui::GetWindowWidth() - 96 * g_GuiScale.currentScale);
      if (ImGui::SmallButton("Flip"))
        clipPlaneTool.flipNormal(i);
      ImGui::SameLine();
      if (ImGui::SmallButton("X"))
        deleteIndex = i;

      if (selected) {
        ImGui::Indent();
        ImGui::DragFloat3("Position", glm::value_ptr(p.position), 0.05f);
        ImGui::DragFloat3("Normal", glm::value_ptr(p.normal), 0.02f, -1.0f,
                          1.0f);
        ImGui::Unindent();
      }
      ImGui::PopID();
    }
    if (deleteIndex >= 0)
      clipPlaneTool.deletePlane(deleteIndex);

    // Gizmo controls (move / rotate) for the active plane.
    if (clipPlaneTool.activeIndex() >= 0) {
      ImGui::Spacing();
      DrawSectionHeader("Active Plane Gizmo");
      DrawTransformGizmoControls(/*canRotateScale=*/true);
    } else {
      ImGui::Spacing();
      ImGui::TextDisabled("Select a plane to edit it with the gizmo.");
    }
  }

  ImGui::Spacing();
  ImGui::TextDisabled(
      "Section planes are saved with the scene file. Default key: P.");

  ImGui::End();

  // Closing the window (X) disables the editing UX; the planes keep clipping.
  if (!showClipPlaneToolWindow)
    clipPlaneTool.setEnabled(false);
}

// Draw a FontAwesome glyph inline with an optional hover tooltip. The icon font
// is merged into the regular font, so the glyph is available directly; the
// explicit PushFont keeps it visually consistent with the rest of the GUI.
static void DrawSnapshotIcon(const char *icon, const char *tooltip) {
  if (g_Fonts.icons)
    ImGui::PushFont(g_Fonts.icons);
  ImGui::TextUnformatted(icon);
  if (g_Fonts.icons)
    ImGui::PopFont();
  if (tooltip && ImGui::IsItemHovered())
    ImGui::SetTooltip("%s", tooltip);
}

// ── Snapshot organization helpers ─────────────────────────────────────────

// Case-insensitive subsequence match: every character of `needle` must appear
// in `haystack` in order (not necessarily contiguous). An empty needle always
// matches. This gives a forgiving "fuzzy" feel for the search box.
static bool snapshotFuzzyMatch(const std::string &needle,
                               const std::string &haystack) {
  if (needle.empty())
    return true;
  size_t h = 0;
  for (char rawN : needle) {
    char n = static_cast<char>(std::tolower(static_cast<unsigned char>(rawN)));
    bool found = false;
    for (; h < haystack.size(); ++h) {
      char c = static_cast<char>(
          std::tolower(static_cast<unsigned char>(haystack[h])));
      if (c == n) {
        ++h;
        found = true;
        break;
      }
    }
    if (!found)
      return false;
  }
  return true;
}

// Build the combined searchable text for a snapshot: name, timestamp, tags and
// the names of the aspects it stored. Used by the fuzzy search box.
static std::string snapshotSearchText(const Core::Snapshot &s) {
  std::string text = s.name + " " + s.timestamp;
  for (const auto &t : s.tags)
    text += " " + t;
  if (s.flags & Core::SNAPSHOT_CAMERA)
    text += " camera";
  if (s.flags & Core::SNAPSHOT_SCENE)
    text += " scene";
  if (s.flags & Core::SNAPSHOT_TOOLS)
    text += " tools";
  return text;
}

// Split a comma-separated string into trimmed, non-empty tags.
static std::vector<std::string> parseSnapshotTags(const char *csv) {
  std::vector<std::string> out;
  std::string cur;
  auto flush = [&]() {
    size_t a = cur.find_first_not_of(" \t");
    size_t b = cur.find_last_not_of(" \t");
    if (a != std::string::npos)
      out.push_back(cur.substr(a, b - a + 1));
    cur.clear();
  };
  for (const char *p = csv; *p; ++p) {
    if (*p == ',')
      flush();
    else
      cur += *p;
  }
  flush();
  return out;
}

static std::string joinSnapshotTags(const std::vector<std::string> &tags) {
  std::string s;
  for (size_t i = 0; i < tags.size(); ++i) {
    if (i)
      s += ", ";
    s += tags[i];
  }
  return s;
}

// A small rounded "chip" used to display / toggle a tag. Returns true when the
// chip is clicked. `active` draws it with the accent fill (used for active
// filters).
static bool DrawTagChip(const char *label, bool active) {
  ImVec4 base =
      active ? g_StyleColors.accent
             : ImVec4(g_StyleColors.primary.x, g_StyleColors.primary.y,
                      g_StyleColors.primary.z, 0.35f);
  ImGui::PushStyleColor(ImGuiCol_Button, base);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_StyleColors.primaryHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_StyleColors.primaryActive);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,
                      ImGui::GetFrameHeight() * 0.5f);
  std::string id = std::string(label) + "##chip";
  bool clicked = ImGui::SmallButton(id.c_str());
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
  return clicked;
}

// Scene Manager panel: a single home for scene file operations (new / open /
// save / save-as), recent files, the live scene's stats and editable metadata,
// and the per-scene save options. Complements the File menu so all of this is
// reachable from one dockable window.
void renderSceneManagerWindow() {
  float scale = g_GuiScale.currentScale;
  ImGui::SetNextWindowSize(ImVec2(460 * scale, 640 * scale),
                           ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(340 * scale, 320 * scale),
                                      ImVec2(100000.0f, 100000.0f));
  ImGui::Begin("Scene Manager", &showSceneManagerWindow);

  // ── Current scene ────────────────────────────────────────────────────────
  DrawSectionHeader("Current Scene");

  const bool untitled = g_currentScenePath.empty();
  std::string sceneName =
      untitled ? std::string("Untitled")
               : std::filesystem::path(g_currentScenePath).stem().string();

  ImGui::Text("%s", sceneName.c_str());
  ImGui::SameLine();
  if (g_sceneDirty) {
    ImGui::TextColored(ImVec4(1.0f, 0.7f, 0.2f, 1.0f), "  (unsaved changes)");
  } else {
    ImGui::TextColored(ImVec4(0.5f, 0.8f, 0.5f, 1.0f), "  (saved)");
  }
  if (!untitled) {
    ImGui::TextDisabled("%s", g_currentScenePath.c_str());
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("%s", g_currentScenePath.c_str());
  }
  if (!currentScene.metadata.modifiedAt.empty()) {
    ImGui::TextDisabled("Last saved: %s",
                        currentScene.metadata.modifiedAt.c_str());
  }

  ImGui::Spacing();

  // ── Action buttons ───────────────────────────────────────────────────────
  float fullW = ImGui::GetContentRegionAvail().x;
  float halfW = (fullW - ImGui::GetStyle().ItemSpacing.x) * 0.5f;
  if (ImGui::Button("New", ImVec2(halfW, 0)))
    SceneRequestNew();
  ImGui::SameLine();
  if (ImGui::Button("Open...", ImVec2(halfW, 0)))
    SceneOpenDialog();
  if (ImGui::Button("Save", ImVec2(halfW, 0)))
    SceneQuickSave();
  ImGui::SameLine();
  if (ImGui::Button("Save As...", ImVec2(halfW, 0)))
    SceneSaveAsDialog();

  // ── Statistics ───────────────────────────────────────────────────────────
  DrawSectionHeader("Contents");
  if (ImGui::BeginTable("scene_stats", 2,
                        ImGuiTableFlags_SizingStretchProp)) {
    auto row = [](const char *label, size_t count) {
      ImGui::TableNextRow();
      ImGui::TableSetColumnIndex(0);
      ImGui::TextUnformatted(label);
      ImGui::TableSetColumnIndex(1);
      ImGui::Text("%zu", count);
    };
    row("Models", currentScene.models.size());
    row("Point Clouds", currentScene.pointClouds.size());
    row("Point Lights", pointLights.size());
    row("Spot Lights", spotLights.size());
    row("Measurements", currentScene.measurements.size());
    row("Section Planes", currentScene.clipPlanes.size());
    row("Snapshots", Core::SnapshotManager::instance().snapshots().size());
    ImGui::EndTable();
  }

  // ── Metadata ─────────────────────────────────────────────────────────────
  DrawSectionHeader("Metadata");
  static char descBuf[1024];
  static char authorBuf[256];
  // Keep the buffers in sync with the live scene's metadata while the fields
  // aren't being edited (so New / Load refreshes them, and edits aren't lost).
  if (!ImGui::IsAnyItemActive()) {
    strncpy_s(descBuf, currentScene.metadata.description.c_str(),
              sizeof(descBuf) - 1);
    strncpy_s(authorBuf, currentScene.metadata.author.c_str(),
              sizeof(authorBuf) - 1);
  }
  if (ImGui::InputText("Author", authorBuf, sizeof(authorBuf))) {
    currentScene.metadata.author = authorBuf;
    g_sceneDirty = true;
  }
  if (ImGui::InputTextMultiline("Description", descBuf, sizeof(descBuf),
                                ImVec2(ImGui::GetContentRegionAvail().x,
                                       60 * scale))) {
    currentScene.metadata.description = descBuf;
    g_sceneDirty = true;
  }
  if (!currentScene.metadata.createdAt.empty()) {
    ImGui::TextDisabled("Created: %s", currentScene.metadata.createdAt.c_str());
  }

  // ── Save options ─────────────────────────────────────────────────────────
  DrawSectionHeader("Save Options");
  auto &opt = preferences.sceneSaveSettings;
  bool changed = false;
  changed |= ImGui::Checkbox("Camera viewpoint", &opt.includeCamera);
  changed |= ImGui::Checkbox("Lighting (sun + lights)", &opt.includeLighting);
  changed |= ImGui::Checkbox("Environment (skybox + mode)",
                             &opt.includeEnvironment);
  changed |= ImGui::Checkbox("Measurements", &opt.includeMeasurements);
  changed |= ImGui::Checkbox("Section planes", &opt.includeClipPlanes);
  changed |= ImGui::Checkbox("Snapshots", &opt.includeSnapshots);
  ImGui::SameLine();
  DrawHelpMarker("Save named snapshots (metadata + thumbnails) into the "
                 "scene folder so they reload with the scene.");
  changed |= ImGui::Checkbox("Compact (minified) JSON", &opt.compact);
  ImGui::SameLine();
  DrawHelpMarker("Write the scene's .scene file without indentation. Smaller "
                 "files, slightly harder to read by hand.");
  changed |= ImGui::Checkbox("Apply saved environment on load",
                             &preferences.applySceneEnvironmentOnLoad);
  ImGui::SameLine();
  DrawHelpMarker("When loading a scene that stored a skybox / lighting mode, "
                 "switch the live session to match it.");

  const char *behaviors[] = {"Always Ask", "Always Replace",
                             "Always Merge"};
  int behavior = static_cast<int>(preferences.sceneLoadingBehavior);
  if (ImGui::Combo("On load (with existing scene)", &behavior, behaviors, 3)) {
    preferences.sceneLoadingBehavior =
        static_cast<GUI::SceneLoadingBehavior>(behavior);
    changed = true;
  }
  if (changed)
    savePreferences();

  // ── Recent scenes ────────────────────────────────────────────────────────
  DrawSectionHeader("Recent Scenes");
  if (preferences.recentScenes.empty()) {
    ImGui::TextDisabled("No recent scenes.");
  } else {
    // Defer all mutating actions until after the loop: loading a scene calls
    // SceneAddRecent(), which rewrites preferences.recentScenes and would
    // invalidate the iterator / dangling `recent` reference mid-iteration.
    std::string toRemove;
    std::string toLoad;
    bool clearAll = false;
    for (const auto &recent : preferences.recentScenes) {
      ImGui::PushID(recent.c_str());
      bool exists = std::filesystem::exists(recent);
      std::string fname = std::filesystem::path(recent).filename().string();

      if (!exists)
        ImGui::BeginDisabled();
      if (ImGui::Button("Load"))
        toLoad = recent;
      if (!exists)
        ImGui::EndDisabled();

      ImGui::SameLine();
      if (ImGui::SmallButton("x"))
        toRemove = recent;
      ImGui::SameLine();
      ImGui::TextUnformatted(fname.c_str());
      if (!exists) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.5f, 1.0f), "(missing)");
      }
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("%s", recent.c_str());
      ImGui::PopID();
    }
    ImGui::Spacing();
    if (ImGui::Button("Clear Recent List"))
      clearAll = true;

    if (clearAll) {
      preferences.recentScenes.clear();
      savePreferences();
    } else if (!toRemove.empty()) {
      auto &r = preferences.recentScenes;
      r.erase(std::remove(r.begin(), r.end(), toRemove), r.end());
      savePreferences();
    } else if (!toLoad.empty()) {
      SceneRequestLoad(toLoad);
    }
  }

  ImGui::End();
}

// Snapshots panel: capture the current camera / scene / tool state into named,
// thumbnailed checkpoints and roll back to them. Mirrors the screenshot capture
// pipeline for the thumbnail and the Undo edit-states for the scene data.
void renderSnapshotsWindow() {
  float scale = g_GuiScale.currentScale;
  ImGui::SetNextWindowSize(ImVec2(560 * scale, 620 * scale),
                           ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(380 * scale, 300 * scale),
                                      ImVec2(100000.0f, 100000.0f));
  ImGui::Begin("Snapshots", &showSnapshotsWindow);

  auto &mgr = Core::SnapshotManager::instance();

  // ── Create card ─────────────────────────────────────────────────────────
  DrawSectionHeader("New Snapshot");

  static char snapName[128] = "";
  static bool saveCamera = true;
  static bool saveScene = true;
  static bool saveTools = false;
  static int autoCounter = 1;

  ImGui::PushStyleColor(ImGuiCol_ChildBg,
                        ImVec4(g_StyleColors.primary.x, g_StyleColors.primary.y,
                               g_StyleColors.primary.z, 0.06f));
  ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * scale);
  ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                      ImVec2(12.0f * scale, 12.0f * scale));
  ImGui::BeginChild("CreateCard", ImVec2(0, 0),
                    ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);

  ImGui::SetNextItemWidth(-1);
  ImGui::InputTextWithHint("##snapName", "Name (optional)…", snapName,
                           IM_ARRAYSIZE(snapName));

  ImGui::Spacing();
  ImGui::TextDisabled("WHICH ASPECTS TO CAPTURE");
  ImGui::Spacing();

  // Right-aligned toggle rows so the three aspects read as a clean list. The
  // leading icon brightens to the accent color when its aspect is enabled.
  auto aspectRow = [&](const char *icon, const char *label, const char *help,
                       bool *v) {
    DrawInlineIcon(icon, *v ? g_StyleColors.accent
                            : ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
    if (help) {
      ImGui::SameLine();
      DrawHelpMarker(help);
    }
    float tw = ImGui::GetFrameHeight() * 1.85f;
    ImGui::SameLine();
    float room = ImGui::GetContentRegionAvail().x;
    if (room > tw)
      ImGui::SetCursorPosX(ImGui::GetCursorPosX() + room - tw);
    ImGui::PushID(label);
    DrawToggleSwitch("##t", v);
    ImGui::PopID();
  };
  aspectRow(ICON_FA_CAMERA, "Camera", "Camera position & orientation",
            &saveCamera);
  aspectRow(ICON_FA_CUBES, "Scene",
            "Objects: transform, color, visibility, materials, lights, sun, "
            "measurements, section planes",
            &saveScene);
  aspectRow(ICON_FA_TOOLS, "Tools",
            "Tool settings (brush, measure, section planes)", &saveTools);

  uint32_t flags = (saveCamera ? Core::SNAPSHOT_CAMERA : 0u) |
                   (saveScene ? Core::SNAPSHOT_SCENE : 0u) |
                   (saveTools ? Core::SNAPSHOT_TOOLS : 0u);

  ImGui::Spacing();
  const bool canCapture = flags != 0u;
  if (!canCapture)
    ImGui::BeginDisabled();
  ImGui::PushStyleColor(ImGuiCol_Button, g_StyleColors.primary);
  ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_StyleColors.primaryHover);
  ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_StyleColors.primaryActive);
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
  if (ImGui::Button(ICON_FA_CAMERA "  Capture Snapshot",
                    ImVec2(-1, 36 * scale))) {
    std::string name = snapName[0] != '\0'
                           ? std::string(snapName)
                           : ("Snapshot " + std::to_string(autoCounter));
    autoCounter++;
    Core::RequestSnapshotCapture(name, flags);
    snapName[0] = '\0';
  }
  ImGui::PopStyleVar();
  ImGui::PopStyleColor(3);
  if (!canCapture) {
    ImGui::EndDisabled();
    ImGui::Spacing();
    ImGui::TextDisabled("Select at least one aspect to capture.");
  }

  ImGui::EndChild();
  ImGui::PopStyleVar(2);
  ImGui::PopStyleColor();

  ImGui::Spacing();

  // ── Saved snapshots ──────────────────────────────────────────────────────
  auto &snaps = mgr.snapshots();
  DrawSectionHeader(
      ("Saved Snapshots (" + std::to_string(snaps.size()) + ")").c_str());

  if (snaps.empty()) {
    ImGui::Spacing();
    ImGui::Spacing();
    const char *msg = "No snapshots yet — capture one above.";
    float tw = ImGui::CalcTextSize(msg).x;
    ImGui::SetCursorPosX(
        std::max(0.0f, (ImGui::GetContentRegionAvail().x - tw) * 0.5f));
    ImGui::TextDisabled("%s", msg);
    ImGui::End();
    return;
  }

  // ── Search box ──────────────────────────────────────────────────────────
  // Fuzzy (subsequence) match across name, timestamp, tags and aspect names.
  static char searchBuf[128] = "";
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 8.0f * scale);
  DrawInlineIcon(ICON_FA_SEARCH,
                 ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::SetNextItemWidth(-(38 * scale));
  ImGui::InputTextWithHint("##snapSearch", "Search name, tags, date, aspects…",
                           searchBuf, IM_ARRAYSIZE(searchBuf));
  ImGui::SameLine();
  if (ImGui::Button(ICON_FA_TIMES "##clearSearch"))
    searchBuf[0] = '\0';
  if (ImGui::IsItemHovered())
    ImGui::SetTooltip("Clear search");
  ImGui::PopStyleVar();

  // ── Tag filter chips ────────────────────────────────────────────────────
  // Collect the live tag set and drop any active filters whose tag no longer
  // exists (e.g. after an edit or delete). A snapshot passes when it carries
  // any of the selected tags.
  static std::set<std::string> activeTagFilters;
  std::set<std::string> allTags;
  for (const auto &s : snaps)
    for (const auto &t : s.tags)
      allTags.insert(t);
  for (auto it = activeTagFilters.begin(); it != activeTagFilters.end();) {
    if (allTags.find(*it) == allTags.end())
      it = activeTagFilters.erase(it);
    else
      ++it;
  }

  if (!allTags.empty()) {
    const ImGuiStyle &style = ImGui::GetStyle();
    float rightEdge =
        ImGui::GetCursorScreenPos().x + ImGui::GetContentRegionAvail().x;
    DrawInlineIcon(ICON_FA_FILTER,
                   ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    int chipId = 0;
    for (const auto &t : allTags) {
      ImGui::PushID(chipId++);
      float thisW = ImGui::CalcTextSize(t.c_str()).x +
                    style.FramePadding.x * 2.0f + 8.0f * scale;
      if (ImGui::GetItemRectMax().x + style.ItemSpacing.x + thisW < rightEdge)
        ImGui::SameLine();
      bool active = activeTagFilters.count(t) != 0;
      if (DrawTagChip(t.c_str(), active)) {
        if (active)
          activeTagFilters.erase(t);
        else
          activeTagFilters.insert(t);
      }
      ImGui::PopID();
    }
    if (!activeTagFilters.empty()) {
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear"))
        activeTagFilters.clear();
      if (ImGui::IsItemHovered())
        ImGui::SetTooltip("Clear tag filters");
    }
  }

  ImGui::Spacing();

  int toRestore = -1;
  int toDelete = -1;

  // Inline editor state. `editingIndex` indexes into the live snapshot vector;
  // it is cleared whenever the vector is mutated (delete) so it can't dangle.
  static int editingIndex = -1;
  static char editName[128] = "";
  static char editTags[256] = "";
  static bool editHasColor = false;
  static glm::vec3 editColor = glm::vec3(0.40f, 0.65f, 1.0f);

  int visibleCount = 0;

  ImGui::BeginChild("SnapshotList", ImVec2(0, 0), ImGuiChildFlags_None);
  for (int i = 0; i < static_cast<int>(snaps.size()); ++i) {
    Core::Snapshot &s = snaps[i];

    // Apply the search box and tag filters.
    if (searchBuf[0] != '\0' &&
        !snapshotFuzzyMatch(searchBuf, snapshotSearchText(s)))
      continue;
    if (!activeTagFilters.empty()) {
      bool anyMatch = false;
      for (const auto &t : s.tags)
        if (activeTagFilters.count(t)) {
          anyMatch = true;
          break;
        }
      if (!anyMatch)
        continue;
    }
    ++visibleCount;

    ImGui::PushID(i);

    // Each snapshot is a self-contained rounded card.
    ImGui::PushStyleColor(
        ImGuiCol_ChildBg,
        ImVec4(g_StyleColors.primary.x, g_StyleColors.primary.y,
               g_StyleColors.primary.z, 0.05f));
    ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding, 10.0f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,
                        ImVec2(10.0f * scale, 10.0f * scale));
    ImGui::BeginChild("card", ImVec2(0, 0),
                      ImGuiChildFlags_Border | ImGuiChildFlags_AutoResizeY);

    const float thumbW = 150 * scale;
    const float thumbH =
        s.thumbWidth > 0
            ? thumbW * static_cast<float>(s.thumbHeight) /
                  static_cast<float>(s.thumbWidth)
            : thumbW * 0.5625f;

    // Top row: thumbnail on the left, details column on the right.
    if (s.thumbnailTexture != 0)
      ImGui::Image((void *)(intptr_t)s.thumbnailTexture,
                   ImVec2(thumbW, thumbH));
    else
      ImGui::Dummy(ImVec2(thumbW, thumbH));

    ImGui::SameLine();
    ImGui::BeginGroup();

    // Color marker dot + name.
    if (s.hasColor) {
      ImGui::ColorButton("##marker",
                         ImVec4(s.color.r, s.color.g, s.color.b, 1.0f),
                         ImGuiColorEditFlags_NoTooltip |
                             ImGuiColorEditFlags_NoPicker,
                         ImVec2(12 * scale, 12 * scale));
      ImGui::SameLine();
    }
    if (g_Fonts.bold)
      ImGui::PushFont(g_Fonts.bold);
    ImGui::TextUnformatted(s.name.c_str());
    if (g_Fonts.bold)
      ImGui::PopFont();

    DrawInlineIcon(ICON_FA_CLOCK,
                   ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
    ImGui::TextDisabled("%s", s.timestamp.c_str());

    // Aspect badges marking what this snapshot stored.
    if (s.flags & Core::SNAPSHOT_CAMERA) {
      DrawSnapshotIcon(ICON_FA_CAMERA, "Camera");
      ImGui::SameLine();
    }
    if (s.flags & Core::SNAPSHOT_SCENE) {
      DrawSnapshotIcon(ICON_FA_CUBES, "Scene");
      ImGui::SameLine();
    }
    if (s.flags & Core::SNAPSHOT_TOOLS) {
      DrawSnapshotIcon(ICON_FA_TOOLS, "Tools");
      ImGui::SameLine();
    }
    ImGui::NewLine();

    // Tag chips: click one to toggle it as a filter.
    if (!s.tags.empty()) {
      DrawSnapshotIcon(ICON_FA_TAGS, "Tags");
      for (size_t ti = 0; ti < s.tags.size(); ++ti) {
        ImGui::SameLine();
        ImGui::PushID(static_cast<int>(ti));
        bool active = activeTagFilters.count(s.tags[ti]) != 0;
        if (DrawTagChip(s.tags[ti].c_str(), active)) {
          if (active)
            activeTagFilters.erase(s.tags[ti]);
          else
            activeTagFilters.insert(s.tags[ti]);
        }
        ImGui::PopID();
      }
    }
    ImGui::EndGroup();

    // Action row (full card width). Restore reads as the primary action via
    // the accent fill; Delete uses the danger color.
    ImGui::Spacing();
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * scale);
    ImGui::PushStyleColor(ImGuiCol_Button, g_StyleColors.primary);
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_StyleColors.primaryHover);
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_StyleColors.primaryActive);
    if (ImGui::Button(ICON_FA_HISTORY " Restore"))
      toRestore = i;
    ImGui::PopStyleColor(3);
    if (ImGui::IsItemHovered())
      ImGui::SetTooltip("Roll the saved aspects back onto the scene");

    ImGui::SameLine();
    const bool editingThis = (editingIndex == i);
    if (editingThis) {
      ImGui::PushStyleColor(ImGuiCol_Button, g_StyleColors.accent);
      ImGui::PushStyleColor(ImGuiCol_ButtonHovered, g_StyleColors.accent);
      ImGui::PushStyleColor(ImGuiCol_ButtonActive, g_StyleColors.accent);
    }
    if (ImGui::Button(ICON_FA_PEN " Edit")) {
      editingIndex = editingThis ? -1 : i;
      if (editingIndex == i) {
        std::snprintf(editName, sizeof(editName), "%s", s.name.c_str());
        std::string joined = joinSnapshotTags(s.tags);
        std::snprintf(editTags, sizeof(editTags), "%s", joined.c_str());
        editHasColor = s.hasColor;
        editColor = s.color;
      }
    }
    if (editingThis)
      ImGui::PopStyleColor(3);

    ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_Button, g_StyleColors.danger);
    if (ImGui::Button(ICON_FA_TRASH " Delete"))
      toDelete = i;
    ImGui::PopStyleColor();
    ImGui::PopStyleVar();

    // Inline editor for name / tags / color.
    if (editingIndex == i) {
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();
      ImGui::SetNextItemWidth(-1);
      ImGui::InputTextWithHint("##editName", "Name", editName,
                               IM_ARRAYSIZE(editName));
      ImGui::SetNextItemWidth(-1);
      ImGui::InputTextWithHint("##editTags", "Tags (comma-separated)", editTags,
                               IM_ARRAYSIZE(editTags));
      ImGui::Checkbox("Color marker", &editHasColor);
      if (editHasColor) {
        ImGui::SameLine();
        ImGui::ColorEdit3("##editColor", &editColor.x,
                          ImGuiColorEditFlags_NoInputs |
                              ImGuiColorEditFlags_NoLabel);
      }
      ImGui::Spacing();
      ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 7.0f * scale);
      ImGui::PushStyleColor(ImGuiCol_Button, g_StyleColors.success);
      if (ImGui::Button(ICON_FA_CHECK " Save")) {
        if (editName[0] != '\0')
          s.name = editName;
        s.tags = parseSnapshotTags(editTags);
        s.hasColor = editHasColor;
        s.color = editColor;
        editingIndex = -1;
      }
      ImGui::PopStyleColor();
      ImGui::SameLine();
      if (ImGui::Button(ICON_FA_TIMES " Cancel"))
        editingIndex = -1;
      ImGui::PopStyleVar();
    }

    ImGui::EndChild();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();

    // Draw the left color stripe and a hover ring over the finished card.
    ImVec2 cardMin = ImGui::GetItemRectMin();
    ImVec2 cardMax = ImGui::GetItemRectMax();
    ImDrawList *dl = ImGui::GetWindowDrawList();
    if (s.hasColor)
      dl->AddRectFilled(
          cardMin, ImVec2(cardMin.x + 4.0f * scale, cardMax.y),
          ImGui::ColorConvertFloat4ToU32(
              ImVec4(s.color.r, s.color.g, s.color.b, 1.0f)),
          10.0f * scale, ImDrawFlags_RoundCornersLeft);
    if (ImGui::IsItemHovered())
      dl->AddRect(cardMin, cardMax, ImGui::GetColorU32(g_StyleColors.accent),
                  10.0f * scale, 0, 1.5f * scale);

    ImGui::PopID();
    ImGui::Spacing();
  }
  if (visibleCount == 0) {
    ImGui::Spacing();
    const char *msg = "No snapshots match the current search / filters.";
    float tw = ImGui::CalcTextSize(msg).x;
    ImGui::SetCursorPosX(
        std::max(0.0f, (ImGui::GetContentRegionAvail().x - tw) * 0.5f));
    ImGui::TextDisabled("%s", msg);
  }
  ImGui::EndChild();

  // Apply deferred actions after the loop so the list isn't mutated mid-draw.
  if (toRestore >= 0)
    Core::RestoreSnapshot(toRestore);
  if (toDelete >= 0) {
    mgr.remove(toDelete);
    // Indices shift after a delete; close any open editor to avoid dangling.
    editingIndex = -1;
  }

  ImGui::End();
}

// Projects measurement values (segment lengths, angles, coordinates) into
// screen space and draws them as labels on the foreground draw list.
static void drawMeasurementLabels() {
  if (!measurementTool.showLabels)
    return;
  auto *measurements = measurementTool.getMeasurements();

  const glm::mat4 viewProj =
      camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                 preferences.farPlane) *
      camera.GetViewMatrix();
  ImDrawList *drawList = ImGui::GetForegroundDrawList();

  // GetForegroundDrawList() targets the main viewport, whose coordinate space
  // is desktop-absolute when multi-viewport is enabled. The projection below
  // yields main-window-relative pixels, so shift by the main viewport origin
  // (a no-op when the window sits at the desktop origin).
  const ImVec2 vpOrigin = ImGui::GetMainViewport()->Pos;

  auto project = [&](const glm::vec3 &world, ImVec2 &out) -> bool {
    glm::vec4 clip = viewProj * glm::vec4(world, 1.0f);
    if (clip.w <= 1e-4f)
      return false;
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (ndc.x < -1.1f || ndc.x > 1.1f || ndc.y < -1.1f || ndc.y > 1.1f)
      return false;
    out = ImVec2(vpOrigin.x + static_cast<float>(g_viewportX) +
                     (ndc.x + 1.0f) * 0.5f * static_cast<float>(g_viewportWidth),
                 vpOrigin.y + static_cast<float>(g_viewportTopInset) +
                     (1.0f - ndc.y) * 0.5f *
                         static_cast<float>(g_viewportHeight));
    return true;
  };

  auto drawLabel = [&](const glm::vec3 &world, const std::string &text,
                       ImU32 color) {
    ImVec2 px;
    if (!project(world, px))
      return;
    const ImVec2 size = ImGui::CalcTextSize(text.c_str());
    const ImVec2 p0(px.x - size.x * 0.5f - 4.0f, px.y - size.y - 10.0f);
    const ImVec2 p1(p0.x + size.x + 8.0f, p0.y + size.y + 4.0f);
    drawList->AddRectFilled(p0, p1, IM_COL32(15, 15, 15, 200), 4.0f);
    drawList->AddText(ImVec2(p0.x + 4.0f, p0.y + 2.0f), color, text.c_str());
  };

  auto drawForMeasurement = [&](const Engine::Measurement &m) {
    const ImU32 color = ImGui::ColorConvertFloat4ToU32(
        ImVec4(m.color.r, m.color.g, m.color.b, 1.0f));
    char buf[96];
    switch (m.type) {
    case Engine::Measurement::Type::Angle:
      if (m.points.size() >= 3) {
        snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", m.angleDegrees());
        drawLabel(m.points[1], buf, color);
      }
      break;
    case Engine::Measurement::Type::Point:
      if (!m.points.empty()) {
        snprintf(buf, sizeof(buf), "(%.2f, %.2f, %.2f)", m.points[0].x,
                 m.points[0].y, m.points[0].z);
        drawLabel(m.points[0], buf, color);
      }
      break;
    case Engine::Measurement::Type::Area:
      if (m.points.size() >= 3) {
        drawLabel(m.centroid(),
                  measurementTool.formatArea(m.area()), color);
      }
      break;
    case Engine::Measurement::Type::Distance:
    default: {
      const size_t segments = m.points.size() >= 2 ? m.points.size() - 1 : 0;
      if (measurementTool.showSegmentLabels || segments == 1) {
        for (size_t i = 1; i < m.points.size(); i++) {
          const float len = glm::length(m.points[i] - m.points[i - 1]);
          drawLabel((m.points[i - 1] + m.points[i]) * 0.5f,
                    measurementTool.formatLength(len), color);
        }
      }
      if (segments > 1) {
        drawLabel(m.points.back(),
                  "Total " + measurementTool.formatLength(m.totalLength()),
                  color);
      }
      break;
    }
    }
  };

  if (measurements) {
    for (const auto &m : *measurements) {
      if (m.visible)
        drawForMeasurement(m);
    }
  }

  // In-progress measurement: same labels plus a live readout to the cursor
  if (measurementTool.hasActive()) {
    const auto &active = measurementTool.getActive();
    drawForMeasurement(active);

    glm::vec3 preview;
    if (measurementTool.getPreviewPoint(preview) && !active.points.empty()) {
      const ImU32 liveColor = IM_COL32(255, 255, 255, 230);
      if (active.type == Engine::Measurement::Type::Angle &&
          active.points.size() == 2) {
        // Live angle preview with the cursor as the third point
        Engine::Measurement tmp = active;
        tmp.points.push_back(preview);
        char buf[32];
        snprintf(buf, sizeof(buf), "%.1f\xC2\xB0", tmp.angleDegrees());
        drawLabel(tmp.points[1], buf, liveColor);
      } else if (active.type == Engine::Measurement::Type::Area &&
                 active.points.size() >= 2) {
        // Live area preview with the cursor closing the polygon.
        Engine::Measurement tmp = active;
        tmp.points.push_back(preview);
        drawLabel(tmp.centroid(),
                  measurementTool.formatArea(tmp.area()), liveColor);
      } else if (active.type == Engine::Measurement::Type::Distance) {
        const float len = glm::length(preview - active.points.back());
        drawLabel((active.points.back() + preview) * 0.5f,
                  measurementTool.formatLength(len), liveColor);
      }
    }
  }
}

void renderSunManipulationPanel() {
  const Engine::Sun sunPreFrame = sun;

  DrawPanelTitle(ICON_FA_SUN, "Sun");

  ImGui::Checkbox("Enabled", &sun.enabled);
  ImGui::ColorEdit3("Color", glm::value_ptr(sun.color));
  ImGui::SliderFloat("Intensity", &sun.intensity, 0.0f, 10.0f, "%.1f");

  ImGui::Spacing();

  // Edit the direction vector directly and re-normalize so the control always
  // reflects the live sun.direction (load, undo, or the settings panel).
  glm::vec3 dir = sun.direction;
  if (ImGui::DragFloat3("Direction", glm::value_ptr(dir), 0.01f, -1.0f, 1.0f,
                        "%.2f")) {
    if (glm::length(dir) > 1e-4f) {
      sun.direction = glm::normalize(dir);
    }
  }

  s_sunEditTracker.update(0, sunPreFrame, sun,
                          [](int, const Engine::Sun &before,
                             const Engine::Sun &after) {
                            Engine::Undo::recordSunEdit(before, after);
                          });
}

void renderModelManipulationPanel(Engine::Model &model,
                                  Engine::Shader *shader) {
  const Engine::Undo::ModelEditState modelStatePreFrame =
      Engine::Undo::ModelEditState::capture(model);

  DrawPanelTitle(ICON_FA_CUBE, model.name);

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool transformChanged = false;
    transformChanged |=
        ImGui::DragFloat3("Position", glm::value_ptr(model.position), 0.1f);
    transformChanged |=
        ImGui::DragFloat3("Rotation", glm::value_ptr(model.rotation), 1.0f,
                          -360.0f, 360.0f, "%.0f°");
    transformChanged |= ImGui::DragFloat3("Scale", glm::value_ptr(model.scale),
                                          0.01f, 0.01f, 100.0f);

    if (transformChanged) {
      updateSpaceMouseBounds();
    }

    DrawTransformGizmoControls(/*canRotateScale=*/true);
  }

  if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::ColorEdit3("Base Color", glm::value_ptr(model.color))) {
      // Auto-update F0 when color changes (if PBR is enabled)
      if (preferences.materialSettings.enablePBR) {
        model.F0 =
            glm::mix(glm::vec3(0.04f), model.color, model.metallicFactor);
      }
    }

    // PBR Material Properties
    if (preferences.materialSettings.enablePBR) {
      ImGui::Spacing();
      DrawSectionHeader("PBR Properties");

      if (ImGui::SliderFloat("Metallic", &model.metallicFactor, 0.0f, 1.0f,
                             "%.3f")) {
        // Auto-update F0 based on metallic workflow
        model.F0 =
            glm::mix(glm::vec3(0.04f), model.color, model.metallicFactor);
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "0.0 = Dielectric (plastic, wood)\n1.0 = Metal (iron, gold)");

      ImGui::SliderFloat("Roughness", &model.roughnessFactor, 0.0f, 1.0f,
                         "%.3f");
      ImGui::SameLine();
      DrawHelpMarker("0.0 = Mirror-like\n1.0 = Completely rough");

      ImGui::ColorEdit3("F0 (Base Reflectance)", glm::value_ptr(model.F0));
      ImGui::SameLine();
      DrawHelpMarker("Fresnel reflectance at normal incidence\nUsually 0.04 "
                     "for dielectrics, metal color for metals");

      ImGui::SliderFloat("Normal Intensity", &model.normalScale, 0.0f, 2.0f,
                         "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Intensity of normal mapping effect");

      ImGui::SliderFloat("Height Scale", &model.heightScale, 0.0f, 0.1f,
                         "%.4f");
      ImGui::SameLine();
      DrawHelpMarker("Scale for parallax mapping height");

    } else {
      // Traditional material properties
      ImGui::SliderFloat("Shininess", &model.shininess, 1.0f, 90.0f);
    }

    ImGui::SliderFloat("Emissive", &model.emissive, 0.0f, 1.0f);

    if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
      ImGui::Spacing();
      DrawSectionHeader("VCT Properties");

      static const char *material_types[] = {
          "Concrete", "Metal", "Plastic", "Glass", "Wood", "Marble", "Custom"};
      int current_type = static_cast<int>(model.materialType);

      if (ImGui::Combo("Preset", &current_type, material_types,
                       IM_ARRAYSIZE(material_types))) {
        model.applyMaterialPreset(static_cast<MaterialType>(current_type));
      }

      ImGui::SliderFloat("Diffuse", &model.diffuseReflectivity, 0.0f, 1.0f);
      ImGui::ColorEdit3("Specular", glm::value_ptr(model.specularColor));
      ImGui::SliderFloat("Reflectivity", &model.specularReflectivity, 0.0f,
                         1.0f);
      ImGui::SliderFloat("Glossiness", &model.specularDiffusion, 0.0f, 1.0f);
      ImGui::SliderFloat("IOR", &model.refractiveIndex, 1.0f, 3.0f);
      ImGui::SameLine();
      DrawHelpMarker("Index of Refraction:\n1.0 = Air\n1.33 = Water\n1.5 = "
                     "Glass\n2.4 = Diamond");
      ImGui::SliderFloat("Transparency", &model.transparency, 0.0f, 1.0f);
    }
  }

  if (ImGui::CollapsingHeader("Textures")) {
    if (!model.getMeshes().empty()) {
      const auto &mesh = model.getMeshes()[0];
      if (!mesh.textures.empty()) {
        ImGui::Text("Loaded:");
        for (const auto &texture : mesh.textures) {
          ImGui::BulletText("%s", texture.type.c_str());
        }
      } else {
        ImGui::TextDisabled("No textures loaded");
      }
    }

    ImGui::Spacing();

    auto textureLoadButton = [&](const char *label, const char *type) {
      if (ImGui::Button(label, ImVec2(-1, 0))) {
        auto selection =
            pfd::open_file("Select texture", ".",
                           {"Images", "*.png *.jpg *.jpeg *.bmp", "All", "*"})
                .result();
        if (!selection.empty()) {
          Texture texture;
          texture.fullPath = selection[0];
          texture.id = model.TextureFromFile(selection[0].c_str(), selection[0],
                                             texture.fullPath);
          texture.type = type;
          texture.path = selection[0];
          for (auto &mesh : model.getMeshes()) {
            mesh.textures.push_back(texture);
          }
        }
      }
    };

    textureLoadButton("Load Diffuse", "texture_diffuse");
    textureLoadButton("Load Normal", "texture_normal");
    textureLoadButton("Load Specular", "texture_specular");

    if (preferences.materialSettings.enablePBR) {
      ImGui::Spacing();
      ImGui::Text("PBR Textures:");
      textureLoadButton("Load Metallic", "texture_metallic");
      textureLoadButton("Load Roughness", "texture_roughness");
      textureLoadButton("Load AO", "texture_ao");
      textureLoadButton("Load Height", "texture_height");
    }
  }

  // Record property edits made above as one undo entry per gesture. Must run
  // before the delete handling below, which can invalidate `model`.
  s_modelEditTracker.update(
      currentSelectedIndex, modelStatePreFrame,
      Engine::Undo::ModelEditState::capture(model),
      [](int index, const Engine::Undo::ModelEditState &before,
         const Engine::Undo::ModelEditState &after) {
        Engine::Undo::recordModelEdit(index, before, after);
      });

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Model", ImVec2(-1, 0))) {
    ImGui::OpenPopup("Delete Model?");
  }

  if (ImGui::BeginPopupModal("Delete Model?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete '%s'?", model.name.c_str());
    ImGui::TextDisabled("This can be undone with Ctrl+Z.");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      deleteSelectedModel();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void renderMeshManipulationPanel(Engine::Model &model, int meshIndex,
                                 Engine::Shader *shader) {
  auto &mesh = model.getMeshes()[meshIndex];

  // Mesh materials are part of the model edit snapshot, so mesh-panel edits
  // share the model edit tracker (only one of the two panels is open at a
  // time).
  const Engine::Undo::ModelEditState modelStatePreFrame =
      Engine::Undo::ModelEditState::capture(model);

  DrawPanelTitle(ICON_FA_CUBE,
                 model.name + "  -  Mesh " + std::to_string(meshIndex + 1));

  ImGui::Checkbox("Visible", &mesh.visible);

  if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSectionHeader("Basic Properties");
    ImGui::ColorEdit3("Albedo Color", glm::value_ptr(mesh.color));
    ImGui::SliderFloat("Shininess", &mesh.shininess, 1.0f, 90.0f);
    ImGui::SliderFloat("Emissive", &mesh.emissive, 0.0f, 1.0f);

    if (preferences.materialSettings.enablePBR) {
      DrawSectionHeader("PBR Properties");

      // Add metallic and roughness sliders for this mesh
      static float meshMetallic = preferences.materialSettings.metallicFactor;
      static float meshRoughness = preferences.materialSettings.roughnessFactor;

      if (ImGui::SliderFloat("Metallic", &meshMetallic, 0.0f, 1.0f, "%.2f")) {
        // Update mesh metallic property (would need to be added to mesh
        // structure)
      }
      ImGui::SameLine();
      DrawHelpMarker("0 = Dielectric (plastic, wood), 1 = Metal");

      if (ImGui::SliderFloat("Roughness", &meshRoughness, 0.0f, 1.0f, "%.2f")) {
        // Update mesh roughness property (would need to be added to mesh
        // structure)
      }
      ImGui::SameLine();
      DrawHelpMarker("0 = Mirror-like, 1 = Completely rough");
    }

    if (preferences.materialSettings.enableNormalMapping) {
      DrawSectionHeader("Normal Mapping");
      static float normalScale = preferences.materialSettings.normalScale;
      if (ImGui::SliderFloat("Normal Intensity", &normalScale, 0.0f, 2.0f,
                             "%.2f")) {
        preferences.materialSettings.normalScale = normalScale;
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Strength of normal map effect");
    }

    if (preferences.materialSettings.enableParallaxMapping) {
      DrawSectionHeader("Parallax Mapping");
      static float heightScale = preferences.materialSettings.heightScale;
      if (ImGui::SliderFloat("Height Scale", &heightScale, 0.0f, 0.1f,
                             "%.4f")) {
        preferences.materialSettings.heightScale = heightScale;
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Depth of parallax displacement");
    }

    if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
      ImGui::Spacing();
      ImGui::TextDisabled("VCT properties are per-model only");
    }
  }

  if (ImGui::CollapsingHeader("Textures")) {
    if (!mesh.textures.empty()) {
      ImGui::Text("Loaded:");
      for (const auto &texture : mesh.textures) {
        ImGui::BulletText("%s", texture.type.c_str());
      }
    } else {
      ImGui::TextDisabled("No textures");
    }

    ImGui::Spacing();

    auto textureLoadButton = [&](const char *label, const char *type) {
      if (ImGui::Button(label, ImVec2(-1, 0))) {
        auto selection =
            pfd::open_file("Select texture", ".",
                           {"Images", "*.png *.jpg *.jpeg *.bmp", "All", "*"})
                .result();
        if (!selection.empty()) {
          Texture texture;
          texture.fullPath = selection[0];
          texture.id = model.TextureFromFile(selection[0].c_str(), selection[0],
                                             texture.fullPath);
          texture.type = type;
          texture.path = selection[0];
          mesh.textures.push_back(texture);
        }
      }
    };

    // Basic textures
    DrawSectionHeader("Basic Textures");
    textureLoadButton("Load Diffuse", "texture_diffuse");
    textureLoadButton("Load Specular", "texture_specular");

    // Enhanced textures
    DrawSectionHeader("Enhanced Textures");
    textureLoadButton("Load Normal Map", "texture_normal");
    textureLoadButton("Load Ambient Occlusion", "texture_ao");

    // PBR textures
    if (preferences.materialSettings.enablePBR) {
      DrawSectionHeader("PBR Textures");
      textureLoadButton("Load Metallic Map", "texture_metallic");
      textureLoadButton("Load Roughness Map", "texture_roughness");
    }

    // Advanced textures
    if (preferences.materialSettings.enableParallaxMapping) {
      DrawSectionHeader("Advanced Textures");
      textureLoadButton("Load Height Map", "texture_height");
    }
  }

  s_modelEditTracker.update(
      currentSelectedIndex, modelStatePreFrame,
      Engine::Undo::ModelEditState::capture(model),
      [](int index, const Engine::Undo::ModelEditState &before,
         const Engine::Undo::ModelEditState &after) {
        Engine::Undo::recordModelEdit(index, before, after);
      });

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Mesh", ImVec2(-1, 0))) {
    if (model.getMeshes().size() > 1) {
      ImGui::OpenPopup("Delete Mesh?");
    } else {
      ImGui::OpenPopup("Cannot Delete");
    }
  }

  if (ImGui::BeginPopupModal("Delete Mesh?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete this mesh?");
    ImGui::TextDisabled("This can be undone with Ctrl+Z.");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      Engine::Undo::deleteMesh(currentSelectedIndex, meshIndex);
      currentSelectedMeshIndex = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Cannot Delete", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Cannot delete the last mesh.");
    ImGui::Text("Delete the entire model instead.");
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void renderPointCloudManipulationPanel(Engine::PointCloud &pointCloud) {
  DrawPanelTitle(ICON_FA_CLOUD, pointCloud.name);

  // isLoaded() returns true when compute SSBOs are ready (numBatches > 0)
  // or when a legacy CPU-side points vector is still populated.
  if (!pointCloud.isLoaded()) {
    ImGui::TextDisabled("Point cloud is empty");
    return;
  }

  const Engine::Undo::PointCloudEditState pointCloudStatePreFrame =
      Engine::Undo::PointCloudEditState::capture(pointCloud);

  ImGui::Separator();

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool transformChanged = false;
    transformChanged |= ImGui::DragFloat3(
        "Position", glm::value_ptr(pointCloud.position), 0.1f);
    transformChanged |=
        ImGui::DragFloat3("Rotation", glm::value_ptr(pointCloud.rotation), 1.0f,
                          -360.0f, 360.0f, "%.0f°");
    transformChanged |= ImGui::DragFloat3(
        "Scale", glm::value_ptr(pointCloud.scale), 0.01f, 0.01f, 100.0f);

    if (transformChanged) {
      updateSpaceMouseBounds();
    }

    DrawTransformGizmoControls(/*canRotateScale=*/true);
  }

  if (ImGui::CollapsingHeader("Display Settings",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SliderFloat("Point Size", &pointCloud.basePointSize, 1.0f, 10.0f,
                       "%.1f");
    ImGui::SameLine();
    DrawHelpMarker("Base size of points in the cloud");
  }

  if (ImGui::CollapsingHeader("Info")) {
    ImGui::Text("Points: %s",
                [&]() -> std::string {
                  uint32_t n = pointCloud.totalPointCount;
                  if (n >= 1'000'000)
                    return std::to_string(n / 1'000'000) + "M";
                  if (n >= 1'000)
                    return std::to_string(n / 1'000) + "K";
                  return std::to_string(n);
                }()
                             .c_str());
    ImGui::Text("Batches: %u", pointCloud.numBatches);
    if (pointCloud.hasBounds()) {
      const glm::vec3 sz = pointCloud.boundsMax - pointCloud.boundsMin;
      ImGui::Text("Extent: %.2f x %.2f x %.2f m", sz.x, sz.y, sz.z);
    }
  }

  if (ImGui::CollapsingHeader("Export")) {
    static int exportFormat = 0;
    ImGui::RadioButton("XYZ", &exportFormat, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Binary (.pcb)", &exportFormat, 1);
    ImGui::SameLine();
    ImGui::RadioButton("HDF5", &exportFormat, 2);
    ImGui::SameLine();
    ImGui::RadioButton("PLY", &exportFormat, 3);

    static bool exportPlyBinary = true;
    if (exportFormat == 3) {
      ImGui::Checkbox("Binary PLY", &exportPlyBinary);
      ImGui::SameLine();
      DrawHelpMarker("Write a compact binary_little_endian PLY (recommended). "
                     "Disable for a human-readable ascii PLY.");
    }

    static bool exportApplyTransform = true;
    ImGui::Checkbox("Apply transform", &exportApplyTransform);
    ImGui::SameLine();
    DrawHelpMarker("Bake the cloud's position/rotation/scale into the "
                   "exported coordinates. Disable to export the raw "
                   "local-space points.");

    static std::string lastExportStatus;
    static bool lastExportOk = false;

    if (ImGui::Button("Export Point Cloud...", ImVec2(-1, 0))) {
      std::string defaultExt =
          (exportFormat == 0) ? ".xyz"
          : (exportFormat == 1) ? ".pcb"
          : (exportFormat == 2) ? ".h5"
                                : ".ply";
      auto destination = pfd::save_file("Export point cloud", ".",
                                        {"Point Cloud Files", "*" + defaultExt,
                                         "All Files", "*"})
                             .result();

      if (!destination.empty()) {
        bool success = false;
        if (exportFormat == 0) {
          success = Engine::PointCloudLoader::exportToXYZ(
              pointCloud, destination, exportApplyTransform);
        } else if (exportFormat == 1) {
          success = Engine::PointCloudLoader::exportToBinary(
              pointCloud, destination, exportApplyTransform);
        } else if (exportFormat == 2) {
          success = Engine::PointCloudLoader::exportToHDF5(
              pointCloud, destination, exportApplyTransform);
        } else {
          success = Engine::PointCloudLoader::exportToPLY(
              pointCloud, destination, exportApplyTransform, exportPlyBinary);
        }

        lastExportOk = success;
        if (success) {
          lastExportStatus = "Exported to " + destination;
          std::cout << "Point cloud exported successfully to " << destination
                    << std::endl;
        } else {
          lastExportStatus = "Export FAILED (see console)";
          std::cerr << "Failed to export point cloud to " << destination
                    << std::endl;
        }
      }
    }

    if (!lastExportStatus.empty()) {
      ImGui::PushStyleColor(ImGuiCol_Text,
                            lastExportOk ? ImVec4(0.4f, 0.9f, 0.4f, 1.0f)
                                         : ImVec4(0.95f, 0.4f, 0.4f, 1.0f));
      ImGui::TextWrapped("%s", lastExportStatus.c_str());
      ImGui::PopStyleColor();
    }
  }

  s_pointCloudEditTracker.update(
      currentSelectedIndex, pointCloudStatePreFrame,
      Engine::Undo::PointCloudEditState::capture(pointCloud),
      [](int index, const Engine::Undo::PointCloudEditState &before,
         const Engine::Undo::PointCloudEditState &after) {
        Engine::Undo::recordPointCloudEdit(index, before, after);
      });

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Point Cloud", ImVec2(-1, 0))) {
    ImGui::OpenPopup("Delete Point Cloud?");
  }

  if (ImGui::BeginPopupModal("Delete Point Cloud?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete '%s'?", pointCloud.name.c_str());
    ImGui::TextDisabled("This can be undone with Ctrl+Z.");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      deleteSelectedPointCloud();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void deleteSelectedModel() {
  if (currentSelectedType == SelectedType::Model && currentSelectedIndex >= 0 &&
      currentSelectedIndex < currentScene.models.size()) {
    Engine::Undo::deleteModel(currentSelectedIndex);
    currentSelectedIndex = -1;
    currentSelectedType = SelectedType::None;
    updateSpaceMouseBounds();

    // Mark voxelizer dirty for re-voxelization
    if (voxelizer) {
      voxelizer->markDirty();
    }
  }
}

void deleteSelectedPointCloud() {
  if (currentSelectedType == SelectedType::PointCloud &&
      currentSelectedIndex >= 0 &&
      currentSelectedIndex < currentScene.pointClouds.size()) {
    // The cloud (including its GL buffers) is kept alive by the undo entry
    // and freed when the entry is discarded from the history
    Engine::Undo::deletePointCloud(currentSelectedIndex);
    currentSelectedIndex = -1;
    currentSelectedType = SelectedType::None;
    updateSpaceMouseBounds();
  }
}

void renderPointLightManipulationPanel() {
  if (currentSelectedIndex >= pointLights.size()) {
    ImGui::Text("Error: Invalid light selection");
    return;
  }

  auto &light = pointLights[currentSelectedIndex];
  const Engine::PointLight lightStatePreFrame = light;

  DrawPanelTitle(ICON_FA_LIGHTBULB,
                 "Point Light " + std::to_string(currentSelectedIndex + 1));

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.1f);
    DrawTransformGizmoControls(/*canRotateScale=*/false);
  }

  if (ImGui::CollapsingHeader("Light Properties",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
    ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f, "%.1f");
    ImGui::SameLine();
    DrawHelpMarker("Brightness of the point light");

    // Cast Shadows toggle
    bool cast = light.castShadows;
    if (ImGui::Checkbox("Cast Shadows", &cast)) {
      light.castShadows = cast;
    }
    ImGui::SameLine();
    DrawHelpMarker(
        "If enabled, this light renders a depth cubemap and casts shadows");
  }

  s_pointLightEditTracker.update(
      currentSelectedIndex, lightStatePreFrame, light,
      [](int index, const Engine::PointLight &before,
         const Engine::PointLight &after) {
        Engine::Undo::recordPointLightEdit(index, before, after);
      });

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Light", ImVec2(-1, 0))) {
    ImGui::OpenPopup("Delete Light?");
  }

  if (ImGui::BeginPopupModal("Delete Light?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete this point light?");
    ImGui::TextDisabled("This can be undone with Ctrl+Z.");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      Engine::Undo::deletePointLight(currentSelectedIndex);
      currentSelectedIndex = -1;
      currentSelectedType = SelectedType::None;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void renderSpotLightManipulationPanel() {
  if (currentSelectedIndex >= spotLights.size()) {
    ImGui::Text("Error: Invalid light selection");
    return;
  }

  auto &light = spotLights[currentSelectedIndex];
  const Engine::SpotLight lightStatePreFrame = light;

  DrawPanelTitle(ICON_FA_BULLSEYE,
                 "Spot Light " + std::to_string(currentSelectedIndex + 1));

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.1f);
    ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.01f,
                      -1.0f, 1.0f);

    if (ImGui::Button("Normalize Direction", ImVec2(-1, 0))) {
      light.direction = glm::normalize(light.direction);
    }
    ImGui::SameLine();
    DrawHelpMarker("Make direction vector unit length");
    DrawTransformGizmoControls(/*canRotateScale=*/false);
  }

  if (ImGui::CollapsingHeader("Light Properties",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
    ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f, "%.1f");

    DrawSectionHeader("Cone Settings");

    float innerAngle = glm::degrees(glm::acos(light.innerCutOff));
    float outerAngle = glm::degrees(glm::acos(light.outerCutOff));

    if (ImGui::SliderFloat("Inner Angle", &innerAngle, 0.0f, 89.0f, "%.1f°")) {
      light.innerCutOff = glm::cos(glm::radians(innerAngle));
    }
    ImGui::SameLine();
    DrawHelpMarker("Angle of the bright inner cone");

    if (ImGui::SliderFloat("Outer Angle", &outerAngle, 0.0f, 89.0f, "%.1f°")) {
      light.outerCutOff = glm::cos(glm::radians(outerAngle));
    }
    ImGui::SameLine();
    DrawHelpMarker("Angle of the soft falloff cone");

    // Ensure inner angle is always smaller than outer angle
    if (innerAngle > outerAngle) {
      light.outerCutOff = light.innerCutOff;
    }

    // Cast Shadows toggle for spot light (future use if spot shadows are
    // implemented)
    bool cast = light.castShadows;
    if (ImGui::Checkbox("Cast Shadows", &cast)) {
      light.castShadows = cast;
    }
    ImGui::SameLine();
    DrawHelpMarker(
        "Toggle whether this spot light should cast shadows (when supported)");
  }

  s_spotLightEditTracker.update(
      currentSelectedIndex, lightStatePreFrame, light,
      [](int index, const Engine::SpotLight &before,
         const Engine::SpotLight &after) {
        Engine::Undo::recordSpotLightEdit(index, before, after);
      });

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Light", ImVec2(-1, 0))) {
    ImGui::OpenPopup("Delete Light?");
  }

  if (ImGui::BeginPopupModal("Delete Light?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete this spot light?");
    ImGui::TextDisabled("This can be undone with Ctrl+Z.");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      Engine::Undo::deleteSpotLight(currentSelectedIndex);
      currentSelectedIndex = -1;
      currentSelectedType = SelectedType::None;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}