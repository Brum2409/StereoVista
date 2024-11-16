// ---- Core Definitions ----
#define NOMINMAX
#include "core.h"
#include <thread>
#include <atomic>

// ---- Asset Loading ----
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

// ---- Project-Specific Includes ----
#include "obj_loader.h"
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
void renderEye(GLenum drawBuffer, const glm::mat4& projection, const glm::mat4& view, Shader* shader, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags);
void renderSphereCursor(const glm::mat4& projection, const glm::mat4& view);
void renderOrbitCenter(const glm::mat4& projection, const glm::mat4& view);
void renderGUI(bool isLeftEye, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, Shader* shader);
void renderSunManipulationPanel();
void renderCursorSettingsWindow();
void renderModelManipulationPanel(Engine::ObjModel& model, Shader* shader);
void renderPointCloudManipulationPanel(Engine::PointCloud& pointCloud);
void deleteSelectedPointCloud();
void renderModels(Shader* shader);

void renderPointClouds(Shader* shader);

// ---- Update Functions ----
void updateCursorPosition(GLFWwindow* window, const glm::mat4& projection, const glm::mat4& view, Shader* shader);
void updateFragmentShaderUniforms(Shader* shader);
void updatePointLights();

PointCloud loadPointCloudFile(const std::string& filePath, size_t downsampleFactor = 1);


// ---- Utility Functions ----
float calculateLargestModelDimension();
void calculateMouseRay(float mouseX, float mouseY, glm::vec3& rayOrigin, glm::vec3& rayDirection, glm::vec3& rayNear, glm::vec3& rayFar, float aspect);
bool rayIntersectsModel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const Engine::ObjModel& model, float& distance);

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

// ---- Stereo Rendering Settings ----
float maxSeparation = 0.05f;   // Maximum stereo separation
float minSeparation = 0.0001f; // Minimum stereo separation

// The convergence will shift the zFokus but there is still some weirdness when going into negative
float minConvergence = -3.0f;  // Minimum convergence
float maxConvergence = 3.0f;   // Maximum convergence

// ---- GUI Settings ----
bool showGui = true;
bool showFPS = true;
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

