// ---- Core Definitions ----
#define NOMINMAX
#include "core.h"
#include <thread>
#include <atomic>

// ---- Project-Specific Includes ----
#include "model_loader.h"
#include "Camera.h"
#include "scene_manager.h"
#include "cursor_presets.h"
#include "point_cloud_loader.h"

// ---- GUI and Dialog ----
#include "imgui/imgui_incl.h"
#include <portable-file-dialogs.h>

// ---- Utility Libraries ----
#include <json.h>
#include <corecrt_math_defines.h>
#include <openLinks.h>
#include <glm/gtx/component_wise.hpp>
#include <stb_image.h>
#include <voxalizer.h>

using namespace Engine;
using json = nlohmann::json;


// ---- Function Declarations ----
#pragma region Function Declarations
// ---- GLFW Callback Functions ----
void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods);
void scroll_callback(GLFWwindow* window, double xoffset, double yoffset);
void mouse_button_callback(GLFWwindow* window, int button, int action, int mods);
void mouse_callback(GLFWwindow* window, double xpos, double ypos);
void framebuffer_size_callback(GLFWwindow* window, int width, int height);

// ---- Rendering Functions ----
void renderEye(GLenum drawBuffer, const glm::mat4& projection, const glm::mat4& view, Shader* shader, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, GLFWwindow* window);
void renderSphereCursor(const glm::mat4& projection, const glm::mat4& view);
void renderOrbitCenter(const glm::mat4& projection, const glm::mat4& view);
void renderGUI(bool isLeftEye, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, Shader* shader);
void renderSunManipulationPanel();
void renderCursorSettingsWindow();
void renderModelManipulationPanel(Engine::Model& model, Shader* shader);
void renderPointCloudManipulationPanel(Engine::PointCloud& pointCloud);
void deleteSelectedPointCloud();
void renderModels(Shader* shader);
void applyPresetToGlobalSettings(const Engine::CursorPreset& preset);
void renderPointClouds(Shader* shader);

// ---- Update Functions ----
void updateCursorPosition(GLFWwindow* window, const glm::mat4& projection, const glm::mat4& view, Shader* shader);
void updateFragmentShaderUniforms(Shader* shader);
void updatePointLights();

PointCloud loadPointCloudFile(const std::string& filePath, size_t downsampleFactor = 1);

void createDefaultCubemap();
void initSkybox();

// ---- Utility Functions ----
float calculateLargestModelDimension();
void calculateMouseRay(float mouseX, float mouseY, glm::vec3& rayOrigin, glm::vec3& rayDirection, glm::vec3& rayNear, glm::vec3& rayFar, float aspect);
bool rayIntersectsModel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const Engine::Model& model, float& distance);

// ---- Scene Management Functions ----
void deleteSelectedModel();

// ---- Cleanup Functions ----
void cleanup(Shader* shader);
void terminateGLFW();
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
float minConvergence = -3.0f;  // Minimum convergence
float maxConvergence = 6.0f;   // Maximum convergence

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


enum SkyboxType {
    SKYBOX_CUBEMAP,     // Standard cubemap texture
    SKYBOX_SOLID_COLOR, // Solid color
    SKYBOX_GRADIENT     // Gradient color
};

struct SkyboxConfig {
    SkyboxType type = SKYBOX_CUBEMAP;
    glm::vec3 solidColor = glm::vec3(0.2f, 0.3f, 0.4f);
    glm::vec3 gradientTopColor = glm::vec3(0.1f, 0.1f, 0.3f);
    glm::vec3 gradientBottomColor = glm::vec3(0.7f, 0.7f, 1.0f);
    int selectedCubemap = 0;  // Index of the selected predefined cubemap
};

SkyboxConfig skyboxConfig;

// ---- Preferences Structure ----
struct ApplicationPreferences {
    bool isDarkTheme = true;
    float separation = 0.005f;
    float convergence = 1.5f;
    float nearPlane = 0.1f;
    float farPlane = 200.0f;
    std::string currentPresetName = "Sphere";
    float cameraSpeedFactor = 1.0f;
    bool showFPS = true;
    bool show3DCursor = true;
    bool useNewStereoMethod = true;
    float fov = 45.0f;
    float scrollMomentum = 0.5f;
    float maxScrollVelocity = 3.0f;
    float scrollDeceleration = 10.0f;
    bool useSmoothScrolling = true;
    bool zoomToCursor = true;
    bool orbitAroundCursor = true;
    bool orbitFollowsCursor = false;
    float mouseSmoothingFactor = 1.0f;
    float mouseSensitivity = 0.17f;

    int skyboxType = SKYBOX_CUBEMAP;
    glm::vec3 skyboxSolidColor = glm::vec3(0.2f, 0.3f, 0.4f);
    glm::vec3 skyboxGradientTop = glm::vec3(0.1f, 0.1f, 0.3f);
    glm::vec3 skyboxGradientBottom = glm::vec3(0.7f, 0.7f, 1.0f);
    int selectedCubemap = 0;
};


ApplicationPreferences preferences;


// ---- Scene Persistence ----
static char saveFilename[256] = "scene.json"; // Buffer for saving scene filename
static char loadFilename[256] = "scene.json"; // Buffer for loading scene filename
static std::string currentPresetName = "Default";
static bool isEditingPresetName = false;
static char editPresetNameBuffer[256] = "";

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

// ---- Cursor Position ----
glm::vec3 g_cursorPos(0.0f);
bool g_cursorValid = false;

// ---- Window Configuration ----
int windowWidth = 1920;
int windowHeight = 1080;

// ---- Lighting ----
std::vector<PointLight> pointLights;
float zOffset = 0.5f;
Sun sun = {
    glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f)), // More vertical angle
    glm::vec3(1.0f, 0.95f, 0.8f),                   // Warmer color
    0.16f,                                            // Higher intensity
    true
};

unsigned int depthMapFBO;
unsigned int depthMap;
const unsigned int SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;
Shader* simpleDepthShader = nullptr;



// Predefined cubemap paths
struct CubemapPreset {
    std::string name;
    std::string path;
    std::string description;
};

std::vector<CubemapPreset> cubemapPresets = {
    {"Default", "skybox/Default/", "Default skybox environment"},
    {"Yokohama", "skybox/Yokohama/", "Yokohama, Japan. View towards Intercontinental Yokohama Grand hotel."},
    {"Storforsen", "skybox/Storforsen/", "At the top of Storforsen. Taken with long exposure, resulting in smooth looking water flow."},
    {"Yokohama Night", "skybox/YokohamaNight/", "Yokohama at night."},
    {"Lycksele", "skybox/Lycksele/", "Lycksele. View of Ansia Camping, Lycksele."}
};

// ---- Sphere Cursor ----
GLuint sphereVAO, sphereVBO, sphereEBO;
std::vector<float> sphereVertices;
std::vector<unsigned int> sphereIndices;
Shader* sphereShader = nullptr;

enum CursorScalingMode {
    CURSOR_NORMAL,
    CURSOR_FIXED,
    CURSOR_CONSTRAINED_DYNAMIC,
    CURSOR_LOGARITHMIC
};

CursorScalingMode currentCursorScalingMode = CURSOR_CONSTRAINED_DYNAMIC;
float fixedSphereRadius = 0.7f;
float minDiff = 0.01f;
float maxDiff = 0.1f;
float oldSphereRadius = fixedSphereRadius;
glm::vec4 cursorColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.7f); // Initial color: semi-transparent red
float cursorTransparency = 1.0f;
bool showCursor = true;
float cursorEdgeSoftness = 0.8f;
float cursorCenterTransparency = 0.2f;

bool showInnerSphere = false;
glm::vec4 innerSphereColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f); // Initial color: semi-transparent green
float innerSphereFactor = 0.1f;

// ---- Fragment Shader Cursor ----
struct FragmentShaderCursorSettings {
    float baseOuterRadius = 0.04f;
    float baseOuterBorderThickness = 0.005f;
    float baseInnerRadius = 0.004f;
    float baseInnerBorderThickness = 0.005f;
    glm::vec4 outerColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 innerColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
};

FragmentShaderCursorSettings fragmentCursorSettings;


struct PlaneCursor {
    GLuint VAO, VBO, EBO;
    glm::vec4 color = glm::vec4(0.0f, 1.0f, 0.0f, 0.7f);
    float diameter = 0.5f;
    bool show = false;
    Shader* shader = nullptr;
};

PlaneCursor planeCursor;

Engine::Voxelizer* voxelizer = nullptr;


// ---- Cursor Visibility ----
bool showSphereCursor = false;
bool showFragmentCursor = true;

// ---- Orbit Settings ----
glm::vec3 capturedCursorPos;
bool orbitFollowsCursor = false;
bool showOrbitCenter = false;
glm::vec4 orbitCenterColor = glm::vec4(0.0f, 1.0f, 0.0f, 0.7f); // Initial color: semi-transparent green
float orbitCenterSphereRadius = 0.2f; // Fixed size for the orbit center sphere

