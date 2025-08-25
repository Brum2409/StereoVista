// ---- Core Definitions ----
#define NOMINMAX
#include "Engine/Core.h"
#include <thread>
#include <atomic>
#include <iostream>

// ---- Project-Specific Includes ----
#include "Loaders/ModelLoader.h"
#include "Core/Camera.h"
#include "Core/SceneManager.h"
#include "Cursors/CursorPresets.h"
#include "Loaders/PointCloudLoader.h"
#include "Cursors/Base/CursorManager.h"
#include "Core/Voxalizer.h"
#include "Engine/OctreePointCloudManager.h"
#include "Engine/SpaceMouseInput.h"
#include "Gui/Gui.h"
#include "Gui/GuiTypes.h"
#include "../headers/Engine/BVH.h"
#include "../headers/Engine/BVHDebug.h"

// ---- GUI and Dialog ----
#include "imgui/imgui_incl.h"
#include "imgui/imgui_sytle.h"
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
void renderZeroPlane(Engine::Shader* shader, const glm::mat4& projection, const glm::mat4& view, float convergence);
void DrawRadar(bool isStereoWindow, Camera camera, GLfloat focaldist, 
    glm::mat4 view, glm::mat4 projection, 
    glm::mat4 leftview, glm::mat4 leftprojection,
    glm::mat4 rightview, glm::mat4 rightprojection,
    Engine::Shader* shader,
    bool renderScene, float radarScale, glm::vec2 position);
glm::vec4 divw(glm::vec4 vec);

// ---- Update Functions ----
void updatePointLights();
void updateSpaceMouseBounds();
void updateSpaceMouseCursorAnchor();

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
// Separate camera for SpaceMouse to prevent navlib from overriding normal input
Camera spaceMouseCamera(glm::vec3(0.0f, 0.0f, 3.0f));
std::shared_ptr<Camera> spaceMouseCameraPtr = std::make_shared<Camera>(spaceMouseCamera);
SpaceMouseInput spaceMouseInput;
bool spaceMouseInitialized = false;
bool spaceMouseActive = false;
float lastX = 1920.0f / 2.0;
float lastY = 1080.0f / 2.0;
float aspectRatio = 1.0f;
float mouseSmoothingFactor = 0.7;

// ---- Stereo Rendering Settings ----
float maxSeparation = 2.0f;   // Maximum stereo separation
float minSeparation = 0.01f; // Minimum stereo separation

// The convergence will shift the zFokus but there is still some weirdness when going into negative
float minConvergence = 0.0f;  // Minimum convergence
float maxConvergence = 40.0f;   // Maximum convergence

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
Engine::Shader* radianceShader = nullptr;

GUI::LightingMode currentLightingMode = GUI::LIGHTING_SHADOW_MAPPING;
bool enableShadows = true;

GUI::VCTSettings vctSettings;
GUI::ApplicationPreferences::RadianceSettings radianceSettings;

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

// ---- Raytracing Triangle Buffer ----
GLuint triangleSSBO = 0;
std::vector<float> triangleData;

// ---- BVH System ----
Engine::BVHBuilder bvhBuilder;
GLuint bvhNodeSSBO = 0;
GLuint triangleIndexSSBO = 0;
std::vector<Engine::GPUBVHNode> gpuBVHNodes;
std::vector<uint32_t> gpuTriangleIndices;
std::vector<Engine::GPUTriangle> gpuTriangles;
bool bvhBuilt = false;
bool bvhBuffersUploaded = false;
bool enableBVH = true; // BVH toggle

// BVH Debug Renderer
Engine::BVHDebugRenderer bvhDebugRenderer;
bool showBVHDebug = false;

// BVH invalidation tracking
struct SceneState {
    size_t modelCount = 0;
    std::vector<glm::vec3> modelPositions;
    std::vector<glm::vec3> modelRotations;
    std::vector<glm::vec3> modelScales;
    
    bool hasChanged(const Engine::Scene& scene) const {
        if (modelCount != scene.models.size()) return true;
        
        for (size_t i = 0; i < scene.models.size() && i < modelPositions.size(); i++) {
            if (modelPositions[i] != scene.models[i].position ||  
                modelRotations[i] != scene.models[i].rotation ||
                modelScales[i] != scene.models[i].scale) {
                return true;
            }
        }
        return false;
    }
    
    void update(const Engine::Scene& scene) {
        modelCount = scene.models.size();
        modelPositions.clear();
        modelRotations.clear();
        modelScales.clear();
        
        for (const auto& model : scene.models) {
            modelPositions.push_back(model.position);
            modelRotations.push_back(model.rotation);
            modelScales.push_back(model.scale);
        }
    }
};
static SceneState lastSceneState;

// ---- Zero Plane Rendering ----
Engine::Shader* zeroPlaneShader = nullptr;
GLuint zeroPlaneVAO, zeroPlaneVBO, zeroPlaneEBO;

glm::vec4 divw(glm::vec4 vec) {
    if (vec.w != 0) {
        vec.x /= vec.w;
        vec.y /= vec.w;
        vec.z /= vec.w;
        vec.w = 1.0f;
    }
    return vec;
}


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

// ---- Triangle Buffer Setup for Raytracing ----
void setupTriangleBuffer() {
    if (triangleSSBO == 0) {
        glGenBuffers(1, &triangleSSBO);
    }
}

void updateTriangleBuffer(const std::vector<float>& data) {
    if (triangleSSBO == 0) {
        setupTriangleBuffer();
    }
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, data.size() * sizeof(float), data.data(), GL_DYNAMIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triangleSSBO);
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void cleanupTriangleBuffer() {
    if (triangleSSBO != 0) {
        glDeleteBuffers(1, &triangleSSBO);
        triangleSSBO = 0;
    }
}

// ---- BVH Buffer Setup ----
void setupBVHBuffers() {
    if (bvhNodeSSBO == 0) {
        glGenBuffers(1, &bvhNodeSSBO);
    }
    if (triangleIndexSSBO == 0) {
        glGenBuffers(1, &triangleIndexSSBO);
    }
}

void updateBVHBuffers() {
    if (!bvhBuilt) return;
    
    setupBVHBuffers();
    
    // Upload BVH nodes to SSBO binding 1
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, bvhNodeSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, gpuBVHNodes.size() * sizeof(Engine::GPUBVHNode), 
                 gpuBVHNodes.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bvhNodeSSBO);
    
    // Upload triangle indices to SSBO binding 2
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleIndexSSBO);
    glBufferData(GL_SHADER_STORAGE_BUFFER, gpuTriangleIndices.size() * sizeof(uint32_t), 
                 gpuTriangleIndices.data(), GL_STATIC_DRAW);
    glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, triangleIndexSSBO);
    
    glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
    
    std::cout << "BVH buffers updated: " << gpuBVHNodes.size() << " nodes, " 
              << gpuTriangleIndices.size() << " triangle indices" << std::endl;
}

void cleanupBVHBuffers() {
    if (bvhNodeSSBO != 0) {
        glDeleteBuffers(1, &bvhNodeSSBO);
        bvhNodeSSBO = 0;
    }
    if (triangleIndexSSBO != 0) {
        glDeleteBuffers(1, &triangleIndexSSBO);
        triangleIndexSSBO = 0;
    }
}

