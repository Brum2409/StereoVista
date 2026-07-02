#pragma once
#include <filesystem>
#include <unordered_map>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_vulkan.h"
#include <imgui/imgui_internal.h>

struct GLFWwindow;

// Function declarations
void SetupImGuiStyle(bool bStyleDark_, float alpha_);
void ApplyCustomWidgetStyles();
void PushSectionHeaderStyle();
void PopSectionHeaderStyle();
void PushCompactStyle();
void PopCompactStyle();

struct ImGuiFonts {
    ImFont* regular = nullptr;
    ImFont* bold = nullptr;
    ImFont* header = nullptr;
    ImFont* smallFont = nullptr;
    ImFont* mono = nullptr;
    ImFont* icons = nullptr;
};

// GUI Scale Settings
struct GuiScaleSettings {
  float currentScale = 1.0f;
  float userScaleFactor = 1.0f;  // User-controlled multiplier on top of window-based scale
  int lastWindowWidth = 0;
  int lastWindowHeight = 0;
  bool needsRescale = false;
  bool needsFontRebuild = false;
  float lastFontScale = 0.0f;

  static constexpr float MIN_SCALE = 0.5f;
  static constexpr float MAX_SCALE = 2.0f;
  static constexpr float MIN_USER_FACTOR = 0.5f;
  static constexpr float MAX_USER_FACTOR = 2.0f;
  static constexpr int MIN_WINDOW_WIDTH = 800;
  static constexpr int MIN_WINDOW_HEIGHT = 600;
  static constexpr int REFERENCE_WIDTH = 1920;
  static constexpr int REFERENCE_HEIGHT = 1080;
};

// Style presets
enum class GuiStylePreset {
    MODERN_DARK,
    MODERN_LIGHT,
    HIGH_CONTRAST,
    MINIMAL
};

// Selectable color themes. The index of each entry doubles as the value
// persisted in preferences ("ui.theme"), so only ever append new themes to the
// end -- never reorder or remove -- to keep saved preferences stable.
enum GuiTheme {
    GUI_THEME_MODERN_DARK = 0,
    GUI_THEME_MODERN_LIGHT,
    GUI_THEME_SCHNEIDER_DIGITAL,
    GUI_THEME_NORD,
    GUI_THEME_FOREST,
    GUI_THEME_LAGOON,
    GUI_THEME_AMETHYST,
    GUI_THEME_COUNT
};

// Custom style colors
struct CustomStyleColors {
    ImVec4 primary;
    ImVec4 primaryHover;
    ImVec4 primaryActive;
    ImVec4 secondary;
    ImVec4 accent;
    ImVec4 success;
    ImVec4 warning;
    ImVec4 danger;
    ImVec4 info;
};

// Declare as extern
extern ImGuiFonts g_Fonts;
extern GuiScaleSettings g_GuiScale;
extern CustomStyleColors g_StyleColors;
// Index (GuiTheme) of the theme currently applied. Authoritative for restyles
// triggered by GUI-scale changes, so a custom theme survives a window resize.
extern int g_currentTheme;

// Function declarations
bool InitializeImGuiWithFonts(GLFWwindow* window, bool isDarkTheme);
void UpdateGuiScale(int windowWidth, int windowHeight);
float CalculateGuiScale(int windowWidth, int windowHeight);
void RescaleImGuiFonts(GLFWwindow* window, bool isDarkTheme);
void RebuildImGuiFontAtlas(bool isDarkTheme);
void ApplyStylePreset(GuiStylePreset preset);

// Theme system. ApplyGuiTheme() applies a full color theme (and the shared
// geometry/spacing) and records it as the current theme. The helpers expose
// theme metadata for building the picker UI.
void ApplyGuiTheme(int theme, float alpha);
int GetGuiThemeCount();
const char *GetGuiThemeName(int theme);
bool IsGuiThemeDark(int theme);
// Fills up to `maxColors` swatch colors (background, accent, two text/surface
// tones) for a compact preview in the picker. Returns the count written.
int GetGuiThemeSwatches(int theme, ImVec4 *out, int maxColors);