// ---- Cursor Presets ----
struct CursorPreset {
    bool showSphereCursor;
    bool showFragmentCursor;
    float fragmentBaseInnerRadius;
    CursorScalingMode sphereScalingMode;
    float sphereFixedRadius;
    float sphereTransparency;
    bool showInnerSphere;
    bool showPlaneCursor;
    float planeDiameter;
    glm::vec4 planeColor;
};

void setDefaultCursorSettings() {
    showSphereCursor = true;
    showFragmentCursor = true;
    currentCursorScalingMode = CURSOR_CONSTRAINED_DYNAMIC;
    fixedSphereRadius = 0.05f;
    cursorColor = glm::vec4(1.0f, 0.0f, 0.0f, 0.7f);
    cursorTransparency = 0.7f;
    cursorEdgeSoftness = 0.8f;
    cursorCenterTransparency = 0.2f;
    showInnerSphere = false;
    innerSphereColor = glm::vec4(0.0f, 1.0f, 0.0f, 1.0f);
    innerSphereFactor = 0.1f;
    orbitFollowsCursor = true;
    showOrbitCenter = false;
    orbitCenterColor = glm::vec4(0.0f, 1.0f, 0.0f, 0.7f);
    orbitCenterSphereRadius = 0.2f;
    fragmentCursorSettings = FragmentShaderCursorSettings();
}
#pragma endregion


GLuint skyboxVAO, skyboxVBO, cubemapTexture;
float ambientStrengthFromSkybox = 0.1f;
Shader* skyboxShader = nullptr;

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
    case SKYBOX_SOLID_COLOR:
        createSolidColorSkybox(skyboxConfig.solidColor);
        break;

    case SKYBOX_GRADIENT:
        createGradientSkybox(skyboxConfig.gradientBottomColor, skyboxConfig.gradientTopColor);
        break;

    case SKYBOX_CUBEMAP:
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

void setupPlaneCursor() {
    // Create a circular plane using triangles
    std::vector<float> vertices;
    std::vector<unsigned int> indices;

    const int segments = 32;
    const float radius = 0.5f; // Unit radius, will be scaled by diameter

    // Center vertex
    vertices.push_back(0.0f); // x
    vertices.push_back(0.0f); // y
    vertices.push_back(0.0f); // z

    // Circle vertices
    for (int i = 0; i <= segments; i++) {
        float angle = (2.0f * M_PI * i) / segments;
        vertices.push_back(radius * cos(angle));
        vertices.push_back(radius * sin(angle));
        vertices.push_back(0.0f);
    }

    // Indices for triangle fan
    for (int i = 0; i < segments; i++) {
        indices.push_back(0);
        indices.push_back(i + 1);
        indices.push_back(i + 2);
    }

    glGenVertexArrays(1, &planeCursor.VAO);
    glGenBuffers(1, &planeCursor.VBO);
    glGenBuffers(1, &planeCursor.EBO);

    glBindVertexArray(planeCursor.VAO);

    glBindBuffer(GL_ARRAY_BUFFER, planeCursor.VBO);
    glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(float), vertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, planeCursor.EBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW);

    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(0);

    try {
        planeCursor.shader = Engine::loadShader("planeCursorVertexShader.glsl", "planeCursorFragmentShader.glsl");
    }
    catch (const std::exception& e) {
        std::cerr << "Failed to load plane cursor shaders: " << e.what() << std::endl;
    }
}

void renderPlaneCursor(const glm::mat4& projection, const glm::mat4& view) {
    if (!planeCursor.show || !g_cursorValid) return;

    planeCursor.shader->use();
    planeCursor.shader->setMat4("projection", projection);
    planeCursor.shader->setMat4("view", view);

    glm::mat4 model = glm::translate(glm::mat4(1.0f), g_cursorPos);

    // Orient plane to face camera
    glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
    glm::vec3 forward = glm::normalize(camera.Position - g_cursorPos);
    glm::vec3 right = glm::normalize(glm::cross(up, forward));
    up = glm::cross(forward, right);

    glm::mat4 rotation = glm::mat4(
        glm::vec4(right, 0.0f),
        glm::vec4(up, 0.0f),
        glm::vec4(forward, 0.0f),
        glm::vec4(0.0f, 0.0f, 0.0f, 1.0f)
    );

    model = model * rotation;
    model = glm::scale(model, glm::vec3(planeCursor.diameter));

    planeCursor.shader->setMat4("model", model);
    planeCursor.shader->setVec4("color", planeCursor.color);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(planeCursor.VAO);
    glDrawElements(GL_TRIANGLES, 96, GL_UNSIGNED_INT, 0);

    glDisable(GL_BLEND);
    glBindVertexArray(0);
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

void renderSkybox(const glm::mat4& projection, const glm::mat4& view, Shader* mainShader) {
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
void bindSkyboxUniforms(Shader* shader) {
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
    preferences.skyboxType = skyboxConfig.type;
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

    skyboxConfig.type = static_cast<SkyboxType>(preferences.skyboxType);
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
            applyPresetToGlobalSettings(loadedPreset);
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
                spherePreset.sphereScalingMode = static_cast<int>(CURSOR_CONSTRAINED_DYNAMIC);
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
                applyPresetToGlobalSettings(spherePreset);
            }
        }
    }
}

void loadPreferences() {
    std::ifstream file("preferences.json");
    bool fileExists = file.is_open();

    // Initialize with default values first
    preferences = ApplicationPreferences(); // This uses the default values from the struct

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
            preferences.skyboxType = j["skybox"].value("type", SKYBOX_CUBEMAP);

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
    preferences = ApplicationPreferences();

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
        spherePreset.sphereScalingMode = static_cast<int>(CURSOR_CONSTRAINED_DYNAMIC);
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

        skyboxConfig.type = SKYBOX_CUBEMAP;
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
        applyPresetToGlobalSettings(spherePreset);
    }

    // Set ImGui style
    SetupImGuiStyle(isDarkTheme, 1.0f);
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

// ---- Sphere Cursor Functions
#pragma region Sphere Cursor Functions
Engine::CursorPreset createPresetFromCurrentSettings(const std::string& name) {
    Engine::CursorPreset preset;
    preset.name = name;
    preset.showSphereCursor = showSphereCursor;
    preset.showFragmentCursor = showFragmentCursor;
    preset.fragmentBaseInnerRadius = fragmentCursorSettings.baseInnerRadius;
    preset.sphereScalingMode = static_cast<int>(currentCursorScalingMode);
    preset.sphereFixedRadius = fixedSphereRadius;
    preset.sphereTransparency = cursorTransparency;
    preset.showInnerSphere = showInnerSphere;
    preset.cursorColor = cursorColor;
    preset.innerSphereColor = innerSphereColor;
    preset.innerSphereFactor = innerSphereFactor;
    preset.cursorEdgeSoftness = cursorEdgeSoftness;
    preset.cursorCenterTransparency = cursorCenterTransparency;
    preset.showPlaneCursor = planeCursor.show;
    preset.planeDiameter = planeCursor.diameter;
    preset.planeColor = planeCursor.color;
    return preset;
}

void applyPresetToGlobalSettings(const Engine::CursorPreset& preset) {
    showSphereCursor = preset.showSphereCursor;
    showFragmentCursor = preset.showFragmentCursor;
    fragmentCursorSettings.baseInnerRadius = preset.fragmentBaseInnerRadius;
    currentCursorScalingMode = static_cast<CursorScalingMode>(preset.sphereScalingMode);
    fixedSphereRadius = preset.sphereFixedRadius;
    cursorTransparency = preset.sphereTransparency;
    showInnerSphere = preset.showInnerSphere;
    cursorColor = preset.cursorColor;
    innerSphereColor = preset.innerSphereColor;
    innerSphereFactor = preset.innerSphereFactor;
    cursorEdgeSoftness = preset.cursorEdgeSoftness;
    cursorCenterTransparency = preset.cursorCenterTransparency;
    planeCursor.show = preset.showPlaneCursor;
    planeCursor.diameter = preset.planeDiameter;
    planeCursor.color = preset.planeColor;
}