void buildBVH(const std::vector<Engine::BVHTriangle>& triangles) {
    if (triangles.empty()) {
        bvhBuilt = false;
        return;
    }
    
    std::cout << "Building BVH for " << triangles.size() << " triangles..." << std::endl;
    
    // Build BVH
    bvhBuilder.build(triangles);
    
    // Convert to GPU format
    const auto& nodes = bvhBuilder.getNodes();
    const auto& indices = bvhBuilder.getTriangleIndices();
    const auto& bvhTriangles = bvhBuilder.getTriangles();
    
    // Convert BVH nodes to GPU format
    gpuBVHNodes.clear();
    gpuBVHNodes.reserve(nodes.size());
    for (const auto& node : nodes) {
        Engine::GPUBVHNode gpuNode;
        gpuNode.minX = node.minBounds.x;
        gpuNode.minY = node.minBounds.y;
        gpuNode.minZ = node.minBounds.z;
        gpuNode.leftFirst = node.leftFirst;
        gpuNode.maxX = node.maxBounds.x;
        gpuNode.maxY = node.maxBounds.y;
        gpuNode.maxZ = node.maxBounds.z;
        gpuNode.triCount = node.triCount;
        gpuBVHNodes.push_back(gpuNode);
    }
    
    // Copy triangle indices
    gpuTriangleIndices = indices;
    
    // Convert triangles to GPU format (reordered according to BVH)
    gpuTriangles.clear();
    gpuTriangles.reserve(bvhTriangles.size());
    for (const auto& tri : bvhTriangles) {
        Engine::GPUTriangle gpuTri;
        gpuTri.v0[0] = tri.v0.x; gpuTri.v0[1] = tri.v0.y; gpuTri.v0[2] = tri.v0.z; gpuTri.v0[3] = 0.0f;
        gpuTri.v1[0] = tri.v1.x; gpuTri.v1[1] = tri.v1.y; gpuTri.v1[2] = tri.v1.z; gpuTri.v1[3] = 0.0f;
        gpuTri.v2[0] = tri.v2.x; gpuTri.v2[1] = tri.v2.y; gpuTri.v2[2] = tri.v2.z; gpuTri.v2[3] = 0.0f;
        gpuTri.normal[0] = tri.normal.x; gpuTri.normal[1] = tri.normal.y; gpuTri.normal[2] = tri.normal.z; gpuTri.normal[3] = 0.0f;
        gpuTri.color[0] = tri.color.x; gpuTri.color[1] = tri.color.y; gpuTri.color[2] = tri.color.z; gpuTri.color[3] = tri.emissiveness;
        gpuTri.shininess = tri.shininess;
        gpuTri.materialId = static_cast<uint32_t>(tri.materialId);
        gpuTri.padding[0] = 0.0f; gpuTri.padding[1] = 0.0f;
        gpuTriangles.push_back(gpuTri);
    }
    
    bvhBuilt = true;
    bvhBuffersUploaded = false;  // Mark that buffers need to be uploaded
    std::cout << "BVH built successfully" << std::endl;
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
        skyboxShader = Engine::loadShader("skybox/skyboxVertexShader.glsl",
            "skybox/skyboxFragmentShader.glsl");
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
        simpleDepthShader = Engine::loadShader("core/simpleDepthVertexShader.glsl", "core/simpleDepthFragmentShader.glsl");
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
    
    // Radar settings
    j["radar"]["enabled"] = preferences.radarEnabled;
    j["radar"]["posX"] = preferences.radarPos.x;
    j["radar"]["posY"] = preferences.radarPos.y;
    j["radar"]["scale"] = preferences.radarScale;
    j["radar"]["showScene"] = preferences.radarShowScene;

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

    // SpaceMouse settings
    j["spacemouse"]["enabled"] = preferences.spaceMouseEnabled;
    j["spacemouse"]["deadzone"] = preferences.spaceMouseDeadzone;
    j["spacemouse"]["translationSensitivity"] = preferences.spaceMouseTranslationSensitivity;
    j["spacemouse"]["rotationSensitivity"] = preferences.spaceMouseRotationSensitivity;
    j["spacemouse"]["anchorMode"] = static_cast<int>(preferences.spaceMouseAnchorMode);
    j["spacemouse"]["centerCursor"] = preferences.spaceMouseCenterCursor;

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

    // Startup scene settings
    j["startup"]["loadScene"] = preferences.loadStartupScene;
    j["startup"]["scenePath"] = preferences.startupScenePath;

    // Save lighting settings
    j["lighting"]["mode"] = static_cast<int>(preferences.lightingMode);
    j["lighting"]["enableShadows"] = preferences.enableShadows;

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
    currentScene.settings.autoConvergence = preferences.autoConvergence;
    currentScene.settings.convergenceDistanceFactor = preferences.convergenceDistanceFactor;
    currentScene.settings.nearPlane = preferences.nearPlane;
    currentScene.settings.farPlane = preferences.farPlane;
    camera.useNewMethod = preferences.useNewStereoMethod;
    camera.Zoom = preferences.fov;
    
    // Apply radar preferences
    currentScene.settings.radarEnabled = preferences.radarEnabled;
    currentScene.settings.radarPos = preferences.radarPos;
    currentScene.settings.radarScale = preferences.radarScale;
    currentScene.settings.radarShowScene = preferences.radarShowScene;
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
                spherePreset.showInnerSphere = true;
                spherePreset.cursorColor = glm::vec4(0.656f, 0.183f, 0.183f, 0.7f);
                spherePreset.innerSphereColor = glm::vec4(0.309f, 1.0f, 0.011f, 1.0f);
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

        // Radar settings
        if (j.contains("radar")) {
            preferences.radarEnabled = j["radar"].value("enabled", false);
            preferences.radarPos.x = j["radar"].value("posX", 0.8f);
            preferences.radarPos.y = j["radar"].value("posY", -0.8f);
            preferences.radarScale = j["radar"].value("scale", 0.2f);
            preferences.radarShowScene = j["radar"].value("showScene", true);
        }

        // Camera settings
        if (j.contains("camera")) {
            preferences.separation = j["camera"].value("separation", 0.5f);
            preferences.convergence = j["camera"].value("convergence", 2.6f);
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

        // SpaceMouse settings
        if (j.contains("spacemouse")) {
            preferences.spaceMouseEnabled = j["spacemouse"].value("enabled", true);
            preferences.spaceMouseDeadzone = j["spacemouse"].value("deadzone", 0.025f);
            preferences.spaceMouseTranslationSensitivity = j["spacemouse"].value("translationSensitivity", 1.0f);
            preferences.spaceMouseRotationSensitivity = j["spacemouse"].value("rotationSensitivity", 1.0f);
            
            // Handle backward compatibility with old useCursorAnchor setting
            if (j["spacemouse"].contains("useCursorAnchor")) {
                bool oldUseCursorAnchor = j["spacemouse"].value("useCursorAnchor", false);
                preferences.spaceMouseAnchorMode = oldUseCursorAnchor ? GUI::SPACEMOUSE_ANCHOR_CONTINUOUS : GUI::SPACEMOUSE_ANCHOR_DISABLED;
            } else {
                preferences.spaceMouseAnchorMode = static_cast<GUI::SpaceMouseAnchorMode>(j["spacemouse"].value("anchorMode", static_cast<int>(GUI::SPACEMOUSE_ANCHOR_DISABLED)));
            }
            
            preferences.spaceMouseCenterCursor = j["spacemouse"].value("centerCursor", false);
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

        // Startup scene settings
        if (j.contains("startup")) {
            preferences.loadStartupScene = j["startup"].value("loadScene", false);
            preferences.startupScenePath = j["startup"].value("scenePath", "");
        }

        // Cursor settings
        if (j.contains("cursor")) {
            preferences.currentPresetName = j["cursor"].value("currentPreset", "Sphere");
        }

        // Lighting settings
        if (j.contains("lighting")) {
            preferences.lightingMode = static_cast<GUI::LightingMode>(j["lighting"].value("mode", static_cast<int>(GUI::LIGHTING_SHADOW_MAPPING)));
            preferences.enableShadows = j["lighting"].value("enableShadows", true);
            
            // Update the global lighting mode
            currentLightingMode = preferences.lightingMode;
            enableShadows = preferences.enableShadows;
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

void initializeVCTSettings() {
    // Set default VCT settings
    vctSettings.indirectSpecularLight = true;
    vctSettings.indirectDiffuseLight = true;
    vctSettings.directLight = true;
    vctSettings.shadows = true;
    vctSettings.voxelSize = 1.0f / 64.0f;

    // Quality settings
    vctSettings.diffuseConeCount = 9;         // Default: high quality with 9 cones
    vctSettings.tracingMaxDistance = 1.41421356237;   // Default maximum distance
    vctSettings.shadowSampleCount = 10;       // Default shadow samples
    vctSettings.shadowStepMultiplier = 0.15f; // Default step multiplier

    // Load from preferences if available
    if (preferences.vctSettings.diffuseConeCount > 0) {
        vctSettings.diffuseConeCount = preferences.vctSettings.diffuseConeCount;
    }
    else {
        preferences.vctSettings.diffuseConeCount = vctSettings.diffuseConeCount;
    }

    if (preferences.vctSettings.tracingMaxDistance > 0) {
        vctSettings.tracingMaxDistance = preferences.vctSettings.tracingMaxDistance;
    }
    else {
        preferences.vctSettings.tracingMaxDistance = vctSettings.tracingMaxDistance;
    }

    if (preferences.vctSettings.shadowSampleCount > 0) {
        vctSettings.shadowSampleCount = preferences.vctSettings.shadowSampleCount;
    }
    else {
        preferences.vctSettings.shadowSampleCount = vctSettings.shadowSampleCount;
    }

    if (preferences.vctSettings.shadowStepMultiplier > 0) {
        vctSettings.shadowStepMultiplier = preferences.vctSettings.shadowStepMultiplier;
    }
    else {
        preferences.vctSettings.shadowStepMultiplier = vctSettings.shadowStepMultiplier;
    }
}

void InitializeDefaults() {
    // Set up default values
    preferences = GUI::ApplicationPreferences();
    
    // Set default radar settings
    currentScene.settings.radarEnabled = preferences.radarEnabled;
    currentScene.settings.radarPos = preferences.radarPos;
    currentScene.settings.radarScale = preferences.radarScale;
    currentScene.settings.radarShowScene = preferences.radarShowScene;

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
    currentScene.settings.autoConvergence = preferences.autoConvergence;
    currentScene.settings.convergenceDistanceFactor = preferences.convergenceDistanceFactor;
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
        spherePreset.showInnerSphere = true;
        spherePreset.cursorColor = glm::vec4(0.656f, 0.183f, 0.183f, 0.7f);
        spherePreset.innerSphereColor = glm::vec4(0.309f, 1.0f, 0.011f, 1.0f);
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
    // ---- Initialize Async Loading System ----
    OctreePointCloudManager::initializeAsyncSystem();
    
    // ---- Initialize GLFW ----
    if (!glfwInit()) {
        std::cout << "Failed to initialize GLFW" << std::endl;
        OctreePointCloudManager::shutdownAsyncSystem();
        return -1;
    }

    // ---- Set OpenGL Version and Profile ----
    glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
    glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 6);
    glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
    glfwWindowHint(GLFW_STEREO, GLFW_TRUE);  // Enable stereo hint
    glfwWindowHint(GLFW_SAMPLES, currentScene.settings.msaaSamples);

    // ---- Create GLFW Window ----
    GLFWwindow* window = glfwCreateWindow(windowWidth, windowHeight, "StereoVista", nullptr, nullptr);
    bool isStereoWindow = (window != nullptr);

    if (!isStereoWindow) {
        std::cout << "Failed to create stereo GLFW window, falling back to mono rendering." << std::endl;
        glfwWindowHint(GLFW_STEREO, GLFW_FALSE);
        window = glfwCreateWindow(windowWidth, windowHeight, "StereoVista (Monoviewer)", nullptr, nullptr);

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
        shader = Engine::loadShader("core/vertexShader.glsl", "core/fragmentShader.glsl");
    }
    catch (std::exception& e) {
        std::cout << e.what() << std::endl;
        glfwTerminate();
        return -1;
    }

    // ---- Initialize Zero Plane Shader ----
    try {
        zeroPlaneShader = Engine::loadShader("core/zeroPlaneVertexShader.glsl", "core/zeroPlaneFragmentShader.glsl");
    }
    catch (std::exception& e) {
        std::cout << "Warning: Failed to load zero plane shader: " << e.what() << std::endl;
        zeroPlaneShader = nullptr;
    }

    // ---- Initialize Radiance Shader ----
    try {
        radianceShader = Engine::loadShader("core/radianceVertexShader.glsl", "core/radianceFragmentShader.glsl");
    }
    catch (std::exception& e) {
        std::cout << "Warning: Failed to load radiance shader: " << e.what() << std::endl;
        radianceShader = nullptr;
    }


    // ---- Create Temp Default scene

    Engine::Model basePlatform = Engine::createCube(glm::vec3(0.3f, 0.3f, 0.3f), 1.0f, 0.0f);
    basePlatform.scale = glm::vec3(4.0f, 0.2f, 4.0f);
    basePlatform.name = "Base_Platform";
    basePlatform.position = glm::vec3(0.0f, -1.0f, 0.0f);
    currentScene.models.push_back(basePlatform);


    Engine::Model centralCube = Engine::createCube(glm::vec3(1.0f, 0.2f, 0.2f), 1.0f, 0.8f);
    centralCube.scale = glm::vec3(0.8f, 0.8f, 0.8f);
    centralCube.name = "Central_Light_Cube";
    centralCube.position = glm::vec3(0.0f, 0.0f, 0.0f);
    currentScene.models.push_back(centralCube);


    Engine::Model blueCube = Engine::createCube(glm::vec3(0.2f, 0.4f, 1.0f), 1.0f, 0.0f);
    blueCube.scale = glm::vec3(0.6f, 0.6f, 0.6f);
    blueCube.name = "Blue_Cube";
    blueCube.position = glm::vec3(-1.5f, 0.2f, 1.5f);
    currentScene.models.push_back(blueCube);


    Engine::Model greenCube = Engine::createCube(glm::vec3(0.2f, 1.0f, 0.3f), 1.0f, 0.0f);
    greenCube.scale = glm::vec3(0.5f, 1.2f, 0.5f);
    greenCube.name = "Green_Tower";
    greenCube.position = glm::vec3(1.2f, 0.6f, 1.0f);
    currentScene.models.push_back(greenCube);


    Engine::Model yellowCube = Engine::createCube(glm::vec3(1.0f, 1.0f, 0.3f), 1.0f, 0.4f);
    yellowCube.scale = glm::vec3(0.4f, 0.4f, 0.4f);
    yellowCube.name = "Yellow_Light";
    yellowCube.position = glm::vec3(-1.8f, 0.5f, -1.8f);
    currentScene.models.push_back(yellowCube);


    Engine::Model purpleCube = Engine::createCube(glm::vec3(0.8f, 0.2f, 0.9f), 1.0f, 0.0f);
    purpleCube.scale = glm::vec3(0.7f, 0.7f, 0.7f);
    purpleCube.name = "Purple_Cube";
    purpleCube.position = glm::vec3(1.5f, 0.35f, -1.5f);
    currentScene.models.push_back(purpleCube);


    Engine::Model orangeCube = Engine::createCube(glm::vec3(1.0f, 0.6f, 0.1f), 1.0f, 0.0f);
    orangeCube.scale = glm::vec3(0.3f, 0.3f, 0.3f);
    orangeCube.name = "Orange_Small";
    orangeCube.position = glm::vec3(0.5f, 1.5f, 0.5f);
    currentScene.models.push_back(orangeCube);


    Engine::Model cyanCube = Engine::createCube(glm::vec3(0.2f, 0.9f, 0.9f), 1.0f, 0.1f);
    cyanCube.scale = glm::vec3(0.4f, 0.8f, 0.4f);
    cyanCube.name = "Cyan_Pillar";
    cyanCube.position = glm::vec3(-2.5f, 0.4f, 0.0f);
    currentScene.models.push_back(cyanCube);


    Engine::Model whiteCube = Engine::createCube(glm::vec3(0.9f, 0.9f, 0.9f), 1.0f, 0.0f);
    whiteCube.scale = glm::vec3(0.5f, 0.5f, 0.5f);
    whiteCube.name = "White_Reflective";
    whiteCube.position = glm::vec3(2.5f, 0.25f, 0.5f);
    currentScene.models.push_back(whiteCube);

    for (int i = 0; i < 3; i++) {
        Engine::Model smallCube = Engine::createCube(glm::vec3(0.6f + i * 0.3f, 0.4f, 0.7f - i * 0.3f), 1.0f, 0.0f);
        smallCube.scale = glm::vec3(0.2f, 0.2f, 0.2f);
        smallCube.name = "Small_Detail_" + std::to_string(i);
        smallCube.position = glm::vec3(-0.5f + i * 0.3f, -0.7f, -0.8f + i * 0.6f);
        currentScene.models.push_back(smallCube);
    }
    
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

    // Initialize GUI scaling based on current window size (only if window is valid)
    if (Engine::Window::windowWidth > 0 && Engine::Window::windowHeight > 0) {
        UpdateGuiScale(Engine::Window::windowWidth, Engine::Window::windowHeight);
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
    initializeVCTSettings();

    // ---- Load Startup Scene ----
    if (preferences.loadStartupScene && !preferences.startupScenePath.empty()) {
        try {
            std::cout << "Loading startup scene: " << preferences.startupScenePath << std::endl;
            currentScene = Engine::loadScene(preferences.startupScenePath, camera);
            currentModelIndex = currentScene.models.empty() ? -1 : 0;
            updateSpaceMouseBounds();
            std::cout << "Startup scene loaded successfully" << std::endl;
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to load startup scene '" << preferences.startupScenePath << "': " << e.what() << std::endl;
        }
    }

    // ---- Initialize SpaceMouse Input ----
    // Use separate camera for SpaceMouse to prevent navlib override
    spaceMouseCamera = camera; // Initialize SpaceMouse camera with current camera state
    *spaceMouseCameraPtr = spaceMouseCamera;
    spaceMouseInput.SetCamera(spaceMouseCameraPtr);
    spaceMouseInitialized = spaceMouseInput.Initialize("StereoVista");
    if (spaceMouseInitialized) {
        std::cout << "SpaceMouse initialized successfully" << std::endl;
        
        // Apply SpaceMouse preferences
        spaceMouseInput.SetEnabled(preferences.spaceMouseEnabled);
        spaceMouseInput.SetDeadzone(preferences.spaceMouseDeadzone);
        spaceMouseInput.SetSensitivity(preferences.spaceMouseTranslationSensitivity, preferences.spaceMouseRotationSensitivity);
        
        // Initialize SpaceMouse bounds with current scene content
        updateSpaceMouseBounds();
        updateSpaceMouseCursorAnchor();
        spaceMouseInput.SetWindowSize(windowWidth, windowHeight);
        spaceMouseInput.SetFieldOfView(camera.Zoom);
        spaceMouseInput.SetPerspectiveMode(true);
        
        // Set up callbacks
        spaceMouseInput.OnNavigationStarted = []() {
            spaceMouseActive = true;
            // Sync current camera state to SpaceMouse camera when navigation starts
            spaceMouseCamera = camera;
            *spaceMouseCameraPtr = spaceMouseCamera;
            
            // Center cursor if enabled
            if (preferences.spaceMouseCenterCursor) {
                glfwSetCursorPos(Engine::Window::nativeWindow, windowWidth / 2.0, windowHeight / 2.0);
            }
            
            std::cout << "SpaceMouse navigation started" << std::endl;
        };
        spaceMouseInput.OnNavigationEnded = []() {
            spaceMouseActive = false;
            // Sync SpaceMouse camera state back to main camera when navigation ends
            camera = *spaceMouseCameraPtr;
            std::cout << "SpaceMouse navigation ended" << std::endl;
        };
    } else {
        std::cout << "Failed to initialize SpaceMouse - continuing without 3D navigation" << std::endl;
    }

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
        glfwPollEvents();

        // ---- Update SpaceMouse Input ----
        if (spaceMouseInitialized) {
            bool wasSpaceMouseActive = spaceMouseActive;
            spaceMouseInput.Update(deltaTime);
            
            // Handle camera synchronization for mode transitions
            if (spaceMouseActive) {
                // SpaceMouse is active - sync from SpaceMouse camera to main camera
                camera = *spaceMouseCameraPtr;
            } else if (wasSpaceMouseActive && !spaceMouseActive) {
                // Just transitioned from SpaceMouse to normal - sync is handled in OnNavigationEnded callback
                // This ensures smooth transition without camera jump
            } else if (!spaceMouseActive) {
                // Normal navigation mode - sync main camera changes to SpaceMouse camera for next activation
                spaceMouseCamera = camera;
                *spaceMouseCameraPtr = spaceMouseCamera;
            }
        }

        // --- Process Accumulated Mouse Input (Once Per Frame) ---
        // Check if mouse is captured and if there's any accumulated movement to process
        // Don't process mouse input when SpaceMouse is actively navigating
        if (isMouseCaptured && windowHasFocus && !ImGui::GetIO().WantCaptureMouse && !spaceMouseActive) {

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
        // Don't process keyboard movement when SpaceMouse is actively navigating
        if (!spaceMouseActive) {
            Input::handleKeyInput(camera, deltaTime); // Make sure handleKeyInput uses deltaTime
        }

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

        if (isStereoWindow || currentScene.settings.radarEnabled) {
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
        // ---- Update Scene State ----
        // Set wireframe mode before rendering
        glPolygonMode(GL_FRONT_AND_BACK, camera.wireframe ? GL_LINE : GL_FILL);

        // Reset cursor position calculation flag at start of frame
        cursorManager.resetFrameCalculationFlag();
        
        // Center cursor during SpaceMouse navigation if enabled
        if (spaceMouseInput.IsNavigating() && preferences.spaceMouseCenterCursor) {
            glfwSetCursorPos(window, windowWidth * 0.5, windowHeight * 0.5);
        }

        // ---- Shader Selection ----
        Engine::Shader* activeShader = shader;  // Default to standard shader
        if (currentLightingMode == GUI::LIGHTING_RADIANCE && radianceShader) {
            activeShader = radianceShader;
        }

        // ---- Rendering ----
        if (isStereoWindow) {
            // Render left eye to left buffer (cursor position will be calculated here first time)
            renderEye(GL_BACK_LEFT, leftProjection, leftView, activeShader, viewport, windowFlags, window);
            // Render right eye to right buffer (cursor position will use cached value)
            renderEye(GL_BACK_RIGHT, rightProjection, rightView, activeShader, viewport, windowFlags, window);
        }
        else {
            // Render mono view to default buffer (cursor position will be calculated here)
            renderEye(GL_BACK_LEFT, projection, view, activeShader, viewport, windowFlags, window);
        }
        
        // Update the cursor's captured position if available (after rendering)
        if (cursorManager.isCursorPositionValid()) {
            capturedCursorPos = cursorManager.getCursorPosition();
        }

        if (currentScene.settings.radarEnabled) {
            DrawRadar(isStereoWindow, camera, currentScene.settings.convergence,
                view, projection,
                leftView, leftProjection,
                rightView, rightProjection,
                shader, currentScene.settings.radarShowScene,
                currentScene.settings.radarScale, currentScene.settings.radarPos);
        }

        // ---- Swap Buffers ----
        glfwSwapBuffers(window);
    }

    // ---- Cleanup ----
    cleanup(shader);
    
    // ---- Shutdown Async Loading System ----
    OctreePointCloudManager::shutdownAsyncSystem();

    return 0;
}


// ---- Initialization and Cleanup -----
#pragma region Initialization and Cleanup
void cleanup(Engine::Shader* shader) {
    // Delete cursor manager resources
    cursorManager.cleanup();

    // Delete point cloud resources
    for (auto& pointCloud : currentScene.pointClouds) {
        glDeleteVertexArrays(1, &pointCloud.vao);
        glDeleteBuffers(1, &pointCloud.vbo);
    }

    // Delete triangle buffer resources
    cleanupTriangleBuffer();
    cleanupBVHBuffers();
    
    // Cleanup BVH debug renderer
    bvhDebugRenderer.cleanup();

    // Delete skybox resources
    glDeleteVertexArrays(1, &skyboxVAO);
    glDeleteBuffers(1, &skyboxVBO);
    glDeleteTextures(1, &cubemapTexture);
    delete skyboxShader;

    // Delete zero plane resources
    glDeleteVertexArrays(1, &zeroPlaneVAO);
    glDeleteBuffers(1, &zeroPlaneVBO);
    glDeleteBuffers(1, &zeroPlaneEBO);
    delete zeroPlaneShader;

    // Delete shadow mapping resources
    glDeleteFramebuffers(1, &depthMapFBO);
    glDeleteTextures(1, &depthMap);
    delete simpleDepthShader;
    delete radianceShader;

    // Delete shader
    delete shader;

    // Clean up SpaceMouse input
    spaceMouseInput.Shutdown();

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

        shader->setInt("vctSettings.diffuseConeCount", vctSettings.diffuseConeCount);
        shader->setFloat("vctSettings.tracingMaxDistance", vctSettings.tracingMaxDistance);
        shader->setInt("vctSettings.shadowSampleCount", vctSettings.shadowSampleCount);
        shader->setFloat("vctSettings.shadowStepMultiplier", vctSettings.shadowStepMultiplier);

        // Bind voxel 3D texture - using texture unit 5
        glActiveTexture(GL_TEXTURE5);
        glBindTexture(GL_TEXTURE_3D, voxelizer->getVoxelTexture());
        shader->setInt("voxelGrid", 5);

        // Set default material properties for voxel cone tracing
        shader->setFloat("material.diffuseReflectivity", 0.8f);
        shader->setFloat("material.specularReflectivity", 0.0f);
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
    // Radiance rendering specific setup
    else if (currentLightingMode == GUI::LIGHTING_RADIANCE) {
        // Set raytracing parameters from GUI settings
        shader->setBool("enableRaytracing", radianceSettings.enableRaytracing);
        shader->setInt("maxBounces", radianceSettings.maxBounces);
        shader->setInt("samplesPerPixel", radianceSettings.samplesPerPixel);
        shader->setFloat("rayMaxDistance", radianceSettings.rayMaxDistance);
        shader->setBool("enableIndirectLighting", radianceSettings.enableIndirectLighting);
        shader->setBool("enableEmissiveLighting", radianceSettings.enableEmissiveLighting);
        shader->setFloat("indirectIntensity", radianceSettings.indirectIntensity);
        shader->setFloat("skyIntensity", radianceSettings.skyIntensity);
        shader->setFloat("emissiveIntensity", radianceSettings.emissiveIntensity);
        shader->setFloat("materialRoughness", radianceSettings.materialRoughness);
        
        // No camera matrices needed - using rasterized fragment positions
        
        // Set actual scene lights (same as other modes)
        updatePointLights();
        
        // Set point light uniforms
        for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
            std::string lightName = "pointLights[" + std::to_string(i) + "]";
            shader->setVec3(lightName + ".position", pointLights[i].position);
            shader->setVec3(lightName + ".color", pointLights[i].color);
            shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
        }
        shader->setInt("numPointLights", std::min((int)pointLights.size(), MAX_LIGHTS));
        
        // Set sun properties
        shader->setBool("sun.enabled", sun.enabled);
        shader->setVec3("sun.direction", sun.direction);
        shader->setVec3("sun.color", sun.color);
        shader->setFloat("sun.intensity", sun.intensity);
        
        // Extract triangle data from scene models and pack into SSBO
        triangleData.clear();
        std::vector<Engine::BVHTriangle> bvhTriangles;
        int triangleCount = 0;
        
        for (const auto& model : currentScene.models) {
            for (const auto& mesh : model.getMeshes()) {
                // Calculate model matrix for this model
                glm::mat4 modelMatrix = glm::mat4(1.0f);
                modelMatrix = glm::translate(modelMatrix, model.position);
                modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x), glm::vec3(1, 0, 0));
                modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y), glm::vec3(0, 1, 0));
                modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z), glm::vec3(0, 0, 1));
                modelMatrix = glm::scale(modelMatrix, model.scale);
                
                // Get mesh vertices and indices directly (they are public members)
                const auto& vertices = mesh.vertices;
                const auto& indices = mesh.indices;
                
                // Extract ALL triangles (no more skipping for performance)
                for (size_t i = 0; i < indices.size(); i += 3) {
                    if (i + 2 < indices.size()) {
                        // Get triangle vertices
                        glm::vec3 v0 = glm::vec3(modelMatrix * glm::vec4(vertices[indices[i]].position, 1.0f));
                        glm::vec3 v1 = glm::vec3(modelMatrix * glm::vec4(vertices[indices[i+1]].position, 1.0f));
                        glm::vec3 v2 = glm::vec3(modelMatrix * glm::vec4(vertices[indices[i+2]].position, 1.0f));
                        
                        // Calculate triangle normal
                        glm::vec3 normal = normalize(cross(v1 - v0, v2 - v0));
                        
                        // Pack triangle data into buffer (matches shader Triangle struct layout)
                        // struct Triangle { vec3 v0, v1, v2; vec3 normal; vec3 color; float emissiveness; float shininess; int materialId; }
                        // std430 layout: vec3 takes 3 floats, then next vec3 starts at next 4-float boundary
                        triangleData.insert(triangleData.end(), {v0.x, v0.y, v0.z}); // vec3 v0
                        triangleData.push_back(0.0f); // padding for vec3 alignment
                        triangleData.insert(triangleData.end(), {v1.x, v1.y, v1.z}); // vec3 v1  
                        triangleData.push_back(0.0f); // padding for vec3 alignment
                        triangleData.insert(triangleData.end(), {v2.x, v2.y, v2.z}); // vec3 v2
                        triangleData.push_back(0.0f); // padding for vec3 alignment
                        triangleData.insert(triangleData.end(), {normal.x, normal.y, normal.z}); // vec3 normal
                        triangleData.push_back(0.0f); // padding for vec3 alignment
                        triangleData.insert(triangleData.end(), {model.color.x, model.color.y, model.color.z}); // vec3 color
                        triangleData.push_back(model.emissive); // float emissiveness (no padding needed, fills vec3 slot)
                        triangleData.push_back(model.shininess); // float shininess
                        // For int materialId, we need to use reinterpret_cast to pack as float bits
                        int materialId = triangleCount;
                        triangleData.push_back(*reinterpret_cast<float*>(&materialId)); // int materialId packed as float
                        triangleData.push_back(0.0f); // padding for next struct alignment
                        triangleData.push_back(0.0f); // padding for next struct alignment
                        
                        // Create BVH triangle
                        Engine::BVHTriangle bvhTri(v0, v1, v2, normal, model.color, 
                                                  model.emissive, model.shininess, materialId);
                        bvhTriangles.push_back(bvhTri);
                        
                        triangleCount++;
                    }
                }
            }
        }
        
        // Update the SSBO with triangle data
        if (!triangleData.empty()) {
            updateTriangleBuffer(triangleData);
        }
        
        // Build BVH only if scene has changed and we have triangles and BVH is enabled
        bool sceneChanged = lastSceneState.hasChanged(currentScene);
        if (!bvhTriangles.empty() && enableBVH && (sceneChanged || !bvhBuilt)) {
            std::cout << "Scene changed, rebuilding BVH..." << std::endl;
            buildBVH(bvhTriangles);
            updateBVHBuffers();
            bvhBuffersUploaded = true;
            
            // Update debug renderer if debug is enabled
            if (showBVHDebug) {
                // Get max depth from GUI settings
                int maxDepth = preferences.radianceSettings.bvhDebugMaxDepth;
                bvhDebugRenderer.updateFromBVH(bvhBuilder.getNodes(), maxDepth);
                bvhDebugRenderer.setEnabled(true); // Enable rendering
            }
            
            lastSceneState.update(currentScene);
        } else if (bvhBuilt && enableBVH && !bvhBuffersUploaded) {
            // BVH built but buffers not uploaded yet (e.g., BVH was just enabled)
            updateBVHBuffers();
            bvhBuffersUploaded = true;
        }
        
        // Update debug renderer if user toggled debug and BVH is already built
        static bool lastShowBVHDebug = false;
        if (showBVHDebug != lastShowBVHDebug) {
            if (showBVHDebug && bvhBuilt) {
                std::cout << "Enabling BVH debug visualization..." << std::endl;
                int maxDepth = preferences.radianceSettings.bvhDebugMaxDepth;
                bvhDebugRenderer.updateFromBVH(bvhBuilder.getNodes(), maxDepth);
                bvhDebugRenderer.setEnabled(true); // Enable rendering
                
                // Set render mode from GUI
                Engine::BVHDebugRenderer::RenderMode mode = static_cast<Engine::BVHDebugRenderer::RenderMode>(preferences.radianceSettings.bvhDebugRenderMode);
                bvhDebugRenderer.setRenderMode(mode);
            } else {
                bvhDebugRenderer.setEnabled(false); // Disable rendering
            }
            lastShowBVHDebug = showBVHDebug;
        }
        
        // Update debug renderer settings if they changed
        static int lastMaxDepth = 3;
        static int lastRenderMode = 1;
        if (showBVHDebug && bvhBuilt && 
            (preferences.radianceSettings.bvhDebugMaxDepth != lastMaxDepth)) {
            // Max depth changed - rebuild debug geometry
            bvhDebugRenderer.updateFromBVH(bvhBuilder.getNodes(), preferences.radianceSettings.bvhDebugMaxDepth);
            lastMaxDepth = preferences.radianceSettings.bvhDebugMaxDepth;
        }
        if (showBVHDebug && 
            (preferences.radianceSettings.bvhDebugRenderMode != lastRenderMode)) {
            // Render mode changed - update renderer
            Engine::BVHDebugRenderer::RenderMode mode = static_cast<Engine::BVHDebugRenderer::RenderMode>(preferences.radianceSettings.bvhDebugRenderMode);
            bvhDebugRenderer.setRenderMode(mode);
            lastRenderMode = preferences.radianceSettings.bvhDebugRenderMode;
        }
        
        shader->setInt("numTriangles", triangleCount);
        shader->setInt("numBVHNodes", static_cast<int>(gpuBVHNodes.size()));
        shader->setBool("enableBVH", enableBVH && bvhBuilt);
        
        // Disable ground plane for pure raytracing (was causing unwanted lighting)
        shader->setBool("hasGroundPlane", false);
    }

    // Render scene
    renderModels(shader);
    renderPointClouds(shader);
    
    // Render BVH debug visualization (after main scene rendering)
    if (showBVHDebug && bvhBuilt) {
        // BVH debug lines are now rendering
        bvhDebugRenderer.render(view, projection);
    }
    
    // Calculate distance to nearest object AFTER scene rendering but BEFORE zero plane
    // This ensures zero plane doesn't interfere with distance calculation
    // Only calculate once per frame (left eye for stereo, or single eye for mono)
    static bool distanceCalculatedThisFrame = false;
    static double lastFrameTime = 0.0;
    double currentFrameTime = glfwGetTime();
    
    if (currentFrameTime != lastFrameTime) {
        distanceCalculatedThisFrame = false;
        lastFrameTime = currentFrameTime;
    }
    
    if (!distanceCalculatedThisFrame) {
        float distanceToNearestObject = camera.getDistanceToNearestObject(camera, projection, view, currentScene.settings.farPlane, windowWidth, windowHeight);
        camera.UpdateDistanceToObject(distanceToNearestObject);
        float largestDimension = calculateLargestModelDimension();
        camera.AdjustMovementSpeed(distanceToNearestObject, largestDimension, currentScene.settings.farPlane);
        
        // Update convergence automatically if enabled
        if (currentScene.settings.autoConvergence) {
            float cameraDistance = camera.distanceToNearestObject;
            if (cameraDistance < currentScene.settings.farPlane * 0.95f && camera.distanceUpdated) {
                float autoConvergenceValue = cameraDistance * currentScene.settings.convergenceDistanceFactor;
                float minSafeConvergence = cameraDistance + 0.5f;
                autoConvergenceValue = glm::max(autoConvergenceValue, minSafeConvergence);
                autoConvergenceValue = glm::clamp(autoConvergenceValue, 0.5f, 40.0f);
                currentScene.settings.convergence = autoConvergenceValue;
                preferences.convergence = autoConvergenceValue;
            }
            // If looking at empty space, keep previous convergence value
        }
        distanceCalculatedThisFrame = true;
    }
    
    // Render zero plane if enabled (AFTER distance calculation)
    if (currentScene.settings.showZeroPlane) {
        renderZeroPlane(shader, projection, view, currentScene.settings.convergence);
    }

    // Calculate cursor position (only once per frame for stereo consistency)
    // For stereo: first call (left eye) calculates, second call (right eye) uses cached value
    cursorManager.updateCursorPosition(window, projection, view, shader, false);
    
    // Update SpaceMouse cursor anchor when cursor position changes
    updateSpaceMouseCursorAnchor();

    // Update shader uniforms for cursors (use active shader, not original shader)
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

        // Always use octree-based rendering (legacy system removed)
        if (pointCloud.octreeRoot) {
            // Update LOD system for current camera position
            glm::vec3 cameraPosition = camera.Position;
            OctreePointCloudManager::updateLOD(pointCloud, cameraPosition);
            
            // Bind VAO for octree rendering (octree nodes use their own VBOs but need the VAO for attributes)
            glBindVertexArray(pointCloud.vao);
            
            // Render visible octree nodes
            OctreePointCloudManager::renderVisible(pointCloud, cameraPosition);
            
            glBindVertexArray(0);
        }

        // Visualize octree structure if enabled
        if (pointCloud.visualizeOctree && pointCloud.octreeRoot) {
            // Generate octree visualization if not already done
            if (pointCloud.chunkOutlineVertices.empty()) {
                OctreePointCloudManager::generateOctreeVisualization(pointCloud, pointCloud.visualizeDepth);
            }
            
            shader->setBool("isChunkOutline", true);
            shader->setVec3("outlineColor", glm::vec3(0.0f, 1.0f, 0.0f));

            glBindVertexArray(pointCloud.chunkOutlineVAO);
            glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(pointCloud.chunkOutlineVertices.size()));
            glBindVertexArray(0);

            shader->setBool("isChunkOutline", false);
        }
    }

    shader->setBool("isPointCloud", false);
}

