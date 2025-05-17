// ---- Core Definitions ----
#define NOMINMAX
#include "Engine/Core.h"
#include <thread>
#include <atomic>

// ---- Project-Specific Includes ----
#include "Loaders/ModelLoader.h"
#include "Core/Camera.h"
#include "Core/SceneManager.h"
#include "Cursors/CursorPresets.h"
#include "Loaders/PointCloudLoader.h"
#include "Cursors/Base/CursorManager.h"
#include "Core/Voxalizer.h"
#include "Gui/Gui.h"
#include "Gui/GuiTypes.h"

// ---- GUI and Dialog ----
#include "imgui/imgui_incl.h"
#include <portable-file-dialogs.h>

// ---- Utility Libraries ----
#include <json.h>
#include <corecrt_math_defines.h>
#include <openLinks.h>
#include <glm/gtx/component_wise.hpp>
#include <stb_image.h>


using namespace Engine;
using json = nlohmann::json;

// Use the GUI namespace types
using GUI::SkyboxType;
using GUI::SKYBOX_CUBEMAP;
using GUI::SKYBOX_SOLID_COLOR;
using GUI::SKYBOX_GRADIENT;
using GUI::CursorScalingMode;
using GUI::CURSOR_NORMAL;
using GUI::CURSOR_FIXED;
using GUI::CURSOR_CONSTRAINED_DYNAMIC;
using GUI::CURSOR_LOGARITHMIC;
using GUI::CubemapPreset;
using GUI::FragmentShaderCursorSettings;
using GUI::ApplicationPreferences;

// ---- Function Declarations ----
#pragma region Function Declarations
// ---- GLFW Callback Functions ----
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);

// ---- Rendering Functions ----
void renderEye(GLenum drawBuffer, const glm::mat4& projection, const glm::mat4& view, Engine::Shader* shader, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, GLFWwindow* window);
void renderModels(Engine::Shader* shader);
void renderPointClouds(Engine::Shader* shader);

// ---- Update Functions ----
void updatePointLights();

PointCloud loadPointCloudFile(const std::string& filePath, size_t downsampleFactor = 1);

void createDefaultCubemap();
void initSkybox();

void cleanup(Engine::Shader* shader);

// ---- Utility Functions ----
float calculateLargestModelDimension();
void calculateMouseRay(float mouseX, float mouseY, glm::vec3& rayOrigin, glm::vec3& rayDirection, glm::vec3& rayNear, glm::vec3& rayFar, float aspect);
bool rayIntersectsModel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const Engine::Model& model, float& distance);
#pragma endregion


// ---- Global Variables ----
#pragma region Global Variables
// ---- Scene Management ----
Engine::Scene currentScene;
int currentModelIndex = -1;
std::string modelPath = "D:/OBJ/motorbike.obj";
static char modelPathBuffer[256] = ""; // Buffer for ImGui model path input

// ---- Camera Configuration ----
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
float lastX = 1920.0f / 2.0;
float lastY = 1080.0f / 2.0;
float aspectRatio = 1.0f;
float mouseSmoothingFactor = 0.7;

// ---- Stereo Rendering Settings ----
float maxSeparation = 0.05f;   // Maximum stereo separation
float minSeparation = 0.0001f; // Minimum stereo separation

// The convergence will shift the zFokus but there is still some weirdness when going into negative
float minConvergence = 0.0f;  // Minimum convergence
float maxConvergence = 200.0f;   // Maximum convergence

double accumulatedXOffset = 0.0;
double accumulatedYOffset = 0.0;
bool windowHasFocus = true; // Assume focused initially
bool justRegainedFocus = false; // Flag to handle the first mouse event after focus regain
bool firstMouse = true;

// ---- GUI Settings ----
bool showGui = true;
bool showFPS = true;
bool isDarkTheme = true;
bool showInfoWindow = false;
bool showSettingsWindow = false;
bool show3DCursor = true;
bool showCursorSettingsWindow = false;
enum class SelectedType {
    None,
    Model,
    PointCloud,
    Sun
};

std::atomic<bool> isRecalculatingChunks(false);

struct SelectionState {
    SelectedType type = SelectedType::None;
    int modelIndex = -1;
    int meshIndex = -1;  // New: track selected mesh within model
};

SelectedType currentSelectedType;
int currentSelectedIndex;
int currentSelectedMeshIndex;
// Replace current selection globals with
SelectionState currentSelection;

// Now using the GUI namespace
GUI::SkyboxConfig skyboxConfig;

// ---- Preferences Structure ----
GUI::ApplicationPreferences preferences;

// ---- Scene Persistence ----
static char saveFilename[256] = "scene.json"; // Buffer for saving scene filename
static char loadFilename[256] = "scene.json"; // Buffer for loading scene filename

std::string currentPresetName = "Default";
bool isEditingPresetName = false;
char editPresetNameBuffer[256] = "";

// ---- Input and Interaction ----
bool selectionMode = false;
bool isMovingModel = false;
bool isMouseCaptured = false;
bool leftMousePressed = false;   // Left mouse button state
bool rightMousePressed = false;  // Right mouse button state
bool middleMousePressed = false; // Middle mouse button state
bool ctrlPressed = false;
double lastClickTime = 0.0;
const double doubleClickTime = 0.3; // 300 ms double-click threshold

// ---- Timing ----
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ---- Cursor System ----
Cursor::CursorManager cursorManager;
glm::vec3 capturedCursorPos;
bool orbitFollowsCursor = false;

// ---- Window Configuration ----
int windowWidth = 1920;
int windowHeight = 1080;

// ---- Lighting ----
std::vector<PointLight> pointLights;
float zOffset = 0.5f;
Sun sun = {
    glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f)), // More vertical angle
    glm::vec3(1.0f, 0.95f, 0.8f),                   // Warmer color
    0.16f,                                          // Higher intensity
    true
};

unsigned int depthMapFBO;
unsigned int depthMap;
const unsigned int SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;
Engine::Shader* simpleDepthShader = nullptr;

GUI::LightingMode currentLightingMode = GUI::LIGHTING_SHADOW_MAPPING;
bool enableShadows = true;

GUI::VCTSettings vctSettings;

// Predefined cubemap paths
std::vector<GUI::CubemapPreset> cubemapPresets = {
    {"Default", "skybox/Default/", "Default skybox environment"},
    {"Yokohama", "skybox/Yokohama/", "Yokohama, Japan. View towards Intercontinental Yokohama Grand hotel."},
    {"Storforsen", "skybox/Storforsen/", "At the top of Storforsen. Taken with long exposure, resulting in smooth looking water flow."},
    {"Yokohama Night", "skybox/YokohamaNight/", "Yokohama at night."},
    {"Lycksele", "skybox/Lycksele/", "Lycksele. View of Ansia Camping, Lycksele."}
};

Engine::Voxelizer* voxelizer = nullptr;

#pragma endregion


GLuint skyboxVAO, skyboxVBO, cubemapTexture;
float ambientStrengthFromSkybox = 0.1f;
Engine::Shader* skyboxShader = nullptr;


void window_focus_callback(GLFWwindow* window, int focused) {
    if (focused) {
        // The window gained input focus
        windowHasFocus = true;
        justRegainedFocus = true;
        firstMouse = true;
    }
    else {
        // The window lost input focus
        windowHasFocus = false;
    }
}

// Helper functions for cubemap and skybox rendering
GLuint loadCubemap(const std::vector<std::string>& faces) {
    GLuint textureID;
    glGenTextures(1, &textureID);
    glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

    int width, height, nrChannels;
    for (unsigned int i = 0; i < faces.size(); i++) {
        unsigned char* data = stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
        if (data) {
            glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB,
                width, height, 0, GL_RGB, GL_UNSIGNED_BYTE, data);
            stbi_image_free(data);
        }
        else {
            std::cerr << "Cubemap texture failed to load at path: " << faces[i] << std::endl;
            stbi_image_free(data);
        }
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

    return textureID;
}

void setupSkyboxVAO(GLuint& skyboxVAO, GLuint& skyboxVBO) {
    float skyboxVertices[] = {
        // positions          
        -1.0f,  1.0f, -1.0f,
        -1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f, -1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,

        -1.0f, -1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f, -1.0f,  1.0f,
        -1.0f, -1.0f,  1.0f,

        -1.0f,  1.0f, -1.0f,
         1.0f,  1.0f, -1.0f,
         1.0f,  1.0f,  1.0f,
         1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f,  1.0f,
        -1.0f,  1.0f, -1.0f,

        -1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f, -1.0f,
         1.0f, -1.0f, -1.0f,
        -1.0f, -1.0f,  1.0f,
         1.0f, -1.0f,  1.0f
    };

    glGenVertexArrays(1, &skyboxVAO);
    glGenBuffers(1, &skyboxVBO);

    glBindVertexArray(skyboxVAO);
    glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices, GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    std::vector<std::string> skyboxDirs = {
    "skybox",
    "skybox/Default",
    "skybox/Yokohama",
    "skybox/Storforsen",
    "skybox/YokohamaNight",
    "skybox/Lycksele"
    };

    for (const auto& dir : skyboxDirs) {
        if (!std::filesystem::exists(dir)) {
            std::filesystem::create_directory(dir);
            std::cout << "Created directory: " << dir << std::endl;
        }
    }
}

void createSolidColorSkybox(const glm::vec3& color) {
    glGenTextures(1, &cubemapTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);

    // Create a 1x1 texture for each face with the specified color
    GLubyte texData[] = {
        static_cast<GLubyte>(color.r * 255),
        static_cast<GLubyte>(color.g * 255),
        static_cast<GLubyte>(color.b * 255),
        255  // Alpha (fully opaque)
    };

    for (int i = 0; i < 6; i++) {
        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGBA, 1, 1, 0,
            GL_RGBA, GL_UNSIGNED_BYTE, texData);
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

}

// Create a gradient skybox
void createGradientSkybox(const glm::vec3& topColor, const glm::vec3& bottomColor) {
    glGenTextures(1, &cubemapTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);

    // Create a simple gradient for each face
    const int size = 128;
    std::vector<GLubyte> faceData(size * size * 3);

    for (int face = 0; face < 6; face++) {
        // Clear the face data buffer
        std::fill(faceData.begin(), faceData.end(), 0);

        // Determine which face we're working with
        // 0: right, 1: left, 2: top, 3: bottom, 4: front, 5: back

        if (face == 3) {
            // Top face - solid top color
            glm::vec3 color = topColor;
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    int idx = (y * size + x) * 3;
                    faceData[idx] = static_cast<GLubyte>(color.r * 255);
                    faceData[idx + 1] = static_cast<GLubyte>(color.g * 255);
                    faceData[idx + 2] = static_cast<GLubyte>(color.b * 255);
                }
            }
        }
        else if (face == 2) {
            // Bottom face - solid bottom color
            glm::vec3 color = bottomColor;
            for (int y = 0; y < size; y++) {
                for (int x = 0; x < size; x++) {
                    int idx = (y * size + x) * 3;
                    faceData[idx] = static_cast<GLubyte>(color.r * 255);
                    faceData[idx + 1] = static_cast<GLubyte>(color.g * 255);
                    faceData[idx + 2] = static_cast<GLubyte>(color.b * 255);
                }
            }
        }
        else {
            // Side faces - create a vertical gradient
            for (int y = 0; y < size; y++) {
                // Calculate gradient factor (0 at bottom, 1 at top)
                float factor = static_cast<float>(y) / (size - 1);

                // Interpolate between bottom and top colors
                glm::vec3 color = bottomColor * (1.0f - factor) + topColor * factor;

                for (int x = 0; x < size; x++) {
                    int idx = (y * size + x) * 3;
                    faceData[idx] = static_cast<GLubyte>(color.r * 255);
                    faceData[idx + 1] = static_cast<GLubyte>(color.g * 255);
                    faceData[idx + 2] = static_cast<GLubyte>(color.b * 255);
                }
            }
        }

        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB,
            size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, faceData.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

}

