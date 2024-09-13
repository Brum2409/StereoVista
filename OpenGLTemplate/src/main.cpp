// ---- Core Definitions ----
#define NOMINMAX
#include "core.h"

// ---- Asset Loading ----
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

// ---- Project-Specific Includes ----
#include "obj_loader.h"
#include "Camera.h"
#include "scene_manager.h"

// ---- GUI and Dialog ----
#include "imgui/imgui_incl.h"
#include <portable-file-dialogs.h>

// ---- Utility Libraries ----
#include <json.h>
#include <corecrt_math_defines.h>
#include <openLinks.h>


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
void renderModels(Shader* shader);

// ---- Update Functions ----
void updateCursorPosition(GLFWwindow* window, const glm::mat4& projection, const glm::mat4& view, Shader* shader);
void updateFragmentShaderUniforms(Shader* shader);
void updatePointLights();

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

// ---- Scene Persistence ----
static char saveFilename[256] = "scene.json"; // Buffer for saving scene filename
static char loadFilename[256] = "scene.json"; // Buffer for loading scene filename

// ---- Input and Interaction ----
bool selectionMode = false;
bool isMovingModel = false;
bool leftMousePressed = false;   // Left mouse button state
bool rightMousePressed = false;  // Right mouse button state
bool middleMousePressed = false; // Middle mouse button state
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

CursorPreset preset1 = {
    true,  // showSphereCursor
    true,  // showFragmentCursor
    0.0f,  // fragmentBaseInnerRadius
    CURSOR_FIXED,  // sphereScalingMode
    0.1f,  // sphereFixedRadius
    1.0f,  // sphereTransparency
    false  // showInnerSphere
};

CursorPreset preset2 = {
    true,  // showSphereCursor
    false,  // showFragmentCursor
    0.0f,  // fragmentBaseInnerRadius
    CURSOR_CONSTRAINED_DYNAMIC,  // sphereScalingMode
    0.7f,  // sphereFixedRadius
    0.7f,  // sphereTransparency
    true  // showInnerSphere
};
#pragma endregion