SelectedType currentSelectedType = SelectedType::None;
int currentSelectedIndex = -1;

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
    glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f)), // direction
    glm::vec3(1.0f, 1.0f, 0.9f),                    // warm sunlight color
    0.3f,                                           // intensity
    true                                            // enabled
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

    // ---- Set GLFW Callbacks ----
    glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
    glfwSetCursorPosCallback(window, mouse_callback);
    glfwSetScrollCallback(window, scroll_callback);
    glfwSetKeyCallback(window, key_callback);
    glfwSetMouseButtonCallback(window, mouse_button_callback);


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
    Engine::ObjModel cube = Engine::createCube(glm::vec3(1.0f, 1.0f, 1.0f), 32.0f, 0.0f);
    cube.scale = glm::vec3(0.5f);
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

    // ---- Calculate Largest Model Dimension ----
    float largestDimension = calculateLargestModelDimension();

    // ---- Initialize ImGui ----
    if (!InitializeImGuiWithFonts(window)) {
        std::cerr << "Failed to initialize ImGui with fonts" << std::endl;
        return -1;
    }

    // Configure additional ImGui settings
    ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;



    // ---- OpenGL Settings ----
    glEnable(GL_DEPTH_TEST);
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
    glfwSwapInterval(1); // Enable vsync

    setupSphereCursor();

    // ---- Main Loop ----
    while (!glfwWindowShouldClose(window)) {
        // ---- Per-frame Time Logic ----
        float currentFrame = glfwGetTime();
        deltaTime = currentFrame - lastFrame;
        lastFrame = currentFrame;

        if (deltaTime <= 0.0f) continue;

        // ---- Handle Input ----
        Input::handleKeyInput(camera, deltaTime);

        // ---- Update Camera Animation ----
        camera.UpdateAnimation(deltaTime);

        // ---- Set Projection Matrices ----
        glm::mat4 view = camera.GetViewMatrix();
        aspectRatio = (float)windowWidth / (float)windowHeight;
        glm::mat4 projection = camera.GetProjectionMatrix(aspectRatio, currentScene.settings.nearPlane, currentScene.settings.farPlane);
        glm::mat4 leftProjection = camera.offsetProjection(projection, currentScene.settings.separation / 2, abs(camera.Position.z) * currentScene.settings.convergence);
        glm::mat4 rightProjection = camera.offsetProjection(projection, -currentScene.settings.separation / 2, abs(camera.Position.z) * currentScene.settings.convergence);



        // ---- Set Common Shader Uniforms ----
        shader->use();
        shader->setVec3("viewPos", camera.Position);
        shader->setVec4("cursorPos", camera.IsOrbiting && orbitFollowsCursor && showSphereCursor ?
            glm::vec4(capturedCursorPos, g_cursorValid ? 1.0f : 0.0f) :
            glm::vec4(g_cursorPos, g_cursorValid ? 1.0f : 0.0f));
        updateFragmentShaderUniforms(shader);

        // Adjust camera speed
        float distanceToNearestObject = camera.getDistanceToNearestObject(camera, projection, view, currentScene.settings.farPlane, windowWidth, windowHeight);
        camera.AdjustMovementSpeed(distanceToNearestObject, largestDimension, currentScene.settings.farPlane);
        camera.isMoving = false;  // Reset the moving flag at the end of each frame

        // ---- Render for Left and Right Eye ----

        renderEye(GL_BACK_LEFT, leftProjection, view, shader, viewport, windowFlags);


        // Render for Right Eye (if in Stereo Mode)
        if (isStereoWindow) {
            renderEye(GL_BACK_RIGHT, rightProjection, view, shader, viewport, windowFlags);
        }

        // ---- Update Cursor Position ----
        updateCursorPosition(window, projection, view, shader);

        renderSphereCursor(leftProjection, view);

        if (isStereoWindow) {
            renderSphereCursor(rightProjection, view);
        }

        // Render GUI
        renderGUI(GL_BACK_LEFT, viewport, windowFlags, shader);

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

    for (const auto& vertex : currentScene.models[0].vertices) {
        minBounds = glm::min(minBounds, vertex.position);
        maxBounds = glm::max(maxBounds, vertex.position);
    }

    glm::vec3 modelsize = maxBounds - minBounds;
    return glm::max(glm::max(modelsize.x, modelsize.y), modelsize.z);
}

#pragma endregion


