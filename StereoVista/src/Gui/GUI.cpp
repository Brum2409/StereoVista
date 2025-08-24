#include "Gui/Gui.h"
#include "Gui/GUITypes.h"
#include "Engine/Core.h"
#include <json.h>
#include <fstream>
#include <sstream>
#include <iostream>
#include <filesystem>
#include "Core/Camera.h"
#include "Core/Voxalizer.h"
#include "Cursors/Base/CursorManager.h"
#include "Engine/SpaceMouseInput.h"
#include "imgui/imgui_sytle.h"
#include <utility>

using namespace GUI;

// Forward declarations
void updateSpaceMouseBounds();
void updateSpaceMouseCursorAnchor();

// Application globals used throughout the GUI system
extern int windowWidth;
extern int windowHeight;
extern Engine::Scene currentScene;
extern Camera camera;
extern bool showGui;
extern bool showFPS;
extern bool isDarkTheme;
extern bool showInfoWindow;
extern bool showSettingsWindow;
extern bool show3DCursor;
extern bool showCursorSettingsWindow;
extern Cursor::CursorManager cursorManager;
extern Engine::Voxelizer* voxelizer;
extern float ambientStrengthFromSkybox;
extern float mouseSmoothingFactor;
extern bool orbitFollowsCursor;

// SpaceMouse variables
extern SpaceMouseInput spaceMouseInput;
extern bool spaceMouseInitialized;

// Cursor variables
extern Cursor::CursorManager cursorManager;

extern GUI::LightingMode currentLightingMode;
extern bool enableShadows;
extern GUI::VCTSettings vctSettings;
extern GUI::ApplicationPreferences::RadianceSettings radianceSettings;
extern bool enableBVH;

// Selection state for object interaction
extern enum class SelectedType {
    None,
    Model,
    PointCloud,
    Sun
} currentSelectedType;
extern int currentSelectedIndex;
extern int currentSelectedMeshIndex;

extern Sun sun;

// Skybox configuration
extern GUI::SkyboxConfig skyboxConfig;
extern std::vector<GUI::CubemapPreset> cubemapPresets;

// User preferences and presets
extern GUI::ApplicationPreferences preferences;
extern std::string currentPresetName;
extern bool isEditingPresetName;
extern char editPresetNameBuffer[256];

// External function declarations
extern void savePreferences();
extern void updateSkybox();

// Constants
extern const int MAX_LIGHTS;

bool InitializeGUI(GLFWwindow* window, bool isDarkTheme) {
    return InitializeImGuiWithFonts(window, isDarkTheme);
}

void CleanupGUI() {
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
}