// Load a specific cubemap from path
bool loadSkyboxFromPath(const std::string& basePath) {
    // If basePath is empty, use default loading logic
    if (basePath.empty()) {
        return loadSkyboxFromPath("skybox/Default/");
    }

    // Different naming conventions to try
    struct NamingConvention {
        std::vector<std::string> faceNames;
        std::string description;
    };

    std::vector<NamingConvention> conventions = {
        // Standard naming (right, left, top, etc.)
        {
            {"right.jpg", "left.jpg", "top.jpg", "bottom.jpg", "front.jpg", "back.jpg"},
            "Standard naming"
        },
        // Directional naming (posx, negx, etc.)
        {
            {"posx.jpg", "negx.jpg", "posy.jpg", "negy.jpg", "posz.jpg", "negz.jpg"},
            "Directional naming"
        },
        // Alternative naming
        {
            {"east.jpg", "west.jpg", "up.jpg", "down.jpg", "north.jpg", "south.jpg"},
            "Cardinal directions"
        }
    };

    // Try different file extensions
    std::vector<std::string> extensions = { ".jpg", ".png", ".tga", ".bmp" };

    // Check if directory exists
    std::string fullPath = basePath;
    if (fullPath.back() != '/' && fullPath.back() != '\\') {
        fullPath += '/';
    }

    for (const auto& convention : conventions) {
        for (const auto& ext : extensions) {
            std::vector<std::string> faces;
            bool allFilesExist = true;

            // Build list of faces with current convention and extension
            for (int i = 0; i < 6; ++i) {
                std::string filename = convention.faceNames[i];

                // Replace extension if needed
                if (filename.size() > 4) {
                    std::string currentExt = filename.substr(filename.size() - 4);
                    if (currentExt == ".jpg" || currentExt == ".png" || currentExt == ".tga" || currentExt == ".bmp") {
                        filename = filename.substr(0, filename.size() - 4) + ext;
                    }
                    else {
                        filename += ext;
                    }
                }
                else {
                    filename += ext;
                }

                std::string facePath = fullPath + filename;
                faces.push_back(facePath);

                // Check if file exists
                std::ifstream f(facePath.c_str());
                if (!f.good()) {
                    allFilesExist = false;
                    break;
                }
            }

            if (allFilesExist) {
                try {
                    cubemapTexture = loadCubemap(faces);
                    std::cout << "Skybox textures loaded from: " << fullPath
                        << " using " << convention.description << std::endl;
                    return true;
                }
                catch (const std::exception& e) {
                    std::cerr << "Failed to load skybox textures from " << fullPath
                        << ": " << e.what() << std::endl;
                }
            }
        }
    }

    std::cerr << "Could not find a complete set of skybox textures in " << fullPath << std::endl;
    return false;
}

void cleanupSkybox() {
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &skyboxVBO);
    glDeleteTextures(1, &cubemapTexture);

    // Set cubemapTexture to 0 after deletion
    cubemapTexture = 0;

    // Only delete shader if it exists, then set to nullptr
    if (skyboxShader) {
        delete skyboxShader;
        skyboxShader = nullptr;
    }
}

void updateSkybox() {
    // Clean up existing skybox resources, including shader
    cleanupSkybox();

    // Setup skybox VAO again
    setupSkyboxVAO(skyboxVAO, skyboxVBO);

    // Create/load skybox based on the current type
    switch (skyboxConfig.type) {
    case GUI::SKYBOX_SOLID_COLOR:
        createSolidColorSkybox(skyboxConfig.solidColor);
        break;

    case GUI::SKYBOX_GRADIENT:
        createGradientSkybox(skyboxConfig.gradientBottomColor, skyboxConfig.gradientTopColor);
        break;

    case GUI::SKYBOX_CUBEMAP:
    default:
        // Try to load the selected cubemap
        if (skyboxConfig.selectedCubemap >= 0 &&
            skyboxConfig.selectedCubemap < cubemapPresets.size()) {

            if (!loadSkyboxFromPath(cubemapPresets[skyboxConfig.selectedCubemap].path)) {
                // Fallback to default cubemap
                createDefaultCubemap();
            }
        }
        else {
            // Fallback to default cubemap
            createDefaultCubemap();
        }
        break;
    }

    // Always (re)create the skybox shader
    try {
        skyboxShader = Engine::loadShader("skyboxVertexShader.glsl",
            "skyboxFragmentShader.glsl");
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading skybox shaders: " << e.what() << std::endl;
        skyboxShader = nullptr;  // Ensure shader is nullptr if loading fails
    }
}

void setupShadowMapping() {
    // Create multisampled depth map FBO

    glGenFramebuffers(1, &depthMapFBO);
    glGenTextures(1, &depthMap);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24,
        SHADOW_WIDTH, SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);
    float borderColor[] = { 1.0f, 1.0f, 1.0f, 1.0f };
    glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, depthMap, 0);
    glDrawBuffer(GL_NONE);
    glReadBuffer(GL_NONE);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cout << "Shadow framebuffer is not complete!" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);

    // Load shadow mapping shaders
    try {
        simpleDepthShader = Engine::loadShader("simpleDepthVertexShader.glsl", "simpleDepthFragmentShader.glsl");
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading depth shader: " << e.what() << std::endl;
    }
}

// Helper function to create a default colored cubemap when textures can't be loaded
void createDefaultCubemap() {
    glGenTextures(1, &cubemapTexture);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);

    // Create a simple gradient pattern for each face
    const int size = 128;
    std::vector<GLubyte> faceData(size * size * 3);

    // Different colors for each face
    const std::vector<glm::vec3> colors = {
        glm::vec3(1.0f, 0.5f, 0.5f), // right - red tint
        glm::vec3(0.5f, 1.0f, 0.5f), // left - green tint
        glm::vec3(0.7f, 0.7f, 1.0f), // top - blue tint
        glm::vec3(0.5f, 0.5f, 0.5f), // bottom - gray
        glm::vec3(1.0f, 1.0f, 0.5f), // front - yellow tint
        glm::vec3(0.5f, 1.0f, 1.0f)  // back - cyan tint
    };

    for (int face = 0; face < 6; face++) {
        for (int y = 0; y < size; y++) {
            for (int x = 0; x < size; x++) {
                float intensity = (float)(x + y) / (2.0f * size);
                intensity = 0.5f + 0.5f * intensity; // Keep it in [0.5, 1.0] range

                int idx = (y * size + x) * 3;
                faceData[idx] = static_cast<GLubyte>(255 * intensity * colors[face].r);
                faceData[idx + 1] = static_cast<GLubyte>(255 * intensity * colors[face].g);
                faceData[idx + 2] = static_cast<GLubyte>(255 * intensity * colors[face].b);
            }
        }

        glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face,
            0, GL_RGB, size, size, 0, GL_RGB, GL_UNSIGNED_BYTE, faceData.data());
    }

    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

void initSkybox() {
    // Load and create skybox shader
    try {
        skyboxShader = Engine::loadShader("skyboxVertexShader.glsl",
            "skyboxFragmentShader.glsl");
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading skybox shaders: " << e.what() << std::endl;
        return;
    }

    // Setup skybox VAO
    setupSkyboxVAO(skyboxVAO, skyboxVBO);

    // Load cubemap textures with multiple path options
    const std::vector<std::string> searchPaths = {
        "./assets/textures/skybox/",
        "./skybox/",
        "./assets/skybox/",
        "./textures/skybox/"
    };

    std::vector<std::string> faceNames = {
        "right.jpg", "left.jpg",
        "top.jpg", "bottom.jpg",
        "front.jpg", "back.jpg"
    };

    bool texturesLoaded = false;
    std::vector<std::string> faces;

    // Try different paths until we find the textures
    for (const auto& basePath : searchPaths) {
        faces.clear();
        for (const auto& faceName : faceNames) {
            faces.push_back(basePath + faceName);
        }

        // Check if all files exist before trying to load
        bool allFilesExist = true;
        for (const auto& face : faces) {
            std::ifstream f(face.c_str());
            if (!f.good()) {
                allFilesExist = false;
                break;
            }
        }

        if (allFilesExist) {
            try {
                cubemapTexture = loadCubemap(faces);
                texturesLoaded = true;
                std::cout << "Skybox textures loaded from: " << basePath << std::endl;
                break;
            }
            catch (const std::exception& e) {
                std::cerr << "Failed to load skybox textures from " << basePath
                    << ": " << e.what() << std::endl;
            }
        }
    }

    if (!texturesLoaded) {
        std::cerr << "Failed to load skybox textures from any path" << std::endl;
        // Create a default colored cubemap
        createDefaultCubemap();
    }
}

void renderSkybox(const glm::mat4& projection, const glm::mat4& view, Engine::Shader* mainShader) {
    // More robust check to ensure both shader and texture are valid
    if (!skyboxShader || !cubemapTexture || cubemapTexture == 0) {
        // Don't render if resources aren't properly initialized
        if (mainShader) {
            // Still set up skybox texture for the main shader, using a default value
            mainShader->use();
            mainShader->setInt("skybox", 6);
            mainShader->setFloat("skyboxIntensity", ambientStrengthFromSkybox);
        }
        return;
    }

    // Save current OpenGL state
    GLint previousDepthFunc;
    glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);

    // Change depth function so depth test passes when values are equal to depth buffer's content
    glDepthFunc(GL_LEQUAL);

    skyboxShader->use();

    // Remove translation from view matrix for skybox
    glm::mat4 skyView = glm::mat4(glm::mat3(view)); // Remove translation

    skyboxShader->setMat4("projection", projection);
    skyboxShader->setMat4("view", skyView);

    // Render skybox
    glBindVertexArray(skyboxVAO);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
    glDrawArrays(GL_TRIANGLES, 0, 36);

    // Reset to previous depth function
    glDepthFunc(previousDepthFunc);

    // Set up skybox texture for the main shader
    if (mainShader) {
        mainShader->use();
        glActiveTexture(GL_TEXTURE6); // Use a different texture unit than other textures
        glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
        mainShader->setInt("skybox", 6);
        mainShader->setFloat("skyboxIntensity", ambientStrengthFromSkybox);
    }

    // Reset state
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
}