// ---- Sphere Cursor Calculations ----
float calculateSphereRadius(const glm::vec3& cursorPosition, const glm::vec3& cameraPosition) {
    float distance = glm::distance(cursorPosition, cameraPosition);

    switch (currentCursorScalingMode) {
    case CURSOR_NORMAL:
        return fixedSphereRadius;

    case CURSOR_FIXED:
        return fixedSphereRadius * distance;

    case CURSOR_CONSTRAINED_DYNAMIC: {
        float distanceFactor = std::sqrt(distance);
        float defaultScreenSize = std::pow(fixedSphereRadius, 2) * distanceFactor;
        float minScreenSize = std::pow(fixedSphereRadius - minDiff, 2) * distanceFactor;
        float maxScreenSize = std::pow(fixedSphereRadius + maxDiff, 2) * distanceFactor;
        oldSphereRadius = glm::clamp(oldSphereRadius, minScreenSize, maxScreenSize);
        return oldSphereRadius;
    }

    case CURSOR_LOGARITHMIC:
        return fixedSphereRadius * (1.0f + std::log(distance));

    default:
        return fixedSphereRadius * distance;
    }
}

// ---- Sphere Mesh Generation ----
void generateSphereMesh(float radius, unsigned int rings, unsigned int sectors) {
    sphereVertices.clear();
    sphereIndices.clear();

    float const R = 1.0f / (float)(rings - 1);
    float const S = 1.0f / (float)(sectors - 1);

    for (unsigned int r = 0; r < rings; ++r) {
        for (unsigned int s = 0; s < sectors; ++s) {
            float const y = sin(-M_PI_2 + M_PI * r * R);
            float const x = cos(2 * M_PI * s * S) * sin(M_PI * r * R);
            float const z = sin(2 * M_PI * s * S) * sin(M_PI * r * R);

            sphereVertices.push_back(x * radius);
            sphereVertices.push_back(y * radius);
            sphereVertices.push_back(z * radius);

            sphereVertices.push_back(x);
            sphereVertices.push_back(y);
            sphereVertices.push_back(z);
        }
    }

    for (unsigned int r = 0; r < rings - 1; ++r) {
        for (unsigned int s = 0; s < sectors - 1; ++s) {
            sphereIndices.push_back(r * sectors + s);
            sphereIndices.push_back(r * sectors + (s + 1));
            sphereIndices.push_back((r + 1) * sectors + (s + 1));

            sphereIndices.push_back(r * sectors + s);
            sphereIndices.push_back((r + 1) * sectors + (s + 1));
            sphereIndices.push_back((r + 1) * sectors + s);
        }
    }
}

// ---- Sphere Cursor Setup ----
void setupSphereCursor() {
    generateSphereMesh(0.05f, 32, 32);

    glGenVertexArrays(1, &sphereVAO);
    glGenBuffers(1, &sphereVBO);
    glGenBuffers(1, &sphereEBO);

    glBindVertexArray(sphereVAO);

    glBindBuffer(GL_ARRAY_BUFFER, sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, sphereVertices.size() * sizeof(float), sphereVertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sphereIndices.size() * sizeof(unsigned int), sphereIndices.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);

    try {
        sphereShader = Engine::loadShader("sphereVertexShader.glsl", "sphereFragmentShader.glsl");
    }
    catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << std::endl;
    }

    // Initialize uniforms for both shaders
    sphereShader->use();
    sphereShader->setMat4("projection", glm::mat4(1.0f));
    sphereShader->setMat4("view", glm::mat4(1.0f));
    sphereShader->setMat4("model", glm::mat4(1.0f));
    sphereShader->setVec3("viewPos", camera.Position);
}
#pragma endregion


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


    voxelizer = new Engine::Voxelizer(128);

    // ---- Initialize Shader ----
    Shader* shader = nullptr;
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
        Engine::CursorPreset defaultPreset = createPresetFromCurrentSettings("Default");
        Engine::CursorPresetManager::savePreset("Default", defaultPreset);
    }
    currentPresetName = Engine::CursorPresetManager::getPresetNames().front();


    setupShadowMapping();
    setupSphereCursor();
    setupPlaneCursor();
    setupSkyboxVAO(skyboxVAO, skyboxVBO);

    // ---- Calculate Largest Model Dimension ----
    float largestDimension = calculateLargestModelDimension();

    // ---- Initialize ImGui ----
    if (!InitializeImGuiWithFonts(window, true)) {
        std::cerr << "Failed to initialize ImGui with fonts" << std::endl;
        return -1;
    }

    // Configure additional ImGui settings
    ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    InitializeDefaults();

    loadPreferences();



    // ---- OpenGL Settings ----
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glfwSwapInterval(1); // Enable vsync

    // ---- Main Loop ----
    while (!glfwWindowShouldClose(window)) {
        // ---- Per-frame Time Logic ----
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;


        if (deltaTime <= 0.0f) continue;

        camera.UpdateScrolling(deltaTime);

        // ---- Handle Input ----
        Input::handleKeyInput(camera, deltaTime);

        // ---- Update Camera Animation ----
        camera.UpdateAnimation(deltaTime);

        // ---- Set Projection Matrices ----
        glm::mat4 view = camera.GetViewMatrix();
        aspectRatio = (float)windowWidth / (float)windowHeight;
        glm::mat4 projection = camera.GetProjectionMatrix(aspectRatio, currentScene.settings.nearPlane, currentScene.settings.farPlane);
        glm::mat4 leftProjection;
        glm::mat4 rightProjection;
        if (!camera.useNewMethod) {
            // Original method
            leftProjection = camera.offsetProjection(projection, currentScene.settings.separation / 2,
                abs(camera.Position.z) * currentScene.settings.convergence);
            rightProjection = camera.offsetProjection(projection, -currentScene.settings.separation / 2,
                abs(camera.Position.z) * currentScene.settings.convergence);
        }
        else {
            // New method
            GLfloat frustum[6];
            PerspectiveProjection(frustum, +1.0f, camera.Zoom, aspectRatio,
                currentScene.settings.nearPlane, currentScene.settings.farPlane,
                currentScene.settings.separation * 10, currentScene.settings.convergence);
            leftProjection = glm::frustum(frustum[0], frustum[1], frustum[2], frustum[3], frustum[4], frustum[5]);

            PerspectiveProjection(frustum, -1.0f, camera.Zoom, aspectRatio,
                currentScene.settings.nearPlane, currentScene.settings.farPlane,
                currentScene.settings.separation * 10, currentScene.settings.convergence);
            rightProjection = glm::frustum(frustum[0], frustum[1], frustum[2], frustum[3], frustum[4], frustum[5]);
        }

        // Set wireframe mode before rendering
        if (camera.wireframe) {
            glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
        }
        else {
            glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
        }
        // Adjust camera speed
        float distanceToNearestObject = camera.getDistanceToNearestObject(camera, projection, view, currentScene.settings.farPlane, windowWidth, windowHeight);
        camera.AdjustMovementSpeed(distanceToNearestObject, largestDimension, currentScene.settings.farPlane);
        camera.isMoving = false;  // Reset the moving flag at the end of each frame


        // Render for Right Eye (if in Stereo Mode)
        if (isStereoWindow) {
            renderEye(GL_BACK_LEFT, leftProjection, view, shader, viewport, windowFlags, window);
            renderEye(GL_BACK_RIGHT, rightProjection, view, shader, viewport, windowFlags, window);
        }
        else {
            renderEye(GL_BACK_LEFT, projection, view, shader, viewport, windowFlags, window);
        }


        // ---- Swap Buffers and Poll Events ----
        glfwSwapBuffers(window);
        glfwPollEvents();
    }

    // ---- Cleanup ----
    cleanup(shader);

    return 0;
}