void renderGUI(bool isLeftEye, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, Engine::Shader* shader) {
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
        if (showFPS) {
            ImGui::SetNextWindowPos(ImVec2(windowWidth - 120, windowHeight - 60));
            ImGui::Begin("FPS Counter", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
            ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
            ImGui::End();
        }
        ImGui::Render();
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return;
    }

    // Main Menu Bar
    if (ImGui::BeginMainMenuBar()) {
        if (ImGui::BeginMenu("File")) {
            if (ImGui::BeginMenu("Import")) {
                if (ImGui::MenuItem("3D Model...")) {
                    auto selection = pfd::open_file("Select a 3D model to import", ".",
                        { "3D Models", "*.obj *.fbx *.3ds *.gltf *.glb",
                          "All Files", "*" }).result();

                    if (!selection.empty()) {
                        std::string filePath = selection[0];
                        try {
                            Engine::Model newModel = *Engine::loadModel(filePath);
                            currentScene.models.push_back(newModel);
                            currentSelectedIndex = currentScene.models.size() - 1;
                            currentSelectedType = SelectedType::Model;
                            updateSpaceMouseBounds();
                        }
                        catch (const std::exception& e) {
                            std::cerr << "Failed to load model: " << e.what() << std::endl;
                        }
                    }
                }
                if (ImGui::MenuItem("Point Cloud...")) {
                    auto selection = pfd::open_file("Select a point cloud to import", ".",
                        { "Point Cloud Files", "*.txt *.xyz *.ply *.pcb *.h5 *.hdf5 *.f5",
                          "All Files", "*" }).result();

                    if (!selection.empty()) {
                        std::string filePath = selection[0];
                        std::string extension = std::filesystem::path(filePath).extension().string();

                        if (extension == ".txt" || extension == ".xyz" || extension == ".ply") {
                            Engine::PointCloud newPointCloud = std::move(Engine::PointCloudLoader::loadPointCloudFile(filePath));
                            newPointCloud.filePath = filePath;
                            currentScene.pointClouds.emplace_back(std::move(newPointCloud));
                            updateSpaceMouseBounds();
                        }
                        else if (extension == ".pcb") {
                            Engine::PointCloud newPointCloud = std::move(Engine::PointCloudLoader::loadFromBinary(filePath));
                            if (newPointCloud.octreeRoot || !newPointCloud.points.empty()) {
                                newPointCloud.filePath = filePath;
                                newPointCloud.name = std::filesystem::path(filePath).stem().string();
                                currentScene.pointClouds.emplace_back(std::move(newPointCloud));
                                std::cout << "[DEBUG] Successfully loaded point cloud: " << filePath << std::endl;
                                updateSpaceMouseBounds();
                            }
                            else {
                                std::cerr << "Failed to load point cloud from: " << filePath << std::endl;
                            }
                        }
                        else if (extension == ".h5" || extension == ".hdf5" || extension == ".f5") {
                            Engine::PointCloud newPointCloud = std::move(Engine::PointCloudLoader::loadPointCloudFile(filePath));
                            if (newPointCloud.octreeRoot || !newPointCloud.points.empty()) {
                                newPointCloud.filePath = filePath;
                                newPointCloud.name = std::filesystem::path(filePath).stem().string();
                                currentScene.pointClouds.emplace_back(std::move(newPointCloud));
                                std::cout << "[DEBUG] Successfully loaded HDF5 point cloud: " << filePath << std::endl;
                                updateSpaceMouseBounds();
                            }
                            else {
                                std::cerr << "Failed to load HDF5 point cloud from: " << filePath << std::endl;
                            }
                        }
                    }
                }
                ImGui::EndMenu();
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Load Scene...")) {
                auto selection = pfd::open_file("Select a scene file to load", ".",
                    { "Scene Files", "*.scene", "All Files", "*" }).result();
                if (!selection.empty()) {
                    try {
                        currentScene = Engine::loadScene(selection[0], camera);
                        currentSelectedIndex = currentScene.models.empty() ? -1 : 0;
                        updateSpaceMouseBounds();
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load scene: " << e.what() << std::endl;
                    }
                }
            }
            if (ImGui::MenuItem("Save Scene...")) {
                auto destination = pfd::save_file("Select a file to save scene", ".",
                    { "Scene Files", "*.scene", "All Files", "*" }).result();
                if (!destination.empty()) {
                    try {
                        Engine::saveScene(destination, currentScene, camera);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to save scene: " << e.what() << std::endl;
                    }
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Create")) {
            if (ImGui::MenuItem("Cube")) {
                Engine::Model newCube = Engine::createCube(glm::vec3(0.8f, 0.8f, 0.8f), 1.0f, 0.0f);
                newCube.scale = glm::vec3(0.5f);
                newCube.position = glm::vec3(0.0f, 0.0f, 0.0f);
                currentScene.models.push_back(newCube);
                currentSelectedIndex = currentScene.models.size() - 1;
                currentSelectedType = SelectedType::Model;
                updateSpaceMouseBounds();
            }
            if (ImGui::MenuItem("Sphere")) {
                Engine::Model newSphere = Engine::createSphere(glm::vec3(0.8f, 0.4f, 0.4f), 1.0f, 0.0f);
                newSphere.scale = glm::vec3(0.5f);
                newSphere.position = glm::vec3(0.0f, 0.0f, 0.0f);
                currentScene.models.push_back(newSphere);
                currentSelectedIndex = currentScene.models.size() - 1;
                currentSelectedType = SelectedType::Model;
                updateSpaceMouseBounds();
            }
            if (ImGui::MenuItem("Cylinder")) {
                Engine::Model newCylinder = Engine::createCylinder(glm::vec3(0.4f, 0.8f, 0.4f), 1.0f, 0.0f);
                newCylinder.scale = glm::vec3(0.5f);
                newCylinder.position = glm::vec3(0.0f, 0.0f, 0.0f);
                currentScene.models.push_back(newCylinder);
                currentSelectedIndex = currentScene.models.size() - 1;
                currentSelectedType = SelectedType::Model;
                updateSpaceMouseBounds();
            }
            if (ImGui::MenuItem("Plane")) {
                Engine::Model newPlane = Engine::createPlane(glm::vec3(0.6f, 0.6f, 0.8f), 1.0f, 0.0f);
                newPlane.scale = glm::vec3(1.0f);
                newPlane.position = glm::vec3(0.0f, 0.0f, 0.0f);
                currentScene.models.push_back(newPlane);
                currentSelectedIndex = currentScene.models.size() - 1;
                currentSelectedType = SelectedType::Model;
                updateSpaceMouseBounds();
            }
            if (ImGui::MenuItem("Torus (Ring)")) {
                Engine::Model newTorus = Engine::createTorus(glm::vec3(0.8f, 0.6f, 0.2f), 1.0f, 0.0f);
                newTorus.scale = glm::vec3(0.8f);
                newTorus.position = glm::vec3(0.0f, 0.0f, 0.0f);
                currentScene.models.push_back(newTorus);
                currentSelectedIndex = currentScene.models.size() - 1;
                currentSelectedType = SelectedType::Model;
                updateSpaceMouseBounds();
            }
            ImGui::EndMenu();
        }


        if (ImGui::BeginMenu("Camera")) {
            // Always use new stereo method
            if (!camera.useNewMethod) {
                camera.useNewMethod = true;
                preferences.useNewStereoMethod = true;
                savePreferences();
            }
            
            // Speed Multiplier Control
            if (ImGui::SliderFloat("Speed Multiplier", &camera.speedFactor, 0.1f, 5.0f, "%.1f")) {
                // Speed factor is automatically applied in the camera's movement calculations
            }
            ImGui::SetItemTooltip("Adjusts camera movement speed (default: 1.0)");
            
            ImGui::Separator();

            // Zoom to Cursor Option
            ImGui::MenuItem("Zoom to Cursor", nullptr, &camera.zoomToCursor);
            ImGui::SetItemTooltip("When enabled, scroll wheel zooms toward/away from cursor position");
            
            ImGui::Separator();
            ImGui::Text("Orbit Mode:");
            
            bool standardOrbit = !camera.orbitAroundCursor && !orbitFollowsCursor;
            bool orbitAroundCursorOption = camera.orbitAroundCursor;
            bool orbitFollowsCursorOption = orbitFollowsCursor;

            if (ImGui::RadioButton("Standard", standardOrbit)) {
                camera.orbitAroundCursor = false;
                orbitFollowsCursor = false;
                preferences.orbitAroundCursor = false;
                preferences.orbitFollowsCursor = false;
                savePreferences();
            }
            ImGui::SetItemTooltip("Orbits around the viewport center at cursor depth");

            if (ImGui::RadioButton("Around Cursor", orbitAroundCursorOption)) {
                camera.orbitAroundCursor = true;
                orbitFollowsCursor = false;
                preferences.orbitAroundCursor = true;
                preferences.orbitFollowsCursor = false;
                savePreferences();
            }
            ImGui::SetItemTooltip("Orbits around the 3D position of the cursor without centering the view");

            if (ImGui::RadioButton("Follow Cursor", orbitFollowsCursorOption)) {
                camera.orbitAroundCursor = false;
                orbitFollowsCursor = true;
                preferences.orbitAroundCursor = false;
                preferences.orbitFollowsCursor = true;
                savePreferences();
            }
            ImGui::SetItemTooltip("Centers the view on cursor position before orbiting");
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Cursor")) {
            auto* sphereCursor = cursorManager.getSphereCursor();
            auto* fragmentCursor = cursorManager.getFragmentCursor();
            auto* planeCursor = cursorManager.getPlaneCursor();

            bool showSphereCursor = sphereCursor->isVisible();
            if (ImGui::MenuItem("Show Sphere Cursor", nullptr, &showSphereCursor)) {
                sphereCursor->setVisible(showSphereCursor);
            }

            bool showFragmentCursor = fragmentCursor->isVisible();
            if (ImGui::MenuItem("Show Fragment Cursor", nullptr, &showFragmentCursor)) {
                fragmentCursor->setVisible(showFragmentCursor);
            }

            bool showPlaneCursor = planeCursor->isVisible();
            if (ImGui::MenuItem("Show Plane Cursor", nullptr, &showPlaneCursor)) {
                planeCursor->setVisible(showPlaneCursor);
            }
            
            ImGui::Separator();
            if (ImGui::BeginMenu("Presets")) {
                std::vector<std::string> presetNames = Engine::CursorPresetManager::getPresetNames();
                for (const auto& name : presetNames) {
                    if (ImGui::MenuItem(name.c_str(), nullptr, currentPresetName == name)) {
                        currentPresetName = name;
                        Engine::CursorPreset loadedPreset = Engine::CursorPresetManager::applyCursorPreset(name);

                        sphereCursor->setVisible(loadedPreset.showSphereCursor);
                        sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(loadedPreset.sphereScalingMode));
                        sphereCursor->setFixedRadius(loadedPreset.sphereFixedRadius);
                        sphereCursor->setTransparency(loadedPreset.sphereTransparency);
                        sphereCursor->setShowInnerSphere(loadedPreset.showInnerSphere);
                        sphereCursor->setColor(loadedPreset.cursorColor);
                        sphereCursor->setInnerSphereColor(loadedPreset.innerSphereColor);
                        sphereCursor->setInnerSphereFactor(loadedPreset.innerSphereFactor);
                        sphereCursor->setEdgeSoftness(loadedPreset.cursorEdgeSoftness);
                        sphereCursor->setCenterTransparency(loadedPreset.cursorCenterTransparency);

                        fragmentCursor->setVisible(loadedPreset.showFragmentCursor);
                        fragmentCursor->setBaseInnerRadius(loadedPreset.fragmentBaseInnerRadius);

                        planeCursor->setVisible(loadedPreset.showPlaneCursor);
                        planeCursor->setDiameter(loadedPreset.planeDiameter);
                        planeCursor->setColor(loadedPreset.planeColor);

                        bool showOrbitCenter = cursorManager.isShowOrbitCenter();
                        cursorManager.setShowOrbitCenter(showOrbitCenter);

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

        if (ImGui::MenuItem("Settings")) {
            showSettingsWindow = true;
        }

        ImGui::EndMainMenuBar();
    }

    // Scene Objects Window
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(300, viewport->Size.y - ImGui::GetFrameHeight()));
    ImGui::Begin("Scene Objects", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse);

    if (ImGui::BeginChild("ObjectList", ImVec2(0, 268), true)) {
        ImGui::Columns(2, "ObjectColumns", false);
        ImGui::SetColumnWidth(0, 60);

        // Sun Object
        ImGui::PushID("sun");
        bool sunVisible = sun.enabled;
        if (ImGui::Checkbox("##visible", &sunVisible)) sun.enabled = sunVisible;
        ImGui::NextColumn();

        bool isSunSelected = (currentSelectedType == SelectedType::Sun);
        ImGui::AlignTextToFramePadding();
        if (ImGui::Selectable("Sun", isSunSelected, ImGuiSelectableFlags_SpanAllColumns)) {
            currentSelectedType = SelectedType::Sun;
            currentSelectedIndex = -1;
            currentSelectedMeshIndex = -1;  // Reset mesh selection
        }
        ImGui::NextColumn();
        ImGui::PopID();

        // Models List
        for (int i = 0; i < currentScene.models.size(); i++) {
            ImGui::PushID(i);
            ImGui::AlignTextToFramePadding();

            bool visible = currentScene.models[i].visible;
            if (ImGui::Checkbox("##visible", &visible)) {
                currentScene.models[i].visible = visible;
            }
            ImGui::NextColumn();

            bool isModelSelected = (currentSelectedIndex == i && currentSelectedType == SelectedType::Model);
            bool hasMeshes = currentScene.models[i].getMeshes().size() > 0;

            ImGui::AlignTextToFramePadding();
            ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;

            // Only make it a leaf if there are no meshes
            if (!hasMeshes) flags |= ImGuiTreeNodeFlags_Leaf;

            // Select the model node if it's selected and no mesh is selected
            if (isModelSelected && currentSelectedMeshIndex == -1) flags |= ImGuiTreeNodeFlags_Selected;

            bool nodeOpen = ImGui::TreeNodeEx(currentScene.models[i].name.c_str(), flags);

            // Handle model selection
            if (ImGui::IsItemClicked()) {
                currentSelectedType = SelectedType::Model;
                currentSelectedIndex = i;
                currentSelectedMeshIndex = -1;  // Reset mesh selection when selecting model
            }

            ImGui::NextColumn();

            if (nodeOpen && hasMeshes) {
                // Display individual meshes
                for (size_t meshIndex = 0; meshIndex < currentScene.models[i].getMeshes().size(); meshIndex++) {
                    ImGui::Columns(2, "MeshColumns", false);
                    ImGui::SetColumnWidth(0, 60);

                    ImGui::PushID(static_cast<int>(meshIndex));
                    bool meshVisible = currentScene.models[i].getMeshes()[meshIndex].visible;
                    if (ImGui::Checkbox("##meshvisible", &meshVisible)) {
                        currentScene.models[i].getMeshes()[meshIndex].visible = meshVisible;
                    }
                    ImGui::PopID();
                    ImGui::NextColumn();

                    // Mesh selection flags
                    ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf |
                        ImGuiTreeNodeFlags_NoTreePushOnOpen |
                        ImGuiTreeNodeFlags_SpanAvailWidth;

                    // Highlight selected mesh
                    if (isModelSelected && currentSelectedMeshIndex == static_cast<int>(meshIndex)) {
                        meshFlags |= ImGuiTreeNodeFlags_Selected;
                    }

                    ImGui::Indent(20.0f);
                    ImGui::TreeNodeEx(("Mesh " + std::to_string(meshIndex + 1)).c_str(), meshFlags);

                    // Handle mesh selection
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
        }

        // Point Clouds List
        for (int i = 0; i < currentScene.pointClouds.size(); i++) {
            ImGui::PushID(i + currentScene.models.size());
            bool isSelected = (currentSelectedIndex == i && currentSelectedType == SelectedType::PointCloud);

            bool visible = currentScene.pointClouds[i].visible;
            if (ImGui::Checkbox("##visible", &visible)) {
                currentScene.pointClouds[i].visible = visible;
            }
            ImGui::NextColumn();

            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable(currentScene.pointClouds[i].name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                currentSelectedType = SelectedType::PointCloud;
                currentSelectedIndex = i;
                currentSelectedMeshIndex = -1;  // Reset mesh selection
            }
            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);
        ImGui::EndChild();
    }

    ImGui::Separator();

    // Object Manipulation Panels
    if (currentSelectedType == SelectedType::Model && currentSelectedIndex >= 0 &&
        currentSelectedIndex < currentScene.models.size()) {

        auto& model = currentScene.models[currentSelectedIndex];

        // If a specific mesh is selected, render mesh manipulation panel
        if (currentSelectedMeshIndex >= 0 &&
            currentSelectedMeshIndex < static_cast<int>(model.getMeshes().size())) {
            renderMeshManipulationPanel(model, currentSelectedMeshIndex, shader);
        }
        else {
            // Otherwise render the model manipulation panel
            renderModelManipulationPanel(model, shader);
        }
    }
    else if (currentSelectedType == SelectedType::PointCloud && currentSelectedIndex >= 0 &&
        currentSelectedIndex < currentScene.pointClouds.size()) {
        renderPointCloudManipulationPanel(currentScene.pointClouds[currentSelectedIndex]);
    }
    else if (currentSelectedType == SelectedType::Sun) {
        renderSunManipulationPanel();
    }

    ImGui::End();

    // Settings Windows
    if (showSettingsWindow) {
        renderSettingsWindow();
    }

    // Cursor Settings Window
    if (showCursorSettingsWindow) {
        renderCursorSettingsWindow();
    }

    // FPS Counter
    if (showFPS) {
        ImGui::SetNextWindowPos(ImVec2(windowWidth - 120, windowHeight - 60));
        ImGui::Begin("FPS Counter", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void renderSettingsWindow() {
    ImGui::SetNextWindowSize(ImVec2(450, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings", &showSettingsWindow);
    bool settingsChanged = false;

    if (ImGui::BeginTabBar("SettingsTabs")) {
        
        // =====================
        // TAB 1: CAMERA & VIEW
        // =====================
        if (ImGui::BeginTabItem("Camera & View")) {
            
            // Camera Properties
            ImGui::BeginGroup();
            ImGui::Text("Camera Properties");
            ImGui::Separator();
            
            if (ImGui::SliderFloat("Field of View", &camera.Zoom, 1.0f, 120.0f)) {
                preferences.fov = camera.Zoom;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Controls the camera's field of view. Higher values show more of the scene");

            if (ImGui::SliderFloat("Near Plane", &currentScene.settings.nearPlane, 0.01f, 10.0f)) {
                preferences.nearPlane = currentScene.settings.nearPlane;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Minimum visible distance from camera. Smaller values can cause visual artifacts");

            if (ImGui::SliderFloat("Far Plane", &currentScene.settings.farPlane, 10.0f, 1000.0f)) {
                preferences.farPlane = currentScene.settings.farPlane;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Maximum visible distance from camera. Higher values may impact performance");
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // Stereo Settings
            ImGui::BeginGroup();
            ImGui::Text("Stereo Settings");
            ImGui::Separator();
            
            // Always disable stereo visualization
            preferences.showStereoVisualization = false;

            if (ImGui::SliderFloat("Separation", &currentScene.settings.separation, 0.01f, 2.0f)) {
                preferences.separation = currentScene.settings.separation;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Adjusts the distance between stereo views. Higher values increase 3D effect");

            if (ImGui::Checkbox("Auto Convergence", &currentScene.settings.autoConvergence)) {
                preferences.autoConvergence = currentScene.settings.autoConvergence;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Automatically sets convergence based on distance to nearest object");

            if (currentScene.settings.autoConvergence) {
                if (ImGui::SliderFloat("Distance Factor", &currentScene.settings.convergenceDistanceFactor, 0.1f, 2.0f)) {
                    preferences.convergenceDistanceFactor = currentScene.settings.convergenceDistanceFactor;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Multiplier for the distance to nearest object used for convergence");
                
                // Display current auto-calculated convergence value
                ImGui::Text("Current Convergence: %.2f", currentScene.settings.convergence);
                ImGui::SetItemTooltip("Auto-calculated convergence distance based on nearest object");
            } else {
                if (ImGui::SliderFloat("Convergence", &currentScene.settings.convergence, 0.0f, 40.0f)) {
                    preferences.convergence = currentScene.settings.convergence;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Sets the focal point distance where left and right views converge");
            }
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // Movement & Controls
            ImGui::BeginGroup();
            ImGui::Text("Movement & Controls");
            ImGui::Separator();
            
            if (ImGui::SliderFloat("Mouse Sensitivity", &camera.MouseSensitivity, 0.01f, 0.08f)) {
                preferences.mouseSensitivity = camera.MouseSensitivity;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Adjusts how quickly the camera rotates in response to mouse movement");

            if (ImGui::SliderFloat("Mouse Smoothing", &mouseSmoothingFactor, 0.1f, 1.0f)) {
                preferences.mouseSmoothingFactor = mouseSmoothingFactor;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Controls smoothness of mouse movement. Lower values = smoother, higher values = more responsive");

            if (ImGui::SliderFloat("Speed Multiplier", &camera.speedFactor, 0.1f, 5.0f)) {
                preferences.cameraSpeedFactor = camera.speedFactor;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Multiplies base movement speed. Useful for navigating larger scenes");

            if (ImGui::Checkbox("Zoom to Cursor", &camera.zoomToCursor)) {
                preferences.zoomToCursor = camera.zoomToCursor;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("When enabled, scrolling zooms toward or away from the 3D cursor position");
            
            ImGui::Text("Orbiting Behavior");
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
            ImGui::SetItemTooltip("Orbits around the viewport center at cursor depth");

            if (ImGui::RadioButton("Orbit Around Cursor", orbitAroundCursorOption)) {
                camera.orbitAroundCursor = true;
                orbitFollowsCursor = false;
                preferences.orbitAroundCursor = true;
                preferences.orbitFollowsCursor = false;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Orbits around the 3D position of the cursor without centering the view");

            if (ImGui::RadioButton("Orbit Follows Cursor (Center)", orbitFollowsCursorOption)) {
                camera.orbitAroundCursor = false;
                orbitFollowsCursor = true;
                preferences.orbitAroundCursor = false;
                preferences.orbitFollowsCursor = true;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Centers the view on cursor position before orbiting");
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // Smooth Scrolling
            ImGui::BeginGroup();
            ImGui::Text("Smooth Scrolling");
            ImGui::Separator();
            
            if (ImGui::Checkbox("Enable Smooth Scrolling", &camera.useSmoothScrolling)) {
                preferences.useSmoothScrolling = camera.useSmoothScrolling;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Enable physics-based smooth scrolling");

            if (camera.useSmoothScrolling) {
                if (ImGui::SliderFloat("Momentum", &camera.scrollMomentum, 0.0f, 1.0f)) {
                    preferences.scrollMomentum = camera.scrollMomentum;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Controls how much scrolling 'carries' (higher = more momentum)");

                if (ImGui::SliderFloat("Max Velocity", &camera.maxScrollVelocity, 0.5f, 10.0f)) {
                    preferences.maxScrollVelocity = camera.maxScrollVelocity;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Maximum scroll speed");

                if (ImGui::SliderFloat("Deceleration", &camera.scrollDeceleration, 1.0f, 20.0f)) {
                    preferences.scrollDeceleration = camera.scrollDeceleration;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("How quickly scrolling slows down");
            }
            ImGui::EndGroup();

            ImGui::EndTabItem();
        }

        // ============================
        // TAB 2: RENDERING & GRAPHICS
        // ============================
        if (ImGui::BeginTabItem("Rendering & Graphics")) {
            
            // Rendering Mode
            ImGui::BeginGroup();
            ImGui::Text("Rendering Mode");
            ImGui::Separator();
            
            const char* lightingModes[] = { "Shadow Mapping", "Voxel Cone Tracing", "Radiance" };
            int currentLightingMode = static_cast<int>(preferences.lightingMode);
            if (ImGui::Combo("Lighting Mode", &currentLightingMode, lightingModes, IM_ARRAYSIZE(lightingModes))) {
                preferences.lightingMode = static_cast<GUI::LightingMode>(currentLightingMode);
                ::currentLightingMode = preferences.lightingMode;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Switch between traditional shadow mapping, voxel cone tracing for global illumination, and radiance rendering");

            if (ImGui::Checkbox("Wireframe Mode", &camera.wireframe)) {
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Renders objects as wireframes instead of solid surfaces");
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // Shadow Mapping Settings
            if (preferences.lightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
                ImGui::BeginGroup();
                ImGui::Text("Shadow Mapping Settings");
                ImGui::Separator();
                
                if (ImGui::Checkbox("Enable Shadows", &preferences.enableShadows)) {
                    ::enableShadows = preferences.enableShadows;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Toggle shadow mapping on/off");
                ImGui::EndGroup();
            }
            
            // Voxel Cone Tracing Settings
            else if (preferences.lightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
                ImGui::BeginGroup();
                ImGui::Text("Voxel Cone Tracing Settings");
                ImGui::Separator();
                
                // VCT Components
                ImGui::Text("Enable VCT Components");
                if (ImGui::Checkbox("Indirect Diffuse Light", &preferences.vctSettings.indirectDiffuseLight)) {
                    vctSettings.indirectDiffuseLight = preferences.vctSettings.indirectDiffuseLight;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Enable indirect diffuse lighting (global illumination effects)");

                if (ImGui::Checkbox("Indirect Specular Light", &preferences.vctSettings.indirectSpecularLight)) {
                    vctSettings.indirectSpecularLight = preferences.vctSettings.indirectSpecularLight;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Enable indirect specular reflections (glossy reflections)");

                if (ImGui::Checkbox("Direct Light", &preferences.vctSettings.directLight)) {
                    vctSettings.directLight = preferences.vctSettings.directLight;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Enable direct lighting from light sources");

                if (ImGui::Checkbox("Shadows", &preferences.vctSettings.shadows)) {
                    vctSettings.shadows = preferences.vctSettings.shadows;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Enable soft shadows through voxel cone tracing");
                
                ImGui::Separator();
                
                // Quality Settings
                ImGui::Text("Quality Settings");
                
                // Quality presets
                if (ImGui::Button("Low Quality")) {
                    preferences.vctSettings.diffuseConeCount = 1;
                    preferences.vctSettings.shadowSampleCount = 5;
                    preferences.vctSettings.shadowStepMultiplier = 0.3f;
                    preferences.vctSettings.tracingMaxDistance = 1.0f;
                    vctSettings.diffuseConeCount = preferences.vctSettings.diffuseConeCount;
                    vctSettings.shadowSampleCount = preferences.vctSettings.shadowSampleCount;
                    vctSettings.shadowStepMultiplier = preferences.vctSettings.shadowStepMultiplier;
                    vctSettings.tracingMaxDistance = preferences.vctSettings.tracingMaxDistance;
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Medium Quality")) {
                    preferences.vctSettings.diffuseConeCount = 5;
                    preferences.vctSettings.shadowSampleCount = 8;
                    preferences.vctSettings.shadowStepMultiplier = 0.2f;
                    preferences.vctSettings.tracingMaxDistance = 1.5f;
                    vctSettings.diffuseConeCount = preferences.vctSettings.diffuseConeCount;
                    vctSettings.shadowSampleCount = preferences.vctSettings.shadowSampleCount;
                    vctSettings.shadowStepMultiplier = preferences.vctSettings.shadowStepMultiplier;
                    vctSettings.tracingMaxDistance = preferences.vctSettings.tracingMaxDistance;
                    settingsChanged = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("High Quality")) {
                    preferences.vctSettings.diffuseConeCount = 9;
                    preferences.vctSettings.shadowSampleCount = 15;
                    preferences.vctSettings.shadowStepMultiplier = 0.1f;
                    preferences.vctSettings.tracingMaxDistance = 2.0f;
                    vctSettings.diffuseConeCount = preferences.vctSettings.diffuseConeCount;
                    vctSettings.shadowSampleCount = preferences.vctSettings.shadowSampleCount;
                    vctSettings.shadowStepMultiplier = preferences.vctSettings.shadowStepMultiplier;
                    vctSettings.tracingMaxDistance = preferences.vctSettings.tracingMaxDistance;
                    settingsChanged = true;
                }
                
                const char* coneCountOptions[] = { "1 (Low)", "5 (Medium)", "9 (High)" };
                int coneCountIndex = 0;
                if (preferences.vctSettings.diffuseConeCount <= 1) coneCountIndex = 0;
                else if (preferences.vctSettings.diffuseConeCount <= 5) coneCountIndex = 1;
                else coneCountIndex = 2;

                if (ImGui::Combo("Diffuse Cone Count", &coneCountIndex, coneCountOptions, IM_ARRAYSIZE(coneCountOptions))) {
                    switch (coneCountIndex) {
                    case 0: preferences.vctSettings.diffuseConeCount = 1; break;
                    case 1: preferences.vctSettings.diffuseConeCount = 5; break;
                    case 2: preferences.vctSettings.diffuseConeCount = 9; break;
                    }
                    vctSettings.diffuseConeCount = preferences.vctSettings.diffuseConeCount;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Controls the number of cones used for indirect diffuse lighting.\nMore cones = better quality but slower performance");

                if (ImGui::SliderFloat("Max Tracing Distance", &preferences.vctSettings.tracingMaxDistance, 0.5f, 2.5f, "%.2f")) {
                    vctSettings.tracingMaxDistance = preferences.vctSettings.tracingMaxDistance;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Maximum distance for tracing cones (in grid units).\nLarger values capture more distant lighting but reduce performance");

                int shadowSamples = preferences.vctSettings.shadowSampleCount;
                if (ImGui::SliderInt("Shadow Samples", &shadowSamples, 5, 20)) {
                    preferences.vctSettings.shadowSampleCount = shadowSamples;
                    vctSettings.shadowSampleCount = shadowSamples;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Number of samples taken when tracing shadow cones.\nMore samples = smoother shadows but slower performance");

                if (ImGui::SliderFloat("Shadow Step Multiplier", &preferences.vctSettings.shadowStepMultiplier, 0.05f, 0.5f, "%.3f")) {
                    vctSettings.shadowStepMultiplier = preferences.vctSettings.shadowStepMultiplier;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Controls how fast shadow cone tracing advances.\nLarger values are faster but may miss details");
                
                ImGui::Separator();
                
                // Grid Configuration
                ImGui::Text("Grid Configuration");
                
                float gridSize = voxelizer->getVoxelGridSize();
                if (ImGui::SliderFloat("Grid Dimensions", &gridSize, 1.0f, 50.0f)) {
                    voxelizer->setVoxelGridSize(gridSize);
                }
                ImGui::SetItemTooltip("World space area coverage for voxelization (larger = more world captured, more voxels)");

                float voxelSize = preferences.vctSettings.voxelSize;
                if (ImGui::SliderFloat("VCT Voxel Resolution", &voxelSize, 1.0f / 256.0f, 1.0f / 32.0f, "%.5f")) {
                    preferences.vctSettings.voxelSize = voxelSize;
                    vctSettings.voxelSize = voxelSize;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Voxel resolution for cone tracing calculations (smaller = higher quality but slower)");
                
                ImGui::Separator();
                
                // Debug Visualization
                ImGui::Text("Debug Visualization");
                
                ImGui::Checkbox("Show Voxel Visualization", &voxelizer->showDebugVisualization);
                ImGui::SetItemTooltip("Show the voxel representation for debugging");

                float debugVoxelSize = voxelizer->debugVoxelSize;
                if (ImGui::SliderFloat("Debug Cube Size", &debugVoxelSize, 0.001f, 0.1f, "%.4f")) {
                    voxelizer->debugVoxelSize = debugVoxelSize;
                }
                ImGui::SetItemTooltip("Visual size of debug voxel cubes (affected by mipmap level)");

                float opacity = voxelizer->voxelOpacity;
                if (ImGui::SliderFloat("Voxel Opacity", &opacity, 0.0f, 1.0f)) {
                    voxelizer->voxelOpacity = opacity;
                }

                float colorIntensity = voxelizer->voxelColorIntensity;
                if (ImGui::SliderFloat("Color Intensity", &colorIntensity, 0.0f, 5.0f)) {
                    voxelizer->voxelColorIntensity = colorIntensity;
                }
                ImGui::EndGroup();
            }
            
            // Radiance Raytracing Settings
            else if (preferences.lightingMode == GUI::LIGHTING_RADIANCE) {
                ImGui::BeginGroup();
                ImGui::Text("Radiance Raytracing Settings");
                ImGui::Separator();
                
                if (ImGui::Checkbox("Enable Raytracing", &preferences.radianceSettings.enableRaytracing)) {
                    ::radianceSettings.enableRaytracing = preferences.radianceSettings.enableRaytracing;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Enable proper raytracing from camera through pixels");
                
                // Performance group
                ImGui::Text("Performance");
                if (ImGui::SliderInt("Max Bounces", &preferences.radianceSettings.maxBounces, 1, 4)) {
                    ::radianceSettings.maxBounces = preferences.radianceSettings.maxBounces;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Maximum number of ray bounces (1=direct lighting only, 2+=indirect lighting)");
                
                if (ImGui::SliderInt("Samples Per Pixel", &preferences.radianceSettings.samplesPerPixel, 1, 100)) {
                    ::radianceSettings.samplesPerPixel = preferences.radianceSettings.samplesPerPixel;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Number of rays cast per pixel (higher = better quality, lower performance)");
                
                if (ImGui::SliderFloat("Ray Max Distance", &preferences.radianceSettings.rayMaxDistance, 10.0f, 100.0f)) {
                    ::radianceSettings.rayMaxDistance = preferences.radianceSettings.rayMaxDistance;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Maximum distance for ray casting");
                
                ImGui::Separator();
                
                // Lighting Features group
                ImGui::Text("Lighting Features");
                if (ImGui::Checkbox("Enable Indirect Lighting", &preferences.radianceSettings.enableIndirectLighting)) {
                    ::radianceSettings.enableIndirectLighting = preferences.radianceSettings.enableIndirectLighting;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Enable indirect lighting through ray bounces");
                
                if (ImGui::Checkbox("Enable Emissive Lighting", &preferences.radianceSettings.enableEmissiveLighting)) {
                    ::radianceSettings.enableEmissiveLighting = preferences.radianceSettings.enableEmissiveLighting;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Use emissive objects as light sources");
                
                ImGui::Separator();
                
                // Intensity Controls group
                ImGui::Text("Intensity Controls");
                if (ImGui::SliderFloat("Indirect Intensity", &preferences.radianceSettings.indirectIntensity, 0.0f, 1.0f)) {
                    ::radianceSettings.indirectIntensity = preferences.radianceSettings.indirectIntensity;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Intensity of indirect lighting contribution");
                
                if (ImGui::SliderFloat("Sky Intensity", &preferences.radianceSettings.skyIntensity, 0.0f, 2.0f)) {
                    ::radianceSettings.skyIntensity = preferences.radianceSettings.skyIntensity;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Brightness of sky lighting when rays miss geometry");
                
                if (ImGui::SliderFloat("Emissive Intensity", &preferences.radianceSettings.emissiveIntensity, 0.0f, 3.0f)) {
                    ::radianceSettings.emissiveIntensity = preferences.radianceSettings.emissiveIntensity;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Multiplier for emissive object brightness");
                
                // Material Override
                if (ImGui::SliderFloat("Material Roughness", &preferences.radianceSettings.materialRoughness, 0.0f, 1.0f)) {
                    ::radianceSettings.materialRoughness = preferences.radianceSettings.materialRoughness;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Global material roughness (0=mirror, 1=diffuse)");
                
                ImGui::Separator();
                ImGui::Text("Acceleration Structure");
                if (ImGui::Checkbox("Enable BVH", &preferences.radianceSettings.enableBVH)) {
                    ::enableBVH = preferences.radianceSettings.enableBVH;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Enable Bounding Volume Hierarchy for faster ray-triangle intersection");
                
                ImGui::EndGroup();
            }

            ImGui::EndTabItem();
        }

        // ===============================
        // TAB 3: ENVIRONMENT & LIGHTING
        // ===============================
        if (ImGui::BeginTabItem("Environment & Lighting")) {
            
            // Skybox Configuration
            ImGui::BeginGroup();
            ImGui::Text("Skybox Configuration");
            ImGui::Separator();
            
            const char* skyboxTypes[] = { "Cubemap Texture", "Solid Color", "Gradient" };
            int currentType = static_cast<int>(skyboxConfig.type);
            if (ImGui::Combo("Skybox Type", &currentType, skyboxTypes, IM_ARRAYSIZE(skyboxTypes))) {
                skyboxConfig.type = static_cast<GUI::SkyboxType>(currentType);
                updateSkybox();
                preferences.skyboxType = static_cast<int>(skyboxConfig.type);
                savePreferences();
            }
            ImGui::SetItemTooltip("Change the type of skybox used in the scene");

            // Type-specific controls
            if (skyboxConfig.type == GUI::SKYBOX_CUBEMAP) {
                std::vector<const char*> presetNames;
                for (const auto& preset : cubemapPresets) {
                    presetNames.push_back(preset.name.c_str());
                }

                if (ImGui::Combo("Cubemap Theme", &skyboxConfig.selectedCubemap,
                    presetNames.data(), static_cast<int>(presetNames.size()))) {
                    updateSkybox();
                    preferences.selectedCubemap = skyboxConfig.selectedCubemap;
                    savePreferences();
                }

                if (skyboxConfig.selectedCubemap >= 0 && skyboxConfig.selectedCubemap < cubemapPresets.size()) {
                    ImGui::SetItemTooltip("%s", cubemapPresets[skyboxConfig.selectedCubemap].description.c_str());
                }

                if (ImGui::Button("Browse Custom Skybox")) {
                    auto selection = pfd::select_folder("Select skybox directory").result();
                    if (!selection.empty()) {
                        std::string path = selection + "/";
                        std::string dirName = std::filesystem::path(selection).filename().string();
                        std::string name = "Custom: " + dirName;

                        GUI::CubemapPreset newPreset;
                        newPreset.name = name;
                        newPreset.path = path;
                        newPreset.description = "Custom skybox from: " + path;
                        cubemapPresets.push_back(newPreset);

                        skyboxConfig.selectedCubemap = static_cast<int>(cubemapPresets.size()) - 1;
                        updateSkybox();
                        preferences.selectedCubemap = skyboxConfig.selectedCubemap;
                        savePreferences();
                    }
                }
                ImGui::SetItemTooltip("Select a directory containing skybox textures (right.jpg, left.jpg, etc. OR posx.jpg, negx.jpg, etc.)");
            }
            else if (skyboxConfig.type == GUI::SKYBOX_SOLID_COLOR) {
                if (ImGui::ColorEdit3("Skybox Color", glm::value_ptr(skyboxConfig.solidColor))) {
                    updateSkybox();
                    preferences.skyboxSolidColor = skyboxConfig.solidColor;
                    savePreferences();
                }
                ImGui::SetItemTooltip("Set a single color for the entire skybox");
            }
            else if (skyboxConfig.type == GUI::SKYBOX_GRADIENT) {
                bool colorChanged = false;
                colorChanged |= ImGui::ColorEdit3("Top Color", glm::value_ptr(skyboxConfig.gradientTopColor));
                ImGui::SetItemTooltip("Color of the top portion of the skybox");

                colorChanged |= ImGui::ColorEdit3("Bottom Color", glm::value_ptr(skyboxConfig.gradientBottomColor));
                ImGui::SetItemTooltip("Color of the bottom portion of the skybox");

                if (colorChanged) {
                    updateSkybox();
                    preferences.skyboxGradientTop = skyboxConfig.gradientTopColor;
                    preferences.skyboxGradientBottom = skyboxConfig.gradientBottomColor;
                    savePreferences();
                }
            }

            if (ImGui::SliderFloat("Ambient Strength", &ambientStrengthFromSkybox, 0.0f, 1.0f)) {
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Controls how much the skybox illuminates the scene. Higher values create brighter ambient lighting");
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // Sun Lighting
            ImGui::BeginGroup();
            ImGui::Text("Sun Lighting");
            ImGui::Separator();
            
            settingsChanged |= ImGui::Checkbox("Enable Sun", &sun.enabled);
            ImGui::SetItemTooltip("Toggles sun lighting on/off");
            
            settingsChanged |= ImGui::ColorEdit3("Sun Color", glm::value_ptr(sun.color));
            ImGui::SetItemTooltip("Sets the color of sunlight in the scene");

            settingsChanged |= ImGui::SliderFloat("Sun Intensity", &sun.intensity, 0.0f, 1.0f);
            ImGui::SetItemTooltip("Controls the brightness of sunlight");

            settingsChanged |= ImGui::DragFloat3("Sun Direction", glm::value_ptr(sun.direction), 0.01f, -1.0f, 1.0f);
            ImGui::SetItemTooltip("Sets the direction of sunlight. Affects shadows and lighting");
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // Default Material Properties
            ImGui::BeginGroup();
            ImGui::Text("Default Material Properties");
            ImGui::Separator();
            
            static float diffuseReflectivity = 0.8f;
            if (ImGui::SliderFloat("Diffuse Reflectivity", &diffuseReflectivity, 0.0f, 1.0f)) {
                // Update default material property
            }

            static float specularReflectivity = 0.0f;
            if (ImGui::SliderFloat("Specular Reflectivity", &specularReflectivity, 0.0f, 1.0f)) {
                // Update default material property
            }

            static float specularDiffusion = 0.5f;
            if (ImGui::SliderFloat("Specular Diffusion", &specularDiffusion, 0.0f, 1.0f)) {
                // Update default material property
            }
            ImGui::EndGroup();

            ImGui::EndTabItem();
        }

        // ==============================
        // TAB 4: INTERFACE & DISPLAY
        // ==============================
        if (ImGui::BeginTabItem("Interface & Display")) {
            
            // Interface Options
            ImGui::BeginGroup();
            ImGui::Text("Interface Options");
            ImGui::Separator();
            
            if (ImGui::Checkbox("Show FPS Counter", &showFPS)) {
                preferences.showFPS = showFPS;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Shows/hides the FPS counter in the top-right corner");

            if (ImGui::Checkbox("Show GUI", &showGui)) {
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Toggle the entire GUI interface on/off");

            if (ImGui::Checkbox("Dark Theme", &isDarkTheme)) {
                SetupImGuiStyle(isDarkTheme, 1.0f);
                preferences.isDarkTheme = isDarkTheme;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Switches between light and dark color themes for the interface");

            if (ImGui::Checkbox("Show Radar", &preferences.radarEnabled)) {
                currentScene.settings.radarEnabled = preferences.radarEnabled;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Show a radar overlay with the camera frustum");
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // Radar Configuration
            if (preferences.radarEnabled) {
                ImGui::BeginGroup();
                ImGui::Text("Radar Configuration");
                ImGui::Separator();
                
                if (ImGui::SliderFloat("X Position", &preferences.radarPos.x, -1.0f, 1.0f)) {
                    currentScene.settings.radarPos.x = preferences.radarPos.x;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Horizontal position of the radar (-1 to 1)");

                if (ImGui::SliderFloat("Y Position", &preferences.radarPos.y, -1.0f, 1.0f)) {
                    currentScene.settings.radarPos.y = preferences.radarPos.y;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Vertical position of the radar (-1 to 1)");

                if (ImGui::SliderFloat("Scale", &preferences.radarScale, 0.001f, 0.5f)) {
                    currentScene.settings.radarScale = preferences.radarScale;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Size of the radar display");

                if (ImGui::Checkbox("Show Scene in Radar", &preferences.radarShowScene)) {
                    currentScene.settings.radarShowScene = preferences.radarShowScene;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Show the scene models in the radar view");
                ImGui::EndGroup();
                
                ImGui::Spacing();
            }
            
            // Zero Plane
            ImGui::BeginGroup();
            ImGui::Text("Zero Plane");
            ImGui::Separator();
            
            if (ImGui::Checkbox("Show Zero Plane", &preferences.showZeroPlane)) {
                currentScene.settings.showZeroPlane = preferences.showZeroPlane;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Display the zero plane in the visualization");
            ImGui::EndGroup();

            ImGui::EndTabItem();
        }

        // ============================
        // TAB 5: INPUT & CONTROLS
        // ============================
        if (ImGui::BeginTabItem("Input & Controls")) {
            
            // Mouse Settings
            ImGui::BeginGroup();
            ImGui::Text("Mouse Settings");
            ImGui::Separator();
            
            if (ImGui::SliderFloat("Mouse Sensitivity", &camera.MouseSensitivity, 0.01f, 0.08f)) {
                preferences.mouseSensitivity = camera.MouseSensitivity;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Adjusts how quickly the camera rotates in response to mouse movement");

            if (ImGui::SliderFloat("Mouse Smoothing", &mouseSmoothingFactor, 0.1f, 1.0f)) {
                preferences.mouseSmoothingFactor = mouseSmoothingFactor;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Controls smoothness of mouse movement. Lower values = smoother, higher values = more responsive");

            if (ImGui::SliderFloat("Speed Multiplier", &camera.speedFactor, 0.1f, 5.0f)) {
                preferences.cameraSpeedFactor = camera.speedFactor;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Multiplies base movement speed. Useful for navigating larger scenes");

            if (ImGui::Checkbox("Zoom to Cursor", &camera.zoomToCursor)) {
                preferences.zoomToCursor = camera.zoomToCursor;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("When enabled, scrolling zooms toward or away from the 3D cursor position");
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // Camera Behavior
            ImGui::BeginGroup();
            ImGui::Text("Camera Behavior");
            ImGui::Separator();
            
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
            ImGui::SetItemTooltip("Orbits around the viewport center at cursor depth");

            if (ImGui::RadioButton("Orbit Around Cursor", orbitAroundCursorOption)) {
                camera.orbitAroundCursor = true;
                orbitFollowsCursor = false;
                preferences.orbitAroundCursor = true;
                preferences.orbitFollowsCursor = false;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Orbits around the 3D position of the cursor without centering the view");

            if (ImGui::RadioButton("Orbit Follows Cursor (Center)", orbitFollowsCursorOption)) {
                camera.orbitAroundCursor = false;
                orbitFollowsCursor = true;
                preferences.orbitAroundCursor = false;
                preferences.orbitFollowsCursor = true;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Centers the view on cursor position before orbiting");
            
            if (ImGui::Checkbox("Smooth Scrolling", &camera.useSmoothScrolling)) {
                preferences.useSmoothScrolling = camera.useSmoothScrolling;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Enable physics-based smooth scrolling");

            if (camera.useSmoothScrolling) {
                if (ImGui::SliderFloat("Momentum", &camera.scrollMomentum, 0.0f, 1.0f)) {
                    preferences.scrollMomentum = camera.scrollMomentum;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Controls how much scrolling 'carries' (higher = more momentum)");

                if (ImGui::SliderFloat("Max Velocity", &camera.maxScrollVelocity, 0.5f, 10.0f)) {
                    preferences.maxScrollVelocity = camera.maxScrollVelocity;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Maximum scroll speed");

                if (ImGui::SliderFloat("Deceleration", &camera.scrollDeceleration, 1.0f, 20.0f)) {
                    preferences.scrollDeceleration = camera.scrollDeceleration;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("How quickly scrolling slows down");
            }
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // SpaceMouse Settings
            ImGui::BeginGroup();
            ImGui::Text("3DConnexion SpaceMouse");
            ImGui::Separator();
            
            if (spaceMouseInitialized) {
                if (ImGui::Checkbox("Enable SpaceMouse", &preferences.spaceMouseEnabled)) {
                    spaceMouseInput.SetEnabled(preferences.spaceMouseEnabled);
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Enable or disable 3DConnexion SpaceMouse input");
                
                if (preferences.spaceMouseEnabled) {
                    if (ImGui::SliderFloat("Deadzone", &preferences.spaceMouseDeadzone, 0.0f, 0.5f)) {
                        spaceMouseInput.SetDeadzone(preferences.spaceMouseDeadzone);
                        settingsChanged = true;
                    }
                    ImGui::SetItemTooltip("Movement threshold below which SpaceMouse input is ignored");
                    
                    if (ImGui::SliderFloat("Translation Sensitivity", &preferences.spaceMouseTranslationSensitivity, 0.1f, 3.0f)) {
                        spaceMouseInput.SetSensitivity(preferences.spaceMouseTranslationSensitivity, preferences.spaceMouseRotationSensitivity);
                        settingsChanged = true;
                    }
                    ImGui::SetItemTooltip("Controls how sensitive translation movements are");
                    
                    if (ImGui::SliderFloat("Rotation Sensitivity", &preferences.spaceMouseRotationSensitivity, 0.1f, 3.0f)) {
                        spaceMouseInput.SetSensitivity(preferences.spaceMouseTranslationSensitivity, preferences.spaceMouseRotationSensitivity);
                        settingsChanged = true;
                    }
                    ImGui::SetItemTooltip("Controls how sensitive rotation movements are");
                    
                    ImGui::Spacing();
                    ImGui::Text("Anchor Point Mode:");
                    
                    int currentMode = static_cast<int>(preferences.spaceMouseAnchorMode);
                    bool modeChanged = false;
                    
                    if (ImGui::RadioButton("Scene Center", &currentMode, static_cast<int>(GUI::SPACEMOUSE_ANCHOR_DISABLED))) {
                        modeChanged = true;
                    }
                    ImGui::SetItemTooltip("Use the scene center as the SpaceMouse pivot point");
                    
                    if (ImGui::RadioButton("Cursor on Start", &currentMode, static_cast<int>(GUI::SPACEMOUSE_ANCHOR_ON_START))) {
                        modeChanged = true;
                    }
                    ImGui::SetItemTooltip("Set anchor to cursor position when SpaceMouse navigation starts, then keep it fixed");
                    
                    if (ImGui::RadioButton("Cursor Continuous", &currentMode, static_cast<int>(GUI::SPACEMOUSE_ANCHOR_CONTINUOUS))) {
                        modeChanged = true;
                    }
                    ImGui::SetItemTooltip("Continuously update anchor to follow the cursor position");
                    
                    if (modeChanged) {
                        preferences.spaceMouseAnchorMode = static_cast<GUI::SpaceMouseAnchorMode>(currentMode);
                        settingsChanged = true;
                        updateSpaceMouseCursorAnchor();
                        spaceMouseInput.RefreshPivotPosition();
                    }
                    
                    ImGui::Spacing();
                    if (ImGui::Checkbox("Center Cursor During Navigation", &preferences.spaceMouseCenterCursor)) {
                        settingsChanged = true;
                    }
                    ImGui::SetItemTooltip("Keep the mouse cursor fixed at the screen center while using SpaceMouse");
                }
            } else {
                ImGui::TextDisabled("SpaceMouse device not detected");
            }
            ImGui::EndGroup();
            
            ImGui::Spacing();
            
            // Keybind Reference (Read-only collapsible section)
            if (ImGui::CollapsingHeader("Keybind Reference")) {
                ImGui::Text("Camera Controls");
                ImGui::Separator();
                ImGui::Columns(2, "keybinds");
                ImGui::SetColumnWidth(0, 150);

                ImGui::Text("W/S"); ImGui::NextColumn();
                ImGui::Text("Move forward/backward"); ImGui::NextColumn();

                ImGui::Text("A/D"); ImGui::NextColumn();
                ImGui::Text("Move left/right"); ImGui::NextColumn();

                ImGui::Text("Space/Shift"); ImGui::NextColumn();
                ImGui::Text("Move up/down"); ImGui::NextColumn();

                ImGui::Text("Left Mouse + Drag"); ImGui::NextColumn();
                ImGui::Text("Orbit around the viewport center at cursor depth"); ImGui::NextColumn();

                ImGui::Text("Right Mouse + Drag"); ImGui::NextColumn();
                ImGui::Text("Rotate the camera"); ImGui::NextColumn();

                ImGui::Text("Middle Mouse + Drag"); ImGui::NextColumn();
                ImGui::Text("Pan camera"); ImGui::NextColumn();

                ImGui::Text("Mouse Wheel"); ImGui::NextColumn();
                ImGui::Text("Zoom in/out"); ImGui::NextColumn();

                ImGui::Text("Double Click"); ImGui::NextColumn();
                ImGui::Text("Center on cursor"); ImGui::NextColumn();

                ImGui::Spacing(); ImGui::NextColumn(); ImGui::Spacing(); ImGui::NextColumn();

                ImGui::Text("Other Controls");
                ImGui::Separator();

                ImGui::Text("G"); ImGui::NextColumn();
                ImGui::Text("Toggle GUI"); ImGui::NextColumn();

                ImGui::Text("Ctrl + Click"); ImGui::NextColumn();
                ImGui::Text("Select object"); ImGui::NextColumn();

                ImGui::Text("Ctrl + Click + Drag"); ImGui::NextColumn();
                ImGui::Text("Move Objects around"); ImGui::NextColumn();

                ImGui::Text("Delete"); ImGui::NextColumn();
                ImGui::Text("Delete selected object"); ImGui::NextColumn();

                ImGui::Text("C"); ImGui::NextColumn();
                ImGui::Text("Center the Scene to the Cursor/Selected Model/Scene Center"); ImGui::NextColumn();

                ImGui::Text("Esc"); ImGui::NextColumn();
                ImGui::Text("Exit application"); ImGui::NextColumn();

                ImGui::Columns(1);
            }

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (settingsChanged) {
        savePreferences();
    }

    ImGui::End();
}

void renderCursorSettingsWindow() {
    // Get cursor instances from manager for easier access
    auto* sphereCursor = cursorManager.getSphereCursor();
    auto* fragmentCursor = cursorManager.getFragmentCursor();
    auto* planeCursor = cursorManager.getPlaneCursor();

    ImGui::SetNextWindowSize(ImVec2(520, 650), ImGuiCond_FirstUseEver);
    ImGui::Begin("3D Cursor Settings", &showCursorSettingsWindow);

    // =====================
    // CURSOR PRESETS
    // =====================
    ImGui::BeginGroup();
    ImGui::Text("Cursor Presets");
    ImGui::Separator();
    
    if (ImGui::BeginCombo("Preset", currentPresetName.c_str())) {
        std::vector<std::string> presetNames = Engine::CursorPresetManager::getPresetNames();

        if (ImGui::Selectable("New Preset")) {
            currentPresetName = "New Preset";
            isEditingPresetName = true;
            strcpy_s(editPresetNameBuffer, currentPresetName.c_str());
        }

        for (const auto& name : presetNames) {
            bool isSelected = (currentPresetName == name);
            if (ImGui::Selectable(name.c_str(), isSelected)) {
                currentPresetName = name;
                try {
                    Engine::CursorPreset loadedPreset = Engine::CursorPresetManager::applyCursorPreset(name);

                    // Apply to cursor manager
                    sphereCursor->setVisible(loadedPreset.showSphereCursor);
                    sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(loadedPreset.sphereScalingMode));
                    sphereCursor->setFixedRadius(loadedPreset.sphereFixedRadius);
                    sphereCursor->setTransparency(loadedPreset.sphereTransparency);
                    sphereCursor->setShowInnerSphere(loadedPreset.showInnerSphere);
                    sphereCursor->setColor(loadedPreset.cursorColor);
                    sphereCursor->setInnerSphereColor(loadedPreset.innerSphereColor);
                    sphereCursor->setInnerSphereFactor(loadedPreset.innerSphereFactor);
                    sphereCursor->setEdgeSoftness(loadedPreset.cursorEdgeSoftness);
                    sphereCursor->setCenterTransparency(loadedPreset.cursorCenterTransparency);

                    fragmentCursor->setVisible(loadedPreset.showFragmentCursor);
                    fragmentCursor->setBaseInnerRadius(loadedPreset.fragmentBaseInnerRadius);

                    planeCursor->setVisible(loadedPreset.showPlaneCursor);
                    planeCursor->setDiameter(loadedPreset.planeDiameter);
                    planeCursor->setColor(loadedPreset.planeColor);

                    // Update orbit center settings if needed
                    bool showOrbitCenter = cursorManager.isShowOrbitCenter();
                    cursorManager.setShowOrbitCenter(showOrbitCenter);

                    preferences.currentPresetName = currentPresetName;
                    savePreferences();
                }
                catch (const std::exception& e) {
                    std::cerr << "Error loading preset: " << e.what() << std::endl;
                }
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }

    // Preset name editing
    if (isEditingPresetName) {
        ImGui::InputText("##EditPresetName", editPresetNameBuffer, IM_ARRAYSIZE(editPresetNameBuffer));

        if (ImGui::Button("Save")) {
            std::string newName = editPresetNameBuffer;
            if (!newName.empty()) {
                if (newName != currentPresetName) {
                    // Create new preset from current settings
                    Engine::CursorPreset newPreset;
                    newPreset.name = newName;

                    // Get settings from cursor manager
                    newPreset.showSphereCursor = sphereCursor->isVisible();
                    newPreset.showFragmentCursor = fragmentCursor->isVisible();
                    newPreset.fragmentBaseInnerRadius = fragmentCursor->getBaseInnerRadius();
                    newPreset.sphereScalingMode = static_cast<int>(sphereCursor->getScalingMode());
                    newPreset.sphereFixedRadius = sphereCursor->getFixedRadius();
                    newPreset.sphereTransparency = sphereCursor->getTransparency();
                    newPreset.showInnerSphere = sphereCursor->getShowInnerSphere();
                    newPreset.cursorColor = sphereCursor->getColor();
                    newPreset.innerSphereColor = sphereCursor->getInnerSphereColor();
                    newPreset.innerSphereFactor = sphereCursor->getInnerSphereFactor();
                    newPreset.cursorEdgeSoftness = sphereCursor->getEdgeSoftness();
                    newPreset.cursorCenterTransparency = sphereCursor->getCenterTransparency();
                    newPreset.showPlaneCursor = planeCursor->isVisible();
                    newPreset.planeDiameter = planeCursor->getDiameter();
                    newPreset.planeColor = planeCursor->getColor();

                    Engine::CursorPresetManager::savePreset(newName, newPreset);
                    if (currentPresetName != "New Preset") {
                        Engine::CursorPresetManager::deletePreset(currentPresetName);
                    }
                    currentPresetName = newName;
                }
                isEditingPresetName = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel")) {
            isEditingPresetName = false;
            if (currentPresetName == "New Preset") {
                currentPresetName = Engine::CursorPresetManager::getPresetNames().front();
            }
        }
    }
    else {
        if (ImGui::Button("Rename Preset")) {
            isEditingPresetName = true;
            strcpy_s(editPresetNameBuffer, currentPresetName.c_str());
        }
    }
    
    // Preset management buttons
    ImGui::Spacing();
    ImGui::Text("Preset Management");
    
    if (ImGui::Button("Update Preset")) {
        // Create preset from current cursor manager settings
        Engine::CursorPreset updatedPreset;
        updatedPreset.name = currentPresetName;

        // Get settings from cursor manager
        updatedPreset.showSphereCursor = sphereCursor->isVisible();
        updatedPreset.showFragmentCursor = fragmentCursor->isVisible();
        updatedPreset.fragmentBaseInnerRadius = fragmentCursor->getBaseInnerRadius();
        updatedPreset.sphereScalingMode = static_cast<int>(sphereCursor->getScalingMode());
        updatedPreset.sphereFixedRadius = sphereCursor->getFixedRadius();
        updatedPreset.sphereTransparency = sphereCursor->getTransparency();
        updatedPreset.showInnerSphere = sphereCursor->getShowInnerSphere();
        updatedPreset.cursorColor = sphereCursor->getColor();
        updatedPreset.innerSphereColor = sphereCursor->getInnerSphereColor();
        updatedPreset.innerSphereFactor = sphereCursor->getInnerSphereFactor();
        updatedPreset.cursorEdgeSoftness = sphereCursor->getEdgeSoftness();
        updatedPreset.cursorCenterTransparency = sphereCursor->getCenterTransparency();
        updatedPreset.showPlaneCursor = planeCursor->isVisible();
        updatedPreset.planeDiameter = planeCursor->getDiameter();
        updatedPreset.planeColor = planeCursor->getColor();

        Engine::CursorPresetManager::savePreset(currentPresetName, updatedPreset);
    }

    ImGui::SameLine();
    if (ImGui::Button("Delete Preset")) {
        if (currentPresetName != "Default") {
            Engine::CursorPresetManager::deletePreset(currentPresetName);
            std::vector<std::string> remainingPresets = Engine::CursorPresetManager::getPresetNames();
            if (!remainingPresets.empty()) {
                currentPresetName = remainingPresets.front();
                Engine::CursorPreset loadedPreset = Engine::CursorPresetManager::applyCursorPreset(currentPresetName);

                // Apply to cursor manager
                sphereCursor->setVisible(loadedPreset.showSphereCursor);
                sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(loadedPreset.sphereScalingMode));
                sphereCursor->setFixedRadius(loadedPreset.sphereFixedRadius);
                sphereCursor->setTransparency(loadedPreset.sphereTransparency);
                sphereCursor->setShowInnerSphere(loadedPreset.showInnerSphere);
                sphereCursor->setColor(loadedPreset.cursorColor);
                sphereCursor->setInnerSphereColor(loadedPreset.innerSphereColor);
                sphereCursor->setInnerSphereFactor(loadedPreset.innerSphereFactor);
                sphereCursor->setEdgeSoftness(loadedPreset.cursorEdgeSoftness);
                sphereCursor->setCenterTransparency(loadedPreset.cursorCenterTransparency);

                fragmentCursor->setVisible(loadedPreset.showFragmentCursor);
                fragmentCursor->setBaseInnerRadius(loadedPreset.fragmentBaseInnerRadius);

                planeCursor->setVisible(loadedPreset.showPlaneCursor);
                planeCursor->setDiameter(loadedPreset.planeDiameter);
                planeCursor->setColor(loadedPreset.planeColor);
            }
            else {
                currentPresetName = "Default";
                // Reset to default settings
                // Setup default cursor settings
                sphereCursor->setVisible(true);
                sphereCursor->setScalingMode(GUI::CURSOR_CONSTRAINED_DYNAMIC);
                sphereCursor->setFixedRadius(0.05f);
                sphereCursor->setTransparency(0.7f);
                sphereCursor->setShowInnerSphere(false);
                sphereCursor->setColor(glm::vec4(1.0f, 0.0f, 0.0f, 0.7f));
                sphereCursor->setInnerSphereColor(glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));
                sphereCursor->setInnerSphereFactor(0.1f);
                sphereCursor->setEdgeSoftness(0.8f);
                sphereCursor->setCenterTransparency(0.2f);

                fragmentCursor->setVisible(true);
                GUI::FragmentShaderCursorSettings fragSettings;
                fragmentCursor->setSettings(fragSettings);

                planeCursor->setVisible(false);
                planeCursor->setDiameter(0.5f);
                planeCursor->setColor(glm::vec4(0.0f, 1.0f, 0.0f, 0.7f));

                cursorManager.setShowOrbitCenter(false);
                cursorManager.setOrbitCenterColor(glm::vec4(0.0f, 1.0f, 0.0f, 0.7f));
                cursorManager.setOrbitCenterSphereRadius(0.2f);
            }
        }
    }
    ImGui::EndGroup();

    ImGui::Spacing();

    // =====================
    // ORBIT VISUALIZATION
    // =====================
    ImGui::BeginGroup();
    ImGui::Text("Orbit Visualization");
    ImGui::Separator();
    
    ImGui::Text("Camera Orbit Behavior");
    bool standardOrbit = !orbitFollowsCursor && !camera.orbitAroundCursor;
    bool orbitAroundCursorOption = camera.orbitAroundCursor;
    bool orbitFollowsCursorOption = orbitFollowsCursor;

    if (ImGui::RadioButton("Standard Orbit", standardOrbit)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = false;
        savePreferences();
    }
    ImGui::SetItemTooltip("Orbits around the viewport center at cursor depth");

    if (ImGui::RadioButton("Orbit Around Cursor", orbitAroundCursorOption)) {
        camera.orbitAroundCursor = true;
        orbitFollowsCursor = false;
        preferences.orbitAroundCursor = true;
        preferences.orbitFollowsCursor = false;
        savePreferences();
    }
    ImGui::SetItemTooltip("Orbits around the 3D position of the cursor without centering the view");

    if (ImGui::RadioButton("Orbit Follows Cursor (Center)", orbitFollowsCursorOption)) {
        camera.orbitAroundCursor = false;
        orbitFollowsCursor = true;
        preferences.orbitAroundCursor = false;
        preferences.orbitFollowsCursor = true;
        savePreferences();
    }
    ImGui::SetItemTooltip("Centers the view on cursor position before orbiting");
    
    bool showOrbitCenter = cursorManager.isShowOrbitCenter();
    if (ImGui::Checkbox("Show Orbit Center", &showOrbitCenter)) {
        cursorManager.setShowOrbitCenter(showOrbitCenter);
        savePreferences();
    }
    ImGui::SetItemTooltip("Display a visual indicator at the orbit center point");

    if (showOrbitCenter) {
        glm::vec4 orbitCenterColor = cursorManager.getOrbitCenterColor();
        if (ImGui::ColorEdit3("Orbit Center Color", glm::value_ptr(orbitCenterColor))) {
            cursorManager.setOrbitCenterColor(orbitCenterColor);
            savePreferences();
        }

        float orbitCenterSize = cursorManager.getOrbitCenterSphereRadius();
        if (ImGui::SliderFloat("Orbit Center Size", &orbitCenterSize, 0.01f, 1.0f)) {
            cursorManager.setOrbitCenterSphereRadius(orbitCenterSize);
            savePreferences();
        }
    }
    ImGui::EndGroup();

    ImGui::Spacing();

    // =====================
    // 3D SPHERE CURSOR
    // =====================
    if (ImGui::CollapsingHeader("3D Sphere Cursor", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool sphereVisible = sphereCursor->isVisible();
        if (ImGui::Checkbox("Show 3D Sphere Cursor", &sphereVisible)) {
            sphereCursor->setVisible(sphereVisible);
        }
        ImGui::SetItemTooltip("Display a 3D sphere at the cursor position");

        if (sphereVisible) {
            // Cursor Scaling Mode
            ImGui::Text("Scaling Mode");
            const char* scalingModes[] = { "Fixed Size", "Depth-based" };
            int currentMode = static_cast<int>(sphereCursor->getScalingMode());
            if (ImGui::Combo("Scaling Mode", &currentMode, scalingModes, IM_ARRAYSIZE(scalingModes))) {
                sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(currentMode));
            }
            ImGui::SetItemTooltip("Fixed: constant size, Depth-based: size varies with distance");

            // Size controls
            if (currentMode == static_cast<int>(GUI::CursorScalingMode::CURSOR_FIXED)) {
                float fixedRadius = sphereCursor->getFixedRadius();
                if (ImGui::SliderFloat("Fixed Sphere Radius", &fixedRadius, 0.01f, 3.0f)) {
                    sphereCursor->setFixedRadius(fixedRadius);
                }
            } else {
                float minDifference = sphereCursor->getMinDiff();
                if (ImGui::SliderFloat("Min Difference", &minDifference, 0.01f, 2.0f)) {
                    sphereCursor->setMinDiff(minDifference);
                }

                float maxDifference = sphereCursor->getMaxDiff();
                if (ImGui::SliderFloat("Max Difference", &maxDifference, 0.02f, 5.0f)) {
                    sphereCursor->setMaxDiff(maxDifference);
                }
            }

            ImGui::Separator();
            
            // Appearance
            ImGui::Text("Appearance");
            glm::vec4 cursorColor = sphereCursor->getColor();
            if (ImGui::ColorEdit3("Cursor Color", glm::value_ptr(cursorColor))) {
                sphereCursor->setColor(cursorColor);
            }

            float transparency = sphereCursor->getTransparency();
            if (ImGui::SliderFloat("Cursor Transparency", &transparency, 0.0f, 1.0f)) {
                sphereCursor->setTransparency(transparency);
            }

            float edgeSoftness = sphereCursor->getEdgeSoftness();
            if (ImGui::SliderFloat("Edge Softness", &edgeSoftness, 0.0f, 1.0f)) {
                sphereCursor->setEdgeSoftness(edgeSoftness);
            }

            float centerTransparency = sphereCursor->getCenterTransparency();
            if (ImGui::SliderFloat("Center Transparency", &centerTransparency, 0.0f, 1.0f)) {
                sphereCursor->setCenterTransparency(centerTransparency);
            }

            ImGui::Separator();
            
            // Inner Sphere
            ImGui::Text("Inner Sphere");
            bool showInnerSphere = sphereCursor->getShowInnerSphere();
            if (ImGui::Checkbox("Show Inner Sphere", &showInnerSphere)) {
                sphereCursor->setShowInnerSphere(showInnerSphere);
            }

            if (showInnerSphere) {
                glm::vec4 innerSphereColor = sphereCursor->getInnerSphereColor();
                if (ImGui::ColorEdit3("Inner Sphere Color", glm::value_ptr(innerSphereColor))) {
                    sphereCursor->setInnerSphereColor(innerSphereColor);
                }

                float innerSphereFactor = sphereCursor->getInnerSphereFactor();
                if (ImGui::SliderFloat("Inner Sphere Factor", &innerSphereFactor, 0.1f, 0.9f)) {
                    sphereCursor->setInnerSphereFactor(innerSphereFactor);
                }
            }
        }
    }

    // ============================
    // FRAGMENT SHADER CURSOR
    // ============================
    if (ImGui::CollapsingHeader("Fragment Shader Cursor", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool fragmentVisible = fragmentCursor->isVisible();
        if (ImGui::Checkbox("Show Fragment Shader Cursor", &fragmentVisible)) {
            fragmentCursor->setVisible(fragmentVisible);
        }
        ImGui::SetItemTooltip("Display a 2D circular cursor rendered in the fragment shader");

        if (fragmentVisible) {
            float outerRadius = fragmentCursor->getBaseOuterRadius();
            if (ImGui::SliderFloat("Outer Radius", &outerRadius, 0.0f, 0.3f)) {
                fragmentCursor->setBaseOuterRadius(outerRadius);
            }

            float outerBorderThickness = fragmentCursor->getBaseOuterBorderThickness();
            if (ImGui::SliderFloat("Outer Border Thickness", &outerBorderThickness, 0.0f, 0.08f)) {
                fragmentCursor->setBaseOuterBorderThickness(outerBorderThickness);
            }

            float innerRadius = fragmentCursor->getBaseInnerRadius();
            if (ImGui::SliderFloat("Inner Radius", &innerRadius, 0.0f, 0.2f)) {
                fragmentCursor->setBaseInnerRadius(innerRadius);
            }

            float innerBorderThickness = fragmentCursor->getBaseInnerBorderThickness();
            if (ImGui::SliderFloat("Inner Border Thickness", &innerBorderThickness, 0.0f, 0.08f)) {
                fragmentCursor->setBaseInnerBorderThickness(innerBorderThickness);
            }

            glm::vec4 outerColor = fragmentCursor->getOuterColor();
            if (ImGui::ColorEdit4("Outer Color", glm::value_ptr(outerColor))) {
                fragmentCursor->setOuterColor(outerColor);
            }

            glm::vec4 innerColor = fragmentCursor->getInnerColor();
            if (ImGui::ColorEdit4("Inner Color", glm::value_ptr(innerColor))) {
                fragmentCursor->setInnerColor(innerColor);
            }
        }
    }

    // ==================
    // PLANE CURSOR
    // ==================
    if (ImGui::CollapsingHeader("Plane Cursor", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool planeVisible = planeCursor->isVisible();
        if (ImGui::Checkbox("Show Plane Cursor", &planeVisible)) {
            planeCursor->setVisible(planeVisible);
        }
        ImGui::SetItemTooltip("Display a plane that follows surface geometry");

        if (planeVisible) {
            glm::vec4 planeColor = planeCursor->getColor();
            if (ImGui::ColorEdit3("Plane Color", glm::value_ptr(planeColor))) {
                planeCursor->setColor(planeColor);
            }

            float planeDiameter = planeCursor->getDiameter();
            if (ImGui::SliderFloat("Plane Diameter", &planeDiameter, 0.1f, 5.0f)) {
                planeCursor->setDiameter(planeDiameter);
            }
        }
    }

    ImGui::End();
}
            

void renderSunManipulationPanel() {
    ImGui::Text("Sun Settings");
    ImGui::Separator();

    // Direction control using angles
    static glm::vec3 angles = glm::vec3(-45.0f, -45.0f, 0.0f);
    if (ImGui::DragFloat3("Direction (Angles)", glm::value_ptr(angles), 1.0f, -180.0f, 180.0f)) {
        // Convert angles to direction vector
        glm::mat4 rotationMatrix = glm::mat4(1.0f);
        rotationMatrix = glm::rotate(rotationMatrix, glm::radians(angles.x), glm::vec3(1, 0, 0));
        rotationMatrix = glm::rotate(rotationMatrix, glm::radians(angles.y), glm::vec3(0, 1, 0));
        rotationMatrix = glm::rotate(rotationMatrix, glm::radians(angles.z), glm::vec3(0, 0, 1));

        sun.direction = glm::normalize(glm::vec3(rotationMatrix * glm::vec4(0, -1, 0, 0)));
    }

    ImGui::ColorEdit3("Color", glm::value_ptr(sun.color));
    ImGui::DragFloat("Intensity", &sun.intensity, 0.01f, 0.0f, 10.0f);

    ImGui::Text("Direction Vector: (%.2f, %.2f, %.2f)",
        sun.direction.x, sun.direction.y, sun.direction.z);
}

void renderModelManipulationPanel(Engine::Model& model, Engine::Shader* shader) {
    ImGui::Text("Model Manipulation: %s", model.name.c_str());
    ImGui::Separator();

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool transformChanged = false;
        transformChanged |= ImGui::DragFloat3("Position", glm::value_ptr(model.position), 0.1f);
        transformChanged |= ImGui::DragFloat3("Scale", glm::value_ptr(model.scale), 0.01f, 0.01f, 100.0f);
        transformChanged |= ImGui::DragFloat3("Rotation", glm::value_ptr(model.rotation), 1.0f, -360.0f, 360.0f);
        
        // Update SpaceMouse bounds only when transform changes
        if (transformChanged) {
            updateSpaceMouseBounds();
        }
    }

    // Material
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        // Basic material properties
        ImGui::ColorEdit3("Diffuse Color", glm::value_ptr(model.color));
        ImGui::SliderFloat("Shininess", &model.shininess, 1.0f, 90.0f);
        ImGui::SliderFloat("Emissive", &model.emissive, 0.0f, 1.0f);

        // Add VCT specific material properties
        ImGui::Separator();
        ImGui::Text("Voxel Cone Tracing Properties:");

        // Material preset selection
        static const char* material_types[] = { "Concrete", "Metal", "Plastic", "Glass", "Wood", "Marble", "Custom" };
        int current_type = static_cast<int>(model.materialType);

        if (ImGui::Combo("Material Preset", &current_type, material_types, IM_ARRAYSIZE(material_types))) {
            // Apply preset when changed
            model.applyMaterialPreset(static_cast<MaterialType>(current_type));
        }
        ImGui::SetItemTooltip("Select a material preset to quickly configure material properties");

        if (ImGui::Button("Apply Concrete (Default)")) {
            model.applyMaterialPreset(MaterialType::CONCRETE);
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply Metal")) {
            model.applyMaterialPreset(MaterialType::METAL);
        }
        ImGui::SameLine();
        if (ImGui::Button("Apply Glass")) {
            model.applyMaterialPreset(MaterialType::GLASS);
        }

        ImGui::SliderFloat("Diffuse Reflectivity", &model.diffuseReflectivity, 0.0f, 1.0f);
        ImGui::SetItemTooltip("Controls how much diffuse light is reflected");

        ImGui::ColorEdit3("Specular Color", glm::value_ptr(model.specularColor));
        ImGui::SetItemTooltip("Color of specular reflections (highlights)");

        ImGui::SliderFloat("Specular Reflectivity", &model.specularReflectivity, 0.0f, 1.0f);
        ImGui::SetItemTooltip("Controls strength of specular reflections");

        ImGui::SliderFloat("Specular Diffusion", &model.specularDiffusion, 0.0f, 1.0f);
        ImGui::SetItemTooltip("Controls glossiness - lower values = sharper reflections");

        ImGui::SliderFloat("Refractive Index", &model.refractiveIndex, 1.0f, 3.0f);
        ImGui::SetItemTooltip("Refractive index (1.0=air, 1.33=water, 1.5=glass, 2.4=diamond)");

        ImGui::SliderFloat("Transparency", &model.transparency, 0.0f, 1.0f);
        ImGui::SetItemTooltip("0=opaque, 1=fully transparent");
    }

    // Textures
    if (ImGui::CollapsingHeader("Textures")) {
        // Display a list of loaded textures
        if (!model.getMeshes().empty()) {
            const auto& mesh = model.getMeshes()[0];
            ImGui::Text("Loaded Textures:");
            for (const auto& texture : mesh.textures) {
                ImGui::BulletText("%s: %s", texture.type.c_str(), texture.path.c_str());
            }
        }

        // Texture loading interface
        auto textureLoadingGUI = [&](const char* label, const char* type) {
            if (ImGui::Button(("Load " + std::string(label)).c_str())) {
                auto selection = pfd::open_file("Select a texture file", ".",
                    { "Image Files", "*.png *.jpg *.jpeg *.bmp", "All Files", "*" }).result();
                if (!selection.empty()) {
                    // Create and load new texture
                    Texture texture;
                    texture.id = model.TextureFromFile(selection[0].c_str(), selection[0], selection[0]);
                    texture.type = type;
                    texture.path = selection[0];

                    // Add to all meshes
                    for (auto& mesh : model.getMeshes()) {
                        mesh.textures.push_back(texture);
                    }
                }
            }
            };

        textureLoadingGUI("Diffuse Texture", "texture_diffuse");
        textureLoadingGUI("Normal Map", "texture_normal");
        textureLoadingGUI("Specular Map", "texture_specular");
        textureLoadingGUI("AO Map", "texture_ao");
    }

    ImGui::Separator();

    // Delete button
    if (ImGui::Button("Delete Model", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Delete Model?");
    }

    // Confirmation popup
    if (ImGui::BeginPopupModal("Delete Model?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to delete this model?\nThis operation cannot be undone!\n\n");
        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            deleteSelectedModel();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void renderMeshManipulationPanel(Engine::Model& model, int meshIndex, Engine::Shader* shader) {
    auto& mesh = model.getMeshes()[meshIndex];

    ImGui::Text("Mesh Manipulation: %s - Mesh %d", model.name.c_str(), meshIndex + 1);
    ImGui::Separator();

    ImGui::Checkbox("Visible", &mesh.visible);

    // Material properties specific to this mesh
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", glm::value_ptr(mesh.color));
        ImGui::SliderFloat("Shininess", &mesh.shininess, 1.0f, 90.0f);
        ImGui::SliderFloat("Emissive", &mesh.emissive, 0.0f, 1.0f);

        // Add VCT specific material properties for mesh
        ImGui::Separator();
        ImGui::Text("Voxel Cone Tracing Properties: Currently only per Model");
    }

    // Texture management for this specific mesh
    if (ImGui::CollapsingHeader("Textures")) {
        ImGui::Text("Loaded Textures:");
        for (const auto& texture : mesh.textures) {
            ImGui::BulletText("%s: %s", texture.type.c_str(), texture.path.c_str());
        }

        // Texture loading buttons
        auto textureLoadingGUI = [&](const char* label, const char* type) {
            if (ImGui::Button(("Load " + std::string(label)).c_str())) {
                auto selection = pfd::open_file("Select a texture file", ".",
                    { "Image Files", "*.png *.jpg *.jpeg *.bmp", "All Files", "*" }).result();
                if (!selection.empty()) {
                    // Create and load new texture
                    Texture texture;
                    texture.id = model.TextureFromFile(selection[0].c_str(), selection[0], selection[0]);
                    texture.type = type;
                    texture.path = selection[0];

                    // Add to this specific mesh
                    mesh.textures.push_back(texture);
                }
            }
            };

        textureLoadingGUI("Diffuse Texture", "texture_diffuse");
        textureLoadingGUI("Normal Map", "texture_normal");
        textureLoadingGUI("Specular Map", "texture_specular");
        textureLoadingGUI("AO Map", "texture_ao");
    }

    // Transform for individual mesh (if needed)
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Transform controls could be added here");
        // Note: Implementing individual mesh transforms would require
        // additional modifications to the mesh rendering system
    }

    ImGui::Separator();

    // Delete mesh button
    if (ImGui::Button("Delete Mesh", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Delete Mesh?");
    }

    if (ImGui::BeginPopupModal("Delete Mesh?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to delete this mesh?\nThis operation cannot be undone!\n\n");
        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            // Only delete if there's more than one mesh (to avoid empty models)
            if (model.getMeshes().size() > 1) {
                model.getMeshes().erase(model.getMeshes().begin() + meshIndex);
                currentSelectedMeshIndex = -1;  // Reset mesh selection
            }
            else {
                // If this is the last mesh, maybe delete the whole model
                std::cout << "Cannot delete last mesh. Delete the entire model instead." << std::endl;
            }
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void renderPointCloudManipulationPanel(Engine::PointCloud& pointCloud) {
    ImGui::Text("Point Cloud Manipulation: %s", pointCloud.name.c_str());
    // Check if point cloud has data (either in points vector or octree)
    bool hasData = !pointCloud.points.empty() || pointCloud.octreeRoot;
    if (!hasData) {
        ImGui::Text("Point cloud is empty");
        return;
    }
    ImGui::Separator();

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool transformChanged = false;
        transformChanged |= ImGui::DragFloat3("Position", glm::value_ptr(pointCloud.position), 0.1f);
        transformChanged |= ImGui::DragFloat3("Rotation", glm::value_ptr(pointCloud.rotation), 1.0f, -360.0f, 360.0f);
        transformChanged |= ImGui::DragFloat3("Scale", glm::value_ptr(pointCloud.scale), 0.01f, 0.01f, 100.0f);
        
        // Update SpaceMouse bounds only when transform changes
        if (transformChanged) {
            updateSpaceMouseBounds();
        }
    }

    // Point Cloud specific settings
    if (ImGui::CollapsingHeader("Point Cloud Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Base Point Size", &pointCloud.basePointSize, 1.0f, 10.0f);
    }

    if (ImGui::CollapsingHeader("LOD Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("LOD Distance 1", &pointCloud.lodDistances[0], 1.0f, 15.0f);
        ImGui::SliderFloat("LOD Distance 2", &pointCloud.lodDistances[1], 10.0f, 30.0f);
        ImGui::SliderFloat("LOD Distance 3", &pointCloud.lodDistances[2], 15.0f, 40.0f);
        ImGui::SliderFloat("LOD Distance 4", &pointCloud.lodDistances[3], 20.0f, 50.0f);
        ImGui::SliderFloat("LOD Distance 5", &pointCloud.lodDistances[4], 25.0f, 60.0f);

        ImGui::SliderFloat("Chunk Size", &pointCloud.newChunkSize, 1.0f, 50.0f);

        if (ImGui::Button("Recalculate Chunks")) {
            if (pointCloud.newChunkSize != pointCloud.chunkSize) {
                pointCloud.chunkSize = pointCloud.newChunkSize;

                Engine::generateChunks(pointCloud, pointCloud.chunkSize);
            }
        }

        extern std::atomic<bool> isRecalculatingChunks;
        if (isRecalculatingChunks.load()) {
            ImGui::SameLine();
            ImGui::Text("Recalculating chunks...");
        }

        ImGui::Checkbox("Visualize Chunks", &pointCloud.visualizeChunks);
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Export", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Export Point Cloud")) {
            ImGui::OpenPopup("Export Point Cloud");
        }

        if (ImGui::BeginPopupModal("Export Point Cloud", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
            static int exportFormat = 0;
            ImGui::RadioButton("XYZ", &exportFormat, 0);
            ImGui::RadioButton("Optimized Binary", &exportFormat, 1);

            if (ImGui::Button("Export")) {
                std::string defaultExt = (exportFormat == 0) ? ".xyz" : ".pcb";
                auto destination = pfd::save_file("Select a file to export point cloud", ".",
                    { "Point Cloud Files", "*" + defaultExt, "All Files", "*" }).result();

                if (!destination.empty()) {
                    bool success = false;
                    if (exportFormat == 0) {
                        success = Engine::PointCloudLoader::exportToXYZ(pointCloud, destination);
                    }
                    else {
                        success = Engine::PointCloudLoader::exportToBinary(pointCloud, destination);
                    }

                    if (success) {
                        std::cout << "Point cloud exported successfully to " << destination << std::endl;
                    }
                    else {
                        std::cerr << "Failed to export point cloud to " << destination << std::endl;
                    }
                }
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel")) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
    }

    ImGui::Separator();

    // Delete button
    if (ImGui::Button("Delete Point Cloud", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Delete Point Cloud?");
    }

    // Confirmation popup
    if (ImGui::BeginPopupModal("Delete Point Cloud?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Are you sure you want to delete this point cloud?\nThis operation cannot be undone!\n\n");
        ImGui::Separator();

        if (ImGui::Button("Yes", ImVec2(120, 0))) {
            deleteSelectedPointCloud();
            ImGui::CloseCurrentPopup();
        }
        ImGui::SetItemDefaultFocus();
        ImGui::SameLine();
        if (ImGui::Button("No", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void deleteSelectedModel() {
    if (currentSelectedType == SelectedType::Model && currentSelectedIndex >= 0 && currentSelectedIndex < currentScene.models.size()) {
        currentScene.models.erase(currentScene.models.begin() + currentSelectedIndex);
        currentSelectedIndex = -1;
        currentSelectedType = SelectedType::None;
        updateSpaceMouseBounds();
    }
}

void deleteSelectedPointCloud() {
    if (currentSelectedType == SelectedType::PointCloud && currentSelectedIndex >= 0 && currentSelectedIndex < currentScene.pointClouds.size()) {
        // Clean up OpenGL resources
        glDeleteVertexArrays(1, &currentScene.pointClouds[currentSelectedIndex].vao);
        glDeleteBuffers(1, &currentScene.pointClouds[currentSelectedIndex].vbo);

        // Remove from the vector
        currentScene.pointClouds.erase(currentScene.pointClouds.begin() + currentSelectedIndex);
        currentSelectedIndex = -1;
        currentSelectedType = SelectedType::None;
        updateSpaceMouseBounds();
    }
}

// Stereo visualization removed - not working correctly
/*
void renderStereoCameraVisualization(const Camera& camera, const Engine::SceneSettings& settings) {
    // Canvas setup
    ImVec2 canvasPos = ImGui::GetCursorScreenPos();
    ImVec2 contentSize = ImGui::GetContentRegionAvail();
    float canvasSize = std::min(contentSize.x, 300.0f);
    canvasSize = std::max(canvasSize, 50.0f);
    ImVec2 squareSize(canvasSize, canvasSize);
    ImVec2 canvasBottomRight = ImVec2(canvasPos.x + squareSize.x, canvasPos.y + squareSize.y);
    ImDrawList* drawList = ImGui::GetWindowDrawList();

    // Background
    drawList->AddRectFilled(canvasPos, canvasBottomRight, IM_COL32(20, 20, 25, 255));
    drawList->AddRect(canvasPos, canvasBottomRight, IM_COL32(100, 100, 100, 255));

    // Coordinate system
    float margin = canvasSize * 0.05f;
    float drawingWidth = squareSize.x - 2 * margin;
    float drawingHeight = squareSize.y - 2 * margin;
    ImVec2 drawingTopLeft = ImVec2(canvasPos.x + margin, canvasPos.y + margin);

    // World parameters
    float separation = settings.separation;
    float convergence = settings.convergence;
    convergence = std::max(0.01f, convergence);
    float fovDeg = camera.Zoom;
    float fovRad = glm::radians(fovDeg);
    float halfFovRad = fovRad / 2.0f;
    float halfSeparation = separation / 2.0f;

    // Determine world bounds for scaling
    float maxReach = convergence * 1.2f;
    float worldViewHeight = separation * 1.5f;
    if (convergence > 0.01f) {
        float centerAngle = atan2(halfSeparation, convergence);
        float outerAngle = centerAngle + halfFovRad;
        float maxYEdge = halfSeparation + tan(outerAngle) * maxReach;
        worldViewHeight = std::max(worldViewHeight, std::abs(maxYEdge) * 2.5f);
    }
    worldViewHeight = std::max(worldViewHeight, separation * 1.5f);
    worldViewHeight = std::max(worldViewHeight, 0.1f);

    // Scaling
    float scaleX = drawingWidth / maxReach;
    float scaleY = drawingHeight / worldViewHeight;
    float scale = std::min(scaleX, scaleY);

    // Origin
    float originX = drawingTopLeft.x;
    float originY = drawingTopLeft.y + drawingHeight / 2.0f;

    // World to screen coordinate conversion
    auto worldToScreen = [&](float worldX, float worldY) -> ImVec2 {
        return ImVec2(originX + worldX * scale, originY - worldY * scale);
        };

    // Camera positions
    float leftCamY = halfSeparation;
    float rightCamY = -halfSeparation;

    // Screen positions
    ImVec2 leftCameraPosScreen = worldToScreen(0.0f, leftCamY);
    ImVec2 rightCameraPosScreen = worldToScreen(0.0f, rightCamY);

    // Calculate frustum angles
    float angleLeftCenter = atan2(-leftCamY, convergence);
    float angleRightCenter = atan2(-rightCamY, convergence);

    float angleLeftOuter = angleLeftCenter + halfFovRad;
    float angleLeftInner = angleLeftCenter - halfFovRad;
    float angleRightInner = angleRightCenter + halfFovRad;
    float angleRightOuter = angleRightCenter - halfFovRad;

    // Calculate intersection points at convergence plane
    float yLeftOuterConv = leftCamY + tan(angleLeftOuter) * convergence;
    float yLeftInnerConv = leftCamY + tan(angleLeftInner) * convergence;
    float yRightInnerConv = rightCamY + tan(angleRightInner) * convergence;
    float yRightOuterConv = rightCamY + tan(angleRightOuter) * convergence;

    // Calculate far points for drawing
    float farDist = maxReach * 0.98f;
    float yLeftOuterFar = leftCamY + tan(angleLeftOuter) * farDist;
    float yLeftInnerFar = leftCamY + tan(angleLeftInner) * farDist;
    float yRightInnerFar = rightCamY + tan(angleRightInner) * farDist;
    float yRightOuterFar = rightCamY + tan(angleRightOuter) * farDist;

    // Convert to screen coordinates
    ImVec2 leftFarOuter = worldToScreen(farDist, yLeftOuterFar);
    ImVec2 leftFarInner = worldToScreen(farDist, yLeftInnerFar);
    ImVec2 rightFarInner = worldToScreen(farDist, yRightInnerFar);
    ImVec2 rightFarOuter = worldToScreen(farDist, yRightOuterFar);

    // Points on convergence plane - UPDATED
    ImVec2 leftConvOuter = worldToScreen(convergence, yLeftOuterConv);
    ImVec2 rightConvOuter = worldToScreen(convergence, yRightOuterConv);

    // Define colors for visualization
    ImU32 colorLeftFill = IM_COL32(0, 100, 255, 50);
    ImU32 colorLeftLine = IM_COL32(100, 150, 255, 180);
    ImU32 colorRightFill = IM_COL32(210, 80, 80, 50);
    ImU32 colorRightLine = IM_COL32(255, 130, 130, 180);
    ImU32 colorOverlapFill = IM_COL32(150, 100, 255, 60);
    ImU32 colorFocus = IM_COL32(255, 50, 50, 255);

    // Left Camera Frustum
    drawList->AddTriangleFilled(leftCameraPosScreen, leftFarOuter, leftFarInner, colorLeftFill);
    drawList->AddLine(leftCameraPosScreen, leftFarOuter, colorLeftLine, 1.0f);
    drawList->AddLine(leftCameraPosScreen, leftFarInner, colorLeftLine, 1.0f);

    // Right Camera Frustum
    drawList->AddTriangleFilled(rightCameraPosScreen, rightFarInner, rightFarOuter, colorRightFill);
    drawList->AddLine(rightCameraPosScreen, rightFarInner, colorRightLine, 1.0f);
    drawList->AddLine(rightCameraPosScreen, rightFarOuter, colorRightLine, 1.0f);

    // Overlap Area - REMOVED

    // Convergence line - UPDATED
    drawList->AddLine(leftConvOuter, rightConvOuter, colorFocus, 2.5f);

    // Camera markers
    float cameraMarkerRadius = 4.0f;
    drawList->AddCircleFilled(leftCameraPosScreen, cameraMarkerRadius, colorLeftLine);
    drawList->AddCircleFilled(rightCameraPosScreen, cameraMarkerRadius, colorRightLine);

    // Reserve space for drawing
    ImGui::Dummy(squareSize);
}
*/