// ---- Rendering ----
#pragma region Rendering
void renderEye(GLenum drawBuffer, const glm::mat4& projection, const glm::mat4& view, Shader* shader, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags) {
    // Set the draw buffer and clear color and depth buffers
    glDrawBuffer(drawBuffer);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Reset OpenGL state
    glUseProgram(0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);

    // Set up shader for this eye
    shader->use();
    shader->setMat4("projection", projection);
    shader->setMat4("view", view);
    shader->setVec3("viewPos", camera.Position);

    // Set cursor position based on orbiting state
    shader->setVec4("cursorPos", camera.IsOrbiting && orbitFollowsCursor && showSphereCursor ?
        glm::vec4(capturedCursorPos, g_cursorValid ? 1.0f : 0.0f) :
        glm::vec4(g_cursorPos, g_cursorValid ? 1.0f : 0.0f));
    updateFragmentShaderUniforms(shader);

    // Render scene elements
    renderModels(shader);
    renderPointClouds(shader);
    renderOrbitCenter(projection, view);

    // Reset OpenGL state again
    glUseProgram(0);
    glBindVertexArray(0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
}

void renderSphereCursor(const glm::mat4& projection, const glm::mat4& view) {
    if (g_cursorValid && showSphereCursor) {
        // Enable depth testing and blending
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Set up sphere shader
        sphereShader->use();
        sphereShader->setMat4("projection", projection);
        sphereShader->setMat4("view", view);
        sphereShader->setVec3("viewPos", camera.Position);

        // Calculate cursor position and size
        glm::vec3 cursorRenderPos = camera.IsOrbiting && orbitFollowsCursor ? capturedCursorPos : g_cursorPos;
        float sphereRadius = calculateSphereRadius(cursorRenderPos, camera.Position);
        glm::mat4 model = glm::translate(glm::mat4(1.0f), cursorRenderPos);
        model = glm::scale(model, glm::vec3(sphereRadius));

        sphereShader->setMat4("model", model);
        sphereShader->setFloat("innerSphereFactor", innerSphereFactor);

        glBindVertexArray(sphereVAO);

        // Render inner sphere if enabled
        if (showInnerSphere) {
            glDepthMask(GL_FALSE);
            sphereShader->setBool("isInnerSphere", true);
            sphereShader->setVec4("sphereColor", innerSphereColor);
            sphereShader->setFloat("transparency", 1.0); // Full opacity for inner sphere
            glm::mat4 innerModel = glm::scale(model, glm::vec3(innerSphereFactor));
            sphereShader->setMat4("model", innerModel);
            glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);
        }

        // Render outer sphere
        glDepthMask(GL_TRUE);
        sphereShader->setBool("isInnerSphere", false);
        sphereShader->setVec4("sphereColor", cursorColor);
        sphereShader->setFloat("transparency", cursorTransparency);
        sphereShader->setFloat("edgeSoftness", cursorEdgeSoftness);
        sphereShader->setFloat("centerTransparencyFactor", cursorCenterTransparency);
        sphereShader->setMat4("model", model);
        glDrawElements(GL_TRIANGLES, sphereIndices.size(), GL_UNSIGNED_INT, 0);

        // Clean up
        glBindVertexArray(0);
        glUseProgram(0);
        glDisable(GL_BLEND);
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

void renderGUI(bool isLeftEye, ImGuiViewportP* viewport, ImGuiWindowFlags windowFlags, Shader* shader) {
    ImGui_ImplOpenGL3_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();

    if (showGui) {
        // Top bar
        if (ImGui::BeginMainMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("Open")) {
                    auto selection = pfd::open_file("Select a file to open", ".",
                        { "All Supported Files", "*.obj *.txt *.xyz *.ply *.pcb",
                          "OBJ Files", "*.obj",
                          "Point Cloud Files", "*.txt *.xyz *.ply *.pcb",
                          "All Files", "*" }).result();
                    if (!selection.empty()) {
                        std::string filePath = selection[0];
                        std::string extension = std::filesystem::path(filePath).extension().string();
                        if (extension == ".obj") {
                            Engine::ObjModel newModel = Engine::loadObjFile(filePath);
                            currentScene.models.push_back(newModel);
                            currentModelIndex = currentScene.models.size() - 1;
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
                                std::cout << "Added point cloud: " << newPointCloud.name << " with " << newPointCloud.points.size() << " points" << std::endl;
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
                        { "JSON Files", "*.json", "All Files", "*" }).result();
                    if (!selection.empty()) {
                        currentScene = Engine::loadScene(selection[0]);
                        currentModelIndex = currentScene.models.empty() ? -1 : 0;
                    }
                }
                if (ImGui::MenuItem("Save")) {
                    auto destination = pfd::save_file("Select a file to save scene", ".",
                        { "JSON Files", "*.json", "All Files", "*" }).result();
                    if (!destination.empty()) {
                        Engine::saveScene(destination, currentScene);
                    }
                }
                ImGui::EndMenu();
            }
            if (ImGui::MenuItem("Settings")) {
                showSettingsWindow = true;
            }
            ImGui::EndMainMenuBar();
        }

        ImGui::SetNextWindowPos(ImVec2(0, ImGui::GetFrameHeight()));
        ImGui::SetNextWindowSize(ImVec2(300, viewport->Size.y - ImGui::GetFrameHeight()));
        ImGui::Begin("Scene Objects", nullptr, ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse);

        if (ImGui::BeginChild("ObjectList", ImVec2(0, 268), true)) {
            ImGui::Columns(2, "ObjectColumns", false);
            ImGui::SetColumnWidth(0, 100); // Adjust this value to fit your needs

            ImGui::PushID("sun");
            bool sunVisible = sun.enabled;
            if (ImGui::Checkbox("##visible", &sunVisible)) {
                sun.enabled = sunVisible;
            }
            ImGui::NextColumn();

            bool isSunSelected = (currentSelectedType == SelectedType::Sun);
            ImGui::AlignTextToFramePadding();
            if (ImGui::Selectable("Sun", isSunSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                currentSelectedType = SelectedType::Sun;
                currentSelectedIndex = -1;  // Not needed for sun
            }
            ImGui::NextColumn();
            ImGui::PopID();

            for (int i = 0; i < currentScene.models.size(); i++) {
                ImGui::PushID(i);

                ImGui::AlignTextToFramePadding();
                bool visible = currentScene.models[i].visible;
                if (ImGui::Checkbox("##visible", &visible)) {
                    currentScene.models[i].visible = visible;
                }
                ImGui::NextColumn();
                bool isSelected = (currentSelectedIndex == i && currentSelectedType == SelectedType::Model);

                ImGui::AlignTextToFramePadding();
                if (ImGui::Selectable(currentScene.models[i].name.c_str(), isSelected, ImGuiSelectableFlags_SpanAllColumns)) {
                    currentSelectedIndex = i;
                    currentSelectedType = SelectedType::Model;
                }

                ImGui::NextColumn();
                ImGui::PopID();
            }

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
                    currentSelectedIndex = i;
                    currentSelectedType = SelectedType::PointCloud;
                }
                ImGui::NextColumn();

                ImGui::AlignTextToFramePadding();

                ImGui::PopID();
            }



            ImGui::Columns(1);
        }
        ImGui::EndChild();

        if (ImGui::Button("Create Cube", ImVec2(-1, 0))) {
            ObjModel newCube = Engine::createCube(glm::vec3(1.0f, 1.0f, 1.0f), 32.0f, 0.0f);
            newCube.scale = glm::vec3(0.5f);
            newCube.position = glm::vec3(0.0f, 0.0f, 0.0f);
            currentScene.models.push_back(newCube);
            currentSelectedIndex = currentScene.models.size() - 1;
            currentSelectedType = SelectedType::Model;
        }

        ImGui::Separator();

        // Manipulation panel
        if (currentSelectedType == SelectedType::Model && currentSelectedIndex >= 0 && currentSelectedIndex < currentScene.models.size()) {
            renderModelManipulationPanel(currentScene.models[currentSelectedIndex], shader);
        }
        else if (currentSelectedType == SelectedType::PointCloud && currentSelectedIndex >= 0 && currentSelectedIndex < currentScene.pointClouds.size()) {
            renderPointCloudManipulationPanel(currentScene.pointClouds[currentSelectedIndex]);
        }
        else if (currentSelectedType == SelectedType::Sun) {
            renderSunManipulationPanel();
        }

        ImGui::End();

        // Settings window
        if (showSettingsWindow) {
            ImGui::SetNextWindowSize(ImVec2(400, 300), ImGuiCond_FirstUseEver);
            ImGui::Begin("Settings", &showSettingsWindow);

            ImGui::Text("Camera Settings");
            ImGui::SliderFloat("Separation", &currentScene.settings.separation, minSeparation, maxSeparation);
            ImGui::SliderFloat("Convergence / ZFocus", &currentScene.settings.convergence, minConvergence, maxConvergence);
            ImGui::SliderFloat("Camera Speed Multiplier", &camera.speedFactor, 0.1f, 5.0f);
            ImGui::SliderFloat("Near Plane", &currentScene.settings.nearPlane, 0.01f, 10.0f);
            ImGui::SliderFloat("Far Plane", &currentScene.settings.farPlane, 10.0f, 1000.0f);

            ImGui::Checkbox("Show FPS", &showFPS);

            ImGui::Separator();

            ImGui::Checkbox("Show 3D Cursor", &show3DCursor);
            if (ImGui::Button("Cursor Settings")) {
                showCursorSettingsWindow = true;
            }

            ImGui::End();
        }

        if (showCursorSettingsWindow) {
            renderCursorSettingsWindow();
        }
    }

    if (showFPS) {
        ImGui::SetNextWindowPos(ImVec2(windowWidth - 120, windowHeight - 60));
        ImGui::Begin("FPS Counter", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoBackground);
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
        float pitch = glm::radians(angles.x);
        float yaw = glm::radians(angles.y);
        sun.direction = glm::normalize(glm::vec3(
            cos(pitch) * cos(yaw),
            sin(pitch),
            cos(pitch) * sin(yaw)
        ));
    }

    // Color with color picker
    ImGui::ColorEdit3("Color", glm::value_ptr(sun.color));

    // Intensity with slider
    ImGui::DragFloat("Intensity", &sun.intensity, 0.01f, 0.0f, 10.0f);

    // Display current direction vector
    ImGui::Text("Direction Vector: (%.2f, %.2f, %.2f)",
        sun.direction.x, sun.direction.y, sun.direction.z);
}

