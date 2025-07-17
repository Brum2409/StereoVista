#pragma once
#include "Engine/Core.h"
#include <filesystem>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <imgui/imgui_internal.h>

using namespace Engine;

// Function declaration only
void SetupImGuiStyle(bool bStyleDark_, float alpha_);

struct ImGuiFonts {
    ImFont* regular = nullptr;
    ImFont* header = nullptr;
    ImFont* small = nullptr;
};

// Forward declaration
struct GuiScaleSettings;

// Declare as extern
extern ImGuiFonts g_Fonts;
extern GuiScaleSettings g_GuiScale;

// Function declarations
bool InitializeImGuiWithFonts(GLFWwindow* window, bool isDarkTheme);
void UpdateGuiScale(int windowWidth, int windowHeight);
float CalculateGuiScale(int windowWidth, int windowHeight);
void RescaleImGuiFonts(GLFWwindow* window, bool isDarkTheme);