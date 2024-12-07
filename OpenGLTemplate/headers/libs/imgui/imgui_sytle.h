//imgui/imgui_style.h
#include "core.h"
#include <filesystem>

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <imgui/imgui_internal.h>

using namespace Engine;


inline void SetupImGuiStyle(bool bStyleDark_, float alpha_) {
    ImGuiStyle& style = ImGui::GetStyle();

    // Set up modern style parameters
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.6f;
    style.WindowPadding = ImVec2(12, 12);
    style.WindowRounding = 8.0f;
    style.WindowBorderSize = 1.0f;
    style.WindowMinSize = ImVec2(32, 32);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);  // Center window titles
    style.WindowMenuButtonPosition = ImGuiDir_Right;  // More modern position
    style.ChildRounding = 8.0f;
    style.ChildBorderSize = 1.0f;
    style.PopupRounding = 8.0f;
    style.PopupBorderSize = 1.0f;
    style.FramePadding = ImVec2(6, 4);
    style.FrameRounding = 6.0f;  // More rounded corners
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8, 6);
    style.ItemInnerSpacing = ImVec2(6, 4);
    style.CellPadding = ImVec2(4, 2);
    style.IndentSpacing = 25.0f;
    style.ColumnsMinSpacing = 6.0f;
    style.ScrollbarSize = 12.0f;
    style.ScrollbarRounding = 6.0f;
    style.GrabMinSize = 12.0f;
    style.GrabRounding = 6.0f;
    style.TabRounding = 6.0f;
    style.TabBorderSize = 0.0f;  
    style.ColorButtonPosition = ImGuiDir_Right;
    style.ButtonTextAlign = ImVec2(0.5f, 0.5f);
    style.SelectableTextAlign = ImVec2(0.0f, 0.0f);

    // Modern dark theme colors
    if (bStyleDark_) {
        ImVec4* colors = style.Colors;

        // Background colors
        colors[ImGuiCol_WindowBg] = ImVec4(0.15f, 0.16f, 0.17f, 0.95f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.15f, 0.16f, 0.17f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.15f, 0.16f, 0.17f, 0.94f);
        colors[ImGuiCol_Border] = ImVec4(0.25f, 0.26f, 0.27f, 0.50f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Text colors
        colors[ImGuiCol_Text] = ImVec4(0.95f, 0.96f, 0.98f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.36f, 0.42f, 0.47f, 1.00f);

        // Interactive element colors
        const ImVec4 accentColor = ImVec4(0.28f, 0.56f, 1.00f, 1.00f);  // Modern blue accent
        const ImVec4 accentHovered = ImVec4(0.38f, 0.66f, 1.00f, 1.00f);
        const ImVec4 accentActive = ImVec4(0.18f, 0.46f, 0.90f, 1.00f);

        // Headers
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.25f, 0.29f, 0.55f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.26f, 0.31f, 0.35f, 0.80f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.24f, 0.29f, 0.33f, 1.00f);

        // Buttons
        colors[ImGuiCol_Button] = ImVec4(0.20f, 0.21f, 0.22f, 0.90f);
        colors[ImGuiCol_ButtonHovered] = accentColor;
        colors[ImGuiCol_ButtonActive] = accentActive;

        // Frame backgrounds
        colors[ImGuiCol_FrameBg] = ImVec4(0.20f, 0.21f, 0.22f, 0.54f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.25f, 0.26f, 0.27f, 0.54f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.31f, 0.32f, 0.54f);

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4(0.15f, 0.16f, 0.17f, 0.86f);
        colors[ImGuiCol_TabHovered] = accentColor;
        colors[ImGuiCol_TabActive] = accentActive;
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.15f, 0.16f, 0.17f, 0.97f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.20f, 0.21f, 0.22f, 1.00f);

        // Title
        colors[ImGuiCol_TitleBg] = ImVec4(0.15f, 0.16f, 0.17f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.15f, 0.16f, 0.17f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.15f, 0.16f, 0.17f, 0.75f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.18f, 0.19f, 0.20f, 1.00f);

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.15f, 0.16f, 0.17f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.30f, 0.31f, 0.32f, 0.80f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.35f, 0.36f, 0.37f, 0.80f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.40f, 0.41f, 0.42f, 0.80f);

        // Slider/CheckBox
        colors[ImGuiCol_CheckMark] = accentColor;
        colors[ImGuiCol_SliderGrab] = accentColor;
        colors[ImGuiCol_SliderGrabActive] = accentActive;

        // Interactive elements
        colors[ImGuiCol_Separator] = ImVec4(0.25f, 0.26f, 0.27f, 0.50f);
        colors[ImGuiCol_SeparatorHovered] = accentColor;
        colors[ImGuiCol_SeparatorActive] = accentActive;
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.25f, 0.26f, 0.27f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered] = accentColor;
        colors[ImGuiCol_ResizeGripActive] = accentActive;

        // Plot/Visual elements
        colors[ImGuiCol_PlotLines] = ImVec4(0.61f, 0.61f, 0.61f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = accentColor;
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.90f, 0.70f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(1.00f, 0.60f, 0.00f, 1.00f);

        // Miscellaneous
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.28f, 0.56f, 1.00f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = accentColor;
        colors[ImGuiCol_NavHighlight] = accentColor;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(1.00f, 1.00f, 1.00f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.80f, 0.80f, 0.80f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
    }
    else {
        ImVec4* colors = style.Colors;

        // Background colors - keeping these muted
        colors[ImGuiCol_WindowBg] = ImVec4(0.90f, 0.90f, 0.90f, 0.95f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.90f, 0.90f, 0.90f, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.90f, 0.90f, 0.90f, 0.94f);
        colors[ImGuiCol_Border] = ImVec4(0.60f, 0.60f, 0.60f, 0.50f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Text colors - darker for better contrast
        colors[ImGuiCol_Text] = ImVec4(0.15f, 0.15f, 0.15f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.45f, 0.45f, 0.45f, 1.00f);

        // Interactive element colors - Darker blue
        const ImVec4 accentColor = ImVec4(0.20f, 0.45f, 0.80f, 1.00f);
        const ImVec4 accentHovered = ImVec4(0.30f, 0.55f, 0.90f, 1.00f);
        const ImVec4 accentActive = ImVec4(0.15f, 0.40f, 0.75f, 1.00f);

        // Headers - darker
        colors[ImGuiCol_Header] = ImVec4(0.70f, 0.70f, 0.70f, 0.55f);
        colors[ImGuiCol_HeaderHovered] = accentHovered;
        colors[ImGuiCol_HeaderActive] = accentActive;

        // Buttons - darker
        colors[ImGuiCol_Button] = ImVec4(0.75f, 0.75f, 0.75f, 0.90f);
        colors[ImGuiCol_ButtonHovered] = accentHovered;
        colors[ImGuiCol_ButtonActive] = accentActive;

        // Frame backgrounds - darker
        colors[ImGuiCol_FrameBg] = ImVec4(0.72f, 0.72f, 0.72f, 0.54f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.77f, 0.77f, 0.77f, 0.54f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.80f, 0.80f, 0.80f, 0.54f);

        // Tabs - darker
        colors[ImGuiCol_Tab] = ImVec4(0.72f, 0.72f, 0.72f, 0.86f);
        colors[ImGuiCol_TabHovered] = accentHovered;
        colors[ImGuiCol_TabActive] = accentActive;
        colors[ImGuiCol_TabUnfocused] = ImVec4(0.75f, 0.75f, 0.75f, 0.97f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(0.72f, 0.72f, 0.72f, 1.00f);

        // Title - darker
        colors[ImGuiCol_TitleBg] = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.65f, 0.65f, 0.65f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.72f, 0.72f, 0.72f, 0.75f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.68f, 0.68f, 0.68f, 1.00f);

        // Scrollbar - darker
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.70f, 0.70f, 0.70f, 0.60f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.55f, 0.55f, 0.55f, 0.80f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.50f, 0.50f, 0.50f, 0.80f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.45f, 0.45f, 0.45f, 0.80f);

        // Slider/CheckBox
        colors[ImGuiCol_CheckMark] = accentColor;
        colors[ImGuiCol_SliderGrab] = accentColor;
        colors[ImGuiCol_SliderGrabActive] = accentActive;

        // Interactive elements - darker
        colors[ImGuiCol_Separator] = ImVec4(0.60f, 0.60f, 0.60f, 0.50f);
        colors[ImGuiCol_SeparatorHovered] = accentHovered;
        colors[ImGuiCol_SeparatorActive] = accentActive;
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.60f, 0.60f, 0.60f, 0.20f);
        colors[ImGuiCol_ResizeGripHovered] = accentHovered;
        colors[ImGuiCol_ResizeGripActive] = accentActive;

        // Plot/Visual elements - darker
        colors[ImGuiCol_PlotLines] = ImVec4(0.35f, 0.35f, 0.35f, 1.00f);
        colors[ImGuiCol_PlotLinesHovered] = accentHovered;
        colors[ImGuiCol_PlotHistogram] = ImVec4(0.70f, 0.50f, 0.00f, 1.00f);
        colors[ImGuiCol_PlotHistogramHovered] = ImVec4(0.80f, 0.40f, 0.00f, 1.00f);

        // Miscellaneous - adjusted
        colors[ImGuiCol_TextSelectedBg] = ImVec4(0.20f, 0.45f, 0.80f, 0.35f);
        colors[ImGuiCol_DragDropTarget] = accentColor;
        colors[ImGuiCol_NavHighlight] = accentColor;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(0.20f, 0.20f, 0.20f, 0.70f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.20f);
        colors[ImGuiCol_ModalWindowDimBg] = ImVec4(0.20f, 0.20f, 0.20f, 0.35f);
    }

    // Apply alpha to colors if needed
    if (alpha_ < 1.0f) {
        ImVec4* colors = style.Colors;
        for (int i = 0; i < ImGuiCol_COUNT; i++) {
            colors[i].w *= alpha_;
        }
    }
}