void renderZeroPlane(Engine::Shader* shader, const glm::mat4& projection, const glm::mat4& view, float convergence) {
    // Skip if zero plane shader is not loaded
    if (!zeroPlaneShader) return;
    
    // Create quad geometry for zero plane (only once)
    static bool initialized = false;
    if (!initialized) {
        // Quad vertices with texture coordinates
        float vertices[] = {
            // positions        // texture coords
            -1.0f,  1.0f, 0.0f,  0.0f, 1.0f,
            -1.0f, -1.0f, 0.0f,  0.0f, 0.0f,
             1.0f, -1.0f, 0.0f,  1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,  1.0f, 1.0f
        };
        
        unsigned int indices[] = {
            0, 1, 2,
            0, 2, 3
        };
        
        glGenVertexArrays(1, &zeroPlaneVAO);
        glGenBuffers(1, &zeroPlaneVBO);
        glGenBuffers(1, &zeroPlaneEBO);
        
        glBindVertexArray(zeroPlaneVAO);
        
        glBindBuffer(GL_ARRAY_BUFFER, zeroPlaneVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, zeroPlaneEBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
        
        // Position attribute
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);
        
        // Texture coordinate attribute
        glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);
        
        glBindVertexArray(0);
        initialized = true;
    }
    
    // Enable blending for transparency
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    
    // Use zero plane shader
    zeroPlaneShader->use();
    
    // Position the plane at convergence distance in world space
    glm::vec3 planePosition = camera.Position + camera.Front * convergence;
    
    // Create billboard matrix that always faces the camera
    // The plane's normal should point towards the camera
    glm::vec3 forward = -camera.Front;  // Plane normal points toward camera
    glm::vec3 right = camera.Right;     // Use camera's right vector
    glm::vec3 up = camera.Up;           // Use camera's up vector
    
    // Construct billboard transformation matrix
    glm::mat4 billboardMatrix = glm::mat4(1.0f);
    billboardMatrix[0] = glm::vec4(right, 0.0f);
    billboardMatrix[1] = glm::vec4(up, 0.0f);
    billboardMatrix[2] = glm::vec4(forward, 0.0f);
    billboardMatrix[3] = glm::vec4(planePosition, 1.0f);
    
    // Scale the plane to make it large enough to be visible
    glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 10.0f, 1.0f));
    glm::mat4 model = billboardMatrix * scaleMatrix;
    
    // Set uniforms
    zeroPlaneShader->setMat4("model", model);
    zeroPlaneShader->setMat4("view", view);
    zeroPlaneShader->setMat4("projection", projection);
    zeroPlaneShader->setVec4("planeColor", glm::vec4(0.0f, 1.0f, 0.0f, 0.5f)); // Green with transparency
    zeroPlaneShader->setFloat("convergence", convergence);
    zeroPlaneShader->setVec3("cameraPos", camera.Position);
    
    // Render the quad
    glBindVertexArray(zeroPlaneVAO);
    glDrawElements(GL_TRIANGLES, 6, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
    
    // Disable blending
    glDisable(GL_BLEND);
}

