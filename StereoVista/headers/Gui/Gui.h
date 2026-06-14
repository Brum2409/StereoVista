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

namespace GUI {

// Toast notifications: transient, non-interactive status messages stacked at
// the bottom-center of the window (e.g. "Scene saved", import failures).
// Safe to call from anywhere after the ImGui context exists; calls made
// before that are silently ignored.
enum class ToastType { Success, Info, Warning, Error };
void ShowToast(const std::string &message, ToastType type = ToastType::Info);

// Reflect the loaded scene file in the OS window title (pass an empty string
// to restore the plain application title). Preserves the mono-fallback
// marker chosen at window creation.
void UpdateWindowTitleForScene(const std::string &scenePath);

} // namespace GUI

// UI Windows
void renderSettingsWindow();
void renderCursorSettingsWindow();
void renderBrushToolWindow();
void renderMeasurementToolWindow();
void renderClipPlaneToolWindow();
void renderSnapshotsWindow();
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

// Published every frame by renderGUI: the pixel extents of the docked GUI
// regions the 3D viewport must not render behind. g_dockLeftWidth is the width
// of the left Scene Hierarchy panel; g_dockTopHeight is the height of the main
// menu bar. Both are 0 when the GUI is hidden (viewport fills the window).
extern float g_dockLeftWidth;
extern float g_dockTopHeight;