// ---- Initialization and Cleanup -----
#pragma region Initialization and Cleanup
void cleanup(Shader* shader) {
    delete shader;
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    delete sphereShader;

    for (auto& pointCloud : currentScene.pointClouds) {
        for (auto& chunk : pointCloud.chunks) {
            glDeleteBuffers(chunk.lodVBOs.size(), chunk.lodVBOs.data());
        }
        glDeleteBuffers(1, &pointCloud.instanceVBO);
        glDeleteVertexArrays(1, &pointCloud.vao);
    }

    cleanupSkybox();

    glDeleteVertexArrays(1, &planeCursor.VAO);
    glDeleteBuffers(1, &planeCursor.VBO);
    glDeleteBuffers(1, &planeCursor.EBO);
    delete planeCursor.shader;

    glDeleteFramebuffers(1, &depthMapFBO);
    glDeleteTextures(1, &depthMap);
    delete simpleDepthShader;

    glfwTerminate();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext();
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


void renderEye(GLenum drawBuffer, const glm::mat4& projection, const glm::mat4& view, Shader* shader, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, GLFWwindow* window) {
    // Set the draw buffer and clear color and depth buffers
    glDrawBuffer(drawBuffer);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Reset OpenGL state
    glUseProgram(0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // 1. First render pass: Shadow map generation
    // 1. IMPORTANT: Update the voxel grid FIRST - this is crucial 
    // so visualization can use the latest voxel data
    //voxelizer->update(camera.Position, currentScene.models);

    // 2. First render pass: Shadow map generation
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

    // 3. Second pass: Regular rendering with shadows
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glViewport(0, 0, windowWidth, windowHeight);

    shader->use();
    shader->setMat4("projection", projection);
    shader->setMat4("view", view);
    shader->setVec3("viewPos", camera.Position);
    shader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    shader->setVec3("sun.direction", sun.direction);
    shader->setVec3("sun.color", sun.color);
    shader->setFloat("sun.intensity", sun.intensity);
    shader->setBool("sun.enabled", sun.enabled);

    // Shadow map binding
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_2D, depthMap);
    shader->setInt("shadowMap", 5);

    // Render scene
    renderModels(shader);
    renderPointClouds(shader);

    // Update cursor position after scene is rendered
    updateCursorPosition(window, projection, view, shader);

    // Set cursor uniforms after position update
    shader->setVec4("cursorPos", camera.IsOrbiting ?
        glm::vec4(capturedCursorPos, true) : // Always valid during orbiting
        glm::vec4(g_cursorPos, g_cursorValid ? 1.0f : 0.0f));

    updateFragmentShaderUniforms(shader);

    // Render orbit center if needed
    if (!orbitFollowsCursor && showOrbitCenter && camera.IsOrbiting) {
        renderOrbitCenter(projection, view);
    }

    renderSkybox(projection, view, shader);

    if (camera.IsPanning == false) {
        renderSphereCursor(projection, view);
        renderPlaneCursor(projection, view);
    }

    // 4. IMPORTANT: Voxel visualization AFTER main rendering
    // but don't update voxels again - we already did it at the beginning
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
}

void renderSphereCursor(const glm::mat4& projection, const glm::mat4& view) {
    if ((g_cursorValid || camera.IsOrbiting) && showSphereCursor) {
        // Enable blending and depth testing
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_DEPTH_TEST);

        sphereShader->use();
        sphereShader->setMat4("projection", projection);
        sphereShader->setMat4("view", view);
        sphereShader->setVec3("viewPos", camera.Position);

        // Use captured position when orbiting
        glm::vec3 cursorRenderPos = camera.IsOrbiting ? capturedCursorPos : g_cursorPos;
        float sphereRadius = calculateSphereRadius(cursorRenderPos, camera.Position);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), cursorRenderPos);
        model = glm::scale(model, glm::vec3(sphereRadius));

        sphereShader->setMat4("model", model);
        sphereShader->setFloat("innerSphereFactor", innerSphereFactor);

        glBindVertexArray(sphereVAO);

        // First pass: Render back faces
        glDepthMask(GL_TRUE);
        glCullFace(GL_FRONT);

        if (showInnerSphere) {
            sphereShader->setBool("isInnerSphere", true);
            sphereShader->setVec4("sphereColor", innerSphereColor);
            sphereShader->setFloat("transparency", 1.0);
            glm::mat4 innerModel = glm::scale(model, glm::vec3(innerSphereFactor));
            sphereShader->setMat4("model", innerModel);
            glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);
        }

        sphereShader->setBool("isInnerSphere", false);
        sphereShader->setVec4("sphereColor", cursorColor);
        sphereShader->setFloat("transparency", cursorTransparency);
        sphereShader->setFloat("edgeSoftness", cursorEdgeSoftness);
        sphereShader->setFloat("centerTransparencyFactor", cursorCenterTransparency);
        sphereShader->setMat4("model", model);
        glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);

        // Second pass: Render front faces
        glDepthMask(GL_FALSE);
        glCullFace(GL_BACK);

        if (showInnerSphere) {
            sphereShader->setBool("isInnerSphere", true);
            sphereShader->setVec4("sphereColor", innerSphereColor);
            sphereShader->setFloat("transparency", 1.0);
            glm::mat4 innerModel = glm::scale(model, glm::vec3(innerSphereFactor));
            sphereShader->setMat4("model", innerModel);
            glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);
        }

        sphereShader->setBool("isInnerSphere", false);
        sphereShader->setVec4("sphereColor", cursorColor);
        sphereShader->setFloat("transparency", cursorTransparency);
        sphereShader->setFloat("edgeSoftness", cursorEdgeSoftness);
        sphereShader->setFloat("centerTransparencyFactor", cursorCenterTransparency);
        sphereShader->setMat4("model", model);
        glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);

        // Reset OpenGL state
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);
        glDepthMask(GL_TRUE);
        glDisable(GL_BLEND);
        glBindVertexArray(0);
        glUseProgram(0);
    }
}

void renderOrbitCenter(const glm::mat4& projection, const glm::mat4& view) {
    if (!orbitFollowsCursor && showOrbitCenter && camera.IsOrbiting) {
        // Enable depth testing and blending
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Set up sphere shader for orbit center
        sphereShader->use();
        sphereShader->setMat4("projection", projection);
        sphereShader->setMat4("view", view);

        // Create model matrix for orbit center
        glm::mat4 model = glm::translate(glm::mat4(1.0f), camera.OrbitPoint);
        model = glm::scale(model, glm::vec3(orbitCenterSphereRadius));

        // Set shader uniforms
        sphereShader->setMat4("model", model);
        sphereShader->setVec3("viewPos", camera.Position);
        sphereShader->setVec4("sphereColor", orbitCenterColor);
        sphereShader->setFloat("transparency", 1.0f);
        sphereShader->setFloat("edgeSoftness", 0.0f);
        sphereShader->setFloat("centerTransparencyFactor", 0.0f);

        // Render orbit center sphere
        glBindVertexArray(sphereVAO);
        glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
    }
}