// ---- Sphere Cursor Functions
#pragma region Sphere Cursor Functions
// ---- Cursor Preset Application ----
void applyCursorPreset(const CursorPreset& preset) {
    showSphereCursor = preset.showSphereCursor;
    showFragmentCursor = preset.showFragmentCursor;
    fragmentCursorSettings.baseInnerRadius = preset.fragmentBaseInnerRadius;
    currentCursorScalingMode = preset.sphereScalingMode;
    fixedSphereRadius = preset.sphereFixedRadius;
    cursorTransparency = preset.sphereTransparency;
    showInnerSphere = preset.showInnerSphere;
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

    sphereShader = new Shader("assets/shaders/sphereVertexShader.glsl", "assets/shaders/sphereFragmentShader.glsl");

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
        shader = new Shader("assets/shaders/vertexShader.glsl", "assets/shaders/fragmentShader.glsl");
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

    // ---- Calculate Largest Model Dimension ----
    float largestDimension = calculateLargestModelDimension();

    // ---- Initialize ImGui ----
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO(); (void)io;
    ImGui_ImplGlfw_InitForOpenGL(window, true);
    ImGui_ImplOpenGL3_Init("#version 330 core");
    ImGuiViewportP* viewport = (ImGuiViewportP*)(void*)ImGui::GetMainViewport();
    ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNavFocus;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    // Setup ImGui style
    SetupImGuiStyle(true, 1.0f);
    strcpy_s(modelPathBuffer, modelPath.c_str());

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
        float aspectRatio = (float)windowWidth / (float)windowHeight;
        glm::mat4 projection = camera.GetProjectionMatrix(aspectRatio, currentScene.settings.nearPlane, currentScene.settings.farPlane);
        glm::mat4 leftProjection = camera.offsetProjection(projection, currentScene.settings.separation / 2, abs(camera.Position.z) * currentScene.settings.convergence);
        glm::mat4 rightProjection = camera.offsetProjection(projection, -currentScene.settings.separation / 2, abs(camera.Position.z) * currentScene.settings.convergence);

        // ---- Update Cursor Position ----
        updateCursorPosition(window, projection, view, shader);

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

        // ---- Render for Left Eye ----
        renderEye(GL_BACK_LEFT, leftProjection, view, shader, viewport, windowFlags);

        // ---- Render for Right Eye (if in Stereo Mode) ----
        if (isStereoWindow) {
            renderEye(GL_BACK_RIGHT, rightProjection, view, shader, viewport, windowFlags);
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
void cleanup(Shader * shader) {
    delete shader;
    glDeleteVertexArrays(1, &sphereVAO);
    glDeleteBuffers(1, &sphereVBO);
    glDeleteBuffers(1, &sphereEBO);
    delete sphereShader;
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
    renderOrbitCenter(projection, view);
    renderSphereCursor(projection, view);

    // Render GUI
    renderGUI(drawBuffer == GL_BACK_LEFT, viewport, windowFlags, shader);

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
        ImGui::BeginViewportSideBar("##ToolBar", viewport, ImGuiDir_Left, 630.0f, windowFlags | ImGuiWindowFlags_NoNavInputs);

        // Slider controls
        ImGui::Text("Camera Settings");
        ImGui::Separator();
        ImGui::SliderFloat("Separation", &currentScene.settings.separation, minSeparation, maxSeparation);

        ImGui::SliderFloat("Convergence / ZFocus", &currentScene.settings.convergence, minConvergence, maxConvergence);

        // FPS and Camera Settings
        ImGui::SliderFloat("Camera Speed Multiplier", &camera.speedFactor, 0.1f, 5.0f);
        ImGui::Checkbox("Show FPS", &showFPS);

        ImGui::SliderFloat("Near Plane", &currentScene.settings.nearPlane, 0.01f, 10.0f);
        ImGui::SliderFloat("Far Plane", &currentScene.settings.farPlane, 10.0f, 1000.0f);

        ImGui::Separator();

        // Model loading UI
        ImGui::Text("Model Loading");
        ImGui::InputText("Model Path", modelPathBuffer, IM_ARRAYSIZE(modelPathBuffer));
        if (ImGui::Button("Browse##Model")) {
            auto selection = pfd::open_file("Select a model file", ".",
                { "OBJ Files", "*.obj", "All Files", "*" }).result();
            if (!selection.empty()) {
                strncpy_s(modelPathBuffer, selection[0].c_str(), sizeof(modelPathBuffer) - 1);
            }
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Model")) {
            Engine::ObjModel newModel = Engine::loadObjFile(modelPathBuffer);
            currentScene.models.push_back(newModel);
            currentModelIndex = currentScene.models.size() - 1;
        }

        ImGui::Separator();

        // Model selection and manipulation
        if (!currentScene.models.empty()) {
            ImGui::Text("Model Manipulation");
            if (ImGui::BeginCombo("Select Model", currentScene.models[currentModelIndex].name.c_str())) {
                for (int i = 0; i < currentScene.models.size(); i++) {
                    bool isSelected = (currentModelIndex == i);
                    if (ImGui::Selectable(currentScene.models[i].name.c_str(), isSelected)) {
                        currentModelIndex = i;
                    }
                    if (isSelected) {
                        ImGui::SetItemDefaultFocus();
                    }
                }
                ImGui::EndCombo();
            }

            if (currentModelIndex >= 0) {
                ImGui::DragFloat3("Position", glm::value_ptr(currentScene.models[currentModelIndex].position), 0.1f);
                ImGui::DragFloat3("Scale", glm::value_ptr(currentScene.models[currentModelIndex].scale), 0.1f);
                ImGui::DragFloat3("Rotation", glm::value_ptr(currentScene.models[currentModelIndex].rotation), 1.0f, -360.0f, 360.0f);

                ImGui::Text("Texture Loading");

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
                            currentScene.models[currentModelIndex].hasCustomTexture = true;
                            shader->use();
                            shader->setInt(uniformName, textureID);
                        }
                    }
                    };

                textureLoadingGUI("Diffuse Texture", currentScene.models[currentModelIndex].texture,
                    currentScene.models[currentModelIndex].diffuseTexturePath, "texture_diffuse1");

                // ---- There is currently a problem with displaying Normal Maps ----
                /*textureLoadingGUI("Normal Map", currentScene.models[currentModelIndex].normalMap,
                    currentScene.models[currentModelIndex].normalTexturePath, "texture_normal1");*/

                textureLoadingGUI("Specular Map", currentScene.models[currentModelIndex].specularMap,
                    currentScene.models[currentModelIndex].specularTexturePath, "texture_specular1");
                textureLoadingGUI("AO Map", currentScene.models[currentModelIndex].aoMap,
                    currentScene.models[currentModelIndex].aoTexturePath, "texture_ao1");

                if (ImGui::Button("Delete Selected Model")) {
                    deleteSelectedModel();
                }
            }
        }

        ImGui::Separator();

        // Adding and clearing cubes
        ImGui::Text("Cube Operations");
        if (ImGui::Button("Add Cube")) {
            ObjModel newCube = Engine::createCube(glm::vec3(1.0f, 1.0f, 1.0f), 10.0f, 0.0f);
            currentScene.models.push_back(newCube);
            currentModelIndex = currentScene.models.size() - 1;
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear Cubes")) {
            currentScene.models.erase(
                std::remove_if(currentScene.models.begin(), currentScene.models.end(),
                    [](const ObjModel& model) { return model.name == "Cube"; }),
                currentScene.models.end()
            );
            if (currentModelIndex >= currentScene.models.size()) {
                currentModelIndex = currentScene.models.empty() ? -1 : currentScene.models.size() - 1;
            }
        }

        ImGui::Separator();

        // Model properties
        if (currentModelIndex >= 0 && currentModelIndex < currentScene.models.size()) {
            auto& model = currentScene.models[currentModelIndex];
            ImGui::Text("Model Properties");
            ImGui::ColorEdit3("Model Color", glm::value_ptr(model.color));
            ImGui::SliderFloat("Model Shininess", &model.shininess, 1.0f, 90.0f);
            ImGui::SliderFloat("Model Emissive", &model.emissive, 0.0f, 1.0f);
        }

        ImGui::Separator();


        // Cursor Settings
        if (ImGui::CollapsingHeader("Cursor Settings")) {
            if (ImGui::TreeNode("Cursor Presets")) {
                if (ImGui::Button("Apply Preset 1")) {
                    applyCursorPreset(preset1);
                }
                ImGui::SameLine();
                if (ImGui::Button("Apply Preset 2")) {
                    applyCursorPreset(preset2);
                }
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("3D Sphere Cursor")) {
                ImGui::Checkbox("Show 3D Sphere Cursor", &showSphereCursor);

                if (showSphereCursor) {
                    ImGui::Checkbox("Orbit Follows Cursor", &orbitFollowsCursor);

                    if (!orbitFollowsCursor) {
                        ImGui::Checkbox("Show Orbit Center", &showOrbitCenter);
                        if (showOrbitCenter) {
                            float color[4] = { orbitCenterColor.r, orbitCenterColor.g, orbitCenterColor.b, orbitCenterColor.a };
                            if (ImGui::ColorEdit4("Orbit Center Color", color)) {
                                orbitCenterColor = glm::vec4(color[0], color[1], color[2], color[3]);
                            }

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

                    float color[4] = { cursorColor.r, cursorColor.g, cursorColor.b, cursorColor.a };
                    if (ImGui::ColorEdit4("Cursor Color", color)) {
                        cursorColor = glm::vec4(color[0], color[1], color[2], color[3]);
                    }

                    ImGui::SliderFloat("Cursor Transparency", &cursorTransparency, 0.0f, 1.0f);
                    ImGui::SliderFloat("Edge Softness", &cursorEdgeSoftness, 0.0f, 1.0f);
                    ImGui::SliderFloat("Center Transparency", &cursorCenterTransparency, 0.0f, 1.0f);

                    ImGui::Checkbox("Show Inner Sphere", &showInnerSphere);
                    if (showInnerSphere) {
                        float innerColor[4] = { innerSphereColor.r, innerSphereColor.g, innerSphereColor.b, innerSphereColor.a };
                        if (ImGui::ColorEdit4("Inner Sphere Color", innerColor)) {
                            innerSphereColor = glm::vec4(innerColor[0], innerColor[1], innerColor[2], innerColor[3]);
                        }
                        ImGui::SliderFloat("Inner Sphere Factor", &innerSphereFactor, 0.1f, 0.9f);
                    }
                }

                ImGui::TreePop();
            }

            if (ImGui::TreeNode("Fragment Shader Cursor")) {
                ImGui::Checkbox("Show Fragment Shader Cursor", &showFragmentCursor);

                if (showFragmentCursor) {
                    ImGui::SliderFloat("Outer Radius", &fragmentCursorSettings.baseOuterRadius, 0.0f, 0.2f);
                    ImGui::SliderFloat("Outer Border Thickness", &fragmentCursorSettings.baseOuterBorderThickness, 0.01f, 0.05f);
                    ImGui::SliderFloat("Inner Radius", &fragmentCursorSettings.baseInnerRadius, 0.0f, 0.1f);
                    ImGui::SliderFloat("Inner Border Thickness", &fragmentCursorSettings.baseInnerBorderThickness, 0.01f, 0.05f);

                    float outerColor[4] = { fragmentCursorSettings.outerColor.r, fragmentCursorSettings.outerColor.g, fragmentCursorSettings.outerColor.b, fragmentCursorSettings.outerColor.a };
                    if (ImGui::ColorEdit4("Outer Color", outerColor)) {
                        fragmentCursorSettings.outerColor = glm::vec4(outerColor[0], outerColor[1], outerColor[2], outerColor[3]);
                    }

                    float innerColor[4] = { fragmentCursorSettings.innerColor.r, fragmentCursorSettings.innerColor.g, fragmentCursorSettings.innerColor.b, fragmentCursorSettings.innerColor.a };
                    if (ImGui::ColorEdit4("Inner Color", innerColor)) {
                        fragmentCursorSettings.innerColor = glm::vec4(innerColor[0], innerColor[1], innerColor[2], innerColor[3]);
                    }
                }

                ImGui::TreePop();
            }
        }

        ImGui::Separator();

        // Scene Management
        if (ImGui::CollapsingHeader("Scene Management")) {
            ImGui::InputText("Save Filename", saveFilename, IM_ARRAYSIZE(saveFilename));

            if (ImGui::Button("Browse##Save")) {
                auto destination = pfd::save_file("Select a file to save", ".",
                    { "JSON Files", "*.json", "All Files", "*" }).result();
                if (!destination.empty()) {
                    strncpy_s(saveFilename, destination.c_str(), sizeof(saveFilename) - 1);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Save Scene")) {
                Engine::saveScene(saveFilename, currentScene);
            }

            ImGui::InputText("Load Filename", loadFilename, IM_ARRAYSIZE(loadFilename));
            if (ImGui::Button("Browse##Load")) {
                auto selection = pfd::open_file("Select a file to load", ".",
                    { "JSON Files", "*.json", "All Files", "*" }).result();
                if (!selection.empty()) {
                    strncpy_s(loadFilename, selection[0].c_str(), sizeof(loadFilename) - 1);
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("Load Scene")) {
                currentScene = Engine::loadScene(loadFilename);
                currentModelIndex = currentScene.models.empty() ? -1 : 0;
            }
        }

        if (ImGui::Button("Info")) {
            showInfoWindow = !showInfoWindow;
        }

        if (showInfoWindow) {
            ImGui::SetNextWindowSize(ImVec2(400, 400), ImGuiCond_FirstUseEver);
            ImGui::Begin("Info", &showInfoWindow);

            ImGui::Text("Controls:");
            ImGui::BulletText("Left Mouse Button: Orbit camera / Select and move objects");
            ImGui::BulletText("Middle Mouse Button: Pan camera");
            ImGui::BulletText("Right Mouse Button: Rotate camera");
            ImGui::BulletText("Scroll Wheel: Zoom in/out");
            ImGui::BulletText("WASD: Move camera");
            ImGui::BulletText("Ctrl: Enter selection mode");
            ImGui::BulletText("Delete: Remove selected object");
            ImGui::BulletText("G: Toggle GUI visibility");

            ImGui::Separator();

            ImGui::Text("Libraries:");
            if (ImGui::TreeNode("OpenGL")) {
                ImGui::Text("Core graphics API");
                if (ImGui::Button("Website##OpenGL"))
                    openURL("https://www.opengl.org/");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("GLFW")) {
                ImGui::Text("Window creation and management");
                if (ImGui::Button("GitHub##GLFW"))
                    openURL("https://github.com/glfw/glfw");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("GLAD")) {
                ImGui::Text("OpenGL function loader");
                if (ImGui::Button("GitHub##GLAD"))
                    openURL("https://github.com/Dav1dde/glad");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("GLM")) {
                ImGui::Text("Mathematics library for graphics");
                if (ImGui::Button("GitHub##GLM"))
                    openURL("https://github.com/g-truc/glm");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("ImGui")) {
                ImGui::Text("User interface library");
                if (ImGui::Button("GitHub##ImGui"))
                    openURL("https://github.com/ocornut/imgui");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("stb_image")) {
                ImGui::Text("Image loading library");
                if (ImGui::Button("GitHub##stb_image"))
                    openURL("https://github.com/nothings/stb");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("tinyobjloader")) {
                ImGui::Text("Wavefront .obj file loader");
                if (ImGui::Button("GitHub##tinyobjloader"))
                    openURL("https://github.com/tinyobjloader/tinyobjloader");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("json.hpp")) {
                ImGui::Text("JSON for Modern C++");
                if (ImGui::Button("GitHub##json.hpp"))
                    openURL("https://github.com/nlohmann/json");
                ImGui::TreePop();
            }
            if (ImGui::TreeNode("portable-file-dialogs")) {
                ImGui::Text("Cross-platform file dialog library");
                if (ImGui::Button("GitHub##portable-file-dialogs"))
                    openURL("https://github.com/samhocevar/portable-file-dialogs");
                ImGui::TreePop();
            }

            ImGui::End();
        }
        ImGui::End();
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

    for (int i = 0; i < currentScene.models.size(); i++) {
        const auto& model = currentScene.models[i];
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
        shader->setBool("isSelected", selectionMode && (i == currentModelIndex));

        glBindVertexArray(model.vao);
        glDrawElements(GL_TRIANGLES, model.indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }
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



// ---- Cursor and Ray Casting ----
#pragma region Cursor and Ray Casting
void updateCursorPosition(GLFWwindow* window, const glm::mat4& projection, const glm::mat4& view, Shader* shader) {
    // Prepare for rendering
    glDrawBuffer(GL_BACK);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
    shader->use();
    shader->setMat4("projection", projection);
    shader->setMat4("view", view);
    renderModels(shader);

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
    if (currentModelIndex >= 0 && currentModelIndex < currentScene.models.size()) {
        currentScene.models.erase(currentScene.models.begin() + currentModelIndex);
        if (currentScene.models.empty()) {
            currentModelIndex = -1;
        }
        else {
            currentModelIndex = std::min(currentModelIndex, static_cast<int>(currentScene.models.size()) - 1);
        }
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
        // Mouse is over ImGui UI, ensure Windows cursor is visible
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        return;
    }

    if (button == GLFW_MOUSE_BUTTON_LEFT) {
        if (action == GLFW_PRESS) {
            // Handle double-click
            double currentTime = glfwGetTime();
            if (currentTime - lastClickTime < doubleClickTime) {
                if (g_cursorValid) {
                    camera.StartCenteringAnimation(g_cursorPos);
                }
            }
            lastClickTime = currentTime;

            if (!camera.IsAnimating && !camera.IsOrbiting) {
                leftMousePressed = true;

                if (g_cursorValid) {
                    // Handle orbit and cursor following
                    if (orbitFollowsCursor && showSphereCursor) {
                        camera.StartCenteringAnimation(g_cursorPos);
                        capturedCursorPos = g_cursorPos;
                    }
                    else {
                        // Set orbit point and start orbiting
                        float cursorDepth = glm::length(g_cursorPos - camera.Position);
                        glm::vec3 viewportCenter = camera.Position + camera.Front * cursorDepth;
                        capturedCursorPos = viewportCenter;
                        camera.SetOrbitPointDirectly(capturedCursorPos);
                        camera.OrbitDistance = cursorDepth;
                        camera.StartOrbiting();
                    }
                }
                else {
                    // Set default orbit point
                    capturedCursorPos = camera.Position + camera.Front * camera.OrbitDistance;
                    camera.SetOrbitPointDirectly(capturedCursorPos);
                    camera.StartOrbiting();
                }
            }
            if (selectionMode) {
                glm::vec3 rayOrigin, rayDirection, rayNear, rayFar;
                calculateMouseRay(lastX, lastY, rayOrigin, rayDirection, rayNear, rayFar, (float)windowWidth / (float)windowHeight);

                float closestDistance = std::numeric_limits<float>::max();
                int closestModelIndex = -1;

                for (int i = 0; i < currentScene.models.size(); i++) {
                    const auto& model = currentScene.models[i];
                    float distance;
                    if (rayIntersectsModel(rayOrigin, rayDirection, model, distance)) {
                        if (distance < closestDistance) {
                            closestDistance = distance;
                            closestModelIndex = i;
                        }
                    }
                }

                if (closestModelIndex != -1) {
                    currentModelIndex = closestModelIndex;
                    isMovingModel = true;
                }
            }
        }
        else if (action == GLFW_RELEASE) {
            leftMousePressed = false;
            if (orbitFollowsCursor && showSphereCursor && camera.IsOrbiting) {
                glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
            }
            camera.StopOrbiting();
            leftMousePressed = false;
            isMovingModel = false;

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
    if (button == GLFW_MOUSE_BUTTON_RIGHT) {
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

    float xoffset = (xpos - lastX);
    float yoffset = (lastY - ypos);

    lastX = xpos;
    lastY = ypos;

    if (isMovingModel && currentModelIndex != -1) {
        // Move selected model
        float distanceToModel = glm::distance(camera.Position, currentScene.models[currentModelIndex].position);
        float aspectRatio = static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
        float normalizedXOffset = xoffset / static_cast<float>(windowWidth);
        float normalizedYOffset = yoffset / static_cast<float>(windowHeight);
        float baseSensitivity = 0.66f;
        float sensitivityFactor = (baseSensitivity * distanceToModel);
        normalizedXOffset *= aspectRatio;

        glm::vec3 right = glm::normalize(glm::cross(camera.Front, camera.Up));
        glm::vec3 up = glm::normalize(glm::cross(right, camera.Front));

        currentScene.models[currentModelIndex].position += right * normalizedXOffset * sensitivityFactor;
        currentScene.models[currentModelIndex].position += up * normalizedYOffset * sensitivityFactor;
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
            selectionMode = true;
            std::cout << "Entered selection mode" << std::endl;
        }
        else if (action == GLFW_RELEASE)
        {
            selectionMode = false;
            std::cout << "Exited selection mode" << std::endl;
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