struct ImGuiFonts {
    ImFont* regular = nullptr;
    ImFont* header = nullptr;
    ImFont* small = nullptr;
} g_Fonts;

// Function to initialize ImGui with fonts
bool InitializeImGuiWithFonts(GLFWwindow* window, bool isDarkTheme) {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;

    // Initialize GLFW and OpenGL backends
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");

    // Configure style using the global theme setting
    SetupImGuiStyle(isDarkTheme, 1.0f);

    // Font configuration
    ImFontConfig fontConfig;
    fontConfig.OversampleH = 2;
    fontConfig.OversampleV = 2;
    fontConfig.PixelSnapH = true;

    // Default font paths to try
    const char* fontPaths[] = {
        "C:\\Windows\\Fonts\\segoeui.ttf",
        "C:\\Windows\\Fonts\\arial.ttf",
        "C:\\Windows\\Fonts\\tahoma.ttf"
    };

    bool fontLoaded = false;
    float defaultFontSize = 18.0f;
    float headerFontSize = 20.0f;
    float smallFontSize = 14.0f;

    // Try to load fonts
    for (const char* fontPath : fontPaths) {
        if (std::filesystem::exists(fontPath)) {
            try {
                g_Fonts.regular = io.Fonts->AddFontFromFileTTF(fontPath, defaultFontSize, &fontConfig);
                g_Fonts.header = io.Fonts->AddFontFromFileTTF(fontPath, headerFontSize, &fontConfig);
                g_Fonts.small = io.Fonts->AddFontFromFileTTF(fontPath, smallFontSize, &fontConfig);

                if (g_Fonts.regular && g_Fonts.header && g_Fonts.small) {
                    fontLoaded = true;
                    std::cout << "Successfully loaded font: " << fontPath << std::endl;
                    break;
                }
            }
            catch (const std::exception& e) {
                std::cerr << "Failed to load font " << fontPath << ": " << e.what() << std::endl;
            }
        }
    }

    // Fallback to default font if needed
    if (!fontLoaded) {
        std::cout << "Using ImGui default font" << std::endl;
        g_Fonts.regular = io.Fonts->AddFontDefault();
        g_Fonts.header = io.Fonts->AddFontDefault();
        g_Fonts.small = io.Fonts->AddFontDefault();
    }

    // Build font atlas
    io.Fonts->Build();

    return true;
}