// Function to bind skybox uniforms
void bindSkyboxUniforms(Engine::Shader* shader) {
    shader->setFloat("skyboxIntensity", ambientStrengthFromSkybox);
    shader->setInt("skybox", 6);  // Skybox texture unit
}

void savePreferences() {
    json j;

    // UI preferences
    j["ui"]["darkTheme"] = preferences.isDarkTheme;
    j["ui"]["showFPS"] = preferences.showFPS;
    j["ui"]["show3DCursor"] = preferences.show3DCursor;

    // Camera settings
    j["camera"]["separation"] = preferences.separation;
    j["camera"]["convergence"] = preferences.convergence;
    j["camera"]["nearPlane"] = preferences.nearPlane;
    j["camera"]["farPlane"] = preferences.farPlane;
    j["camera"]["speedFactor"] = preferences.cameraSpeedFactor;
    j["camera"]["useNewStereoMethod"] = preferences.useNewStereoMethod;
    j["camera"]["fov"] = preferences.fov;
    j["camera"]["scrollMomentum"] = preferences.scrollMomentum;
    j["camera"]["maxScrollVelocity"] = preferences.maxScrollVelocity;
    j["camera"]["scrollDeceleration"] = preferences.scrollDeceleration;
    j["camera"]["useSmoothScrolling"] = preferences.useSmoothScrolling;
    j["camera"]["zoomToCursor"] = preferences.zoomToCursor;
    j["camera"]["orbitAroundCursor"] = preferences.orbitAroundCursor;
    j["camera"]["orbitFollowsCursor"] = preferences.orbitFollowsCursor;
    j["camera"]["mouseSmoothingFactor"] = preferences.mouseSmoothingFactor;
    j["camera"]["mouseSensitivity"] = preferences.mouseSensitivity;

    // Cursor settings
    j["cursor"]["currentPreset"] = preferences.currentPresetName;

    j["skybox"]["type"] = skyboxConfig.type;
    j["skybox"]["solidColor"] = {
        skyboxConfig.solidColor.r,
        skyboxConfig.solidColor.g,
        skyboxConfig.solidColor.b
    };
    j["skybox"]["gradientTop"] = {
        skyboxConfig.gradientTopColor.r,
        skyboxConfig.gradientTopColor.g,
        skyboxConfig.gradientTopColor.b
    };
    j["skybox"]["gradientBottom"] = {
        skyboxConfig.gradientBottomColor.r,
        skyboxConfig.gradientBottomColor.g,
        skyboxConfig.gradientBottomColor.b
    };
    j["skybox"]["selectedCubemap"] = skyboxConfig.selectedCubemap;

    // Update preferences struct
    preferences.skyboxType = static_cast<int>(skyboxConfig.type);
    preferences.skyboxSolidColor = skyboxConfig.solidColor;
    preferences.skyboxGradientTop = skyboxConfig.gradientTopColor;
    preferences.skyboxGradientBottom = skyboxConfig.gradientBottomColor;
    preferences.selectedCubemap = skyboxConfig.selectedCubemap;

    // Save to file
    std::ofstream file("preferences.json");
    if (file.is_open()) {
        file << std::setw(4) << j << std::endl;
        file.close();
    }
    else {
        std::cerr << "Failed to save preferences" << std::endl;
    }
}

void applyPreferencesToProgram() {
    // Apply UI preferences
    isDarkTheme = preferences.isDarkTheme;
    SetupImGuiStyle(isDarkTheme, 1.0f);
    showFPS = preferences.showFPS;
    show3DCursor = preferences.show3DCursor;

    // Apply camera preferences
    currentScene.settings.separation = preferences.separation;
    currentScene.settings.convergence = preferences.convergence;
    currentScene.settings.nearPlane = preferences.nearPlane;
    currentScene.settings.farPlane = preferences.farPlane;
    camera.useNewMethod = preferences.useNewStereoMethod;
    camera.Zoom = preferences.fov;
    camera.scrollMomentum = preferences.scrollMomentum;
    camera.maxScrollVelocity = preferences.maxScrollVelocity;
    camera.scrollDeceleration = preferences.scrollDeceleration;
    camera.useSmoothScrolling = preferences.useSmoothScrolling;
    camera.zoomToCursor = preferences.zoomToCursor;
    camera.orbitAroundCursor = preferences.orbitAroundCursor;
    camera.speedFactor = preferences.cameraSpeedFactor;
    orbitFollowsCursor = preferences.orbitFollowsCursor;
    mouseSmoothingFactor = preferences.mouseSmoothingFactor;
    camera.MouseSensitivity = preferences.mouseSensitivity;

    skyboxConfig.type = static_cast<GUI::SkyboxType>(preferences.skyboxType);
    skyboxConfig.solidColor = preferences.skyboxSolidColor;
    skyboxConfig.gradientTopColor = preferences.skyboxGradientTop;
    skyboxConfig.gradientBottomColor = preferences.skyboxGradientBottom;
    skyboxConfig.selectedCubemap = preferences.selectedCubemap;

    updateSkybox();

    // Load cursor preset
    currentPresetName = preferences.currentPresetName;
    if (!currentPresetName.empty()) {
        try {
            Engine::CursorPreset loadedPreset = Engine::CursorPresetManager::applyCursorPreset(currentPresetName);

            // Apply preset to cursor manager
            auto* sphereCursor = cursorManager.getSphereCursor();
            auto* fragmentCursor = cursorManager.getFragmentCursor();
            auto* planeCursor = cursorManager.getPlaneCursor();

            // Sphere cursor settings
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

            // Fragment cursor settings
            fragmentCursor->setVisible(loadedPreset.showFragmentCursor);
            fragmentCursor->setBaseInnerRadius(loadedPreset.fragmentBaseInnerRadius);

            // Plane cursor settings
            planeCursor->setVisible(loadedPreset.showPlaneCursor);
            planeCursor->setDiameter(loadedPreset.planeDiameter);
            planeCursor->setColor(loadedPreset.planeColor);
        }
        catch (const std::exception& e) {
            std::cerr << "Error loading cursor preset: " << e.what() << std::endl;
            // If preset doesn't exist, we might want to create it
            if (currentPresetName == "Sphere" &&
                Engine::CursorPresetManager::getPresetNames().empty()) {
                // Create the Sphere preset if it doesn't exist
                Engine::CursorPreset spherePreset;
                spherePreset.name = "Sphere";
                spherePreset.showSphereCursor = true;
                spherePreset.showFragmentCursor = false;
                spherePreset.fragmentBaseInnerRadius = 0.004f;
                spherePreset.sphereScalingMode = static_cast<int>(GUI::CURSOR_CONSTRAINED_DYNAMIC);
                spherePreset.sphereFixedRadius = 0.05f;
                spherePreset.sphereTransparency = 0.7f;
                spherePreset.showInnerSphere = false;
                spherePreset.cursorColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.7f);
                spherePreset.innerSphereColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
                spherePreset.innerSphereFactor = 0.1f;
                spherePreset.cursorEdgeSoftness = 0.8f;
                spherePreset.cursorCenterTransparency = 0.2f;
                spherePreset.showPlaneCursor = false;
                spherePreset.planeDiameter = 0.5f;
                spherePreset.planeColor = glm::vec4(0.0f, 1.0f, 0.0f, 0.7f);

                Engine::CursorPresetManager::savePreset("Sphere", spherePreset);

                // Apply preset to cursor manager
                auto* sphereCursor = cursorManager.getSphereCursor();
                auto* fragmentCursor = cursorManager.getFragmentCursor();
                auto* planeCursor = cursorManager.getPlaneCursor();

                // Sphere cursor settings
                sphereCursor->setVisible(spherePreset.showSphereCursor);
                sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(spherePreset.sphereScalingMode));
                sphereCursor->setFixedRadius(spherePreset.sphereFixedRadius);
                sphereCursor->setTransparency(spherePreset.sphereTransparency);
                sphereCursor->setShowInnerSphere(spherePreset.showInnerSphere);
                sphereCursor->setColor(spherePreset.cursorColor);
                sphereCursor->setInnerSphereColor(spherePreset.innerSphereColor);
                sphereCursor->setInnerSphereFactor(spherePreset.innerSphereFactor);
                sphereCursor->setEdgeSoftness(spherePreset.cursorEdgeSoftness);
                sphereCursor->setCenterTransparency(spherePreset.cursorCenterTransparency);

                // Fragment cursor settings
                fragmentCursor->setVisible(spherePreset.showFragmentCursor);
                fragmentCursor->setBaseInnerRadius(spherePreset.fragmentBaseInnerRadius);

                // Plane cursor settings
                planeCursor->setVisible(spherePreset.showPlaneCursor);
                planeCursor->setDiameter(spherePreset.planeDiameter);
                planeCursor->setColor(spherePreset.planeColor);
            }
        }
    }
}

void loadPreferences() {
    std::ifstream file("preferences.json");
    bool fileExists = file.is_open();

    // Initialize with default values first
    preferences = GUI::ApplicationPreferences(); // This uses the default values from the struct

    // Apply these defaults to the actual variables
    applyPreferencesToProgram();

    // If no file exists, we're done - defaults have been applied
    if (!fileExists) {
        std::cout << "No preferences file found, using defaults" << std::endl;
        return;
    }

    // If file exists, load values from it
    try {
        json j;
        file >> j;

        // UI preferences
        if (j.contains("ui")) {
            preferences.isDarkTheme = j["ui"].value("darkTheme", true);
            preferences.showFPS = j["ui"].value("showFPS", true);
            preferences.show3DCursor = j["ui"].value("show3DCursor", true);
        }

        // Camera settings
        if (j.contains("camera")) {
            preferences.separation = j["camera"].value("separation", 0.005f);
            preferences.convergence = j["camera"].value("convergence", 1.5f);
            preferences.nearPlane = j["camera"].value("nearPlane", 0.1f);
            preferences.farPlane = j["camera"].value("farPlane", 200.0f);
            preferences.cameraSpeedFactor = j["camera"].value("speedFactor", 1.0f);
            preferences.useNewStereoMethod = j["camera"].value("useNewStereoMethod", true);
            preferences.fov = j["camera"].value("fov", 45.0f);
            preferences.scrollMomentum = j["camera"].value("scrollMomentum", 0.5f);
            preferences.maxScrollVelocity = j["camera"].value("maxScrollVelocity", 3.0f);
            preferences.scrollDeceleration = j["camera"].value("scrollDeceleration", 10.0f);
            preferences.useSmoothScrolling = j["camera"].value("useSmoothScrolling", true);
            preferences.zoomToCursor = j["camera"].value("zoomToCursor", true);
            preferences.orbitAroundCursor = j["camera"].value("orbitAroundCursor", true);
            preferences.orbitFollowsCursor = j["camera"].value("orbitFollowsCursor", false);
            preferences.mouseSmoothingFactor = j["camera"].value("mouseSmoothingFactor", 1.0f);
            preferences.mouseSensitivity = j["camera"].value("mouseSensitivity", 0.17f);
        }

        if (j.contains("skybox")) {
            preferences.skyboxType = j["skybox"].value("type", static_cast<int>(GUI::SKYBOX_CUBEMAP));

            if (j["skybox"].contains("solidColor")) {
                auto& color = j["skybox"]["solidColor"];
                preferences.skyboxSolidColor = glm::vec3(
                    color[0].get<float>(),
                    color[1].get<float>(),
                    color[2].get<float>()
                );
            }

            if (j["skybox"].contains("gradientTop")) {
                auto& color = j["skybox"]["gradientTop"];
                preferences.skyboxGradientTop = glm::vec3(
                    color[0].get<float>(),
                    color[1].get<float>(),
                    color[2].get<float>()
                );
            }

            if (j["skybox"].contains("gradientBottom")) {
                auto& color = j["skybox"]["gradientBottom"];
                preferences.skyboxGradientBottom = glm::vec3(
                    color[0].get<float>(),
                    color[1].get<float>(),
                    color[2].get<float>()
                );
            }

            preferences.selectedCubemap = j["skybox"].value("selectedCubemap", 0);
        }

        // Cursor settings
        if (j.contains("cursor")) {
            preferences.currentPresetName = j["cursor"].value("currentPreset", "Sphere");
        }

        // Apply loaded preferences
        applyPreferencesToProgram();
    }
    catch (const std::exception& e) {
        std::cerr << "Error loading preferences: " << e.what() << std::endl;
        // If an error occurs, we already applied defaults above
    }

    file.close();
}

