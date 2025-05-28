#pragma once
#include "imgui_sytle.h"

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

// Define the g_Fonts variable here
ImGuiFonts g_Fonts;