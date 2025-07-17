#include "imgui_sytle.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <iostream>

// Define the scaling settings struct here to avoid compilation issues
struct GuiScaleSettings {
    float currentScale = 1.0f;
    int lastWindowWidth = 0;
    int lastWindowHeight = 0;
    bool needsRescale = false;
    
    static constexpr float MIN_SCALE = 0.5f;
    static constexpr float MAX_SCALE = 2.0f;
    static constexpr int MIN_WINDOW_WIDTH = 800;
    static constexpr int MIN_WINDOW_HEIGHT = 600;
    static constexpr int REFERENCE_WIDTH = 1920;
    static constexpr int REFERENCE_HEIGHT = 1080;
};

// Move the implementation of the function here
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

    // Apply scaling to font sizes
    float scale = g_GuiScale.currentScale;
    defaultFontSize *= scale;
    headerFontSize *= scale;
    smallFontSize *= scale;
    
    // Try to load fonts
    for (const char* fontPath : fontPaths) {
        if (std::filesystem::exists(fontPath)) {
            try {
                g_Fonts.regular = io.Fonts->AddFontFromFileTTF(fontPath, defaultFontSize, &fontConfig);
                g_Fonts.header = io.Fonts->AddFontFromFileTTF(fontPath, headerFontSize, &fontConfig);
                g_Fonts.small = io.Fonts->AddFontFromFileTTF(fontPath, smallFontSize, &fontConfig);

                if (g_Fonts.regular && g_Fonts.header && g_Fonts.small) {
                    fontLoaded = true;
                    std::cout << "Successfully loaded font: " << fontPath << " with scale " << scale << std::endl;
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

// Define the g_Fonts variable here
ImGuiFonts g_Fonts;
GuiScaleSettings g_GuiScale;

float CalculateGuiScale(int windowWidth, int windowHeight) {
    // Calculate scale based on window dimensions relative to reference resolution
    float widthRatio = static_cast<float>(windowWidth) / 1920.0f;
    float heightRatio = static_cast<float>(windowHeight) / 1080.0f;
    
    // Use the smaller ratio to ensure UI fits within the window
    float scale = std::min(widthRatio, heightRatio);
    
    // Clamp the scale to reasonable bounds
    scale = std::max(0.5f, std::min(2.0f, scale));
    
    return scale;
}

void UpdateGuiScale(int windowWidth, int windowHeight) {
    // Ignore invalid window dimensions
    if (windowWidth <= 0 || windowHeight <= 0) {
        std::cout << "Invalid window dimensions, ignoring" << std::endl;
        return;
    }
    
    // Calculate new scale
    float newScale = CalculateGuiScale(windowWidth, windowHeight);
    
    // Always update on first call (when lastWindowWidth/Height are 0)
    bool isFirstCall = (g_GuiScale.lastWindowWidth == 0 && g_GuiScale.lastWindowHeight == 0);
    
    // Only update if window dimensions changed significantly OR it's the first call
    if (isFirstCall || 
        abs(windowWidth - g_GuiScale.lastWindowWidth) > 50 || 
        abs(windowHeight - g_GuiScale.lastWindowHeight) > 50) {
        
        // Only rescale if the scale change is significant OR it's the first call
        if (isFirstCall || fabs(newScale - g_GuiScale.currentScale) > 0.05f) {
            g_GuiScale.currentScale = newScale;
            g_GuiScale.needsRescale = true;
        } else {
        }
        
        g_GuiScale.lastWindowWidth = windowWidth;
        g_GuiScale.lastWindowHeight = windowHeight;
    } else {
    }
}

void RescaleImGuiFonts(GLFWwindow* window, bool isDarkTheme) {
    if (!g_GuiScale.needsRescale) return;
    
    // Skip rescaling if window or context is invalid
    if (!window || !ImGui::GetCurrentContext()) {
        std::cout << "Invalid window or ImGui context, skipping rescale" << std::endl;
        g_GuiScale.needsRescale = false;
        return;
    }
    
    
    // For now, just update the style without clearing fonts to avoid crashes
    // TODO: Implement proper font rescaling in a safer way
    SetupImGuiStyle(isDarkTheme, 1.0f);
    
    g_GuiScale.needsRescale = false;
}

void SetupImGuiStyle(bool bStyleDark_, float alpha_) {
    ImGuiStyle& style = ImGui::GetStyle();

    // Apply scaling to style parameters
    float scale = g_GuiScale.currentScale;
    std::cout << "SetupImGuiStyle called with scale: " << scale << std::endl;
    
    // Set up modern style parameters with scaling
    style.Alpha = 1.0f;
    style.DisabledAlpha = 0.6f;
    style.WindowPadding = ImVec2(12 * scale, 12 * scale);
    style.WindowRounding = 8.0f * scale;
    style.WindowBorderSize = 1.0f * scale;
    style.WindowMinSize = ImVec2(32 * scale, 32 * scale);
    style.WindowTitleAlign = ImVec2(0.5f, 0.5f);  // Center window titles
    style.WindowMenuButtonPosition = ImGuiDir_Right;  // More modern position
    style.ChildRounding = 8.0f * scale;
    style.ChildBorderSize = 1.0f * scale;
    style.PopupRounding = 8.0f * scale;
    style.PopupBorderSize = 1.0f * scale;
    style.FramePadding = ImVec2(6 * scale, 4 * scale);
    style.FrameRounding = 6.0f * scale;  // More rounded corners
    style.FrameBorderSize = 0.0f;
    style.ItemSpacing = ImVec2(8 * scale, 6 * scale);
    style.ItemInnerSpacing = ImVec2(6 * scale, 4 * scale);
    style.CellPadding = ImVec2(4 * scale, 2 * scale);
    style.IndentSpacing = 25.0f * scale;
    style.ColumnsMinSpacing = 6.0f * scale;
    style.ScrollbarSize = 12.0f * scale;
    style.ScrollbarRounding = 6.0f * scale;
    style.GrabMinSize = 12.0f * scale;
    style.GrabRounding = 6.0f * scale;
    style.TabRounding = 6.0f * scale;
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

        // Color palette:
        // F0F5F9 - Very light grayish blue (background)
        // C9D6DF - Light grayish blue (elements, secondary surfaces)
        // 52616B - Dark grayish blue (text, accents)
        // 1E2022 - Very dark gray (primary text)

        const ImVec4 bgColor = ImVec4(0.941f, 0.961f, 0.976f, 1.00f);         // F0F5F9
        const ImVec4 elementColor = ImVec4(0.788f, 0.839f, 0.875f, 1.00f);    // C9D6DF
        const ImVec4 accentColor = ImVec4(0.322f, 0.380f, 0.420f, 1.00f);     // 52616B
        const ImVec4 textColor = ImVec4(0.118f, 0.125f, 0.133f, 1.00f);       // 1E2022

        const ImVec4 accentHovered = ImVec4(0.369f, 0.435f, 0.482f, 1.00f);   // 52616B lighter
        const ImVec4 accentActive = ImVec4(0.275f, 0.325f, 0.357f, 1.00f);    // 52616B darker

        // Background colors
        colors[ImGuiCol_WindowBg] = ImVec4(bgColor.x, bgColor.y, bgColor.z, 0.95f);
        colors[ImGuiCol_ChildBg] = ImVec4(bgColor.x, bgColor.y, bgColor.z, 0.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(bgColor.x, bgColor.y, bgColor.z, 0.94f);
        colors[ImGuiCol_Border] = ImVec4(elementColor.x, elementColor.y, elementColor.z, 0.50f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Text colors
        colors[ImGuiCol_Text] = textColor;
        colors[ImGuiCol_TextDisabled] = ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.65f);

        // Headers
        colors[ImGuiCol_Header] = ImVec4(elementColor.x, elementColor.y, elementColor.z, 0.80f);
        colors[ImGuiCol_HeaderHovered] = accentHovered;
        colors[ImGuiCol_HeaderActive] = accentActive;

        // Buttons
        colors[ImGuiCol_Button] = elementColor;
        colors[ImGuiCol_ButtonHovered] = accentHovered;
        colors[ImGuiCol_ButtonActive] = accentActive;

        // Frame backgrounds
        colors[ImGuiCol_FrameBg] = ImVec4(elementColor.x, elementColor.y, elementColor.z, 0.60f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(elementColor.x * 1.1f, elementColor.y * 1.1f, elementColor.z * 1.1f, 0.60f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(elementColor.x * 1.2f, elementColor.y * 1.2f, elementColor.z * 1.2f, 0.60f);

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4(elementColor.x, elementColor.y, elementColor.z, 0.86f);
        colors[ImGuiCol_TabHovered] = accentHovered;
        colors[ImGuiCol_TabActive] = accentActive;
        colors[ImGuiCol_TabUnfocused] = ImVec4(elementColor.x * 0.9f, elementColor.y * 0.9f, elementColor.z * 0.9f, 0.97f);
        colors[ImGuiCol_TabUnfocusedActive] = ImVec4(elementColor.x, elementColor.y, elementColor.z, 1.00f);

        // Title
        colors[ImGuiCol_TitleBg] = elementColor;
        colors[ImGuiCol_TitleBgActive] = ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.8f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(elementColor.x, elementColor.y, elementColor.z, 0.75f);
        colors[ImGuiCol_MenuBarBg] = ImVec4(elementColor.x * 0.9f, elementColor.y * 0.9f, elementColor.z * 0.9f, 1.00f);

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg] = ImVec4(elementColor.x, elementColor.y, elementColor.z, 0.60f);
        colors[ImGuiCol_ScrollbarGrab] = accentColor;
        colors[ImGuiCol_ScrollbarGrabHovered] = accentHovered;
        colors[ImGuiCol_ScrollbarGrabActive] = accentActive;

        // Slider/CheckBox
        colors[ImGuiCol_CheckMark] = accentColor;
        colors[ImGuiCol_SliderGrab] = accentColor;
        colors[ImGuiCol_SliderGrabActive] = accentActive;

        // Interactive elements
        colors[ImGuiCol_Separator] = ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.50f);
        colors[ImGuiCol_SeparatorHovered] = accentHovered;
        colors[ImGuiCol_SeparatorActive] = accentActive;
        colors[ImGuiCol_ResizeGrip] = ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.20f);
        colors[ImGuiCol_ResizeGripHovered] = accentHovered;
        colors[ImGuiCol_ResizeGripActive] = accentActive;

        // Plot/Visual elements
        colors[ImGuiCol_PlotLines] = textColor;
        colors[ImGuiCol_PlotLinesHovered] = accentHovered;
        colors[ImGuiCol_PlotHistogram] = accentColor;
        colors[ImGuiCol_PlotHistogramHovered] = accentHovered;

        // Miscellaneous
        colors[ImGuiCol_TextSelectedBg] = ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.35f);
        colors[ImGuiCol_DragDropTarget] = accentColor;
        colors[ImGuiCol_NavHighlight] = accentColor;
        colors[ImGuiCol_NavWindowingHighlight] = ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.80f);
        colors[ImGuiCol_NavWindowingDimBg] = ImVec4(accentColor.x, accentColor.y, accentColor.z, 0.20f);
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