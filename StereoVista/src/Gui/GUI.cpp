#include "Gui/Gui.h"
#include "Gui/GUITypes.h"
#include "Engine/Core.h"
#include "Engine/BVHDebug.h"
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
#include "imgui/IconsFontAwesome5.h"
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
extern bool showBVHDebug;
extern Engine::BVHDebugRenderer bvhDebugRenderer;

// Selection state for object interaction
extern enum class SelectedType {
    None,
    Model,
    PointCloud,
    Sun,
    PointLight,
    SpotLight
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

// External function declarations
extern void savePreferences();
extern void updateSkybox();

// Constants
extern const int MAX_LIGHTS;

// Helper function for section headers in ImGui
static void DrawSectionHeader(const char* label) {
    ImGui::Spacing();
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    ImGui::Text("%s", label);
    ImGui::PopStyleColor();
    ImGui::Separator();
}

// Helper function for inline help text
static void DrawHelpMarker(const char* desc) {
    ImGui::TextDisabled("(?)");
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        ImGui::PushTextWrapPos(ImGui::GetFontSize() * 35.0f);
        ImGui::TextUnformatted(desc);
        ImGui::PopTextWrapPos();
        ImGui::EndTooltip();
    }
}

// Comprehensive icon test window
static void ShowIconTestWindow() {
    ImGui::Begin("Icon Test Window", nullptr, ImGuiWindowFlags_AlwaysAutoResize);

    ImGui::Text("=== FontAwesome Icon Tests ===");

    // Test 1: Direct icon display with current font
    ImGui::Separator();
    ImGui::Text("Test 1: Current font");
    ImGui::Text("Lightbulb: %s", ICON_FA_LIGHTBULB);
    ImGui::Text("Star: %s", ICON_FA_STAR);
    ImGui::Text("Home: %s", ICON_FA_HOME);
    ImGui::Text("Cog: %s", ICON_FA_COG);
    ImGui::Text("Heart: %s", ICON_FA_HEART);

    // Test 2: With explicit regular font
    ImGui::Separator();
    ImGui::Text("Test 2: With regular font");
    if (g_Fonts.regular) {
        ImGui::PushFont(g_Fonts.regular);
        ImGui::Text("Lightbulb: %s", ICON_FA_LIGHTBULB);
        ImGui::Text("Star: %s", ICON_FA_STAR);
        ImGui::Text("Home: %s", ICON_FA_HOME);
        ImGui::Text("Cog: %s", ICON_FA_COG);
        ImGui::Text("Heart: %s", ICON_FA_HEART);
        ImGui::PopFont();
    } else {
        ImGui::Text("Regular font not available");
    }

    // Test 3: Raw UTF-8 sequences
    ImGui::Separator();
    ImGui::Text("Test 3: Raw UTF-8 sequences");
    ImGui::Text("Lightbulb raw: \uf0eb");
    ImGui::Text("Star raw: \uf005");
    ImGui::Text("Home raw: \uf015");

    // Test 4: Different character codes
    ImGui::Separator();
    ImGui::Text("Test 4: Character codes");
    char lightbulb_utf8[5];
    ImTextCharToUtf8(lightbulb_utf8, 0xf0eb);
    ImGui::Text("Lightbulb from code: %s", lightbulb_utf8);

    // Test 5: Font diagnostic
    ImGui::Separator();
    ImGui::Text("Test 5: Font Diagnostic");
    ImFont* currentFont = ImGui::GetFont();
    ImGui::Text("Current font: %p", currentFont);
    if (currentFont) {
        ImGui::Text("Font size: %.1f", currentFont->FontSize);
        ImGui::Text("Glyph count: %d", currentFont->Glyphs.Size);

        // Check specific glyphs
        const ImFontGlyph* lightbulb_glyph = currentFont->FindGlyph(0xf0eb);
        const ImFontGlyph* star_glyph = currentFont->FindGlyph(0xf005);
        const ImFontGlyph* home_glyph = currentFont->FindGlyph(0xf015);

        ImGui::Text("Lightbulb glyph: %s", lightbulb_glyph ? "FOUND" : "MISSING");
        ImGui::Text("Star glyph: %s", star_glyph ? "FOUND" : "MISSING");
        ImGui::Text("Home glyph: %s", home_glyph ? "FOUND" : "MISSING");

        if (lightbulb_glyph) {
            ImGui::Text("Lightbulb advance: %.2f", lightbulb_glyph->AdvanceX);
            ImGui::Text("Lightbulb visible: %s", lightbulb_glyph->Visible ? "YES" : "NO");
        }
    }

    // Test 6: All available fonts
    ImGui::Separator();
    ImGui::Text("Test 6: All Fonts");
    ImGuiIO& io = ImGui::GetIO();
    ImGui::Text("Total fonts loaded: %d", io.Fonts->Fonts.Size);

    for (int i = 0; i < io.Fonts->Fonts.Size; i++) {
        ImFont* font = io.Fonts->Fonts[i];
        if (ImGui::TreeNode((void*)(intptr_t)i, "Font %d (%p) - Size: %.1f", i, font, font->FontSize)) {
            ImGui::PushFont(font);

            // Test basic glyphs
            const ImFontGlyph* glyph_A = font->FindGlyph('A');
            const ImFontGlyph* glyph_lightbulb = font->FindGlyph(0xf0eb);

            ImGui::Text("Has 'A': %s", glyph_A ? "YES" : "NO");
            ImGui::Text("Has lightbulb: %s", glyph_lightbulb ? "YES" : "NO");
            ImGui::Text("Regular text: Hello World");
            ImGui::Text("Icon test: %s %s %s", ICON_FA_LIGHTBULB, ICON_FA_STAR, ICON_FA_HOME);

            ImGui::PopFont();
            ImGui::TreePop();
        }
    }

    // Test 7: Manual button test
    ImGui::Separator();
    ImGui::Text("Test 7: Interactive Test");
    if (ImGui::Button(ICON_FA_LIGHTBULB " Click Me")) {
        // Test button with icon
    }

    // Test 8: Hex dump of icon strings
    ImGui::Separator();
    ImGui::Text("Test 8: Icon String Analysis");
    const char* lightbulb_str = ICON_FA_LIGHTBULB;
    ImGui::Text("ICON_FA_LIGHTBULB length: %d", strlen(lightbulb_str));
    ImGui::Text("Hex bytes:");
    for (int i = 0; i < strlen(lightbulb_str) && i < 8; i++) {
        ImGui::SameLine();
        ImGui::Text("%02X", (unsigned char)lightbulb_str[i]);
    }

    // Test 9: Try different lightbulb icons
    ImGui::Separator();
    ImGui::Text("Test 9: Alternative Icons");
    ImGui::Text("Try different codes:");

    // Test with manual UTF-8 encoding of 0xF0EB
    char manual_lightbulb[4] = {0xEF, 0x83, 0xAB, 0x00}; // UTF-8 encoding of U+F0EB
    ImGui::Text("Manual UTF-8 lightbulb: %s", manual_lightbulb);

    // Test with other manual codes
    char manual_star[4] = {0xEF, 0x80, 0x85, 0x00}; // UTF-8 encoding of U+F005
    ImGui::Text("Manual UTF-8 star: %s", manual_star);

    // Test with known working characters
    ImGui::Text("ASCII test: ABC123");
    ImGui::Text("Extended ASCII: ÄÖÜ");

    // Test 10: Force use Font 0 (which has the lightbulb)
    ImGui::Separator();
    ImGui::Text("Test 10: Force Font 0");
    io = ImGui::GetIO();
    if (io.Fonts->Fonts.Size > 0) {
        ImFont* font0 = io.Fonts->Fonts[0];
        ImGui::PushFont(font0);
        ImGui::Text("With Font 0: %s Lightbulb Test", ICON_FA_LIGHTBULB);
        ImGui::Text("With Font 0: %s Star Test", ICON_FA_STAR);
        ImGui::Text("With Font 0: %s Home Test", ICON_FA_HOME);
        ImGui::PopFont();
    }

    // Test 11: Manual font switching
    ImGui::Separator();
    ImGui::Text("Test 11: Try All Fonts");
    for (int i = 0; i < io.Fonts->Fonts.Size; i++) {
        ImFont* font = io.Fonts->Fonts[i];
        ImGui::PushFont(font);
        ImGui::Text("Font %d: %s", i, ICON_FA_LIGHTBULB);
        ImGui::PopFont();
    }

    ImGui::End();
}

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

    // ========================

    // MAIN MENU BAR
    // ========================
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
                    auto selection = pfd::open_file("Select a 3D model to import", ".",
                        { "3D Models", "*.obj *.fbx *.3ds *.gltf *.glb",
                          "All Files", "*" }).result();

                    if (!selection.empty()) {
                        std::string filePath = selection[0];
                        try {
                            Engine::Model newModel = *Engine::loadModel(filePath);
                            glm::vec3 targetScale = newModel.scale;
                            newModel.startSpawnAnimation(targetScale, 0.27f);
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

                // Add icon to Point Cloud import
                std::string cloudMenuText = "Point Cloud...";
                if (g_Fonts.icons) {
                    ImGui::PushFont(g_Fonts.icons);
                    ImGui::Text(ICON_FA_CLOUD);
                    ImGui::PopFont();
                    ImGui::SameLine();
                }
                if (ImGui::MenuItem(cloudMenuText.c_str())) {
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
                                updateSpaceMouseBounds();
                            }
                        }
                        else if (extension == ".h5" || extension == ".hdf5" || extension == ".f5") {
                            Engine::PointCloud newPointCloud = std::move(Engine::PointCloudLoader::loadPointCloudFile(filePath));
                            if (newPointCloud.octreeRoot || !newPointCloud.points.empty()) {
                                newPointCloud.filePath = filePath;
                                newPointCloud.name = std::filesystem::path(filePath).stem().string();
                                currentScene.pointClouds.emplace_back(std::move(newPointCloud));
                                updateSpaceMouseBounds();
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
                auto selection = pfd::open_file("Select a scene file to load", ".",
                    { "Scene Files", "*.scene", "All Files", "*" }).result();
                if (!selection.empty()) {
                    try {
                        currentScene = Engine::loadScene(selection[0], camera);
                        pointLights = currentScene.pointLights;
                        // Ensure default: all point lights cast shadows unless specified otherwise
                        for (auto& pl : pointLights) { pl.castShadows = true; }
                        spotLights = currentScene.spotLights;

                        for (auto& model : currentScene.models) {
                            glm::vec3 targetScale = model.scale;
                            model.startSpawnAnimation(targetScale, 0.27f);
                        }
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
                        currentScene.pointLights = pointLights;
                        currentScene.spotLights = spotLights;
                        Engine::saveScene(destination, currentScene, camera);
                    }
                    catch (const std::exception& e) {
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

        // Create Menu
        if (ImGui::BeginMenu("Create")) {
            // Use non-collapsible headers instead of dropdowns
            DrawSectionHeader("Primitives");
                if (ImGui::MenuItem("Cube")) {
                    Engine::Model newCube = Engine::createCube(glm::vec3(0.8f, 0.8f, 0.8f), 1.0f, 0.0f);
                    newCube.position = glm::vec3(0.0f, 0.0f, 0.0f);
                    newCube.startSpawnAnimation(glm::vec3(0.5f), 0.27f);
                    currentScene.models.push_back(newCube);
                    currentSelectedIndex = currentScene.models.size() - 1;
                    currentSelectedType = SelectedType::Model;
                    updateSpaceMouseBounds();
                }

                if (ImGui::MenuItem("Sphere")) {
                    Engine::Model newSphere = Engine::createSphere(glm::vec3(0.8f, 0.4f, 0.4f), 1.0f, 0.0f);
                    newSphere.position = glm::vec3(0.0f, 0.0f, 0.0f);
                    newSphere.startSpawnAnimation(glm::vec3(0.5f), 0.27f);
                    currentScene.models.push_back(newSphere);
                    currentSelectedIndex = currentScene.models.size() - 1;
                    currentSelectedType = SelectedType::Model;
                    updateSpaceMouseBounds();
                }

                if (ImGui::MenuItem("Cylinder")) {
                    Engine::Model newCylinder = Engine::createCylinder(glm::vec3(0.4f, 0.8f, 0.4f), 1.0f, 0.0f);
                    newCylinder.position = glm::vec3(0.0f, 0.0f, 0.0f);
                    newCylinder.startSpawnAnimation(glm::vec3(0.5f), 0.27f);
                    currentScene.models.push_back(newCylinder);
                    currentSelectedIndex = currentScene.models.size() - 1;
                    currentSelectedType = SelectedType::Model;
                    updateSpaceMouseBounds();
                }

                if (ImGui::MenuItem("Plane")) {
                    Engine::Model newPlane = Engine::createPlane(glm::vec3(0.6f, 0.6f, 0.8f), 1.0f, 0.0f);
                    newPlane.position = glm::vec3(0.0f, 0.0f, 0.0f);
                    newPlane.startSpawnAnimation(glm::vec3(1.0f), 0.27f);
                    currentScene.models.push_back(newPlane);
                    currentSelectedIndex = currentScene.models.size() - 1;
                    currentSelectedType = SelectedType::Model;
                    updateSpaceMouseBounds();
                }

                if (ImGui::MenuItem("Torus")) {
                    Engine::Model newTorus = Engine::createTorus(glm::vec3(0.8f, 0.6f, 0.2f), 1.0f, 0.0f);
                    newTorus.position = glm::vec3(0.0f, 0.0f, 0.0f);
                    newTorus.startSpawnAnimation(glm::vec3(0.8f), 0.27f);
                    currentScene.models.push_back(newTorus);
                    currentSelectedIndex = currentScene.models.size() - 1;
                    currentSelectedType = SelectedType::Model;
                    updateSpaceMouseBounds();
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

        // Camera Menu
        if (ImGui::BeginMenu("Camera")) {
            DrawSectionHeader("Movement");

            if (ImGui::SliderFloat("Speed", &camera.speedFactor, 0.1f, 5.0f, "%.1fx")) {
                preferences.cameraSpeedFactor = camera.speedFactor;
                savePreferences();
            }
            ImGui::SameLine(); DrawHelpMarker("Multiplies camera movement speed");

            if (ImGui::SliderFloat("Sensitivity", &camera.MouseSensitivity, 0.01f, 0.08f, "%.3f")) {
                preferences.mouseSensitivity = camera.MouseSensitivity;
                savePreferences();
            }
            ImGui::SameLine(); DrawHelpMarker("Mouse rotation sensitivity");

            DrawSectionHeader("Zoom Behavior");

            if (ImGui::MenuItem("Zoom to Cursor", nullptr, &camera.zoomToCursor)) {
                preferences.zoomToCursor = camera.zoomToCursor;
                savePreferences();
            }
            ImGui::SameLine(); DrawHelpMarker("Scroll zooms toward cursor position");

            DrawSectionHeader("Orbit Mode");

            bool standardOrbit = !camera.orbitAroundCursor && !orbitFollowsCursor;
            if (ImGui::RadioButton("Standard", standardOrbit)) {
                camera.orbitAroundCursor = false;
                orbitFollowsCursor = false;
                preferences.orbitAroundCursor = false;
                preferences.orbitFollowsCursor = false;
                savePreferences();
            }
            ImGui::SameLine(); DrawHelpMarker("Orbit around viewport center");

            bool orbitAroundCursorOption = camera.orbitAroundCursor;
            if (ImGui::RadioButton("Around Cursor", orbitAroundCursorOption)) {
                camera.orbitAroundCursor = true;
                orbitFollowsCursor = false;
                preferences.orbitAroundCursor = true;
                preferences.orbitFollowsCursor = false;
                savePreferences();
            }
            ImGui::SameLine(); DrawHelpMarker("Orbit around cursor position");

            bool orbitFollowsCursorOption = orbitFollowsCursor;
            if (ImGui::RadioButton("Follow Cursor", orbitFollowsCursorOption)) {
                camera.orbitAroundCursor = false;
                orbitFollowsCursor = true;
                preferences.orbitAroundCursor = false;
                preferences.orbitFollowsCursor = true;
                savePreferences();
            }
            ImGui::SameLine(); DrawHelpMarker("Center view on cursor, then orbit");

            ImGui::EndMenu();
        }

        // Cursor Menu
        if (ImGui::BeginMenu("Cursor")) {
            auto* sphereCursor = cursorManager.getSphereCursor();
            auto* fragmentCursor = cursorManager.getFragmentCursor();
            auto* planeCursor = cursorManager.getPlaneCursor();

            DrawSectionHeader("Cursor Types");

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

            if (ImGui::BeginMenu("Quick Presets")) {
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

                        preferences.currentPresetName = currentPresetName;
                        savePreferences();
                    }
                }
                ImGui::EndMenu();
            }

            ImGui::Separator();

            if (ImGui::MenuItem("Advanced Settings...")) {
                showCursorSettingsWindow = true;
            }

            ImGui::EndMenu();
        }

        // Settings Menu
        if (ImGui::MenuItem("Settings")) {
            showSettingsWindow = true;
        }

        ImGui::EndMainMenuBar();
    }

    // ========================
    // SCENE HIERARCHY PANEL
    // ========================
    ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
    ImGui::SetNextWindowSize(ImVec2(320, viewport->Size.y - ImGui::GetFrameHeight()));
    // Ensure only the Scene Hierarchy window itself has square corners (keep inner elements rounded)
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);

    ImGui::Begin("Scene Hierarchy", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize |
        ImGuiWindowFlags_NoCollapse);

    // Scene objects list with search filter
    static char searchBuffer[128] = "";
    ImGui::InputTextWithHint("##Search", "Search objects...", searchBuffer, sizeof(searchBuffer));
    ImGui::Separator();

    if (ImGui::BeginChild("ObjectList", ImVec2(0, 250), true)) {
        ImGui::Columns(2, "ObjectColumns", false);
        ImGui::SetColumnWidth(0, 60);

        // Sun Object
        ImGui::PushID("sun");
        bool sunVisible = sun.enabled;
        if (ImGui::Checkbox("##visible", &sunVisible)) sun.enabled = sunVisible;
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
        if (ImGui::Selectable("Sun", isSunSelected, ImGuiSelectableFlags_SpanAllColumns)) {
            currentSelectedType = SelectedType::Sun;
            currentSelectedIndex = -1;
            currentSelectedMeshIndex = -1;
        }
        ImGui::NextColumn();
        ImGui::PopID();

        // Models List
        for (int i = 0; i < currentScene.models.size(); i++) {
            std::string modelName = currentScene.models[i].name;
            if (strlen(searchBuffer) > 0 && modelName.find(searchBuffer) == std::string::npos) continue;

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

            if (!hasMeshes) flags |= ImGuiTreeNodeFlags_Leaf;
            if (isModelSelected && currentSelectedMeshIndex == -1) flags |= ImGuiTreeNodeFlags_Selected;

            // Render model icon with FontAwesome and the model name separately to ensure proper fonts are used
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

                    ImGuiTreeNodeFlags meshFlags = ImGuiTreeNodeFlags_Leaf |
                        ImGuiTreeNodeFlags_NoTreePushOnOpen |
                        ImGuiTreeNodeFlags_SpanAvailWidth;

                    if (isModelSelected && currentSelectedMeshIndex == static_cast<int>(meshIndex)) {
                        meshFlags |= ImGuiTreeNodeFlags_Selected;
                    }

                    ImGui::Indent(20.0f);
                    if (g_Fonts.icons) {
                        ImGui::PushFont(g_Fonts.icons);
                        ImGui::Text(ICON_FA_CUBE);
                        ImGui::PopFont();
                        ImGui::SameLine();
                    }
                    ImGui::TreeNodeEx(("Mesh " + std::to_string(meshIndex + 1)).c_str(), meshFlags);

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

        // Point Lights List
        for (int i = 0; i < pointLights.size(); i++) {
            ImGui::PushID(i + currentScene.models.size() + 1000);
            bool isSelected = (currentSelectedIndex == i && currentSelectedType == SelectedType::PointLight);

            ImGui::AlignTextToFramePadding();
            ImGui::Text("");
            ImGui::NextColumn();

            ImGui::AlignTextToFramePadding();
            // Use FontAwesome icons with PushFont/PopFont approach for separate fonts
            if (g_Fonts.icons) {
                ImGui::PushFont(g_Fonts.icons);
                ImGui::Text(ICON_FA_LIGHTBULB);
                ImGui::PopFont();
                ImGui::SameLine();
            }
            std::string lightText = "Point Light " + std::to_string(i + 1);
            if (ImGui::Selectable(lightText.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                currentSelectedType = SelectedType::PointLight;
                currentSelectedIndex = i;
                currentSelectedMeshIndex = -1;
            }
            ImGui::NextColumn();
            ImGui::PopID();
        }

        // Spot Lights List
        for (int i = 0; i < spotLights.size(); i++) {
            ImGui::PushID(i + currentScene.models.size() + 2000);
            bool isSelected = (currentSelectedIndex == i && currentSelectedType == SelectedType::SpotLight);

            ImGui::AlignTextToFramePadding();
            // Render spot light with FontAwesome icon. Using bullseye as a spotlight metaphor.
            if (g_Fonts.icons) {
                ImGui::PushFont(g_Fonts.icons);
                ImGui::Text(ICON_FA_BULLSEYE);
                ImGui::PopFont();
                ImGui::SameLine();
            }
            std::string spotText = "Spot Light " + std::to_string(i + 1);
            if (ImGui::Selectable(spotText.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                currentSelectedType = SelectedType::SpotLight;
                currentSelectedIndex = i;
                currentSelectedMeshIndex = -1;
            }
            ImGui::NextColumn();
            ImGui::PopID();
        }

        // Point Clouds List
        for (int i = 0; i < currentScene.pointClouds.size(); i++) {
            std::string pcName = currentScene.pointClouds[i].name;
            if (strlen(searchBuffer) > 0 && pcName.find(searchBuffer) == std::string::npos) continue;

            ImGui::PushID(i + currentScene.models.size());
            bool isSelected = (currentSelectedIndex == i && currentSelectedType == SelectedType::PointCloud);

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
            if (ImGui::Selectable((pcName).c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                currentSelectedType = SelectedType::PointCloud;
                currentSelectedIndex = i;
                currentSelectedMeshIndex = -1;
            }
            ImGui::NextColumn();
            ImGui::PopID();
        }

        ImGui::Columns(1);
        ImGui::EndChild();
    }

    ImGui::Separator();

    // Properties Panel
    ImGui::Text("Properties");
    ImGui::Separator();

    if (ImGui::BeginChild("PropertiesPanel", ImVec2(0, 0), false)) {
        if (currentSelectedType == SelectedType::Model && currentSelectedIndex >= 0 &&
            currentSelectedIndex < currentScene.models.size()) {

            auto& model = currentScene.models[currentSelectedIndex];

            if (currentSelectedMeshIndex >= 0 &&
                currentSelectedMeshIndex < static_cast<int>(model.getMeshes().size())) {
                renderMeshManipulationPanel(model, currentSelectedMeshIndex, shader);
            }
            else {
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
        else if (currentSelectedType == SelectedType::PointLight && currentSelectedIndex >= 0 &&
            currentSelectedIndex < pointLights.size()) {
            renderPointLightManipulationPanel();
        }
        else if (currentSelectedType == SelectedType::SpotLight && currentSelectedIndex >= 0 &&
            currentSelectedIndex < spotLights.size()) {
            renderSpotLightManipulationPanel();
        }
        else {
            ImGui::TextDisabled("No object selected");
            ImGui::Spacing();
            ImGui::TextWrapped("Select an object from the hierarchy above to view and edit its properties.");
        }
        ImGui::EndChild();
    }

    ImGui::End();
    ImGui::PopStyleVar(1); // Restore rounding only after the Scene Hierarchy window ends

    // Render other windows
    if (showSettingsWindow) {
        renderSettingsWindow();
    }

    if (showCursorSettingsWindow) {
        renderCursorSettingsWindow();
    }

    // FPS Counter (always in top-right corner)
    if (showFPS) {
        ImGui::SetNextWindowPos(ImVec2(windowWidth - 120, windowHeight - 60));
        ImGui::Begin("FPS", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
            ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
        ImGui::Text("FPS: %.1f", ImGui::GetIO().Framerate);
        ImGui::End();
    }

    ImGui::Render();
    ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
}

void renderSettingsWindow() {
    ImGui::SetNextWindowSize(ImVec2(550, 700), ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings", &showSettingsWindow);
    bool settingsChanged = false;

    if (ImGui::BeginTabBar("SettingsTabs")) {

        // ===========================
        // RENDERING TAB
        // ===========================
        if (ImGui::BeginTabItem("Rendering")) {
            DrawSectionHeader("Lighting System");

            const char* lightingModes[] = { "Shadow Mapping", "Voxel Cone Tracing (GI)", "Radiance Raytracing" };
            int currentMode = static_cast<int>(preferences.lightingMode);
            if (ImGui::Combo("Mode", &currentMode, lightingModes, IM_ARRAYSIZE(lightingModes))) {
                preferences.lightingMode = static_cast<GUI::LightingMode>(currentMode);
                ::currentLightingMode = preferences.lightingMode;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Shadow Mapping: Traditional shadows\nVoxel Cone Tracing: Global illumination\nRadiance: Ray-traced lighting");

            // Wireframe mode setting
            if (ImGui::Checkbox("Wireframe Mode", &camera.wireframe)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Renders objects as wireframes instead of solid surfaces");

            // Mode-specific settings
            if (preferences.lightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
                ImGui::Spacing();
                DrawSectionHeader("Shadow Mapping Settings");

                if (ImGui::Checkbox("Enable Shadows", &preferences.enableShadows)) {
                    ::enableShadows = preferences.enableShadows;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Toggle shadow mapping on/off");
            }
            else if (preferences.lightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
                ImGui::Spacing();
                DrawSectionHeader("VCT Components");

                if (ImGui::Checkbox("Indirect Diffuse", &preferences.vctSettings.indirectDiffuseLight)) {
                    vctSettings.indirectDiffuseLight = preferences.vctSettings.indirectDiffuseLight;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Color bleeding and bounce lighting");

                if (ImGui::Checkbox("Indirect Specular", &preferences.vctSettings.indirectSpecularLight)) {
                    vctSettings.indirectSpecularLight = preferences.vctSettings.indirectSpecularLight;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Glossy reflections from environment");

                if (ImGui::Checkbox("Direct Lighting", &preferences.vctSettings.directLight)) {
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
                    preferences.vctSettings.diffuseConeCount = 9;
                    preferences.vctSettings.shadowSampleCount = 15;
                    preferences.vctSettings.shadowStepMultiplier = 0.1f;
                    preferences.vctSettings.tracingMaxDistance = 2.0f;
                    vctSettings = preferences.vctSettings;
                    settingsChanged = true;
                }
                ImGui::PopStyleColor();

                ImGui::Spacing();

                const char* coneOptions[] = { "1 (Fast)", "5 (Balanced)", "9 (Quality)" };
                int coneIndex = preferences.vctSettings.diffuseConeCount <= 1 ? 0 :
                    preferences.vctSettings.diffuseConeCount <= 5 ? 1 : 2;
                if (ImGui::Combo("Cone Count", &coneIndex, coneOptions, IM_ARRAYSIZE(coneOptions))) {
                    switch (coneIndex) {
                    case 0: preferences.vctSettings.diffuseConeCount = 1; break;
                    case 1: preferences.vctSettings.diffuseConeCount = 5; break;
                    case 2: preferences.vctSettings.diffuseConeCount = 9; break;
                    }
                    vctSettings.diffuseConeCount = preferences.vctSettings.diffuseConeCount;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Controls the number of cones used for indirect diffuse lighting.\nMore cones = better quality but slower performance");

                if (ImGui::SliderFloat("Trace Distance", &preferences.vctSettings.tracingMaxDistance, 0.5f, 2.5f, "%.1f units")) {
                    vctSettings.tracingMaxDistance = preferences.vctSettings.tracingMaxDistance;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Maximum distance for tracing cones (in grid units).\nLarger values capture more distant lighting but reduce performance");

                int shadowSamples = preferences.vctSettings.shadowSampleCount;
                if (ImGui::SliderInt("Shadow Samples", &shadowSamples, 5, 20)) {
                    preferences.vctSettings.shadowSampleCount = shadowSamples;
                    vctSettings.shadowSampleCount = shadowSamples;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Number of samples taken when tracing shadow cones.\nMore samples = smoother shadows but slower performance");

                if (ImGui::SliderFloat("Shadow Step Multiplier", &preferences.vctSettings.shadowStepMultiplier, 0.05f, 0.5f, "%.3f")) {
                    vctSettings.shadowStepMultiplier = preferences.vctSettings.shadowStepMultiplier;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Controls how fast shadow cone tracing advances.\nLarger values are faster but may miss details");

                DrawSectionHeader("Grid Configuration");

                float gridSize = voxelizer->getVoxelGridSize();
                if (ImGui::SliderFloat("Grid Dimensions", &gridSize, 1.0f, 50.0f, "%.1f")) {
                    voxelizer->setVoxelGridSize(gridSize);
                }
                ImGui::SameLine(); DrawHelpMarker("World space area coverage for voxelization (larger = more world captured, more voxels)");

                float voxelSize = preferences.vctSettings.voxelSize;
                if (ImGui::SliderFloat("VCT Voxel Resolution", &voxelSize, 1.0f / 256.0f, 1.0f / 32.0f, "%.5f")) {
                    preferences.vctSettings.voxelSize = voxelSize;
                    vctSettings.voxelSize = voxelSize;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Voxel resolution for cone tracing calculations (smaller = higher quality but slower)");

                DrawSectionHeader("VCT Debug");

                ImGui::Checkbox("Show Voxels", &voxelizer->showDebugVisualization);
                ImGui::SameLine(); DrawHelpMarker("Visualize the voxel grid");

                if (voxelizer->showDebugVisualization) {
                    if (ImGui::SliderFloat("Voxel Size", &voxelizer->debugVoxelSize, 0.001f, 0.1f, "%.4f")) {}
                    ImGui::SameLine(); DrawHelpMarker("Visual size of debug voxel cubes (affected by mipmap level)");

                    if (ImGui::SliderFloat("Opacity", &voxelizer->voxelOpacity, 0.0f, 1.0f, "%.2f")) {}

                    if (ImGui::SliderFloat("Color Intensity", &voxelizer->voxelColorIntensity, 0.0f, 5.0f, "%.1f")) {}
                }
            }
            else if (preferences.lightingMode == GUI::LIGHTING_RADIANCE) {
                ImGui::Spacing();
                DrawSectionHeader("Raytracing Settings");

                if (ImGui::Checkbox("Enable Raytracing", &preferences.radianceSettings.enableRaytracing)) {
                    ::radianceSettings.enableRaytracing = preferences.radianceSettings.enableRaytracing;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Enable proper raytracing from camera through pixels");

                DrawSectionHeader("Performance");

                if (ImGui::SliderInt("Max Bounces", &preferences.radianceSettings.maxBounces, 1, 4)) {
                    ::radianceSettings.maxBounces = preferences.radianceSettings.maxBounces;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Maximum number of ray bounces (1=direct lighting only, 2+=indirect lighting)");

                if (ImGui::SliderInt("Samples/Pixel", &preferences.radianceSettings.samplesPerPixel, 1, 100)) {
                    ::radianceSettings.samplesPerPixel = preferences.radianceSettings.samplesPerPixel;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Number of rays cast per pixel (higher = better quality, lower performance)");

                if (ImGui::SliderFloat("Ray Max Distance", &preferences.radianceSettings.rayMaxDistance, 10.0f, 100.0f, "%.1f")) {
                    ::radianceSettings.rayMaxDistance = preferences.radianceSettings.rayMaxDistance;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Maximum distance for ray casting");

                DrawSectionHeader("Lighting Features");

                if (ImGui::Checkbox("Indirect Lighting", &preferences.radianceSettings.enableIndirectLighting)) {
                    ::radianceSettings.enableIndirectLighting = preferences.radianceSettings.enableIndirectLighting;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Enable indirect lighting through ray bounces");

                if (ImGui::Checkbox("Emissive Lighting", &preferences.radianceSettings.enableEmissiveLighting)) {
                    ::radianceSettings.enableEmissiveLighting = preferences.radianceSettings.enableEmissiveLighting;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Use emissive objects as light sources");

                DrawSectionHeader("Intensity Controls");

                if (ImGui::SliderFloat("Indirect Intensity", &preferences.radianceSettings.indirectIntensity, 0.0f, 1.0f, "%.2f")) {
                    ::radianceSettings.indirectIntensity = preferences.radianceSettings.indirectIntensity;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Intensity of indirect lighting contribution");

                if (ImGui::SliderFloat("Sky Intensity", &preferences.radianceSettings.skyIntensity, 0.0f, 2.0f, "%.2f")) {
                    ::radianceSettings.skyIntensity = preferences.radianceSettings.skyIntensity;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Brightness of sky lighting when rays miss geometry");

                if (ImGui::SliderFloat("Emissive Intensity", &preferences.radianceSettings.emissiveIntensity, 0.0f, 3.0f, "%.2f")) {
                    ::radianceSettings.emissiveIntensity = preferences.radianceSettings.emissiveIntensity;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Multiplier for emissive object brightness");

                if (ImGui::SliderFloat("Material Roughness", &preferences.radianceSettings.materialRoughness, 0.0f, 1.0f, "%.2f")) {
                    ::radianceSettings.materialRoughness = preferences.radianceSettings.materialRoughness;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Global material roughness (0=mirror, 1=diffuse)");

                DrawSectionHeader("Acceleration");

                if (ImGui::Checkbox("Enable BVH", &preferences.radianceSettings.enableBVH)) {
                    ::enableBVH = preferences.radianceSettings.enableBVH;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Bounding Volume Hierarchy for faster ray-triangle tests");

                if (ImGui::Checkbox("Debug BVH", &preferences.radianceSettings.showBVHDebug)) {
                    ::showBVHDebug = preferences.radianceSettings.showBVHDebug;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Visualize BVH bounding boxes with color-coded depth levels:\nLevel 0: Red, Level 1: Orange, Level 2: Yellow, Level 3: Green\nLevel 4: Cyan, Level 5: Blue, Level 6: Purple, Level 7: Magenta");

                if (preferences.radianceSettings.showBVHDebug) {
                    ImGui::Indent();

                    if (ImGui::SliderInt("BVH Max Depth", &preferences.radianceSettings.bvhDebugMaxDepth, 1, 8)) {
                        settingsChanged = true;
                    }
                    ImGui::SameLine(); DrawHelpMarker("Maximum BVH depth levels to display (1=root only, higher=more detail)\nEach level has a distinct color: Red→Orange→Yellow→Green→Cyan→Blue→Purple→Magenta");

                    const char* renderModeNames[] = { "Depth Tested", "Always On Top", "Depth Biased" };
                    if (ImGui::Combo("Render Mode", &preferences.radianceSettings.bvhDebugRenderMode, renderModeNames, 3)) {
                        Engine::BVHDebugRenderer::RenderMode mode = static_cast<Engine::BVHDebugRenderer::RenderMode>(preferences.radianceSettings.bvhDebugRenderMode);
                        ::bvhDebugRenderer.setRenderMode(mode);
                        settingsChanged = true;
                    }
                    ImGui::SameLine(); DrawHelpMarker("Depth Tested: lines behind geometry hidden\nAlways On Top: lines always visible\nDepth Biased: lines slightly in front");

                    ImGui::Unindent();
                }
            }

            ImGui::EndTabItem();
        }

        // ===========================
        // CAMERA TAB
        // ===========================
        if (ImGui::BeginTabItem("Camera")) {
            DrawSectionHeader("View Settings");

            if (ImGui::SliderFloat("Field of View", &camera.Zoom, 1.0f, 120.0f, "%.0f°")) {
                preferences.fov = camera.Zoom;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Controls the camera's field of view. Higher values show more of the scene");

            if (ImGui::SliderFloat("Near Clip", &currentScene.settings.nearPlane, 0.01f, 10.0f, "%.2f")) {
                preferences.nearPlane = currentScene.settings.nearPlane;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Minimum visible distance from camera. Smaller values can cause visual artifacts");

            if (ImGui::SliderFloat("Far Clip", &currentScene.settings.farPlane, 10.0f, 1000.0f, "%.0f")) {
                preferences.farPlane = currentScene.settings.farPlane;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Maximum visible distance from camera. Higher values may impact performance");

            DrawSectionHeader("Stereoscopic 3D");

            if (ImGui::SliderFloat("Eye Separation", &currentScene.settings.separation, 0.01f, 2.0f, "%.2f")) {
                preferences.separation = currentScene.settings.separation;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Adjusts the distance between stereo views. Higher values increase 3D effect");

            if (ImGui::Checkbox("Auto Convergence", &currentScene.settings.autoConvergence)) {
                preferences.autoConvergence = currentScene.settings.autoConvergence;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Automatically sets convergence based on distance to nearest object");

            if (currentScene.settings.autoConvergence) {
                if (ImGui::SliderFloat("Distance Factor", &currentScene.settings.convergenceDistanceFactor, 0.1f, 2.0f, "%.1fx")) {
                    preferences.convergenceDistanceFactor = currentScene.settings.convergenceDistanceFactor;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Multiplier for the distance to nearest object used for convergence");

                ImGui::Text("Current Convergence: %.2f", currentScene.settings.convergence);
                ImGui::SameLine(); DrawHelpMarker("Auto-calculated convergence distance based on nearest object");
            }
            else {
                if (ImGui::SliderFloat("Convergence", &currentScene.settings.convergence, 0.0f, 40.0f, "%.1f")) {
                    preferences.convergence = currentScene.settings.convergence;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Sets the focal point distance where left and right views converge");
            }

            DrawSectionHeader("Movement");

            if (ImGui::SliderFloat("Mouse Sensitivity", &camera.MouseSensitivity, 0.01f, 0.08f, "%.3f")) {
                preferences.mouseSensitivity = camera.MouseSensitivity;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Adjusts how quickly the camera rotates in response to mouse movement");

            if (ImGui::SliderFloat("Smoothing", &mouseSmoothingFactor, 0.1f, 1.0f, "%.1f")) {
                preferences.mouseSmoothingFactor = mouseSmoothingFactor;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Controls smoothness of mouse movement. Lower values = smoother, higher values = more responsive");

            if (ImGui::SliderFloat("Speed Multiplier", &camera.speedFactor, 0.1f, 5.0f, "%.1fx")) {
                preferences.cameraSpeedFactor = camera.speedFactor;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Multiplies base movement speed. Useful for navigating larger scenes");

            if (ImGui::Checkbox("Zoom to Cursor", &camera.zoomToCursor)) {
                preferences.zoomToCursor = camera.zoomToCursor;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("When enabled, scrolling zooms toward the cursor position (including background areas)");

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
            ImGui::SameLine(); DrawHelpMarker("Orbits around the viewport center at cursor depth");

            if (ImGui::RadioButton("Orbit Around Cursor", orbitAroundCursorOption)) {
                camera.orbitAroundCursor = true;
                orbitFollowsCursor = false;
                preferences.orbitAroundCursor = true;
                preferences.orbitFollowsCursor = false;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Orbits around the 3D position of the cursor without centering the view");

            if (ImGui::RadioButton("Orbit Follows Cursor (Center)", orbitFollowsCursorOption)) {
                camera.orbitAroundCursor = false;
                orbitFollowsCursor = true;
                preferences.orbitAroundCursor = false;
                preferences.orbitFollowsCursor = true;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Centers the view on cursor position before orbiting");

            DrawSectionHeader("Smooth Scrolling");

            if (ImGui::Checkbox("Enable Smooth Scrolling", &camera.useSmoothScrolling)) {
                preferences.useSmoothScrolling = camera.useSmoothScrolling;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Enable physics-based smooth scrolling");

            if (camera.useSmoothScrolling) {
                ImGui::Indent();
                if (ImGui::SliderFloat("Momentum", &camera.scrollMomentum, 0.0f, 1.0f, "%.2f")) {
                    preferences.scrollMomentum = camera.scrollMomentum;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Controls how much scrolling 'carries' (higher = more momentum)");

                if (ImGui::SliderFloat("Max Speed", &camera.maxScrollVelocity, 0.5f, 10.0f, "%.1f")) {
                    preferences.maxScrollVelocity = camera.maxScrollVelocity;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Maximum scroll speed");

                if (ImGui::SliderFloat("Deceleration", &camera.scrollDeceleration, 1.0f, 20.0f, "%.1f")) {
                    preferences.scrollDeceleration = camera.scrollDeceleration;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("How quickly scrolling slows down");
                ImGui::Unindent();
            }

            ImGui::EndTabItem();
        }

        // ===========================
        // ENVIRONMENT TAB
        // ===========================
        if (ImGui::BeginTabItem("Environment")) {
            DrawSectionHeader("Skybox");

            const char* skyboxTypes[] = { "Cubemap", "Solid Color", "Gradient" };
            int currentType = static_cast<int>(skyboxConfig.type);
            if (ImGui::Combo("Type", &currentType, skyboxTypes, IM_ARRAYSIZE(skyboxTypes))) {
                skyboxConfig.type = static_cast<GUI::SkyboxType>(currentType);
                updateSkybox();
                preferences.skyboxType = static_cast<int>(skyboxConfig.type);
                savePreferences();
            }
            ImGui::SameLine(); DrawHelpMarker("Change the type of skybox used in the scene");

            if (skyboxConfig.type == GUI::SKYBOX_CUBEMAP) {
                std::vector<const char*> presetNames;
                for (const auto& preset : cubemapPresets) {
                    presetNames.push_back(preset.name.c_str());
                }

                if (ImGui::Combo("Theme", &skyboxConfig.selectedCubemap,
                    presetNames.data(), static_cast<int>(presetNames.size()))) {
                    updateSkybox();
                    preferences.selectedCubemap = skyboxConfig.selectedCubemap;
                    savePreferences();
                }

                if (skyboxConfig.selectedCubemap >= 0 && skyboxConfig.selectedCubemap < cubemapPresets.size()) {
                    ImGui::SameLine(); DrawHelpMarker(cubemapPresets[skyboxConfig.selectedCubemap].description.c_str());
                }

                if (ImGui::Button("Browse Custom...", ImVec2(-1, 0))) {
                    auto selection = pfd::select_folder("Select skybox directory").result();
                    if (!selection.empty()) {
                        std::string path = selection + "/";
                        std::string dirName = std::filesystem::path(selection).filename().string();

                        GUI::CubemapPreset newPreset;
                        newPreset.name = "Custom: " + dirName;
                        newPreset.path = path;
                        newPreset.description = "Custom skybox";
                        cubemapPresets.push_back(newPreset);

                        skyboxConfig.selectedCubemap = static_cast<int>(cubemapPresets.size()) - 1;
                        updateSkybox();
                        preferences.selectedCubemap = skyboxConfig.selectedCubemap;
                        savePreferences();
                    }
                }
                ImGui::SameLine(); DrawHelpMarker("Select a directory containing skybox textures (right.jpg, left.jpg, etc. OR posx.jpg, negx.jpg, etc.)");
            }
            else if (skyboxConfig.type == GUI::SKYBOX_SOLID_COLOR) {
                if (ImGui::ColorEdit3("Color", glm::value_ptr(skyboxConfig.solidColor))) {
                    updateSkybox();
                    preferences.skyboxSolidColor = skyboxConfig.solidColor;
                    savePreferences();
                }
                ImGui::SameLine(); DrawHelpMarker("Set a single color for the entire skybox");
            }
            else if (skyboxConfig.type == GUI::SKYBOX_GRADIENT) {
                bool changed = false;
                changed |= ImGui::ColorEdit3("Top", glm::value_ptr(skyboxConfig.gradientTopColor));
                ImGui::SameLine(); DrawHelpMarker("Color of the top portion of the skybox");

                changed |= ImGui::ColorEdit3("Bottom", glm::value_ptr(skyboxConfig.gradientBottomColor));
                ImGui::SameLine(); DrawHelpMarker("Color of the bottom portion of the skybox");

                if (changed) {
                    updateSkybox();
                    preferences.skyboxGradientTop = skyboxConfig.gradientTopColor;
                    preferences.skyboxGradientBottom = skyboxConfig.gradientBottomColor;
                    savePreferences();
                }
            }

            if (ImGui::SliderFloat("Ambient Light", &ambientStrengthFromSkybox, 0.0f, 1.0f, "%.2f")) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Controls how much the skybox illuminates the scene. Higher values create brighter ambient lighting");

            DrawSectionHeader("Sun Light");

            settingsChanged |= ImGui::Checkbox("Enable Sun", &sun.enabled);
            ImGui::SameLine(); DrawHelpMarker("Toggles sun lighting on/off");

            settingsChanged |= ImGui::ColorEdit3("Sun Color", glm::value_ptr(sun.color));
            ImGui::SameLine(); DrawHelpMarker("Sets the color of sunlight in the scene");

            settingsChanged |= ImGui::SliderFloat("Sun Intensity", &sun.intensity, 0.0f, 1.0f, "%.2f");
            ImGui::SameLine(); DrawHelpMarker("Controls the brightness of sunlight");

            static glm::vec3 sunAngles = glm::vec3(-45.0f, -45.0f, 0.0f);
            if (ImGui::DragFloat3("Sun Direction", glm::value_ptr(sunAngles), 1.0f, -180.0f, 180.0f, "%.0f°")) {
                glm::mat4 rotationMatrix = glm::mat4(1.0f);
                rotationMatrix = glm::rotate(rotationMatrix, glm::radians(sunAngles.x), glm::vec3(1, 0, 0));
                rotationMatrix = glm::rotate(rotationMatrix, glm::radians(sunAngles.y), glm::vec3(0, 1, 0));
                rotationMatrix = glm::rotate(rotationMatrix, glm::radians(sunAngles.z), glm::vec3(0, 0, 1));
                sun.direction = glm::normalize(glm::vec3(rotationMatrix * glm::vec4(0, -1, 0, 0)));
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Sets the direction of sunlight. Affects shadows and lighting");

            DrawSectionHeader("Default Material Properties");

            static float diffuseReflectivity = 0.8f;
            if (ImGui::SliderFloat("Diffuse Reflectivity", &diffuseReflectivity, 0.0f, 1.0f, "%.2f")) {
                // Update default material property
            }
            ImGui::SameLine(); DrawHelpMarker("Default diffuse reflectivity for new objects");

            static float specularReflectivity = 0.0f;
            if (ImGui::SliderFloat("Specular Reflectivity", &specularReflectivity, 0.0f, 1.0f, "%.2f")) {
                // Update default material property
            }
            ImGui::SameLine(); DrawHelpMarker("Default specular reflectivity for new objects");

            static float specularDiffusion = 0.5f;
            if (ImGui::SliderFloat("Specular Diffusion", &specularDiffusion, 0.0f, 1.0f, "%.2f")) {
                // Update default material property
            }
            ImGui::SameLine(); DrawHelpMarker("Default specular diffusion for new objects");

            ImGui::EndTabItem();
        }

        // ===========================
        // DISPLAY TAB
        // ===========================
        if (ImGui::BeginTabItem("Display")) {
            DrawSectionHeader("Interface");

            if (ImGui::Checkbox("Dark Theme", &isDarkTheme)) {
                SetupImGuiStyle(isDarkTheme, 1.0f);
                preferences.isDarkTheme = isDarkTheme;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Switches between light and dark color themes for the interface");

            if (ImGui::Checkbox("Show FPS", &showFPS)) {
                preferences.showFPS = showFPS;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Shows/hides the FPS counter in the top-right corner");

            if (ImGui::Checkbox("Show GUI", &showGui)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Toggle the entire GUI interface on/off (also 'G' key)");

            DrawSectionHeader("Radar Overlay");

            if (ImGui::Checkbox("Show Radar", &preferences.radarEnabled)) {
                currentScene.settings.radarEnabled = preferences.radarEnabled;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Show a radar overlay with the camera frustum");

            if (preferences.radarEnabled) {
                ImGui::Indent();
                if (ImGui::SliderFloat2("Position", glm::value_ptr(preferences.radarPos), -1.0f, 1.0f, "%.2f")) {
                    currentScene.settings.radarPos = preferences.radarPos;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Horizontal and vertical position of the radar (-1 to 1)");

                if (ImGui::SliderFloat("Scale", &preferences.radarScale, 0.001f, 0.5f, "%.3f")) {
                    currentScene.settings.radarScale = preferences.radarScale;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Size of the radar display");

                if (ImGui::Checkbox("Show Scene in Radar", &preferences.radarShowScene)) {
                    currentScene.settings.radarShowScene = preferences.radarShowScene;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Show the scene models in the radar view");
                ImGui::Unindent();
            }

            if (ImGui::Checkbox("Show Zero Plane", &preferences.showZeroPlane)) {
                currentScene.settings.showZeroPlane = preferences.showZeroPlane;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Display the zero plane in the visualization");

            DrawSectionHeader("Startup");

            if (ImGui::Checkbox("Load Scene on Start", &preferences.loadStartupScene)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Automatically load the specified scene file when the application starts");

            if (preferences.loadStartupScene) {
                static char scenePathBuffer[1024];
                if (strlen(scenePathBuffer) == 0 && !preferences.startupScenePath.empty()) {
                    strncpy_s(scenePathBuffer, preferences.startupScenePath.c_str(), sizeof(scenePathBuffer) - 1);
                }

                ImGui::InputText("Scene Path", scenePathBuffer, sizeof(scenePathBuffer));
                ImGui::SameLine(); DrawHelpMarker("Path to the .scene file to load on startup");

                ImGui::SameLine();
                if (ImGui::Button("Browse")) {
                    auto result = pfd::open_file("Select Scene", "", { "Scene Files", "*.scene" });
                    if (!result.result().empty()) {
                        std::string selectedPath = result.result()[0];
                        strncpy_s(scenePathBuffer, selectedPath.c_str(), sizeof(scenePathBuffer) - 1);
                        preferences.startupScenePath = selectedPath;
                        settingsChanged = true;
                    }
                }

                if (strcmp(scenePathBuffer, preferences.startupScenePath.c_str()) != 0) {
                    preferences.startupScenePath = std::string(scenePathBuffer);
                    settingsChanged = true;
                }
            }

            ImGui::EndTabItem();
        }

        // ===========================
        // INPUT TAB
        // ===========================
        if (ImGui::BeginTabItem("Input")) {
            DrawSectionHeader("Mouse Settings");

            if (ImGui::SliderFloat("Mouse Sensitivity", &camera.MouseSensitivity, 0.01f, 0.08f, "%.3f")) {
                preferences.mouseSensitivity = camera.MouseSensitivity;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Adjusts how quickly the camera rotates in response to mouse movement");

            if (ImGui::SliderFloat("Mouse Smoothing", &mouseSmoothingFactor, 0.1f, 1.0f, "%.1f")) {
                preferences.mouseSmoothingFactor = mouseSmoothingFactor;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Controls smoothness of mouse movement. Lower values = smoother, higher values = more responsive");

            if (ImGui::SliderFloat("Speed Multiplier", &camera.speedFactor, 0.1f, 5.0f, "%.1fx")) {
                preferences.cameraSpeedFactor = camera.speedFactor;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Multiplies base movement speed. Useful for navigating larger scenes");

            if (ImGui::Checkbox("Zoom to Cursor", &camera.zoomToCursor)) {
                preferences.zoomToCursor = camera.zoomToCursor;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("When enabled, scrolling zooms toward or away from the 3D cursor position");

            DrawSectionHeader("Camera Behavior");

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
            ImGui::SameLine(); DrawHelpMarker("Orbits around the viewport center at cursor depth");

            if (ImGui::RadioButton("Orbit Around Cursor", orbitAroundCursorOption)) {
                camera.orbitAroundCursor = true;
                orbitFollowsCursor = false;
                preferences.orbitAroundCursor = true;
                preferences.orbitFollowsCursor = false;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Orbits around the 3D position of the cursor without centering the view");

            if (ImGui::RadioButton("Orbit Follows Cursor (Center)", orbitFollowsCursorOption)) {
                camera.orbitAroundCursor = false;
                orbitFollowsCursor = true;
                preferences.orbitAroundCursor = false;
                preferences.orbitFollowsCursor = true;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Centers the view on cursor position before orbiting");

            if (ImGui::Checkbox("Smooth Scrolling", &camera.useSmoothScrolling)) {
                preferences.useSmoothScrolling = camera.useSmoothScrolling;
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Enable physics-based smooth scrolling");

            if (camera.useSmoothScrolling) {
                ImGui::Indent();
                if (ImGui::SliderFloat("Momentum", &camera.scrollMomentum, 0.0f, 1.0f, "%.2f")) {
                    preferences.scrollMomentum = camera.scrollMomentum;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Controls how much scrolling 'carries' (higher = more momentum)");

                if (ImGui::SliderFloat("Max Velocity", &camera.maxScrollVelocity, 0.5f, 10.0f, "%.1f")) {
                    preferences.maxScrollVelocity = camera.maxScrollVelocity;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Maximum scroll speed");

                if (ImGui::SliderFloat("Deceleration", &camera.scrollDeceleration, 1.0f, 20.0f, "%.1f")) {
                    preferences.scrollDeceleration = camera.scrollDeceleration;
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("How quickly scrolling slows down");
                ImGui::Unindent();
            }

            DrawSectionHeader("3DConnexion SpaceMouse");

            if (spaceMouseInitialized) {
                if (ImGui::Checkbox("Enable SpaceMouse", &preferences.spaceMouseEnabled)) {
                    spaceMouseInput.SetEnabled(preferences.spaceMouseEnabled);
                    settingsChanged = true;
                }
                ImGui::SameLine(); DrawHelpMarker("Enable or disable 3DConnexion SpaceMouse input");

                if (preferences.spaceMouseEnabled) {
                    if (ImGui::SliderFloat("Deadzone", &preferences.spaceMouseDeadzone, 0.0f, 0.5f, "%.2f")) {
                        spaceMouseInput.SetDeadzone(preferences.spaceMouseDeadzone);
                        settingsChanged = true;
                    }
                    ImGui::SameLine(); DrawHelpMarker("Movement threshold below which SpaceMouse input is ignored");

                    if (ImGui::SliderFloat("Translation", &preferences.spaceMouseTranslationSensitivity, 0.1f, 3.0f, "%.1fx")) {
                        spaceMouseInput.SetSensitivity(preferences.spaceMouseTranslationSensitivity, preferences.spaceMouseRotationSensitivity);
                        settingsChanged = true;
                    }
                    ImGui::SameLine(); DrawHelpMarker("Controls how sensitive translation movements are");

                    if (ImGui::SliderFloat("Rotation", &preferences.spaceMouseRotationSensitivity, 0.1f, 3.0f, "%.1fx")) {
                        spaceMouseInput.SetSensitivity(preferences.spaceMouseTranslationSensitivity, preferences.spaceMouseRotationSensitivity);
                        settingsChanged = true;
                    }
                    ImGui::SameLine(); DrawHelpMarker("Controls how sensitive rotation movements are");

                    ImGui::Spacing();
                    ImGui::Text("Anchor Point Mode:");

                    int currentMode = static_cast<int>(preferences.spaceMouseAnchorMode);
                    bool modeChanged = false;

                    if (ImGui::RadioButton("Scene Center", &currentMode, static_cast<int>(GUI::SPACEMOUSE_ANCHOR_DISABLED))) {
                        modeChanged = true;
                    }
                    ImGui::SameLine(); DrawHelpMarker("Use the scene center as the SpaceMouse pivot point");

                    if (ImGui::RadioButton("Cursor on Start", &currentMode, static_cast<int>(GUI::SPACEMOUSE_ANCHOR_ON_START))) {
                        modeChanged = true;
                    }
                    ImGui::SameLine(); DrawHelpMarker("Set anchor to cursor position when SpaceMouse navigation starts, then keep it fixed");

                    if (ImGui::RadioButton("Follow Cursor", &currentMode, static_cast<int>(GUI::SPACEMOUSE_ANCHOR_CONTINUOUS))) {
                        modeChanged = true;
                    }
                    ImGui::SameLine(); DrawHelpMarker("Continuously update anchor to follow the cursor position");

                    if (modeChanged) {
                        preferences.spaceMouseAnchorMode = static_cast<GUI::SpaceMouseAnchorMode>(currentMode);
                        settingsChanged = true;
                        updateSpaceMouseCursorAnchor();
                        spaceMouseInput.RefreshPivotPosition();
                    }

                    if (ImGui::Checkbox("Center Cursor During Navigation", &preferences.spaceMouseCenterCursor)) {
                        settingsChanged = true;
                    }
                    ImGui::SameLine(); DrawHelpMarker("Keep the mouse cursor fixed at the screen center while using SpaceMouse");
                }
            }
            else {
                ImGui::TextDisabled("SpaceMouse not detected");
                ImGui::TextWrapped("Connect a 3Dconnexion SpaceMouse device to enable 3D navigation.");
            }

            DrawSectionHeader("Keyboard Shortcuts");

            if (ImGui::CollapsingHeader("Keybind Reference")) {
                ImGui::Columns(2, "keybinds");
                ImGui::SetColumnWidth(0, 150);

                ImGui::Text("Camera Controls");
                ImGui::Separator();

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

        // ===========================
        // IMPORT TAB
        // ===========================
        if (ImGui::BeginTabItem("Import")) {
            DrawSectionHeader("Model Import Options");

            if (ImGui::Checkbox("Flip UV Coordinates", &preferences.modelImportSettings.flipUVs)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Flips the V (Y) coordinate of UV maps. Enable if textures appear upside down.");

            if (ImGui::Checkbox("Generate Normals", &preferences.modelImportSettings.generateNormals)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Automatically generate vertex normals for models that don't have them.");

            if (ImGui::Checkbox("Calculate Tangents", &preferences.modelImportSettings.calculateTangentSpace)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Calculate tangent and bitangent vectors for normal mapping.");

            if (ImGui::Checkbox("Merge Vertices", &preferences.modelImportSettings.joinIdenticalVertices)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Merge vertices that share the same position, normal, and UV coordinates.");

            DrawSectionHeader("Optimization");

            if (ImGui::Checkbox("Sort by Primitive Type", &preferences.modelImportSettings.sortByPrimitiveType)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Sort faces by primitive type for better rendering performance.");

            if (ImGui::Checkbox("Fix Inverted Normals", &preferences.modelImportSettings.fixInfacingNormals)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Automatically fix normals that face inward instead of outward.");

            if (ImGui::Checkbox("Remove Redundant Materials", &preferences.modelImportSettings.removeRedundantMaterials)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Remove duplicate materials to reduce memory usage.");

            if (ImGui::Checkbox("Optimize Meshes", &preferences.modelImportSettings.optimizeMeshes)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Optimize mesh data for better rendering performance.");

            if (ImGui::Checkbox("Pre-transform Vertices", &preferences.modelImportSettings.pretransformVertices)) {
                settingsChanged = true;
            }
            ImGui::SameLine(); DrawHelpMarker("Apply node transformations directly to vertices. Use for static models only.");

            ImGui::Spacing();
            if (ImGui::Button("Reset to Defaults", ImVec2(-1, 0))) {
                preferences.modelImportSettings = GUI::ApplicationPreferences::ModelImportSettings{};
                settingsChanged = true;
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
    auto* sphereCursor = cursorManager.getSphereCursor();
    auto* fragmentCursor = cursorManager.getFragmentCursor();
    auto* planeCursor = cursorManager.getPlaneCursor();

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

        std::vector<std::string> presetNames = Engine::CursorPresetManager::getPresetNames();
        for (const auto& name : presetNames) {
            bool isSelected = (currentPresetName == name);
            if (ImGui::Selectable(name.c_str(), isSelected)) {
                currentPresetName = name;
                try {
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

                    preferences.currentPresetName = currentPresetName;
                    savePreferences();
                }
                catch (const std::exception& e) {
                    std::cerr << "Error loading preset: " << e.what() << std::endl;
                }
            }
        }
        ImGui::EndCombo();
    }

    if (isEditingPresetName) {
        ImGui::InputText("Name", editPresetNameBuffer, IM_ARRAYSIZE(editPresetNameBuffer));

        if (ImGui::Button("Save", ImVec2(100, 0))) {
            std::string newName = editPresetNameBuffer;
            if (!newName.empty()) {
                Engine::CursorPreset newPreset;
                newPreset.name = newName;
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
                currentPresetName = newName;
                isEditingPresetName = false;
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Cancel", ImVec2(100, 0))) {
            isEditingPresetName = false;
        }
    }
    else {
        if (ImGui::Button("Update", ImVec2(100, 0))) {
            Engine::CursorPreset updatedPreset;
            updatedPreset.name = currentPresetName;
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

    // Cursor Type Tabs
    if (ImGui::BeginTabBar("CursorTypes")) {

        // 3D Sphere Tab
        if (ImGui::BeginTabItem("3D Sphere")) {
            bool sphereVisible = sphereCursor->isVisible();
            if (ImGui::Checkbox("Enable 3D Sphere", &sphereVisible)) {
                sphereCursor->setVisible(sphereVisible);
            }

            if (sphereVisible) {
                DrawSectionHeader("Size & Scaling");

                const char* scalingModes[] = { "Fixed Size", "Dynamic (Depth-based)" };
                int currentMode = static_cast<int>(sphereCursor->getScalingMode());
                if (ImGui::Combo("Scaling", &currentMode, scalingModes, IM_ARRAYSIZE(scalingModes))) {
                    sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(currentMode));
                }

                if (currentMode == static_cast<int>(GUI::CursorScalingMode::CURSOR_FIXED)) {
                    float fixedRadius = sphereCursor->getFixedRadius();
                    if (ImGui::SliderFloat("Radius", &fixedRadius, 0.01f, 3.0f, "%.2f")) {
                        sphereCursor->setFixedRadius(fixedRadius);
                    }
                }
                else {
                    float minDiff = sphereCursor->getMinDiff();
                    if (ImGui::SliderFloat("Min Size", &minDiff, 0.01f, 2.0f, "%.2f")) {
                        sphereCursor->setMinDiff(minDiff);
                    }

                    float maxDiff = sphereCursor->getMaxDiff();
                    if (ImGui::SliderFloat("Max Size", &maxDiff, 0.02f, 5.0f, "%.2f")) {
                        sphereCursor->setMaxDiff(maxDiff);
                    }
                }

                DrawSectionHeader("Appearance");

                glm::vec4 cursorColor = sphereCursor->getColor();
                if (ImGui::ColorEdit3("Color", glm::value_ptr(cursorColor))) {
                    sphereCursor->setColor(cursorColor);
                }

                float transparency = sphereCursor->getTransparency();
                if (ImGui::SliderFloat("Opacity", &transparency, 0.0f, 1.0f, "%.2f")) {
                    sphereCursor->setTransparency(transparency);
                }

                float edgeSoftness = sphereCursor->getEdgeSoftness();
                if (ImGui::SliderFloat("Edge Softness", &edgeSoftness, 0.0f, 1.0f, "%.2f")) {
                    sphereCursor->setEdgeSoftness(edgeSoftness);
                }

                float centerTransparency = sphereCursor->getCenterTransparency();
                if (ImGui::SliderFloat("Center Fade", &centerTransparency, 0.0f, 1.0f, "%.2f")) {
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
                    if (ImGui::SliderFloat("Inner Size", &innerFactor, 0.1f, 0.9f, "%.2f")) {
                        sphereCursor->setInnerSphereFactor(innerFactor);
                    }
                }
            }
            ImGui::EndTabItem();
        }

        // 2D Circle Tab
        if (ImGui::BeginTabItem("2D Circle")) {
            bool fragmentVisible = fragmentCursor->isVisible();
            if (ImGui::Checkbox("Enable 2D Circle", &fragmentVisible)) {
                fragmentCursor->setVisible(fragmentVisible);
            }

            if (fragmentVisible) {
                DrawSectionHeader("Ring Dimensions");

                float outerRadius = fragmentCursor->getBaseOuterRadius();
                if (ImGui::SliderFloat("Outer Radius", &outerRadius, 0.0f, 0.3f, "%.3f")) {
                    fragmentCursor->setBaseOuterRadius(outerRadius);
                }

                float outerBorder = fragmentCursor->getBaseOuterBorderThickness();
                if (ImGui::SliderFloat("Outer Thickness", &outerBorder, 0.0f, 0.08f, "%.3f")) {
                    fragmentCursor->setBaseOuterBorderThickness(outerBorder);
                }

                float innerRadius = fragmentCursor->getBaseInnerRadius();
                if (ImGui::SliderFloat("Inner Radius", &innerRadius, 0.0f, 0.2f, "%.3f")) {
                    fragmentCursor->setBaseInnerRadius(innerRadius);
                }

                float innerBorder = fragmentCursor->getBaseInnerBorderThickness();
                if (ImGui::SliderFloat("Inner Thickness", &innerBorder, 0.0f, 0.08f, "%.3f")) {
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
            ImGui::EndTabItem();
        }

        // Surface Plane Tab
        if (ImGui::BeginTabItem("Surface Plane")) {
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

                ImGui::TextWrapped("The plane cursor aligns to surface normals and shows the tangent plane at the cursor position.");
            }
            ImGui::EndTabItem();
        }

        // Orbit Visualization Tab
        if (ImGui::BeginTabItem("Orbit Center")) {
            DrawSectionHeader("Orbit Visualization");

            bool showOrbitCenter = cursorManager.isShowOrbitCenter();
            if (ImGui::Checkbox("Show Orbit Center", &showOrbitCenter)) {
                cursorManager.setShowOrbitCenter(showOrbitCenter);
            }
            ImGui::SameLine(); DrawHelpMarker("Display a marker at the camera's orbit pivot point");

            if (showOrbitCenter) {
                glm::vec4 orbitColor = cursorManager.getOrbitCenterColor();
                if (ImGui::ColorEdit3("Marker Color", glm::value_ptr(orbitColor))) {
                    cursorManager.setOrbitCenterColor(orbitColor);
                }

                float orbitSize = cursorManager.getOrbitCenterSphereRadius();
                if (ImGui::SliderFloat("Marker Size", &orbitSize, 0.01f, 1.0f, "%.2f")) {
                    cursorManager.setOrbitCenterSphereRadius(orbitSize);
                }
            }

            DrawSectionHeader("Orbit Behavior");

            ImGui::Text("Camera orbit mode affects where the orbit center appears:");
            ImGui::Spacing();

            ImGui::TextWrapped("• Standard: Center of viewport at cursor depth");
            ImGui::TextWrapped("• Around Cursor: At the 3D cursor position");
            ImGui::TextWrapped("• Follow Cursor: View centers on cursor first");

            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    ImGui::End();
}

void renderSunManipulationPanel() {
    DrawSectionHeader("Sun Light");

    ImGui::Checkbox("Enabled", &sun.enabled);
    ImGui::ColorEdit3("Color", glm::value_ptr(sun.color));
    ImGui::SliderFloat("Intensity", &sun.intensity, 0.0f, 10.0f, "%.1f");

    ImGui::Spacing();

    static glm::vec3 angles = glm::vec3(-45.0f, -45.0f, 0.0f);
    if (ImGui::DragFloat3("Direction (Angles)", glm::value_ptr(angles), 1.0f, -180.0f, 180.0f, "%.0f°")) {
        glm::mat4 rotationMatrix = glm::mat4(1.0f);
        rotationMatrix = glm::rotate(rotationMatrix, glm::radians(angles.x), glm::vec3(1, 0, 0));
        rotationMatrix = glm::rotate(rotationMatrix, glm::radians(angles.y), glm::vec3(0, 1, 0));
        rotationMatrix = glm::rotate(rotationMatrix, glm::radians(angles.z), glm::vec3(0, 0, 1));
        sun.direction = glm::normalize(glm::vec3(rotationMatrix * glm::vec4(0, -1, 0, 0)));
    }

    ImGui::Text("Direction Vector: (%.2f, %.2f, %.2f)",
        sun.direction.x, sun.direction.y, sun.direction.z);
}

void renderModelManipulationPanel(Engine::Model& model, Engine::Shader* shader) {
    ImGui::Text("📦 %s", model.name.c_str());
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool transformChanged = false;
        transformChanged |= ImGui::DragFloat3("Position", glm::value_ptr(model.position), 0.1f);
        transformChanged |= ImGui::DragFloat3("Rotation", glm::value_ptr(model.rotation), 1.0f, -360.0f, 360.0f, "%.0f°");
        transformChanged |= ImGui::DragFloat3("Scale", glm::value_ptr(model.scale), 0.01f, 0.01f, 100.0f);

        if (transformChanged) {
            updateSpaceMouseBounds();
        }
    }

    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Base Color", glm::value_ptr(model.color));
        ImGui::SliderFloat("Shininess", &model.shininess, 1.0f, 90.0f);
        ImGui::SliderFloat("Emissive", &model.emissive, 0.0f, 1.0f);

        if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
            ImGui::Spacing();
            DrawSectionHeader("VCT Properties");

            static const char* material_types[] = { "Concrete", "Metal", "Plastic", "Glass", "Wood", "Marble", "Custom" };
            int current_type = static_cast<int>(model.materialType);

            if (ImGui::Combo("Preset", &current_type, material_types, IM_ARRAYSIZE(material_types))) {
                model.applyMaterialPreset(static_cast<MaterialType>(current_type));
            }

            ImGui::SliderFloat("Diffuse", &model.diffuseReflectivity, 0.0f, 1.0f);
            ImGui::ColorEdit3("Specular", glm::value_ptr(model.specularColor));
            ImGui::SliderFloat("Reflectivity", &model.specularReflectivity, 0.0f, 1.0f);
            ImGui::SliderFloat("Glossiness", &model.specularDiffusion, 0.0f, 1.0f);
            ImGui::SliderFloat("IOR", &model.refractiveIndex, 1.0f, 3.0f);
            ImGui::SameLine(); DrawHelpMarker("Index of Refraction:\n1.0 = Air\n1.33 = Water\n1.5 = Glass\n2.4 = Diamond");
            ImGui::SliderFloat("Transparency", &model.transparency, 0.0f, 1.0f);
        }
    }

    if (ImGui::CollapsingHeader("Textures")) {
        if (!model.getMeshes().empty()) {
            const auto& mesh = model.getMeshes()[0];
            if (!mesh.textures.empty()) {
                ImGui::Text("Loaded:");
                for (const auto& texture : mesh.textures) {
                    ImGui::BulletText("%s", texture.type.c_str());
                }
            }
            else {
                ImGui::TextDisabled("No textures loaded");
            }
        }

        ImGui::Spacing();

        auto textureLoadButton = [&](const char* label, const char* type) {
            if (ImGui::Button(label, ImVec2(-1, 0))) {
                auto selection = pfd::open_file("Select texture", ".",
                    { "Images", "*.png *.jpg *.jpeg *.bmp", "All", "*" }).result();
                if (!selection.empty()) {
                    Texture texture;
                    texture.id = model.TextureFromFile(selection[0].c_str(), selection[0], selection[0]);
                    texture.type = type;
                    texture.path = selection[0];
                    for (auto& mesh : model.getMeshes()) {
                        mesh.textures.push_back(texture);
                    }
                }
            }
            };

        textureLoadButton("Load Diffuse", "texture_diffuse");
        textureLoadButton("Load Normal", "texture_normal");
        textureLoadButton("Load Specular", "texture_specular");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Delete Model", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Delete Model?");
    }

    if (ImGui::BeginPopupModal("Delete Model?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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

void renderMeshManipulationPanel(Engine::Model& model, int meshIndex, Engine::Shader* shader) {
    auto& mesh = model.getMeshes()[meshIndex];

    ImGui::Text("📦 %s - Mesh %d", model.name.c_str(), meshIndex + 1);
    ImGui::Separator();

    ImGui::Checkbox("Visible", &mesh.visible);

    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", glm::value_ptr(mesh.color));
        ImGui::SliderFloat("Shininess", &mesh.shininess, 1.0f, 90.0f);
        ImGui::SliderFloat("Emissive", &mesh.emissive, 0.0f, 1.0f);

        if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
            ImGui::TextDisabled("VCT properties are per-model only");
        }
    }

    if (ImGui::CollapsingHeader("Textures")) {
        if (!mesh.textures.empty()) {
            ImGui::Text("Loaded:");
            for (const auto& texture : mesh.textures) {
                ImGui::BulletText("%s", texture.type.c_str());
            }
        }
        else {
            ImGui::TextDisabled("No textures");
        }

        ImGui::Spacing();

        auto textureLoadButton = [&](const char* label, const char* type) {
            if (ImGui::Button(label, ImVec2(-1, 0))) {
                auto selection = pfd::open_file("Select texture", ".",
                    { "Images", "*.png *.jpg *.jpeg *.bmp", "All", "*" }).result();
                if (!selection.empty()) {
                    Texture texture;
                    texture.id = model.TextureFromFile(selection[0].c_str(), selection[0], selection[0]);
                    texture.type = type;
                    texture.path = selection[0];
                    mesh.textures.push_back(texture);
                }
            }
            };

        textureLoadButton("Load Diffuse", "texture_diffuse");
        textureLoadButton("Load Normal", "texture_normal");
        textureLoadButton("Load Specular", "texture_specular");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Delete Mesh", ImVec2(-1, 0))) {
        if (model.getMeshes().size() > 1) {
            ImGui::OpenPopup("Delete Mesh?");
        }
        else {
            ImGui::OpenPopup("Cannot Delete");
        }
    }

    if (ImGui::BeginPopupModal("Delete Mesh?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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

    if (ImGui::BeginPopupModal("Cannot Delete", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
        ImGui::Text("Cannot delete the last mesh.");
        ImGui::Text("Delete the entire model instead.");
        if (ImGui::Button("OK", ImVec2(120, 0))) {
            ImGui::CloseCurrentPopup();
        }
        ImGui::EndPopup();
    }
}

void renderPointCloudManipulationPanel(Engine::PointCloud& pointCloud) {
    ImGui::Text("☁ %s", pointCloud.name.c_str());

    bool hasData = !pointCloud.points.empty() || pointCloud.octreeRoot;
    if (!hasData) {
        ImGui::TextDisabled("Point cloud is empty");
        return;
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        bool transformChanged = false;
        transformChanged |= ImGui::DragFloat3("Position", glm::value_ptr(pointCloud.position), 0.1f);
        transformChanged |= ImGui::DragFloat3("Rotation", glm::value_ptr(pointCloud.rotation), 1.0f, -360.0f, 360.0f, "%.0f°");
        transformChanged |= ImGui::DragFloat3("Scale", glm::value_ptr(pointCloud.scale), 0.01f, 0.01f, 100.0f);

        if (transformChanged) {
            updateSpaceMouseBounds();
        }
    }

    if (ImGui::CollapsingHeader("Display Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::SliderFloat("Point Size", &pointCloud.basePointSize, 1.0f, 10.0f, "%.1f");
        ImGui::SameLine(); DrawHelpMarker("Base size of points in the cloud");
    }

    if (ImGui::CollapsingHeader("Level of Detail")) {
        DrawSectionHeader("LOD Distances");

        ImGui::SliderFloat("LOD 1", &pointCloud.lodDistances[0], 1.0f, 15.0f, "%.1f");
        ImGui::SliderFloat("LOD 2", &pointCloud.lodDistances[1], 10.0f, 30.0f, "%.1f");
        ImGui::SliderFloat("LOD 3", &pointCloud.lodDistances[2], 15.0f, 40.0f, "%.1f");
        ImGui::SliderFloat("LOD 4", &pointCloud.lodDistances[3], 20.0f, 50.0f, "%.1f");
        ImGui::SliderFloat("LOD 5", &pointCloud.lodDistances[4], 25.0f, 60.0f, "%.1f");

        DrawSectionHeader("Chunking");

        ImGui::SliderFloat("Chunk Size", &pointCloud.newChunkSize, 1.0f, 50.0f, "%.1f");
        ImGui::SameLine(); DrawHelpMarker("Size of spatial chunks for optimization");

        if (ImGui::Button("Recalculate Chunks", ImVec2(-1, 0))) {
            if (pointCloud.newChunkSize != pointCloud.chunkSize) {
                pointCloud.chunkSize = pointCloud.newChunkSize;
                Engine::generateChunks(pointCloud, pointCloud.chunkSize);
            }
        }

        extern std::atomic<bool> isRecalculatingChunks;
        if (isRecalculatingChunks.load()) {
            ImGui::TextColored(ImVec4(1.0f, 1.0f, 0.0f, 1.0f), "Recalculating chunks...");
        }

        ImGui::Checkbox("Visualize Chunks", &pointCloud.visualizeChunks);
        ImGui::SameLine(); DrawHelpMarker("Show chunk boundaries for debugging");
    }

    if (ImGui::CollapsingHeader("Export")) {
        static int exportFormat = 0;
        ImGui::RadioButton("XYZ Format", &exportFormat, 0);
        ImGui::SameLine();
        ImGui::RadioButton("Binary Format", &exportFormat, 1);

        if (ImGui::Button("Export Point Cloud...", ImVec2(-1, 0))) {
            std::string defaultExt = (exportFormat == 0) ? ".xyz" : ".pcb";
            auto destination = pfd::save_file("Export point cloud", ".",
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
        }
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Delete Point Cloud", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Delete Point Cloud?");
    }

    if (ImGui::BeginPopupModal("Delete Point Cloud?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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
        currentScene.models.erase(currentScene.models.begin() + currentSelectedIndex);
        currentSelectedIndex = -1;
        currentSelectedType = SelectedType::None;
        updateSpaceMouseBounds();
    }
}

void deleteSelectedPointCloud() {
    if (currentSelectedType == SelectedType::PointCloud && currentSelectedIndex >= 0 &&
        currentSelectedIndex < currentScene.pointClouds.size()) {
        glDeleteVertexArrays(1, &currentScene.pointClouds[currentSelectedIndex].vao);
        glDeleteBuffers(1, &currentScene.pointClouds[currentSelectedIndex].vbo);
        currentScene.pointClouds.erase(currentScene.pointClouds.begin() + currentSelectedIndex);
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

    auto& light = pointLights[currentSelectedIndex];

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

    if (ImGui::CollapsingHeader("Light Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
        ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f, "%.1f");
        ImGui::SameLine(); DrawHelpMarker("Brightness of the point light");

        // Cast Shadows toggle
        bool cast = light.castShadows;
        if (ImGui::Checkbox("Cast Shadows", &cast)) {
            light.castShadows = cast;
        }
        ImGui::SameLine(); DrawHelpMarker("If enabled, this light renders a depth cubemap and casts shadows");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Delete Light", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Delete Light?");
    }

    if (ImGui::BeginPopupModal("Delete Light?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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

    auto& light = spotLights[currentSelectedIndex];

    ImGui::Text("🔦 Spot Light %d", currentSelectedIndex + 1);
    ImGui::Separator();

    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", glm::value_ptr(light.position), 0.1f);
        ImGui::DragFloat3("Direction", glm::value_ptr(light.direction), 0.01f, -1.0f, 1.0f);

        if (ImGui::Button("Normalize Direction", ImVec2(-1, 0))) {
            light.direction = glm::normalize(light.direction);
        }
        ImGui::SameLine(); DrawHelpMarker("Make direction vector unit length");
    }

    if (ImGui::CollapsingHeader("Light Properties", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", glm::value_ptr(light.color));
        ImGui::SliderFloat("Intensity", &light.intensity, 0.0f, 5.0f, "%.1f");

        DrawSectionHeader("Cone Settings");

        float innerAngle = glm::degrees(glm::acos(light.innerCutOff));
        float outerAngle = glm::degrees(glm::acos(light.outerCutOff));

        if (ImGui::SliderFloat("Inner Angle", &innerAngle, 0.0f, 89.0f, "%.1f°")) {
            light.innerCutOff = glm::cos(glm::radians(innerAngle));
        }
        ImGui::SameLine(); DrawHelpMarker("Angle of the bright inner cone");

        if (ImGui::SliderFloat("Outer Angle", &outerAngle, 0.0f, 89.0f, "%.1f°")) {
            light.outerCutOff = glm::cos(glm::radians(outerAngle));
        }
        ImGui::SameLine(); DrawHelpMarker("Angle of the soft falloff cone");

        // Ensure inner angle is always smaller than outer angle
        if (innerAngle > outerAngle) {
            light.outerCutOff = light.innerCutOff;
        }

        // Cast Shadows toggle for spot light (future use if spot shadows are implemented)
        bool cast = light.castShadows;
        if (ImGui::Checkbox("Cast Shadows", &cast)) {
            light.castShadows = cast;
        }
        ImGui::SameLine(); DrawHelpMarker("Toggle whether this spot light should cast shadows (when supported)");
    }

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (ImGui::Button("Delete Light", ImVec2(-1, 0))) {
        ImGui::OpenPopup("Delete Light?");
    }

    if (ImGui::BeginPopupModal("Delete Light?", NULL, ImGuiWindowFlags_AlwaysAutoResize)) {
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