void DrawRadar(bool isStereoWindow, Camera camera, GLfloat focaldist, 
    glm::mat4 view, glm::mat4 projection, 
    glm::mat4 leftview, glm::mat4 leftprojection,
    glm::mat4 rightview, glm::mat4 rightprojection,
    Engine::Shader* shader,
    bool renderScene, float radarScale, glm::vec2 position)
{
    // construct view and projection matrices
    glm::mat4 p(1.0f);
    glm::mat4 v(1.0f);
    v = glm::translate(v, glm::vec3(position, 0));
    v = glm::rotate(v, -90.0f, glm::vec3(1.0f, 0, 0));
    v = glm::rotate(v, 180.0f, glm::vec3(1.0f, 0.0f, 0.0f));
    v = glm::scale(v, glm::vec3(radarScale));
    v = v * view;

    // construct frustum in NDC and inv-project it 
    glm::vec2 frust_ndc[6]{};
    glm::vec4 frust_world[12]{}; // idx 0-5 left, idx 6-11 right

    glm::mat4 defaultView = glm::lookAt(
        glm::vec3(0.0f, 0.0f, 0.0f),   // eye at origin
        glm::vec3(0.0f, 0.0f, 1.0f),   // looking towards +Z
        glm::vec3(0.0f, 1.0f, 0.0f));  // up is +Y

    glm::vec4 fd_ndc = projection * defaultView * glm::vec4(0, 0, focaldist, 1.0f);
    fd_ndc = divw(fd_ndc);

    // define the line endpoints in ndc
    frust_ndc[0].x = -1.0; // near left
    frust_ndc[0].y = -1.0;
    frust_ndc[1].x = 1.0; // near right
    frust_ndc[1].y = -1.0;
    frust_ndc[2].x = -1.0; // far left
    frust_ndc[2].y = 1.0;
    frust_ndc[3].x = 1.0; // far right
    frust_ndc[3].y = 1.0;
    frust_ndc[4].x = -1.0; // focaldist left
    frust_ndc[4].y = fd_ndc.z;
    frust_ndc[5].x = 1.0; // focaldist right
    frust_ndc[5].y = fd_ndc.z;

    // transform the points back to world space
    glm::mat4 inv_left = glm::inverse(leftprojection * leftview);
    glm::mat4 inv_right = glm::inverse(rightprojection * rightview);
    for (int i = 0; i < 6; i++) {
        glm::vec4 p(frust_ndc[i].x, 0, frust_ndc[i].y, 1.0f);
        frust_world[i] = inv_left * p;
        frust_world[i] = divw(frust_world[i]);
        frust_world[i + 6] = inv_right * p;
        frust_world[i + 6] = divw(frust_world[i + 6]);
    }

    // construct frustum lines 
    const int numPoints = 20;
    GLfloat buf[numPoints * 3];
    int i = 0;
    buf[i++] = frust_world[0].x; // near left --> far left
    buf[i++] = frust_world[0].y;
    buf[i++] = frust_world[0].z;
    buf[i++] = frust_world[2].x;
    buf[i++] = frust_world[2].y;
    buf[i++] = frust_world[2].z;
    buf[i++] = frust_world[1].x; // near right --> far right
    buf[i++] = frust_world[1].y;
    buf[i++] = frust_world[1].z;
    buf[i++] = frust_world[3].x;
    buf[i++] = frust_world[3].y;
    buf[i++] = frust_world[3].z;
    buf[i++] = frust_world[0].x; // near left --> near right
    buf[i++] = frust_world[0].y;
    buf[i++] = frust_world[0].z;
    buf[i++] = frust_world[1].x;
    buf[i++] = frust_world[1].y;
    buf[i++] = frust_world[1].z;
    buf[i++] = frust_world[2].x; // far left --> far right
    buf[i++] = frust_world[2].y;
    buf[i++] = frust_world[2].z;
    buf[i++] = frust_world[3].x;
    buf[i++] = frust_world[3].y;
    buf[i++] = frust_world[3].z;
    buf[i++] = frust_world[4].x; // focal left --> focal right
    buf[i++] = frust_world[4].y;
    buf[i++] = frust_world[4].z;
    buf[i++] = frust_world[5].x;
    buf[i++] = frust_world[5].y;
    buf[i++] = frust_world[5].z;

    buf[i++] = frust_world[6].x; // near left --> far left
    buf[i++] = frust_world[6].y;
    buf[i++] = frust_world[6].z;
    buf[i++] = frust_world[8].x;
    buf[i++] = frust_world[8].y;
    buf[i++] = frust_world[8].z;
    buf[i++] = frust_world[7].x; // near right --> far right
    buf[i++] = frust_world[7].y;
    buf[i++] = frust_world[7].z;
    buf[i++] = frust_world[9].x;
    buf[i++] = frust_world[9].y;
    buf[i++] = frust_world[9].z;
    buf[i++] = frust_world[6].x; // near left --> near right
    buf[i++] = frust_world[6].y;
    buf[i++] = frust_world[6].z;
    buf[i++] = frust_world[7].x;
    buf[i++] = frust_world[7].y;
    buf[i++] = frust_world[7].z;
    buf[i++] = frust_world[8].x; // far left --> far right
    buf[i++] = frust_world[8].y;
    buf[i++] = frust_world[8].z;
    buf[i++] = frust_world[9].x;
    buf[i++] = frust_world[9].y;
    buf[i++] = frust_world[9].z;
    buf[i++] = frust_world[10].x; // focal left --> focal right
    buf[i++] = frust_world[10].y;
    buf[i++] = frust_world[10].z;
    buf[i++] = frust_world[11].x;
    buf[i++] = frust_world[11].y;
    buf[i++] = frust_world[11].z;

    // render the lines
    glUseProgram(0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    
    glViewport(0, 0, windowWidth, windowHeight);
    glDisable(GL_DEPTH_TEST);

    GLuint vao;
    GLuint vbo;
    glGenVertexArrays(1, &vao);
    glGenBuffers(1, &vbo);

    glBindVertexArray(vao);
    glBindBuffer(GL_ARRAY_BUFFER, vbo);
    glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 3 * numPoints, buf, GL_STATIC_DRAW);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat), (void*)0);

    glEnableVertexAttribArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glBindVertexArray(0);

    shader->use();
    shader->setMat4("projection", p);
    shader->setMat4("view", v);
    shader->setMat4("model", glm::mat4(1));

    shader->setBool("isChunkOutline", true);
    shader->setBool("isPointCloud", false);

    glLineWidth(1.0f);

    glm::vec4 leftColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 rightColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);

    glBindVertexArray(vao);
    if (isStereoWindow) {
        glDrawBuffer(GL_BACK_LEFT);
        shader->setVec4("outlineColor", leftColor);
        glDrawArrays(GL_LINES, 0, 10);
        shader->setVec4("outlineColor", rightColor);
        glDrawArrays(GL_LINES, 10, 10);

        glDrawBuffer(GL_BACK_RIGHT);
        shader->setVec4("outlineColor", leftColor);
        glDrawArrays(GL_LINES, 0, 10);
        shader->setVec4("outlineColor", rightColor);
        glDrawArrays(GL_LINES, 10, 10);
    }
    else {
        glDrawBuffer(GL_BACK);
        shader->setVec4("outlineColor", leftColor);
        glDrawArrays(GL_LINES, 0, 10);
        shader->setVec4("outlineColor", rightColor);
        glDrawArrays(GL_LINES, 10, 10);
    }
    glBindVertexArray(0);

    shader->setBool("isChunkOutline", false);

    // render the model seen from above
    if (renderScene) {
        shader->use();
        shader->setMat4("projection", p);
        shader->setMat4("view", v);
        shader->setMat4("model", glm::mat4(1));

        if (isStereoWindow) {
            glDrawBuffer(GL_BACK_LEFT);
            renderModels(shader);
            glDrawBuffer(GL_BACK_RIGHT);
            renderModels(shader);
        }
        else {
            glDrawBuffer(GL_BACK);
            renderModels(shader);
        }
    }

    glEnable(GL_DEPTH_TEST);

    // Cleanup temporary VAO/VBO
    glDeleteVertexArrays(1, &vao);
    glDeleteBuffers(1, &vbo);
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