void InitializeDefaults() {
    // Set up default values
    preferences = GUI::ApplicationPreferences();

    // Initialize the camera with default values from preferences
    camera.useNewMethod = preferences.useNewStereoMethod;
    camera.Zoom = preferences.fov;
    camera.scrollMomentum = preferences.scrollMomentum;
    camera.maxScrollVelocity = preferences.maxScrollVelocity;
    camera.scrollDeceleration = preferences.scrollDeceleration;
    camera.useSmoothScrolling = preferences.useSmoothScrolling;
    camera.zoomToCursor = preferences.zoomToCursor;
    camera.orbitAroundCursor = preferences.orbitAroundCursor;
    camera.speedFactor = preferences.cameraSpeedFactor;
    camera.MouseSensitivity = preferences.mouseSensitivity;

    // Set global variables
    orbitFollowsCursor = preferences.orbitFollowsCursor;
    mouseSmoothingFactor = preferences.mouseSmoothingFactor;
    isDarkTheme = preferences.isDarkTheme;
    showFPS = preferences.showFPS;
    show3DCursor = preferences.show3DCursor;

    // Apply to scene settings
    currentScene.settings.separation = preferences.separation;
    currentScene.settings.convergence = preferences.convergence;
    currentScene.settings.nearPlane = preferences.nearPlane;
    currentScene.settings.farPlane = preferences.farPlane;

    // Set up default cursor preset if needed
    if (Engine::CursorPresetManager::getPresetNames().empty()) {
        // Create and save the default Sphere preset
        Engine::CursorPreset spherePreset;
        spherePreset.name = "Sphere";
        spherePreset.showSphereCursor = true;
        spherePreset.showFragmentCursor = false;
        spherePreset.fragmentBaseInnerRadius = 0.004f;
        spherePreset.sphereScalingMode = static_cast<int>(GUI::CURSOR_CONSTRAINED_DYNAMIC);
        spherePreset.sphereFixedRadius = 0.05f;
        spherePreset.sphereTransparency = 0.7f;
        spherePreset.showInnerSphere = false;
        spherePreset.cursorColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.7f);
        spherePreset.innerSphereColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
        spherePreset.innerSphereFactor = 0.1f;
        spherePreset.cursorEdgeSoftness = 0.8f;
        spherePreset.cursorCenterTransparency = 0.2f;
        spherePreset.showPlaneCursor = false;
        spherePreset.planeDiameter = 0.5f;
        spherePreset.planeColor = glm::vec4(0.0f, 1.0f, 0.0f, 0.7f);

        skyboxConfig.type = GUI::SKYBOX_CUBEMAP;
        skyboxConfig.solidColor = glm::vec3(0.2f, 0.3f, 0.4f);
        skyboxConfig.gradientTopColor = glm::vec3(0.1f, 0.1f, 0.3f);
        skyboxConfig.gradientBottomColor = glm::vec3(0.7f, 0.7f, 1.0f);
        skyboxConfig.selectedCubemap = 0;

        // default cubemap presets with descriptions
        if (cubemapPresets.empty()) {
            cubemapPresets = {
                {"Default", "skybox/Default/", "Default skybox environment"},
                {"Yokohama", "skybox/Yokohama/", "Yokohama, Japan. View towards Intercontinental Yokohama Grand hotel."},
                {"Storforsen", "skybox/Storforsen/", "At the top of Storforsen. Taken with long exposure, resulting in smooth looking water flow."},
                {"Yokohama Night", "skybox/YokohamaNight/", "Yokohama at night."},
                {"Lycksele", "skybox/Lycksele/", "Lycksele. View of Ansia Camping, Lycksele."}
            };
        }

        Engine::CursorPresetManager::savePreset("Sphere", spherePreset);
        currentPresetName = "Sphere";

        // Apply preset to cursor manager
        auto* sphereCursor = cursorManager.getSphereCursor();
        auto* fragmentCursor = cursorManager.getFragmentCursor();
        auto* planeCursor = cursorManager.getPlaneCursor();

        // Sphere cursor settings
        sphereCursor->setVisible(spherePreset.showSphereCursor);
        sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(spherePreset.sphereScalingMode));
        sphereCursor->setFixedRadius(spherePreset.sphereFixedRadius);
        sphereCursor->setTransparency(spherePreset.sphereTransparency);
        sphereCursor->setShowInnerSphere(spherePreset.showInnerSphere);
        sphereCursor->setColor(spherePreset.cursorColor);
        sphereCursor->setInnerSphereColor(spherePreset.innerSphereColor);
        sphereCursor->setInnerSphereFactor(spherePreset.innerSphereFactor);
        sphereCursor->setEdgeSoftness(spherePreset.cursorEdgeSoftness);
        sphereCursor->setCenterTransparency(spherePreset.cursorCenterTransparency);

        // Fragment cursor settings
        fragmentCursor->setVisible(spherePreset.showFragmentCursor);
        fragmentCursor->setBaseInnerRadius(spherePreset.fragmentBaseInnerRadius);

        // Plane cursor settings
        planeCursor->setVisible(spherePreset.showPlaneCursor);
        planeCursor->setDiameter(spherePreset.planeDiameter);
        planeCursor->setColor(spherePreset.planeColor);
    }

    // Set ImGui style
    SetupImGuiStyle(isDarkTheme, 1.0f);
}


void PerspectiveProjection(GLfloat* frustum, GLfloat dir,
    GLfloat fovy, GLfloat aspect,
    GLfloat znear, GLfloat zfar,
    GLfloat eyesep, GLfloat focaldist)
{
    GLfloat h_half = tanf(glm::radians(fovy / 2.0f));
    GLfloat w_half = h_half * aspect;

    frustum[0] = -w_half * focaldist - ((eyesep / 2.0f) * dir);
    frustum[1] = w_half * focaldist - ((eyesep / 2.0f) * dir);
    frustum[0] = (frustum[0] / focaldist) * znear;
    frustum[1] = (frustum[1] / focaldist) * znear;

    frustum[2] = -h_half * znear;
    frustum[3] = h_half * znear;
    frustum[4] = znear;
    frustum[5] = zfar;
}