void renderSettingsWindow() {
    ImGui::SetNextWindowSize(ImVec2(400, 500), ImGuiCond_FirstUseEver);
    ImGui::Begin("Settings", &showSettingsWindow);
    bool settingsChanged = false;

    if (ImGui::BeginTabBar("SettingsTabs")) {

        /*
        if (ImGui::BeginTabItem("Voxelization")) {
            ImGui::Text("CURRENTLY DISABLED!!!");
            ImGui::Separator();
            ImGui::Text("Voxelization Settings");
            ImGui::Separator();

            ImGui::Checkbox("Show Voxel Visualization", &voxelizer->showDebugVisualization);

            static int currentState = 0;
            ImGui::Text("Visualization Mipmap Level:");
            if (ImGui::SliderInt("Level", &currentState, 0, 7)) {
                // Adjust the visualization state
                while (currentState > 0) {
                    voxelizer->increaseState();
                    currentState--;
                }
                while (currentState < 0) {
                    voxelizer->decreaseState();
                    currentState++;
                }
            }

            ImGui::Text("Controls:");
            ImGui::BulletText("V: Toggle visualization");
            ImGui::BulletText("Page Up/Down: Change mipmap level");

            float gridSize = voxelizer->getVoxelGridSize();
            if (ImGui::SliderFloat("Grid Size", &gridSize, 1.0f, 50.0f)) {
                voxelizer->setVoxelGridSize(gridSize);
            }

            ImGui::EndTabItem();
        }*/

        // Camera Tab
        if (ImGui::BeginTabItem("Camera")) {
            ImGui::Text("Stereo Settings");
            ImGui::Separator();

            if (ImGui::BeginCombo("Stereo Method", camera.useNewMethod ? "New Method" : "Legacy")) {
                if (ImGui::Selectable("Legacy", !camera.useNewMethod)) {
                    camera.useNewMethod = false;
                    currentScene.settings.separation = 0.02f;
                    preferences.useNewStereoMethod = false;
                    savePreferences();
                }
                if (ImGui::Selectable("New Method", camera.useNewMethod)) {
                    camera.useNewMethod = true;
                    currentScene.settings.separation = 0.005f;
                    preferences.useNewStereoMethod = true;
                    savePreferences();
                }
                ImGui::EndCombo();
            }
            ImGui::SetItemTooltip("Switch between legacy and new stereo rendering methods. New method provides better depth perception");

            float minSep = 0.0f;
            float maxSep = camera.useNewMethod ? 0.025f : maxSeparation;
            if (ImGui::SliderFloat("Separation", &currentScene.settings.separation, minSep, maxSep)) {
                preferences.separation = currentScene.settings.separation;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Adjusts the distance between stereo views. Higher values increase 3D effect");

            if (ImGui::SliderFloat("Convergence", &currentScene.settings.convergence, minConvergence, maxConvergence)) {
                preferences.convergence = currentScene.settings.convergence;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Sets the focal point distance where left and right views converge");

            ImGui::Spacing();
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

            ImGui::EndTabItem();
        }

        // Movement Tab
        if (ImGui::BeginTabItem("Movement")) {
            ImGui::Text("Mouse Settings");
            ImGui::Separator();
            if (ImGui::SliderFloat("Mouse Sensitivity", &camera.MouseSensitivity, 0.01f, 0.5f)) {
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

            ImGui::Spacing();
            ImGui::Text("Smooth Scrolling");
            ImGui::Separator();

            if (ImGui::Checkbox("Enable Smooth Scrolling", &camera.useSmoothScrolling)) {
                preferences.useSmoothScrolling = camera.useSmoothScrolling;
                if (!camera.useSmoothScrolling) {
                    camera.scrollVelocity = 0.0f;
                }
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Enables physics-based smooth scrolling instead of instant movement");

            if (camera.useSmoothScrolling) {
                ImGui::BeginGroup();
                ImGui::Text("Scroll Properties");
                if (ImGui::SliderFloat("Momentum", &camera.scrollMomentum, 0.1f, 5.0f)) {
                    preferences.scrollMomentum = camera.scrollMomentum;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Controls how quickly scroll speed builds up. Higher values feel more responsive");

                if (ImGui::SliderFloat("Max Speed", &camera.maxScrollVelocity, 0.1f, 10.0f)) {
                    preferences.maxScrollVelocity = camera.maxScrollVelocity;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Limits maximum scrolling speed for more controlled movement");

                if (ImGui::SliderFloat("Deceleration", &camera.scrollDeceleration, 0.1f, 10.0f)) {
                    preferences.scrollDeceleration = camera.scrollDeceleration;
                    settingsChanged = true;
                }
                ImGui::SetItemTooltip("Determines how quickly scrolling comes to a stop");
                ImGui::EndGroup();
            }

            ImGui::Spacing();
            ImGui::Text("Orbiting Behavior");
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
            ImGui::EndTabItem();
        }

        // Environment Tab
        if (ImGui::BeginTabItem("Environment")) {
            bool settingsChanged = false;
            ImGui::Text("Skybox Settings");
            ImGui::Separator();

            // Skybox type dropdown
            const char* skyboxTypes[] = { "Cubemap Texture", "Solid Color", "Gradient" };
            int currentType = static_cast<int>(skyboxConfig.type);
            if (ImGui::Combo("Skybox Type", &currentType, skyboxTypes, IM_ARRAYSIZE(skyboxTypes))) {
                skyboxConfig.type = static_cast<SkyboxType>(currentType);
                updateSkybox();

                // Save the preferences
                preferences.skyboxType = skyboxConfig.type;
                savePreferences();
            }
            ImGui::SetItemTooltip("Change the type of skybox used in the scene");

            // Type-specific controls
            if (skyboxConfig.type == SKYBOX_CUBEMAP) {
                // Create a vector of preset names for the combo box
                std::vector<const char*> presetNames;
                for (const auto& preset : cubemapPresets) {
                    presetNames.push_back(preset.name.c_str());
                }

                if (ImGui::Combo("Cubemap Theme", &skyboxConfig.selectedCubemap,
                    presetNames.data(), static_cast<int>(presetNames.size()))) {
                    updateSkybox();

                    // Save the preferences
                    preferences.selectedCubemap = skyboxConfig.selectedCubemap;
                    savePreferences();
                }

                // Display description as tooltip
                if (skyboxConfig.selectedCubemap >= 0 && skyboxConfig.selectedCubemap < cubemapPresets.size()) {
                    ImGui::SetItemTooltip("%s", cubemapPresets[skyboxConfig.selectedCubemap].description.c_str());
                }

                if (ImGui::Button("Browse Custom Skybox")) {
                    auto selection = pfd::select_folder("Select skybox directory").result();
                    if (!selection.empty()) {
                        std::string path = selection + "/";

                        // Create a name from the directory name
                        std::string dirName = std::filesystem::path(selection).filename().string();
                        std::string name = "Custom: " + dirName;

                        // Add to presets
                        cubemapPresets.push_back({ name, path, "Custom skybox from: " + path });
                        skyboxConfig.selectedCubemap = static_cast<int>(cubemapPresets.size()) - 1;
                        updateSkybox();

                        // Save the preferences (including new cubemap preset in preferences.json)
                        preferences.selectedCubemap = skyboxConfig.selectedCubemap;
                        savePreferences();
                    }
                }
                ImGui::SetItemTooltip("Select a directory containing skybox textures (right.jpg, left.jpg, etc. OR posx.jpg, negx.jpg, etc.)");
            }
            else if (skyboxConfig.type == SKYBOX_SOLID_COLOR) {
                if (ImGui::ColorEdit3("Skybox Color", glm::value_ptr(skyboxConfig.solidColor))) {
                    updateSkybox();

                    // Save the preferences
                    preferences.skyboxSolidColor = skyboxConfig.solidColor;
                    savePreferences();
                }
                ImGui::SetItemTooltip("Set a single color for the entire skybox");
            }
            else if (skyboxConfig.type == SKYBOX_GRADIENT) {
                bool colorChanged = false;
                colorChanged |= ImGui::ColorEdit3("Top Color", glm::value_ptr(skyboxConfig.gradientTopColor));
                ImGui::SetItemTooltip("Color of the top portion of the skybox");

                colorChanged |= ImGui::ColorEdit3("Bottom Color", glm::value_ptr(skyboxConfig.gradientBottomColor));
                ImGui::SetItemTooltip("Color of the bottom portion of the skybox");

                if (colorChanged) {
                    updateSkybox();

                    // Save the preferences
                    preferences.skyboxGradientTop = skyboxConfig.gradientTopColor;
                    preferences.skyboxGradientBottom = skyboxConfig.gradientBottomColor;
                    savePreferences();
                }
            }

            ImGui::Spacing();
            if (ImGui::SliderFloat("Ambient Strength", &ambientStrengthFromSkybox, 0.0f, 1.0f)) {
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Controls how much the skybox illuminates the scene. Higher values create brighter ambient lighting");

            ImGui::Spacing();
            ImGui::Text("Sun Settings");
            ImGui::Separator();
            settingsChanged |= ImGui::ColorEdit3("Sun Color", glm::value_ptr(sun.color));
            ImGui::SetItemTooltip("Sets the color of sunlight in the scene");

            settingsChanged |= ImGui::SliderFloat("Sun Intensity", &sun.intensity, 0.0f, 1.0f);
            ImGui::SetItemTooltip("Controls the brightness of sunlight");

            settingsChanged |= ImGui::DragFloat3("Sun Direction", glm::value_ptr(sun.direction), 0.01f, -1.0f, 1.0f);
            ImGui::SetItemTooltip("Sets the direction of sunlight. Affects shadows and lighting");

            settingsChanged |= ImGui::Checkbox("Enable Sun", &sun.enabled);
            ImGui::SetItemTooltip("Toggles sun lighting on/off");

            if (settingsChanged) {
                savePreferences();
            }

            ImGui::EndTabItem();
        }

        // Display Tab
        if (ImGui::BeginTabItem("Display")) {
            ImGui::Text("Interface Settings");
            ImGui::Separator();

            if (ImGui::Checkbox("Show FPS Counter", &showFPS)) {
                preferences.showFPS = showFPS;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Shows/hides the FPS counter in the top-right corner");

            if (ImGui::Checkbox("Dark Theme", &isDarkTheme)) {
                SetupImGuiStyle(isDarkTheme, 1.0f);
                preferences.isDarkTheme = isDarkTheme;
                settingsChanged = true;
            }
            ImGui::SetItemTooltip("Switches between light and dark color themes for the interface");

            ImGui::Spacing();
            ImGui::Text("Rendering Settings");
            ImGui::Separator();
            ImGui::Checkbox("Wireframe Mode", &camera.wireframe);
            ImGui::SetItemTooltip("Renders objects as wireframes instead of solid surfaces");

            ImGui::EndTabItem();
        }

        // Keybinds Tab
        if (ImGui::BeginTabItem("Keybinds")) {
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
            ImGui::EndTabItem();
        }

        ImGui::EndTabBar();
    }

    if (settingsChanged) {
        savePreferences();
    }

    ImGui::End();
}

void renderMeshManipulationPanel(Model& model, int meshIndex, Shader* shader) {
    auto& mesh = model.getMeshes()[meshIndex];

    ImGui::Text("Mesh Manipulation: %s - Mesh %d", model.name.c_str(), meshIndex + 1);
    ImGui::Separator();

    ImGui::Checkbox("Visible", &mesh.visible);

    // Material properties specific to this mesh
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", glm::value_ptr(mesh.color));
        ImGui::SliderFloat("Shininess", &mesh.shininess, 1.0f, 90.0f);
        ImGui::SliderFloat("Emissive", &mesh.emissive, 0.0f, 1.0f);
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

void renderGUI(bool isLeftEye, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, Shader* shader) {
    if (!isLeftEye) {
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        return;
    }

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
            if (ImGui::MenuItem("Open")) {
                auto selection = pfd::open_file("Select a file to open", ".",
                    { "All Supported Files", "*.obj *.fbx *.3ds *.gltf *.glb",
                      "3D Models", "*.obj *.fbx *.3ds *.gltf *.glb",
                      "Point Cloud Files", "*.txt *.xyz *.ply *.pcb",
                      "All Files", "*" }).result();

                if (!selection.empty()) {
                    std::string filePath = selection[0];
                    std::string extension = std::filesystem::path(filePath).extension().string();

                    if (extension == ".obj" || extension == ".fbx" || extension == ".3ds" ||
                        extension == ".gltf" || extension == ".glb") {
                        try {
                            Engine::Model newModel = *Engine::loadModel(filePath);
                            currentScene.models.push_back(newModel);
                            currentModelIndex = currentScene.models.size() - 1;
                        }
                        catch (const std::exception& e) {
                            std::cerr << "Failed to load model: " << e.what() << std::endl;
                        }
                    }
                    else if (extension == ".txt" || extension == ".xyz" || extension == ".ply") {
                        PointCloud newPointCloud = Engine::PointCloudLoader::loadPointCloudFile(filePath);
                        newPointCloud.filePath = filePath;
                        currentScene.pointClouds.push_back(newPointCloud);
                    }
                    else if (extension == ".pcb") {
                        PointCloud newPointCloud = Engine::PointCloudLoader::loadFromBinary(filePath);
                        if (!newPointCloud.points.empty()) {
                            newPointCloud.filePath = filePath;
                            newPointCloud.name = std::filesystem::path(filePath).stem().string();
                            currentScene.pointClouds.push_back(newPointCloud);
                        }
                        else {
                            std::cerr << "Failed to load point cloud from: " << filePath << std::endl;
                        }
                    }
                }
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Scene")) {
            if (ImGui::MenuItem("Load")) {
                auto selection = pfd::open_file("Select a scene file to load", ".",
                    { "Scene Files", "*.scene", "All Files", "*" }).result();
                if (!selection.empty()) {
                    try {
                        currentScene = Engine::loadScene(selection[0]);
                        currentModelIndex = currentScene.models.empty() ? -1 : 0;
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load scene: " << e.what() << std::endl;
                    }
                }
            }
            if (ImGui::MenuItem("Save")) {
                auto destination = pfd::save_file("Select a file to save scene", ".",
                    { "Scene Files", "*.scene", "All Files", "*" }).result();
                if (!destination.empty()) {
                    try {
                        Engine::saveScene(destination, currentScene);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to save scene: " << e.what() << std::endl;
                    }
                }
            }
            ImGui::Separator();
            if (ImGui::MenuItem("Create Cube")) {
                Model newCube = Engine::createCube(glm::vec3(1.0f, 1.0f, 1.0f), 1.0f, 0.0f);
                newCube.scale = glm::vec3(0.5f);
                newCube.position = glm::vec3(0.0f, 0.0f, 0.0f);
                currentScene.models.push_back(newCube);
                currentSelectedIndex = currentScene.models.size() - 1;
                currentSelectedType = SelectedType::Model;
            }
            ImGui::EndMenu();
        }if (ImGui::BeginMenu("View")) {
            ImGui::MenuItem("Show FPS", nullptr, &showFPS);
            ImGui::MenuItem("Wireframe Mode", nullptr, &camera.wireframe);
            ImGui::MenuItem("Show GUI", nullptr, &showGui);
            if (ImGui::BeginMenu("Theme")) {
                if (ImGui::MenuItem("Light Theme", nullptr, !isDarkTheme)) {
                    isDarkTheme = false;
                    SetupImGuiStyle(isDarkTheme, 1.0f);
                    preferences.isDarkTheme = isDarkTheme;
                    savePreferences();
                }
                if (ImGui::MenuItem("Dark Theme", nullptr, isDarkTheme)) {
                    isDarkTheme = true;
                    SetupImGuiStyle(isDarkTheme, 1.0f);
                    preferences.isDarkTheme = isDarkTheme;
                    savePreferences();
                }
                ImGui::EndMenu();
            }
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Camera")) {
            if (ImGui::BeginCombo("Stereo Method", camera.useNewMethod ? "New Method" : "Legacy")) {
                if (ImGui::Selectable("Legacy", !camera.useNewMethod)) {
                    camera.useNewMethod = false;
                    currentScene.settings.separation = 0.02f;
                    preferences.useNewStereoMethod = false;
                    savePreferences();
                }
                if (ImGui::Selectable("New Method", camera.useNewMethod)) {
                    camera.useNewMethod = true;
                    currentScene.settings.separation = 0.005f;
                    preferences.useNewStereoMethod = true;
                    savePreferences();
                }
                ImGui::EndCombo();
            }
            ImGui::MenuItem("Smooth Scrolling", nullptr, &camera.useSmoothScrolling);
            ImGui::EndMenu();
        }

        if (ImGui::BeginMenu("Cursor")) {
            ImGui::MenuItem("Show Sphere Cursor", nullptr, &showSphereCursor);
            ImGui::MenuItem("Show Fragment Cursor", nullptr, &showFragmentCursor);
            ImGui::MenuItem("Show Plane Cursor", nullptr, &planeCursor.show);
            ImGui::Separator();

            bool standardOrbit = !camera.orbitAroundCursor && !orbitFollowsCursor;
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
            ImGui::Separator();

            if (ImGui::BeginMenu("Presets")) {
                std::vector<std::string> presetNames = Engine::CursorPresetManager::getPresetNames();
                for (const auto& name : presetNames) {
                    if (ImGui::MenuItem(name.c_str(), nullptr, currentPresetName == name)) {
                        currentPresetName = name;
                        Engine::CursorPreset loadedPreset = Engine::CursorPresetManager::applyCursorPreset(name);
                        applyPresetToGlobalSettings(loadedPreset);
                        preferences.currentPresetName = currentPresetName;
                        savePreferences();
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Cursor Settings")) {
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

void renderCursorSettingsWindow() {
    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("3D Cursor Settings", &showCursorSettingsWindow);

    // Preset management
    if (ImGui::BeginCombo("Cursor Preset", currentPresetName.c_str())) {
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
                    applyPresetToGlobalSettings(loadedPreset);
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
                    // Rename existing preset or save new preset
                    Engine::CursorPreset newPreset = createPresetFromCurrentSettings(newName);
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

    ImGui::SameLine();
    if (ImGui::Button("Update Preset")) {
        Engine::CursorPreset updatedPreset = createPresetFromCurrentSettings(currentPresetName);
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
                applyPresetToGlobalSettings(loadedPreset);
            }
            else {
                currentPresetName = "Default";
                // Reset to default settings
                setDefaultCursorSettings();
            }
        }
    }

    ImGui::Separator();

    if (ImGui::CollapsingHeader("Orbit Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Camera Orbit Behavior:");

        bool standardOrbit = !orbitFollowsCursor && !camera.orbitAroundCursor;
        bool orbitAroundCursorOption = camera.orbitAroundCursor;
        bool orbitFollowsCursorOption = orbitFollowsCursor;

        if (ImGui::RadioButton("Standard Orbit", standardOrbit)) {
            // Enable standard orbit - disable the other two
            camera.orbitAroundCursor = false;
            orbitFollowsCursor = false;
            preferences.orbitAroundCursor = false;
            preferences.orbitFollowsCursor = false;
            savePreferences();
        }
        ImGui::SetItemTooltip("Orbits around the viewport center at cursor depth");

        if (ImGui::RadioButton("Orbit Around Cursor", orbitAroundCursorOption)) {
            // Enable orbit around cursor - disable the other one
            camera.orbitAroundCursor = true;
            orbitFollowsCursor = false;
            preferences.orbitAroundCursor = true;
            preferences.orbitFollowsCursor = false;
            savePreferences();
        }
        ImGui::SetItemTooltip("Orbits around the 3D cursor position without centering the view");

        if (ImGui::RadioButton("Orbit Follows Cursor (Center)", orbitFollowsCursorOption)) {
            // Enable orbit follows cursor - disable the other one
            camera.orbitAroundCursor = false;
            orbitFollowsCursor = true;
            preferences.orbitAroundCursor = false;
            preferences.orbitFollowsCursor = true;
            savePreferences();
        }
        ImGui::SetItemTooltip("Centers the view on cursor position before orbiting");

        ImGui::Separator();
        ImGui::Checkbox("Show Orbit Center", &showOrbitCenter);

        if (showOrbitCenter) {
            ImGui::ColorEdit4("Orbit Center Color", glm::value_ptr(orbitCenterColor));
            ImGui::SliderFloat("Orbit Center Size", &orbitCenterSphereRadius, 0.01f, 1.0f);
        }
    }

    ImGui::Separator();

    // 3D Sphere Cursor Settings
    if (ImGui::CollapsingHeader("3D Sphere Cursor", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show 3D Sphere Cursor", &showSphereCursor);

        if (showSphereCursor) {
            if (!orbitFollowsCursor) {
                ImGui::Checkbox("Show Orbit Center", &showOrbitCenter);
                if (showOrbitCenter) {
                    ImGui::ColorEdit4("Orbit Center Color", glm::value_ptr(orbitCenterColor));
                    ImGui::SliderFloat("Orbit Center Size", &orbitCenterSphereRadius, 0.01f, 1.0f);
                }
            }

            const char* scalingModes[] = { "Normal", "Fixed", "Constrained Dynamic", "Logarithmic" };
            int currentMode = static_cast<int>(currentCursorScalingMode);
            if (ImGui::Combo("Cursor Scaling Mode", &currentMode, scalingModes, IM_ARRAYSIZE(scalingModes))) {
                currentCursorScalingMode = static_cast<CursorScalingMode>(currentMode);
            }

            ImGui::SliderFloat("Fixed Sphere Radius", &fixedSphereRadius, 0.01f, 3.0f);

            if (currentCursorScalingMode == CURSOR_CONSTRAINED_DYNAMIC) {
                ImGui::SliderFloat("Min Difference", &minDiff, 0.001f, 0.1f);
                ImGui::SliderFloat("Max Difference", &maxDiff, 0.01f, 1.0f);
            }

            ImGui::ColorEdit4("Cursor Color", glm::value_ptr(cursorColor));
            ImGui::SliderFloat("Cursor Transparency", &cursorTransparency, 0.0f, 1.0f);
            ImGui::SliderFloat("Edge Softness", &cursorEdgeSoftness, 0.0f, 1.0f);
            ImGui::SliderFloat("Center Transparency", &cursorCenterTransparency, 0.0f, 1.0f);

            ImGui::Checkbox("Show Inner Sphere", &showInnerSphere);
            if (showInnerSphere) {
                ImGui::ColorEdit4("Inner Sphere Color", glm::value_ptr(innerSphereColor));
                ImGui::SliderFloat("Inner Sphere Factor", &innerSphereFactor, 0.1f, 0.9f);
            }
        }
    }

    // Fragment Shader Cursor Settings
    if (ImGui::CollapsingHeader("Fragment Shader Cursor", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Fragment Shader Cursor", &showFragmentCursor);

        if (showFragmentCursor) {
            ImGui::SliderFloat("Outer Radius", &fragmentCursorSettings.baseOuterRadius, 0.0f, 0.3f);
            ImGui::SliderFloat("Outer Border Thickness", &fragmentCursorSettings.baseOuterBorderThickness, 0.0f, 0.08f);
            ImGui::SliderFloat("Inner Radius", &fragmentCursorSettings.baseInnerRadius, 0.0f, 0.2f);
            ImGui::SliderFloat("Inner Border Thickness", &fragmentCursorSettings.baseInnerBorderThickness, 0.0f, 0.08f);
            ImGui::ColorEdit4("Outer Color", glm::value_ptr(fragmentCursorSettings.outerColor));
            ImGui::ColorEdit4("Inner Color", glm::value_ptr(fragmentCursorSettings.innerColor));
        }
    }

    if (ImGui::CollapsingHeader("Plane Cursor", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show Plane Cursor", &planeCursor.show);
        if (planeCursor.show) {
            ImGui::ColorEdit4("Plane Color", glm::value_ptr(planeCursor.color));
            ImGui::SliderFloat("Plane Diameter", &planeCursor.diameter, 0.f, 5.0f);
        }
    }

    ImGui::End();
}

void renderModelManipulationPanel(Engine::Model& model, Shader* shader) {
    ImGui::Text("Model Manipulation: %s", model.name.c_str());
    ImGui::Separator();

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", glm::value_ptr(model.position), 0.1f);
        ImGui::DragFloat3("Scale", glm::value_ptr(model.scale), 0.01f, 0.01f, 100.0f);
        ImGui::DragFloat3("Rotation", glm::value_ptr(model.rotation), 1.0f, -360.0f, 360.0f);
    }

    // Material
    if (ImGui::CollapsingHeader("Material", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::ColorEdit3("Color", glm::value_ptr(model.color));
        ImGui::SliderFloat("Shininess", &model.shininess, 1.0f, 90.0f);
        ImGui::SliderFloat("Emissive", &model.emissive, 0.0f, 1.0f);
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

void renderPointCloudManipulationPanel(Engine::PointCloud& pointCloud) {
    ImGui::Text("Point Cloud Manipulation: %s", pointCloud.name.c_str());
    if (pointCloud.points.empty()) {
        ImGui::Text("Point cloud is empty");
        return;
    }
    ImGui::Separator();

    // Transform
    if (ImGui::CollapsingHeader("Transform", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::DragFloat3("Position", glm::value_ptr(pointCloud.position), 0.1f);
        ImGui::DragFloat3("Rotation", glm::value_ptr(pointCloud.rotation), 1.0f, -360.0f, 360.0f);
        ImGui::DragFloat3("Scale", glm::value_ptr(pointCloud.scale), 0.01f, 0.01f, 100.0f);
    }


    // Point Cloud specific settings
    if (ImGui::CollapsingHeader("Point Cloud Settings", ImGuiTreeNodeFlags_DefaultOpen)) {
        // any point cloud specific settings here
        // For example:
        ImGui::SliderFloat("Base Point Size", &pointCloud.basePointSize, 1.0f, 10.0f);
        // ImGui::ColorEdit3("Point Color", glm::value_ptr(pointCloud.color));
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
                isRecalculatingChunks = true;
                pointCloud.chunkSize = pointCloud.newChunkSize;
                generateChunks(pointCloud, pointCloud.chunkSize);
                isRecalculatingChunks = false;
            }
        }

        if (isRecalculatingChunks) {
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

void deleteSelectedPointCloud() {
    if (currentSelectedType == SelectedType::PointCloud && currentSelectedIndex >= 0 && currentSelectedIndex < currentScene.pointClouds.size()) {
        // Clean up OpenGL resources
        glDeleteVertexArrays(1, &currentScene.pointClouds[currentSelectedIndex].vao);
        glDeleteBuffers(1, &currentScene.pointClouds[currentSelectedIndex].vbo);

        // Remove from the vector
        currentScene.pointClouds.erase(currentScene.pointClouds.begin() + currentSelectedIndex);
        currentSelectedIndex = -1;
        currentSelectedType = SelectedType::None;
    }
}


void renderModels(Shader* shader) {
    // Only update lighting information for the main shader, not the depth shader
    if (shader != simpleDepthShader) {

        bindSkyboxUniforms(shader);

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

    // Calculate view projection matrix for frustum culling
    glm::mat4 viewProj;
    if (shader != simpleDepthShader) {
        viewProj = camera.GetProjectionMatrix(aspectRatio, currentScene.settings.nearPlane, currentScene.settings.farPlane)
            * camera.GetViewMatrix();
    }

    // Render all models
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

        shader->setMat4("model", modelMatrix);
        shader->setBool("useInstancing", false);

        // Set material properties
        shader->setBool("material.hasNormalMap", model.hasNormalMap());
        shader->setBool("material.hasSpecularMap", model.hasSpecularMap());
        shader->setBool("material.hasAOMap", model.hasAOMap());
        shader->setFloat("material.hasTexture", !model.getMeshes().empty() && !model.getMeshes()[0].textures.empty() ? 1.0f : 0.0f);
        shader->setVec3("material.objectColor", model.color);
        shader->setFloat("material.shininess", model.shininess);
        shader->setFloat("material.emissive", model.emissive);

        // Set selection state
        shader->setBool("selectionMode", selectionMode);
        shader->setBool("isSelected", selectionMode && (i == currentSelectedIndex) && (currentSelectedType == SelectedType::Model));

        // Draw the model
        model.Draw(*shader);
    }
}

void renderPointClouds(Shader* shader) {
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
void updateFragmentShaderUniforms(Shader* shader) {
    // Set fragment shader uniforms for cursor rendering
    shader->setFloat("baseOuterRadius", showFragmentCursor ? fragmentCursorSettings.baseOuterRadius : 0.0f);
    shader->setFloat("baseOuterBorderThickness", showFragmentCursor ? fragmentCursorSettings.baseOuterBorderThickness : 0.0f);
    shader->setFloat("baseInnerRadius", showFragmentCursor ? fragmentCursorSettings.baseInnerRadius : 0.0f);
    shader->setFloat("baseInnerBorderThickness", showFragmentCursor ? fragmentCursorSettings.baseInnerBorderThickness : 0.0f);
    shader->setVec4("outerCursorColor", showFragmentCursor ? fragmentCursorSettings.outerColor : glm::vec4(0.0f));
    shader->setVec4("innerCursorColor", showFragmentCursor ? fragmentCursorSettings.innerColor : glm::vec4(0.0f));
    shader->setBool("showFragmentCursor", showFragmentCursor);
}

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


// ---- Cursor and Ray Casting ----
#pragma region Cursor and Ray Casting
void updateCursorPosition(GLFWwindow* window, const glm::mat4& projection, const glm::mat4& view, Shader* shader) {

    if (ImGui::GetIO().WantCaptureMouse) {
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        return;
    }

    // Only update cursor position during left eye rendering
    static bool isLeftEye = true;
    if (!isLeftEye) {
        isLeftEye = true;
        return;
    }
    isLeftEye = false;

    // During orbiting, maintain cursor at the captured position
    if (camera.IsOrbiting) {
        g_cursorValid = true;
        g_cursorPos = capturedCursorPos;
        return;
    }

    // Only update if not animating
    if (camera.IsAnimating) {
        return;
    }

    // Read depth at cursor position
    float depth = 0.0;
    glReadPixels(lastX, (float)windowHeight - lastY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

    // Convert cursor position to world space
    glm::mat4 vpInv = glm::inverse(projection * view);
    glm::vec4 ndc = glm::vec4((lastX / (float)windowWidth) * 2.0 - 1.0, 1.0 - (lastY / (float)windowHeight) * 2.0, depth * 2.0 - 1.0, 1.0);
    auto worldPosH = vpInv * ndc;
    auto worldPos = worldPosH / worldPosH.w;
    auto isHit = depth != 1.0;


    if (isHit && (showSphereCursor || showFragmentCursor || planeCursor.show)) {
        g_cursorValid = true;
        g_cursorPos = glm::vec3(worldPos);
        if (camera.IsPanning || rightMousePressed) return;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);

    }
    else {
        g_cursorValid = false;

        if (camera.IsPanning || rightMousePressed) return;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

    }
}

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

            // M�ller�Trumbore intersection algorithm
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



// ---- Model Management ----
#pragma region Model Management
void deleteSelectedModel() {
    if (currentSelectedType == SelectedType::Model && currentSelectedIndex >= 0 && currentSelectedIndex < currentScene.models.size()) {
        currentScene.models.erase(currentScene.models.begin() + currentSelectedIndex);
        currentSelectedIndex = -1;
        currentSelectedType = SelectedType::None;
    }
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
        camera.UpdateCursorInfo(g_cursorPos, g_cursorValid);
        camera.ProcessMouseScroll(yoffset);
    }
}

bool firstMouse = true;


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
            }

            // Handle double-click (if not in selection mode)
            if (!selectionMode) {
                double currentTime = glfwGetTime();
                if (currentTime - lastClickTime < doubleClickTime) {
                    if (g_cursorValid) {
                        camera.StartCenteringAnimation(g_cursorPos);
                        glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
                    }
                }
                lastClickTime = currentTime;
            }

            if (!camera.IsAnimating && !camera.IsOrbiting && !selectionMode) {
                leftMousePressed = true;

                if (g_cursorValid) {
                    // Different orbiting behaviors based on settings
                    if (camera.orbitAroundCursor) {
                        camera.UpdateCursorInfo(g_cursorPos, g_cursorValid);
                        camera.StartOrbiting(true); // Pass true to use current cursor position
                        capturedCursorPos = g_cursorPos;
                        // Enable mouse capture when orbiting starts
                        isMouseCaptured = true;
                        firstMouse = true; // Reset the first mouse flag
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        // Center cursor
                        glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
                    }
                    else if (orbitFollowsCursor && showSphereCursor) {
                        camera.StartCenteringAnimation(g_cursorPos);
                        capturedCursorPos = g_cursorPos;
                    }
                    else {
                        float cursorDepth = glm::length(g_cursorPos - camera.Position);
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
    // Skip if ImGui is capturing mouse
    if (ImGui::GetIO().WantCaptureMouse) {
        firstMouse = true;
        return;
    }

    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    // When mouse is captured for orbit, panning, or rotation
    if (isMouseCaptured) {
        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
            return;
        }

        // Calculate deltas directly
        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos; // Reversed since y-coordinates range from bottom to top

        // Update last positions for next frame
        lastX = xpos;
        lastY = ypos;


        // Apply smoothing
        xoffset *= mouseSmoothingFactor;
        yoffset *= mouseSmoothingFactor;

        // Apply sensitivity scaling
        xoffset *= camera.MouseSensitivity;
        yoffset *= camera.MouseSensitivity;

        // Clamp to prevent extreme movements
        const float maxMovement = 5.0f;
        xoffset = glm::clamp(xoffset, -maxMovement, maxMovement);
        yoffset = glm::clamp(yoffset, -maxMovement, maxMovement);

        // Apply movement based on mode
        if (isMovingModel && currentSelectedType == SelectedType::Model && currentSelectedIndex != -1) {
            // Move selected model
            float distanceToModel = glm::distance(camera.Position, currentScene.models[currentSelectedIndex].position);
            float normalizedXOffset = xoffset / static_cast<float>(windowWidth);
            float normalizedYOffset = yoffset / static_cast<float>(windowHeight);
            float baseSensitivity = 0.7f;
            float sensitivityFactor = (baseSensitivity * distanceToModel);
            normalizedXOffset *= aspectRatio;

            glm::vec3 right = glm::normalize(glm::cross(camera.Front, camera.Up));
            glm::vec3 up = glm::normalize(glm::cross(right, camera.Front));

            currentScene.models[currentSelectedIndex].position += right * normalizedXOffset * sensitivityFactor;
            currentScene.models[currentSelectedIndex].position += up * normalizedYOffset * sensitivityFactor;
        }
        else if (camera.IsOrbiting && !camera.IsAnimating) {
            // When orbiting, pass deltas directly to camera
            camera.ProcessMouseMovement(xoffset, yoffset);
        }
        else if ((leftMousePressed || rightMousePressed || middleMousePressed) && !camera.IsAnimating) {
            camera.ProcessMouseMovement(xoffset, yoffset);
        }
    }
    else {
        // For non-captured mouse behavior (fallback)
        if (firstMouse) {
            lastX = xpos;
            lastY = ypos;
            firstMouse = false;
            return;
        }

        float xoffset = xpos - lastX;
        float yoffset = lastY - ypos;

        lastX = xpos;
        lastY = ypos;

        if (isMovingModel && currentSelectedType == SelectedType::Model && currentSelectedIndex != -1) {
            // Move selected model
            float distanceToModel = glm::distance(camera.Position, currentScene.models[currentSelectedIndex].position);
            float normalizedXOffset = xoffset / static_cast<float>(windowWidth);
            float normalizedYOffset = yoffset / static_cast<float>(windowHeight);
            float baseSensitivity = 0.7f;
            float sensitivityFactor = (baseSensitivity * distanceToModel);
            normalizedXOffset *= aspectRatio;

            glm::vec3 right = glm::normalize(glm::cross(camera.Front, camera.Up));
            glm::vec3 up = glm::normalize(glm::cross(right, camera.Front));

            currentScene.models[currentSelectedIndex].position += right * normalizedXOffset * sensitivityFactor;
            currentScene.models[currentSelectedIndex].position += up * normalizedYOffset * sensitivityFactor;
        }
        else if (camera.IsOrbiting && !camera.IsAnimating) {
            // Handle camera orbiting
            camera.ProcessMouseMovement(xoffset, yoffset);
            glm::vec3 directionToCamera = glm::normalize(camera.Position - camera.OrbitPoint);
            camera.Position = camera.OrbitPoint + directionToCamera * camera.OrbitDistance;
        }
        else if ((leftMousePressed || rightMousePressed || middleMousePressed) && !camera.IsAnimating) {
            // Handle regular camera movement
            camera.ProcessMouseMovement(xoffset, yoffset);
        }
    }
}

void key_callback(GLFWwindow* window, int key, int scancode, int action, int mods)
{
    if (key == GLFW_KEY_G && action == GLFW_PRESS)
    {
        showGui = !showGui;
        std::cout << "GUI visibility toggled. showGui = " << (showGui ? "true" : "false") << std::endl;
    }

    if (key == GLFW_KEY_PAGE_UP && action == GLFW_PRESS)
    {
        voxelizer->increaseState();
    }
    if (key == GLFW_KEY_PAGE_DOWN && action == GLFW_PRESS)
    {
        voxelizer->decreaseState();
    }
    if (key == GLFW_KEY_V && action == GLFW_PRESS)
    {
        voxelizer->showDebugVisualization = !voxelizer->showDebugVisualization;
    }

    if (key == GLFW_KEY_C && action == GLFW_PRESS)
    {
        // Try to find a good center point
        glm::vec3 centerPoint(0.0f);
        int objectCount = 0;

        // First, try to use the current cursor position if valid
        if (g_cursorValid) {
            glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
            camera.StartCenteringAnimation(g_cursorPos);
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
        deleteSelectedModel();
        std::cout << "Deleted selected model" << std::endl;
    }
}
#pragma endregion