void updateSpaceMouseBounds() {
    // Calculate combined bounding box for models and point clouds
    // This function is called when:
    // - Models or point clouds are loaded/deleted
    // - Model/point cloud transforms (position, scale, rotation) change
    // - Scenes are loaded
    // This ensures SpaceMouse navigation bounds stay accurate without per-frame updates
    glm::vec3 modelMin(FLT_MAX), modelMax(-FLT_MAX);
    
    // Include models in bounding box calculation
    for (const auto& model : currentScene.models) {
        for (const auto& mesh : model.getMeshes()) {
            for (const auto& vertex : mesh.vertices) {
                glm::vec3 worldPos = model.position + (glm::vec3(vertex.position) * model.scale);
                modelMin = glm::min(modelMin, worldPos);
                modelMax = glm::max(modelMax, worldPos);
            }
        }
    }
    
    // Include point clouds in bounding box calculation
    for (const auto& pointCloud : currentScene.pointClouds) {
        if (pointCloud.octreeRoot) {
            // Use octree bounds if available
            glm::vec3 pcMin = pointCloud.position + (pointCloud.octreeBoundsMin * pointCloud.scale);
            glm::vec3 pcMax = pointCloud.position + (pointCloud.octreeBoundsMax * pointCloud.scale);
            modelMin = glm::min(modelMin, pcMin);
            modelMax = glm::max(modelMax, pcMax);
        } else if (!pointCloud.points.empty()) {
            // Fall back to calculating bounds from points
            for (const auto& point : pointCloud.points) {
                glm::vec3 worldPos = pointCloud.position + (point.position * pointCloud.scale);
                modelMin = glm::min(modelMin, worldPos);
                modelMax = glm::max(modelMax, worldPos);
            }
        }
    }
    
    // Fallback if no content found
    if (modelMin.x == FLT_MAX) {
        modelMin = glm::vec3(-5.0f);
        modelMax = glm::vec3(5.0f);
    }
    
    // Update SpaceMouse with new bounds
    spaceMouseInput.SetModelExtents(modelMin, modelMax);
}