int main() {
    // ---- Initialize GLFW ----
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW" << std::endl;
        return -1;
    }

    // ---- Set OpenGL Version and Profile ----
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_STEREO, GLFW_TRUE);  // Enable stereo hint
    glfwWindowHint(GLFW_SAMPLES, currentScene.settings.msaaSamples);

    // ---- Create GLFW Window ----
    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "OpenGL Stereo Template", nullptr, nullptr);
    bool isStereoWindow = (window != nullptr);

    if (!isStereoWindow) {
        std::cout << "Failed to create stereo GLFW window, falling back to mono rendering." << std::endl;
        glfwWindowHint(GLFW_STEREO, GLFW_FALSE);
        window = glfwCreateWindow(windowWidth, windowHeight, "OpenGL Mono Template", nullptr, nullptr);

        if (window == nullptr) {
            std::cout << "Failed to create GLFW window" << std::endl;
            glfwTerminate();
            return -1;
        }
    }

    glfwMakeContextCurrent(window);
    Window::nativeWindow = window;


    if (glfwRawMouseMotionSupported()) {
        glfwSetInputMode(window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
        std::cout << "Raw mouse motion enabled." << std::endl;
    }
    else {
        std::cout << "Raw mouse motion not supported." << std::endl;
    }

    // ---- Initialize GLAD ----
    if (!gladLoadGLLoader((GLADloadproc)glfwGetProcAddress)) {
        std::cout << "Failed to initialize GLAD" << std::endl;
        glfwTerminate();
        return -1;
    }

    glEnable(GL_MULTISAMPLE);

    // ---- Set GLFW Callbacks ----
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);
    glfwSetWindowFocusCallback(window, window_focus_callback);


    voxelizer = new Engine::Voxelizer(128);

    // ---- Initialize Shader ----
    Engine::Shader* shader = nullptr;
    try {
        shader = Engine::loadShader("vertexShader.glsl", "fragmentShader.glsl");
    }
    catch (std::exception& e) {
        std::cout << e.what() << std::endl;
        glfwTerminate();
        return -1;
    }

    // ---- Load Default cube ----
    Engine::Model cube = Engine::createCube(glm::vec3(1.0f, 1.0f, 1.0f), 1.0f, 0.0f);
    cube.scale = glm::vec3(0.5f);
    cube.name = "Default_Cube";
    cube.position = glm::vec3(0.0f, 0.0f, 0.0f);
    cube.emissive = 0.5f;
    currentScene.models.push_back(cube);
    currentModelIndex = 0;

    camera.centeringCompletedCallback = [&]() {

        if (orbitFollowsCursor) {
            camera.SetOrbitPointDirectly(capturedCursorPos);
            camera.StartOrbiting();

        }
        };

    // ---- Init Preset ----
    if (Engine::CursorPresetManager::getPresetNames().empty()) {
        // Create and save a default preset
        Engine::CursorPreset defaultPreset;
        Engine::CursorPresetManager::savePreset("Default", defaultPreset);
    }
    currentPresetName = Engine::CursorPresetManager::getPresetNames().front();

    // Initialize cursor manager
    cursorManager.initialize();

    setupShadowMapping();
    setupSkyboxVAO(skyboxVAO, skyboxVBO);

    // ---- Calculate Largest Model Dimension ----
    float largestDimension = calculateLargestModelDimension();

    // ---- Initialize ImGui ----
    if (!InitializeGUI(window, isDarkTheme)) {
        std::cerr << "Failed to initialize GUI" << std::endl;
        return -1;
    }

    // Configure additional ImGui settings
    ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    vctSettings.indirectSpecularLight = true;
    vctSettings.indirectDiffuseLight = true;
    vctSettings.directLight = true;
    vctSettings.shadows = true;
    vctSettings.voxelSize = 1.0f / 64.0f;

    InitializeDefaults();

    loadPreferences();

    // ---- OpenGL Settings ----
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glfwSwapInterval(1); // Enable vsync

    // ---- Main Loop ----
    while (!glfwWindowShouldClose(window)) {
        // ---- Per-frame Time Logic ----
        float currentFrame = static_cast<float>(glfwGetTime()); // Use static_cast for clarity
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        // Prevent division by zero or negative delta time issues
        if (deltaTime <= 0.0f) {
            deltaTime = 0.0001f; // Assign a very small positive value if frame time is zero or negative
        }

        // ---- Process Events ----
        // This will call callbacks like mouse_callback, key_callback etc.
        // mouse_callback will now update `accumulatedXOffset` and `accumulatedYOffset` if mouse is captured.
        glfwPollEvents();

        // --- Process Accumulated Mouse Input (Once Per Frame) ---
        // Check if mouse is captured and if there's any accumulated movement to process
        if (isMouseCaptured && windowHasFocus && !ImGui::GetIO().WantCaptureMouse && (accumulatedXOffset != 0.0 || accumulatedYOffset != 0.0)) {

            // Use the *total* accumulated offset for this frame
            float totalXOffset = static_cast<float>(accumulatedXOffset);
            float totalYOffset = static_cast<float>(accumulatedYOffset);

            // --- Process Movement Based on Active Mode ---
            if (isMovingModel && currentSelectedType == SelectedType::Model && currentSelectedIndex != -1)
            {
                // Calculate sensitivity based on distance to the model
                float distanceToModel = glm::distance(camera.Position, currentScene.models[currentSelectedIndex].position);
                distanceToModel = glm::max(distanceToModel, 0.1f); // Prevent sensitivity from becoming zero or negative

                // Normalize the raw accumulated offset relative to window size for consistent feel
                // This makes the movement speed less dependent on raw pixel offset values
                float normalizedXOffset = totalXOffset / static_cast<float>(windowWidth);
                float normalizedYOffset = totalYOffset / static_cast<float>(windowHeight);

                // Define base sensitivity for model dragging (tune this value)
                float baseSensitivity = 0.71f; // Increased base sensitivity might feel better for normalized input
                float sensitivityFactor = baseSensitivity * distanceToModel; // Scale sensitivity by distance

                // Adjust horizontal movement by aspect ratio
                normalizedXOffset *= aspectRatio;

                // Determine movement directions based on camera's current orientation
                glm::vec3 rightDir = glm::normalize(glm::cross(camera.Front, camera.Up));
                glm::vec3 upDir = camera.Up; // Use camera's up vector for vertical movement

                // Apply the movement to the selected model's position
                currentScene.models[currentSelectedIndex].position += rightDir * normalizedXOffset * sensitivityFactor;
                currentScene.models[currentSelectedIndex].position += upDir * normalizedYOffset * sensitivityFactor;
            }
            else if ((camera.IsOrbiting || camera.IsPanning || rightMousePressed) && !camera.IsAnimating)
            {
                // Pass the accumulated offsets directly to the camera's processing function.
                // The Camera class handles applying its own MouseSensitivity and determining
                // whether to Orbit, Pan, or Free-Look based on its internal state (IsOrbiting, IsPanning).
                camera.ProcessMouseMovement(totalXOffset, totalYOffset);
            }

            // --- Reset accumulators for the next frame ---
            accumulatedXOffset = 0.0;
            accumulatedYOffset = 0.0;
        }
        else {
            // Ensure accumulators are zero if not processing (capture stopped, lost focus, etc.)
            accumulatedXOffset = 0.0;
            accumulatedYOffset = 0.0;
        }


        // ---- Handle Keyboard Input ----
        // Process continuous key presses (like WASD movement) after event polling.
        Input::handleKeyInput(camera, deltaTime); // Make sure handleKeyInput uses deltaTime

        // ---- Update Camera State ----
        // Update smooth scrolling deceleration, centering animation etc.
        camera.UpdateScrolling(deltaTime);
        camera.UpdateAnimation(deltaTime);

        // ---- Calculate View and Projection ----
        glm::mat4 view = camera.GetViewMatrix();

        // Hack malo - Ensure valid window dimensions
        if (windowWidth <= 0 || windowHeight <= 0) {
            // Get current framebuffer size as a fallback
            glfwGetFramebufferSize(window, &windowWidth, &windowHeight);
            if (windowWidth <= 0 || windowHeight <= 0) {
                // If still invalid, use defaults to prevent division by zero
                windowWidth = 1920;
                windowHeight = 1080;
                glViewport(0, 0, windowWidth, windowHeight); // Reset viewport explicitly
            }
        }

        aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
        glm::mat4 projection = camera.GetProjectionMatrix(aspectRatio, currentScene.settings.nearPlane, currentScene.settings.farPlane);

        // Calculate stereo projections if needed
        glm::mat4 leftProjection = projection;
        glm::mat4 rightProjection = projection;
        glm::mat4 leftView = view;
        glm::mat4 rightView = view;

        if (isStereoWindow) {
            if (!camera.useNewMethod) {
                // Original method using offsetProjection
                leftProjection = camera.offsetProjection(projection, currentScene.settings.separation / 2.0f,
                    currentScene.settings.convergence);
                rightProjection = camera.offsetProjection(projection, -currentScene.settings.separation / 2.0f,
                    currentScene.settings.convergence);
            }
            else {
                // New method using asymmetric frustums and view offset
                GLfloat frustum[6];
                float effectiveSeparation = currentScene.settings.separation;

                PerspectiveProjection(frustum, -1.0f, camera.Zoom, aspectRatio,
                    currentScene.settings.nearPlane, currentScene.settings.farPlane,
                    effectiveSeparation, currentScene.settings.convergence);
                leftProjection = glm::frustum(frustum[0], frustum[1], frustum[2], frustum[3], frustum[4], frustum[5]);

                PerspectiveProjection(frustum, +1.0f, camera.Zoom, aspectRatio,
                    currentScene.settings.nearPlane, currentScene.settings.farPlane,
                    effectiveSeparation, currentScene.settings.convergence);
                rightProjection = glm::frustum(frustum[0], frustum[1], frustum[2], frustum[3], frustum[4], frustum[5]);

                // Calculate offset view matrices
                glm::vec3 pos = camera.Position;
                glm::vec3 rightVec = camera.Right;
                glm::vec3 upVec = camera.Up;
                glm::vec3 frontVec = camera.Front;

                glm::vec3 leftEyePos = pos - (rightVec * effectiveSeparation / 2.0f);
                leftView = glm::lookAt(leftEyePos, leftEyePos + frontVec, upVec);

                glm::vec3 rightEyePos = pos + (rightVec * effectiveSeparation / 2.0f);
                rightView = glm::lookAt(rightEyePos, rightEyePos + frontVec, upVec);
            }
        }

        // ---- Update Scene State ----
        // Set wireframe mode before rendering
        glPolygonMode(GL_FRONT_AND_BACK, camera.wireframe ? GL_LINE : GL_FILL);

        // Adjust camera movement speed based on distance
        float distanceToNearestObject = camera.getDistanceToNearestObject(camera, projection, view, currentScene.settings.farPlane, windowWidth, windowHeight);
        // Update camera's internal distance info AFTER getting it
        camera.UpdateDistanceToObject(distanceToNearestObject);
        // Now adjust speed using the updated internal value
        camera.AdjustMovementSpeed(distanceToNearestObject, largestDimension, currentScene.settings.farPlane); // Assuming largestDimension is calculated elsewhere

        // ---- Rendering ----
        if (isStereoWindow) {
            // Render left eye to left buffer
            renderEye(GL_BACK_LEFT, leftProjection, leftView, shader, viewport, windowFlags, window);
            // Render right eye to right buffer
            renderEye(GL_BACK_RIGHT, rightProjection, rightView, shader, viewport, windowFlags, window);
        }
        else {
            // Render mono view to default buffer (usually GL_BACK_LEFT or just GL_BACK)
            renderEye(GL_BACK_LEFT, projection, view, shader, viewport, windowFlags, window); // Or use GL_BACK if not explicitly stereo
        }

        // ---- Swap Buffers ----
        glfwSwapBuffers(window);
    }

    // ---- Cleanup ----
    cleanup(shader);

    return 0;
}


// ---- Initialization and Cleanup -----
#pragma region Initialization and Cleanup
void cleanup(Engine::Shader* shader) {
    // Delete cursor manager resources
    cursorManager.cleanup();

    // Delete point cloud resources
    for (auto& pointCloud : currentScene.pointClouds) {
        for (auto& chunk : pointCloud.chunks) {
            glDeleteBuffers(chunk.lodVBOs.size(), chunk.lodVBOs.data());
        }
        glDeleteBuffers(1, &pointCloud.instanceVBO);
        glDeleteVertexArrays(1, &pointCloud.vao);
    }

    // Delete skybox resources
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &skyboxVBO);
    glDeleteTextures(1, &cubemapTexture);
    delete skyboxShader;

    // Delete shadow mapping resources
    glDeleteFramebuffers(1, &depthMapFBO);
    glDeleteTextures(1, &depthMap);
    delete simpleDepthShader;

    // Delete shader
    delete shader;

    // Clean up GUI and GLFW resources
    CleanupGUI();
    glfwTerminate();
}

void terminateGLFW() {
    glfwDestroyWindow(Window::nativeWindow);
}

float calculateLargestModelDimension() {
    if (currentScene.models.empty()) return 1.0f;

    glm::vec3 minBounds(std::numeric_limits<float>::max());
    glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

    // Loop through all meshes in the first model
    for (const auto& mesh : currentScene.models[0].getMeshes()) {
        for (const auto& vertex : mesh.vertices) {
            minBounds = glm::min(minBounds, vertex.position);
            maxBounds = glm::max(maxBounds, vertex.position);
        }
    }

    glm::vec3 modelsize = maxBounds - minBounds;
    return glm::max(glm::max(modelsize.x, modelsize.y), modelsize.z);
}
#pragma endregion