void renderCursorSettingsWindow() {
    ImGui::SetNextWindowSize(ImVec2(500, 600), ImGuiCond_FirstUseEver);
    ImGui::Begin("3D Cursor Settings", &showCursorSettingsWindow);

    // Preset management
    if (ImGui::BeginCombo("Cursor Preset", currentPresetName.c_str())) {
        std::vector<std::string> presetNames = Engine::CursorPresetManager::getPresetNames();

        // Add "New Preset" option
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

    // 3D Sphere Cursor Settings
    if (ImGui::CollapsingHeader("3D Sphere Cursor", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Checkbox("Show 3D Sphere Cursor", &showSphereCursor);

        if (showSphereCursor) {
            ImGui::Checkbox("Orbit Follows Cursor", &orbitFollowsCursor);

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
            ImGui::SliderFloat("Outer Radius", &fragmentCursorSettings.baseOuterRadius, 0.0f, 0.2f);
            ImGui::SliderFloat("Outer Border Thickness", &fragmentCursorSettings.baseOuterBorderThickness, 0.01f, 0.05f);
            ImGui::SliderFloat("Inner Radius", &fragmentCursorSettings.baseInnerRadius, 0.0f, 0.1f);
            ImGui::SliderFloat("Inner Border Thickness", &fragmentCursorSettings.baseInnerBorderThickness, 0.01f, 0.05f);
            ImGui::ColorEdit4("Outer Color", glm::value_ptr(fragmentCursorSettings.outerColor));
            ImGui::ColorEdit4("Inner Color", glm::value_ptr(fragmentCursorSettings.innerColor));
        }
    }

    ImGui::End();
}

void renderModelManipulationPanel(Engine::ObjModel& model, Shader* shader) {
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
        auto textureLoadingGUI = [&](const char* label, GLuint& textureID, std::string& texturePath, const char* uniformName) {
            char pathBuffer[256];
            strncpy_s(pathBuffer, texturePath.c_str(), sizeof(pathBuffer));
            pathBuffer[sizeof(pathBuffer) - 1] = '\0';

            ImGui::Text("%s", label);
            if (ImGui::InputText(("Path##" + std::string(label)).c_str(), pathBuffer, IM_ARRAYSIZE(pathBuffer))) {
                texturePath = pathBuffer;
            }
            if (ImGui::Button(("Browse##" + std::string(label)).c_str())) {
                auto selection = pfd::open_file("Select a texture file", ".",
                    { "Image Files", "*.png *.jpg *.jpeg *.bmp", "All Files", "*" }).result();
                if (!selection.empty()) {
                    texturePath = selection[0];
                    strcpy_s(pathBuffer, texturePath.c_str());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button(("Load##" + std::string(label)).c_str())) {
                GLuint newTexture = loadTextureFromFile(texturePath.c_str());
                if (newTexture != 0) {
                    if (textureID != 0) {
                        glDeleteTextures(1, &textureID);
                    }
                    textureID = newTexture;
                    std::cout << label << " loaded successfully." << std::endl;
                    model.hasCustomTexture = true;
                    shader->use();
                    shader->setInt(uniformName, textureID);
                }
            }
            };

        textureLoadingGUI("Diffuse Texture", model.texture, model.diffuseTexturePath, "texture_diffuse1");
        textureLoadingGUI("Normal Map", model.normalMap, model.normalTexturePath, "texture_normal1");
        textureLoadingGUI("Specular Map", model.specularMap, model.specularTexturePath, "texture_specular1");
        textureLoadingGUI("AO Map", model.aoMap, model.aoTexturePath, "texture_ao1");
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
        // Add any point cloud specific settings here
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


void renderModels(Shader* shader)
{
    updatePointLights();

    // Set point light uniforms
    for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
        std::string lightName = "lights[" + std::to_string(i) + "]";
        shader->setVec3(lightName + ".position", pointLights[i].position);
        shader->setVec3(lightName + ".color", pointLights[i].color);
        shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
    }
    shader->setInt("numLights", std::min((int)pointLights.size(), MAX_LIGHTS));
    glm::mat4 viewProj = camera.GetProjectionMatrix(aspectRatio, currentScene.settings.nearPlane, currentScene.settings.farPlane) * camera.GetViewMatrix();

    shader->setBool("sun.enabled", sun.enabled);
    shader->setVec3("sun.direction", sun.direction);
    shader->setVec3("sun.color", sun.color);
    shader->setFloat("sun.intensity", sun.intensity);

    for (int i = 0; i < currentScene.models.size(); i++) {
        const auto& model = currentScene.models[i];
        if (!model.visible) continue;
        glm::vec3 modelPos = glm::vec3(model.position);
        if (!camera.isInFrustum(modelPos, model.boundingSphereRadius, viewProj)) {
            continue;  // Skip this model if it's outside the frustum
        }

        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::translate(modelMatrix, model.position);
        modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x), glm::vec3(1, 0, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y), glm::vec3(0, 1, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z), glm::vec3(0, 0, 1));
        modelMatrix = glm::scale(modelMatrix, model.scale);

        shader->setMat4("model", modelMatrix);

        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, model.texture);
        shader->setInt("texture_diffuse1", 0);
        shader->setFloat("hasTexture", model.hasCustomTexture ? 1.0f : 0.0f);
        shader->setVec3("objectColor", model.color);

        shader->setBool("isPointCloud", false);

        glActiveTexture(GL_TEXTURE1);
        glBindTexture(GL_TEXTURE_2D, model.normalMap);
        shader->setInt("texture_normal1", 1);

        glActiveTexture(GL_TEXTURE2);
        glBindTexture(GL_TEXTURE_2D, model.specularMap);
        shader->setInt("texture_specular1", 2);

        glActiveTexture(GL_TEXTURE3);
        glBindTexture(GL_TEXTURE_2D, model.aoMap);
        shader->setInt("texture_ao1", 3);

        // Set flags for texture availability
        shader->setInt("hasNormalMap", model.normalMap != 0);
        shader->setInt("hasSpecularMap", model.specularMap != 0);
        shader->setInt("hasAOMap", model.aoMap != 0);

        // Set color and material properties
        shader->setFloat("shininess", model.shininess);
        shader->setFloat("emissive", model.emissive);

        // Set selection mode variables
        shader->setBool("selectionMode", selectionMode);
        shader->setBool("isSelected", selectionMode && (i == currentSelectedIndex) && (currentSelectedType == SelectedType::Model));

        glBindVertexArray(model.vao);
        glDrawElements(GL_TRIANGLES, model.indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
}

void renderPointClouds(Shader* shader) {
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

            float pointSizeMultiplier = 1.0f + (lodLevel) * 0.5f;  // Increase size for higher detail levels
            float adjustedPointSize = pointCloud.basePointSize * pointSizeMultiplier;

            // Bind the appropriate LOD VBO
            glBindBuffer(GL_ARRAY_BUFFER, chunk.lodVBOs[lodLevel]);

            // Set up vertex attributes
            glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)0);
            glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)offsetof(PointCloudPoint, color));
            glVertexAttribPointer(2, 1, GL_FLOAT, GL_FALSE, sizeof(PointCloudPoint), (void*)offsetof(PointCloudPoint, intensity));

            glEnableVertexAttribArray(0);
            glEnableVertexAttribArray(1);
            glEnableVertexAttribArray(2);

            // Set the point size
            glPointSize(adjustedPointSize);

            glDrawArrays(GL_POINTS, 0, chunk.lodPointCounts[lodLevel]);
        }

        glBindVertexArray(0);

        // Visualize chunk boundaries if enabled
        if (pointCloud.visualizeChunks) {
            shader->setBool("isChunkOutline", true);
            shader->setVec3("outlineColor", glm::vec3(1.0f, 1.0f, 0.0f)); // Yellow outline

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
            for (const auto& vertex : model.vertices) {
                glm::vec4 rotatedPos = rotationMatrix * glm::vec4(vertex.position, 1.0f);
                glm::vec3 worldPos = model.position + model.scale * glm::vec3(rotatedPos);
                minBounds = glm::min(minBounds, worldPos);
                maxBounds = glm::max(maxBounds, worldPos);
            }

            // Create multiple point lights distributed across the model's bounding box
            int numLightsPerDimension = 3; // Adjust this for more or fewer lights
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
    // Only update cursor position during left eye rendering
    static bool isLeftEye = true;
    if (!isLeftEye) {
        isLeftEye = true;
        return;
    }
    isLeftEye = false;

    // Read depth at cursor position
    float depth = 0.0;
    glReadPixels(lastX, (float)windowHeight - lastY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

    // Convert cursor position to world space
    glm::mat4 vpInv = glm::inverse(projection * view);
    glm::vec4 ndc = glm::vec4((lastX / (float)windowWidth) * 2.0 - 1.0, 1.0 - (lastY / (float)windowHeight) * 2.0, depth * 2.0 - 1.0, 1.0);
    auto worldPosH = vpInv * ndc;
    auto worldPos = worldPosH / worldPosH.w;
    auto isHit = depth != 1.0;

    static bool wasHit = false; // Keep track of previous hit state

    if (!((camera.IsOrbiting && orbitFollowsCursor) || camera.IsAnimating)) {
        if (isHit && (showSphereCursor || showFragmentCursor)) {
            g_cursorValid = true;
            g_cursorPos = glm::vec3(worldPos);

            if (!wasHit) {
                // Cursor just entered an object
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
            }
            wasHit = true;
        }
        else {
            g_cursorValid = false;

            if (wasHit) {
                // Cursor just left an object
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            }
            wasHit = false;
        }
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

bool rayIntersectsModel(const glm::vec3& rayOrigin, const glm::vec3& rayDirection, const Engine::ObjModel& model, float& distance) {
    float closestDistance = std::numeric_limits<float>::max();
    bool intersected = false;

    // Transform ray to model space
    glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), model.position) *
        glm::rotate(glm::mat4(1.0f), glm::radians(model.rotation.x), glm::vec3(1, 0, 0)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(model.rotation.y), glm::vec3(0, 1, 0)) *
        glm::rotate(glm::mat4(1.0f), glm::radians(model.rotation.z), glm::vec3(0, 0, 1)) *
        glm::scale(glm::mat4(1.0f), model.scale);
    glm::mat4 invModelMatrix = glm::inverse(modelMatrix);
    glm::vec3 rayOriginModel = glm::vec3(invModelMatrix * glm::vec4(rayOrigin, 1.0f));
    glm::vec3 rayDirectionModel = glm::vec3(invModelMatrix * glm::vec4(rayDirection, 0.0f));

    // Iterate through all triangles in the model
    for (size_t i = 0; i < model.indices.size(); i += 3) {
        glm::vec3 v0 = model.vertices[model.indices[i]].position;
        glm::vec3 v1 = model.vertices[model.indices[i + 1]].position;
        glm::vec3 v2 = model.vertices[model.indices[i + 2]].position;

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
                    currentSelectedIndex = closestModelIndex;
                    currentSelectedType = SelectedType::Model;
                    if (ctrlPressed) {
                        selectionMode = true;
                        isMovingModel = true;
                    }
                }
                else if (closestPointCloudIndex != -1) {
                    currentSelectedIndex = closestPointCloudIndex;
                    currentSelectedType = SelectedType::PointCloud;
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
                    if (orbitFollowsCursor && showSphereCursor) {
                        camera.StartCenteringAnimation(g_cursorPos);
                        capturedCursorPos = g_cursorPos;
                        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
                        isMouseCaptured = true;
                    }
                    else {
                        float cursorDepth = glm::length(g_cursorPos - camera.Position);
                        glm::vec3 viewportCenter = camera.Position + camera.Front * cursorDepth;
                        capturedCursorPos = viewportCenter;
                        camera.SetOrbitPointDirectly(capturedCursorPos);
                        camera.OrbitDistance = cursorDepth;
                        camera.StartOrbiting();
                    }
                }
                else {
                    capturedCursorPos = camera.Position + camera.Front * camera.OrbitDistance;
                    camera.SetOrbitPointDirectly(capturedCursorPos);
                    camera.StartOrbiting();
                }
            }
        }
        else if (action == GLFW_RELEASE) {
            if (orbitFollowsCursor && showSphereCursor && camera.IsOrbiting) {
                glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
                isMouseCaptured = false;
                glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
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
        }
        else if (action == GLFW_RELEASE) {
            middleMousePressed = false;
            camera.StopPanning();
        }
    }
    else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
        if (action == GLFW_PRESS) {
            rightMousePressed = true;
        }
        else if (action == GLFW_RELEASE) {
            rightMousePressed = false;
        }
    }
}

