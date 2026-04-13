#pragma once

#include "Core/Camera.h"
#include "Core/SceneManager.h"
#include "Cursors/CursorPresets.h"
#include "Engine/Core.h"
#include "Gui/CursorPreview3D.h"
#include "GuiTypes.h"
#include "Loaders/ModelLoader.h"
#include "Loaders/PointCloudLoader.h"
#include "imgui/imgui_incl.h"
#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <portable-file-dialogs.h>


// Forward declarations
class ImGuiViewportP;
namespace Engine {
class CursorPreset;
class Voxelizer;
} // namespace Engine

// Main rendering function
void renderGUI(bool isLeftEye, ImGuiViewportP *viewport,
               ImGuiWindowFlags windowFlags, Engine::Shader *shader);

// UI Windows
void renderSettingsWindow();
void renderCursorSettingsWindow();
void renderBrushToolWindow();
void renderSunManipulationPanel();
void renderModelManipulationPanel(Engine::Model &model, Engine::Shader *shader);
void renderMeshManipulationPanel(Engine::Model &model, int meshIndex,
                                 Engine::Shader *shader);
void renderPointCloudManipulationPanel(Engine::PointCloud &pointCloud);

// Scene management functions
void deleteSelectedModel();
void deleteSelectedPointCloud();

// GUI initialization and cleanup
bool InitializeGUI(GLFWwindow *window, bool isDarkTheme);
void CleanupGUI();

// ImGui style setup (to avoid redefinition)
void SetupImGuiStyle(bool bStyleDark_, float alpha_);
bool InitializeImGuiWithFonts(GLFWwindow *window, bool isDarkTheme);