// ---- Rendering ----
#pragma region Rendering
void renderEye(GLenum drawBuffer, const glm::mat4& projection, const glm::mat4& view, Engine::Shader* shader, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, GLFWwindow* window) {
    // Set the draw buffer and clear color and depth buffers
    glDrawBuffer(drawBuffer);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Reset OpenGL state
    glUseProgram(0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_3D, 0);

    // 1. Update the voxel grid if voxel visualization is enabled or we're using voxel cone tracing
    if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING || voxelizer->showDebugVisualization) {
        voxelizer->update(camera.Position, currentScene.models);
    }

    // 2. Shadow mapping pass (only if using shadow mapping AND shadows are enabled)
    if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING && enableShadows) {
        glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
        glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
        glClear(GL_DEPTH_BUFFER_BIT);

        // Calculate light's view and projection matrices
        float sceneRadius = 10.0f;  // Adjust based on your scene size
        glm::vec3 sceneCenter = glm::vec3(0.0f);
        glm::vec3 lightDir = glm::normalize(sun.direction);
        glm::vec3 lightPos = sceneCenter - lightDir * (sceneRadius * 2.0f);

        glm::mat4 lightProjection = glm::ortho(
            -sceneRadius, sceneRadius,
            -sceneRadius, sceneRadius,
            0.0f, sceneRadius * 4.0f);

        glm::mat4 lightView = glm::lookAt(
            lightPos,
            sceneCenter,
            glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightSpaceMatrix = lightProjection * lightView;

        // Use depth shader for shadow map generation
        simpleDepthShader->use();
        simpleDepthShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);

        // Render scene to depth buffer
        glDisable(GL_CULL_FACE);
        renderModels(simpleDepthShader);
        glEnable(GL_CULL_FACE);
    }

    // 3. Regular rendering pass
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowWidth, windowHeight);

    shader->use();
    shader->setMat4("projection", projection);
    shader->setMat4("view", view);
    shader->setVec3("viewPos", camera.Position);

    // Set lighting mode uniforms - this is always needed
    shader->setInt("lightingMode", static_cast<int>(currentLightingMode));
    shader->setBool("enableShadows", enableShadows);

    // Set common light properties - these are needed for both modes
    shader->setVec3("sun.direction", sun.direction);
    shader->setVec3("sun.color", sun.color);
    shader->setFloat("sun.intensity", sun.intensity);
    shader->setBool("sun.enabled", sun.enabled);

    // Shadow mapping specific setup
    if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
        // Calculate light space matrix for shadow mapping
        float sceneRadius = 10.0f;
        glm::vec3 sceneCenter = glm::vec3(0.0f);
        glm::vec3 lightDir = glm::normalize(sun.direction);
        glm::vec3 lightPos = sceneCenter - lightDir * (sceneRadius * 2.0f);

        glm::mat4 lightProjection = glm::ortho(
            -sceneRadius, sceneRadius,
            -sceneRadius, sceneRadius,
            0.0f, sceneRadius * 4.0f);

        glm::mat4 lightView = glm::lookAt(
            lightPos,
            sceneCenter,
            glm::vec3(0.0f, 1.0f, 0.0f));
        glm::mat4 lightSpaceMatrix = lightProjection * lightView;

        shader->setMat4("lightSpaceMatrix", lightSpaceMatrix);

        // Bind shadow map if shadows are enabled
        if (enableShadows) {
            glActiveTexture(GL_TEXTURE4);  // Using texture unit 4 for shadow map
            glBindTexture(GL_TEXTURE_2D, depthMap);
            shader->setInt("shadowMap", 4);
        }

        // Update point lights (for shadow mapping mode)
        updatePointLights();

        // Set point light uniforms
        for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
            std::string lightName = "lights[" + std::to_string(i) + "]";
            shader->setVec3(lightName + ".position", pointLights[i].position);
            shader->setVec3(lightName + ".color", pointLights[i].color);
            shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
        }
        shader->setInt("numLights", std::min((int)pointLights.size(), MAX_LIGHTS));
    }
    // Voxel cone tracing specific setup
    else if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
        // Set voxel grid parameters - only when in VCT mode
        float halfSize = voxelizer->getVoxelGridSize() * 0.5f;
        shader->setVec3("gridMin", glm::vec3(-halfSize));
        shader->setVec3("gridMax", glm::vec3(halfSize));
        shader->setFloat("voxelSize", vctSettings.voxelSize);

        // Set VCT settings - only when in VCT mode
        shader->setBool("vctSettings.indirectSpecularLight", vctSettings.indirectSpecularLight);
        shader->setBool("vctSettings.indirectDiffuseLight", vctSettings.indirectDiffuseLight);
        shader->setBool("vctSettings.directLight", vctSettings.directLight);
        shader->setBool("vctSettings.shadows", vctSettings.shadows);

        // Bind voxel 3D texture - using texture unit 5
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_3D, voxelizer->getVoxelTexture());
        shader->setInt("voxelGrid", 5);

        // Set default material properties for voxel cone tracing
        shader->setFloat("material.diffuseReflectivity", 0.8f);
        shader->setFloat("material.specularReflectivity", 0.2f);
        shader->setFloat("material.specularDiffusion", 0.5f);
        shader->setFloat("material.refractiveIndex", 1.0f);  // Default to no refraction
        shader->setFloat("material.transparency", 0.0f);     // Default to opaque

        // Set visualization flag (for debugging)
        shader->setBool("enableVoxelVisualization", voxelizer->showDebugVisualization);

        // Update point lights (still needed for direct lighting in VCT)
        updatePointLights();

        // Set point light uniforms
        for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
            std::string lightName = "lights[" + std::to_string(i) + "]";
            shader->setVec3(lightName + ".position", pointLights[i].position);
            shader->setVec3(lightName + ".color", pointLights[i].color);
            shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
        }
        shader->setInt("numLights", std::min((int)pointLights.size(), MAX_LIGHTS));
    }

    // Render scene
    renderModels(shader);
    renderPointClouds(shader);

    // Update cursor position after scene is rendered
    cursorManager.updateCursorPosition(window, projection, view, shader);

    // Update the cursor's captured position if available
    if (cursorManager.isCursorPositionValid()) {
        capturedCursorPos = cursorManager.getCursorPosition();
    }

    // Update shader uniforms for cursors
    cursorManager.updateShaderUniforms(shader);

    // Render orbit center if needed
    if (!orbitFollowsCursor && cursorManager.isShowOrbitCenter() && camera.IsOrbiting) {
        cursorManager.renderOrbitCenter(projection, view, camera.OrbitPoint);
    }

    renderSkybox(projection, view, shader);

    if (camera.IsPanning == false) {
        cursorManager.renderCursors(projection, view);
    }

    // 4. Voxel visualization (if enabled) - AFTER main rendering
    if (voxelizer->showDebugVisualization) {
        voxelizer->renderDebugVisualization(camera.Position, projection, view);
    }

    // Render UI
    if (showGui) {
        renderGUI(drawBuffer == GL_BACK_LEFT, viewport, windowFlags, shader);
    }

    // Reset OpenGL state
    glUseProgram(0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glBindTexture(GL_TEXTURE_3D, 0);
}

void renderModels(Engine::Shader* shader) {
    // Don't do lighting setup for the depth shader
    if (shader != simpleDepthShader) {
        // Bind skybox for reflections
        bindSkyboxUniforms(shader);

        // Set lighting mode
        shader->setInt("lightingMode", static_cast<int>(currentLightingMode));
        shader->setBool("enableShadows", enableShadows);

        // Set lighting uniforms based on current lighting mode
        if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
            // Update point lights
            updatePointLights();

            // Set point light uniforms
            for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
                std::string lightName = "lights[" + std::to_string(i) + "]";
                shader->setVec3(lightName + ".position", pointLights[i].position);
                shader->setVec3(lightName + ".color", pointLights[i].color);
                shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
            }
            shader->setInt("numLights", std::min((int)pointLights.size(), MAX_LIGHTS));

            // Set sun properties
            shader->setBool("sun.enabled", sun.enabled);
            shader->setVec3("sun.direction", sun.direction);
            shader->setVec3("sun.color", sun.color);
            shader->setFloat("sun.intensity", sun.intensity);
        }
        else if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
            // Update point lights (still needed for direct lighting in VCT)
            updatePointLights();

            // Set point light uniforms
            for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
                std::string lightName = "lights[" + std::to_string(i) + "]";
                shader->setVec3(lightName + ".position", pointLights[i].position);
                shader->setVec3(lightName + ".color", pointLights[i].color);
                shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
            }
            shader->setInt("numLights", std::min((int)pointLights.size(), MAX_LIGHTS));

            // Set sun properties
            shader->setBool("sun.enabled", sun.enabled);
            shader->setVec3("sun.direction", sun.direction);
            shader->setVec3("sun.color", sun.color);
            shader->setFloat("sun.intensity", sun.intensity);

            // Set VCT settings
            shader->setBool("vctSettings.indirectSpecularLight", vctSettings.indirectSpecularLight);
            shader->setBool("vctSettings.indirectDiffuseLight", vctSettings.indirectDiffuseLight);
            shader->setBool("vctSettings.directLight", vctSettings.directLight);
            shader->setBool("vctSettings.shadows", vctSettings.shadows);

            // Set voxel grid parameters
            float halfSize = voxelizer->getVoxelGridSize() * 0.5f;
            shader->setVec3("gridMin", glm::vec3(-halfSize));
            shader->setVec3("gridMax", glm::vec3(halfSize));
            shader->setFloat("voxelSize", vctSettings.voxelSize);

            // Set visualization flag (for debugging)
            shader->setBool("enableVoxelVisualization", voxelizer->showDebugVisualization);
        }
    }

    // Calculate view projection matrix for frustum culling
    glm::mat4 viewProj;
    if (shader != simpleDepthShader) {
        viewProj = camera.GetProjectionMatrix(aspectRatio, currentScene.settings.nearPlane, currentScene.settings.farPlane)
            * camera.GetViewMatrix();
    }

    // Render each model
    for (int i = 0; i < currentScene.models.size(); i++) {
        auto& model = currentScene.models[i];
        if (!model.visible) continue;

        // Calculate model matrix
        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::translate(modelMatrix, model.position);
        modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x), glm::vec3(1, 0, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y), glm::vec3(0, 1, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z), glm::vec3(0, 0, 1));
        modelMatrix = glm::scale(modelMatrix, model.scale);

        // Set model matrix in shader
        shader->setMat4("model", modelMatrix);
        shader->setBool("useInstancing", false);

        // Set standard material properties
        shader->setBool("material.hasNormalMap", model.hasNormalMap());
        shader->setBool("material.hasSpecularMap", model.hasSpecularMap());
        shader->setBool("material.hasAOMap", model.hasAOMap());
        shader->setFloat("material.hasTexture", !model.getMeshes().empty() && !model.getMeshes()[0].textures.empty() ? 1.0f : 0.0f);
        shader->setVec3("material.objectColor", model.color);
        shader->setFloat("material.shininess", model.shininess);
        shader->setFloat("material.emissive", model.emissive);

        // For VCT, set additional material properties
        if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
            // Set VCT specific material properties
            shader->setFloat("material.diffuseReflectivity", model.diffuseReflectivity);
            shader->setVec3("material.specularColor", model.specularColor);
            shader->setFloat("material.specularReflectivity", model.specularReflectivity);
            shader->setFloat("material.specularDiffusion", model.specularDiffusion);
            shader->setFloat("material.refractiveIndex", model.refractiveIndex);
            shader->setFloat("material.transparency", model.transparency);
        }

        // Set selection state
        shader->setBool("selectionMode", selectionMode);
        shader->setBool("isSelected", selectionMode && (i == currentSelectedIndex) && (currentSelectedType == SelectedType::Model));
        shader->setInt("selectedMeshIndex", currentSelectedMeshIndex);
        shader->setBool("isMeshSelected", currentSelectedMeshIndex >= 0);

        // Set current mesh index for all meshes
        for (int j = 0; j < model.getMeshes().size(); j++) {
            shader->setInt("currentMeshIndex", j);
            model.getMeshes()[j].Draw(*shader);
        }
    }
}

