#include "core.h"

#include "imgui/imgui.h"
#include "imgui/backends/imgui_impl_glfw.h"
#include "imgui/backends/imgui_impl_opengl3.h"
#include <imgui/imgui_internal.h>

using namespace Engine;


inline void SetupImGuiStyle(bool bStyleDark_, float alpha_)
{
    ImGuiStyle& style = ImGui::GetStyle();

    // General rounding for a modern look
    style.FrameRounding = 4.0f;
    style.WindowRounding = 6.0f;
    style.GrabRounding = 4.0f;
    style.ScrollbarRounding = 4.0f;
    style.ChildRounding = 4.0f;

    style.ItemSpacing = ImVec2(8.0f, 6.0f);  // More space between elements
    style.ScaleAllSizes(1.8f);  // Slightly smaller scaling for a more compact look

    // Neutral, light colors for the overall background with soft accents
    style.Colors[ImGuiCol_Text] = ImVec4(0.10f, 0.10f, 0.10f, 1.00f);
    style.Colors[ImGuiCol_TextDisabled] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    style.Colors[ImGuiCol_WindowBg] = ImVec4(0.96f, 0.96f, 0.96f, 1.00f);  // Clean white background
    style.Colors[ImGuiCol_PopupBg] = ImVec4(1.00f, 1.00f, 1.00f, 0.95f);
    style.Colors[ImGuiCol_Border] = ImVec4(0.80f, 0.80f, 0.80f, 0.6f);  // Softer borders

    // Softer highlights and active states
    style.Colors[ImGuiCol_FrameBg] = ImVec4(0.94f, 0.94f, 0.94f, 1.00f);
    style.Colors[ImGuiCol_FrameBgHovered] = ImVec4(0.30f, 0.55f, 0.95f, 0.4f);  // Light blue hover
    style.Colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.55f, 0.95f, 0.7f);

    style.Colors[ImGuiCol_TitleBg] = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);
    style.Colors[ImGuiCol_TitleBgActive] = ImVec4(0.72f, 0.72f, 0.72f, 1.00f);
    style.Colors[ImGuiCol_MenuBarBg] = ImVec4(0.88f, 0.88f, 0.88f, 1.00f);

    // Scrollbars with rounded corners and more distinct grab colors
    style.Colors[ImGuiCol_ScrollbarBg] = ImVec4(0.92f, 0.92f, 0.92f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.70f, 0.70f, 0.70f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.60f, 0.60f, 0.60f, 1.00f);
    style.Colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.50f, 0.50f, 0.50f, 1.00f);

    // Accent colors for interactive elements
    style.Colors[ImGuiCol_Button] = ImVec4(0.30f, 0.55f, 0.95f, 0.40f);
    style.Colors[ImGuiCol_ButtonHovered] = ImVec4(0.30f, 0.55f, 0.95f, 0.90f);
    style.Colors[ImGuiCol_ButtonActive] = ImVec4(0.20f, 0.45f, 0.85f, 1.00f);

    style.Colors[ImGuiCol_Header] = ImVec4(0.30f, 0.55f, 0.95f, 0.40f);
    style.Colors[ImGuiCol_HeaderHovered] = ImVec4(0.30f, 0.55f, 0.95f, 0.80f);
    style.Colors[ImGuiCol_HeaderActive] = ImVec4(0.30f, 0.55f, 0.95f, 1.00f);

    style.Colors[ImGuiCol_ResizeGrip] = ImVec4(0.75f, 0.75f, 0.75f, 0.60f);
    style.Colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.30f, 0.55f, 0.95f, 0.67f);
    style.Colors[ImGuiCol_ResizeGripActive] = ImVec4(0.30f, 0.55f, 0.95f, 0.95f);

    // Consistent transparency scaling
    if (bStyleDark_)
    {
        for (int i = 0; i <= ImGuiCol_COUNT; i++)
        {
            ImVec4& col = style.Colors[i];
            float H, S, V;
            ImGui::ColorConvertRGBtoHSV(col.x, col.y, col.z, H, S, V);
            if (S < 0.1f)
                V = 1.0f - V;
            ImGui::ColorConvertHSVtoRGB(H, S, V, col.x, col.y, col.z);
            col.w *= alpha_;  // Apply alpha uniformly
        }
    }
    else
    {
        for (int i = 0; i <= ImGuiCol_COUNT; i++)
        {
            ImVec4& col = style.Colors[i];
            col.w *= alpha_;
        }
    }
}
