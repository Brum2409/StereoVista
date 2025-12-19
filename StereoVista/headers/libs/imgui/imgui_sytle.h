#pragma once
#include "Engine/Core.h"
#include <filesystem>
#include <unordered_map>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <imgui/imgui_internal.h>

using namespace Engine;

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
    ImFont* small = nullptr;
    ImFont* mono = nullptr;
    ImFont* icons = nullptr;
};

// Forward declaration
struct GuiScaleSettings;

// Style presets
enum class GuiStylePreset {
    MODERN_DARK,
    MODERN_LIGHT,
    HIGH_CONTRAST,
    MINIMAL
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

// Function declarations
bool InitializeImGuiWithFonts(GLFWwindow* window, bool isDarkTheme);
void UpdateGuiScale(int windowWidth, int windowHeight);
float CalculateGuiScale(int windowWidth, int windowHeight);
void RescaleImGuiFonts(GLFWwindow* window, bool isDarkTheme);
void RebuildImGuiFontAtlas(bool isDarkTheme);
void ApplyStylePreset(GuiStylePreset preset);