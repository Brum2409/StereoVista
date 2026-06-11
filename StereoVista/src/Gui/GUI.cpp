#include "Gui/Gui.h"
#include "Core/Camera.h"
#include "Core/Voxalizer.h"
#include "Cursors/Base/CursorManager.h"
#include "Engine/BVHDebug.h"
#include "Engine/Core.h"
#include "Engine/ShortcutManager.h"
#include "Engine/SpaceMouseInput.h"
#include "Engine/ThreeDConnexionSync.h"
#include "Gui/GUITypes.h"
#include "Tools/BrushTool.h"
#include "imgui/IconsFontAwesome5.h"
#include "imgui/imgui_sytle.h"
#include "libs/portable-file-dialogs.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <json.h>
#include <map>
#include <set>
#include <sstream>
#include <utility>

using namespace GUI;

// Forward declarations
void updateSpaceMouseBounds();
void updateSpaceMouseCursorAnchor();
void renderPointLightManipulationPanel();
void renderSpotLightManipulationPanel();

// Application globals used throughout the GUI system
extern int windowWidth;
extern int windowHeight;
extern Engine::Scene currentScene;
extern Camera camera;
extern bool showGui;
extern bool showFPS;
extern bool isDarkTheme;
extern bool showSettingsWindow;
extern bool show3DCursor;
extern bool showCursorSettingsWindow;
extern bool showBrushToolWindow;
extern Cursor::CursorManager cursorManager;
extern Engine::Voxelizer *voxelizer;
extern float ambientStrengthFromSkybox;
extern float mouseSmoothingFactor;
extern bool orbitFollowsCursor;
extern float convergenceSmoothingSpeed;

// SpaceMouse variables
extern SpaceMouseInput spaceMouseInput;
extern bool spaceMouseInitialized;
extern ThreeDConnexionSync tdxSync;

// Cursor variables
extern Cursor::CursorManager cursorManager;

// Brush tool
extern Tools::BrushTool brushTool;

extern GUI::LightingMode currentLightingMode;
extern bool enableShadows;
extern GUI::VCTSettings vctSettings;
extern GUI::ApplicationPreferences::RadianceSettings radianceSettings;
extern bool enableBVH;
extern bool showBVHDebug;
extern Engine::BVHDebugRenderer bvhDebugRenderer;
extern bool g_ddgiResetRequested; // set by GUI, consumed in the render loop

// Selection state for object interaction
extern enum class SelectedType {
  None,
  Model,
  PointCloud,
  Sun,
  PointLight,
  SpotLight,
  BrushCluster
} currentSelectedType;
extern int currentSelectedIndex;
extern int currentSelectedMeshIndex;

extern Engine::Sun sun;
extern std::vector<Engine::PointLight> pointLights;
extern std::vector<Engine::SpotLight> spotLights;

// Skybox configuration
extern GUI::SkyboxConfig skyboxConfig;
extern std::vector<GUI::CubemapPreset> cubemapPresets;

// User preferences and presets
extern GUI::ApplicationPreferences preferences;
extern std::string currentPresetName;
extern bool isEditingPresetName;
extern char editPresetNameBuffer[256];
extern GUI::CursorPreview3D cursorPreview3D;

// Shortcut manager
extern StereoVista::ShortcutManager shortcutManager;

// External function declarations
extern void savePreferences();
extern void updateSkybox();

// Constants
extern const int MAX_LIGHTS;

// ===========================================================================
// Redesigned GUI: navigation state + reusable modern widgets
// ===========================================================================

// Categories for the sidebar-navigated Settings window. Declared at file scope
// so menus could deep-link straight to a category if needed.
enum SettingsCategory {
  SETTINGS_CAT_RENDERING = 0,
  SETTINGS_CAT_CAMERA,
  SETTINGS_CAT_STEREO,
  SETTINGS_CAT_ENVIRONMENT,
  SETTINGS_CAT_INTERFACE,
  SETTINGS_CAT_SPACEMOUSE,
  SETTINGS_CAT_IMPORT,
  SETTINGS_CAT_SHORTCUTS
};
static int g_settingsCategory = SETTINGS_CAT_RENDERING;

// Docked-region insets published to the render loop (see Gui.h). Updated each
// frame in renderGUI so the 3D viewport can be sized to the free area.
float g_dockLeftWidth = 0.0f;
float g_dockTopHeight = 0.0f;

// Draw a FontAwesome glyph inline using the dedicated icon font (the icon font
// is always guaranteed to contain the glyph, unlike the merged regular font),
// then keep the cursor on the same line so a label can follow.
static void DrawInlineIcon(const char *icon, const ImVec4 &color) {
  ImGui::AlignTextToFramePadding();
  if (g_Fonts.icons) {
    ImGui::PushFont(g_Fonts.icons);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(icon);
    ImGui::PopStyleColor();
    ImGui::PopFont();
    ImGui::SameLine();
  }
}

// Modern section header: a colored accent bar followed by a bright title and a
// thin separator. Used everywhere via the existing call sites.
static void DrawSectionHeader(const char *label) {
  float scale = g_GuiScale.currentScale;
  ImGui::Spacing();
  ImVec2 p = ImGui::GetCursorScreenPos();
  float fontSize = ImGui::GetFontSize();
  ImDrawList *dl = ImGui::GetWindowDrawList();
  ImU32 accent = ImGui::GetColorU32(g_StyleColors.accent);
  dl->AddRectFilled(ImVec2(p.x, p.y + 2.0f * scale),
                    ImVec2(p.x + 3.5f * scale, p.y + fontSize),
                    accent, 2.0f * scale);
  ImGui::Indent(10.0f * scale);
  ImGui::TextUnformatted(label);
  ImGui::Unindent(10.0f * scale);
  ImGui::Spacing();
  ImGui::Separator();
}

// Vertical sidebar navigation entry (icon + label). Returns true when clicked.
// The icon is rendered through the dedicated icon font via the draw list so it
// is reliable regardless of whether the merged-font path succeeded.
static bool DrawNavItem(const char *icon, const char *label, bool selected) {
  float scale = g_GuiScale.currentScale;
  float fullWidth = ImGui::GetContentRegionAvail().x;
  float rowH = ImGui::GetFrameHeight() + 8.0f * scale;
  ImVec2 p0 = ImGui::GetCursorScreenPos();

  ImGui::PushID(label);
  ImGui::PushStyleColor(
      ImGuiCol_HeaderHovered,
      ImVec4(g_StyleColors.primary.x, g_StyleColors.primary.y,
             g_StyleColors.primary.z, 0.16f));
  // Pass selected=false so ImGui does not paint its own selection fill; we draw
  // a custom accent pill instead.
  bool clicked = ImGui::Selectable("##navitem", false,
                                   ImGuiSelectableFlags_None,
                                   ImVec2(fullWidth, rowH));
  ImGui::PopStyleColor();
  ImGui::PopID();

  ImDrawList *dl = ImGui::GetWindowDrawList();
  float fontSize = ImGui::GetFontSize();
  ImU32 textCol = ImGui::GetColorU32(selected ? ImGuiCol_Text
                                              : ImGuiCol_TextDisabled);

  if (selected) {
    ImU32 fill = ImGui::GetColorU32(
        ImVec4(g_StyleColors.primary.x, g_StyleColors.primary.y,
               g_StyleColors.primary.z, 0.16f));
    dl->AddRectFilled(p0, ImVec2(p0.x + fullWidth, p0.y + rowH), fill,
                      8.0f * scale);
    dl->AddRectFilled(ImVec2(p0.x, p0.y + rowH * 0.18f),
                      ImVec2(p0.x + 3.0f * scale, p0.y + rowH * 0.82f),
                      ImGui::GetColorU32(g_StyleColors.primary), 2.0f * scale);
    textCol = ImGui::GetColorU32(ImGuiCol_Text);
  }

  float pad = 14.0f * scale;
  float iconSlot = 24.0f * scale;
  ImVec2 iconPos(p0.x + pad, p0.y + (rowH - fontSize) * 0.5f);
  if (g_Fonts.icons)
    dl->AddText(g_Fonts.icons, fontSize, iconPos, textCol, icon);
  ImVec2 labelPos(p0.x + pad + iconSlot, p0.y + (rowH - fontSize) * 0.5f);
  dl->AddText(labelPos, textCol, label);

  return clicked;
}

// A modern on/off toggle switch. Returns true on the frame it changes.
static bool DrawToggleSwitch(const char *label, bool *v) {
  float scale = g_GuiScale.currentScale;
  ImGui::PushID(label);
  float height = ImGui::GetFrameHeight();
  float width = height * 1.85f;
  float radius = height * 0.5f;
  ImVec2 p = ImGui::GetCursorScreenPos();
  ImGui::InvisibleButton("##toggle", ImVec2(width, height));
  bool changed = false;
  if (ImGui::IsItemClicked()) {
    *v = !*v;
    changed = true;
  }

  ImDrawList *dl = ImGui::GetWindowDrawList();
  bool hovered = ImGui::IsItemHovered();
  ImU32 bg;
  if (*v)
    bg = ImGui::GetColorU32(hovered ? g_StyleColors.primaryHover
                                    : g_StyleColors.primary);
  else
    bg = ImGui::GetColorU32(hovered ? ImGuiCol_FrameBgHovered
                                    : ImGuiCol_FrameBg);
  dl->AddRectFilled(p, ImVec2(p.x + width, p.y + height), bg, radius);
  float knob = radius - 2.5f * scale;
  float cx = *v ? (p.x + width - radius) : (p.x + radius);
  dl->AddCircleFilled(ImVec2(cx, p.y + radius), knob,
                      IM_COL32(255, 255, 255, 255));

  if (label && label[0] != '\0' &&
      !(label[0] == '#' && label[1] == '#')) {
    ImGui::SameLine();
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted(label);
  }
  ImGui::PopID();
  return changed;
}

// Case-insensitive substring match used by the Scene Hierarchy search box.
static bool MatchesSearch(const std::string &name, const char *query) {
  if (!query || query[0] == '\0')
    return true;
  std::string lowerName = name, lowerQuery = query;
  std::transform(lowerName.begin(), lowerName.end(), lowerName.begin(),
                 ::tolower);
  std::transform(lowerQuery.begin(), lowerQuery.end(), lowerQuery.begin(),
                 ::tolower);
  return lowerName.find(lowerQuery) != std::string::npos;
}

// Helper function for help markers (tooltips)
static void DrawHelpMarker(const char *desc) {
  ImGui::TextDisabled("(?)");
  if (ImGui::IsItemHovered()) {
    ImGui::BeginTooltip();
    ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
    ImGui::TextUnformatted(desc);
    ImGui::PopTextWrapPos();
    ImGui::EndTooltip();
  }
}

bool InitializeGUI(GLFWwindow *window, bool isDarkTheme) {
  return InitializeImGuiWithFonts(window, isDarkTheme);
}

void CleanupGUI() {
  // Note: We intentionally skip ImGui cleanup to avoid crashes during static
  // destruction. ImGui has global/static objects that are destroyed after
  // main() returns, and calling shutdown functions can cause double-free or
  // use-after-free errors. The OS will clean up all memory when the process
  // exits anyway. See: https://github.com/ocornut/imgui/issues/586

  // ImGui_ImplOpenGL3_Shutdown();
  // ImGui_ImplGlfw_Shutdown();
  // ImGui::DestroyContext();
}