void updateSpaceMouseCursorAnchor() {
    // Update SpaceMouse cursor anchor based on mode
    static glm::vec3 lastCursorPosition = glm::vec3(FLT_MAX);
    static GUI::SpaceMouseAnchorMode lastAnchorMode = GUI::SPACEMOUSE_ANCHOR_DISABLED;
    
    bool settingChanged = (lastAnchorMode != preferences.spaceMouseAnchorMode);
    lastAnchorMode = preferences.spaceMouseAnchorMode;
    
    // Update anchor mode
    spaceMouseInput.SetAnchorMode(preferences.spaceMouseAnchorMode);
    spaceMouseInput.SetCenterCursor(preferences.spaceMouseCenterCursor);
    
    if (cursorManager.isCursorPositionValid()) {
        glm::vec3 currentCursorPosition = cursorManager.getCursorPosition();
        
        // For CONTINUOUS mode, always update cursor position
        // For ON_START mode, only update when not navigating
        // For DISABLED mode, don't update cursor anchor
        bool shouldUpdate = false;
        
        switch (preferences.spaceMouseAnchorMode) {
            case GUI::SPACEMOUSE_ANCHOR_CONTINUOUS:
                shouldUpdate = (glm::distance(lastCursorPosition, currentCursorPosition) > 0.001f) || settingChanged;
                break;
            case GUI::SPACEMOUSE_ANCHOR_ON_START:
                shouldUpdate = !spaceMouseInput.IsNavigating() && 
                              ((glm::distance(lastCursorPosition, currentCursorPosition) > 0.001f) || settingChanged);
                break;
            case GUI::SPACEMOUSE_ANCHOR_DISABLED:
            default:
                shouldUpdate = settingChanged;
                break;
        }
        
        if (shouldUpdate) {
            lastCursorPosition = currentCursorPosition;
            spaceMouseInput.SetCursorAnchor(currentCursorPosition, preferences.spaceMouseAnchorMode);
            
            // Force NavLib to refresh the pivot position
            if (preferences.spaceMouseAnchorMode != GUI::SPACEMOUSE_ANCHOR_DISABLED) {
                spaceMouseInput.RefreshPivotPosition();
            }
        }
    } else {
        // Always update the setting state even if cursor is not valid
        spaceMouseInput.SetCursorAnchor(glm::vec3(0.0f), preferences.spaceMouseAnchorMode);
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
    
    // Update GUI scaling based on new window dimensions
    UpdateGuiScale(width, height);
}

void scroll_callback(GLFWwindow* window, double xoffset, double yoffset) {
    if (!ImGui::GetIO().WantCaptureMouse && !spaceMouseActive) {
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
            // Check if Ctrl or Alt is held down
            bool ctrlPressed = (mods & GLFW_MOD_CONTROL);
            bool altPressed = (mods & GLFW_MOD_ALT);
            if (ctrlPressed || altPressed) {
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
                    // Handle Alt+drag duplication
                    if (altPressed) {
                        // Duplicate the model at the same position
                        Engine::Model duplicatedModel = currentScene.models[closestModelIndex];
                        duplicatedModel.name += "_Copy";
                        currentScene.models.push_back(duplicatedModel);
                        
                        // Select the new duplicated model for moving
                        currentSelectedIndex = currentScene.models.size() - 1;
                        std::cout << "Model duplicated: " << duplicatedModel.name << std::endl;
                    } else {
                        // Normal Ctrl+drag - select existing model
                        currentSelectedIndex = closestModelIndex;
                    }
                    
                    currentSelectedType = SelectedType::Model;
                    currentSelectedMeshIndex = -1;

                    if (!isMouseCaptured) {
                        isMouseCaptured = true;
                        firstMouse = true; // Reset flag for delta calculation
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                    }

                    if (ctrlPressed || altPressed) {
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
            // Check if we were moving a model before processing the release
            bool wasMovingModel = isMovingModel;

            if (isMouseCaptured) {
                // Disable mouse capture when orbiting ends
                isMouseCaptured = false;
                firstMouse = true; // Reset first mouse flag for next time

                // Only set input mode to normal if we were moving a model
                if (wasMovingModel) {
                    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                }
            }

            // Only reset cursor position if we were NOT moving a model
            // AND if orbit around cursor is false
            if (!wasMovingModel && camera.orbitAroundCursor == false) {
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
    // Note: This handles special keys like GUI toggle, lighting mode changes
    // Movement keys (WASD) are handled in Input::handleKeyInput which respects SpaceMouse input blocking
    if (key == GLFW_KEY_G && action == GLFW_PRESS)
    {
        showGui = !showGui;
        std::cout << "GUI visibility toggled. showGui = " << (showGui ? "true" : "false") << std::endl;
    }

    if (key == GLFW_KEY_L && action == GLFW_PRESS) {
        // Cycle through lighting modes: Shadow Mapping -> Voxel Cone Tracing -> Radiance -> Shadow Mapping
        if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
            currentLightingMode = GUI::LIGHTING_VOXEL_CONE_TRACING;
        }
        else if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
            currentLightingMode = GUI::LIGHTING_RADIANCE;
        }
        else {
            currentLightingMode = GUI::LIGHTING_SHADOW_MAPPING;
        }


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

            // Don't reset cursor position when releasing Ctrl key
            // This allows the cursor to stay where it is when exiting selection mode
            if (isMouseCaptured && isMovingModel) {
                // Get cursor position before releasing capture
                double mouseX, mouseY;
                glfwGetCursorPos(window, &mouseX, &mouseY);

                // When releasing Ctrl while moving a model, keep the cursor where it is
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);

                // No need to reset cursor to center
                isMovingModel = false;
            }
        }
    }

    // Handle Delete key
    if (key == GLFW_KEY_DELETE && action == GLFW_PRESS)
    {
        if (currentSelectedType == SelectedType::Model && currentSelectedIndex >= 0 && currentSelectedIndex < currentScene.models.size()) {
            std::cout << "Deleting selected model: " << currentScene.models[currentSelectedIndex].name << std::endl;
            
            // Remove the selected model from the scene
            currentScene.models.erase(currentScene.models.begin() + currentSelectedIndex);
            
            // Adjust selection indices after deletion
            if (currentScene.models.empty()) {
                currentSelectedIndex = -1; // No models left
                currentSelectedType = SelectedType::None;
            } else if (currentSelectedIndex >= currentScene.models.size()) {
                currentSelectedIndex = currentScene.models.size() - 1; // Select last model if we deleted the last one
            }
            // If currentSelectedIndex < currentScene.models.size(), it stays the same (next model takes the same index)
            
            // Also update currentModelIndex if it was pointing to the deleted model
            if (currentModelIndex == currentSelectedIndex) {
                currentModelIndex = currentSelectedIndex;
            } else if (currentModelIndex > currentSelectedIndex) {
                currentModelIndex--; // Adjust if it was pointing to a model after the deleted one
            }
            
            std::cout << "Model deleted successfully. Remaining models: " << currentScene.models.size() << std::endl;
        } else {
            std::cout << "No model selected or invalid selection" << std::endl;
        }
    }
}
#pragma endregion