void renderPointClouds(Engine::Shader* shader) {
    // Skip point cloud rendering for depth pass as points don't cast good shadows
    if (shader == simpleDepthShader) return;

    for (auto& pointCloud : currentScene.pointClouds) {
        if (!pointCloud.visible) continue;

        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::translate(modelMatrix, pointCloud.position);
        modelMatrix = glm::rotate(modelMatrix, glm::radians(pointCloud.rotation.x), glm::vec3(1, 0, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(pointCloud.rotation.y), glm::vec3(0, 1, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(pointCloud.rotation.z), glm::vec3(0, 0, 1));
        modelMatrix = glm::scale(modelMatrix, pointCloud.scale);

        shader->setMat4("model", modelMatrix);
        shader->setBool("isPointCloud", true);

        glBindVertexArray(pointCloud.vao);

        glm::mat4 viewMatrix = camera.GetViewMatrix();
        glm::mat4 projectionMatrix = camera.GetProjectionMatrix(aspectRatio, currentScene.settings.nearPlane, currentScene.settings.farPlane);
        glm::mat4 viewProjectionMatrix = projectionMatrix * viewMatrix;
        glm::vec3 cameraPosition = camera.Position;

        for (const auto& chunk : pointCloud.chunks) {
            // Calculate transformed chunk position using the point cloud's model matrix
            glm::vec3 chunkWorldPos = glm::vec3(modelMatrix * glm::vec4(chunk.centerPosition, 1.0f));

            // Frustum culling using transformed position
            if (!camera.isInFrustum(chunkWorldPos, chunk.boundingRadius * glm::compMax(pointCloud.scale), viewProjectionMatrix)) {
                continue;
            }

            float distanceToCamera = glm::distance(chunkWorldPos, cameraPosition);

            // Determine LOD based on distance
            int lodLevel = 4;  // Start with lowest detail
            for (int i = 0; i < 5; ++i) {
                if (distanceToCamera < pointCloud.lodDistances[i] * 1.0f) {
                    lodLevel = i;
                    break;
                }
            }

            float pointSizeMultiplier = 1.0f + (lodLevel) * 0.5f;
            float adjustedPointSize = pointCloud.basePointSize * pointSizeMultiplier;

            glBindBuffer(GL_ARRAY_BUFFER, chunk.lodVBOs[lodLevel]);
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)offsetof(PointCloudPoint, color));
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)offsetof(PointCloudPoint, intensity));

            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);

            glPointSize(adjustedPointSize);
            glDrawArrays(GL_POINTS, 0, chunk.lodPointCounts[lodLevel]);
        }

        glBindVertexArray(0);

        // Visualize chunk boundaries if enabled
        if (pointCloud.visualizeChunks) {
            shader->setBool("isChunkOutline", true);
            shader->setVec3("outlineColor", glm::vec3(1.0f, 1.0f, 0.0f));

            glBindVertexArray(pointCloud.chunkOutlineVAO);
            glDrawArrays(GL_LINES, 0, pointCloud.chunkOutlineVertices.size());
            glBindVertexArray(0);

            shader->setBool("isChunkOutline", false);
        }
    }

    shader->setBool("isPointCloud", false);
}
#pragma endregion


// ---- Shader and Lighting ----
#pragma region Shader and Lighting
void updatePointLights() {
    pointLights.clear();
    for (const auto& model : currentScene.models) {
        if (model.emissive > 0.0f) {
            // Calculate rotation matrix for the model
            glm::mat4 rotX = glm::rotate(glm::mat4(1.0f), glm::radians(model.rotation.x), glm::vec3(1, 0, 0));
            glm::mat4 rotY = glm::rotate(glm::mat4(1.0f), glm::radians(model.rotation.y), glm::vec3(0, 1, 0));
            glm::mat4 rotZ = glm::rotate(glm::mat4(1.0f), glm::radians(model.rotation.z), glm::vec3(0, 0, 1));
            glm::mat4 rotationMatrix = rotZ * rotY * rotX;

            // Calculate the model's bounding box in world space
            glm::vec3 minBounds(std::numeric_limits<float>::max());
            glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

            // Iterate through all meshes in the model
            for (const auto& mesh : model.getMeshes()) {
                for (const auto& vertex : mesh.vertices) {
                    glm::vec4 rotatedPos = rotationMatrix * glm::vec4(vertex.position, 1.0f);
                    glm::vec3 worldPos = model.position + model.scale * glm::vec3(rotatedPos);
                    minBounds = glm::min(minBounds, worldPos);
                    maxBounds = glm::max(maxBounds, worldPos);
                }
            }

            // Create multiple point lights distributed across the model's bounding box
            int numLightsPerDimension = 2; // Adjust this for more or fewer lights
            glm::vec3 step = (maxBounds - minBounds) / float(numLightsPerDimension - 1);
            for (int x = 0; x < numLightsPerDimension; ++x) {
                for (int y = 0; y < numLightsPerDimension; ++y) {
                    for (int z = 0; z < numLightsPerDimension; ++z) {
                        PointLight light;
                        light.position = minBounds + glm::vec3(x * step.x, y * step.y, z * step.z);
                        light.color = model.color;
                        light.intensity = model.emissive / float(numLightsPerDimension * numLightsPerDimension * numLightsPerDimension);
                        pointLights.push_back(light);
                    }
                }
            }
        }
    }
}
#pragma endregion


PointCloud loadPointCloudFile(const std::string& filePath, size_t downsampleFactor) {
    return Engine::PointCloudLoader::loadPointCloudFile(filePath, downsampleFactor);
}


// ---- Ray Casting ----
#pragma region Ray Casting
void calculateMouseRay(float mouseX, float mouseY, glm::vec3& rayOrigin, glm::vec3& rayDirection, glm::vec3& rayNear, glm::vec3& rayFar, float aspect) {
    // Convert mouse position to normalized device coordinates
    float x = (2.0f * mouseX) / windowWidth - 1.0f;
    float y = 1.0f - (2.0f * mouseY) / windowHeight;

    // Calculate near and far points in clip space
    glm::vec4 rayNearClip = glm::vec4(x, y, -1.0, 1.0);
    glm::vec4 rayFarClip = glm::vec4(x, y, 1.0, 1.0);

    // Convert to eye space
    glm::mat4 invProj = glm::inverse(camera.GetProjectionMatrix(aspect, currentScene.settings.nearPlane, currentScene.settings.farPlane));
    glm::vec4 rayNearEye = invProj * rayNearClip;
    glm::vec4 rayFarEye = invProj * rayFarClip;

    // Normalize eye space coordinates
    rayNearEye /= rayNearEye.w;
    rayFarEye /= rayFarEye.w;

    // Convert to world space
    glm::mat4 invView = glm::inverse(camera.GetViewMatrix());
    glm::vec4 rayNearWorld = invView * rayNearEye;
    glm::vec4 rayFarWorld = invView * rayFarEye;

    rayNear = glm::vec3(rayNearWorld);
    rayFar = glm::vec3(rayFarWorld);

    rayOrigin = camera.Position;
    rayDirection = glm::normalize(rayFar - rayNear);
}

bool rayIntersectsModel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const Engine::Model& model, float& distance) {
    float closestDistance = std::numeric_limits<float>::max();
    bool intersected = false;
    // Calculate model matrix
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), model.position);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x), glm::vec3(1, 0, 0));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y), glm::vec3(0, 1, 0));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z), glm::vec3(0, 0, 1));
    modelMatrix = glm::scale(modelMatrix, model.scale);

    // Transform ray to model space
    glm::mat4 invModelMatrix = glm::inverse(modelMatrix);
    glm::vec3 rayOriginModel = glm::vec3(invModelMatrix * glm::vec4(rayOrigin, 1.0f));
    glm::vec3 rayDirectionModel = glm::normalize(glm::vec3(invModelMatrix * glm::vec4(rayDirection, 0.0f)));

    // Check each mesh in the model
    for (const auto& mesh : model.getMeshes()) {
        // Iterate through all triangles in the mesh
        for (size_t i = 0; i < mesh.indices.size(); i += 3) {
            glm::vec3 v0 = mesh.vertices[mesh.indices[i]].position;
            glm::vec3 v1 = mesh.vertices[mesh.indices[i + 1]].position;
            glm::vec3 v2 = mesh.vertices[mesh.indices[i + 2]].position;

            // Möller–Trumbore intersection algorithm
            glm::vec3 edge1 = v1 - v0;
            glm::vec3 edge2 = v2 - v0;
            glm::vec3 h = glm::cross(rayDirectionModel, edge2);
            float a = glm::dot(edge1, h);

            if (a > -0.00001f && a < 0.00001f) continue; // Ray is parallel to triangle

            float f = 1.0f / a;
            glm::vec3 s = rayOriginModel - v0;
            float u = f * glm::dot(s, h);

            if (u < 0.0f || u > 1.0f) continue;

            glm::vec3 q = glm::cross(s, edge1);
            float v = f * glm::dot(rayDirectionModel, q);

            if (v < 0.0f || u + v > 1.0f) continue;

            float t = f * glm::dot(edge2, q);

            if (t > 0.00001f && t < closestDistance) {
                closestDistance = t;
                intersected = true;
            }
        }
    }

    if (intersected) {
        // Transform the intersection point back to world space
        glm::vec3 intersectionPointModel = rayOriginModel + rayDirectionModel * closestDistance;
        glm::vec4 intersectionPointWorld = modelMatrix * glm::vec4(intersectionPointModel, 1.0f);
        distance = glm::distance(rayOrigin, glm::vec3(intersectionPointWorld));
        return true;
    }

    return false;
}
#pragma endregion



// ---- Callbacks ----
#pragma region Callbacks
void framebuffer_size_callback(GLFWwindow* window, int width, int height)
{
    glViewport(0, 0, width, height);
    windowWidth = width;
    windowHeight = height;
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (!ImGui::GetIO().WantCaptureMouse) {
        // Update cursor info before processing scroll
        if (cursorManager.isCursorPositionValid()) {
            camera.UpdateCursorInfo(cursorManager.getCursorPosition(), true);
        }
        else {
            camera.UpdateCursorInfo(glm::vec3(0.0f), false);
        }
        camera.ProcessMouseScroll(yoffset);
    }
}




