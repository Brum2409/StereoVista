#pragma once

#include "core.h"
#include "GUI_Types.h"
#include "imgui/imgui_incl.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Camera.h"
#include "scene_manager.h"
#include "model_loader.h"
#include "cursor_presets.h"
#include "point_cloud_loader.h"
#include <portable-file-dialogs.h>

// Forward declarations
class ImGuiViewportP;
namespace Engine {
    class CursorPreset;
    class Voxelizer;
}

// Main rendering function
void renderGUI(bool isLeftEye, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, Engine::Shader* shader);

// UI Windows
void renderSettingsWindow();
void renderCursorSettingsWindow();
void renderSunManipulationPanel();
void renderModelManipulationPanel(Engine::Model& model, Engine::Shader* shader);
void renderMeshManipulationPanel(Engine::Model& model, int meshIndex, Engine::Shader* shader);
void renderPointCloudManipulationPanel(Engine::PointCloud& pointCloud);
void renderStereoCameraVisualization(const Camera& camera, const Engine::SceneSettings& settings);

// Scene management functions
void deleteSelectedModel();
void deleteSelectedPointCloud();

// Utility functions
void applyPresetToGlobalSettings(const Engine::CursorPreset& preset);
Engine::CursorPreset createPresetFromCurrentSettings(const std::string& name);
void setDefaultCursorSettings();

// GUI initialization and cleanup
bool InitializeGUI(GLFWwindow* window, bool isDarkTheme);
void CleanupGUI();

// ImGui style setup (to avoid redefinition)
void SetupImGuiStyle(bool bStyleDark_, float alpha_);
bool InitializeImGuiWithFonts(GLFWwindow* window, bool isDarkTheme);