void renderGUI(bool isLeftEye, ImGuiViewportP *viewport,
               ImGuiWindowFlags windowFlags, Engine::Shader *shader) {
  if (!isLeftEye) {
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return;
  }

  // Check if GUI rescaling is needed
  RescaleImGuiFonts(Engine::Window::nativeWindow, isDarkTheme);

  ImGui_ImplOpenGL3_NewFrame();
  ImGui_ImplGlfw_NewFrame();
  ImGui::NewFrame();

  if (!showGui) {
    // GUI hidden: the viewport fills the whole window.
    g_dockLeftWidth = 0.0f;
    g_dockTopHeight = 0.0f;
    if (showFPS) {
      ImGui::SetNextWindowPos(ImVec2(windowWidth - 120, windowHeight - 60));
      ImGui::Begin("FPS Counter", nullptr,
                   ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                       ImGuiWindowFlags_AlwaysAutoResize |
                       ImGuiWindowFlags_NoBackground);
      ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
      ImGui::End();
    }
    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    return;
  }

  // ========================

  // MAIN MENU BAR
  // ========================

  // Static variables for scene loading dialog (must be outside menu scope)
  static std::string pendingSceneToLoad = "";
  static bool showLoadSceneDialog = false;

  // Helper lambda to load and replace scene
  auto loadAndReplaceScene = [&](const std::string &sceneFile) {
    try {
      // Clear all existing objects
      currentScene.models.clear();
      currentScene.pointClouds.clear();
      pointLights.clear();
      spotLights.clear();
      currentSelectedType = SelectedType::None;
      currentSelectedIndex = -1;
      currentSelectedMeshIndex = -1;

      // Load new scene
      currentScene = Engine::loadScene(sceneFile, camera);
      pointLights = currentScene.pointLights;
      for (auto &pl : pointLights) {
        pl.castShadows = true;
      }
      spotLights = currentScene.spotLights;

      for (auto &model : currentScene.models) {
        glm::vec3 targetScale = model.scale;
        if (preferences.enableSpawnAnimation) {
          model.startSpawnAnimation(targetScale, 1.1f);
        }
      }
      currentSelectedIndex = currentScene.models.empty() ? -1 : 0;
      updateSpaceMouseBounds();

      // Mark voxelizer dirty for re-voxelization with new scene
      if (voxelizer) {
        voxelizer->markDirty();
      }
    } catch (const std::exception &e) {
      std::cerr << "Failed to load scene: " << e.what() << std::endl;
    }
  };

  // Helper lambda to load and merge scene
  auto loadAndMergeScene = [&](const std::string &sceneFile) {
    try {
      // Load new scene and merge with existing
      Engine::Scene newScene = Engine::loadScene(sceneFile, camera);

      // Merge models
      for (auto &model : newScene.models) {
        glm::vec3 targetScale = model.scale;
        if (preferences.enableSpawnAnimation) {
          model.startSpawnAnimation(targetScale, 1.1f);
        }
        currentScene.models.push_back(model);
      }

      // Merge point clouds
      for (auto &pc : newScene.pointClouds) {
        currentScene.pointClouds.push_back(std::move(pc));
      }

      // Merge point lights
      for (auto &pl : newScene.pointLights) {
        pl.castShadows = true;
        pointLights.push_back(pl);
      }

      // Merge spot lights
      for (auto &sl : newScene.spotLights) {
        spotLights.push_back(sl);
      }

      updateSpaceMouseBounds();

      // Mark voxelizer dirty for re-voxelization with merged geometry
      if (voxelizer) {
        voxelizer->markDirty();
      }
    } catch (const std::exception &e) {
      std::cerr << "Failed to load scene: " << e.what() << std::endl;
    }
  };

  // Helper lambda to load scene (first load or based on preference)
  auto loadSceneWithPreference = [&](const std::string &sceneFile) {
    currentScene = Engine::loadScene(sceneFile, camera);
    pointLights = currentScene.pointLights;
    for (auto &pl : pointLights) {
      pl.castShadows = true;
    }
    spotLights = currentScene.spotLights;

    for (auto &model : currentScene.models) {
      glm::vec3 targetScale = model.scale;
      if (preferences.enableSpawnAnimation) {
        model.startSpawnAnimation(targetScale, 1.1f);
      }
    }
    currentSelectedIndex = currentScene.models.empty() ? -1 : 0;
    updateSpaceMouseBounds();

    // Mark voxelizer dirty for re-voxelization with new scene
    if (voxelizer) {
      voxelizer->markDirty();
    }
  };

  if (ImGui::BeginMainMenuBar()) {
    // File Menu
    if (ImGui::BeginMenu("File")) {
      if (ImGui::BeginMenu("Import")) {
        // Add icon to 3D Model import
        std::string modelMenuText = "3D Model...";
        if (g_Fonts.icons) {
          ImGui::PushFont(g_Fonts.icons);
          ImGui::Text(ICON_FA_CUBE);
          ImGui::PopFont();
          ImGui::SameLine();
        }
        if (ImGui::MenuItem(modelMenuText.c_str())) {
          auto selection =
              pfd::open_file("Select a 3D model to import", ".",
                             {"3D Models", "*.obj *.fbx *.3ds *.gltf *.glb",
                              "All Files", "*"})
                  .result();

          if (!selection.empty()) {
            std::string filePath = selection[0];
            try {
              Engine::Model newModel = *Engine::loadModel(filePath);
              glm::vec3 targetScale = newModel.scale;
              if (preferences.enableSpawnAnimation) {
                newModel.startSpawnAnimation(targetScale, 1.1f);
              }
              currentScene.models.push_back(newModel);
              currentSelectedIndex = currentScene.models.size() - 1;
              currentSelectedType = SelectedType::Model;
              updateSpaceMouseBounds();

              // Mark voxelizer dirty for re-voxelization
              if (voxelizer) {
                voxelizer->markDirty();
              }
            } catch (const std::exception &e) {
              std::cerr << "Failed to load model: " << e.what() << std::endl;
            }
          }
        }

        // Add icon to Point Cloud import
        std::string cloudMenuText = "Point Cloud...";
        if (g_Fonts.icons) {
          ImGui::PushFont(g_Fonts.icons);
          ImGui::Text(ICON_FA_CLOUD);
          ImGui::PopFont();
          ImGui::SameLine();
        }
        if (ImGui::MenuItem(cloudMenuText.c_str())) {
          auto selection =
              pfd::open_file(
                  "Select point cloud file(s) to import", ".",
                  {"Point Cloud Files",
                   "*.txt *.xyz *.ply *.pcb *.h5 *.hdf5 *.f5 *.las *.laz",
                   "All Files", "*"},
                  pfd::opt::multiselect)
                  .result();

          if (!selection.empty()) {
            // Separate LAS/LAZ tiles from other formats.  Multiple LAS/LAZ
            // files are loaded together so they share a global centre and
            // remain correctly positioned relative to each other.
            std::vector<std::string> lasFiles, otherFiles;
            for (const auto& path : selection) {
              std::string ext = std::filesystem::path(path).extension().string();
              std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
              if (ext == ".las" || ext == ".laz") lasFiles.push_back(path);
              else otherFiles.push_back(path);
            }

            if (!lasFiles.empty()) {
              auto clouds = Engine::PointCloudLoader::loadFromLASMultiple(lasFiles);
              for (auto& pc : clouds) {
                currentScene.pointClouds.emplace_back(std::move(pc));
              }
              updateSpaceMouseBounds();
            }

            for (const auto& filePath : otherFiles) {
              std::string extension =
                  std::filesystem::path(filePath).extension().string();
              std::transform(extension.begin(), extension.end(),
                             extension.begin(), ::tolower);

              if (extension == ".txt" || extension == ".xyz" ||
                  extension == ".ply") {
                Engine::PointCloud newPointCloud = std::move(
                    Engine::PointCloudLoader::loadPointCloudFile(filePath));
                newPointCloud.filePath = filePath;
                currentScene.pointClouds.emplace_back(std::move(newPointCloud));
                updateSpaceMouseBounds();
              } else if (extension == ".pcb") {
                Engine::PointCloud newPointCloud =
                    std::move(Engine::PointCloudLoader::loadFromBinary(filePath));
                if (newPointCloud.isLoaded()) {
                  newPointCloud.filePath = filePath;
                  newPointCloud.name =
                      std::filesystem::path(filePath).stem().string();
                  currentScene.pointClouds.emplace_back(std::move(newPointCloud));
                  updateSpaceMouseBounds();
                }
              } else if (extension == ".h5" || extension == ".hdf5" ||
                         extension == ".f5") {
                Engine::PointCloud newPointCloud = std::move(
                    Engine::PointCloudLoader::loadPointCloudFile(filePath));
                if (newPointCloud.isLoaded()) {
                  newPointCloud.filePath = filePath;
                  newPointCloud.name =
                      std::filesystem::path(filePath).stem().string();
                  currentScene.pointClouds.emplace_back(std::move(newPointCloud));
                  updateSpaceMouseBounds();
                }
              }
            }
          }
        }
        ImGui::EndMenu();
      }

      ImGui::Separator();

      // Add icon to Load Scene
      std::string sceneMenuText = "Load Scene...";
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_FOLDER_OPEN);
        ImGui::PopFont();
        ImGui::SameLine();
      }

      if (ImGui::MenuItem(sceneMenuText.c_str())) {
        auto selection =
            pfd::open_file("Select a scene file to load", ".",
                           {"Scene Files", "*.scene", "All Files", "*"})
                .result();
        if (!selection.empty()) {
          // Check if there are existing objects in the scene
          bool hasExistingObjects = !currentScene.models.empty() ||
                                    !currentScene.pointClouds.empty() ||
                                    !pointLights.empty() || !spotLights.empty();

          if (hasExistingObjects) {
            // Check user preference for scene loading behavior
            if (preferences.sceneLoadingBehavior ==
                GUI::SCENE_LOAD_ALWAYS_REPLACE) {
              // Auto-replace: clear existing and load new
              loadAndReplaceScene(selection[0]);
            } else if (preferences.sceneLoadingBehavior ==
                       GUI::SCENE_LOAD_ALWAYS_MERGE) {
              // Auto-merge: keep existing and add new
              loadAndMergeScene(selection[0]);
            } else {
              // Always ask: show dialog
              pendingSceneToLoad = selection[0];
              showLoadSceneDialog = true;
            }
          } else {
            // No existing objects, just load directly
            try {
              loadSceneWithPreference(selection[0]);
            } catch (const std::exception &e) {
              std::cerr << "Failed to load scene: " << e.what() << std::endl;
            }
          }
        }
      }

      if (ImGui::MenuItem("Save Scene...")) {
        auto destination =
            pfd::save_file("Select a file to save scene", ".",
                           {"Scene Files", "*.scene", "All Files", "*"})
                .result();
        if (!destination.empty()) {
          try {
            currentScene.pointLights = pointLights;
            currentScene.spotLights = spotLights;
            Engine::saveScene(destination, currentScene, camera);
          } catch (const std::exception &e) {
            std::cerr << "Failed to save scene: " << e.what() << std::endl;
          }
        }
      }

      ImGui::Separator();

      if (ImGui::MenuItem("Exit", "Esc")) {
        glfwSetWindowShouldClose(Engine::Window::nativeWindow, true);
      }

      ImGui::EndMenu();
    }

    // Load Scene Dialog Modal (must be outside menu scope for popup to work
    // correctly)
    if (showLoadSceneDialog) {
      ImGui::OpenPopup("Load Scene");
      showLoadSceneDialog = false;
    }

    // Center the popup in the viewport (both horizontally and vertically)
    ImVec2 center = ImGui::GetMainViewport()->GetCenter();
    ImGui::SetNextWindowPos(center, ImGuiCond_Always, ImVec2(0.5f, 0.5f));

    if (ImGui::BeginPopupModal("Load Scene", nullptr,
                               ImGuiWindowFlags_AlwaysAutoResize |
                                   ImGuiWindowFlags_NoMove)) {
      ImGui::Text("An existing scene is already loaded.");
      ImGui::Spacing();
      ImGui::Text("How would you like to load the new scene?");
      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Replace button - clear existing scene
      if (ImGui::Button("Replace (Clear Existing)", ImVec2(200, 0))) {
        loadAndReplaceScene(pendingSceneToLoad);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      DrawHelpMarker("Remove all existing objects and load the new scene");

      ImGui::Spacing();

      // Merge button - keep existing scene
      if (ImGui::Button("Merge (Keep Existing)", ImVec2(200, 0))) {
        loadAndMergeScene(pendingSceneToLoad);
        ImGui::CloseCurrentPopup();
      }
      ImGui::SameLine();
      DrawHelpMarker("Add new scene objects to the existing scene");

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Cancel button
      if (ImGui::Button("Cancel", ImVec2(-1, 0))) {
        ImGui::CloseCurrentPopup();
      }

      ImGui::Spacing();
      ImGui::Separator();
      ImGui::Spacing();

      // Hint about settings
      ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
      ImGui::TextWrapped("Tip: You can set a default behavior in Settings > "
                         "Interface > Scene Loading");
      ImGui::PopStyleColor();

      ImGui::EndPopup();
    }

    // Create Menu
    if (ImGui::BeginMenu("Create")) {
      // Use non-collapsible headers instead of dropdowns
      DrawSectionHeader("Primitives");
      if (ImGui::MenuItem("Cube")) {
        Engine::Model newCube =
            Engine::createCube(glm::vec3(0.8f, 0.8f, 0.8f), 1.0f, 0.0f);
        newCube.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(0.5f);
        if (preferences.enableSpawnAnimation) {
          newCube.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newCube.scale = targetScale;
        }
        currentScene.models.push_back(newCube);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      if (ImGui::MenuItem("Sphere")) {
        Engine::Model newSphere =
            Engine::createSphere(glm::vec3(0.8f, 0.4f, 0.4f), 1.0f, 0.0f);
        newSphere.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(0.5f);
        if (preferences.enableSpawnAnimation) {
          newSphere.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newSphere.scale = targetScale;
        }
        currentScene.models.push_back(newSphere);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      if (ImGui::MenuItem("Cylinder")) {
        Engine::Model newCylinder =
            Engine::createCylinder(glm::vec3(0.4f, 0.8f, 0.4f), 1.0f, 0.0f);
        newCylinder.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(0.5f);
        if (preferences.enableSpawnAnimation) {
          newCylinder.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newCylinder.scale = targetScale;
        }
        currentScene.models.push_back(newCylinder);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      if (ImGui::MenuItem("Plane")) {
        Engine::Model newPlane =
            Engine::createPlane(glm::vec3(0.6f, 0.6f, 0.8f), 1.0f, 0.0f);
        newPlane.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(1.0f);
        if (preferences.enableSpawnAnimation) {
          newPlane.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newPlane.scale = targetScale;
        }
        currentScene.models.push_back(newPlane);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      if (ImGui::MenuItem("Torus")) {
        Engine::Model newTorus =
            Engine::createTorus(glm::vec3(0.8f, 0.6f, 0.2f), 1.0f, 0.0f);
        newTorus.position = glm::vec3(0.0f, 0.0f, 0.0f);
        glm::vec3 targetScale = glm::vec3(0.8f);
        if (preferences.enableSpawnAnimation) {
          newTorus.startSpawnAnimation(targetScale, 1.1f);
        } else {
          newTorus.scale = targetScale;
        }
        currentScene.models.push_back(newTorus);
        currentSelectedIndex = currentScene.models.size() - 1;
        currentSelectedType = SelectedType::Model;
        updateSpaceMouseBounds();
        if (voxelizer)
          voxelizer->markDirty();
      }

      ImGui::Separator();

      // Use non-collapsible header for Lights
      DrawSectionHeader("Lights");
      if (ImGui::MenuItem("Point Light")) {
        Engine::PointLight newPointLight;
        newPointLight.position = glm::vec3(0.0f, 2.0f, 0.0f);
        newPointLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
        newPointLight.intensity = 1.0f;
        newPointLight.lightSpaceMatrix = glm::mat4(1.0f);
        newPointLight.castShadows = true; // default: cast shadows
        pointLights.push_back(newPointLight);
        currentSelectedIndex = pointLights.size() - 1;
        currentSelectedType = SelectedType::PointLight;
      }

      if (ImGui::MenuItem("Spot Light")) {
        Engine::SpotLight newSpotLight;
        newSpotLight.position = glm::vec3(0.0f, 3.0f, 0.0f);
        newSpotLight.direction = glm::vec3(0.0f, -1.0f, 0.0f);
        newSpotLight.color = glm::vec3(1.0f, 1.0f, 1.0f);
        newSpotLight.intensity = 1.0f;
        newSpotLight.innerCutOff = glm::cos(glm::radians(12.5f));
        newSpotLight.outerCutOff = glm::cos(glm::radians(17.5f));
        newSpotLight.lightSpaceMatrix = glm::mat4(1.0f);
        spotLights.push_back(newSpotLight);
        currentSelectedIndex = spotLights.size() - 1;
        currentSelectedType = SelectedType::SpotLight;
      }

      ImGui::EndMenu();
    }

    // View Menu
    if (ImGui::BeginMenu("View")) {
      ImGui::MenuItem("Show GUI", "G", &showGui);
      ImGui::MenuItem("Show FPS Counter", nullptr, &showFPS);
      ImGui::MenuItem("Wireframe Mode", nullptr, &camera.wireframe);

      ImGui::Separator();

      ImGui::MenuItem("Show Radar", nullptr, &preferences.radarEnabled);
      ImGui::MenuItem("Show Zero Plane", nullptr, &preferences.showZeroPlane);

      ImGui::EndMenu();
    }

    // Tools Menu: 3D cursor configuration and the brush tool. Camera
    // tuning lives in Settings > Camera; quick view toggles live in View.
    if (ImGui::BeginMenu("Tools")) {
      if (ImGui::BeginMenu("3D Cursor")) {
        auto *sphereCursor = cursorManager.getSphereCursor();
        auto *fragmentCursor = cursorManager.getFragmentCursor();
        auto *planeCursor = cursorManager.getPlaneCursor();

        bool showSphereCursor = sphereCursor->isVisible();
        if (ImGui::MenuItem("3D Sphere", nullptr, &showSphereCursor)) {
          sphereCursor->setVisible(showSphereCursor);
        }

        bool showFragmentCursor = fragmentCursor->isVisible();
        if (ImGui::MenuItem("2D Circle", nullptr, &showFragmentCursor)) {
          fragmentCursor->setVisible(showFragmentCursor);
        }

        bool showPlaneCursor = planeCursor->isVisible();
        if (ImGui::MenuItem("Surface Plane", nullptr, &showPlaneCursor)) {
          planeCursor->setVisible(showPlaneCursor);
        }

        ImGui::Separator();

        if (ImGui::BeginMenu("Presets")) {
          std::vector<std::string> presetNames =
              Engine::CursorPresetManager::getPresetNames();
          for (const auto &name : presetNames) {
            if (ImGui::MenuItem(name.c_str(), nullptr,
                                currentPresetName == name)) {
              currentPresetName = name;
              Engine::CursorPreset loadedPreset =
                  Engine::CursorPresetManager::applyCursorPreset(name);

              sphereCursor->setVisible(loadedPreset.showSphereCursor);
              sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(
                  loadedPreset.sphereScalingMode));
              sphereCursor->setFixedRadius(loadedPreset.sphereFixedRadius);
              sphereCursor->setTransparency(loadedPreset.sphereTransparency);
              sphereCursor->setShowInnerSphere(loadedPreset.showInnerSphere);
              sphereCursor->setColor(loadedPreset.cursorColor);
              sphereCursor->setInnerSphereColor(loadedPreset.innerSphereColor);
              sphereCursor->setInnerSphereFactor(
                  loadedPreset.innerSphereFactor);
              sphereCursor->setEdgeSoftness(loadedPreset.cursorEdgeSoftness);
              sphereCursor->setCenterTransparency(
                  loadedPreset.cursorCenterTransparency);

              fragmentCursor->setVisible(loadedPreset.showFragmentCursor);
              fragmentCursor->setBaseInnerRadius(
                  loadedPreset.fragmentBaseInnerRadius);

              planeCursor->setVisible(loadedPreset.showPlaneCursor);
              planeCursor->setDiameter(loadedPreset.planeDiameter);
              planeCursor->setColor(loadedPreset.planeColor);

              preferences.currentPresetName = currentPresetName;
              savePreferences();
            }
          }
          ImGui::EndMenu();
        }

        ImGui::Separator();

        if (ImGui::MenuItem("Cursor Settings...")) {
          showCursorSettingsWindow = true;
        }

        ImGui::EndMenu();
      }

      if (ImGui::MenuItem("Brush Tool...")) {
        showBrushToolWindow = true;
      }

      ImGui::EndMenu();
    }

    // Settings window
    if (ImGui::MenuItem("Settings")) {
      showSettingsWindow = true;
    }

    ImGui::EndMainMenuBar();
  }

  // Publish the menu bar height so the render loop can reserve the top strip.
  g_dockTopHeight = ImGui::GetFrameHeight();

  // ========================
  // SCENE HIERARCHY PANEL
  // ========================
  ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
  ImGui::SetNextWindowSize(ImVec2(320 * g_GuiScale.currentScale,
                                  viewport->Size.y - ImGui::GetFrameHeight()));
  // Ensure only the Scene Hierarchy window itself has square corners (keep
  // inner elements rounded)
  ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

  ImGui::Begin("Scene Hierarchy", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
                   ImGuiWindowFlags_NoCollapse);

  // Publish the panel width so the render loop can reserve the left strip.
  g_dockLeftWidth = ImGui::GetWindowWidth();

  // Scene objects list with search filter
  static char searchBuffer[128] = "";
  DrawInlineIcon(ICON_FA_SEARCH,
                 ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
  ImGui::PushItemWidth(-1);
  ImGui::InputTextWithHint("##Search", "Search objects...", searchBuffer,
                           sizeof(searchBuffer));
  ImGui::PopItemWidth();
  ImGui::Separator();

  if (ImGui::BeginChild("ObjectList", ImVec2(0, 250 * g_GuiScale.currentScale),
                        true)) {
    ImGui::Columns(2, "ObjectColumns", false);
    ImGui::SetColumnWidth(0, 60 * g_GuiScale.currentScale);

    // Sun Object (always at top, ungrouped)
    ImGui::PushID("sun");
    bool sunVisible = sun.enabled;
    if (ImGui::Checkbox("##visible", &sunVisible))
      sun.enabled = sunVisible;
    ImGui::NextColumn();

    bool isSunSelected = (currentSelectedType == SelectedType::Sun);
    ImGui::AlignTextToFramePadding();
    // Render Sun with FontAwesome icon to avoid missing glyphs in fonts
    if (g_Fonts.icons) {
      ImGui::PushFont(g_Fonts.icons);
      ImGui::Text(ICON_FA_SUN);
      ImGui::PopFont();
      ImGui::SameLine();
    }
    if (ImGui::Selectable("Sun", isSunSelected,
                          ImGuiSelectableFlags_SpanAllColumns)) {
      currentSelectedType = SelectedType::Sun;
      currentSelectedIndex = -1;
      currentSelectedMeshIndex = -1;
    }
    ImGui::NextColumn();
    ImGui::PopID();

    // Group objects by sourceScenePath
    std::map<std::string, std::vector<int>> sceneModels;
    std::map<std::string, std::vector<int>> scenePointClouds;
    std::map<std::string, std::vector<int>> scenePointLights;
    std::map<std::string, std::vector<int>> sceneSpotLights;

    // Collect models by scene
    for (int i = 0; i < currentScene.models.size(); i++) {
      sceneModels[currentScene.models[i].sourceScenePath].push_back(i);
    }

    // Collect point clouds by scene
    for (int i = 0; i < currentScene.pointClouds.size(); i++) {
      scenePointClouds[currentScene.pointClouds[i].sourceScenePath].push_back(
          i);
    }

    // Collect point lights by scene
    for (int i = 0; i < pointLights.size(); i++) {
      scenePointLights[pointLights[i].sourceScenePath].push_back(i);
    }

    // Collect spot lights by scene
    for (int i = 0; i < spotLights.size(); i++) {
      sceneSpotLights[spotLights[i].sourceScenePath].push_back(i);
    }

    // Get all unique scene paths
    std::set<std::string> allScenePaths;
    for (const auto &pair : sceneModels)
      allScenePaths.insert(pair.first);
    for (const auto &pair : scenePointClouds)
      allScenePaths.insert(pair.first);
    for (const auto &pair : scenePointLights)
      allScenePaths.insert(pair.first);
    for (const auto &pair : sceneSpotLights)
      allScenePaths.insert(pair.first);

    // Helper lambda to render a model
    auto renderModel = [&](int i) {
      std::string modelName = currentScene.models[i].name;
      if (!MatchesSearch(modelName, searchBuffer))
        return;

      ImGui::PushID(i);
      ImGui::AlignTextToFramePadding();

      bool visible = currentScene.models[i].visible;
      if (ImGui::Checkbox("##visible", &visible)) {
        currentScene.models[i].visible = visible;
      }
      ImGui::NextColumn();

      bool isModelSelected = (currentSelectedIndex == i &&
                              currentSelectedType == SelectedType::Model);
      bool hasMeshes = currentScene.models[i].getMeshes().size() > 0;

      ImGui::AlignTextToFramePadding();
      ImGuiTreeNodeFlags flags =
          ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

      if (!hasMeshes)
        flags |= ImGuiTreeNodeFlags_Leaf;
      if (isModelSelected && currentSelectedMeshIndex == -1)
        flags |= ImGuiTreeNodeFlags_Selected;

      // Render model icon with FontAwesome and the model name separately
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_CUBE);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      bool nodeOpen = ImGui::TreeNodeEx((modelName).c_str(), flags);

      if (ImGui::IsItemClicked()) {
        currentSelectedType = SelectedType::Model;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
      }

      ImGui::NextColumn();

      if (nodeOpen && hasMeshes) {
        for (size_t meshIndex = 0;
             meshIndex < currentScene.models[i].getMeshes().size();
             meshIndex++) {
          ImGui::Columns(2, "MeshColumns", false);
          ImGui::SetColumnWidth(0, 60);

          ImGui::PushID(static_cast<int>(meshIndex));
          bool meshVisible =
              currentScene.models[i].getMeshes()[meshIndex].visible;
          if (ImGui::Checkbox("##meshvisible", &meshVisible)) {
            currentScene.models[i].getMeshes()[meshIndex].visible = meshVisible;
          }
          ImGui::PopID();
          ImGui::NextColumn();

          ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf |
                                         ImGuiTreeNodeFlags_NoTreePushOnOpen |
                                         ImGuiTreeNodeFlags_SpanAvailWidth;

          if (isModelSelected &&
              currentSelectedMeshIndex == static_cast<int>(meshIndex)) {
            meshFlags |= ImGuiTreeNodeFlags_Selected;
          }

          ImGui::Indent(20.0f);
          if (g_Fonts.icons) {
            ImGui::PushFont(g_Fonts.icons);
            ImGui::Text(ICON_FA_CUBE);
            ImGui::PopFont();
            ImGui::SameLine();
          }
          ImGui::TreeNodeEx(("Mesh " + std::to_string(meshIndex + 1)).c_str(),
                            meshFlags);

          if (ImGui::IsItemClicked()) {
            currentSelectedType = SelectedType::Model;
            currentSelectedIndex = i;
            currentSelectedMeshIndex = static_cast<int>(meshIndex);
          }

          ImGui::Unindent(20.0f);
          ImGui::NextColumn();
        }
        ImGui::Columns(2, "ObjectColumns", false);
        ImGui::SetColumnWidth(0, 60);
        ImGui::TreePop();
      }
      ImGui::PopID();
    };

    // Helper lambda to render a point cloud
    auto renderPointCloud = [&](int i) {
      std::string pcName = currentScene.pointClouds[i].name;
      if (!MatchesSearch(pcName, searchBuffer))
        return;

      ImGui::PushID(i + currentScene.models.size());
      bool isSelected = (currentSelectedIndex == i &&
                         currentSelectedType == SelectedType::PointCloud);

      bool visible = currentScene.pointClouds[i].visible;
      if (ImGui::Checkbox("##visible", &visible)) {
        currentScene.pointClouds[i].visible = visible;
      }
      ImGui::NextColumn();

      ImGui::AlignTextToFramePadding();
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_CLOUD);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      if (ImGui::Selectable((pcName).c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        currentSelectedType = SelectedType::PointCloud;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
      }
      ImGui::NextColumn();
      ImGui::PopID();
    };

    // Helper lambda to render a point light
    auto renderPointLight = [&](int i) {
      ImGui::PushID(i + currentScene.models.size() + 1000);
      bool isSelected = (currentSelectedIndex == i &&
                         currentSelectedType == SelectedType::PointLight);

      ImGui::AlignTextToFramePadding();
      ImGui::Text("");
      ImGui::NextColumn();

      ImGui::AlignTextToFramePadding();
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_LIGHTBULB);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      std::string lightText = "Point Light " + std::to_string(i + 1);
      if (ImGui::Selectable(lightText.c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        currentSelectedType = SelectedType::PointLight;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
      }
      ImGui::NextColumn();
      ImGui::PopID();
    };

    // Helper lambda to render a spot light
    auto renderSpotLight = [&](int i) {
      ImGui::PushID(i + currentScene.models.size() + 2000);
      bool isSelected = (currentSelectedIndex == i &&
                         currentSelectedType == SelectedType::SpotLight);

      ImGui::AlignTextToFramePadding();
      ImGui::Text(""); // No visibility checkbox for lights
      ImGui::NextColumn();

      ImGui::AlignTextToFramePadding();
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_BULLSEYE);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      std::string spotText = "Spot Light " + std::to_string(i + 1);
      if (ImGui::Selectable(spotText.c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        currentSelectedType = SelectedType::SpotLight;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
      }
      ImGui::NextColumn();
      ImGui::PopID();
    };

    // First, render ungrouped objects (empty sourceScenePath)
    if (sceneModels.count("") > 0) {
      for (int idx : sceneModels[""]) {
        renderModel(idx);
      }
    }
    if (scenePointClouds.count("") > 0) {
      for (int idx : scenePointClouds[""]) {
        renderPointCloud(idx);
      }
    }
    if (scenePointLights.count("") > 0) {
      for (int idx : scenePointLights[""]) {
        renderPointLight(idx);
      }
    }
    if (sceneSpotLights.count("") > 0) {
      for (int idx : sceneSpotLights[""]) {
        renderSpotLight(idx);
      }
    }

    // Then, render grouped scenes
    for (const auto &scenePath : allScenePaths) {
      if (scenePath.empty())
        continue; // Skip empty path (already rendered)

      // Extract scene name from path
      std::filesystem::path path(scenePath);
      std::string sceneName = path.stem().string();

      // Count total objects in this scene
      int totalObjects = 0;
      if (sceneModels.count(scenePath))
        totalObjects += sceneModels[scenePath].size();
      if (scenePointClouds.count(scenePath))
        totalObjects += scenePointClouds[scenePath].size();
      if (scenePointLights.count(scenePath))
        totalObjects += scenePointLights[scenePath].size();
      if (sceneSpotLights.count(scenePath))
        totalObjects += sceneSpotLights[scenePath].size();

      if (totalObjects == 0)
        continue;

      ImGui::PushID(scenePath.c_str());

      // Calculate master visibility state for this scene
      bool anyVisible = false;
      bool allVisible = true;
      int visibleCount = 0;
      int totalVisibleObjects = 0;

      if (sceneModels.count(scenePath)) {
        for (int idx : sceneModels[scenePath]) {
          totalVisibleObjects++;
          if (currentScene.models[idx].visible) {
            anyVisible = true;
            visibleCount++;
          } else {
            allVisible = false;
          }
        }
      }
      if (scenePointClouds.count(scenePath)) {
        for (int idx : scenePointClouds[scenePath]) {
          totalVisibleObjects++;
          if (currentScene.pointClouds[idx].visible) {
            anyVisible = true;
            visibleCount++;
          } else {
            allVisible = false;
          }
        }
      }

      // Master visibility toggle
      bool masterVisible = allVisible;
      if (visibleCount > 0 && visibleCount < totalVisibleObjects) {
        // Mixed state - show as partially checked
        ImGui::PushItemFlag(ImGuiItemFlags_MixedValue, true);
      }

      if (ImGui::Checkbox("##scenevisible", &masterVisible)) {
        // Toggle all objects in this scene
        if (sceneModels.count(scenePath)) {
          for (int idx : sceneModels[scenePath]) {
            currentScene.models[idx].visible = masterVisible;
          }
        }
        if (scenePointClouds.count(scenePath)) {
          for (int idx : scenePointClouds[scenePath]) {
            currentScene.pointClouds[idx].visible = masterVisible;
          }
        }
      }

      if (visibleCount > 0 && visibleCount < totalVisibleObjects) {
        ImGui::PopItemFlag();
      }

      ImGui::NextColumn();

      // Scene group header
      ImGuiTreeNodeFlags sceneFlags =
          ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_FOLDER);
        ImGui::PopFont();
        ImGui::SameLine();
      }

      bool sceneOpen = ImGui::TreeNodeEx(
          (sceneName + " (" + std::to_string(totalObjects) + ")").c_str(),
          sceneFlags);

      // Context menu for scene group
      if (ImGui::BeginPopupContextItem()) {
        if (ImGui::MenuItem("Delete Scene")) {
          // Mark for deletion
          std::vector<int> modelsToDelete;
          std::vector<int> pointCloudsToDelete;
          std::vector<int> pointLightsToDelete;
          std::vector<int> spotLightsToDelete;

          if (sceneModels.count(scenePath)) {
            modelsToDelete = sceneModels[scenePath];
          }
          if (scenePointClouds.count(scenePath)) {
            pointCloudsToDelete = scenePointClouds[scenePath];
          }
          if (scenePointLights.count(scenePath)) {
            pointLightsToDelete = scenePointLights[scenePath];
          }
          if (sceneSpotLights.count(scenePath)) {
            spotLightsToDelete = sceneSpotLights[scenePath];
          }

          // Sort in descending order to avoid index issues
          std::sort(modelsToDelete.rbegin(), modelsToDelete.rend());
          std::sort(pointCloudsToDelete.rbegin(), pointCloudsToDelete.rend());
          std::sort(pointLightsToDelete.rbegin(), pointLightsToDelete.rend());
          std::sort(spotLightsToDelete.rbegin(), spotLightsToDelete.rend());

          // Delete objects
          for (int idx : modelsToDelete) {
            currentScene.models.erase(currentScene.models.begin() + idx);
          }
          for (int idx : pointCloudsToDelete) {
            currentScene.pointClouds.erase(currentScene.pointClouds.begin() +
                                           idx);
          }
          for (int idx : pointLightsToDelete) {
            pointLights.erase(pointLights.begin() + idx);
          }
          for (int idx : spotLightsToDelete) {
            spotLights.erase(spotLights.begin() + idx);
          }

          // Mark voxelizer dirty if models were deleted
          if (!modelsToDelete.empty() && voxelizer) {
            voxelizer->markDirty();
          }

          // Clear selection if needed
          currentSelectedType = SelectedType::None;
          currentSelectedIndex = -1;
          currentSelectedMeshIndex = -1;
        }
        ImGui::EndPopup();
      }

      ImGui::NextColumn();

      if (sceneOpen) {
        // Render all objects in this scene group
        if (sceneModels.count(scenePath)) {
          for (int idx : sceneModels[scenePath]) {
            renderModel(idx);
          }
        }
        if (scenePointClouds.count(scenePath)) {
          for (int idx : scenePointClouds[scenePath]) {
            renderPointCloud(idx);
          }
        }
        if (scenePointLights.count(scenePath)) {
          for (int idx : scenePointLights[scenePath]) {
            renderPointLight(idx);
          }
        }
        if (sceneSpotLights.count(scenePath)) {
          for (int idx : sceneSpotLights[scenePath]) {
            renderSpotLight(idx);
          }
        }

        ImGui::TreePop();
      }

      ImGui::PopID();
    }

    // Brush Clusters List
    extern Tools::BrushTool brushTool;
    int clusterCount = brushTool.getClusterCount();
    for (int i = 0; i < clusterCount; i++) {
      const Tools::BrushCluster *cluster = brushTool.getCluster(i);
      if (!cluster)
        continue;

      std::string clusterName = cluster->name;
      if (!MatchesSearch(clusterName, searchBuffer))
        continue;

      ImGui::PushID(i + currentScene.models.size() +
                    currentScene.pointClouds.size() + 5000);
      bool isSelected = (currentSelectedIndex == i &&
                         currentSelectedType == SelectedType::BrushCluster);

      ImGui::AlignTextToFramePadding();
      ImGui::Text(""); // No visibility checkbox for clusters
      ImGui::NextColumn();

      ImGui::AlignTextToFramePadding();
      if (g_Fonts.icons) {
        ImGui::PushFont(g_Fonts.icons);
        ImGui::Text(ICON_FA_PAINT_BRUSH);
        ImGui::PopFont();
        ImGui::SameLine();
      }
      std::string displayName =
          clusterName + " (" + std::to_string(cluster->instances.size()) + ")";
      if (ImGui::Selectable(displayName.c_str(), isSelected,
                            ImGuiSelectableFlags_SpanAllColumns)) {
        currentSelectedType = SelectedType::BrushCluster;
        currentSelectedIndex = i;
        currentSelectedMeshIndex = -1;
        brushTool.setActiveCluster(i);
      }
      ImGui::NextColumn();
      ImGui::PopID();
    }

    ImGui::Columns(1);
    ImGui::EndChild();
  }

  ImGui::Spacing();

  // Properties Panel
  DrawSectionHeader("Properties");

  if (ImGui::BeginChild("PropertiesPanel", ImVec2(0, 0), false)) {
    if (currentSelectedType == SelectedType::Model &&
        currentSelectedIndex >= 0 &&
        currentSelectedIndex < currentScene.models.size()) {

      auto &model = currentScene.models[currentSelectedIndex];

      if (currentSelectedMeshIndex >= 0 &&
          currentSelectedMeshIndex <
              static_cast<int>(model.getMeshes().size())) {
        renderMeshManipulationPanel(model, currentSelectedMeshIndex, shader);
      } else {
        renderModelManipulationPanel(model, shader);
      }
    } else if (currentSelectedType == SelectedType::PointCloud &&
               currentSelectedIndex >= 0 &&
               currentSelectedIndex < currentScene.pointClouds.size()) {
      renderPointCloudManipulationPanel(
          currentScene.pointClouds[currentSelectedIndex]);
    } else if (currentSelectedType == SelectedType::Sun) {
      renderSunManipulationPanel();
    } else if (currentSelectedType == SelectedType::PointLight &&
               currentSelectedIndex >= 0 &&
               currentSelectedIndex < pointLights.size()) {
      renderPointLightManipulationPanel();
    } else if (currentSelectedType == SelectedType::SpotLight &&
               currentSelectedIndex >= 0 &&
               currentSelectedIndex < spotLights.size()) {
      renderSpotLightManipulationPanel();
    } else if (currentSelectedType == SelectedType::BrushCluster &&
               currentSelectedIndex >= 0) {
      extern Tools::BrushTool brushTool;
      int clusterCount = brushTool.getClusterCount();
      if (currentSelectedIndex < clusterCount) {
        Tools::BrushCluster *cluster =
            brushTool.getCluster(currentSelectedIndex);
        if (cluster) {
          DrawSectionHeader("Brush Cluster Properties");

          ImGui::Text("Name: %s", cluster->name.c_str());
          if (cluster->sourceModelIndex >= 0 &&
              cluster->sourceModelIndex < currentScene.models.size()) {
            ImGui::Text(
                "Source Model: %s",
                currentScene.models[cluster->sourceModelIndex].name.c_str());
          }
          ImGui::Text("Instances: %d", brushTool.getInstanceCountForCluster(
                                           currentSelectedIndex));

          ImGui::Spacing();

          // Instance Variation
          DrawSectionHeader("Instance Variation");

          ImGui::SliderFloat("Min Scale", &cluster->minScale, 0.1f, 5.0f,
                             "%.2f");
          ImGui::SameLine();
          DrawHelpMarker("Minimum scale multiplier");

          ImGui::SliderFloat("Max Scale", &cluster->maxScale, 0.1f, 5.0f,
                             "%.2f");
          ImGui::SameLine();
          DrawHelpMarker("Maximum scale multiplier");

          if (cluster->maxScale < cluster->minScale) {
            cluster->maxScale = cluster->minScale;
          }

          ImGui::SliderFloat("Rotation", &cluster->rotationRandomization, 0.0f,
                             1.0f, "%.2f");
          ImGui::SameLine();
          DrawHelpMarker("Random rotation");

          ImGui::SliderFloat("Color Variation", &cluster->colorVariation, 0.0f,
                             1.0f, "%.2f");
          ImGui::SameLine();
          DrawHelpMarker("Random color variation");

          ImGui::Checkbox("Align to Surface", &cluster->alignToNormal);

          ImGui::Spacing();

          // Cluster Actions
          DrawSectionHeader("Actions");

          if (ImGui::Button("Set as Active Cluster", ImVec2(-1, 0))) {
            brushTool.setActiveCluster(currentSelectedIndex);
          }

          if (ImGui::Button("Clear Instances", ImVec2(-1, 0))) {
            brushTool.clearInstancesForCluster(currentSelectedIndex);
          }

          if (ImGui::Button("Delete Cluster", ImVec2(-1, 0))) {
            brushTool.deleteCluster(currentSelectedIndex);
            currentSelectedType = SelectedType::None;
            currentSelectedIndex = -1;
          }
        }
      }
    } else {
      ImGui::TextDisabled("No object selected");
      ImGui::Spacing();
      ImGui::TextWrapped("Select an object from the hierarchy above to view "
                         "and edit its properties.");
    }
    ImGui::EndChild();
  }

  ImGui::End();
  ImGui::PopStyleVar(
      1); // Restore rounding only after the Scene Hierarchy window ends

  // Render other windows
  if (showSettingsWindow) {
    renderSettingsWindow();
  }

  if (showCursorSettingsWindow) {
    renderCursorSettingsWindow();
  }

  if (showBrushToolWindow) {
    renderBrushToolWindow();
  }

  // FPS Counter (bottom-right corner)
  if (showFPS) {
    ImGui::SetNextWindowPos(ImVec2(windowWidth - 120, windowHeight - 60));
    ImGui::Begin("FPS", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                     ImGuiWindowFlags_AlwaysAutoResize |
                     ImGuiWindowFlags_NoBackground);
    ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
    ImGui::End();
  }

  ImGui::Render();
  ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void renderSettingsWindow() {
  float scale = g_GuiScale.currentScale;
  ImGui::SetNextWindowSize(ImVec2(900 * scale, 720 * scale),
                           ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSizeConstraints(ImVec2(640 * scale, 420 * scale),
                                      ImVec2(100000.0f, 100000.0f));
  ImGui::Begin("Settings", &showSettingsWindow);
  bool settingsChanged = false;

  // Shared DDGI tuning controls. The same probe volume drives GI in both
  // Radiance mode ("Enable DDGI") and Shadow Mapping mode ("Enable Indirect
  // Lighting"), so both expose this identical set of sliders. These live in the
  // mutually-exclusive lighting-mode branches of the Rendering tab, so only one
  // copy is ever submitted per frame -- no ImGui ID collision.
  auto drawDDGISettings = [&]() {
    ImGui::Indent();

    if (ImGui::SliderInt3("Probe Counts",
                          &preferences.radianceSettings.ddgiProbeCounts.x, 2,
                          48)) {
      ::radianceSettings.ddgiProbeCounts =
          preferences.radianceSettings.ddgiProbeCounts;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Probes along X/Y/Z. More probes = finer GI, but more cost "
                   "and memory. Changing this rebuilds the volume.");

    if (ImGui::SliderInt("Rays / Probe",
                         &preferences.radianceSettings.ddgiRaysPerProbe, 16,
                         256)) {
      ::radianceSettings.ddgiRaysPerProbe =
          preferences.radianceSettings.ddgiRaysPerProbe;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Rays traced per probe each frame. More = faster "
                   "convergence, less noise, higher cost.");

    if (ImGui::SliderFloat("GI Intensity",
                           &preferences.radianceSettings.ddgiGIIntensity, 0.0f,
                           1.0f, "%.3f")) {
      ::radianceSettings.ddgiGIIntensity =
          preferences.radianceSettings.ddgiGIIntensity;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Multiplier for the indirect diffuse contribution. GI is "
                   "physically multi-bounce, so high-albedo or bright-sky "
                   "scenes need a low value (~0.05-0.3). Ctrl+click to type an "
                   "exact value.");

    if (ImGui::SliderFloat("Visibility (AO)",
                           &preferences.radianceSettings.ddgiVisibilityStrength,
                           0.0f, 1.0f, "%.2f")) {
      ::radianceSettings.ddgiVisibilityStrength =
          preferences.radianceSettings.ddgiVisibilityStrength;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("How strongly probe visibility darkens indirect light near "
                   "geometry. Lower this if contact shadows look like harsh AO; "
                   "raise it if light leaks through walls.");

    if (ImGui::SliderFloat("Hysteresis",
                           &preferences.radianceSettings.ddgiHysteresis, 0.0f,
                           0.99f, "%.3f")) {
      ::radianceSettings.ddgiHysteresis =
          preferences.radianceSettings.ddgiHysteresis;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Temporal blend. Higher = more stable but slower to react "
                   "to lighting/scene changes.");

    if (ImGui::SliderFloat("Normal Bias",
                           &preferences.radianceSettings.ddgiNormalBias, 0.0f,
                           1.0f, "%.2f")) {
      ::radianceSettings.ddgiNormalBias =
          preferences.radianceSettings.ddgiNormalBias;
      settingsChanged = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Surface offset (fraction of probe spacing) used when "
                   "sampling probes. Reduces self-shadowing and leaks.");

    if (ImGui::Button("Reset DDGI")) {
      g_ddgiResetRequested = true;
    }
    ImGui::SameLine();
    DrawHelpMarker("Clear all probes; they reconverge over a few frames.");

    ImGui::Unindent();
  };

  // ===========================================================================
  // Sidebar navigation (left) + content area (right)
  // ===========================================================================
  struct SettingsNavEntry {
    int id;
    const char *icon;
    const char *label;
  };
  static const SettingsNavEntry kNavEntries[] = {
      {SETTINGS_CAT_RENDERING, ICON_FA_LIGHTBULB, "Rendering"},
      {SETTINGS_CAT_CAMERA, ICON_FA_VIDEO, "Camera"},
      {SETTINGS_CAT_STEREO, ICON_FA_EYE, "Stereo 3D"},
      {SETTINGS_CAT_ENVIRONMENT, ICON_FA_MOUNTAIN, "Environment"},
      {SETTINGS_CAT_INTERFACE, ICON_FA_DESKTOP, "Interface"},
      {SETTINGS_CAT_SPACEMOUSE, ICON_FA_MOUSE, "SpaceMouse"},
      {SETTINGS_CAT_IMPORT, ICON_FA_FILE_IMPORT, "Import"},
      {SETTINGS_CAT_SHORTCUTS, ICON_FA_KEYBOARD, "Shortcuts"},
  };

  float navWidth = 210.0f * scale;
  ImGui::PushStyleColor(ImGuiCol_ChildBg, ImVec4(0.0f, 0.0f, 0.0f, 0.18f));
  ImGui::BeginChild("##SettingsNav", ImVec2(navWidth, 0), true);
  ImGui::PopStyleColor();
  {
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(4 * scale, 6 * scale));
    DrawInlineIcon(ICON_FA_SLIDERS_H, g_StyleColors.accent);
    ImGui::TextUnformatted("Settings");
    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();
    for (const auto &entry : kNavEntries) {
      if (DrawNavItem(entry.icon, entry.label, g_settingsCategory == entry.id))
        g_settingsCategory = entry.id;
    }
    ImGui::PopStyleVar();
  }
  ImGui::EndChild();

  ImGui::SameLine();
  ImGui::BeginChild("##SettingsContent", ImVec2(0, 0), false);

  // ===========================
  // RENDERING TAB
  // ===========================
  if (g_settingsCategory == SETTINGS_CAT_RENDERING) {
      ImGui::PushID("RenderingTab");
      DrawSectionHeader("Lighting System");

      const char *lightingModes[] = {
          "Shadow Mapping", "Voxel Cone Tracing (GI)", "Radiance Raytracing"};
      int currentMode = static_cast<int>(preferences.lightingMode);
      if (ImGui::Combo("Mode", &currentMode, lightingModes,
                       IM_ARRAYSIZE(lightingModes))) {
        preferences.lightingMode = static_cast<GUI::LightingMode>(currentMode);
        ::currentLightingMode = preferences.lightingMode;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Shadow Mapping: Traditional shadows\nVoxel Cone Tracing: "
                     "Global illumination\nRadiance: Ray-traced lighting");

      // Mode-specific settings
      if (preferences.lightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
        ImGui::Spacing();
        DrawSectionHeader("Shadow Mapping Settings");

        if (DrawToggleSwitch("Enable Shadows", &preferences.enableShadows)) {
          ::enableShadows = preferences.enableShadows;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Toggle shadow mapping on/off");

        ImGui::Spacing();
        DrawSectionHeader("HDR Rendering");

        if (DrawToggleSwitch("Enable HDR", &preferences.hdrSettings.enabled)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Enable High Dynamic Range rendering for better lighting");

        if (preferences.hdrSettings.enabled) {
          if (ImGui::SliderFloat("Exposure", &preferences.hdrSettings.exposure,
                                 0.1f, 5.0f, "%.2f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Controls overall brightness of the scene");

          const char *toneMapOperators[] = {"Reinhard",
                                            "ACES",
                                            "Uncharted 2",
                                            "AgX",
                                            "Khronos PBR Neutral",
                                            "Tony McMapface"};
          if (ImGui::Combo("Tone Mapping",
                           &preferences.hdrSettings.toneMapOperator,
                           toneMapOperators, IM_ARRAYSIZE(toneMapOperators))) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          const char *toneMapDescriptions[] = {
              "Reinhard: Simple, classic tone mapping",
              "ACES: Industry standard, cinematic look",
              "Uncharted 2: Popular in games, good contrast",
              "AgX: Modern, perceptually accurate",
              "Khronos PBR Neutral: Neutral for PBR workflows",
              "Tony McMapface: Modern, perceptually motivated"};
          DrawHelpMarker(
              toneMapDescriptions[preferences.hdrSettings.toneMapOperator]);

          if (DrawToggleSwitch("Enable Bloom",
                               &preferences.hdrSettings.enableBloom)) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Add glow effect to bright areas");

          if (preferences.hdrSettings.enableBloom) {
            if (ImGui::SliderFloat("Bloom Threshold",
                                   &preferences.hdrSettings.bloomThreshold,
                                   0.01f, 1.0f, "%.2f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Brightness threshold for bloom effect");

            if (ImGui::SliderFloat("Bloom Intensity",
                                   &preferences.hdrSettings.bloomIntensity,
                                   0.0f, 1.0f, "%.3f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Strength of the bloom effect");
          }

          ImGui::Spacing();
          if (DrawToggleSwitch("Enable FXAA",
                               &preferences.hdrSettings.enableFXAA)) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Fast Approximate Anti-Aliasing. A cheap post-process pass that "
              "smooths jagged edges after tone mapping.");

          if (preferences.hdrSettings.enableFXAA) {
            if (ImGui::SliderFloat("FXAA Sub-pixel",
                                   &preferences.hdrSettings.fxaaSubpixel, 0.0f,
                                   1.0f, "%.2f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Amount of sub-pixel aliasing removal. Higher = "
                           "softer, lower = sharper.");

            if (ImGui::SliderFloat("FXAA Edge Threshold",
                                   &preferences.hdrSettings.fxaaEdgeThreshold,
                                   0.063f, 0.333f, "%.3f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Minimum contrast treated as an edge. Lower = more "
                           "edges smoothed (higher quality, slower).");
          }
        }

        ImGui::Spacing();
        DrawSectionHeader("Screen Space Ambient Occlusion");

        if (DrawToggleSwitch("Enable SSAO", &preferences.ssaoSettings.enabled)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Screen-Space Ambient Occlusion darkens creases and corners "
            "for more realistic lighting. Requires HDR to be enabled.");

        if (preferences.ssaoSettings.enabled) {
          const char *ssaoSampleCounts[] = {"16 (Fast)", "32 (Balanced)",
                                            "64 (Quality)"};
          int ssaoKernelIndex = 2;
          if (preferences.ssaoSettings.kernelSize <= 16)
            ssaoKernelIndex = 0;
          else if (preferences.ssaoSettings.kernelSize <= 32)
            ssaoKernelIndex = 1;
          if (ImGui::Combo("SSAO Samples", &ssaoKernelIndex, ssaoSampleCounts,
                           IM_ARRAYSIZE(ssaoSampleCounts))) {
            const int kernelSizes[] = {16, 32, 64};
            preferences.ssaoSettings.kernelSize = kernelSizes[ssaoKernelIndex];
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Number of samples per pixel. More samples = better quality "
              "but slower");

          if (ImGui::SliderFloat("SSAO Radius",
                                 &preferences.ssaoSettings.radius, 0.1f, 2.0f,
                                 "%.2f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Radius of the hemisphere sample kernel in view space");

          if (ImGui::SliderFloat("SSAO Bias", &preferences.ssaoSettings.bias,
                                 0.001f, 0.1f, "%.3f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Depth bias to prevent self-occlusion artifacts");

          if (ImGui::SliderFloat("SSAO Power", &preferences.ssaoSettings.power,
                                 0.5f, 5.0f, "%.1f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Power curve to control occlusion intensity. Higher "
                         "values = stronger darkening");
        }

        ImGui::Spacing();
        DrawSectionHeader("Shadow Quality");

        const char *pcfKernelSizes[] = {"3x3 (Fast)", "5x5 (Balanced)",
                                        "7x7 (Quality)", "9x9 (High Quality)"};
        int kernelIndex = (preferences.shadowSettings.pcfKernelSize - 3) / 2;
        kernelIndex = glm::clamp(kernelIndex, 0, 3);
        if (ImGui::Combo("PCF Kernel Size", &kernelIndex, pcfKernelSizes,
                         IM_ARRAYSIZE(pcfKernelSizes))) {
          preferences.shadowSettings.pcfKernelSize = 3 + (kernelIndex * 2);
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Size of the shadow filtering kernel. Larger = softer "
                       "shadows but slower");

        if (ImGui::Checkbox("Enable PCSS",
                            &preferences.shadowSettings.enablePCSS)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Percentage Closer Soft Shadows - realistic variable "
                       "shadow softness");

        if (preferences.shadowSettings.enablePCSS) {
          if (ImGui::SliderFloat("Light Size",
                                 &preferences.shadowSettings.lightSize, 0.01f,
                                 1.0f, "%.3f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Size of the light source for PCSS calculations");
        }

        if (ImGui::SliderFloat("Shadow Softness",
                               &preferences.shadowSettings.shadowSoftness, 0.1f,
                               30.0f, "%.2f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Adjusts the softness of shadow edges (affects both "
                       "standard PCF and PCSS)");

        ImGui::Spacing();
        DrawSectionHeader("Indirect Lighting (Global Illumination)");

        if (ImGui::Checkbox(
                "Enable Indirect Lighting",
                &preferences.shadowSettings.enableIndirectLighting)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Add real-time diffuse global illumination (DDGI light probes) to "
            "shadow mapping mode. Direct lighting stays shadow-mapped; this only "
            "adds the indirect bounce. Shares the same probe volume and tuning "
            "as Radiance mode.");

        if (preferences.shadowSettings.enableIndirectLighting) {
          drawDDGISettings();
        }

        ImGui::Spacing();
        DrawSectionHeader("Material Enhancement");

        if (DrawToggleSwitch("Enable PBR Materials",
                             &preferences.materialSettings.enablePBR)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Physically Based Rendering for realistic materials");

        if (ImGui::Checkbox("Ambient Occlusion Maps",
                            &preferences.materialSettings.enableAO)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Use ambient occlusion textures when a model provides them");

        if (ImGui::Checkbox("Normal Mapping",
                            &preferences.materialSettings.enableNormalMapping)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Use normal-map textures for fine surface detail");

        if (ImGui::Checkbox(
                "Parallax Mapping",
                &preferences.materialSettings.enableParallaxMapping)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Use height-map textures to fake surface depth "
                       "(requires a height map on the model)");

      } else if (preferences.lightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
        ImGui::Spacing();
        DrawSectionHeader("VCT Components");

        if (ImGui::Checkbox("Indirect Diffuse",
                            &preferences.vctSettings.indirectDiffuseLight)) {
          vctSettings.indirectDiffuseLight =
              preferences.vctSettings.indirectDiffuseLight;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Color bleeding and bounce lighting");

        if (ImGui::Checkbox("Indirect Specular",
                            &preferences.vctSettings.indirectSpecularLight)) {
          vctSettings.indirectSpecularLight =
              preferences.vctSettings.indirectSpecularLight;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Glossy reflections from environment");

        if (ImGui::Checkbox("Direct Lighting",
                            &preferences.vctSettings.directLight)) {
          vctSettings.directLight = preferences.vctSettings.directLight;
          settingsChanged = true;
        }

        if (ImGui::Checkbox("Soft Shadows", &preferences.vctSettings.shadows)) {
          vctSettings.shadows = preferences.vctSettings.shadows;
          settingsChanged = true;
        }

        DrawSectionHeader("VCT Quality");

        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.2f, 0.3f, 0.8f, 0.4f));
        if (ImGui::Button("Low", ImVec2(100, 0))) {
          preferences.vctSettings.diffuseConeCount = 1;
          preferences.vctSettings.shadowSampleCount = 5;
          preferences.vctSettings.shadowStepMultiplier = 0.3f;
          preferences.vctSettings.tracingMaxDistance = 1.0f;
          vctSettings = preferences.vctSettings;
          settingsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Medium", ImVec2(100, 0))) {
          preferences.vctSettings.diffuseConeCount = 5;
          preferences.vctSettings.shadowSampleCount = 8;
          preferences.vctSettings.shadowStepMultiplier = 0.2f;
          preferences.vctSettings.tracingMaxDistance = 1.5f;
          vctSettings = preferences.vctSettings;
          settingsChanged = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("High", ImVec2(100, 0))) {
          preferences.vctSettings.diffuseConeCount = 6;
          preferences.vctSettings.shadowSampleCount = 15;
          preferences.vctSettings.shadowStepMultiplier = 0.1f;
          preferences.vctSettings.tracingMaxDistance = 2.0f;
          vctSettings = preferences.vctSettings;
          settingsChanged = true;
        }
        ImGui::PopStyleColor();

        ImGui::Spacing();

        const char *coneOptions[] = {"1 (Fast)", "5 (Balanced)", "6 (Quality)"};
        int coneIndex = preferences.vctSettings.diffuseConeCount <= 1   ? 0
                        : preferences.vctSettings.diffuseConeCount <= 5 ? 1
                                                                        : 2;
        if (ImGui::Combo("Cone Count", &coneIndex, coneOptions,
                         IM_ARRAYSIZE(coneOptions))) {
          switch (coneIndex) {
          case 0:
            preferences.vctSettings.diffuseConeCount = 1;
            break;
          case 1:
            preferences.vctSettings.diffuseConeCount = 5;
            break;
          case 2:
            preferences.vctSettings.diffuseConeCount = 6;
            break;
          }
          vctSettings.diffuseConeCount =
              preferences.vctSettings.diffuseConeCount;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Controls the number of cones used for indirect diffuse "
            "lighting.\n6 cones with 60° aperture provides optimal hemisphere "
            "coverage.\nMore cones = better quality but slower performance");

        if (ImGui::SliderFloat("Trace Distance",
                               &preferences.vctSettings.tracingMaxDistance,
                               0.5f, 2.5f, "%.1f units")) {
          vctSettings.tracingMaxDistance =
              preferences.vctSettings.tracingMaxDistance;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Maximum distance for tracing cones (in grid units).\nLarger "
            "values capture more distant lighting but reduce performance");

        int shadowSamples = preferences.vctSettings.shadowSampleCount;
        if (ImGui::SliderInt("Shadow Samples", &shadowSamples, 5, 20)) {
          preferences.vctSettings.shadowSampleCount = shadowSamples;
          vctSettings.shadowSampleCount = shadowSamples;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Number of samples taken when tracing shadow cones.\nMore samples "
            "= smoother shadows but slower performance");

        if (ImGui::SliderFloat("Shadow Step Multiplier",
                               &preferences.vctSettings.shadowStepMultiplier,
                               0.05f, 0.5f, "%.3f")) {
          vctSettings.shadowStepMultiplier =
              preferences.vctSettings.shadowStepMultiplier;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Controls how fast shadow cone tracing advances.\nLarger values "
            "are faster but may miss details");

        DrawSectionHeader("Grid Configuration");

        float gridSize = voxelizer->getVoxelGridSize();
        if (ImGui::SliderFloat("Grid Dimensions", &gridSize, 1.0f, 50.0f,
                               "%.1f")) {
          voxelizer->setVoxelGridSize(gridSize);
        }
        ImGui::SameLine();
        DrawHelpMarker("World space area coverage for voxelization (larger = "
                       "more world captured, more voxels)");

        float voxelSize = preferences.vctSettings.voxelSize;
        if (ImGui::SliderFloat("VCT Voxel Resolution", &voxelSize,
                               1.0f / 256.0f, 1.0f / 32.0f, "%.5f")) {
          preferences.vctSettings.voxelSize = voxelSize;
          vctSettings.voxelSize = voxelSize;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Voxel resolution for cone tracing calculations "
                       "(smaller = higher quality but slower)");

        DrawSectionHeader("VCT Debug");

        ImGui::Checkbox("Show Voxels", &voxelizer->showDebugVisualization);
        ImGui::SameLine();
        DrawHelpMarker("Visualize the voxel grid as colored cubes");

        if (voxelizer->showDebugVisualization) {
          // Mipmap level selector
          int mipLevel = voxelizer->getDebugMipLevel();
          int maxMip = voxelizer->getMaxMipLevels() - 1;
          if (ImGui::SliderInt("Mip Level", &mipLevel, 0, maxMip)) {
            voxelizer->setDebugMipLevel(mipLevel);
          }
          ImGui::SameLine();
          {
            int res =
                voxelizer->getResolution() >> voxelizer->getDebugMipLevel();
            char buf[64];
            snprintf(buf, sizeof(buf), "Effective resolution: %dx%dx%d", res,
                     res, res);
            DrawHelpMarker(buf);
          }

          // Visualization mode combo
          {
            const char *vizModes[] = {"Color", "Luminance", "Alpha",
                                      "Emissive"};
            int currentViz = static_cast<int>(voxelizer->visualizationMode);
            if (ImGui::Combo("Viz Mode", &currentViz, vizModes,
                             IM_ARRAYSIZE(vizModes))) {
              voxelizer->visualizationMode =
                  static_cast<Engine::Voxelizer::VisualizationMode>(currentViz);
            }
          }

          ImGui::Separator();

          if (ImGui::SliderFloat("Voxel Size", &voxelizer->debugVoxelSize,
                                 0.001f, 0.1f, "%.4f")) {
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Visual size of debug voxel cubes (scaled by 2^mipLevel)");

          if (ImGui::SliderFloat("Opacity", &voxelizer->voxelOpacity, 0.0f,
                                 1.0f, "%.2f")) {
          }

          if (ImGui::SliderFloat("Color Intensity",
                                 &voxelizer->voxelColorIntensity, 0.0f, 5.0f,
                                 "%.1f")) {
          }

          ImGui::Checkbox("Wireframe", &voxelizer->debugWireframe);
          ImGui::SameLine();
          DrawHelpMarker("Render voxel cubes as wireframe outlines");
        }
      } else if (preferences.lightingMode == GUI::LIGHTING_RADIANCE) {
        ImGui::Spacing();
        DrawSectionHeader("Raytracing Settings");

        if (DrawToggleSwitch("Enable Raytracing",
                             &preferences.radianceSettings.enableRaytracing)) {
          ::radianceSettings.enableRaytracing =
              preferences.radianceSettings.enableRaytracing;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Enable proper raytracing from camera through pixels");

        DrawSectionHeader("Performance");

        if (ImGui::SliderInt("Max Bounces",
                             &preferences.radianceSettings.maxBounces, 1, 4)) {
          ::radianceSettings.maxBounces =
              preferences.radianceSettings.maxBounces;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Maximum number of ray bounces (1=direct lighting only, "
                       "2+=indirect lighting)");

        if (ImGui::SliderInt("Samples/Pixel",
                             &preferences.radianceSettings.samplesPerPixel, 1,
                             100)) {
          ::radianceSettings.samplesPerPixel =
              preferences.radianceSettings.samplesPerPixel;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Number of rays cast per pixel (higher = better "
                       "quality, lower performance)");

        if (ImGui::SliderFloat("Ray Max Distance",
                               &preferences.radianceSettings.rayMaxDistance,
                               10.0f, 100.0f, "%.1f")) {
          ::radianceSettings.rayMaxDistance =
              preferences.radianceSettings.rayMaxDistance;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Maximum distance for ray casting");

        DrawSectionHeader("Lighting Features");

        if (ImGui::Checkbox(
                "Indirect Lighting",
                &preferences.radianceSettings.enableIndirectLighting)) {
          ::radianceSettings.enableIndirectLighting =
              preferences.radianceSettings.enableIndirectLighting;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Enable indirect lighting through ray bounces");

        if (ImGui::Checkbox(
                "Emissive Lighting",
                &preferences.radianceSettings.enableEmissiveLighting)) {
          ::radianceSettings.enableEmissiveLighting =
              preferences.radianceSettings.enableEmissiveLighting;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Use emissive objects as light sources");

        DrawSectionHeader("Intensity Controls");

        if (ImGui::SliderFloat("Indirect Intensity",
                               &preferences.radianceSettings.indirectIntensity,
                               0.0f, 1.0f, "%.2f")) {
          ::radianceSettings.indirectIntensity =
              preferences.radianceSettings.indirectIntensity;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Intensity of indirect lighting contribution");

        if (ImGui::SliderFloat("Sky Intensity",
                               &preferences.radianceSettings.skyIntensity, 0.0f,
                               2.0f, "%.2f")) {
          ::radianceSettings.skyIntensity =
              preferences.radianceSettings.skyIntensity;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Brightness of sky lighting when rays miss geometry");

        if (ImGui::SliderFloat("Emissive Intensity",
                               &preferences.radianceSettings.emissiveIntensity,
                               0.0f, 3.0f, "%.2f")) {
          ::radianceSettings.emissiveIntensity =
              preferences.radianceSettings.emissiveIntensity;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Multiplier for emissive object brightness");

        if (ImGui::SliderFloat("Material Roughness",
                               &preferences.radianceSettings.materialRoughness,
                               0.0f, 1.0f, "%.2f")) {
          ::radianceSettings.materialRoughness =
              preferences.radianceSettings.materialRoughness;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Global material roughness (0=mirror, 1=diffuse)");

        DrawSectionHeader("Soft Shadows");

        if (ImGui::SliderInt("Shadow Samples",
                             &preferences.radianceSettings.shadowSamples, 1,
                             16)) {
          ::radianceSettings.shadowSamples =
              preferences.radianceSettings.shadowSamples;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Shadow rays per light for direct lighting. 1 = hard "
                       "shadows; higher = smoother penumbra (more cost).");

        if (ImGui::SliderFloat("Shadow Softness",
                               &preferences.radianceSettings.shadowSoftness,
                               0.0f, 1.0f, "%.2f")) {
          ::radianceSettings.shadowSoftness =
              preferences.radianceSettings.shadowSoftness;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Penumbra size: angular size of the sun and radius of "
                       "point/spot sources. 0 = hard shadows.");

        DrawSectionHeader("Acceleration");

        if (ImGui::Checkbox("Enable BVH",
                            &preferences.radianceSettings.enableBVH)) {
          ::enableBVH = preferences.radianceSettings.enableBVH;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Bounding Volume Hierarchy for faster ray-triangle tests");

        if (ImGui::Checkbox("Debug BVH",
                            &preferences.radianceSettings.showBVHDebug)) {
          ::showBVHDebug = preferences.radianceSettings.showBVHDebug;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Visualize BVH bounding boxes with color-coded depth "
                       "levels:\nLevel 0: Red, Level 1: Orange, Level 2: "
                       "Yellow, Level 3: Green\nLevel 4: Cyan, Level 5: Blue, "
                       "Level 6: Purple, Level 7: Magenta");

        if (preferences.radianceSettings.showBVHDebug) {
          ImGui::Indent();

          if (ImGui::SliderInt("BVH Max Depth",
                               &preferences.radianceSettings.bvhDebugMaxDepth,
                               1, 8)) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Maximum BVH depth levels to display (1=root only, higher=more "
              "detail)\nEach level has a distinct color: "
              "Red→Orange→Yellow→Green→Cyan→Blue→Purple→Magenta");

          const char *renderModeNames[] = {"Depth Tested", "Always On Top",
                                           "Depth Biased"};
          if (ImGui::Combo("Render Mode",
                           &preferences.radianceSettings.bvhDebugRenderMode,
                           renderModeNames, 3)) {
            Engine::BVHDebugRenderer::RenderMode mode =
                static_cast<Engine::BVHDebugRenderer::RenderMode>(
                    preferences.radianceSettings.bvhDebugRenderMode);
            ::bvhDebugRenderer.setRenderMode(mode);
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Depth Tested: lines behind geometry hidden\nAlways On Top: "
              "lines always visible\nDepth Biased: lines slightly in front");

          ImGui::Unindent();
        }

        // Dynamic Diffuse GI (DDGI) Section
        ImGui::Separator();
        DrawSectionHeader("Dynamic Diffuse GI (DDGI)");

        if (DrawToggleSwitch("Enable DDGI",
                             &preferences.radianceSettings.enableDDGI)) {
          ::radianceSettings.enableDDGI =
              preferences.radianceSettings.enableDDGI;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Real-time diffuse global illumination from a grid of "
                       "light probes (irradiance + visibility). Updated every "
                       "frame via the BVH - no precompute.");

        if (preferences.radianceSettings.enableDDGI) {
          drawDDGISettings();
        }
      }

      // ---- Point cloud rendering (applies in every lighting mode) ----
      ImGui::Spacing();
      DrawSectionHeader("Point Clouds");

      if (ImGui::SliderFloat("Point Radius", &preferences.pointCloudBaseSize,
                             0.001f, 0.2f, "%.3f m",
                             ImGuiSliderFlags_Logarithmic)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("World-space radius of each point in metres. On-screen "
                     "size follows perspective (closer points appear larger).");

      ImGui::Spacing();
      if (DrawToggleSwitch("Enable EDL", &preferences.edlSettings.enabled)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Eye-Dome Lighting adds depth cues to point clouds by darkening "
          "depth discontinuities. Requires HDR to be enabled.");

      if (preferences.edlSettings.enabled) {
        if (ImGui::SliderFloat("EDL Strength",
                               &preferences.edlSettings.strength, 0.1f, 5.0f,
                               "%.2f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Controls how strongly depth edges are darkened. "
                       "1.0 = default Potree strength.");

        if (ImGui::SliderFloat("EDL Radius", &preferences.edlSettings.radius,
                               0.5f, 5.0f, "%.1f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Neighbourhood sampling radius in pixels. "
                       "Larger values detect wider depth transitions.");
      }

      ImGui::Spacing();
      if (DrawToggleSwitch("Enable Splatting",
                           &preferences.pointSplatSettings.enabled)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Widens each point into a small round splat sized from its on-screen "
          "spacing, filling the gaps that appear when you view the cloud close "
          "up or in a sparse area. Distant / dense views collapse back to "
          "single pixels, so the cost is paid only where it's needed.");

      if (preferences.pointSplatSettings.enabled) {
        if (ImGui::SliderInt("Max Splat Radius",
                             &preferences.pointSplatSettings.maxRadius, 1, 8,
                             "%d px")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Upper limit on the splat radius in pixels. Higher = "
                       "fills larger gaps when extremely close, but costs more "
                       "atomic writes. 3-4 is a good balance.");
      }

      ImGui::PopID();
  }

    // ===========================
    // CAMERA TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_CAMERA) {
      ImGui::PushID("CameraTab");
      DrawSectionHeader("View Settings");

      if (ImGui::SliderFloat("Field of View", &camera.Zoom, 1.0f, 120.0f,
                             "%.0f°")) {
        preferences.fov = camera.Zoom;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Controls the camera's field of view. Higher values show "
                     "more of the scene");

      if (ImGui::SliderFloat("Near Clip", &preferences.nearPlane, 0.01f, 10.0f,
                             "%.2f")) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Minimum visible distance from camera. Smaller values can "
                     "cause visual artifacts");

      if (ImGui::SliderFloat("Far Clip", &preferences.farPlane, 10.0f, 1000.0f,
                             "%.0f")) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Maximum visible distance from camera. Higher values may "
                     "impact performance");

      DrawSectionHeader("Movement");

      if (ImGui::SliderFloat("Mouse Sensitivity", &camera.MouseSensitivity,
                             0.01f, 0.08f, "%.3f")) {
        preferences.mouseSensitivity = camera.MouseSensitivity;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Adjusts how quickly the camera rotates in response to "
                     "mouse movement");

      if (ImGui::SliderFloat("Smoothing", &mouseSmoothingFactor, 0.1f, 1.0f,
                             "%.1f")) {
        preferences.mouseSmoothingFactor = mouseSmoothingFactor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Controls smoothness of mouse movement. Lower values = "
                     "smoother, higher values = more responsive");

      if (ImGui::SliderFloat("Speed Multiplier", &camera.speedFactor, 0.1f,
                             5.0f, "%.1fx")) {
        preferences.cameraSpeedFactor = camera.speedFactor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Multiplies base movement speed. Useful for navigating "
                     "larger scenes");

      if (ImGui::Checkbox("Zoom to Cursor", &camera.zoomToCursor)) {
        preferences.zoomToCursor = camera.zoomToCursor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("When enabled, scrolling zooms toward the cursor position "
                     "(including background areas)");

      DrawSectionHeader("Orbit Behavior");

      bool standardOrbit = !camera.orbitAroundCursor && !orbitFollowsCursor;
      bool orbitAroundCursorOption = camera.orbitAroundCursor;
      bool orbitFollowsCursorOption = orbitFollowsCursor;

      if (ImGui::RadioButton("Standard Orbit", standardOrbit)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = false;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Orbits around the viewport center at cursor depth");

      if (ImGui::RadioButton("Orbit Around Cursor", orbitAroundCursorOption)) {
        camera.orbitAroundCursor = true;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = true;
        preferences.orbitFollowsCursor = false;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Orbits around the 3D position of the cursor without "
                     "centering the view");

      if (ImGui::RadioButton("Orbit Follows Cursor (Center)",
                             orbitFollowsCursorOption)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = true;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = true;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Centers the view on cursor position before orbiting");

      DrawSectionHeader("Smooth Scrolling");

      if (DrawToggleSwitch("Enable Smooth Scrolling",
                           &camera.useSmoothScrolling)) {
        preferences.useSmoothScrolling = camera.useSmoothScrolling;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Enable physics-based smooth scrolling");

      if (camera.useSmoothScrolling) {
        ImGui::Indent();
        if (ImGui::SliderFloat("Momentum", &camera.scrollMomentum, 0.0f, 1.0f,
                               "%.2f")) {
          preferences.scrollMomentum = camera.scrollMomentum;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Controls how much scrolling 'carries' (higher = more momentum)");

        if (ImGui::SliderFloat("Max Speed", &camera.maxScrollVelocity, 0.5f,
                               10.0f, "%.1f")) {
          preferences.maxScrollVelocity = camera.maxScrollVelocity;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Maximum scroll speed");

        if (ImGui::SliderFloat("Deceleration", &camera.scrollDeceleration, 1.0f,
                               20.0f, "%.1f")) {
          preferences.scrollDeceleration = camera.scrollDeceleration;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("How quickly scrolling slows down");
        ImGui::Unindent();
      }

      ImGui::PopID();
  }

    // ===========================
    // STEREO 3D TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_STEREO) {
      ImGui::PushID("StereoTab");
      DrawSectionHeader("Depth & Convergence");

      if (ImGui::SliderFloat("Eye Separation", &preferences.separation, 0.01f,
                             2.0f, "%.2f")) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Adjusts the distance between stereo views. Higher values "
                     "increase 3D effect");

      if (DrawToggleSwitch("Auto Convergence", &preferences.autoConvergence)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Automatically sets convergence based on distance to nearest object");

      if (preferences.autoConvergence) {
        if (ImGui::SliderFloat("Distance Factor",
                               &preferences.convergenceDistanceFactor, 0.1f,
                               2.0f, "%.1fx")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Multiplier for the distance to nearest object used for "
                       "convergence");

        if (ImGui::SliderFloat("Smoothing Speed", &convergenceSmoothingSpeed,
                               0.5f, 20.0f, "%.1f")) {
          preferences.convergenceSmoothingSpeed = convergenceSmoothingSpeed;
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Speed of convergence transition. Higher values = faster "
            "transitions, lower values = smoother but slower");

        if (ImGui::Checkbox("Enable Convergence Cap",
                            &preferences.enableConvergenceCap)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Limit auto convergence to a specific range");

        if (preferences.enableConvergenceCap) {
          if (ImGui::SliderFloat("Min Convergence",
                                 &preferences.convergenceCapMin, 0.1f, 20.0f,
                                 "%.1f")) {
            // Ensure min doesn't exceed max
            if (preferences.convergenceCapMin > preferences.convergenceCapMax) {
              preferences.convergenceCapMin = preferences.convergenceCapMax;
            }
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Minimum convergence distance when auto convergence is active");

          if (ImGui::SliderFloat("Max Convergence",
                                 &preferences.convergenceCapMax, 0.1f, 100.0f,
                                 "%.1f")) {
            // Ensure max doesn't go below min
            if (preferences.convergenceCapMax < preferences.convergenceCapMin) {
              preferences.convergenceCapMax = preferences.convergenceCapMin;
            }
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Maximum convergence distance when auto convergence is active");
        }

        ImGui::Text("Current Convergence: %.2f", preferences.convergence);
        ImGui::SameLine();
        DrawHelpMarker(
            "Auto-calculated convergence distance based on nearest object");
      } else {
        if (ImGui::SliderFloat("Convergence", &preferences.convergence, 0.0f,
                               40.0f, "%.1f")) {
          settingsChanged = true;
        }
        // Lock cursor position while adjusting convergence to prevent it from
        // sliding
        cursorManager.setPositionLocked(ImGui::IsItemActive());

        ImGui::SameLine();
        DrawHelpMarker("Sets the focal point distance where left and right "
                       "views converge");
      }

      if (ImGui::Checkbox("Flip Eyes", &preferences.flipEyes)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Swaps the left and right eye views for stereoscopic display");

      DrawSectionHeader("Stereo Overlays");

      if (DrawToggleSwitch("Show Radar", &preferences.radarEnabled)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Show a radar overlay with the camera frustum");

      if (preferences.radarEnabled) {
        ImGui::Indent();
        if (ImGui::SliderFloat2("Position",
                                glm::value_ptr(preferences.radarPos), -1.0f,
                                1.0f, "%.2f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Horizontal and vertical position of the radar (-1 to 1)");

        if (ImGui::SliderFloat("Size", &preferences.radarRadius, 0.05f, 0.5f,
                               "%.2f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("On-screen radius of the radar scope");

        if (ImGui::Checkbox("Auto-fit to Convergence",
                            &preferences.radarAutoFit)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Automatically scale the radar so the convergence plane (green "
            "line) always sits inside the scope. Turn off for manual zoom.");

        if (!preferences.radarAutoFit) {
          if (ImGui::SliderFloat("Zoom", &preferences.radarScale, 0.001f, 0.5f,
                                 "%.3f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "How much of the scene fits inside the scope (world units to "
              "radar units). Lower zooms out to show more of the scene.");
        }

        if (ImGui::SliderFloat("Frustum Separation",
                               &preferences.radarFrustumSpread, 0.0f, 0.4f,
                               "%.2f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Exaggerate the gap between the left/right eye frustums so they "
            "are distinguishable at comfortable separations. The convergence "
            "crossing stays accurate. 0 = true-to-life.");

        if (ImGui::Checkbox("Show Scene in Radar",
                            &preferences.radarShowScene)) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Show the scene models in the radar view");

        if (preferences.radarShowScene) {
          if (ImGui::SliderFloat("Scene Brightness",
                                 &preferences.radarSceneBrightness, 1.0f, 30.0f,
                                 "%.1f")) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Exposure boost for the top-down scene in the radar. The radar "
              "skips the normal HDR/bloom tone-map, so crank this up to keep "
              "the floor plan bright and readable (not realistic).");

          if (ImGui::Checkbox("Slice Scene (Top-Down)",
                              &preferences.radarSliceEnabled)) {
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Cut away geometry above the camera so building interiors are "
              "visible from above instead of just the roof.");

          if (preferences.radarSliceEnabled) {
            if (ImGui::SliderFloat("Slice Height", &preferences.radarSliceOffset,
                                   -2.0f, 10.0f, "%.2f")) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker(
                "Height of the slice plane above the camera (world units). "
                "Lower values cut closer to the floor.");
          }
        }
        ImGui::Unindent();
      }

      if (DrawToggleSwitch("Show Zero Plane", &preferences.showZeroPlane)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Display the zero plane in the visualization");

      ImGui::PopID();
  }

    // ===========================
    // ENVIRONMENT TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_ENVIRONMENT) {
      ImGui::PushID("EnvironmentTab");
      DrawSectionHeader("Skybox");

      const char *skyboxTypes[] = {"Cubemap", "HDR", "Solid Color", "Gradient"};
      int currentType = static_cast<int>(skyboxConfig.type);
      if (ImGui::Combo("Type", &currentType, skyboxTypes,
                       IM_ARRAYSIZE(skyboxTypes))) {
        skyboxConfig.type = static_cast<GUI::SkyboxType>(currentType);
        updateSkybox();
        preferences.skyboxType = static_cast<int>(skyboxConfig.type);
        savePreferences();
      }
      ImGui::SameLine();
      DrawHelpMarker("Change the type of skybox used in the scene");

      if (skyboxConfig.type == GUI::SKYBOX_CUBEMAP) {
        std::vector<const char *> presetNames;
        for (const auto &preset : cubemapPresets) {
          presetNames.push_back(preset.name.c_str());
        }

        if (ImGui::Combo("Theme", &skyboxConfig.selectedCubemap,
                         presetNames.data(),
                         static_cast<int>(presetNames.size()))) {
          updateSkybox();
          preferences.selectedCubemap = skyboxConfig.selectedCubemap;
          savePreferences();
        }

        if (skyboxConfig.selectedCubemap >= 0 &&
            skyboxConfig.selectedCubemap < cubemapPresets.size()) {
          ImGui::SameLine();
          DrawHelpMarker(
              cubemapPresets[skyboxConfig.selectedCubemap].description.c_str());
        }

        if (ImGui::Button("Browse Custom...", ImVec2(-1, 0))) {
          auto selection =
              pfd::select_folder("Select skybox directory").result();
          if (!selection.empty()) {
            std::string path = selection + "/";
            std::string dirName =
                std::filesystem::path(selection).filename().string();

            GUI::CubemapPreset newPreset;
            newPreset.name = "Custom: " + dirName;
            newPreset.path = path;
            newPreset.description = "Custom skybox";
            cubemapPresets.push_back(newPreset);

            skyboxConfig.selectedCubemap =
                static_cast<int>(cubemapPresets.size()) - 1;
            updateSkybox();
            preferences.selectedCubemap = skyboxConfig.selectedCubemap;
            savePreferences();
          }
        }
        ImGui::SameLine();
        DrawHelpMarker(
            "Select a directory containing skybox textures (right.jpg, "
            "left.jpg, etc. OR posx.jpg, negx.jpg, etc.)");
      } else if (skyboxConfig.type == GUI::SKYBOX_HDR) {
        ImGui::Text("HDR File:");
        ImGui::SameLine();

        // Display current HDR path or "None selected"
        std::string displayPath =
            skyboxConfig.hdrPath.empty()
                ? "None selected"
                : skyboxConfig.hdrPath.substr(
                      skyboxConfig.hdrPath.find_last_of("/\\") + 1);
        ImGui::TextDisabled("%s", displayPath.c_str());

        if (ImGui::Button("Browse HDR File...")) {
          auto selection =
              pfd::open_file("Select HDR environment map", ".",
                             {"HDR Images", "*.hdr", "All Files", "*"})
                  .result();
          if (!selection.empty()) {
            skyboxConfig.hdrPath = selection[0];
            updateSkybox();
            preferences.skyboxHdrPath = skyboxConfig.hdrPath;
            savePreferences();
          }
        }
        ImGui::SameLine();
        DrawHelpMarker("Select an HDR (.hdr) equirectangular environment map");

        // Exposure control for HDR skyboxes
        if (ImGui::SliderFloat("HDR Exposure", &preferences.skyboxExposure,
                               0.01f, 2.0f, "%.2f",
                               ImGuiSliderFlags_Logarithmic)) {
          savePreferences();
        }
        ImGui::SameLine();
        DrawHelpMarker("Controls the brightness of the HDR skybox. Most HDR "
                       "environment maps need values between 0.1-0.5");
      } else if (skyboxConfig.type == GUI::SKYBOX_SOLID_COLOR) {
        if (ImGui::ColorEdit3("Color",
                              glm::value_ptr(skyboxConfig.solidColor))) {
          updateSkybox();
          preferences.skyboxSolidColor = skyboxConfig.solidColor;
          savePreferences();
        }
        ImGui::SameLine();
        DrawHelpMarker("Set a single color for the entire skybox");
      } else if (skyboxConfig.type == GUI::SKYBOX_GRADIENT) {
        bool changed = false;
        changed |= ImGui::ColorEdit3(
            "Top", glm::value_ptr(skyboxConfig.gradientTopColor));
        ImGui::SameLine();
        DrawHelpMarker("Color of the top portion of the skybox");

        changed |= ImGui::ColorEdit3(
            "Bottom", glm::value_ptr(skyboxConfig.gradientBottomColor));
        ImGui::SameLine();
        DrawHelpMarker("Color of the bottom portion of the skybox");

        if (changed) {
          updateSkybox();
          preferences.skyboxGradientTop = skyboxConfig.gradientTopColor;
          preferences.skyboxGradientBottom = skyboxConfig.gradientBottomColor;
          savePreferences();
        }
      }

      if (ImGui::SliderFloat("Ambient Light", &ambientStrengthFromSkybox, 0.0f,
                             1.0f, "%.2f")) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Controls how much the skybox illuminates the scene. "
                     "Higher values create brighter ambient lighting");

      ImGui::Spacing();
      ImGui::TextDisabled("Tip: select \"Sun\" in the Scene Hierarchy to edit "
                          "sun light direction, color and intensity.");

      ImGui::PopID();
  }

    // ===========================
    // INTERFACE TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_INTERFACE) {
      ImGui::PushID("InterfaceTab");
      DrawSectionHeader("Interface");

      if (DrawToggleSwitch("Dark Theme", &isDarkTheme)) {
        SetupImGuiStyle(isDarkTheme, 1.0f);
        preferences.isDarkTheme = isDarkTheme;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Switches between light and dark color themes for the interface");

      if (DrawToggleSwitch("Show FPS", &showFPS)) {
        preferences.showFPS = showFPS;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Shows/hides the FPS counter in the bottom-right corner");

      if (DrawToggleSwitch("Spawn Animation", &preferences.enableSpawnAnimation)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Animate models when they are created or imported");

      ImGui::Spacing();
      DrawSectionHeader("GUI Scale");

      float userFactor = g_GuiScale.userScaleFactor;
      if (ImGui::SliderFloat("Scale Factor", &userFactor,
                             GuiScaleSettings::MIN_USER_FACTOR,
                             GuiScaleSettings::MAX_USER_FACTOR, "%.2fx")) {
        g_GuiScale.userScaleFactor = userFactor;
        if (g_GuiScale.lastWindowWidth > 0 && g_GuiScale.lastWindowHeight > 0) {
          float baseScale = CalculateGuiScale(g_GuiScale.lastWindowWidth,
                                              g_GuiScale.lastWindowHeight);
          float newScale =
              std::max(0.5f, std::min(2.0f, baseScale * userFactor));
          g_GuiScale.currentScale = newScale;
        }
        g_GuiScale.needsRescale = true;
        g_GuiScale.needsFontRebuild = true;
        preferences.guiScaleFactor = userFactor;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Multiplier applied on top of the automatic window-size "
                     "scaling. 1.0x = default, 0.5x = smaller, 2.0x = larger");

      if (ImGui::Button("Reset Scale")) {
        g_GuiScale.userScaleFactor = 1.0f;
        if (g_GuiScale.lastWindowWidth > 0 && g_GuiScale.lastWindowHeight > 0) {
          float baseScale = CalculateGuiScale(g_GuiScale.lastWindowWidth,
                                              g_GuiScale.lastWindowHeight);
          g_GuiScale.currentScale = std::max(0.5f, std::min(2.0f, baseScale));
        }
        g_GuiScale.needsRescale = true;
        g_GuiScale.needsFontRebuild = true;
        preferences.guiScaleFactor = 1.0f;
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Reset GUI scale factor to default (1.0x)");


      DrawSectionHeader("Startup");

      if (DrawToggleSwitch("Load Scene on Start",
                           &preferences.loadStartupScene)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Automatically load the specified scene file when the "
                     "application starts");

      if (preferences.loadStartupScene) {
        static char scenePathBuffer[1024];
        if (strlen(scenePathBuffer) == 0 &&
            !preferences.startupScenePath.empty()) {
          strncpy_s(scenePathBuffer, preferences.startupScenePath.c_str(),
                    sizeof(scenePathBuffer) - 1);
        }

        ImGui::InputText("Scene Path", scenePathBuffer,
                         sizeof(scenePathBuffer));
        ImGui::SameLine();
        DrawHelpMarker("Path to the .scene file to load on startup");

        ImGui::SameLine();
        if (ImGui::Button("Browse")) {
          auto result =
              pfd::open_file("Select Scene", "", {"Scene Files", "*.scene"});
          if (!result.result().empty()) {
            std::string selectedPath = result.result()[0];
            strncpy_s(scenePathBuffer, selectedPath.c_str(),
                      sizeof(scenePathBuffer) - 1);
            preferences.startupScenePath = selectedPath;
            settingsChanged = true;
          }
        }

        if (strcmp(scenePathBuffer, preferences.startupScenePath.c_str()) !=
            0) {
          preferences.startupScenePath = std::string(scenePathBuffer);
          settingsChanged = true;
        }
      }

      ImGui::Spacing();
      DrawSectionHeader("Scene Loading");

      const char *sceneLoadingBehaviors[] = {"Always Ask",
                                             "Always Replace Existing",
                                             "Always Merge (Keep Existing)"};
      int currentBehavior = static_cast<int>(preferences.sceneLoadingBehavior);
      if (ImGui::Combo("When Loading Scene", &currentBehavior,
                       sceneLoadingBehaviors, 3)) {
        preferences.sceneLoadingBehavior =
            static_cast<GUI::SceneLoadingBehavior>(currentBehavior);
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Choose default behavior when loading a scene file while "
                     "another scene is already loaded:\n"
                     "• Always Ask: Show dialog each time\n"
                     "• Always Replace: Clear existing scene\n"
                     "• Always Merge: Keep existing scene and add new objects");

      ImGui::PopID();
  }

    // ===========================
    // SPACEMOUSE TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_SPACEMOUSE) {
      ImGui::PushID("SpaceMouseTab");
      DrawSectionHeader("3DConnexion SpaceMouse");

      if (spaceMouseInitialized) {
        if (DrawToggleSwitch("Enable SpaceMouse",
                             &preferences.spaceMouseEnabled)) {
          spaceMouseInput.SetEnabled(preferences.spaceMouseEnabled);
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Enable or disable 3DConnexion SpaceMouse input");

        if (preferences.spaceMouseEnabled) {
          ImGui::Spacing();

          if (ImGui::SliderFloat("Deadzone", &preferences.spaceMouseDeadzone,
                                 0.0f, 0.5f, "%.2f")) {
            spaceMouseInput.SetDeadzone(preferences.spaceMouseDeadzone);
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Movement threshold below which SpaceMouse input is ignored");

          if (ImGui::SliderFloat("Translation",
                                 &preferences.spaceMouseTranslationSensitivity,
                                 0.1f, 3.0f, "%.1fx")) {
            spaceMouseInput.SetSensitivity(
                preferences.spaceMouseTranslationSensitivity,
                preferences.spaceMouseRotationSensitivity);
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Controls how sensitive translation movements are");

          if (ImGui::SliderFloat("Rotation",
                                 &preferences.spaceMouseRotationSensitivity,
                                 0.1f, 3.0f, "%.1fx")) {
            spaceMouseInput.SetSensitivity(
                preferences.spaceMouseTranslationSensitivity,
                preferences.spaceMouseRotationSensitivity);
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Controls how sensitive rotation movements are");

          // Only show anchor mode settings in CAD mode
          if (preferences.spaceMouseNavigationMode == GUI::SPACEMOUSE_NAV_CAD) {
            ImGui::Spacing();
            ImGui::Text("Anchor Point Mode:");

            int currentMode =
                static_cast<int>(preferences.spaceMouseAnchorMode);
            bool modeChanged = false;

            if (ImGui::RadioButton(
                    "Scene Center", &currentMode,
                    static_cast<int>(GUI::SPACEMOUSE_ANCHOR_DISABLED))) {
              modeChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker(
                "Use the scene center as the SpaceMouse pivot point");

            if (ImGui::RadioButton(
                    "Cursor on Start", &currentMode,
                    static_cast<int>(GUI::SPACEMOUSE_ANCHOR_ON_START))) {
              modeChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Set anchor to cursor position when SpaceMouse "
                           "navigation starts, then keep it fixed");

            if (ImGui::RadioButton(
                    "Follow Cursor", &currentMode,
                    static_cast<int>(GUI::SPACEMOUSE_ANCHOR_CONTINUOUS))) {
              modeChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker(
                "Continuously update anchor to follow the cursor position");

            if (ImGui::RadioButton(
                    "Click to Set", &currentMode,
                    static_cast<int>(GUI::SPACEMOUSE_ANCHOR_CLICK))) {
              modeChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker(
                "Left-click in the viewport to fix the SpaceMouse pivot at "
                "that point. Click again anywhere to move the pivot.");

            if (modeChanged) {
              preferences.spaceMouseAnchorMode =
                  static_cast<GUI::SpaceMouseAnchorMode>(currentMode);
              settingsChanged = true;
              updateSpaceMouseCursorAnchor();
              spaceMouseInput.RefreshPivotPosition();
            }

            if (ImGui::Checkbox("Center Cursor During Navigation",
                                &preferences.spaceMouseCenterCursor)) {
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Keep the mouse cursor fixed at the screen center "
                           "while using SpaceMouse");
          }
        }

        // ---- 3DConnexion App Settings (synced to/from 3DxWare XML) ----
        if (tdxSync.IsConnected()) {
          DrawSectionHeader("3DConnexion App Settings");

          // Helper lambda: assemble TdxSettings from preferences + pivot state
          auto buildTdxSettings = [&]() -> ThreeDConnexionSync::TdxSettings {
            ThreeDConnexionSync::TdxSettings s;
            s.motionModel = preferences.tdxSettings.motionModel;
            s.autoPivot = preferences.tdxSettings.autoPivot;
            s.lockHorizon = preferences.tdxSettings.lockHorizon;
            s.suspendInput = preferences.tdxSettings.suspendInput;
            s.lockTo3dViews = preferences.tdxSettings.lockTo3dViews;
            s.moveObjects = preferences.tdxSettings.moveObjects;
            s.autokeyAnimation = preferences.tdxSettings.autokeyAnimation;
            s.selectionFollower = preferences.tdxSettings.selectionFollower;
            s.firstPersonEaseOut = preferences.tdxSettings.firstPersonEaseOut;
            s.floorQueryRate = preferences.tdxSettings.floorQueryRate;
            s.lockSketchPlane = preferences.tdxSettings.lockSketchPlane;
            // Derive pivotVisibility from orbit center prefs
            if (preferences.showOrbitCenter &&
                preferences.alwaysShowOrbitCenter)
              s.pivotVisibility = "ShowPivot";
            else if (preferences.showOrbitCenter)
              s.pivotVisibility = "ShowMovingPivot";
            else
              s.pivotVisibility = "HidePivot";
            return s;
          };

          // Motion Model combo
          {
            const char *motionModels[] = {"Helicopter", "Object", "Fly",
                                          "Walk",       "Orbit",  "Target",
                                          "Drive"};
            int currentModel = 0;
            for (int i = 0; i < 7; ++i) {
              if (preferences.tdxSettings.motionModel == motionModels[i]) {
                currentModel = i;
                break;
              }
            }
            if (ImGui::Combo("Motion Model", &currentModel, motionModels, 7)) {
              preferences.tdxSettings.motionModel = motionModels[currentModel];
              tdxSync.WriteSettings(buildTdxSettings());
              settingsChanged = true;
            }
            ImGui::SameLine();
            DrawHelpMarker("Navigation style used by the SpaceMouse driver");
          }

          if (ImGui::Checkbox("Auto Pivot",
                              &preferences.tdxSettings.autoPivot)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Automatically choose the pivot point based on geometry");

          if (ImGui::Checkbox("Lock Horizon",
                              &preferences.tdxSettings.lockHorizon)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Keep the horizon level while navigating");

          if (ImGui::Checkbox("Suspend Input",
                              &preferences.tdxSettings.suspendInput)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Temporarily disable all SpaceMouse motion input");

          if (ImGui::Checkbox("Lock to 3D Views",
                              &preferences.tdxSettings.lockTo3dViews)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Restrict SpaceMouse input to 3D viewports only");

          if (ImGui::Checkbox("Move Objects",
                              &preferences.tdxSettings.moveObjects)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Move selected objects instead of the camera");

          if (ImGui::Checkbox("Autokey Animation",
                              &preferences.tdxSettings.autokeyAnimation)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Automatically create keyframes during animation");

          if (ImGui::Checkbox("Selection Follower",
                              &preferences.tdxSettings.selectionFollower)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Keep the selection centered in the view");

          if (ImGui::SliderInt("First Person Ease Out",
                               &preferences.tdxSettings.firstPersonEaseOut, 0,
                               1000)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "Deceleration ramp length in first-person mode (0-1000)");

          if (ImGui::SliderInt("Floor Query Rate",
                               &preferences.tdxSettings.floorQueryRate, 1,
                               100)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker(
              "How often (per second) the floor height is queried (1-100)");

          if (ImGui::Checkbox("Lock Sketch Plane",
                              &preferences.tdxSettings.lockSketchPlane)) {
            tdxSync.WriteSettings(buildTdxSettings());
            settingsChanged = true;
          }
          ImGui::SameLine();
          DrawHelpMarker("Lock navigation to the active sketch plane");
        }
      } else {
        ImGui::TextDisabled("SpaceMouse not detected");
        ImGui::TextWrapped(
            "Connect a 3Dconnexion SpaceMouse device to enable 3D navigation.");
      }

      ImGui::PopID();
  }

    // ===========================
    // IMPORT TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_IMPORT) {
      ImGui::PushID("ImportTab");
      DrawSectionHeader("Model Import Options");

      if (ImGui::Checkbox("Flip UV Coordinates",
                          &preferences.modelImportSettings.flipUVs)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Flips the V (Y) coordinate of UV maps. Enable if "
                     "textures appear upside down.");

      if (ImGui::Checkbox("Flip Normals",
                          &preferences.modelImportSettings.flipNormals)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Inverts the direction of all normals. Enable if lighting "
                     "appears inverted (dark faces should be bright).");

      if (ImGui::Checkbox("Generate Normals",
                          &preferences.modelImportSettings.generateNormals)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Automatically generate vertex normals for models that "
                     "don't have them.");

      ImGui::Indent();
      if (ImGui::Checkbox(
              "Use Smooth Normals",
              &preferences.modelImportSettings.generateSmoothNormals)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Generate smooth normals for better shading (only when "
                     "Generate Normals is enabled).");
      ImGui::Unindent();

      if (ImGui::Checkbox(
              "Calculate Tangents",
              &preferences.modelImportSettings.calculateTangentSpace)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Calculate tangent and bitangent vectors for normal mapping.");

      if (ImGui::Checkbox(
              "Merge Vertices",
              &preferences.modelImportSettings.joinIdenticalVertices)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Merge vertices that share the same position, normal, and "
                     "UV coordinates.");

      DrawSectionHeader("Optimization");

      if (ImGui::Checkbox(
              "Sort by Primitive Type",
              &preferences.modelImportSettings.sortByPrimitiveType)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Sort faces by primitive type for better rendering performance.");

      if (ImGui::Checkbox(
              "Fix Inverted Normals",
              &preferences.modelImportSettings.fixInfacingNormals)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "Automatically fix normals that face inward instead of outward.");

      if (ImGui::Checkbox(
              "Remove Redundant Materials",
              &preferences.modelImportSettings.removeRedundantMaterials)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Remove duplicate materials to reduce memory usage.");

      if (ImGui::Checkbox("Optimize Meshes",
                          &preferences.modelImportSettings.optimizeMeshes)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Optimize mesh data for better rendering performance.");

      if (ImGui::Checkbox(
              "Pre-transform Vertices",
              &preferences.modelImportSettings.pretransformVertices)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Apply node transformations directly to vertices. Use for "
                     "static models only.");

      DrawSectionHeader("Auto-Scaling");

      if (DrawToggleSwitch(
              "Auto-scale Large Models",
              &preferences.modelImportSettings.autoScaleLargeModels)) {
        settingsChanged = true;
      }
      ImGui::SameLine();
      DrawHelpMarker("Automatically scale down models that are too large to "
                     "fit comfortably in the scene.");

      if (preferences.modelImportSettings.autoScaleLargeModels) {
        ImGui::Indent();
        if (ImGui::SliderFloat("Max Model Radius",
                               &preferences.modelImportSettings.maxModelRadius,
                               1.0f, 20.0f, "%.1f")) {
          settingsChanged = true;
        }
        ImGui::SameLine();
        DrawHelpMarker("Maximum bounding sphere radius before auto-scaling is "
                       "applied. Default: 5.0 units.");
        ImGui::Unindent();
      }

      ImGui::Spacing();
      if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) {
        preferences.modelImportSettings =
            GUI::ApplicationPreferences::ModelImportSettings{};
        settingsChanged = true;
      }

      ImGui::PopID();
  }

    // ===========================
    // SHORTCUTS TAB
    // ===========================
  if (g_settingsCategory == SETTINGS_CAT_SHORTCUTS) {
      ImGui::PushID("ShortcutsTab");
      DrawSectionHeader("Shortcut Profiles");

      // State variables for shortcut management (static to persist across
      // frames)
      static char profileNameBuffer[256] = "";
      static bool isCreatingProfile = false;
      static bool isRenamingProfile = false;
      static std::string profileToRename = "";
      static int captureActionIndex = -1;
      static int captureSlot = -1;
      static std::string conflictMessage = "";
      static bool waitingForKeyPress = false;

      // Profile selector
      StereoVista::ShortcutProfile *activeProfile =
          shortcutManager.getActiveProfile();
      if (activeProfile) {
        const std::vector<std::string> &profileNames =
            shortcutManager.getProfileNames();

        if (ImGui::BeginCombo("Active Profile",
                              shortcutManager.getActiveProfileName().c_str())) {
          for (const auto &name : profileNames) {
            bool isSelected = (shortcutManager.getActiveProfileName() == name);
            if (ImGui::Selectable(name.c_str(), isSelected)) {
              shortcutManager.setActiveProfile(name);
              settingsChanged = true;
            }
          }
          ImGui::EndCombo();
        }

        // Profile management buttons
        ImGui::SameLine();
        if (ImGui::Button("New")) {
          isCreatingProfile = true;
          strcpy_s(profileNameBuffer, "New Profile");
        }
        ImGui::SameLine();
        if (ImGui::Button("Rename")) {
          isRenamingProfile = true;
          profileToRename = shortcutManager.getActiveProfileName();
          strcpy_s(profileNameBuffer, profileToRename.c_str());
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete") && profileNames.size() > 1) {
          shortcutManager.deleteProfile(shortcutManager.getActiveProfileName());
          settingsChanged = true;
        }
        if (profileNames.size() <= 1 && ImGui::IsItemHovered()) {
          ImGui::BeginTooltip();
          ImGui::Text("Cannot delete the last profile");
          ImGui::EndTooltip();
        }

        // Import/Export buttons
        ImGui::SameLine();
        if (ImGui::Button("Import")) {
          auto result = pfd::open_file("Import Shortcut Profile", ".",
                                       {"JSON Files", "*.json"})
                            .result();
          if (!result.empty()) {
            if (shortcutManager.importProfileFromFile(result[0])) {
              std::cout << "Profile imported successfully" << std::endl;
              settingsChanged = true;
            } else {
              std::cout << "Failed to import profile" << std::endl;
            }
          }
        }
        ImGui::SameLine();
        if (ImGui::Button("Export")) {
          auto result =
              pfd::save_file("Export Shortcut Profile",
                             shortcutManager.getActiveProfileName() + ".json",
                             {"JSON Files", "*.json"})
                  .result();
          if (!result.empty()) {
            if (shortcutManager.exportProfileToFile(
                    shortcutManager.getActiveProfileName(), result)) {
              std::cout << "Profile exported successfully" << std::endl;
            } else {
              std::cout << "Failed to export profile" << std::endl;
            }
          }
        }

        // Profile creation popup
        if (isCreatingProfile) {
          ImGui::OpenPopup("Create Profile");
          isCreatingProfile = false;
        }
        if (ImGui::BeginPopupModal("Create Profile", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::Text("Enter profile name:");
          ImGui::InputText("##profilename", profileNameBuffer, 256);
          if (ImGui::Button("Create", ImVec2(120, 0))) {
            StereoVista::ShortcutProfile newProfile(profileNameBuffer);
            newProfile.bindings =
                activeProfile->bindings; // Copy current bindings
            shortcutManager.addProfile(newProfile);
            shortcutManager.setActiveProfile(profileNameBuffer);
            settingsChanged = true;
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }

        // Profile rename popup
        if (isRenamingProfile) {
          ImGui::OpenPopup("Rename Profile");
          isRenamingProfile = false;
        }
        if (ImGui::BeginPopupModal("Rename Profile", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
          ImGui::Text("Enter new profile name:");
          ImGui::InputText("##renameprofile", profileNameBuffer, 256);
          if (ImGui::Button("Rename", ImVec2(120, 0))) {
            shortcutManager.renameProfile(profileToRename, profileNameBuffer);
            settingsChanged = true;
            ImGui::CloseCurrentPopup();
          }
          ImGui::SameLine();
          if (ImGui::Button("Cancel", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
          }
          ImGui::EndPopup();
        }

        DrawSectionHeader("Key Bindings");

        // Show conflict message if any
        if (!conflictMessage.empty()) {
          ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.3f, 0.3f, 1.0f));
          ImGui::TextWrapped("%s", conflictMessage.c_str());
          ImGui::PopStyleColor();
        }

        // Shortcut list in a scrollable region
        ImGui::BeginChild("ShortcutList", ImVec2(0, 400), true);

        // Table with columns: Action | Primary Binding | Secondary Binding
        ImGui::Columns(3, "shortcutcolumns");
        ImGui::SetColumnWidth(0, 250);
        ImGui::SetColumnWidth(1, 200);
        ImGui::SetColumnWidth(2, 200);

        ImGui::Text("Action");
        ImGui::NextColumn();
        ImGui::Text("Primary Binding");
        ImGui::NextColumn();
        ImGui::Text("Secondary Binding");
        ImGui::NextColumn();
        ImGui::Separator();
        ImGui::Columns(1); // Reset columns after header

        // Helper lambda to render an action with its bindings
        auto renderAction = [&](StereoVista::ShortcutAction action) {
          int i = static_cast<int>(action);
          std::string actionDesc =
              StereoVista::ShortcutManager::getActionDescription(action);
          const std::vector<StereoVista::KeyBinding> &bindings =
              activeProfile->getBindings(action);

          // Action name
          ImGui::Text("%s", actionDesc.c_str());
          ImGui::NextColumn();

          // Primary binding (slot 0)
          std::string primaryBinding =
              (bindings.size() > 0 && bindings[0].isValid())
                  ? bindings[0].toString()
                  : "Unbound";

          if (waitingForKeyPress && captureActionIndex == i &&
              captureSlot == 0) {
            ImGui::Text("[Press any key...]");
          } else {
            if (ImGui::Button(
                    (primaryBinding + "##primary" + std::to_string(i)).c_str(),
                    ImVec2(180, 0))) {
              captureActionIndex = i;
              captureSlot = 0;
              waitingForKeyPress = true;
              conflictMessage = "";
            }
          }
          ImGui::NextColumn();

          // Secondary binding (slot 1)
          std::string secondaryBinding =
              (bindings.size() > 1 && bindings[1].isValid())
                  ? bindings[1].toString()
                  : "Unbound";

          if (waitingForKeyPress && captureActionIndex == i &&
              captureSlot == 1) {
            ImGui::Text("[Press any key...]");
          } else {
            if (ImGui::Button(
                    (secondaryBinding + "##secondary" + std::to_string(i))
                        .c_str(),
                    ImVec2(180, 0))) {
              captureActionIndex = i;
              captureSlot = 1;
              waitingForKeyPress = true;
              conflictMessage = "";
            }
          }
          ImGui::NextColumn();
        };

        // GROUP: View/Display Controls
        if (ImGui::CollapsingHeader("View/Display Controls",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ToggleGUI);
          renderAction(StereoVista::ShortcutAction::ToggleFPS);
          renderAction(StereoVista::ShortcutAction::ToggleWireframe);
          renderAction(StereoVista::ShortcutAction::ToggleRadar);
          renderAction(StereoVista::ShortcutAction::ToggleZeroPlane);
        }

        // GROUP: Camera Controls
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Camera Controls",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::CenterView);
          renderAction(StereoVista::ShortcutAction::ResetCamera);
          renderAction(StereoVista::ShortcutAction::ToggleZoomToCursor);
          renderAction(StereoVista::ShortcutAction::ToggleOrbitAroundCursor);
        }

        // GROUP: Lighting
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Lighting",
                                    ImGuiTreeNodeFlags_DefaultOpen)) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::CycleLighting);
          renderAction(StereoVista::ShortcutAction::ToggleShadows);
          renderAction(StereoVista::ShortcutAction::ToggleHDR);
          renderAction(StereoVista::ShortcutAction::ToggleBloom);
          renderAction(StereoVista::ShortcutAction::TogglePCSS);
          renderAction(StereoVista::ShortcutAction::ToggleSunLight);
        }

        // GROUP: Materials & Rendering
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Materials & Rendering")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::TogglePBR);
        }

        // GROUP: VCT (Voxel Cone Tracing)
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("VCT (Voxel Cone Tracing)")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ToggleVoxelViz);
          renderAction(StereoVista::ShortcutAction::ToggleVCTIndirectDiffuse);
          renderAction(StereoVista::ShortcutAction::ToggleVCTIndirectSpecular);
          renderAction(StereoVista::ShortcutAction::ToggleVCTDirectLight);
          renderAction(StereoVista::ShortcutAction::ToggleVCTSoftShadows);
        }

        // GROUP: Raytracing
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Raytracing")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ToggleRaytracing);
          renderAction(StereoVista::ShortcutAction::ToggleIndirectLighting);
          renderAction(StereoVista::ShortcutAction::ToggleEmissiveLighting);
          renderAction(StereoVista::ShortcutAction::ToggleBVH);
        }

        // GROUP: 3D Cursor
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("3D Cursor")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ToggleSphereCursor);
          renderAction(StereoVista::ShortcutAction::ToggleCircleCursor);
          renderAction(StereoVista::ShortcutAction::TogglePlaneCursor);
        }

        // GROUP: Window Management
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Window Management")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::OpenSettings);
          renderAction(StereoVista::ShortcutAction::OpenCursorSettings);
          renderAction(StereoVista::ShortcutAction::OpenBrushTool);
        }

        // GROUP: File Operations
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("File Operations")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::ImportModel);
          renderAction(StereoVista::ShortcutAction::ImportPointCloud);
          renderAction(StereoVista::ShortcutAction::SaveScene);
          renderAction(StereoVista::ShortcutAction::LoadScene);
        }

        // GROUP: Object Manipulation
        ImGui::Columns(1);
        if (ImGui::CollapsingHeader("Object Manipulation")) {
          ImGui::Columns(3, "shortcutcolumns");
          ImGui::SetColumnWidth(0, 250);
          ImGui::SetColumnWidth(1, 200);
          ImGui::SetColumnWidth(2, 200);

          renderAction(StereoVista::ShortcutAction::DeleteObject);
        }

        ImGui::Columns(1);
        ImGui::EndChild();

        // Key capture logic
        if (waitingForKeyPress && captureActionIndex != -1) {
          ImGuiIO &io = ImGui::GetIO();

          // Check for key presses
          for (int key = GLFW_KEY_SPACE; key <= GLFW_KEY_LAST; ++key) {
            if (ImGui::IsKeyPressed((ImGuiKey)key, false)) {
              // Get modifiers
              bool ctrl = io.KeyCtrl;
              bool alt = io.KeyAlt;
              bool shift = io.KeyShift;

              // Don't capture modifier keys alone
              if (key == GLFW_KEY_LEFT_CONTROL ||
                  key == GLFW_KEY_RIGHT_CONTROL || key == GLFW_KEY_LEFT_ALT ||
                  key == GLFW_KEY_RIGHT_ALT || key == GLFW_KEY_LEFT_SHIFT ||
                  key == GLFW_KEY_RIGHT_SHIFT) {
                continue;
              }

              StereoVista::KeyBinding newBinding(key, ctrl, alt, shift);
              StereoVista::ShortcutAction targetAction =
                  static_cast<StereoVista::ShortcutAction>(captureActionIndex);

              // Check for conflicts
              auto conflict =
                  activeProfile->findConflict(newBinding, targetAction);
              if (conflict.has_value()) {
                conflictMessage =
                    "Conflict! This key is already bound to: " +
                    StereoVista::ShortcutManager::getActionDescription(
                        conflict.value());
              } else {
                // Set the binding
                activeProfile->setBinding(targetAction, newBinding,
                                          captureSlot);
                settingsChanged = true;
                conflictMessage = "";
              }

              waitingForKeyPress = false;
              captureActionIndex = -1;
              captureSlot = -1;
              break;
            }
          }

          // Allow ESC to cancel capture
          if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
            waitingForKeyPress = false;
            captureActionIndex = -1;
            captureSlot = -1;
            conflictMessage = "";
          }

          // Allow right-click to clear binding
          if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
            StereoVista::ShortcutAction targetAction =
                static_cast<StereoVista::ShortcutAction>(captureActionIndex);
            activeProfile->clearBinding(targetAction, captureSlot);
            settingsChanged = true;
            waitingForKeyPress = false;
            captureActionIndex = -1;
            captureSlot = -1;
            conflictMessage = "";
          }
        }

        ImGui::Spacing();
        ImGui::TextWrapped("Click a binding to change it. Press ESC to cancel. "
                           "Right-click to clear a binding.");
        ImGui::Spacing();

        // Reset to defaults button
        if (ImGui::Button("Reset All to Defaults", ImVec2(-1, 0))) {
          shortcutManager.resetActiveProfileToDefaults();
          settingsChanged = true;
        }
      }

      DrawSectionHeader("Built-in Controls");

      if (ImGui::CollapsingHeader("Mouse & Keyboard Reference")) {
        ImGui::Columns(2, "keybinds");
        ImGui::SetColumnWidth(0, 150);

        ImGui::Text("Camera Controls");
        ImGui::Separator();

        ImGui::Text("W/S");
        ImGui::NextColumn();
        ImGui::Text("Move forward/backward");
        ImGui::NextColumn();

        ImGui::Text("A/D");
        ImGui::NextColumn();
        ImGui::Text("Move left/right");
        ImGui::NextColumn();

        ImGui::Text("Space/Shift");
        ImGui::NextColumn();
        ImGui::Text("Move up/down");
        ImGui::NextColumn();

        ImGui::Text("Left Mouse + Drag");
        ImGui::NextColumn();
        ImGui::Text("Orbit around the viewport center at cursor depth");
        ImGui::NextColumn();

        ImGui::Text("Right Mouse + Drag");
        ImGui::NextColumn();
        ImGui::Text("Rotate the camera");
        ImGui::NextColumn();

        ImGui::Text("Middle Mouse + Drag");
        ImGui::NextColumn();
        ImGui::Text("Pan camera");
        ImGui::NextColumn();

        ImGui::Text("Mouse Wheel");
        ImGui::NextColumn();
        ImGui::Text("Zoom in/out");
        ImGui::NextColumn();

        ImGui::Text("Double Click");
        ImGui::NextColumn();
        ImGui::Text("Center on cursor");
        ImGui::NextColumn();

        ImGui::Spacing();
        ImGui::NextColumn();
        ImGui::Spacing();
        ImGui::NextColumn();

        ImGui::Text("Other Controls");
        ImGui::Separator();

        ImGui::Text("Ctrl + Click");
        ImGui::NextColumn();
        ImGui::Text("Select object");
        ImGui::NextColumn();

        ImGui::Text("Ctrl + Click + Drag");
        ImGui::NextColumn();
        ImGui::Text("Move selected object");
        ImGui::NextColumn();

        ImGui::Text("Esc");
        ImGui::NextColumn();
        ImGui::Text("Exit application");
        ImGui::NextColumn();

        ImGui::Columns(1);
      }

      ImGui::PopID();
  }

  ImGui::EndChild(); // ##SettingsContent

  if (settingsChanged) {
    savePreferences();
    // Save shortcuts when settings change
    shortcutManager.saveToFile("shortcuts.json");
  }

  ImGui::End();
}

void renderCursorSettingsWindow() {
  // Push the synced 3DConnexion settings (including pivot visibility derived
  // from the orbit-center preferences) to the 3DxWare driver XML.
  auto syncTdxPivotVisibility = [&]() {
    if (!tdxSync.IsConnected())
      return;
    ThreeDConnexionSync::TdxSettings ws;
    ws.motionModel = preferences.tdxSettings.motionModel;
    ws.autoPivot = preferences.tdxSettings.autoPivot;
    ws.lockHorizon = preferences.tdxSettings.lockHorizon;
    ws.suspendInput = preferences.tdxSettings.suspendInput;
    ws.lockTo3dViews = preferences.tdxSettings.lockTo3dViews;
    ws.moveObjects = preferences.tdxSettings.moveObjects;
    ws.autokeyAnimation = preferences.tdxSettings.autokeyAnimation;
    ws.selectionFollower = preferences.tdxSettings.selectionFollower;
    ws.firstPersonEaseOut = preferences.tdxSettings.firstPersonEaseOut;
    ws.floorQueryRate = preferences.tdxSettings.floorQueryRate;
    ws.lockSketchPlane = preferences.tdxSettings.lockSketchPlane;
    ws.pivotVisibility =
        preferences.showOrbitCenter
            ? (preferences.alwaysShowOrbitCenter ? "ShowPivot"
                                                 : "ShowMovingPivot")
            : "HidePivot";
    tdxSync.WriteSettings(ws);
  };

  auto *sphereCursor = cursorManager.getSphereCursor();
  auto *fragmentCursor = cursorManager.getFragmentCursor();
  auto *planeCursor = cursorManager.getPlaneCursor();

  ImGui::SetNextWindowSize(ImVec2(540, 680), ImGuiCond_FirstUseEver);
  ImGui::Begin("Cursor Settings", &showCursorSettingsWindow);

  // Preset Management Section
  DrawSectionHeader("Cursor Presets");

  if (ImGui::BeginCombo("Current Preset", currentPresetName.c_str())) {
    if (ImGui::Selectable("Create New...")) {
      currentPresetName = "New Preset";
      isEditingPresetName = true;
      strcpy_s(editPresetNameBuffer, currentPresetName.c_str());
    }

    std::vector<std::string> presetNames =
        Engine::CursorPresetManager::getPresetNames();
    for (const auto &name : presetNames) {
      bool isSelected = (currentPresetName == name);
      if (ImGui::Selectable(name.c_str(), isSelected)) {
        currentPresetName = name;
        try {
          Engine::CursorPreset loadedPreset =
              Engine::CursorPresetManager::applyCursorPreset(name);

          sphereCursor->setVisible(loadedPreset.showSphereCursor);
          sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(
              loadedPreset.sphereScalingMode));
          sphereCursor->setFixedRadius(loadedPreset.sphereFixedRadius);
          sphereCursor->setTransparency(loadedPreset.sphereTransparency);
          sphereCursor->setShowInnerSphere(loadedPreset.showInnerSphere);
          sphereCursor->setColor(loadedPreset.cursorColor);
          sphereCursor->setInnerSphereColor(loadedPreset.innerSphereColor);
          sphereCursor->setInnerSphereFactor(loadedPreset.innerSphereFactor);
          sphereCursor->setEdgeSoftness(loadedPreset.cursorEdgeSoftness);
          sphereCursor->setCenterTransparency(
              loadedPreset.cursorCenterTransparency);

          fragmentCursor->setVisible(loadedPreset.showFragmentCursor);
          fragmentCursor->setBaseInnerRadius(
              loadedPreset.fragmentBaseInnerRadius);

          planeCursor->setVisible(loadedPreset.showPlaneCursor);
          planeCursor->setDiameter(loadedPreset.planeDiameter);
          planeCursor->setColor(loadedPreset.planeColor);

          preferences.currentPresetName = currentPresetName;
          savePreferences();
        } catch (const std::exception &e) {
          std::cerr << "Error loading preset: " << e.what() << std::endl;
        }
      }
    }
    ImGui::EndCombo();
  }

  if (isEditingPresetName) {
    ImGui::InputText("Name", editPresetNameBuffer,
                     IM_ARRAYSIZE(editPresetNameBuffer));

    if (ImGui::Button("Save", ImVec2(100, 0))) {
      std::string newName = editPresetNameBuffer;
      if (!newName.empty()) {
        Engine::CursorPreset newPreset;
        newPreset.name = newName;
        newPreset.showSphereCursor = sphereCursor->isVisible();
        newPreset.showFragmentCursor = fragmentCursor->isVisible();
        newPreset.fragmentBaseInnerRadius =
            fragmentCursor->getBaseInnerRadius();
        newPreset.sphereScalingMode =
            static_cast<int>(sphereCursor->getScalingMode());
        newPreset.sphereFixedRadius = sphereCursor->getFixedRadius();
        newPreset.sphereTransparency = sphereCursor->getTransparency();
        newPreset.showInnerSphere = sphereCursor->getShowInnerSphere();
        newPreset.cursorColor = sphereCursor->getColor();
        newPreset.innerSphereColor = sphereCursor->getInnerSphereColor();
        newPreset.innerSphereFactor = sphereCursor->getInnerSphereFactor();
        newPreset.cursorEdgeSoftness = sphereCursor->getEdgeSoftness();
        newPreset.cursorCenterTransparency =
            sphereCursor->getCenterTransparency();
        newPreset.showPlaneCursor = planeCursor->isVisible();
        newPreset.planeDiameter = planeCursor->getDiameter();
        newPreset.planeColor = planeCursor->getColor();

        Engine::CursorPresetManager::savePreset(newName, newPreset);
        currentPresetName = newName;
        isEditingPresetName = false;
      }
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(100, 0))) {
      isEditingPresetName = false;
    }
  } else {
    if (ImGui::Button("Update", ImVec2(100, 0))) {
      Engine::CursorPreset updatedPreset;
      updatedPreset.name = currentPresetName;
      updatedPreset.showSphereCursor = sphereCursor->isVisible();
      updatedPreset.showFragmentCursor = fragmentCursor->isVisible();
      updatedPreset.fragmentBaseInnerRadius =
          fragmentCursor->getBaseInnerRadius();
      updatedPreset.sphereScalingMode =
          static_cast<int>(sphereCursor->getScalingMode());
      updatedPreset.sphereFixedRadius = sphereCursor->getFixedRadius();
      updatedPreset.sphereTransparency = sphereCursor->getTransparency();
      updatedPreset.showInnerSphere = sphereCursor->getShowInnerSphere();
      updatedPreset.cursorColor = sphereCursor->getColor();
      updatedPreset.innerSphereColor = sphereCursor->getInnerSphereColor();
      updatedPreset.innerSphereFactor = sphereCursor->getInnerSphereFactor();
      updatedPreset.cursorEdgeSoftness = sphereCursor->getEdgeSoftness();
      updatedPreset.cursorCenterTransparency =
          sphereCursor->getCenterTransparency();
      updatedPreset.showPlaneCursor = planeCursor->isVisible();
      updatedPreset.planeDiameter = planeCursor->getDiameter();
      updatedPreset.planeColor = planeCursor->getColor();

      Engine::CursorPresetManager::savePreset(currentPresetName, updatedPreset);
    }
    ImGui::SameLine();
    if (ImGui::Button("Rename", ImVec2(100, 0))) {
      isEditingPresetName = true;
      strcpy_s(editPresetNameBuffer, currentPresetName.c_str());
    }
    ImGui::SameLine();
    if (ImGui::Button("Delete", ImVec2(100, 0))) {
      if (currentPresetName != "Default") {
        Engine::CursorPresetManager::deletePreset(currentPresetName);
        currentPresetName = "Default";
      }
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // 3D Preview Section
  DrawSectionHeader("3D Preview");

  // Initialize the preview if not already done
  static bool previewInitialized = false;
  if (!previewInitialized) {
    cursorPreview3D.initialize(256, 256);
    previewInitialized = true;
  }

  // Get the currently active cursor based on visibility
  Cursor::BaseCursor *activeCursor = nullptr;
  if (sphereCursor->isVisible()) {
    activeCursor = sphereCursor;
  } else if (fragmentCursor->isVisible()) {
    activeCursor = fragmentCursor;
  } else if (planeCursor->isVisible()) {
    activeCursor = planeCursor;
  }

  if (activeCursor) {
    // Render the independent preview
    cursorPreview3D.render(activeCursor);

    // Display the preview image
    ImGui::Text("Current Cursor on Cube:");
    float previewDim = 200.0f * g_GuiScale.currentScale;
    ImVec2 previewSize(previewDim, previewDim);
    ImGui::Image((void *)(intptr_t)cursorPreview3D.getTextureID(), previewSize);

    // Handle mouse interaction for rotation
    if (ImGui::IsItemHovered() &&
        ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
      ImVec2 mouseDelta = ImGui::GetMouseDragDelta(ImGuiMouseButton_Left);
      cursorPreview3D.updateRotation(mouseDelta.x, mouseDelta.y);
      ImGui::ResetMouseDragDelta(ImGuiMouseButton_Left);
    }

    ImGui::Text("Drag to rotate the preview");
  } else {
    ImGui::Text("No cursor is currently visible.");
    ImGui::Text("Enable a cursor type below to see the preview.");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Unified Scaling Section (applies to all cursor types)
  DrawSectionHeader("Universal Scaling");
  ImGui::Text("These settings apply to all enabled cursor types:");

  // Get scaling mode from any active cursor (they should all be the same)
  Cursor::BaseCursor *referenceCursor = nullptr;
  if (sphereCursor->isVisible())
    referenceCursor = sphereCursor;
  else if (fragmentCursor->isVisible())
    referenceCursor = fragmentCursor;
  else if (planeCursor->isVisible())
    referenceCursor = planeCursor;

  if (referenceCursor) {
    const char *scalingModes[] = {"Normal", "Fixed", "Constrained Dynamic",
                                  "Logarithmic"};
    int currentMode = static_cast<int>(referenceCursor->getScalingMode());
    if (ImGui::Combo("Scaling Mode", &currentMode, scalingModes,
                     IM_ARRAYSIZE(scalingModes))) {
      // Apply to all cursor types
      GUI::CursorScalingMode mode =
          static_cast<GUI::CursorScalingMode>(currentMode);
      sphereCursor->setScalingMode(mode);
      fragmentCursor->setScalingMode(mode);
      planeCursor->setScalingMode(mode);
    }

    if (currentMode == static_cast<int>(GUI::CursorScalingMode::CURSOR_FIXED)) {
      float baseSize = referenceCursor->getBaseSize();
      if (ImGui::SliderFloat("Base Size", &baseSize, 0.01f, 3.0f, "%.2f")) {
        // Apply to all cursor types
        sphereCursor->setBaseSize(baseSize);
        fragmentCursor->setBaseSize(baseSize);
        planeCursor->setBaseSize(baseSize);
      }
    } else {
      float minDiff = referenceCursor->getMinDiff();
      if (ImGui::SliderFloat("Min Size Difference", &minDiff, 0.01f, 2.0f,
                             "%.2f")) {
        // Apply to all cursor types
        sphereCursor->setMinDiff(minDiff);
        fragmentCursor->setMinDiff(minDiff);
        planeCursor->setMinDiff(minDiff);
      }

      float maxDiff = referenceCursor->getMaxDiff();
      if (ImGui::SliderFloat("Max Size Difference", &maxDiff, 0.02f, 5.0f,
                             "%.2f")) {
        // Apply to all cursor types
        sphereCursor->setMaxDiff(maxDiff);
        fragmentCursor->setMaxDiff(maxDiff);
        planeCursor->setMaxDiff(maxDiff);
      }
    }
  } else {
    ImGui::Text("Enable a cursor type to configure scaling settings.");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  // Cursor Type Tabs
  if (ImGui::BeginTabBar("CursorTypes")) {

    // 3D Sphere Tab
    if (ImGui::BeginTabItem("3D Sphere")) {
      ImGui::PushID("3DSphereTab");
      bool sphereVisible = sphereCursor->isVisible();
      if (ImGui::Checkbox("Enable 3D Sphere", &sphereVisible)) {
        sphereCursor->setVisible(sphereVisible);
      }

      if (sphereVisible) {
        DrawSectionHeader("3D Sphere Settings");

        glm::vec4 cursorColor = sphereCursor->getColor();
        if (ImGui::ColorEdit3("Color", glm::value_ptr(cursorColor))) {
          sphereCursor->setColor(cursorColor);
        }

        float transparency = sphereCursor->getTransparency();
        if (ImGui::SliderFloat("Opacity", &transparency, 0.0f, 1.0f, "%.2f")) {
          sphereCursor->setTransparency(transparency);
        }

        float edgeSoftness = sphereCursor->getEdgeSoftness();
        if (ImGui::SliderFloat("Edge Softness", &edgeSoftness, 0.0f, 1.0f,
                               "%.2f")) {
          sphereCursor->setEdgeSoftness(edgeSoftness);
        }

        float centerTransparency = sphereCursor->getCenterTransparency();
        if (ImGui::SliderFloat("Center Fade", &centerTransparency, 0.0f, 1.0f,
                               "%.2f")) {
          sphereCursor->setCenterTransparency(centerTransparency);
        }

        DrawSectionHeader("Inner Sphere");

        bool showInnerSphere = sphereCursor->getShowInnerSphere();
        if (ImGui::Checkbox("Show Inner Core", &showInnerSphere)) {
          sphereCursor->setShowInnerSphere(showInnerSphere);
        }

        if (showInnerSphere) {
          glm::vec4 innerColor = sphereCursor->getInnerSphereColor();
          if (ImGui::ColorEdit3("Inner Color", glm::value_ptr(innerColor))) {
            sphereCursor->setInnerSphereColor(innerColor);
          }

          float innerFactor = sphereCursor->getInnerSphereFactor();
          if (ImGui::SliderFloat("Inner Size", &innerFactor, 0.1f, 0.9f,
                                 "%.2f")) {
            sphereCursor->setInnerSphereFactor(innerFactor);
          }
        }
      }
      ImGui::PopID();
      ImGui::EndTabItem();
    }

    // 2D Circle Tab
    if (ImGui::BeginTabItem("2D Circle")) {
      ImGui::PushID("2DCircleTab");
      bool fragmentVisible = fragmentCursor->isVisible();
      if (ImGui::Checkbox("Enable 2D Circle", &fragmentVisible)) {
        fragmentCursor->setVisible(fragmentVisible);
      }

      if (fragmentVisible) {
        DrawSectionHeader("Ring Dimensions");

        float outerRadius = fragmentCursor->getBaseOuterRadius();
        if (ImGui::SliderFloat("Outer Radius", &outerRadius, 0.0f, 0.3f,
                               "%.3f")) {
          fragmentCursor->setBaseOuterRadius(outerRadius);
        }

        float outerBorder = fragmentCursor->getBaseOuterBorderThickness();
        if (ImGui::SliderFloat("Outer Thickness", &outerBorder, 0.0f, 0.08f,
                               "%.3f")) {
          fragmentCursor->setBaseOuterBorderThickness(outerBorder);
        }

        float innerRadius = fragmentCursor->getBaseInnerRadius();
        if (ImGui::SliderFloat("Inner Radius", &innerRadius, 0.0f, 0.2f,
                               "%.3f")) {
          fragmentCursor->setBaseInnerRadius(innerRadius);
        }

        float innerBorder = fragmentCursor->getBaseInnerBorderThickness();
        if (ImGui::SliderFloat("Inner Thickness", &innerBorder, 0.0f, 0.08f,
                               "%.3f")) {
          fragmentCursor->setBaseInnerBorderThickness(innerBorder);
        }

        DrawSectionHeader("Colors");

        glm::vec4 outerColor = fragmentCursor->getOuterColor();
        if (ImGui::ColorEdit4("Outer Ring", glm::value_ptr(outerColor))) {
          fragmentCursor->setOuterColor(outerColor);
        }

        glm::vec4 innerColor = fragmentCursor->getInnerColor();
        if (ImGui::ColorEdit4("Inner Ring", glm::value_ptr(innerColor))) {
          fragmentCursor->setInnerColor(innerColor);
        }
      }
      ImGui::PopID();
      ImGui::EndTabItem();
    }

    // Surface Plane Tab
    if (ImGui::BeginTabItem("Surface Plane")) {
      ImGui::PushID("SurfacePlaneTab");
      bool planeVisible = planeCursor->isVisible();
      if (ImGui::Checkbox("Enable Surface Plane", &planeVisible)) {
        planeCursor->setVisible(planeVisible);
      }

      if (planeVisible) {
        DrawSectionHeader("Plane Settings");

        glm::vec4 planeColor = planeCursor->getColor();
        if (ImGui::ColorEdit3("Color", glm::value_ptr(planeColor))) {
          planeCursor->setColor(planeColor);
        }

        float planeDiameter = planeCursor->getDiameter();
        if (ImGui::SliderFloat("Size", &planeDiameter, 0.1f, 5.0f, "%.1f")) {
          planeCursor->setDiameter(planeDiameter);
        }

        ImGui::TextWrapped("The plane cursor aligns to surface normals and "
                           "shows the tangent plane at the cursor position.");
      }
      ImGui::PopID();
      ImGui::EndTabItem();
    }

    // Orbit Visualization Tab
    if (ImGui::BeginTabItem("Orbit Center")) {
      ImGui::PushID("OrbitCenterTab");
      DrawSectionHeader("Orbit Visualization");

      bool showOrbitCenter = cursorManager.isShowOrbitCenter();
      if (ImGui::Checkbox("Show Orbit Center", &showOrbitCenter)) {
        cursorManager.setShowOrbitCenter(showOrbitCenter);
        preferences.showOrbitCenter = showOrbitCenter;
        savePreferences();
        syncTdxPivotVisibility();
      }
      ImGui::SameLine();
      DrawHelpMarker("Display a marker at the camera's orbit pivot point");

      if (showOrbitCenter) {
        bool alwaysShowOrbitCenter = cursorManager.isAlwaysShowOrbitCenter();
        if (ImGui::Checkbox("Always Show Orbit Center",
                            &alwaysShowOrbitCenter)) {
          cursorManager.setAlwaysShowOrbitCenter(alwaysShowOrbitCenter);
          preferences.alwaysShowOrbitCenter = alwaysShowOrbitCenter;
          savePreferences();
          syncTdxPivotVisibility();
        }
        ImGui::SameLine();
        DrawHelpMarker("Keep the orbit center visible even when not orbiting");
      }

      if (showOrbitCenter) {
        glm::vec4 orbitColor = cursorManager.getOrbitCenterColor();
        if (ImGui::ColorEdit3("Marker Color", glm::value_ptr(orbitColor))) {
          cursorManager.setOrbitCenterColor(orbitColor);
          preferences.orbitCenterColor = orbitColor;
          savePreferences();
        }

        float orbitSize = cursorManager.getOrbitCenterSphereRadius();
        if (ImGui::SliderFloat("Marker Size", &orbitSize, 0.01f, 1.0f,
                               "%.2f")) {
          cursorManager.setOrbitCenterSphereRadius(orbitSize);
          preferences.orbitCenterSphereRadius = orbitSize;
          savePreferences();
        }
      }

      DrawSectionHeader("Orbit Behavior");

      ImGui::Text("Camera orbit mode affects where the orbit center appears:");
      ImGui::Spacing();

      ImGui::TextWrapped("• Standard: Center of viewport at cursor depth");
      ImGui::TextWrapped("• Around Cursor: At the 3D cursor position");
      ImGui::TextWrapped("• Follow Cursor: View centers on cursor first");

      ImGui::PopID();
      ImGui::EndTabItem();
    }

    ImGui::EndTabBar();
  }

  ImGui::End();
}

void renderBrushToolWindow() {
  extern Tools::BrushTool brushTool;

  ImGui::SetNextWindowSize(ImVec2(450, 650), ImGuiCond_FirstUseEver);
  ImGui::Begin("Brush Tool", &showBrushToolWindow);

  DrawSectionHeader("Brush Tool");

  // Enable/Disable brush tool (consumed by the paint handler in main.cpp)
  DrawToggleSwitch("Enable Brush Tool", &preferences.brushToolSettings.enabled);

  if (!preferences.brushToolSettings.enabled) {
    ImGui::Spacing();
    ImGui::TextDisabled("Enable the brush tool to start painting.");
    ImGui::TextWrapped("Painting places instances of a model onto surfaces. "
                       "Create a cluster, then hold the left mouse button and "
                       "drag over geometry in the viewport.");
    ImGui::End();
    return;
  }

  ImGui::Spacing();

  // Cluster Management
  DrawSectionHeader("Brush Clusters");

  // Create new cluster section
  static char newClusterName[128] = "New Cluster";
  ImGui::InputText("Cluster Name", newClusterName,
                   IM_ARRAYSIZE(newClusterName));

  static std::vector<std::string> modelNames;
  modelNames.clear();
  modelNames.push_back("None");
  for (size_t i = 0; i < currentScene.models.size(); i++) {
    modelNames.push_back(currentScene.models[i].name);
  }

  int newClusterModelIndex =
      preferences.brushToolSettings.selectedModelIndex + 1;
  ImGui::Combo(
      "Model", &newClusterModelIndex,
      [](void *data, int idx, const char **out_text) {
        std::vector<std::string> *names =
            static_cast<std::vector<std::string> *>(data);
        *out_text = (*names)[idx].c_str();
        return true;
      },
      &modelNames, modelNames.size());
  preferences.brushToolSettings.selectedModelIndex = newClusterModelIndex - 1;

  if (ImGui::Button("Create New Cluster", ImVec2(-1, 0))) {
    if (preferences.brushToolSettings.selectedModelIndex >= 0) {
      brushTool.createCluster(std::string(newClusterName),
                              preferences.brushToolSettings.selectedModelIndex);
      strcpy_s(newClusterName, "New Cluster");
    }
  }

  ImGui::Spacing();

  // List of existing clusters
  DrawSectionHeader("Existing Clusters");

  int activeCluster = brushTool.getActiveCluster();
  int clusterCount = brushTool.getClusterCount();

  if (clusterCount == 0) {
    ImGui::TextDisabled("No clusters created yet");
  } else {
    for (int i = 0; i < clusterCount; i++) {
      const Tools::BrushCluster *cluster = brushTool.getCluster(i);
      if (!cluster)
        continue;

      ImGui::PushID(i);

      bool isActive = (i == activeCluster);
      if (ImGui::Selectable(cluster->name.c_str(), isActive)) {
        brushTool.setActiveCluster(i);
      }

      // Show instance count
      ImGui::SameLine();
      ImGui::TextDisabled("(%d instances)", cluster->instances.size());

      ImGui::PopID();
    }
  }

  ImGui::Spacing();

  // Active cluster settings
  if (activeCluster >= 0 && activeCluster < clusterCount) {
    Tools::BrushCluster *cluster = brushTool.getCluster(activeCluster);
    if (cluster) {
      DrawSectionHeader("Active Cluster Settings");

      ImGui::Text("Cluster: %s", cluster->name.c_str());
      if (cluster->sourceModelIndex >= 0 &&
          cluster->sourceModelIndex < currentScene.models.size()) {
        ImGui::Text(
            "Model: %s",
            currentScene.models[cluster->sourceModelIndex].name.c_str());
      }
      ImGui::Text("Instances: %d",
                  brushTool.getInstanceCountForCluster(activeCluster));

      ImGui::Spacing();

      // Instance Variation
      DrawSectionHeader("Instance Variation");

      ImGui::SliderFloat("Min Scale", &cluster->minScale, 0.1f, 5.0f, "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Minimum scale multiplier (1.0 = original size)");

      ImGui::SliderFloat("Max Scale", &cluster->maxScale, 0.1f, 5.0f, "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Maximum scale multiplier (1.0 = original size)");

      if (cluster->maxScale < cluster->minScale) {
        cluster->maxScale = cluster->minScale;
      }

      ImGui::SliderFloat("Rotation", &cluster->rotationRandomization, 0.0f,
                         1.0f, "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Random rotation amount (0 = no rotation, 1 = full 360°)");

      ImGui::SliderFloat("Color Variation", &cluster->colorVariation, 0.0f,
                         1.0f, "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Random color variation for instances");

      ImGui::Checkbox("Align to Surface", &cluster->alignToNormal);
      ImGui::SameLine();
      DrawHelpMarker("Align painted instances to surface normal");

      ImGui::Spacing();

      // Cluster Actions
      DrawSectionHeader("Cluster Actions");

      if (ImGui::Button("Clear Instances", ImVec2(-1, 0))) {
        brushTool.clearInstancesForCluster(activeCluster);
      }

      if (ImGui::Button("Delete Cluster", ImVec2(-1, 0))) {
        brushTool.deleteCluster(activeCluster);
      }
    }
  }

  ImGui::Spacing();

  // Global Brush Settings
  DrawSectionHeader("Global Brush Settings");

  ImGui::SliderFloat("Brush Radius", &preferences.brushToolSettings.brushRadius,
                     0.1f, 5.0f, "%.2f");
  ImGui::SameLine();
  DrawHelpMarker("Size of the brush area");

  ImGui::SliderFloat("Min Spacing", &preferences.brushToolSettings.minSpacing,
                     0.0f, 2.0f, "%.2f");
  ImGui::SameLine();
  DrawHelpMarker("Minimum distance between painted instances");

  ImGui::SliderFloat("Density", &preferences.brushToolSettings.density, 0.1f,
                     5.0f, "%.2f");
  ImGui::SameLine();
  DrawHelpMarker("Number of instances per paint stroke");

  ImGui::Checkbox("Show Brush Cursor",
                  &preferences.brushToolSettings.showBrushCursor);

  ImGui::Spacing();

  // Global Statistics
  DrawSectionHeader("Statistics");
  ImGui::Text("Total Clusters: %d", clusterCount);
  ImGui::Text("Total Instances: %d", brushTool.getTotalInstanceCount());

  if (ImGui::Button("Clear All Clusters", ImVec2(-1, 0))) {
    brushTool.clearAllInstances();
  }

  ImGui::Spacing();

  // Usage instructions
  DrawSectionHeader("Usage");
  ImGui::TextWrapped("1. Create a new cluster with a model");
  ImGui::TextWrapped("2. Select cluster from the list to edit");
  ImGui::TextWrapped("3. Adjust cluster settings");
  ImGui::TextWrapped(
      "4. Hold LEFT MOUSE BUTTON and drag over surfaces to paint");

  ImGui::End();
}

void renderSunManipulationPanel() {
  DrawSectionHeader("Sun Light");

  ImGui::Checkbox("Enabled", &sun.enabled);
  ImGui::ColorEdit3("Color", glm::value_ptr(sun.color));
  ImGui::SliderFloat("Intensity", &sun.intensity, 0.0f, 10.0f, "%.1f");

  ImGui::Spacing();

  static glm::vec3 angles = glm::vec3(-45.0f, -45.0f, 0.0f);
  if (ImGui::DragFloat3("Direction (Angles)", glm::value_ptr(angles), 1.0f,
                        -180.0f, 180.0f, "%.0f°")) {
    glm::mat4 rotationMatrix = glm::mat4(1.0f);
    rotationMatrix =
        glm::rotate(rotationMatrix, glm::radians(angles.x), glm::vec3(1, 0, 0));
    rotationMatrix =
        glm::rotate(rotationMatrix, glm::radians(angles.y), glm::vec3(0, 1, 0));
    rotationMatrix =
        glm::rotate(rotationMatrix, glm::radians(angles.z), glm::vec3(0, 0, 1));
    sun.direction =
        glm::normalize(glm::vec3(rotationMatrix * glm::vec4(0, -1, 0, 0)));
  }

  ImGui::Text("Direction Vector: (%.2f, %.2f, %.2f)", sun.direction.x,
              sun.direction.y, sun.direction.z);
}

void renderModelManipulationPanel(Engine::Model &model,
                                  Engine::Shader *shader) {
  if (g_Fonts.icons) {
    ImGui::PushFont(g_Fonts.icons);
    ImGui::Text(ICON_FA_CUBE);
    ImGui::PopFont();
    ImGui::SameLine();
  }
  ImGui::Text("%s", model.name.c_str());
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool transformChanged = false;
    transformChanged |=
        ImGui::DragFloat3("Position", glm::value_ptr(model.position), 0.1f);
    transformChanged |=
        ImGui::DragFloat3("Rotation", glm::value_ptr(model.rotation), 1.0f,
                          -360.0f, 360.0f, "%.0f°");
    transformChanged |= ImGui::DragFloat3("Scale", glm::value_ptr(model.scale),
                                          0.01f, 0.01f, 100.0f);

    if (transformChanged) {
      updateSpaceMouseBounds();
    }
  }

  if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
    if (ImGui::ColorEdit3("Base Color", glm::value_ptr(model.color))) {
      // Auto-update F0 when color changes (if PBR is enabled)
      if (preferences.materialSettings.enablePBR) {
        model.F0 =
            glm::mix(glm::vec3(0.04f), model.color, model.metallicFactor);
      }
    }

    // PBR Material Properties
    if (preferences.materialSettings.enablePBR) {
      ImGui::Spacing();
      DrawSectionHeader("PBR Properties");

      if (ImGui::SliderFloat("Metallic", &model.metallicFactor, 0.0f, 1.0f,
                             "%.3f")) {
        // Auto-update F0 based on metallic workflow
        model.F0 =
            glm::mix(glm::vec3(0.04f), model.color, model.metallicFactor);
      }
      ImGui::SameLine();
      DrawHelpMarker(
          "0.0 = Dielectric (plastic, wood)\n1.0 = Metal (iron, gold)");

      ImGui::SliderFloat("Roughness", &model.roughnessFactor, 0.0f, 1.0f,
                         "%.3f");
      ImGui::SameLine();
      DrawHelpMarker("0.0 = Mirror-like\n1.0 = Completely rough");

      ImGui::ColorEdit3("F0 (Base Reflectance)", glm::value_ptr(model.F0));
      ImGui::SameLine();
      DrawHelpMarker("Fresnel reflectance at normal incidence\nUsually 0.04 "
                     "for dielectrics, metal color for metals");

      ImGui::SliderFloat("Normal Intensity", &model.normalScale, 0.0f, 2.0f,
                         "%.2f");
      ImGui::SameLine();
      DrawHelpMarker("Intensity of normal mapping effect");

      ImGui::SliderFloat("Height Scale", &model.heightScale, 0.0f, 0.1f,
                         "%.4f");
      ImGui::SameLine();
      DrawHelpMarker("Scale for parallax mapping height");

    } else {
      // Traditional material properties
      ImGui::SliderFloat("Shininess", &model.shininess, 1.0f, 90.0f);
    }

    ImGui::SliderFloat("Emissive", &model.emissive, 0.0f, 1.0f);

    if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
      ImGui::Spacing();
      DrawSectionHeader("VCT Properties");

      static const char *material_types[] = {
          "Concrete", "Metal", "Plastic", "Glass", "Wood", "Marble", "Custom"};
      int current_type = static_cast<int>(model.materialType);

      if (ImGui::Combo("Preset", &current_type, material_types,
                       IM_ARRAYSIZE(material_types))) {
        model.applyMaterialPreset(static_cast<MaterialType>(current_type));
      }

      ImGui::SliderFloat("Diffuse", &model.diffuseReflectivity, 0.0f, 1.0f);
      ImGui::ColorEdit3("Specular", glm::value_ptr(model.specularColor));
      ImGui::SliderFloat("Reflectivity", &model.specularReflectivity, 0.0f,
                         1.0f);
      ImGui::SliderFloat("Glossiness", &model.specularDiffusion, 0.0f, 1.0f);
      ImGui::SliderFloat("IOR", &model.refractiveIndex, 1.0f, 3.0f);
      ImGui::SameLine();
      DrawHelpMarker("Index of Refraction:\n1.0 = Air\n1.33 = Water\n1.5 = "
                     "Glass\n2.4 = Diamond");
      ImGui::SliderFloat("Transparency", &model.transparency, 0.0f, 1.0f);
    }
  }

  if (ImGui::CollapsingHeader("Textures")) {
    if (!model.getMeshes().empty()) {
      const auto &mesh = model.getMeshes()[0];
      if (!mesh.textures.empty()) {
        ImGui::Text("Loaded:");
        for (const auto &texture : mesh.textures) {
          ImGui::BulletText("%s", texture.type.c_str());
        }
      } else {
        ImGui::TextDisabled("No textures loaded");
      }
    }

    ImGui::Spacing();

    auto textureLoadButton = [&](const char *label, const char *type) {
      if (ImGui::Button(label, ImVec2(-1, 0))) {
        auto selection =
            pfd::open_file("Select texture", ".",
                           {"Images", "*.png *.jpg *.jpeg *.bmp", "All", "*"})
                .result();
        if (!selection.empty()) {
          Texture texture;
          texture.fullPath = selection[0];
          texture.id = model.TextureFromFile(selection[0].c_str(), selection[0],
                                             texture.fullPath);
          texture.type = type;
          texture.path = selection[0];
          for (auto &mesh : model.getMeshes()) {
            mesh.textures.push_back(texture);
          }
        }
      }
    };

    textureLoadButton("Load Diffuse", "texture_diffuse");
    textureLoadButton("Load Normal", "texture_normal");
    textureLoadButton("Load Specular", "texture_specular");

    if (preferences.materialSettings.enablePBR) {
      ImGui::Spacing();
      ImGui::Text("PBR Textures:");
      textureLoadButton("Load Metallic", "texture_metallic");
      textureLoadButton("Load Roughness", "texture_roughness");
      textureLoadButton("Load AO", "texture_ao");
      textureLoadButton("Load Height", "texture_height");
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Model", ImVec2(-1, 0))) {
    ImGui::OpenPopup("Delete Model?");
  }

  if (ImGui::BeginPopupModal("Delete Model?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete '%s'?", model.name.c_str());
    ImGui::Text("This cannot be undone!");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      deleteSelectedModel();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void renderMeshManipulationPanel(Engine::Model &model, int meshIndex,
                                 Engine::Shader *shader) {
  auto &mesh = model.getMeshes()[meshIndex];

  if (g_Fonts.icons) {
    ImGui::PushFont(g_Fonts.icons);
    ImGui::Text(ICON_FA_CUBE);
    ImGui::PopFont();
    ImGui::SameLine();
  }
  ImGui::Text("%s - Mesh %d", model.name.c_str(), meshIndex + 1);
  ImGui::Separator();

  ImGui::Checkbox("Visible", &mesh.visible);

  if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
    DrawSectionHeader("Basic Properties");
    ImGui::ColorEdit3("Albedo Color", glm::value_ptr(mesh.color));
    ImGui::SliderFloat("Shininess", &mesh.shininess, 1.0f, 90.0f);
    ImGui::SliderFloat("Emissive", &mesh.emissive, 0.0f, 1.0f);

    if (preferences.materialSettings.enablePBR) {
      ImGui::Spacing();
      ImGui::TextDisabled(
          "PBR properties (metallic, roughness, normals) are per-model.\n"
          "Select the parent model to edit them.");
    }

    if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
      ImGui::Spacing();
      ImGui::TextDisabled("VCT properties are per-model only");
    }
  }

  if (ImGui::CollapsingHeader("Textures")) {
    if (!mesh.textures.empty()) {
      ImGui::Text("Loaded:");
      for (const auto &texture : mesh.textures) {
        ImGui::BulletText("%s", texture.type.c_str());
      }
    } else {
      ImGui::TextDisabled("No textures");
    }

    ImGui::Spacing();

    auto textureLoadButton = [&](const char *label, const char *type) {
      if (ImGui::Button(label, ImVec2(-1, 0))) {
        auto selection =
            pfd::open_file("Select texture", ".",
                           {"Images", "*.png *.jpg *.jpeg *.bmp", "All", "*"})
                .result();
        if (!selection.empty()) {
          Texture texture;
          texture.fullPath = selection[0];
          texture.id = model.TextureFromFile(selection[0].c_str(), selection[0],
                                             texture.fullPath);
          texture.type = type;
          texture.path = selection[0];
          mesh.textures.push_back(texture);
        }
      }
    };

    // Basic textures
    DrawSectionHeader("Basic Textures");
    textureLoadButton("Load Diffuse", "texture_diffuse");
    textureLoadButton("Load Specular", "texture_specular");

    // Enhanced textures
    DrawSectionHeader("Enhanced Textures");
    textureLoadButton("Load Normal Map", "texture_normal");
    textureLoadButton("Load Ambient Occlusion", "texture_ao");

    // PBR textures
    if (preferences.materialSettings.enablePBR) {
      DrawSectionHeader("PBR Textures");
      textureLoadButton("Load Metallic Map", "texture_metallic");
      textureLoadButton("Load Roughness Map", "texture_roughness");
    }

    // Advanced textures
    if (preferences.materialSettings.enableParallaxMapping) {
      DrawSectionHeader("Advanced Textures");
      textureLoadButton("Load Height Map", "texture_height");
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Mesh", ImVec2(-1, 0))) {
    if (model.getMeshes().size() > 1) {
      ImGui::OpenPopup("Delete Mesh?");
    } else {
      ImGui::OpenPopup("Cannot Delete");
    }
  }

  if (ImGui::BeginPopupModal("Delete Mesh?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete this mesh?");
    ImGui::Text("This cannot be undone!");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      model.getMeshes().erase(model.getMeshes().begin() + meshIndex);
      currentSelectedMeshIndex = -1;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }

  if (ImGui::BeginPopupModal("Cannot Delete", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Cannot delete the last mesh.");
    ImGui::Text("Delete the entire model instead.");
    if (ImGui::Button("OK", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void renderPointCloudManipulationPanel(Engine::PointCloud &pointCloud) {
  if (g_Fonts.icons) {
    ImGui::PushFont(g_Fonts.icons);
    ImGui::Text(ICON_FA_CLOUD);
    ImGui::PopFont();
    ImGui::SameLine();
  }
  ImGui::Text("%s", pointCloud.name.c_str());

  // isLoaded() returns true when compute SSBOs are ready (numBatches > 0)
  // or when a legacy CPU-side points vector is still populated.
  if (!pointCloud.isLoaded()) {
    ImGui::TextDisabled("Point cloud is empty");
    return;
  }

  ImGui::Separator();

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    bool transformChanged = false;
    transformChanged |= ImGui::DragFloat3(
        "Position", glm::value_ptr(pointCloud.position), 0.1f);
    transformChanged |=
        ImGui::DragFloat3("Rotation", glm::value_ptr(pointCloud.rotation), 1.0f,
                          -360.0f, 360.0f, "%.0f°");
    transformChanged |= ImGui::DragFloat3(
        "Scale", glm::value_ptr(pointCloud.scale), 0.01f, 0.01f, 100.0f);

    if (transformChanged) {
      updateSpaceMouseBounds();
    }
  }

  if (ImGui::CollapsingHeader("Display Settings",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::SliderFloat("Point Size", &pointCloud.basePointSize, 1.0f, 10.0f,
                       "%.1f");
    ImGui::SameLine();
    DrawHelpMarker("Base size of points in the cloud");
  }

  if (ImGui::CollapsingHeader("Info")) {
    ImGui::Text("Points: %s",
                [&]() -> std::string {
                  uint32_t n = pointCloud.totalPointCount;
                  if (n >= 1'000'000)
                    return std::to_string(n / 1'000'000) + "M";
                  if (n >= 1'000)
                    return std::to_string(n / 1'000) + "K";
                  return std::to_string(n);
                }()
                             .c_str());
    ImGui::Text("Batches: %u", pointCloud.numBatches);
    if (pointCloud.hasBounds()) {
      const glm::vec3 sz = pointCloud.boundsMax - pointCloud.boundsMin;
      ImGui::Text("Extent: %.2f x %.2f x %.2f m", sz.x, sz.y, sz.z);
    }
  }

  if (ImGui::CollapsingHeader("Export")) {
    static int exportFormat = 0;
    ImGui::RadioButton("XYZ Format", &exportFormat, 0);
    ImGui::SameLine();
    ImGui::RadioButton("Binary Format", &exportFormat, 1);

    if (ImGui::Button("Export Point Cloud...", ImVec2(-1, 0))) {
      std::string defaultExt = (exportFormat == 0) ? ".xyz" : ".pcb";
      auto destination = pfd::save_file("Export point cloud", ".",
                                        {"Point Cloud Files", "*" + defaultExt,
                                         "All Files", "*"})
                             .result();

      if (!destination.empty()) {
        bool success = false;
        if (exportFormat == 0) {
          success =
              Engine::PointCloudLoader::exportToXYZ(pointCloud, destination);
        } else {
          success =
              Engine::PointCloudLoader::exportToBinary(pointCloud, destination);
        }

        if (success) {
          std::cout << "Point cloud exported successfully to " << destination
                    << std::endl;
        } else {
          std::cerr << "Failed to export point cloud to " << destination
                    << std::endl;
        }
      }
    }
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Point Cloud", ImVec2(-1, 0))) {
    ImGui::OpenPopup("Delete Point Cloud?");
  }

  if (ImGui::BeginPopupModal("Delete Point Cloud?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete '%s'?", pointCloud.name.c_str());
    ImGui::Text("This cannot be undone!");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      deleteSelectedPointCloud();
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void deleteSelectedModel() {
  if (currentSelectedType == SelectedType::Model && currentSelectedIndex >= 0 &&
      currentSelectedIndex < currentScene.models.size()) {
    currentScene.models.erase(currentScene.models.begin() +
                              currentSelectedIndex);
    currentSelectedIndex = -1;
    currentSelectedType = SelectedType::None;
    updateSpaceMouseBounds();

    // Mark voxelizer dirty for re-voxelization
    if (voxelizer) {
      voxelizer->markDirty();
    }
  }
}

void deleteSelectedPointCloud() {
  if (currentSelectedType == SelectedType::PointCloud &&
      currentSelectedIndex >= 0 &&
      currentSelectedIndex < currentScene.pointClouds.size()) {
    glDeleteVertexArrays(1,
                         &currentScene.pointClouds[currentSelectedIndex].vao);
    glDeleteBuffers(1, &currentScene.pointClouds[currentSelectedIndex].vbo);
    currentScene.pointClouds.erase(currentScene.pointClouds.begin() +
                                   currentSelectedIndex);
    currentSelectedIndex = -1;
    currentSelectedType = SelectedType::None;
    updateSpaceMouseBounds();
  }
}

void renderPointLightManipulationPanel() {
  if (currentSelectedIndex >= pointLights.size()) {
    ImGui::Text("Error: Invalid light selection");
    return;
  }

  auto &light = pointLights[currentSelectedIndex];

  // Render icon and text with PushFont/PopFont approach
  if (g_Fonts.icons) {
    ImGui::PushFont(g_Fonts.icons);
    ImGui::Text(ICON_FA_LIGHTBULB);
    ImGui::PopFont();
    ImGui::SameLine();
    ImGui::Text("Point Light %d", currentSelectedIndex + 1);
  } else {
    ImGui::Text("? Point Light %d", currentSelectedIndex + 1);
  }
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.1f);
  }

  if (ImGui::CollapsingHeader("Light Properties",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
    ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f, "%.1f");
    ImGui::SameLine();
    DrawHelpMarker("Brightness of the point light");

    // Cast Shadows toggle
    bool cast = light.castShadows;
    if (ImGui::Checkbox("Cast Shadows", &cast)) {
      light.castShadows = cast;
    }
    ImGui::SameLine();
    DrawHelpMarker(
        "If enabled, this light renders a depth cubemap and casts shadows");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Light", ImVec2(-1, 0))) {
    ImGui::OpenPopup("Delete Light?");
  }

  if (ImGui::BeginPopupModal("Delete Light?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete this point light?");
    ImGui::Text("This cannot be undone!");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      pointLights.erase(pointLights.begin() + currentSelectedIndex);
      currentSelectedIndex = -1;
      currentSelectedType = SelectedType::None;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}

void renderSpotLightManipulationPanel() {
  if (currentSelectedIndex >= spotLights.size()) {
    ImGui::Text("Error: Invalid light selection");
    return;
  }

  auto &light = spotLights[currentSelectedIndex];

  if (g_Fonts.icons) {
    ImGui::PushFont(g_Fonts.icons);
    ImGui::Text(ICON_FA_BULLSEYE);
    ImGui::PopFont();
    ImGui::SameLine();
  }
  ImGui::Text("Spot Light %d", currentSelectedIndex + 1);
  ImGui::Separator();

  if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.1f);
    ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.01f,
                      -1.0f, 1.0f);

    if (ImGui::Button("Normalize Direction", ImVec2(-1, 0))) {
      light.direction = glm::normalize(light.direction);
    }
    ImGui::SameLine();
    DrawHelpMarker("Make direction vector unit length");
  }

  if (ImGui::CollapsingHeader("Light Properties",
                              ImGuiTreeNodeFlags_DefaultOpen)) {
    ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
    ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f, "%.1f");

    DrawSectionHeader("Cone Settings");

    float innerAngle = glm::degrees(glm::acos(light.innerCutOff));
    float outerAngle = glm::degrees(glm::acos(light.outerCutOff));

    if (ImGui::SliderFloat("Inner Angle", &innerAngle, 0.0f, 89.0f, "%.1f°")) {
      light.innerCutOff = glm::cos(glm::radians(innerAngle));
    }
    ImGui::SameLine();
    DrawHelpMarker("Angle of the bright inner cone");

    if (ImGui::SliderFloat("Outer Angle", &outerAngle, 0.0f, 89.0f, "%.1f°")) {
      light.outerCutOff = glm::cos(glm::radians(outerAngle));
    }
    ImGui::SameLine();
    DrawHelpMarker("Angle of the soft falloff cone");

    // Ensure inner angle is always smaller than outer angle
    if (innerAngle > outerAngle) {
      light.outerCutOff = light.innerCutOff;
    }

    // Cast Shadows toggle for spot light (future use if spot shadows are
    // implemented)
    bool cast = light.castShadows;
    if (ImGui::Checkbox("Cast Shadows", &cast)) {
      light.castShadows = cast;
    }
    ImGui::SameLine();
    DrawHelpMarker(
        "Toggle whether this spot light should cast shadows (when supported)");
  }

  ImGui::Spacing();
  ImGui::Separator();
  ImGui::Spacing();

  if (ImGui::Button("Delete Light", ImVec2(-1, 0))) {
    ImGui::OpenPopup("Delete Light?");
  }

  if (ImGui::BeginPopupModal("Delete Light?", NULL,
                             ImGuiWindowFlags_AlwaysAutoResize)) {
    ImGui::Text("Delete this spot light?");
    ImGui::Text("This cannot be undone!");
    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120, 0))) {
      spotLights.erase(spotLights.begin() + currentSelectedIndex);
      currentSelectedIndex = -1;
      currentSelectedType = SelectedType::None;
      ImGui::CloseCurrentPopup();
    }
    ImGui::SetItemDefaultFocus();
    ImGui::SameLine();
    if (ImGui::Button("Cancel", ImVec2(120, 0))) {
      ImGui::CloseCurrentPopup();
    }
    ImGui::EndPopup();
  }
}