void mouse_button_callback(GLFWwindow* window, int button, int action, int mods) {
    if (ImGui::GetIO().WantCaptureMouse) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        return;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            // Check if Ctrl is held down
            bool ctrlPressed = (mods & GLFW_MOD_CONTROL);
            if (ctrlPressed) {
                glm::vec3 rayOrigin, rayDirection, rayNear, rayFar;
                calculateMouseRay(lastX, lastY, rayOrigin, rayDirection, rayNear, rayFar, (float)windowWidth / (float)windowHeight);

                float closestDistance = std::numeric_limits<float>::max();
                int closestModelIndex = -1;
                int closestPointCloudIndex = -1;

                // Check intersection with models
                for (int i = 0; i < currentScene.models.size(); i++) {
                    const auto& model = currentScene.models[i];
                    float distance;
                    if (rayIntersectsModel(rayOrigin, rayDirection, model, distance)) {
                        if (distance < closestDistance) {
                            closestDistance = distance;
                            closestModelIndex = i;
                            closestPointCloudIndex = -1;
                        }
                    }
                }

                // Check intersection with point clouds (simplified)
                for (int i = 0; i < currentScene.pointClouds.size(); i++) {
                    const auto& pointCloud = currentScene.pointClouds[i];
                    float distance = glm::length(pointCloud.position - rayOrigin);
                    if (distance < closestDistance) {
                        closestDistance = distance;
                        closestPointCloudIndex = i;
                        closestModelIndex = -1;
                    }
                }

                if (closestModelIndex != -1) {
                    currentSelectedType = SelectedType::Model;
                    currentSelectedIndex = closestModelIndex;
                    currentSelectedMeshIndex = -1;

                    if (!isMouseCaptured) {
                        isMouseCaptured = true;
                        firstMouse = true; // Reset flag for delta calculation
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    }

                    if (ctrlPressed) {
                        selectionMode = true;
                        isMovingModel = true;
                    }
                }
                else if (closestPointCloudIndex != -1) {
                    currentSelectedType = SelectedType::PointCloud;
                    currentSelectedIndex = closestPointCloudIndex;
                    currentSelectedMeshIndex = -1; // Always reset mesh selection for point clouds
                }
                else {
                    // Ctrl+Clicked empty space or non-model object
                    isMovingModel = false;
                }
            }

            // Handle double-click (if not in selection mode)
            if (!selectionMode) {
                double currentTime = glfwGetTime();
                if (currentTime - lastClickTime < doubleClickTime) {
                    if (cursorManager.isCursorPositionValid()) {
                        camera.StartCenteringAnimation(cursorManager.getCursorPosition());
                        glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
                    }
                }
                lastClickTime = currentTime;
            }

            if (!camera.IsAnimating && !camera.IsOrbiting && !selectionMode) {
                leftMousePressed = true;

                if (cursorManager.isCursorPositionValid()) {
                    // Different orbiting behaviors based on settings
                    if (camera.orbitAroundCursor) {
                        camera.UpdateCursorInfo(cursorManager.getCursorPosition(), true);
                        camera.StartOrbiting(true); // Pass true to use current cursor position
                        capturedCursorPos = cursorManager.getCursorPosition();
                        // Enable mouse capture when orbiting starts
                        isMouseCaptured = true;
                        firstMouse = true; // Reset the first mouse flag
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        // Center cursor
                        glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
                    }
                    else if (orbitFollowsCursor) {
                        camera.StartCenteringAnimation(cursorManager.getCursorPosition());
                        capturedCursorPos = cursorManager.getCursorPosition();

                        isMouseCaptured = true;
                        firstMouse = true;
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
                    }
                    else {
                        float cursorDepth = glm::length(cursorManager.getCursorPosition() - camera.Position);
                        glm::vec3 viewportCenter = camera.Position + camera.Front * cursorDepth;
                        capturedCursorPos = viewportCenter;
                        camera.SetOrbitPointDirectly(capturedCursorPos);
                        camera.OrbitDistance = cursorDepth;
                        camera.StartOrbiting();
                        // Enable mouse capture when orbiting starts
                        isMouseCaptured = true;
                        firstMouse = true; // Reset the first mouse flag
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        // Center cursor
                        glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
                    }
                }
                else {
                    capturedCursorPos = camera.Position + camera.Front * camera.OrbitDistance;
                    camera.SetOrbitPointDirectly(capturedCursorPos);
                    camera.StartOrbiting();
                    // Enable mouse capture when orbiting starts
                    isMouseCaptured = true;
                    firstMouse = true; // Reset the first mouse flag
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    // Center cursor
                    glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
                }
            }
        }
        else if (action == GLFW_RELEASE) {
            if (isMouseCaptured) {
                // Disable mouse capture when orbiting ends
                isMouseCaptured = false;
                firstMouse = true; // Reset first mouse flag for next time
            }

            if (camera.orbitAroundCursor == false) {
                glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
            }

            leftMousePressed = false;
            camera.StopOrbiting();
            isMovingModel = false;
            selectionMode = false;

        }
    }
    else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
        if (action == GLFW_PRESS) {
            middleMousePressed = true;
            camera.StartPanning();
            // Enable mouse capture for middle button panning
            isMouseCaptured = true;
            firstMouse = true; // Reset the first mouse flag
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            // Center cursor
            glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
        }
        else if (action == GLFW_RELEASE) {
            middleMousePressed = false;
            camera.StopPanning();
            // Disable mouse capture
            isMouseCaptured = false;
            firstMouse = true; // Reset first mouse flag for next time
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            rightMousePressed = true;
            // Enable mouse capture for right button rotation
            isMouseCaptured = true;
            firstMouse = true; // Reset the first mouse flag
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

            // Center cursor
            glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
        }
        else if (action == GLFW_RELEASE) {
            rightMousePressed = false;
            // Disable mouse capture
            isMouseCaptured = false;
            firstMouse = true; // Reset first mouse flag for next time
        }
    }
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    // --- Early exit if window doesn't have focus ---
    if (!windowHasFocus) {
        return;
    }

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    // --- Handle first mouse input OR first input after regaining focus ---
    if (firstMouse || justRegainedFocus) {
        // This is the first event, or the first one after regaining focus.
        // Treat the current position as the starting point. Don't calculate offset.
        lastX = xpos;
        lastY = ypos;
        firstMouse = false;
        justRegainedFocus = false; // Handled the focus regain

        // Ensure accumulators are zeroed at the start or on focus regain
        accumulatedXOffset = 0.0;
        accumulatedYOffset = 0.0;
        return; // Don't process movement on this initial event
    }

    // --- Calculate Delta from last position (standard approach) ---
    double frameXOffset = static_cast<double>(xpos) - lastX;
    double frameYOffset = lastY - static_cast<double>(ypos); // Reversed

    // --- Update last position for the next frame ---
    // Crucial: Do this *before* checking capture or ImGui
    lastX = xpos;
    lastY = ypos;

    // --- ImGui Check ---
    // If ImGui wants the mouse *after* we've calculated delta and updated lastX/Y
    if (ImGui::GetIO().WantCaptureMouse) {
        // Don't accumulate the delta if ImGui wants it
        // Reset accumulators to prevent processing stale input on next non-ImGui frame
        accumulatedXOffset = 0.0;
        accumulatedYOffset = 0.0;
        // If we *were* capturing, ImGui might need the cursor visible
        if (isMouseCaptured) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
        return;
    }

    // --- Accumulate if mouse is captured ---
    if (isMouseCaptured) {
        // Apply Smoothing (Optional) - Apply to the calculated delta
        frameXOffset *= mouseSmoothingFactor;
        frameYOffset *= mouseSmoothingFactor;

        // Add to accumulators
        accumulatedXOffset += frameXOffset;
        accumulatedYOffset += frameYOffset;
    }
    // No need for an 'else' here, if not captured, we just don't accumulate.
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_G && action == GLFW_PRESS)
    {
        showGui = !showGui;
        std::cout << "GUI visibility toggled. showGui = " << (showGui ? "true" : "false") << std::endl;
    }

    if (key == GLFW_KEY_L && action == GLFW_PRESS) {
        // Toggle between shadow mapping and voxel cone tracing
        currentLightingMode = (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING) ?
            GUI::LIGHTING_VOXEL_CONE_TRACING : GUI::LIGHTING_SHADOW_MAPPING;


        // Reset OpenGL state when switching modes
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        // Make sure texture units are reset
        for (int i = 0; i < 8; i++) {
            glActiveTexture(GL_TEXTURE0 + i);
            glBindTexture(GL_TEXTURE_2D, 0);
            glBindTexture(GL_TEXTURE_3D, 0);
        }
        glActiveTexture(GL_TEXTURE0);

        // If switching to VCT, update the voxel grid
        if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
            voxelizer->update(camera.Position, currentScene.models);
        }

        // Update preferences
        preferences.lightingMode = currentLightingMode;
        savePreferences();
    }

    // Toggle shadows
    if (key == GLFW_KEY_K && action == GLFW_PRESS) {
        enableShadows = !enableShadows;
        std::cout << "Shadows " << (enableShadows ? "enabled" : "disabled") << std::endl;

        // Update preferences
        preferences.enableShadows = enableShadows;
        savePreferences();
    }

    // Toggle voxel visualization
    if (key == GLFW_KEY_V && action == GLFW_PRESS) {
        voxelizer->showDebugVisualization = !voxelizer->showDebugVisualization;
        std::cout << "Voxel visualization " <<
            (voxelizer->showDebugVisualization ? "enabled" : "disabled") << std::endl;
    }

    if (key == GLFW_KEY_C && action == GLFW_PRESS)
    {
        // Try to find a good center point
        glm::vec3 centerPoint(0.0f);
        int objectCount = 0;

        // First, try to use the current cursor position if valid
        if (cursorManager.isCursorPositionValid()) {
            glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
            camera.StartCenteringAnimation(cursorManager.getCursorPosition());
            std::cout << "Centering on cursor position" << std::endl;
            return;
        }

        // If no cursor, calculate scene center
        for (const auto& model : currentScene.models) {
            centerPoint += model.position;
            objectCount++;
        }

        for (const auto& pointCloud : currentScene.pointClouds) {
            centerPoint += pointCloud.position;
            objectCount++;
        }

        if (objectCount > 0) {
            centerPoint /= objectCount;
            camera.StartCenteringAnimation(centerPoint);
            glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
            std::cout << "Centering on scene midpoint" << std::endl;
        }
        else {
            // If no objects, center on the world origin
            camera.StartCenteringAnimation(glm::vec3(0.0f));
            glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
            std::cout << "Centering on world origin" << std::endl;
        }
    }


    // Handle Ctrl key
    if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL)
    {
        if (action == GLFW_PRESS)
        {
            ctrlPressed = true;
            selectionMode = true;
        }
        else if (action == GLFW_RELEASE)
        {
            ctrlPressed = false;
            selectionMode = false;
        }
    }

    // Handle Delete key
    if (key == GLFW_KEY_DELETE && action == GLFW_PRESS)
    {
        std::cout << "Deleted selected model" << std::endl;
    }
}
#pragma endregion