void mouse_callback(GLFWwindow* window, double xposIn, double yposIn) {
    if (ImGui::GetIO().WantCaptureMouse) {
        // Mouse is over ImGui UI, ensure Windows cursor is visible
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        return;
    }
    float xpos = static_cast<float>(xposIn);
    float ypos = static_cast<float>(yposIn);

    if (isMouseCaptured) {
        // Calculate offset from center of window
        float xoffset = xpos - (windowWidth / 2);
        float yoffset = (windowHeight / 2) - ypos;  // Reversed since y-coordinates go from bottom to top

        // Reset cursor to center of window
        glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);

        // Use these offsets for camera movement
        if (camera.IsOrbiting && !camera.IsAnimating) {
            camera.ProcessMouseMovement(xoffset, yoffset);
            glm::vec3 directionToCamera = glm::normalize(camera.Position - camera.OrbitPoint);
            camera.Position = camera.OrbitPoint + directionToCamera * camera.OrbitDistance;
        }
    }
    else {
        float xoffset = (xpos - lastX);
        float yoffset = (lastY - ypos);

        lastX = xpos;
        lastY = ypos;

        if (isMovingModel && currentSelectedType == SelectedType::Model && currentSelectedIndex != -1) {
            // Move selected model
            float distanceToModel = glm::distance(camera.Position, currentScene.models[currentSelectedIndex].position);
            float normalizedXOffset = xoffset / static_cast<float>(windowWidth);
            float normalizedYOffset = yoffset / static_cast<float>(windowHeight);
            float baseSensitivity = 0.66f;
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