// ---- Core Definitions ----
#define NOMINMAX
#include "Engine/Core.h"
#include <atomic>
#include <iostream>
#include <thread>

// ---- Project-Specific Includes ----
#include "../headers/Engine/BVH.h"
#include "../headers/Engine/BVHDebug.h"
#include "../headers/Engine/BloomRenderer.h"
#include "../headers/Engine/IrradianceCache.h"
#include "Core/Camera.h"
#include "Core/CursorSyncState.h"
#include "Core/CursorSynchronizer.h"
#include "Core/SceneManager.h"
#include "Core/Voxalizer.h"
#include "Cursors/Base/CursorManager.h"
#include "Cursors/CursorPresets.h"
#include "Engine/OctreePointCloudManager.h"
#include "Engine/ShortcutManager.h"
#include "Engine/SpaceMouseInput.h"
#include "Gui/CursorPreview3D.h"
#include "Gui/Gui.h"
#include "Gui/GuiTypes.h"
#include "Loaders/ModelLoader.h"
#include "Loaders/PointCloudLoader.h"
#include "Tools/BrushTool.h"

// ---- GUI and Dialog ----
#include "imgui/imgui_incl.h"
#include "imgui/imgui_sytle.h"
#include <portable-file-dialogs.h>

// ---- Utility Libraries ----
#include <corecrt_math_defines.h>
#include <fstream>
#include <glm/gtx/component_wise.hpp>
#include <iostream>
#include <json.h>
#include <openLinks.h>
#include <stb_image.h>

using namespace Engine;
using json = nlohmann::json;

// Use the GUI namespace types
using GUI::ApplicationPreferences;
using GUI::CubemapPreset;
using GUI::CURSOR_CONSTRAINED_DYNAMIC;
using GUI::CURSOR_FIXED;
using GUI::CURSOR_LOGARITHMIC;
using GUI::CURSOR_NORMAL;
using GUI::CursorScalingMode;
using GUI::FragmentShaderCursorSettings;
using GUI::SKYBOX_CUBEMAP;
using GUI::SKYBOX_GRADIENT;
using GUI::SKYBOX_SOLID_COLOR;
using GUI::SkyboxType;

// ---- Function Declarations ----
#pragma region Function Declarations
// ---- GLFW Callback Functions ----
void key_callback(GLFWwindow *window, int key, int scancode, int action,
                  int mods);
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset);
void mouse_button_callback(GLFWwindow *window, int button, int action,
                           int mods);
void mouse_callback(GLFWwindow *window, double xpos, double ypos);
void framebuffer_size_callback(GLFWwindow *window, int width, int height);
void cursor_enter_callback(GLFWwindow *window, int entered);

// ---- Rendering Functions ----
void renderEye(GLenum drawBuffer, const glm::mat4 &projection,
               const glm::mat4 &view, Engine::Shader *shader,
               ImGuiViewportP *viewport, ImGuiWindowFlags windowFlags,
               GLFWwindow *window, bool renderGUIFlag = true,
               bool isStereo = false, const glm::mat4 *leftProjection = nullptr,
               const glm::mat4 *leftView = nullptr,
               const glm::mat4 *rightProjection = nullptr,
               const glm::mat4 *rightView = nullptr);
void renderModels(Engine::Shader *shader);
void renderPointClouds(Engine::Shader *shader);
void renderLightVisualizations(Engine::Shader *shader);
void renderZeroPlane(Engine::Shader *shader, const glm::mat4 &projection,
                     const glm::mat4 &view, float convergence);
void renderSkybox(const glm::mat4 &projection, const glm::mat4 &view,
                  Engine::Shader *mainShader);
void bindSkyboxUniforms(Engine::Shader *shader);

void DrawRadar(bool isStereoWindow, Camera camera, GLfloat focaldist,
               glm::mat4 view, glm::mat4 projection, glm::mat4 leftview,
               glm::mat4 leftprojection, glm::mat4 rightview,
               glm::mat4 rightprojection, Engine::Shader *shader,
               bool renderScene, float radarScale, glm::vec2 position);
glm::vec4 divw(glm::vec4 vec);

// ---- Update Functions ----
void updatePointLights();
void updateSpaceMouseBounds();
void updateSpaceMouseCursorAnchor();

PointCloud loadPointCloudFile(const std::string &filePath,
                              size_t downsampleFactor = 1);

void createDefaultCubemap();
bool loadHDRSkybox(const std::string &hdrPath);
void initSkybox();
void setupPointShadowMapping();

void cleanup();

// ---- Utility Functions ----
float calculateLargestModelDimension();
void calculateMouseRay(float mouseX, float mouseY, glm::vec3 &rayOrigin,
                       glm::vec3 &rayDirection, glm::vec3 &rayNear,
                       glm::vec3 &rayFar, float aspect);
bool rayIntersectsModel(const glm::vec3 &rayOrigin,
                        const glm::vec3 &rayDirection,
                        const Engine::Model &model, float &distance);
bool rayIntersectsModel(const glm::vec3 &rayOrigin,
                        const glm::vec3 &rayDirection,
                        const Engine::Model &model, float &distance,
                        glm::vec3 &outNormal);

// ---- Preferences Functions ----
void savePreferences();
void printCursorSyncDiagnostics();
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
std::shared_ptr<Camera> spaceMouseCameraPtr =
    std::make_shared<Camera>(spaceMouseCamera);
SpaceMouseInput spaceMouseInput;
bool spaceMouseInitialized = false;
bool spaceMouseActive = false;
float lastX = 1920.0f / 2.0;
float lastY = 1080.0f / 2.0;
float aspectRatio = 1.0f;
float mouseSmoothingFactor = 0.7;

// ---- Stereo Rendering Settings ----
float maxSeparation = 2.0f;  // Maximum stereo separation
float minSeparation = 0.01f; // Minimum stereo separation

// The convergence will shift the zFokus but there is still some weirdness when
// going into negative
float minConvergence = 0.0f;  // Minimum convergence
float maxConvergence = 40.0f; // Maximum convergence

// Auto convergence smoothing
float targetConvergence = 2.6f; // Target convergence for smooth interpolation
float convergenceSmoothingSpeed =
    5.0f; // Speed of convergence interpolation (higher = faster)

double accumulatedXOffset = 0.0;
double accumulatedYOffset = 0.0;

bool windowHasFocus = true; // Assume focused initially
bool justRegainedFocus =
    false; // Flag to handle the first mouse event after focus regain
bool firstMouse = true;

// ---- GUI Settings ----
bool showGui = true;
bool showFPS = true;
bool isDarkTheme = true;
bool showInfoWindow = false;
bool showSettingsWindow = false;
bool show3DCursor = true;
bool showCursorSettingsWindow = false;
bool showBrushToolWindow = false;
enum class SelectedType {
  None,
  Model,
  PointCloud,
  Sun,
  PointLight,
  SpotLight,
  BrushCluster
};

std::atomic<bool> isRecalculatingChunks(false);

struct SelectionState {
  SelectedType type = SelectedType::None;
  int modelIndex = -1;
  int meshIndex = -1; // New: track selected mesh within model
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

// ---- Shortcut Manager ----
StereoVista::ShortcutManager shortcutManager;

// ---- Scene Persistence ----
static char saveFilename[256] =
    "scene.json"; // Buffer for saving scene filename
static char loadFilename[256] =
    "scene.json"; // Buffer for loading scene filename

std::string currentPresetName = "Default";
bool isEditingPresetName = false;
char editPresetNameBuffer[256] = "";

// Cursor 3D Preview
GUI::CursorPreview3D cursorPreview3D;

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

// ---- Model Movement Physics ----
float modelScrollVelocity = 0.0f; // Current velocity for model depth movement
float modelScrollMomentum = 0.5f; // Momentum factor (same as camera)
float modelMaxScrollVelocity = 3.0f;  // Maximum velocity (same as camera)
float modelScrollDeceleration = 5.0f; // Deceleration rate (same as camera)
float lastModelScrollTime = 0.0f;     // Last time model scroll was processed

// ---- Model Movement Cursor Tracking ----
glm::vec3
    modelGrabPoint; // World space position where model was initially grabbed
bool hasModelGrabPoint = false; // Whether a valid grab point exists

// ---- Timing ----
float deltaTime = 0.0f;
float lastFrame = 0.0f;

// ---- Cursor System ----
Cursor::CursorManager cursorManager;
glm::vec3 capturedCursorPos;
bool orbitFollowsCursor = false;

// ---- Brush Tool ----
Tools::BrushTool brushTool;

// ---- Window Configuration ----
int windowWidth = 1920;
int windowHeight = 1080;
bool isStereoWindow = false;

// ---- Lighting ----
std::vector<Engine::PointLight> pointLights;
std::vector<Engine::SpotLight> spotLights;
float zOffset = 0.5f;
Engine::Sun sun = {
    glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f)), // More vertical angle
    glm::vec3(1.0f, 0.95f, 0.8f),                   // Warmer color
    0.16f,                                          // Higher intensity
    true};

unsigned int depthMapFBO;
unsigned int depthMap;
const unsigned int SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;
Engine::Shader *simpleDepthShader = nullptr;
glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);

// Point shadow mapping variables
unsigned int depthCubemap;
unsigned int depthMapFBO_point;
const unsigned int SHADOW_WIDTH_POINT = 1024, SHADOW_HEIGHT_POINT = 1024;
Engine::Shader *pointShadowShader = nullptr;
float far_plane = 50.0f;
Engine::Shader *radianceShader = nullptr;
Engine::Shader *shadowMappingShader = nullptr;
Engine::Shader *voxelConeTracingShader = nullptr;
Engine::Shader *instancedShader = nullptr;

// Bloom rendering system
Engine::BloomRenderer *bloomRenderer = nullptr;

GUI::LightingMode currentLightingMode = GUI::LIGHTING_SHADOW_MAPPING;
bool enableShadows = true;

GUI::VCTSettings vctSettings;
GUI::ApplicationPreferences::RadianceSettings radianceSettings;

// Predefined cubemap paths
std::vector<GUI::CubemapPreset> cubemapPresets = {
    {"Default", "skybox/Default/", "Default skybox environment"},
    {"Yokohama", "skybox/Yokohama/",
     "Yokohama, Japan. View towards Intercontinental Yokohama Grand hotel."},
    {"Storforsen", "skybox/Storforsen/",
     "At the top of Storforsen. Taken with long exposure, resulting in smooth "
     "looking water flow."},
    {"Yokohama Night", "skybox/YokohamaNight/", "Yokohama at night."},
    {"Lycksele", "skybox/Lycksele/",
     "Lycksele. View of Ansia Camping, Lycksele."}};

Engine::Voxelizer *voxelizer = nullptr;

#pragma endregion

GLuint skyboxVAO, skyboxVBO, cubemapTexture;
float ambientStrengthFromSkybox = 0.1f;
Engine::Shader *skyboxShader = nullptr;

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
bool triangleDataUploaded = false;
bool enableBVH = true; // BVH toggle

// BVH Debug Renderer
Engine::BVHDebugRenderer bvhDebugRenderer;
bool showBVHDebug = false;

// ---- World-Space Irradiance Cache System ----
Engine::IrradianceCache *irradianceCache = nullptr;
Engine::Shader *irradianceCacheComputeShader = nullptr;

// BVH invalidation tracking
struct SceneState {
  size_t modelCount = 0;
  std::vector<glm::vec3> modelPositions;
  std::vector<glm::vec3> modelRotations;
  std::vector<glm::vec3> modelScales;

  bool hasChanged(const Engine::Scene &scene) const {
    if (modelCount != scene.models.size())
      return true;

    for (size_t i = 0; i < scene.models.size() && i < modelPositions.size();
         i++) {
      if (modelPositions[i] != scene.models[i].position ||
          modelRotations[i] != scene.models[i].rotation ||
          modelScales[i] != scene.models[i].scale) {
        return true;
      }
    }
    return false;
  }

  void update(const Engine::Scene &scene) {
    modelCount = scene.models.size();
    modelPositions.clear();
    modelRotations.clear();
    modelScales.clear();

    for (const auto &model : scene.models) {
      modelPositions.push_back(model.position);
      modelRotations.push_back(model.rotation);
      modelScales.push_back(model.scale);
    }
  }
};
static SceneState lastSceneState;

// ---- Zero Plane Rendering ----
Engine::Shader *zeroPlaneShader = nullptr;
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

void window_focus_callback(GLFWwindow *window, int focused) {
  if (focused) {
    // The window gained input focus
    windowHasFocus = true;
    justRegainedFocus = true;
    firstMouse = true;
  } else {
    // The window lost input focus
    windowHasFocus = false;
  }
}

void cursor_enter_callback(GLFWwindow *window, int entered) {
  // Update cursor manager to track if cursor is inside window
  cursorManager.SetCursorInsideWindow(entered == GLFW_TRUE);

  // When cursor exits, immediately invalidate cursor position to prevent
  // one frame of rendering at the window edge
  if (entered == GLFW_FALSE) {
    cursorManager.getSphereCursor()->setPositionValid(false);
    cursorManager.getFragmentCursor()->setPositionValid(false);
    cursorManager.getPlaneCursor()->setPositionValid(false);
  }
}

// Helper functions for cubemap and skybox rendering
GLuint loadCubemap(const std::vector<std::string> &faces) {
  GLuint textureID;
  glGenTextures(1, &textureID);
  glBindTexture(GL_TEXTURE_CUBE_MAP, textureID);

  int width, height, nrChannels;
  for (unsigned int i = 0; i < faces.size(); i++) {
    unsigned char *data =
        stbi_load(faces[i].c_str(), &width, &height, &nrChannels, 0);
    if (data) {
      glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB, width, height,
                   0, GL_RGB, GL_UNSIGNED_BYTE, data);
      stbi_image_free(data);
    } else {
      std::cerr << "Cubemap texture failed to load at path: " << faces[i]
                << std::endl;
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

void setupSkyboxVAO(GLuint &skyboxVAO, GLuint &skyboxVBO) {
  float skyboxVertices[] = {
      // positions
      -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f,
      1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f,

      -1.0f, -1.0f, 1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  -1.0f,
      -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f, 1.0f,

      1.0f,  -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,
      1.0f,  1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f,

      -1.0f, -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,  1.0f,  1.0f,
      1.0f,  1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f, -1.0f, 1.0f,

      -1.0f, 1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  1.0f,
      1.0f,  1.0f,  1.0f,  -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f,  -1.0f,

      -1.0f, -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, -1.0f,
      1.0f,  -1.0f, -1.0f, -1.0f, -1.0f, 1.0f,  1.0f,  -1.0f, 1.0f};

  glGenVertexArrays(1, &skyboxVAO);
  glGenBuffers(1, &skyboxVBO);

  glBindVertexArray(skyboxVAO);
  glBindBuffer(GL_ARRAY_BUFFER, skyboxVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(skyboxVertices), &skyboxVertices,
               GL_STATIC_DRAW);

  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void *)0);

  std::vector<std::string> skyboxDirs = {"skybox",
                                         "skybox/Default",
                                         "skybox/Yokohama",
                                         "skybox/Storforsen",
                                         "skybox/YokohamaNight",
                                         "skybox/Lycksele"};

  for (const auto &dir : skyboxDirs) {
    if (!std::filesystem::exists(dir)) {
      std::filesystem::create_directory(dir);
      std::cout << "Created directory: " << dir << std::endl;
    }
  }
}

void createSolidColorSkybox(const glm::vec3 &color) {
  glGenTextures(1, &cubemapTexture);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);

  // Create a 1x1 texture for each face with the specified color
  GLubyte texData[] = {
      static_cast<GLubyte>(color.r * 255), static_cast<GLubyte>(color.g * 255),
      static_cast<GLubyte>(color.b * 255),
      255 // Alpha (fully opaque)
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
void createGradientSkybox(const glm::vec3 &topColor,
                          const glm::vec3 &bottomColor) {
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
    } else if (face == 2) {
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
    } else {
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

    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB, size, size,
                 0, GL_RGB, GL_UNSIGNED_BYTE, faceData.data());
  }

  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
}

// Load a specific cubemap from path
bool loadSkyboxFromPath(const std::string &basePath) {
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
      {{"right.jpg", "left.jpg", "top.jpg", "bottom.jpg", "front.jpg",
        "back.jpg"},
       "Standard naming"},
      // Directional naming (posx, negx, etc.)
      {{"posx.jpg", "negx.jpg", "posy.jpg", "negy.jpg", "posz.jpg", "negz.jpg"},
       "Directional naming"},
      // Alternative naming
      {{"east.jpg", "west.jpg", "up.jpg", "down.jpg", "north.jpg", "south.jpg"},
       "Cardinal directions"}};

  // Try different file extensions
  std::vector<std::string> extensions = {".jpg", ".png", ".tga", ".bmp"};

  // Check if directory exists
  std::string fullPath = basePath;
  if (fullPath.back() != '/' && fullPath.back() != '\\') {
    fullPath += '/';
  }

  for (const auto &convention : conventions) {
    for (const auto &ext : extensions) {
      std::vector<std::string> faces;
      bool allFilesExist = true;

      // Build list of faces with current convention and extension
      for (int i = 0; i < 6; ++i) {
        std::string filename = convention.faceNames[i];

        // Replace extension if needed
        if (filename.size() > 4) {
          std::string currentExt = filename.substr(filename.size() - 4);
          if (currentExt == ".jpg" || currentExt == ".png" ||
              currentExt == ".tga" || currentExt == ".bmp") {
            filename = filename.substr(0, filename.size() - 4) + ext;
          } else {
            filename += ext;
          }
        } else {
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
          std::cout << "Skybox textures loaded from: " << fullPath << " using "
                    << convention.description << std::endl;
          return true;
        } catch (const std::exception &e) {
          std::cerr << "Failed to load skybox textures from " << fullPath
                    << ": " << e.what() << std::endl;
        }
      }
    }
  }

  std::cerr << "Could not find a complete set of skybox textures in "
            << fullPath << std::endl;
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

void updateTriangleBuffer(const std::vector<float> &data) {
  if (triangleSSBO == 0) {
    setupTriangleBuffer();
  }

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER, data.size() * sizeof(float),
               data.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triangleSSBO);
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

void cleanupTriangleBuffer() {
  if (triangleSSBO != 0) {
    glDeleteBuffers(1, &triangleSSBO);
    triangleSSBO = 0;
  }
}

// ---- Irradiance Cache Functions ----
// Old screen-space cache functions removed - now using world-space
// IrradianceCache class

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
  if (!bvhBuilt)
    return;

  setupBVHBuffers();

  // Upload BVH nodes to SSBO binding 1
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, bvhNodeSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               gpuBVHNodes.size() * sizeof(Engine::GPUBVHNode),
               gpuBVHNodes.data(), GL_STATIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bvhNodeSSBO);

  // Upload triangle indices to SSBO binding 2
  glBindBuffer(GL_SHADER_STORAGE_BUFFER, triangleIndexSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               gpuTriangleIndices.size() * sizeof(uint32_t),
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

void buildBVH(const std::vector<Engine::BVHTriangle> &triangles) {
  if (triangles.empty()) {
    bvhBuilt = false;
    return;
  }

  std::cout << "Building BVH for " << triangles.size() << " triangles..."
            << std::endl;

  // Build BVH
  bvhBuilder.build(triangles);

  // Convert to GPU format
  const auto &nodes = bvhBuilder.getNodes();
  const auto &indices = bvhBuilder.getTriangleIndices();
  const auto &bvhTriangles = bvhBuilder.getTriangles();

  // Convert BVH nodes to GPU format
  gpuBVHNodes.clear();
  gpuBVHNodes.reserve(nodes.size());
  for (const auto &node : nodes) {
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
  for (const auto &tri : bvhTriangles) {
    Engine::GPUTriangle gpuTri;
    gpuTri.v0[0] = tri.v0.x;
    gpuTri.v0[1] = tri.v0.y;
    gpuTri.v0[2] = tri.v0.z;
    gpuTri.v0[3] = 0.0f;
    gpuTri.v1[0] = tri.v1.x;
    gpuTri.v1[1] = tri.v1.y;
    gpuTri.v1[2] = tri.v1.z;
    gpuTri.v1[3] = 0.0f;
    gpuTri.v2[0] = tri.v2.x;
    gpuTri.v2[1] = tri.v2.y;
    gpuTri.v2[2] = tri.v2.z;
    gpuTri.v2[3] = 0.0f;
    gpuTri.normal[0] = tri.normal.x;
    gpuTri.normal[1] = tri.normal.y;
    gpuTri.normal[2] = tri.normal.z;
    gpuTri.normal[3] = 0.0f;
    gpuTri.color[0] = tri.color.x;
    gpuTri.color[1] = tri.color.y;
    gpuTri.color[2] = tri.color.z;
    gpuTri.color[3] = tri.emissiveness;
    gpuTri.shininess = tri.shininess;
    gpuTri.materialId = static_cast<uint32_t>(tri.materialId);
    gpuTri.padding[0] = 0.0f;
    gpuTri.padding[1] = 0.0f;
    gpuTriangles.push_back(gpuTri);
  }

  bvhBuilt = true;
  bvhBuffersUploaded = false;   // Mark that buffers need to be uploaded
  triangleDataUploaded = false; // Mark that triangle data needs to be uploaded
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
    createGradientSkybox(skyboxConfig.gradientBottomColor,
                         skyboxConfig.gradientTopColor);
    break;

  case GUI::SKYBOX_HDR:
    if (!skyboxConfig.hdrPath.empty()) {
      if (!loadHDRSkybox(skyboxConfig.hdrPath)) {
        // Fallback to default cubemap if HDR loading fails
        createDefaultCubemap();
      }
    } else {
      // No HDR path specified, fallback to default
      createDefaultCubemap();
    }
    break;

  case GUI::SKYBOX_CUBEMAP:
  default:
    // Try to load the selected cubemap
    if (skyboxConfig.selectedCubemap >= 0 &&
        skyboxConfig.selectedCubemap < cubemapPresets.size()) {

      if (!loadSkyboxFromPath(
              cubemapPresets[skyboxConfig.selectedCubemap].path)) {
        // Fallback to default cubemap
        createDefaultCubemap();
      }
    } else {
      // Fallback to default cubemap
      createDefaultCubemap();
    }
    break;
  }

  // Always (re)create the skybox shader
  try {
    skyboxShader = Engine::loadShader("skybox/skyboxVertexShader.glsl",
                                      "skybox/skyboxFragmentShader.glsl");
  } catch (const std::exception &e) {
    std::cerr << "Error loading skybox shaders: " << e.what() << std::endl;
    skyboxShader = nullptr; // Ensure shader is nullptr if loading fails
  }
}

void setupShadowMapping() {
  // Create shadow map framebuffer and texture
  glGenFramebuffers(1, &depthMapFBO);
  glGenTextures(1, &depthMap);

  glBindTexture(GL_TEXTURE_2D, depthMap);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT24, SHADOW_WIDTH,
               SHADOW_HEIGHT, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);

  // Use LINEAR filtering for smoother PCF sampling
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // Set texture wrap mode to clamp to border
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_BORDER);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_BORDER);

  // Set border color to white (no shadow outside shadow map)
  float borderColor[] = {1.0f, 1.0f, 1.0f, 1.0f};
  glTexParameterfv(GL_TEXTURE_2D, GL_TEXTURE_BORDER_COLOR, borderColor);

  // Attach depth texture to framebuffer
  glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D,
                         depthMap, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  // Check if framebuffer is complete
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cout << "ERROR: Shadow framebuffer is not complete!" << std::endl;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  std::cout << "Shadow mapping initialized with " << SHADOW_WIDTH << "x"
            << SHADOW_HEIGHT << " resolution" << std::endl;

  // Load shadow mapping shaders
  try {
    simpleDepthShader =
        Engine::loadShader("core/simpleDepthVertexShader.glsl",
                           "core/simpleDepthFragmentShader.glsl");
  } catch (const std::exception &e) {
    std::cerr << "Error loading depth shader: " << e.what() << std::endl;
  }
}

void setupPointShadowMapping() {
  // Generate depth cubemap array texture (6 layers per light)
  glGenTextures(1, &depthCubemap);
  glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, depthCubemap);

  const int layers = 6 * MAX_LIGHTS; // support up to MAX_LIGHTS point lights
  glTexImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, GL_DEPTH_COMPONENT,
               SHADOW_WIDTH_POINT, SHADOW_HEIGHT_POINT, layers, 0,
               GL_DEPTH_COMPONENT, GL_FLOAT, nullptr);

  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_S,
                  GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_T,
                  GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP_ARRAY, GL_TEXTURE_WRAP_R,
                  GL_CLAMP_TO_EDGE);

  // Create framebuffer for point shadows (layered rendering via geometry shader
  // gl_Layer)
  glGenFramebuffers(1, &depthMapFBO_point);
  glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO_point);
  glFramebufferTexture(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, depthCubemap, 0);
  glDrawBuffer(GL_NONE);
  glReadBuffer(GL_NONE);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cout << "ERROR: Point shadow framebuffer is not complete!"
              << std::endl;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  std::cout << "Point shadow mapping initialized with " << SHADOW_WIDTH_POINT
            << "x" << SHADOW_HEIGHT_POINT
            << " cubemap-array resolution for up to " << MAX_LIGHTS
            << " point lights" << std::endl;

  // Load point shadow shaders
  try {
    pointShadowShader =
        Engine::loadShader("core/pointShadowVertexShader.glsl",
                           "core/pointShadowFragmentShader.glsl",
                           "core/pointShadowGeometryShader.glsl");
  } catch (const std::exception &e) {
    std::cerr << "Error loading point shadow shader: " << e.what() << std::endl;
  }
}

// Convert HDR equirectangular map to cubemap
bool loadHDRSkybox(const std::string &hdrPath) {
  // Load HDR image (force 3 channels for RGB)
  stbi_set_flip_vertically_on_load(true);
  int width, height, nrComponents;
  float *data = stbi_loadf(hdrPath.c_str(), &width, &height, &nrComponents, 3);

  if (!data) {
    std::cerr << "Failed to load HDR texture: " << hdrPath << std::endl;
    stbi_set_flip_vertically_on_load(false);
    return false;
  }

  std::cout << "Loaded HDR image: " << width << "x" << height << " with "
            << nrComponents << " components" << std::endl;

  // Create HDR texture with floating point format
  unsigned int hdrTexture;
  glGenTextures(1, &hdrTexture);
  glBindTexture(GL_TEXTURE_2D, hdrTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB32F, width, height, 0, GL_RGB, GL_FLOAT,
               data);

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  stbi_image_free(data);
  stbi_set_flip_vertically_on_load(false);

  // Create cubemap with HDR format
  glGenTextures(1, &cubemapTexture);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
  for (unsigned int i = 0; i < 6; ++i) {
    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, 0, GL_RGB32F, 512, 512, 0,
                 GL_RGB, GL_FLOAT, nullptr);
  }
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MIN_FILTER,
                  GL_LINEAR_MIPMAP_LINEAR);
  glTexParameteri(GL_TEXTURE_CUBE_MAP, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

  // Create projection matrices for each cubemap face
  glm::mat4 captureProjection =
      glm::perspective(glm::radians(90.0f), 1.0f, 0.1f, 10.0f);
  glm::mat4 captureViews[] = {
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(1.0f, 0.0f, 0.0f),
                  glm::vec3(0.0f, -1.0f, 0.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(-1.0f, 0.0f, 0.0f),
                  glm::vec3(0.0f, -1.0f, 0.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f),
                  glm::vec3(0.0f, 0.0f, 1.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, -1.0f, 0.0f),
                  glm::vec3(0.0f, 0.0f, -1.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, 1.0f),
                  glm::vec3(0.0f, -1.0f, 0.0f)),
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 0.0f, -1.0f),
                  glm::vec3(0.0f, -1.0f, 0.0f))};

  // Create equirectangular to cubemap shader
  const char *vertexShaderSource = R"(
        #version 330 core
        layout (location = 0) in vec3 aPos;

        out vec3 localPos;

        uniform mat4 projection;
        uniform mat4 view;

        void main()
        {
            localPos = aPos;
            gl_Position = projection * view * vec4(localPos, 1.0);
        }
    )";

  const char *fragmentShaderSource = R"(
        #version 330 core
        out vec4 FragColor;
        in vec3 localPos;

        uniform sampler2D equirectangularMap;

        const vec2 invAtan = vec2(0.1591, 0.3183);
        vec2 SampleSphericalMap(vec3 v)
        {
            vec2 uv = vec2(atan(v.z, v.x), asin(v.y));
            uv *= invAtan;
            uv += 0.5;
            return uv;
        }

        void main()
        {
            vec2 uv = SampleSphericalMap(normalize(localPos));
            vec3 color = texture(equirectangularMap, uv).rgb;

            FragColor = vec4(color, 1.0);
        }
    )";

  // Compile shaders with error checking
  GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
  glShaderSource(vertexShader, 1, &vertexShaderSource, nullptr);
  glCompileShader(vertexShader);

  GLint success;
  glGetShaderiv(vertexShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(vertexShader, 512, nullptr, infoLog);
    std::cerr << "HDR vertex shader compilation failed: " << infoLog
              << std::endl;
    glDeleteShader(vertexShader);
    glDeleteTextures(1, &hdrTexture);
    glDeleteTextures(1, &cubemapTexture);
    return false;
  }

  GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
  glShaderSource(fragmentShader, 1, &fragmentShaderSource, nullptr);
  glCompileShader(fragmentShader);

  glGetShaderiv(fragmentShader, GL_COMPILE_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetShaderInfoLog(fragmentShader, 512, nullptr, infoLog);
    std::cerr << "HDR fragment shader compilation failed: " << infoLog
              << std::endl;
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteTextures(1, &hdrTexture);
    glDeleteTextures(1, &cubemapTexture);
    return false;
  }

  GLuint shaderProgram = glCreateProgram();
  glAttachShader(shaderProgram, vertexShader);
  glAttachShader(shaderProgram, fragmentShader);
  glLinkProgram(shaderProgram);

  glGetProgramiv(shaderProgram, GL_LINK_STATUS, &success);
  if (!success) {
    char infoLog[512];
    glGetProgramInfoLog(shaderProgram, 512, nullptr, infoLog);
    std::cerr << "HDR shader program linking failed: " << infoLog << std::endl;
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteProgram(shaderProgram);
    glDeleteTextures(1, &hdrTexture);
    glDeleteTextures(1, &cubemapTexture);
    return false;
  }

  // Create framebuffer
  unsigned int captureFBO;
  unsigned int captureRBO;
  glGenFramebuffers(1, &captureFBO);
  glGenRenderbuffers(1, &captureRBO);

  glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);
  glBindRenderbuffer(GL_RENDERBUFFER, captureRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, 512, 512);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, captureRBO);

  // Check framebuffer completeness
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "HDR framebuffer is not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &captureFBO);
    glDeleteRenderbuffers(1, &captureRBO);
    glDeleteShader(vertexShader);
    glDeleteShader(fragmentShader);
    glDeleteProgram(shaderProgram);
    glDeleteTextures(1, &hdrTexture);
    glDeleteTextures(1, &cubemapTexture);
    return false;
  }

  // Convert HDR equirectangular environment map to cubemap
  glUseProgram(shaderProgram);
  glUniform1i(glGetUniformLocation(shaderProgram, "equirectangularMap"), 0);
  glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "projection"), 1,
                     GL_FALSE, glm::value_ptr(captureProjection));

  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, hdrTexture);

  glViewport(0, 0, 512, 512); // Set viewport to cubemap face size
  glBindFramebuffer(GL_FRAMEBUFFER, captureFBO);

  // Render to each cubemap face
  for (unsigned int i = 0; i < 6; ++i) {
    glUniformMatrix4fv(glGetUniformLocation(shaderProgram, "view"), 1, GL_FALSE,
                       glm::value_ptr(captureViews[i]));
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                           GL_TEXTURE_CUBE_MAP_POSITIVE_X + i, cubemapTexture,
                           0);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Render cube
    glBindVertexArray(skyboxVAO);
    glDrawArrays(GL_TRIANGLES, 0, 36);
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);

  // Generate mipmaps for the cubemap
  glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
  glGenerateMipmap(GL_TEXTURE_CUBE_MAP);

  // Cleanup
  glDeleteTextures(1, &hdrTexture);
  glDeleteFramebuffers(1, &captureFBO);
  glDeleteRenderbuffers(1, &captureRBO);
  glDeleteShader(vertexShader);
  glDeleteShader(fragmentShader);
  glDeleteProgram(shaderProgram);

  // Reset viewport
  int viewport[4];
  glGetIntegerv(GL_VIEWPORT, viewport);
  glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);

  std::cout << "HDR skybox loaded and converted successfully: " << hdrPath
            << std::endl;
  return true;
}

// Helper function to create a default colored cubemap when textures can't be
// loaded
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
        faceData[idx + 1] =
            static_cast<GLubyte>(255 * intensity * colors[face].g);
        faceData[idx + 2] =
            static_cast<GLubyte>(255 * intensity * colors[face].b);
      }
    }

    glTexImage2D(GL_TEXTURE_CUBE_MAP_POSITIVE_X + face, 0, GL_RGB, size, size,
                 0, GL_RGB, GL_UNSIGNED_BYTE, faceData.data());
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
  } catch (const std::exception &e) {
    std::cerr << "Error loading skybox shaders: " << e.what() << std::endl;
    return;
  }

  // Setup skybox VAO
  setupSkyboxVAO(skyboxVAO, skyboxVBO);

  // Load cubemap textures with multiple path options
  const std::vector<std::string> searchPaths = {"./assets/textures/skybox/",
                                                "./skybox/", "./assets/skybox/",
                                                "./textures/skybox/"};

  std::vector<std::string> faceNames = {"right.jpg",  "left.jpg",  "top.jpg",
                                        "bottom.jpg", "front.jpg", "back.jpg"};

  bool texturesLoaded = false;
  std::vector<std::string> faces;

  // Try different paths until we find the textures
  for (const auto &basePath : searchPaths) {
    faces.clear();
    for (const auto &faceName : faceNames) {
      faces.push_back(basePath + faceName);
    }

    // Check if all files exist before trying to load
    bool allFilesExist = true;
    for (const auto &face : faces) {
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
      } catch (const std::exception &e) {
        std::cerr << "Failed to load skybox textures from " << basePath << ": "
                  << e.what() << std::endl;
      }
    }
  }

  if (!texturesLoaded) {
    std::cerr << "Failed to load skybox textures from any path" << std::endl;
    // Create a default colored cubemap as fallback
    createDefaultCubemap();
  }
}

void renderSkybox(const glm::mat4 &projection, const glm::mat4 &view,
                  Engine::Shader *mainShader) {
  // Ensure shader and texture exist
  if (!skyboxShader || !cubemapTexture || cubemapTexture == 0) {
    if (mainShader) {
      mainShader->use();
      mainShader->setInt("skybox", 7);
      mainShader->setFloat("skyboxIntensity", ambientStrengthFromSkybox);
    }
    return;
  }

  // Save and change depth func for skybox
  GLint previousDepthFunc;
  glGetIntegerv(GL_DEPTH_FUNC, &previousDepthFunc);
  glDepthFunc(GL_LEQUAL);

  // Draw skybox
  skyboxShader->use();
  glm::mat4 skyView = glm::mat4(glm::mat3(view));
  skyboxShader->setMat4("projection", projection);
  skyboxShader->setMat4("view", skyView);
  skyboxShader->setBool("hdrEnabled", preferences.hdrSettings.enabled);
  skyboxShader->setBool("isHDRSkybox", skyboxConfig.type == GUI::SKYBOX_HDR);
  skyboxShader->setFloat("skyboxExposure", preferences.skyboxExposure);
  glBindVertexArray(skyboxVAO);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
  glDrawArrays(GL_TRIANGLES, 0, 36);

  // Restore depth func
  glDepthFunc(previousDepthFunc);

  // Bind skybox texture for main shader to unit 7
  if (mainShader) {
    mainShader->use();
    glActiveTexture(GL_TEXTURE7);
    glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
    mainShader->setInt("skybox", 7);
    mainShader->setFloat("skyboxIntensity", ambientStrengthFromSkybox);
  }

  // Reset state
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
}

void bindSkyboxUniforms(Engine::Shader *shader) {
  shader->setFloat("skyboxIntensity", ambientStrengthFromSkybox);
  shader->setInt("skybox", 7);
  glActiveTexture(GL_TEXTURE7);
  glBindTexture(GL_TEXTURE_CUBE_MAP, cubemapTexture);
}

glm::mat4 calculateLightSpaceMatrix() {
  glm::vec3 sceneMin = glm::vec3(1e6f);
  glm::vec3 sceneMax = glm::vec3(-1e6f);

  // Include camera position in the shadow calculation to ensure visible area is
  // covered
  sceneMin = glm::min(sceneMin, camera.Position - glm::vec3(5.0f));
  sceneMax = glm::max(sceneMax, camera.Position + glm::vec3(5.0f));

  // Calculate actual scene bounds from all models
  for (const auto &model : currentScene.models) {
    glm::vec3 modelMin =
        model.position - glm::vec3(model.scale * model.boundingSphereRadius);
    glm::vec3 modelMax =
        model.position + glm::vec3(model.scale * model.boundingSphereRadius);

    sceneMin = glm::min(sceneMin, modelMin);
    sceneMax = glm::max(sceneMax, modelMax);
  }

  // If no models, use camera-centered bounds
  if (currentScene.models.empty()) {
    sceneMin = camera.Position - glm::vec3(10.0f);
    sceneMax = camera.Position + glm::vec3(10.0f);
  }

  // Calculate scene properties
  glm::vec3 sceneCenter = (sceneMin + sceneMax) * 0.5f;
  glm::vec3 sceneSize = sceneMax - sceneMin;
  float sceneRadius = glm::length(sceneSize) * 0.5f;

  // Ensure minimum size
  sceneRadius = std::max(sceneRadius, 5.0f);

  // Position light from sun direction
  glm::vec3 lightDir = glm::normalize(sun.direction);

  // Place light far enough to cover the entire scene
  float lightDistance = sceneRadius * 2.5f;
  glm::vec3 lightPos = sceneCenter - lightDir * lightDistance;

  // Create orthographic projection with proper bounds
  float orthoSize = sceneRadius * 1.5f; // More generous padding
  glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, // left, right
                                         -orthoSize, orthoSize, // bottom, top
                                         0.1f, lightDistance * 2.0f // near, far
  );

  // Create light view matrix
  glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
  // Ensure up vector is not parallel to light direction
  if (abs(glm::dot(lightDir, up)) > 0.99f) {
    up = glm::vec3(1.0f, 0.0f, 0.0f);
  }

  glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, up);

  return lightProjection * lightView;
}

void savePreferences() {
  json j;

  // UI preferences
  j["ui"]["darkTheme"] = preferences.isDarkTheme;
  j["ui"]["showFPS"] = preferences.showFPS;
  j["ui"]["show3DCursor"] = preferences.show3DCursor;
  j["ui"]["enableSpawnAnimation"] = preferences.enableSpawnAnimation;

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

  // Orbit center visualization settings
  j["camera"]["showOrbitCenter"] = preferences.showOrbitCenter;
  j["camera"]["alwaysShowOrbitCenter"] = preferences.alwaysShowOrbitCenter;
  j["camera"]["orbitCenterColor"] = {
      preferences.orbitCenterColor.r,
      preferences.orbitCenterColor.g,
      preferences.orbitCenterColor.b,
      preferences.orbitCenterColor.a
  };
  j["camera"]["orbitCenterSphereRadius"] = preferences.orbitCenterSphereRadius;
  j["camera"]["autoConvergence"] = preferences.autoConvergence;
  j["camera"]["convergenceDistanceFactor"] =
      preferences.convergenceDistanceFactor;
  j["camera"]["convergenceSmoothingSpeed"] =
      preferences.convergenceSmoothingSpeed;
  j["camera"]["enableConvergenceCap"] = preferences.enableConvergenceCap;
  j["camera"]["convergenceCapMin"] = preferences.convergenceCapMin;
  j["camera"]["convergenceCapMax"] = preferences.convergenceCapMax;
  j["camera"]["flipEyes"] = preferences.flipEyes;

  // SpaceMouse settings
  j["spacemouse"]["enabled"] = preferences.spaceMouseEnabled;
  j["spacemouse"]["deadzone"] = preferences.spaceMouseDeadzone;
  j["spacemouse"]["translationSensitivity"] =
      preferences.spaceMouseTranslationSensitivity;
  j["spacemouse"]["rotationSensitivity"] =
      preferences.spaceMouseRotationSensitivity;
  j["spacemouse"]["navigationMode"] =
      static_cast<int>(preferences.spaceMouseNavigationMode);
  j["spacemouse"]["anchorMode"] =
      static_cast<int>(preferences.spaceMouseAnchorMode);
  j["spacemouse"]["centerCursor"] = preferences.spaceMouseCenterCursor;

  // Cursor settings
  j["cursor"]["currentPreset"] = preferences.currentPresetName;

  j["skybox"]["type"] = skyboxConfig.type;
  j["skybox"]["solidColor"] = {skyboxConfig.solidColor.r,
                               skyboxConfig.solidColor.g,
                               skyboxConfig.solidColor.b};
  j["skybox"]["gradientTop"] = {skyboxConfig.gradientTopColor.r,
                                skyboxConfig.gradientTopColor.g,
                                skyboxConfig.gradientTopColor.b};
  j["skybox"]["gradientBottom"] = {skyboxConfig.gradientBottomColor.r,
                                   skyboxConfig.gradientBottomColor.g,
                                   skyboxConfig.gradientBottomColor.b};
  j["skybox"]["selectedCubemap"] = skyboxConfig.selectedCubemap;
  j["skybox"]["hdrPath"] = skyboxConfig.hdrPath;
  j["skybox"]["exposure"] = preferences.skyboxExposure;

  // Startup scene settings
  j["startup"]["loadScene"] = preferences.loadStartupScene;
  j["startup"]["scenePath"] = preferences.startupScenePath;

  // Scene loading behavior
  j["startup"]["sceneLoadingBehavior"] = static_cast<int>(preferences.sceneLoadingBehavior);

  // Save lighting settings
  j["lighting"]["mode"] = static_cast<int>(preferences.lightingMode);
  j["lighting"]["enableShadows"] = preferences.enableShadows;

  // Save HDR settings
  j["hdr"]["enabled"] = preferences.hdrSettings.enabled;
  j["hdr"]["exposure"] = preferences.hdrSettings.exposure;
  j["hdr"]["bloomThreshold"] = preferences.hdrSettings.bloomThreshold;
  j["hdr"]["bloomIntensity"] = preferences.hdrSettings.bloomIntensity;
  j["hdr"]["toneMapOperator"] = preferences.hdrSettings.toneMapOperator;
  j["hdr"]["enableBloom"] = preferences.hdrSettings.enableBloom;

  // Save shadow settings
  j["shadows"]["pcfKernelSize"] = preferences.shadowSettings.pcfKernelSize;
  j["shadows"]["enablePCSS"] = preferences.shadowSettings.enablePCSS;
  j["shadows"]["lightSize"] = preferences.shadowSettings.lightSize;
  j["shadows"]["shadowSoftness"] = preferences.shadowSettings.shadowSoftness;
  j["shadows"]["enableCascades"] = preferences.shadowSettings.enableCascades;
  j["shadows"]["numCascades"] = preferences.shadowSettings.numCascades;
  j["shadows"]["cascadeSplitLambda"] =
      preferences.shadowSettings.cascadeSplitLambda;
  j["shadows"]["enableIndirectLighting"] =
      preferences.shadowSettings.enableIndirectLighting;

  // Save material settings
  j["materials"]["enablePBR"] = preferences.materialSettings.enablePBR;
  j["materials"]["enableAO"] = preferences.materialSettings.enableAO;
  j["materials"]["enableNormalMapping"] =
      preferences.materialSettings.enableNormalMapping;
  j["materials"]["enableParallaxMapping"] =
      preferences.materialSettings.enableParallaxMapping;
  j["materials"]["normalScale"] = preferences.materialSettings.normalScale;
  j["materials"]["heightScale"] = preferences.materialSettings.heightScale;
  j["materials"]["metallicFactor"] =
      preferences.materialSettings.metallicFactor;
  j["materials"]["roughnessFactor"] =
      preferences.materialSettings.roughnessFactor;

  // Save model import settings
  j["modelImport"]["flipUVs"] = preferences.modelImportSettings.flipUVs;
  j["modelImport"]["flipNormals"] = preferences.modelImportSettings.flipNormals;
  j["modelImport"]["generateNormals"] =
      preferences.modelImportSettings.generateNormals;
  j["modelImport"]["generateSmoothNormals"] =
      preferences.modelImportSettings.generateSmoothNormals;
  j["modelImport"]["calculateTangentSpace"] =
      preferences.modelImportSettings.calculateTangentSpace;
  j["modelImport"]["joinIdenticalVertices"] =
      preferences.modelImportSettings.joinIdenticalVertices;
  j["modelImport"]["sortByPrimitiveType"] =
      preferences.modelImportSettings.sortByPrimitiveType;
  j["modelImport"]["fixInfacingNormals"] =
      preferences.modelImportSettings.fixInfacingNormals;
  j["modelImport"]["removeRedundantMaterials"] =
      preferences.modelImportSettings.removeRedundantMaterials;
  j["modelImport"]["optimizeMeshes"] =
      preferences.modelImportSettings.optimizeMeshes;
  j["modelImport"]["pretransformVertices"] =
      preferences.modelImportSettings.pretransformVertices;
  j["modelImport"]["autoScaleLargeModels"] =
      preferences.modelImportSettings.autoScaleLargeModels;
  j["modelImport"]["maxModelRadius"] =
      preferences.modelImportSettings.maxModelRadius;

  // Save radiance settings
  j["radiance"]["enableRaytracing"] =
      preferences.radianceSettings.enableRaytracing;
  j["radiance"]["maxBounces"] = preferences.radianceSettings.maxBounces;
  j["radiance"]["samplesPerPixel"] =
      preferences.radianceSettings.samplesPerPixel;
  j["radiance"]["rayMaxDistance"] = preferences.radianceSettings.rayMaxDistance;
  j["radiance"]["enableIndirectLighting"] =
      preferences.radianceSettings.enableIndirectLighting;
  j["radiance"]["enableEmissiveLighting"] =
      preferences.radianceSettings.enableEmissiveLighting;
  j["radiance"]["indirectIntensity"] =
      preferences.radianceSettings.indirectIntensity;
  j["radiance"]["skyIntensity"] = preferences.radianceSettings.skyIntensity;
  j["radiance"]["emissiveIntensity"] =
      preferences.radianceSettings.emissiveIntensity;
  j["radiance"]["materialRoughness"] =
      preferences.radianceSettings.materialRoughness;
  j["radiance"]["enableBVH"] = preferences.radianceSettings.enableBVH;
  j["radiance"]["showBVHDebug"] = preferences.radianceSettings.showBVHDebug;
  j["radiance"]["bvhDebugMaxDepth"] =
      preferences.radianceSettings.bvhDebugMaxDepth;
  j["radiance"]["bvhDebugRenderMode"] =
      preferences.radianceSettings.bvhDebugRenderMode;

  // Save irradiance cache settings
  j["radiance"]["enableIrradianceCache"] =
      preferences.radianceSettings.enableIrradianceCache;
  j["radiance"]["irradianceCacheDivisor"] =
      preferences.radianceSettings.irradianceCacheDivisor;
  j["radiance"]["irradianceCacheSamplesPerPixel"] =
      preferences.radianceSettings.irradianceCacheSamplesPerPixel;
  j["radiance"]["irradianceCacheMaxDistance"] =
      preferences.radianceSettings.irradianceCacheMaxDistance;
  j["radiance"]["irradianceCacheNormalThreshold"] =
      preferences.radianceSettings.irradianceCacheNormalThreshold;

  // Update preferences struct
  preferences.skyboxType = static_cast<int>(skyboxConfig.type);
  preferences.skyboxSolidColor = skyboxConfig.solidColor;
  preferences.skyboxGradientTop = skyboxConfig.gradientTopColor;
  preferences.skyboxGradientBottom = skyboxConfig.gradientBottomColor;
  preferences.selectedCubemap = skyboxConfig.selectedCubemap;
  preferences.skyboxHdrPath = skyboxConfig.hdrPath;

  // Save to file
  std::ofstream file("preferences.json");
  if (file.is_open()) {
    file << std::setw(4) << j << std::endl;
    file.close();
  } else {
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
  convergenceSmoothingSpeed = preferences.convergenceSmoothingSpeed;
  targetConvergence =
      preferences.convergence; // Initialize target to match current convergence
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

  // Apply orbit center visualization settings
  cursorManager.setShowOrbitCenter(preferences.showOrbitCenter);
  cursorManager.setAlwaysShowOrbitCenter(preferences.alwaysShowOrbitCenter);
  cursorManager.setOrbitCenterColor(preferences.orbitCenterColor);
  cursorManager.setOrbitCenterSphereRadius(preferences.orbitCenterSphereRadius);

  skyboxConfig.type = static_cast<GUI::SkyboxType>(preferences.skyboxType);
  skyboxConfig.solidColor = preferences.skyboxSolidColor;
  skyboxConfig.gradientTopColor = preferences.skyboxGradientTop;
  skyboxConfig.gradientBottomColor = preferences.skyboxGradientBottom;
  skyboxConfig.selectedCubemap = preferences.selectedCubemap;
  skyboxConfig.hdrPath = preferences.skyboxHdrPath;

  updateSkybox();

  // Load cursor preset
  currentPresetName = preferences.currentPresetName;
  if (!currentPresetName.empty()) {
    try {
      Engine::CursorPreset loadedPreset =
          Engine::CursorPresetManager::applyCursorPreset(currentPresetName);

      // Apply preset to cursor manager
      auto *sphereCursor = cursorManager.getSphereCursor();
      auto *fragmentCursor = cursorManager.getFragmentCursor();
      auto *planeCursor = cursorManager.getPlaneCursor();

      // Sphere cursor settings
      sphereCursor->setVisible(loadedPreset.showSphereCursor);
      sphereCursor->setScalingMode(
          static_cast<GUI::CursorScalingMode>(loadedPreset.sphereScalingMode));
      sphereCursor->setFixedRadius(loadedPreset.sphereFixedRadius);
      sphereCursor->setTransparency(loadedPreset.sphereTransparency);
      sphereCursor->setShowInnerSphere(loadedPreset.showInnerSphere);
      sphereCursor->setColor(loadedPreset.cursorColor);
      sphereCursor->setInnerSphereColor(loadedPreset.innerSphereColor);
      sphereCursor->setInnerSphereFactor(loadedPreset.innerSphereFactor);
      sphereCursor->setEdgeSoftness(loadedPreset.cursorEdgeSoftness);
      sphereCursor->setCenterTransparency(
          loadedPreset.cursorCenterTransparency);

      // Fragment cursor settings
      fragmentCursor->setVisible(loadedPreset.showFragmentCursor);
      fragmentCursor->setBaseInnerRadius(loadedPreset.fragmentBaseInnerRadius);

      // Plane cursor settings
      planeCursor->setVisible(loadedPreset.showPlaneCursor);
      planeCursor->setDiameter(loadedPreset.planeDiameter);
      planeCursor->setColor(loadedPreset.planeColor);
    } catch (const std::exception &e) {
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
        spherePreset.sphereScalingMode =
            static_cast<int>(GUI::CURSOR_CONSTRAINED_DYNAMIC);
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
        auto *sphereCursor = cursorManager.getSphereCursor();
        auto *fragmentCursor = cursorManager.getFragmentCursor();
        auto *planeCursor = cursorManager.getPlaneCursor();

        // Sphere cursor settings
        sphereCursor->setVisible(spherePreset.showSphereCursor);
        sphereCursor->setScalingMode(static_cast<GUI::CursorScalingMode>(
            spherePreset.sphereScalingMode));
        sphereCursor->setFixedRadius(spherePreset.sphereFixedRadius);
        sphereCursor->setTransparency(spherePreset.sphereTransparency);
        sphereCursor->setShowInnerSphere(spherePreset.showInnerSphere);
        sphereCursor->setColor(spherePreset.cursorColor);
        sphereCursor->setInnerSphereColor(spherePreset.innerSphereColor);
        sphereCursor->setInnerSphereFactor(spherePreset.innerSphereFactor);
        sphereCursor->setEdgeSoftness(spherePreset.cursorEdgeSoftness);
        sphereCursor->setCenterTransparency(
            spherePreset.cursorCenterTransparency);

        // Fragment cursor settings
        fragmentCursor->setVisible(spherePreset.showFragmentCursor);
        fragmentCursor->setBaseInnerRadius(
            spherePreset.fragmentBaseInnerRadius);

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
  preferences = GUI::ApplicationPreferences(); // This uses the default values
                                               // from the struct

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
      preferences.enableSpawnAnimation =
          j["ui"].value("enableSpawnAnimation", true);
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
      preferences.useNewStereoMethod =
          j["camera"].value("useNewStereoMethod", true);
      preferences.fov = j["camera"].value("fov", 45.0f);
      preferences.scrollMomentum = j["camera"].value("scrollMomentum", 0.5f);
      preferences.maxScrollVelocity =
          j["camera"].value("maxScrollVelocity", 3.0f);
      preferences.scrollDeceleration =
          j["camera"].value("scrollDeceleration", 10.0f);
      preferences.useSmoothScrolling =
          j["camera"].value("useSmoothScrolling", true);
      preferences.zoomToCursor = j["camera"].value("zoomToCursor", true);
      preferences.orbitAroundCursor =
          j["camera"].value("orbitAroundCursor", true);
      preferences.orbitFollowsCursor =
          j["camera"].value("orbitFollowsCursor", false);
      preferences.mouseSmoothingFactor =
          j["camera"].value("mouseSmoothingFactor", 1.0f);
      preferences.mouseSensitivity =
          j["camera"].value("mouseSensitivity", 0.17f);

      // Load orbit center visualization settings
      preferences.showOrbitCenter = j["camera"].value("showOrbitCenter", false);
      preferences.alwaysShowOrbitCenter = j["camera"].value("alwaysShowOrbitCenter", false);
      if (j["camera"].contains("orbitCenterColor") && j["camera"]["orbitCenterColor"].is_array() &&
          j["camera"]["orbitCenterColor"].size() >= 4) {
        preferences.orbitCenterColor = glm::vec4(
            j["camera"]["orbitCenterColor"][0].get<float>(),
            j["camera"]["orbitCenterColor"][1].get<float>(),
            j["camera"]["orbitCenterColor"][2].get<float>(),
            j["camera"]["orbitCenterColor"][3].get<float>()
        );
      }
      preferences.orbitCenterSphereRadius = j["camera"].value("orbitCenterSphereRadius", 0.2f);

      preferences.autoConvergence = j["camera"].value("autoConvergence", false);
      preferences.convergenceDistanceFactor =
          j["camera"].value("convergenceDistanceFactor", 1.0f);
      preferences.convergenceSmoothingSpeed =
          j["camera"].value("convergenceSmoothingSpeed", 5.0f);
      preferences.enableConvergenceCap =
          j["camera"].value("enableConvergenceCap", false);
      preferences.convergenceCapMin =
          j["camera"].value("convergenceCapMin", 0.5f);
      preferences.convergenceCapMax =
          j["camera"].value("convergenceCapMax", 40.0f);
      preferences.flipEyes = j["camera"].value("flipEyes", false);
    }

    // SpaceMouse settings
    if (j.contains("spacemouse")) {
      preferences.spaceMouseEnabled = j["spacemouse"].value("enabled", true);
      preferences.spaceMouseDeadzone =
          j["spacemouse"].value("deadzone", 0.025f);
      preferences.spaceMouseTranslationSensitivity =
          j["spacemouse"].value("translationSensitivity", 1.0f);
      preferences.spaceMouseRotationSensitivity =
          j["spacemouse"].value("rotationSensitivity", 1.0f);

      // Navigation mode with backward compatibility
      preferences.spaceMouseNavigationMode =
          static_cast<GUI::SpaceMouseNavigationMode>(j["spacemouse"].value(
              "navigationMode", static_cast<int>(GUI::SPACEMOUSE_NAV_CAD)));

      // Handle backward compatibility with old useCursorAnchor setting
      if (j["spacemouse"].contains("useCursorAnchor")) {
        bool oldUseCursorAnchor =
            j["spacemouse"].value("useCursorAnchor", false);
        preferences.spaceMouseAnchorMode =
            oldUseCursorAnchor ? GUI::SPACEMOUSE_ANCHOR_CONTINUOUS
                               : GUI::SPACEMOUSE_ANCHOR_DISABLED;
      } else {
        preferences.spaceMouseAnchorMode =
            static_cast<GUI::SpaceMouseAnchorMode>(j["spacemouse"].value(
                "anchorMode",
                static_cast<int>(GUI::SPACEMOUSE_ANCHOR_DISABLED)));
      }

      preferences.spaceMouseCenterCursor =
          j["spacemouse"].value("centerCursor", false);
    }

    if (j.contains("skybox")) {
      preferences.skyboxType =
          j["skybox"].value("type", static_cast<int>(GUI::SKYBOX_CUBEMAP));

      if (j["skybox"].contains("solidColor")) {
        auto &color = j["skybox"]["solidColor"];
        preferences.skyboxSolidColor =
            glm::vec3(color[0].get<float>(), color[1].get<float>(),
                      color[2].get<float>());
      }

      if (j["skybox"].contains("gradientTop")) {
        auto &color = j["skybox"]["gradientTop"];
        preferences.skyboxGradientTop =
            glm::vec3(color[0].get<float>(), color[1].get<float>(),
                      color[2].get<float>());
      }

      if (j["skybox"].contains("gradientBottom")) {
        auto &color = j["skybox"]["gradientBottom"];
        preferences.skyboxGradientBottom =
            glm::vec3(color[0].get<float>(), color[1].get<float>(),
                      color[2].get<float>());
      }

      preferences.selectedCubemap = j["skybox"].value("selectedCubemap", 0);
      preferences.skyboxHdrPath = j["skybox"].value("hdrPath", "");
      preferences.skyboxExposure = j["skybox"].value("exposure", 0.2f);
    }

    // Startup scene settings
    if (j.contains("startup")) {
      preferences.loadStartupScene = j["startup"].value("loadScene", false);
      preferences.startupScenePath = j["startup"].value("scenePath", "");
      preferences.sceneLoadingBehavior = static_cast<GUI::SceneLoadingBehavior>(
        j["startup"].value("sceneLoadingBehavior", static_cast<int>(GUI::SCENE_LOAD_ALWAYS_ASK)));
    }

    // Cursor settings
    if (j.contains("cursor")) {
      preferences.currentPresetName =
          j["cursor"].value("currentPreset", "Sphere");
    }

    // Lighting settings
    if (j.contains("lighting")) {
      preferences.lightingMode =
          static_cast<GUI::LightingMode>(j["lighting"].value(
              "mode", static_cast<int>(GUI::LIGHTING_SHADOW_MAPPING)));
      preferences.enableShadows = j["lighting"].value("enableShadows", true);

      // Update the global lighting mode
      currentLightingMode = preferences.lightingMode;
      enableShadows = preferences.enableShadows;
    }

    // HDR settings
    if (j.contains("hdr")) {
      preferences.hdrSettings.enabled = j["hdr"].value("enabled", true);
      preferences.hdrSettings.exposure = j["hdr"].value("exposure", 1.0f);
      preferences.hdrSettings.bloomThreshold =
          j["hdr"].value("bloomThreshold", 1.0f);
      preferences.hdrSettings.bloomIntensity =
          j["hdr"].value("bloomIntensity", 0.04f);
      preferences.hdrSettings.toneMapOperator =
          j["hdr"].value("toneMapOperator", 0);
      preferences.hdrSettings.enableBloom =
          j["hdr"].value("enableBloom", false);
    }

    // Shadow settings
    if (j.contains("shadows")) {
      preferences.shadowSettings.pcfKernelSize =
          j["shadows"].value("pcfKernelSize", 3);
      preferences.shadowSettings.enablePCSS =
          j["shadows"].value("enablePCSS", false);
      preferences.shadowSettings.lightSize =
          j["shadows"].value("lightSize", 0.1f);
      preferences.shadowSettings.shadowSoftness =
          j["shadows"].value("shadowSoftness", 1.0f);
      preferences.shadowSettings.enableCascades =
          j["shadows"].value("enableCascades", false);
      preferences.shadowSettings.numCascades =
          j["shadows"].value("numCascades", 4);
      preferences.shadowSettings.cascadeSplitLambda =
          j["shadows"].value("cascadeSplitLambda", 0.5f);
      preferences.shadowSettings.enableIndirectLighting =
          j["shadows"].value("enableIndirectLighting", false);
    }

    // Material settings
    if (j.contains("materials")) {
      preferences.materialSettings.enablePBR =
          j["materials"].value("enablePBR", true);
      preferences.materialSettings.enableAO =
          j["materials"].value("enableAO", true);
      preferences.materialSettings.enableNormalMapping =
          j["materials"].value("enableNormalMapping", true);
      preferences.materialSettings.enableParallaxMapping =
          j["materials"].value("enableParallaxMapping", false);
      preferences.materialSettings.normalScale =
          j["materials"].value("normalScale", 1.0f);
      preferences.materialSettings.heightScale =
          j["materials"].value("heightScale", 0.02f);
      preferences.materialSettings.metallicFactor =
          j["materials"].value("metallicFactor", 0.0f);
      preferences.materialSettings.roughnessFactor =
          j["materials"].value("roughnessFactor", 0.5f);
    }

    // VCT settings
    if (j.contains("vct")) {
      preferences.vctSettings.indirectSpecularLight =
          j["vct"].value("indirectSpecularLight", true);
      preferences.vctSettings.indirectDiffuseLight =
          j["vct"].value("indirectDiffuseLight", true);
      preferences.vctSettings.directLight = j["vct"].value("directLight", true);
      preferences.vctSettings.shadows = j["vct"].value("shadows", true);
      preferences.vctSettings.voxelSize =
          j["vct"].value("voxelSize", 1.0f / 64.0f);
      preferences.vctSettings.diffuseConeCount =
          j["vct"].value("diffuseConeCount", 6);
      preferences.vctSettings.tracingMaxDistance =
          j["vct"].value("tracingMaxDistance", 1.41421356237f);
      preferences.vctSettings.shadowSampleCount =
          j["vct"].value("shadowSampleCount", 18);
      preferences.vctSettings.shadowStepMultiplier =
          j["vct"].value("shadowStepMultiplier", 0.15f);
    }

    // Radiance settings
    if (j.contains("radiance")) {
      preferences.radianceSettings.enableRaytracing =
          j["radiance"].value("enableRaytracing", true);
      preferences.radianceSettings.maxBounces =
          j["radiance"].value("maxBounces", 2);
      preferences.radianceSettings.samplesPerPixel =
          j["radiance"].value("samplesPerPixel", 1);
      preferences.radianceSettings.rayMaxDistance =
          j["radiance"].value("rayMaxDistance", 50.0f);
      preferences.radianceSettings.enableIndirectLighting =
          j["radiance"].value("enableIndirectLighting", true);
      preferences.radianceSettings.enableEmissiveLighting =
          j["radiance"].value("enableEmissiveLighting", true);
      preferences.radianceSettings.indirectIntensity =
          j["radiance"].value("indirectIntensity", 0.3f);
      preferences.radianceSettings.skyIntensity =
          j["radiance"].value("skyIntensity", 1.0f);
      preferences.radianceSettings.emissiveIntensity =
          j["radiance"].value("emissiveIntensity", 1.0f);
      preferences.radianceSettings.materialRoughness =
          j["radiance"].value("materialRoughness", 0.5f);
      preferences.radianceSettings.enableBVH =
          j["radiance"].value("enableBVH", true);
      preferences.radianceSettings.showBVHDebug =
          j["radiance"].value("showBVHDebug", false);
      preferences.radianceSettings.bvhDebugMaxDepth =
          j["radiance"].value("bvhDebugMaxDepth", 3);
      preferences.radianceSettings.bvhDebugRenderMode =
          j["radiance"].value("bvhDebugRenderMode", 1);

      // Load irradiance cache settings
      preferences.radianceSettings.enableIrradianceCache =
          j["radiance"].value("enableIrradianceCache", false);
      preferences.radianceSettings.irradianceCacheDivisor =
          j["radiance"].value("irradianceCacheDivisor", 4);
      preferences.radianceSettings.irradianceCacheSamplesPerPixel =
          j["radiance"].value("irradianceCacheSamplesPerPixel", 4);
      preferences.radianceSettings.irradianceCacheMaxDistance =
          j["radiance"].value("irradianceCacheMaxDistance", 2.0f);
      preferences.radianceSettings.irradianceCacheNormalThreshold =
          j["radiance"].value("irradianceCacheNormalThreshold", 0.5f);
    }

    // Model import settings
    if (j.contains("modelImport")) {
      preferences.modelImportSettings.flipUVs =
          j["modelImport"].value("flipUVs", false);
      preferences.modelImportSettings.flipNormals =
          j["modelImport"].value("flipNormals", false);
      preferences.modelImportSettings.generateNormals =
          j["modelImport"].value("generateNormals", true);
      preferences.modelImportSettings.generateSmoothNormals =
          j["modelImport"].value("generateSmoothNormals", false);
      preferences.modelImportSettings.calculateTangentSpace =
          j["modelImport"].value("calculateTangentSpace", true);
      preferences.modelImportSettings.joinIdenticalVertices =
          j["modelImport"].value("joinIdenticalVertices", true);
      preferences.modelImportSettings.sortByPrimitiveType =
          j["modelImport"].value("sortByPrimitiveType", true);
      preferences.modelImportSettings.fixInfacingNormals =
          j["modelImport"].value("fixInfacingNormals", false);
      preferences.modelImportSettings.removeRedundantMaterials =
          j["modelImport"].value("removeRedundantMaterials", true);
      preferences.modelImportSettings.optimizeMeshes =
          j["modelImport"].value("optimizeMeshes", false);
      preferences.modelImportSettings.pretransformVertices =
          j["modelImport"].value("pretransformVertices", false);
      preferences.modelImportSettings.autoScaleLargeModels =
          j["modelImport"].value("autoScaleLargeModels", true);
      preferences.modelImportSettings.maxModelRadius =
          j["modelImport"].value("maxModelRadius", 5.0f);
    }

    // Apply loaded preferences
    applyPreferencesToProgram();
  } catch (const std::exception &e) {
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
  vctSettings.diffuseConeCount = 6; // Default: high quality with 6 cones (60° aperture)
  vctSettings.tracingMaxDistance = 1.41421356237; // Default maximum distance
  vctSettings.shadowSampleCount = 10;             // Default shadow samples
  vctSettings.shadowStepMultiplier = 0.15f;       // Default step multiplier

  // Load from preferences if available
  if (preferences.vctSettings.diffuseConeCount > 0) {
    vctSettings.diffuseConeCount = preferences.vctSettings.diffuseConeCount;
  } else {
    preferences.vctSettings.diffuseConeCount = vctSettings.diffuseConeCount;
  }

  if (preferences.vctSettings.tracingMaxDistance > 0) {
    vctSettings.tracingMaxDistance = preferences.vctSettings.tracingMaxDistance;
  } else {
    preferences.vctSettings.tracingMaxDistance = vctSettings.tracingMaxDistance;
  }

  if (preferences.vctSettings.shadowSampleCount > 0) {
    vctSettings.shadowSampleCount = preferences.vctSettings.shadowSampleCount;
  } else {
    preferences.vctSettings.shadowSampleCount = vctSettings.shadowSampleCount;
  }

  if (preferences.vctSettings.shadowStepMultiplier > 0) {
    vctSettings.shadowStepMultiplier =
        preferences.vctSettings.shadowStepMultiplier;
  } else {
    preferences.vctSettings.shadowStepMultiplier =
        vctSettings.shadowStepMultiplier;
  }
}

void InitializeDefaults() {
  // Set up default values
  preferences = GUI::ApplicationPreferences();

  // Set default radar settings
  preferences.radarEnabled = preferences.radarEnabled;
  preferences.radarPos = preferences.radarPos;
  preferences.radarScale = preferences.radarScale;
  preferences.radarShowScene = preferences.radarShowScene;

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
  preferences.separation = preferences.separation;
  preferences.convergence = preferences.convergence;
  preferences.autoConvergence = preferences.autoConvergence;
  preferences.convergenceDistanceFactor = preferences.convergenceDistanceFactor;
  convergenceSmoothingSpeed = preferences.convergenceSmoothingSpeed;
  targetConvergence =
      preferences.convergence; // Initialize target to match current convergence
  preferences.nearPlane = preferences.nearPlane;
  preferences.farPlane = preferences.farPlane;

  // Set up default cursor preset if needed
  if (Engine::CursorPresetManager::getPresetNames().empty()) {
    // Create and save the default Sphere preset
    Engine::CursorPreset spherePreset;
    spherePreset.name = "Sphere";
    spherePreset.showSphereCursor = true;
    spherePreset.showFragmentCursor = false;
    spherePreset.fragmentBaseInnerRadius = 0.004f;
    spherePreset.sphereScalingMode =
        static_cast<int>(GUI::CURSOR_CONSTRAINED_DYNAMIC);
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
          {"Yokohama", "skybox/Yokohama/",
           "Yokohama, Japan. View towards Intercontinental Yokohama Grand "
           "hotel."},
          {"Storforsen", "skybox/Storforsen/",
           "At the top of Storforsen. Taken with long exposure, resulting in "
           "smooth looking water flow."},
          {"Yokohama Night", "skybox/YokohamaNight/", "Yokohama at night."},
          {"Lycksele", "skybox/Lycksele/",
           "Lycksele. View of Ansia Camping, Lycksele."}};
    }

    Engine::CursorPresetManager::savePreset("Sphere", spherePreset);
    currentPresetName = "Sphere";

    // Apply preset to cursor manager
    auto *sphereCursor = cursorManager.getSphereCursor();
    auto *fragmentCursor = cursorManager.getFragmentCursor();
    auto *planeCursor = cursorManager.getPlaneCursor();

    // Sphere cursor settings
    sphereCursor->setVisible(spherePreset.showSphereCursor);
    sphereCursor->setScalingMode(
        static_cast<GUI::CursorScalingMode>(spherePreset.sphereScalingMode));
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

void PerspectiveProjection(GLfloat *frustum, GLfloat dir, GLfloat fovy,
                           GLfloat aspect, GLfloat znear, GLfloat zfar,
                           GLfloat eyesep, GLfloat focaldist) {
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
  glfwWindowHint(GLFW_STEREO, GLFW_TRUE); // Enable stereo hint

  // ---- Create GLFW Window ----
  GLFWwindow *window = glfwCreateWindow(windowWidth, windowHeight,
                                        "StereoVista", nullptr, nullptr);
  isStereoWindow = (window != nullptr);

  if (!isStereoWindow) {
    std::cout << "Failed to create stereo GLFW window, falling back to mono "
                 "rendering."
              << std::endl;
    glfwWindowHint(GLFW_STEREO, GLFW_FALSE);
    window = glfwCreateWindow(windowWidth, windowHeight,
                              "StereoVista (Monoviewer)", nullptr, nullptr);

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
  } else {
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
  glfwSetCursorEnterCallback(window, cursor_enter_callback);

  voxelizer = new Engine::Voxelizer(128);

  // ---- Initialize Shadow Mapping Shader ----
  try {
    shadowMappingShader =
        Engine::loadShader("core/shadowMappingVertexShader.glsl",
                           "core/shadowMappingFragmentShader.glsl");
  } catch (std::exception &e) {
    std::cout << "Error: Failed to load shadow mapping shader: " << e.what()
              << std::endl;
    glfwTerminate();
    return -1;
  }

  // ---- Initialize Voxel Cone Tracing Shader ----
  try {
    voxelConeTracingShader =
        Engine::loadShader("core/voxelConeTracingVertexShader.glsl",
                           "core/voxelConeTracingFragmentShader.glsl");
  } catch (std::exception &e) {
    std::cout << "Warning: Failed to load voxel cone tracing shader: "
              << e.what() << std::endl;
    voxelConeTracingShader = nullptr;
  }

  // ---- Initialize Zero Plane Shader ----
  try {
    zeroPlaneShader = Engine::loadShader("core/zeroPlaneVertexShader.glsl",
                                         "core/zeroPlaneFragmentShader.glsl");
  } catch (std::exception &e) {
    std::cout << "Warning: Failed to load zero plane shader: " << e.what()
              << std::endl;
    zeroPlaneShader = nullptr;
  }

  // ---- Initialize Radiance Shader ----
  try {
    radianceShader = Engine::loadShader("core/radianceVertexShader.glsl",
                                        "core/radianceFragmentShader.glsl");
  } catch (std::exception &e) {
    std::cout << "Warning: Failed to load radiance shader: " << e.what()
              << std::endl;
    radianceShader = nullptr;
  }

  // ---- Initialize Irradiance Cache Compute Shader ----
  try {
    irradianceCacheComputeShader =
        Engine::loadComputeShader("core/irradianceCacheCompute.glsl");
  } catch (std::exception &e) {
    std::cout << "Warning: Failed to load irradiance cache compute shader: "
              << e.what() << std::endl;
    irradianceCacheComputeShader = nullptr;
  }

  // ---- Initialize Irradiance Cache ----
  irradianceCache = new Engine::IrradianceCache();
  irradianceCache->initialize(glm::ivec3(32, 32, 32), 10000);
  irradianceCache->setSceneBounds(glm::vec3(-10, -10, -10),
                                  glm::vec3(10, 10, 10));

  // ---- Initialize Instanced Rendering Shader ----
  try {
    instancedShader = Engine::loadShader("core/instancedVertexShader.glsl",
                                         "core/instancedFragmentShader.glsl");
  } catch (std::exception &e) {
    std::cout << "Warning: Failed to load instanced rendering shader: "
              << e.what() << std::endl;
    instancedShader = nullptr;
  }

  // ---- Initialize Bloom Renderer ----
  bloomRenderer = new Engine::BloomRenderer();
  if (!bloomRenderer->initialize(windowWidth, windowHeight)) {
    std::cerr << "ERROR: Failed to initialize bloom renderer - HDR/Bloom "
                 "effects will be disabled"
              << std::endl;
    delete bloomRenderer;
    bloomRenderer = nullptr;
  } else {
    // Ensure proper size is set
    bloomRenderer->resize(windowWidth, windowHeight);

    // Validate the bloom system
    if (!bloomRenderer->validateBloomSystem()) {
      std::cerr << "ERROR: Bloom system validation failed - disabling bloom"
                << std::endl;
      delete bloomRenderer;
      bloomRenderer = nullptr;
    }
  }

  // ---- Load Initial Scene ----
  // Try to load office.scene (relative to executable), fallback to simple cube
  bool sceneLoaded = false;
  try {
    std::cout << "Attempting to load office.scene..." << std::endl;
    currentScene = Engine::loadScene("office.scene", camera);

    // Sync lights from scene to global variables
    pointLights = currentScene.pointLights;
    spotLights = currentScene.spotLights;

    // Start spawn animation for all loaded models
    for (auto &model : currentScene.models) {
      glm::vec3 targetScale = model.scale;
      if (preferences.enableSpawnAnimation) {
        model.startSpawnAnimation(targetScale, 1.1f);
      }
    }

    sceneLoaded = true;
    std::cout << "Successfully loaded office.scene" << std::endl;
  } catch (const std::exception &e) {
    std::cout << "Could not load office.scene: " << e.what() << std::endl;
    std::cout << "Creating fallback simple cube scene..." << std::endl;
  }

  // If scene loading failed, create a simple angled cube as fallback
  if (!sceneLoaded) {
    Engine::Model cube =
        Engine::createCube(glm::vec3(0.7f, 0.5f, 0.3f), 0.8f, 0.0f);
    cube.scale = glm::vec3(1.0f, 1.0f, 1.0f);
    cube.name = "Cube";
    cube.position = glm::vec3(0.0f, 0.0f, -1.5f);
    cube.rotation = glm::vec3(glm::radians(15.0f), glm::radians(25.0f), 0.0f);
    cube.scale = glm::vec3(1.0f);
    if (preferences.enableSpawnAnimation) {
      cube.startSpawnAnimation(glm::vec3(1.0f), 1.1f);
    }
    currentScene.models.push_back(cube);
    std::cout << "Fallback cube created" << std::endl;
  }

  currentModelIndex = 0;

  camera.centeringCompletedCallback = [&]() {
    if (orbitFollowsCursor) {
      camera.SetOrbitPointDirectly(capturedCursorPos);
      camera.StartOrbiting();
    } else {
      // For regular double-click centering, set cursor to screen center
      glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
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
  setupPointShadowMapping();
  setupSkyboxVAO(skyboxVAO, skyboxVBO);

  // ---- Calculate Largest Model Dimension ----
  float largestDimension = calculateLargestModelDimension();

  // ---- Initialize ImGui ----
  if (!InitializeGUI(window, isDarkTheme)) {
    std::cerr << "Failed to initialize GUI" << std::endl;
    return -1;
  }

  // Initialize GUI scaling based on current window size (only if window is
  // valid)
  if (Engine::Window::windowWidth > 0 && Engine::Window::windowHeight > 0) {
    UpdateGuiScale(Engine::Window::windowWidth, Engine::Window::windowHeight);
  }

  // Configure additional ImGui settings
  ImGuiViewportP *viewport = (ImGuiViewportP *)(void *)ImGui::GetMainViewport();
  ImGuiWindowFlags windowFlags = ImGuiWindowFlags_NoScrollbar |
                                 ImGuiWindowFlags_NoSavedSettings |
                                 ImGuiWindowFlags_NoNavFocus;
  ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

  vctSettings.indirectSpecularLight = true;
  vctSettings.indirectDiffuseLight = true;
  vctSettings.directLight = true;
  vctSettings.shadows = true;
  vctSettings.voxelSize = 1.0f / 64.0f;

  InitializeDefaults();

  loadPreferences();
  initializeVCTSettings();

  // ---- Load Shortcuts ----
  if (!shortcutManager.loadFromFile("shortcuts.json")) {
    std::cout << "No shortcuts.json found, using defaults" << std::endl;
    // ShortcutManager already has default profile from constructor
  }

  // ---- Load Startup Scene ----
  if (preferences.loadStartupScene && !preferences.startupScenePath.empty()) {
    try {
      std::cout << "Loading startup scene: " << preferences.startupScenePath
                << std::endl;
      currentScene = Engine::loadScene(preferences.startupScenePath, camera);

      // Sync lights from scene to global variables
      pointLights = currentScene.pointLights;
      spotLights = currentScene.spotLights;

      // Start spawn animation for all loaded models
      for (auto &model : currentScene.models) {
        glm::vec3 targetScale = model.scale;
        if (preferences.enableSpawnAnimation) {
          model.startSpawnAnimation(targetScale, 1.1f);
        }
      }
      currentModelIndex = currentScene.models.empty() ? -1 : 0;
      updateSpaceMouseBounds();
      std::cout << "Startup scene loaded successfully" << std::endl;
    } catch (const std::exception &e) {
      std::cerr << "Failed to load startup scene '"
                << preferences.startupScenePath << "': " << e.what()
                << std::endl;
    }
  }

  // ---- Initialize SpaceMouse Input ----
  // Use separate camera for SpaceMouse to prevent navlib override
  spaceMouseCamera =
      camera; // Initialize SpaceMouse camera with current camera state
  *spaceMouseCameraPtr = spaceMouseCamera;
  spaceMouseInput.SetCamera(spaceMouseCameraPtr);
  spaceMouseInitialized = spaceMouseInput.Initialize("StereoVista");
  if (spaceMouseInitialized) {
    std::cout << "SpaceMouse initialized successfully" << std::endl;

    // Apply SpaceMouse preferences
    spaceMouseInput.SetEnabled(preferences.spaceMouseEnabled);
    spaceMouseInput.SetNavigationMode(preferences.spaceMouseNavigationMode);
    spaceMouseInput.SetDeadzone(preferences.spaceMouseDeadzone);
    spaceMouseInput.SetSensitivity(preferences.spaceMouseTranslationSensitivity,
                                   preferences.spaceMouseRotationSensitivity);

    // Initialize SpaceMouse bounds with current scene content
    updateSpaceMouseBounds();
    updateSpaceMouseCursorAnchor();
    spaceMouseInput.SetWindowSize(windowWidth, windowHeight);
    spaceMouseInput.SetFieldOfView(camera.Zoom);
    spaceMouseInput.SetPerspectiveMode(true);

    // Set up callbacks
    spaceMouseInput.OnNavigationStarted = []() {
      spaceMouseActive = true;
      // Ensure Euler angles are up-to-date from quaternion before SpaceMouse
      // takes over
      camera.SynchronizeEulerFromQuaternion();
      // Sync current camera state to SpaceMouse camera when navigation starts
      spaceMouseCamera = camera;
      *spaceMouseCameraPtr = spaceMouseCamera;

      // Center cursor if enabled
      if (preferences.spaceMouseCenterCursor) {
        glfwSetCursorPos(Engine::Window::nativeWindow, windowWidth / 2.0,
                         windowHeight / 2.0);
      }

      std::cout << "SpaceMouse navigation started" << std::endl;
    };
    spaceMouseInput.OnNavigationEnded = []() {
      spaceMouseActive = false;
      // Sync SpaceMouse camera state back to main camera when navigation ends
      camera = *spaceMouseCameraPtr;
      // Instead of converting Euler->Quaternion, derive quaternion from the
      // Front/Up vectors This avoids Euler angle conversion issues
      glm::mat4 lookMatrix = glm::lookAt(glm::vec3(0), camera.Front, camera.Up);
      glm::mat4 rotationMatrix = glm::inverse(lookMatrix);
      camera.Orientation = glm::normalize(glm::quat_cast(rotationMatrix));
      std::cout << "SpaceMouse navigation ended" << std::endl;
    };
    spaceMouseInput.OnCommandExecuted = [](const std::string &commandId) {
      if (commandId == "Fit") {
        camera.SetState(currentScene.cameraState);
        std::cout << "SpaceMouse Fit: Camera reset to scene default"
                  << std::endl;
      }
    };
  } else {
    std::cout
        << "Failed to initialize SpaceMouse - continuing without 3D navigation"
        << std::endl;
  }

  // ---- OpenGL Settings ----
  glEnable(GL_DEPTH_TEST);
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glfwSwapInterval(1); // Enable vsync

  // ---- Main Loop ----
  while (!glfwWindowShouldClose(window)) {
    // ---- Per-frame Time Logic ----
    float currentFrame =
        static_cast<float>(glfwGetTime()); // Use static_cast for clarity
    deltaTime = currentFrame - lastFrame;
    lastFrame = currentFrame;

    // Fix: Clamp deltaTime to prevent huge jumps after blocking operations
    // (like file dialogs)
    if (deltaTime > 0.1f) {
      deltaTime = 0.1f;
    }

    // Prevent division by zero or negative delta time issues
    if (deltaTime <= 0.0f) {
      deltaTime = 0.0001f; // Assign a very small positive value if frame time
                           // is zero or negative
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
        // Don't synchronize quaternion during active SpaceMouse navigation
        // This prevents conflicts between quaternion and Euler-based input
      } else if (wasSpaceMouseActive && !spaceMouseActive) {
        // Just transitioned from SpaceMouse to normal - sync is handled in
        // OnNavigationEnded callback This ensures smooth transition without
        // camera jump
      } else if (!spaceMouseActive) {
        // Normal navigation mode - sync main camera changes to SpaceMouse
        // camera for next activation
        spaceMouseCamera = camera;
        *spaceMouseCameraPtr = spaceMouseCamera;
      }
    }

    // ---- Update Model Animations ----
    // Update spawn animations for all models
    for (auto &model : currentScene.models) {
      model.updateAnimation(deltaTime);
    }

    // ---- Update Brush Tool Settings ----
    // Synchronize global settings to brush tool instance
    brushTool.setBrushRadius(preferences.brushToolSettings.brushRadius);
    brushTool.setSelectedModel(
        preferences.brushToolSettings.selectedModelIndex);
    brushTool.setDensity(preferences.brushToolSettings.density);
    brushTool.setMinSpacing(preferences.brushToolSettings.minSpacing);
    // Note: Per-cluster settings (scale, rotation, alignment, color) are now
    // managed directly in cluster objects

    // Enable/disable brush tool
    if (preferences.brushToolSettings.enabled && !brushTool.isEnabled()) {
      brushTool.enable();
    } else if (!preferences.brushToolSettings.enabled &&
               brushTool.isEnabled()) {
      brushTool.disable();
    }

    // ---- Update Model Depth Movement Physics ----
    // Apply smooth scrolling physics to model depth movement when dragging
    if (isMovingModel && currentSelectedType == SelectedType::Model &&
        currentSelectedIndex != -1 && modelScrollVelocity != 0.0f) {
      // Calculate distance-based sensitivity for consistent feel
      float distanceToModel = glm::distance(
          camera.Position, currentScene.models[currentSelectedIndex].position);
      distanceToModel = glm::max(
          distanceToModel, 0.1f); // Prevent sensitivity from becoming zero

      // Apply movement with physics-based velocity
      float scrollSensitivity =
          1.0f; // Base sensitivity factor (increased from 0.1f)
      float adjustedVelocity =
          modelScrollVelocity * scrollSensitivity * distanceToModel;

      // Move model along camera's front direction
      glm::vec3 movement = camera.Front * adjustedVelocity * deltaTime;
      currentScene.models[currentSelectedIndex].position += movement;

      // Update grab point to move with the model
      if (hasModelGrabPoint) {
        modelGrabPoint += movement;

        // Update cursor position to track the grab point on screen
        glm::mat4 viewMatrix = camera.GetViewMatrix();
        glm::mat4 projMatrix = camera.GetProjectionMatrix(
            aspectRatio, preferences.nearPlane, preferences.farPlane);
        glm::mat4 viewProjMatrix = projMatrix * viewMatrix;

        // Project grab point to screen space
        glm::vec4 grabScreenPos =
            viewProjMatrix * glm::vec4(modelGrabPoint, 1.0f);
        if (grabScreenPos.w > 0.0f) {
          grabScreenPos /= grabScreenPos.w;

          // Convert NDC to screen coordinates
          float screenX = (grabScreenPos.x + 1.0f) * 0.5f * windowWidth;
          float screenY = (1.0f - grabScreenPos.y) * 0.5f * windowHeight;

          // Update hidden cursor position to track the grab point
          glfwSetCursorPos(Window::nativeWindow, screenX, screenY);

          // Update lastX/lastY to prevent jump on next mouse movement
          lastX = screenX;
          lastY = screenY;
        }
      }

      // Apply deceleration
      float deceleration = modelScrollDeceleration * deltaTime;
      if (abs(modelScrollVelocity) <= deceleration) {
        modelScrollVelocity = 0.0f;
      } else {
        modelScrollVelocity -= glm::sign(modelScrollVelocity) * deceleration;
      }
    }
    // Apply smooth scrolling physics to point light depth movement when
    // dragging
    else if (isMovingModel && currentSelectedType == SelectedType::PointLight &&
             currentSelectedIndex != -1 && modelScrollVelocity != 0.0f) {
      // Calculate distance-based sensitivity for consistent feel
      float distanceToLight = glm::distance(
          camera.Position, pointLights[currentSelectedIndex].position);
      distanceToLight = glm::max(
          distanceToLight, 0.1f); // Prevent sensitivity from becoming zero

      // Apply movement with physics-based velocity
      float scrollSensitivity = 1.0f; // Base sensitivity factor
      float adjustedVelocity =
          modelScrollVelocity * scrollSensitivity * distanceToLight;

      // Move point light along camera's front direction
      pointLights[currentSelectedIndex].position +=
          camera.Front * adjustedVelocity * deltaTime;

      // Update cursor position to track the light on screen
      glm::mat4 viewMatrix = camera.GetViewMatrix();
      glm::mat4 projMatrix = camera.GetProjectionMatrix(
          aspectRatio, preferences.nearPlane, preferences.farPlane);
      glm::mat4 viewProjMatrix = projMatrix * viewMatrix;

      glm::vec4 lightScreenPos =
          viewProjMatrix *
          glm::vec4(pointLights[currentSelectedIndex].position, 1.0f);
      if (lightScreenPos.w > 0.0f) {
        lightScreenPos /= lightScreenPos.w;

        float screenX = (lightScreenPos.x + 1.0f) * 0.5f * windowWidth;
        float screenY = (1.0f - lightScreenPos.y) * 0.5f * windowHeight;

        glfwSetCursorPos(Window::nativeWindow, screenX, screenY);
        lastX = screenX;
        lastY = screenY;
      }

      // Apply deceleration
      float deceleration = modelScrollDeceleration * deltaTime;
      if (abs(modelScrollVelocity) <= deceleration) {
        modelScrollVelocity = 0.0f;
      } else {
        modelScrollVelocity -= glm::sign(modelScrollVelocity) * deceleration;
      }
    }
    // Apply smooth scrolling physics to spot light depth movement when dragging
    else if (isMovingModel && currentSelectedType == SelectedType::SpotLight &&
             currentSelectedIndex != -1 && modelScrollVelocity != 0.0f) {
      // Calculate distance-based sensitivity for consistent feel
      float distanceToLight = glm::distance(
          camera.Position, spotLights[currentSelectedIndex].position);
      distanceToLight = glm::max(
          distanceToLight, 0.1f); // Prevent sensitivity from becoming zero

      // Apply movement with physics-based velocity
      float scrollSensitivity = 1.0f; // Base sensitivity factor
      float adjustedVelocity =
          modelScrollVelocity * scrollSensitivity * distanceToLight;

      // Move spot light along camera's front direction
      spotLights[currentSelectedIndex].position +=
          camera.Front * adjustedVelocity * deltaTime;

      // Update cursor position to track the light on screen
      glm::mat4 viewMatrix = camera.GetViewMatrix();
      glm::mat4 projMatrix = camera.GetProjectionMatrix(
          aspectRatio, preferences.nearPlane, preferences.farPlane);
      glm::mat4 viewProjMatrix = projMatrix * viewMatrix;

      glm::vec4 lightScreenPos =
          viewProjMatrix *
          glm::vec4(spotLights[currentSelectedIndex].position, 1.0f);
      if (lightScreenPos.w > 0.0f) {
        lightScreenPos /= lightScreenPos.w;

        float screenX = (lightScreenPos.x + 1.0f) * 0.5f * windowWidth;
        float screenY = (1.0f - lightScreenPos.y) * 0.5f * windowHeight;

        glfwSetCursorPos(Window::nativeWindow, screenX, screenY);
        lastX = screenX;
        lastY = screenY;
      }

      // Apply deceleration
      float deceleration = modelScrollDeceleration * deltaTime;
      if (abs(modelScrollVelocity) <= deceleration) {
        modelScrollVelocity = 0.0f;
      } else {
        modelScrollVelocity -= glm::sign(modelScrollVelocity) * deceleration;
      }
    }
    // Reset velocity when not moving any object
    else if (!isMovingModel) {
      modelScrollVelocity = 0.0f;
    }

    // --- Process Accumulated Mouse Input (Once Per Frame) ---
    // Check if mouse is captured and if there's any accumulated movement to
    // process Don't process mouse input when SpaceMouse is actively navigating
    if (isMouseCaptured && windowHasFocus && !ImGui::GetIO().WantCaptureMouse &&
        !spaceMouseActive) {

      // Use the *total* accumulated offset for this frame
      float totalXOffset = static_cast<float>(accumulatedXOffset);
      float totalYOffset = static_cast<float>(accumulatedYOffset);

      // --- Process Movement Based on Active Mode ---
      if (isMovingModel && currentSelectedType == SelectedType::Model &&
          currentSelectedIndex != -1) {
        // Only update model position if there's actual mouse movement
        // This prevents floating-point drift from projection/unprojection
        // cycles
        if (abs(totalXOffset) > 0.001f || abs(totalYOffset) > 0.001f) {
          // Use the GRAB POINT as the reference for screen-space calculations
          // This ensures the cursor stays exactly on the point where it grabbed
          // the model

          glm::mat4 view = camera.GetViewMatrix();
          glm::mat4 projection = camera.GetProjectionMatrix(
              aspectRatio, preferences.nearPlane, preferences.farPlane);
          glm::mat4 viewProj = projection * view;

          // Use grab point as reference, or model position if no grab point
          glm::vec3 referencePoint =
              hasModelGrabPoint
                  ? modelGrabPoint
                  : currentScene.models[currentSelectedIndex].position;

          // Calculate offset from grab point to model center (to preserve after
          // movement)
          glm::vec3 grabToModelOffset =
              currentScene.models[currentSelectedIndex].position -
              referencePoint;

          // Project the reference point (grab point) to screen space
          glm::vec4 refScreenPos = viewProj * glm::vec4(referencePoint, 1.0f);
          refScreenPos /= refScreenPos.w; // Perspective divide

          // Convert to screen coordinates
          float screenX = (refScreenPos.x + 1.0f) * 0.5f * windowWidth;
          float screenY =
              (1.0f - refScreenPos.y) * 0.5f * windowHeight; // Flip Y

          // Apply mouse offset in screen space
          screenX += totalXOffset;
          screenY -=
              totalYOffset; // Invert Y to match cursor movement direction

          // Convert back to NDC
          glm::vec2 newNDC = glm::vec2((screenX / windowWidth) * 2.0f - 1.0f,
                                       1.0f - (screenY / windowHeight) * 2.0f);

          // Unproject to get the new grab point position in world space
          glm::vec4 newGrabPointWorld =
              glm::vec4(newNDC.x, newNDC.y, refScreenPos.z, 1.0f);
          newGrabPointWorld = glm::inverse(viewProj) * newGrabPointWorld;
          newGrabPointWorld /= newGrabPointWorld.w;

          // Update grab point position
          if (hasModelGrabPoint) {
            modelGrabPoint = glm::vec3(newGrabPointWorld);
          }

          // Update model position by applying the same offset from the new grab
          // point
          currentScene.models[currentSelectedIndex].position =
              glm::vec3(newGrabPointWorld) + grabToModelOffset;
        }
      } else if (isMovingModel &&
                 currentSelectedType == SelectedType::PointLight &&
                 currentSelectedIndex != -1) {
        // Only update light position if there's actual mouse movement
        if (abs(totalXOffset) > 0.001f || abs(totalYOffset) > 0.001f) {
          // Point light dragging using same screen-to-world conversion as
          // models
          glm::vec3 lightPos = pointLights[currentSelectedIndex].position;
          float distanceToLight = glm::distance(camera.Position, lightPos);

          // Project light position to screen space
          glm::mat4 view = camera.GetViewMatrix();
          glm::mat4 projection = camera.GetProjectionMatrix(
              aspectRatio, preferences.nearPlane, preferences.farPlane);
          glm::mat4 viewProj = projection * view;

          // Convert world space position to screen space
          glm::vec4 lightScreenPos = viewProj * glm::vec4(lightPos, 1.0f);
          lightScreenPos /= lightScreenPos.w; // Perspective divide

          // Convert to NDC and add mouse offset
          glm::vec2 currentNDC = glm::vec2(lightScreenPos.x, lightScreenPos.y);
          glm::vec2 mouseOffsetNDC =
              glm::vec2(totalXOffset / (windowWidth * 0.5f),
                        totalYOffset / (windowHeight * 0.5f));
          glm::vec2 newNDC = currentNDC + mouseOffsetNDC;

          // Convert back to world space
          glm::vec4 newWorldPos =
              glm::vec4(newNDC.x, newNDC.y, lightScreenPos.z, 1.0f);
          newWorldPos = glm::inverse(viewProj) * newWorldPos;
          newWorldPos /= newWorldPos.w;

          // Update point light position
          pointLights[currentSelectedIndex].position = glm::vec3(newWorldPos);
        }
      } else if (isMovingModel &&
                 currentSelectedType == SelectedType::SpotLight &&
                 currentSelectedIndex != -1) {
        // Only update light position if there's actual mouse movement
        if (abs(totalXOffset) > 0.001f || abs(totalYOffset) > 0.001f) {
          // Spot light dragging using same screen-to-world conversion as models
          glm::vec3 lightPos = spotLights[currentSelectedIndex].position;
          float distanceToLight = glm::distance(camera.Position, lightPos);

          // Project light position to screen space
          glm::mat4 view = camera.GetViewMatrix();
          glm::mat4 projection = camera.GetProjectionMatrix(
              aspectRatio, preferences.nearPlane, preferences.farPlane);
          glm::mat4 viewProj = projection * view;

          // Convert world space position to screen space
          glm::vec4 lightScreenPos = viewProj * glm::vec4(lightPos, 1.0f);
          lightScreenPos /= lightScreenPos.w; // Perspective divide

          // Convert to NDC and add mouse offset
          glm::vec2 currentNDC = glm::vec2(lightScreenPos.x, lightScreenPos.y);
          glm::vec2 mouseOffsetNDC =
              glm::vec2(totalXOffset / (windowWidth * 0.5f),
                        totalYOffset / (windowHeight * 0.5f));
          glm::vec2 newNDC = currentNDC + mouseOffsetNDC;

          // Convert back to world space
          glm::vec4 newWorldPos =
              glm::vec4(newNDC.x, newNDC.y, lightScreenPos.z, 1.0f);
          newWorldPos = glm::inverse(viewProj) * newWorldPos;
          newWorldPos /= newWorldPos.w;

          // Update spot light position
          spotLights[currentSelectedIndex].position = glm::vec3(newWorldPos);
        }
      } else if ((camera.IsOrbiting || camera.IsPanning || rightMousePressed) &&
                 !camera.IsAnimating) {
        // Pass the accumulated offsets directly to the camera's processing
        // function. The Camera class handles applying its own
        // MouseSensitivity and determining whether to Orbit, Pan, or
        // Free-Look based on its internal state (IsOrbiting, IsPanning).
        camera.ProcessMouseMovement(totalXOffset, totalYOffset);
      }

      // --- Reset accumulators for the next frame ---
      accumulatedXOffset = 0.0;
      accumulatedYOffset = 0.0;
    } else {
      // Ensure accumulators are zero if not processing (capture stopped, lost
      // focus, etc.)
      accumulatedXOffset = 0.0;
      accumulatedYOffset = 0.0;
    }

    // ---- Handle Keyboard Input ----
    // Process continuous key presses (like WASD movement) after event
    // polling. Don't process keyboard movement when SpaceMouse is actively
    // navigating
    if (!spaceMouseActive) {
      Input::handleKeyInput(
          camera, deltaTime); // Make sure handleKeyInput uses deltaTime
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
        glViewport(0, 0, windowWidth,
                   windowHeight); // Reset viewport explicitly
      }
    }

    aspectRatio =
        static_cast<float>(windowWidth) / static_cast<float>(windowHeight);
    glm::mat4 projection = camera.GetProjectionMatrix(
        aspectRatio, preferences.nearPlane, preferences.farPlane);

    // Calculate stereo projections if needed
    glm::mat4 leftProjection = projection;
    glm::mat4 rightProjection = projection;
    glm::mat4 leftView = view;
    glm::mat4 rightView = view;

    if (isStereoWindow || preferences.radarEnabled) {
      GLfloat frustum[6];
      float effectiveSeparation = preferences.separation;

      PerspectiveProjection(frustum, -1.0f, camera.Zoom, aspectRatio,
                            preferences.nearPlane, preferences.farPlane,
                            effectiveSeparation, preferences.convergence);
      leftProjection = glm::frustum(frustum[0], frustum[1], frustum[2],
                                    frustum[3], frustum[4], frustum[5]);

      PerspectiveProjection(frustum, +1.0f, camera.Zoom, aspectRatio,
                            preferences.nearPlane, preferences.farPlane,
                            effectiveSeparation, preferences.convergence);
      rightProjection = glm::frustum(frustum[0], frustum[1], frustum[2],
                                     frustum[3], frustum[4], frustum[5]);

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
    Engine::Shader *activeShader =
        shadowMappingShader; // Default to shadow mapping shader
    if (currentLightingMode == GUI::LIGHTING_RADIANCE && radianceShader) {
      activeShader = radianceShader;
    } else if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING &&
               voxelConeTracingShader) {
      activeShader = voxelConeTracingShader;
    } else if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING &&
               shadowMappingShader) {
      activeShader = shadowMappingShader;
    }

    // ---- Rendering ----
    // Check if HDR/bloom is enabled
    bool hdrEnabled =
        preferences.hdrSettings.enabled && bloomRenderer != nullptr;
    bool bloomEnabled = preferences.hdrSettings.enableBloom && hdrEnabled;

    if (hdrEnabled) {
      // Begin HDR rendering (render to HDR framebuffer)
      bloomRenderer->beginBloomPass();

      // Validate that HDR framebuffer is properly bound
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR: HDR framebuffer not complete in main loop!"
                  << std::endl;
        hdrEnabled = false; // Fall back to non-HDR rendering
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
      }
    }

    if (hdrEnabled) {
      // Update bloom settings from preferences
      Engine::BloomSettings &bloomSettings = bloomRenderer->getSettings();
      bloomSettings.enabled = preferences.hdrSettings.enableBloom;
      bloomSettings.threshold = preferences.hdrSettings.bloomThreshold;
      bloomSettings.intensity = preferences.hdrSettings.bloomIntensity;
      bloomSettings.exposure = preferences.hdrSettings.exposure;
      bloomSettings.toneMapOperator = preferences.hdrSettings.toneMapOperator;

      if (isStereoWindow) {
        // Render and apply HDR/bloom separately for each eye
        // Swap eyes if flipEyes is enabled

        if (preferences.flipEyes) {
          // Flipped: render left projection to right buffer, right projection
          // to left buffer
          renderEye(GL_BACK_LEFT, leftProjection, leftView, activeShader,
                    viewport, windowFlags, window, false, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          bloomRenderer->applyBloom(0, bloomSettings, GL_BACK_RIGHT);

          renderEye(GL_BACK_RIGHT, rightProjection, rightView, activeShader,
                    viewport, windowFlags, window, false, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          bloomRenderer->applyBloom(0, bloomSettings, GL_BACK_LEFT);
        } else {
          // Normal: render left to left, right to right
          renderEye(GL_BACK_LEFT, leftProjection, leftView, activeShader,
                    viewport, windowFlags, window, false, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          bloomRenderer->applyBloom(0, bloomSettings, GL_BACK_LEFT);

          renderEye(GL_BACK_RIGHT, rightProjection, rightView, activeShader,
                    viewport, windowFlags, window, false, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          bloomRenderer->applyBloom(0, bloomSettings, GL_BACK_RIGHT);
        }
      } else {
        // Mono view
        renderEye(GL_BACK_LEFT, projection, view, activeShader, viewport,
                  windowFlags, window, false);
        bloomRenderer->applyBloom(0, bloomSettings, GL_BACK);
      }

      // Now render GUI on top of the composed HDR result
      if (showGui) {
        // Ensure we're rendering to the default framebuffer
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
        glViewport(0, 0, windowWidth, windowHeight);

        // Render GUI for the appropriate eye(s)
        if (isStereoWindow) {
          // For stereo, render GUI to both eyes
          glDrawBuffer(GL_BACK_LEFT);
          renderGUI(true, viewport, windowFlags, activeShader);

          glDrawBuffer(GL_BACK_RIGHT);
          renderGUI(true, viewport, windowFlags, activeShader);
        } else {
          // For mono, render GUI normally
          renderGUI(true, viewport, windowFlags, activeShader);
        }
      }
    } else {
      // Non-HDR rendering path
      if (isStereoWindow) {
        if (preferences.flipEyes) {
          // Flipped: swap left and right eye rendering
          renderEye(GL_BACK_LEFT, rightProjection, rightView, activeShader,
                    viewport, windowFlags, window, true, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          renderEye(GL_BACK_RIGHT, leftProjection, leftView, activeShader,
                    viewport, windowFlags, window, true, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
        } else {
          // Normal: render left eye to left buffer, right eye to right buffer
          renderEye(GL_BACK_LEFT, leftProjection, leftView, activeShader,
                    viewport, windowFlags, window, true, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          renderEye(GL_BACK_RIGHT, rightProjection, rightView, activeShader,
                    viewport, windowFlags, window, true, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
        }
      } else {
        // Render mono view to default buffer (cursor position will be
        // calculated here)
        renderEye(GL_BACK_LEFT, projection, view, activeShader, viewport,
                  windowFlags, window, true);
      }
    }

    // Update the cursor's captured position if available (after rendering)
    if (cursorManager.isCursorPositionValid() && !camera.IsOrbiting) {
      // Only update captured position when not orbiting to preserve the
      // original position
      capturedCursorPos = cursorManager.getCursorPosition();
    }

    if (preferences.radarEnabled) {
      // Swap left/right views for radar if flipEyes is enabled
      if (preferences.flipEyes && isStereoWindow) {
        DrawRadar(isStereoWindow, camera, preferences.convergence, view,
                  projection, rightView, rightProjection, leftView,
                  leftProjection, activeShader, preferences.radarShowScene,
                  preferences.radarScale, preferences.radarPos);
      } else {
        DrawRadar(isStereoWindow, camera, preferences.convergence, view,
                  projection, leftView, leftProjection, rightView,
                  rightProjection, activeShader, preferences.radarShowScene,
                  preferences.radarScale, preferences.radarPos);
      }
    }

    // ---- Swap Buffers ----
    glfwSwapBuffers(window);
  }

  // ---- Cleanup ----
  cleanup();

  // ---- Shutdown Async Loading System ----
  OctreePointCloudManager::shutdownAsyncSystem();

  return 0;
}

// ---- Initialization and Cleanup -----
#pragma region Initialization and Cleanup
void cleanup() {
  // Delete cursor manager resources
  cursorManager.cleanup();

  // Delete point cloud resources
  for (auto &pointCloud : currentScene.pointClouds) {
    glDeleteVertexArrays(1, &pointCloud.vao);
    glDeleteBuffers(1, &pointCloud.vbo);
  }

  // Delete triangle buffer resources
  cleanupTriangleBuffer();
  cleanupBVHBuffers();

  // Cleanup world-space irradiance cache
  if (irradianceCache) {
    delete irradianceCache;
    irradianceCache = nullptr;
  }

  // Cleanup BVH debug renderer
  bvhDebugRenderer.cleanup();

  // Delete skybox resources
  glDeleteVertexArrays(1, &skyboxVAO);
  glDeleteBuffers(1, &skyboxVBO);
  glDeleteTextures(1, &cubemapTexture);
  delete skyboxShader;

  // Cleanup bloom renderer
  if (bloomRenderer) {
    delete bloomRenderer;
    bloomRenderer = nullptr;
  }

  // Delete zero plane resources
  glDeleteVertexArrays(1, &zeroPlaneVAO);
  glDeleteBuffers(1, &zeroPlaneVBO);
  glDeleteBuffers(1, &zeroPlaneEBO);
  delete zeroPlaneShader;

  // Delete shadow mapping resources
  glDeleteFramebuffers(1, &depthMapFBO);
  glDeleteTextures(1, &depthMap);
  delete simpleDepthShader;

  // Cleanup point shadow resources
  glDeleteFramebuffers(1, &depthMapFBO_point);
  glDeleteTextures(1, &depthCubemap);
  delete pointShadowShader;
  delete radianceShader;
  delete shadowMappingShader;
  delete voxelConeTracingShader;

  // Clean up SpaceMouse input
  spaceMouseInput.Shutdown();

  // Clean up cursor preview before GUI shutdown
  cursorPreview3D.cleanup();

  // Clean up GUI before destroying window (ImGui needs valid OpenGL context)
  CleanupGUI();

  // Destroy window and terminate GLFW
  glfwDestroyWindow(Window::nativeWindow);
  glfwTerminate();
}

float calculateLargestModelDimension() {
  if (currentScene.models.empty())
    return 1.0f;

  glm::vec3 minBounds(std::numeric_limits<float>::max());
  glm::vec3 maxBounds(std::numeric_limits<float>::lowest());

  // Loop through all meshes in the first model
  for (const auto &mesh : currentScene.models[0].getMeshes()) {
    for (const auto &vertex : mesh.vertices) {
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
void renderEye(GLenum drawBuffer, const glm::mat4 &projection,
               const glm::mat4 &view, Engine::Shader *shader,
               ImGuiViewportP *viewport, ImGuiWindowFlags windowFlags,
               GLFWwindow *window, bool renderGUIFlag, bool isStereo,
               const glm::mat4 *leftProjection, const glm::mat4 *leftView,
               const glm::mat4 *rightProjection, const glm::mat4 *rightView) {
  // CRITICAL FIX: Calculate grid bounds ONCE and share between shaders
  // This prevents coordinate system mismatch between cache population and
  // lookup
  glm::vec3 sharedGridMin, sharedGridMax;
  glm::ivec3 sharedGridRes;
  bool cacheEnabled = irradianceCache && irradianceCache->isInitialized() &&
                      radianceSettings.enableIrradianceCache;

  if (cacheEnabled) {
    // Calculate grid bounds from BVH root
    if (bvhBuilt && !gpuBVHNodes.empty()) {
      const auto &rootNode = gpuBVHNodes[0];
      sharedGridMin = glm::vec3(rootNode.minX, rootNode.minY, rootNode.minZ);
      sharedGridMax = glm::vec3(rootNode.maxX, rootNode.maxY, rootNode.maxZ);
    } else {
      // Fallback: use default bounds
      sharedGridMin = glm::vec3(-10.0f, -10.0f, -10.0f);
      sharedGridMax = glm::vec3(10.0f, 10.0f, 10.0f);
    }

    // Add 5% padding to avoid edge cases
    glm::vec3 padding = (sharedGridMax - sharedGridMin) * 0.05f;
    sharedGridMin -= padding;
    sharedGridMax += padding;
    sharedGridRes = irradianceCache->getGridResolution();
  }

  // Set the draw buffer and clear color and depth buffers
  // Only set draw buffer if not using HDR (HDR uses MRT - Multiple Render
  // Targets)
  if (!preferences.hdrSettings.enabled || bloomRenderer == nullptr) {
    glDrawBuffer(drawBuffer);
  } else {
    // When HDR is enabled, ensure HDR framebuffer is bound before clearing
    // This prevents clearing the default framebuffer (which would erase the
    // other eye's image in stereo mode)
    Engine::BloomSettings &bloomSettings = bloomRenderer->getSettings();
    glBindFramebuffer(GL_FRAMEBUFFER, bloomSettings.hdrFBO);
  }
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

  // Reset OpenGL state
  glUseProgram(0);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindTexture(GL_TEXTURE_3D, 0);

  // Detect scene changes (model transforms) and mark voxelizer dirty.
  // This must happen before voxelizer->update() and independently of
  // lighting mode / BVH, so that voxelization refreshes whenever any
  // object is moved, rotated, or scaled -- matching BVH behavior.
  {
    static SceneState lastVoxelSceneState;
    if (lastVoxelSceneState.hasChanged(currentScene)) {
      if (voxelizer) {
        voxelizer->markDirty();
      }
      lastVoxelSceneState.update(currentScene);
    }
  }

  // 1. Update the voxel grid if voxel visualization is enabled or we're using
  // voxel cone tracing or shadow mapping with indirect lighting
  bool needsVoxelization =
      (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) ||
      voxelizer->showDebugVisualization ||
      (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING &&
       preferences.shadowSettings.enableIndirectLighting);
  if (needsVoxelization) {
    // Keep voxelizer lights in sync with the scene so voxelized
    // lighting matches the actual point lights (not just the default).
    voxelizer->setLights(pointLights);

    voxelizer->update(camera.Position, currentScene.models);
  }

  // 2. Shadow mapping pass (only if using shadow mapping AND shadows are
  // enabled)
  if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING && enableShadows) {
    // Temporarily disable wireframe mode for shadow mapping
    // Shadow maps need filled polygons, not wireframe lines
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glClear(GL_DEPTH_BUFFER_BIT);

    // Calculate light space matrix based on actual scene bounds
    lightSpaceMatrix = calculateLightSpaceMatrix();

    // Use depth shader for shadow map generation
    simpleDepthShader->use();
    simpleDepthShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);

    // Enable polygon offset to reduce peter panning - fine-tuned values
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.1f, 4.0f); // Reduced factor for tighter attachment

    // Render scene to depth buffer - disable culling to avoid issues with
    // complex geometry
    glDisable(GL_CULL_FACE);
    renderModels(simpleDepthShader);
    glEnable(GL_CULL_FACE);

    // Disable polygon offset
    glDisable(GL_POLYGON_OFFSET_FILL);

    // Restore wireframe mode if it was enabled
    glPolygonMode(GL_FRONT_AND_BACK, camera.wireframe ? GL_LINE : GL_FILL);
  }

  // 2.5. Point shadow mapping pass for all point lights
  if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING && enableShadows &&
      !pointLights.empty()) {
    // Temporarily disable wireframe mode for point shadow mapping
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glViewport(0, 0, SHADOW_WIDTH_POINT, SHADOW_HEIGHT_POINT);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO_point);
    glClear(GL_DEPTH_BUFFER_BIT);

    float near_plane = 0.01f;
    glm::mat4 shadowProj =
        glm::perspective(glm::radians(90.0f), 1.0f, near_plane, far_plane);

    if (pointShadowShader) {
      // Enable polygon offset to reduce peter panning
      glEnable(GL_POLYGON_OFFSET_FILL);
      glPolygonOffset(0.5f, 1.0f);

      for (int li = 0; li < pointLights.size() && li < MAX_LIGHTS; ++li) {
        // Skip generating shadow map for lights that don't cast shadows
        if (!pointLights[li].castShadows)
          continue;
        glm::vec3 lightPos = pointLights[li].position;

        std::vector<glm::mat4> shadowMatrices;
        shadowMatrices.push_back(
            shadowProj * glm::lookAt(lightPos,
                                     lightPos + glm::vec3(1.0f, 0.0f, 0.0f),
                                     glm::vec3(0.0f, -1.0f, 0.0f)));
        shadowMatrices.push_back(
            shadowProj * glm::lookAt(lightPos,
                                     lightPos + glm::vec3(-1.0f, 0.0f, 0.0f),
                                     glm::vec3(0.0f, -1.0f, 0.0f)));
        shadowMatrices.push_back(
            shadowProj * glm::lookAt(lightPos,
                                     lightPos + glm::vec3(0.0f, 1.0f, 0.0f),
                                     glm::vec3(0.0f, 0.0f, 1.0f)));
        shadowMatrices.push_back(
            shadowProj * glm::lookAt(lightPos,
                                     lightPos + glm::vec3(0.0f, -1.0f, 0.0f),
                                     glm::vec3(0.0f, 0.0f, -1.0f)));
        shadowMatrices.push_back(
            shadowProj * glm::lookAt(lightPos,
                                     lightPos + glm::vec3(0.0f, 0.0f, 1.0f),
                                     glm::vec3(0.0f, -1.0f, 0.0f)));
        shadowMatrices.push_back(
            shadowProj * glm::lookAt(lightPos,
                                     lightPos + glm::vec3(0.0f, 0.0f, -1.0f),
                                     glm::vec3(0.0f, -1.0f, 0.0f)));

        pointShadowShader->use();
        for (unsigned int i = 0; i < 6; ++i) {
          pointShadowShader->setMat4(
              "shadowMatrices[" + std::to_string(i) + "]", shadowMatrices[i]);
        }
        pointShadowShader->setVec3("lightPos", lightPos);
        pointShadowShader->setFloat("far_plane", far_plane);
        pointShadowShader->setInt("lightIndex", li);

        // Render scene to this light's 6 faces in array layers via GS
        // gl_Layer
        renderModels(pointShadowShader);
      }

      glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // Restore wireframe mode if it was enabled
    glPolygonMode(GL_FRONT_AND_BACK, camera.wireframe ? GL_LINE : GL_FILL);
  }

  // 3. Regular rendering pass
  // Bind the appropriate framebuffer for the main scene rendering
  if (preferences.hdrSettings.enabled && bloomRenderer != nullptr) {
    // Rebind HDR framebuffer (it may have been unbound during shadow mapping)
    // Don't call beginBloomPass() again as it would clear the buffer
    Engine::BloomSettings &bloomSettings = bloomRenderer->getSettings();
    glBindFramebuffer(GL_FRAMEBUFFER, bloomSettings.hdrFBO);

    // Validate HDR framebuffer is complete
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
      std::cerr << "ERROR: HDR framebuffer not complete in renderEye!"
                << std::endl;
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
    } else {
      // Ensure MRT is set up correctly
      GLuint attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
      glDrawBuffers(2, attachments);

      // Verify MRT setup
      GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
      if (status != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "ERROR: HDR framebuffer with MRT not complete!"
                  << std::endl;
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
      }
    }
  } else {
    // Bind default framebuffer for non-HDR rendering
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
  }
  glViewport(0, 0, windowWidth, windowHeight);

  shader->use();
  shader->setMat4("projection", projection);
  shader->setMat4("view", view);
  shader->setVec3("viewPos", camera.Position);

  // Set lighting mode uniforms - this is always needed
  shader->setInt("lightingMode", static_cast<int>(currentLightingMode));
  shader->setBool("enableShadows", enableShadows);

  // Set HDR settings uniforms
  shader->setBool("hdrSettings.enabled", preferences.hdrSettings.enabled);
  shader->setFloat("hdrSettings.exposure", preferences.hdrSettings.exposure);
  shader->setFloat("hdrSettings.bloomThreshold",
                   preferences.hdrSettings.bloomThreshold);
  shader->setFloat("hdrSettings.bloomIntensity",
                   preferences.hdrSettings.bloomIntensity);
  shader->setInt("hdrSettings.toneMapOperator",
                 preferences.hdrSettings.toneMapOperator);
  shader->setBool("hdrSettings.enableBloom",
                  preferences.hdrSettings.enableBloom);

  // Set shadow quality settings uniforms
  shader->setInt("shadowSettings.pcfKernelSize",
                 preferences.shadowSettings.pcfKernelSize);
  shader->setBool("shadowSettings.enablePCSS",
                  preferences.shadowSettings.enablePCSS);
  shader->setFloat("shadowSettings.lightSize",
                   preferences.shadowSettings.lightSize);
  shader->setFloat("shadowSettings.shadowSoftness",
                   preferences.shadowSettings.shadowSoftness);
  shader->setBool("shadowSettings.enableCascades",
                  preferences.shadowSettings.enableCascades);
  shader->setInt("shadowSettings.numCascades",
                 preferences.shadowSettings.numCascades);
  shader->setFloat("shadowSettings.cascadeSplitLambda",
                   preferences.shadowSettings.cascadeSplitLambda);

  // Set material enhancement uniforms (for enhanced material properties)
  shader->setBool("materialSettings.enablePBR",
                  preferences.materialSettings.enablePBR);
  shader->setBool("materialSettings.enableAO",
                  preferences.materialSettings.enableAO);
  shader->setBool("materialSettings.enableNormalMapping",
                  preferences.materialSettings.enableNormalMapping);
  shader->setBool("materialSettings.enableParallaxMapping",
                  preferences.materialSettings.enableParallaxMapping);
  shader->setFloat("materialSettings.normalScale",
                   preferences.materialSettings.normalScale);
  shader->setFloat("materialSettings.heightScale",
                   preferences.materialSettings.heightScale);
  shader->setFloat("materialSettings.metallicFactor",
                   preferences.materialSettings.metallicFactor);
  shader->setFloat("materialSettings.roughnessFactor",
                   preferences.materialSettings.roughnessFactor);

  // Set common light properties - these are needed for both modes
  shader->setVec3("sun.direction", sun.direction);
  shader->setVec3("sun.color", sun.color);
  shader->setFloat("sun.intensity", sun.intensity);
  shader->setBool("sun.enabled", sun.enabled);

  // Shadow mapping specific setup
  if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
    // Calculate light space matrix based on actual scene bounds (same as
    // depth pass)
    lightSpaceMatrix = calculateLightSpaceMatrix();

    shader->setMat4("lightSpaceMatrix", lightSpaceMatrix);

    // Bind shadow map if shadows are enabled
    if (enableShadows) {
      glActiveTexture(GL_TEXTURE4); // Using texture unit 4 for shadow map
      glBindTexture(GL_TEXTURE_2D, depthMap);
      shader->setInt("shadowMap", 4);

      // Bind point shadow cubemap array
      glActiveTexture(GL_TEXTURE6); // Use texture unit 6 for point shadow maps
      glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, depthCubemap);
      shader->setInt("pointShadowMaps", 6);
      shader->setFloat("far_plane", far_plane);
    }

    // Update point lights (for shadow mapping mode)
    updatePointLights();

    // Set point light uniforms
    for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
      std::string lightName = "lights[" + std::to_string(i) + "]";
      shader->setVec3(lightName + ".position", pointLights[i].position);
      shader->setVec3(lightName + ".color", pointLights[i].color);
      shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
      shader->setFloat(lightName + ".linear", pointLights[i].linear);
      shader->setFloat(lightName + ".quadratic", pointLights[i].quadratic);
      shader->setBool("lightsCastShadows[" + std::to_string(i) + "]",
                      pointLights[i].castShadows);
    }
    shader->setInt("numLights", std::min((int)pointLights.size(), MAX_LIGHTS));

    // Set spot light uniforms
    for (int i = 0; i < spotLights.size() && i < MAX_LIGHTS; i++) {
      std::string lightName = "spotLights[" + std::to_string(i) + "]";
      shader->setVec3(lightName + ".position", spotLights[i].position);
      shader->setVec3(lightName + ".direction", spotLights[i].direction);
      shader->setVec3(lightName + ".color", spotLights[i].color);
      shader->setFloat(lightName + ".intensity", spotLights[i].intensity);
      shader->setFloat(lightName + ".innerCutOff", spotLights[i].innerCutOff);
      shader->setFloat(lightName + ".outerCutOff", spotLights[i].outerCutOff);
    }
    shader->setInt("numSpotLights",
                   std::min((int)spotLights.size(), MAX_LIGHTS));

    // Add VCT uniforms if indirect lighting is enabled
    if (preferences.shadowSettings.enableIndirectLighting) {
      // Set voxel grid parameters -- must account for gridCenter so that
      // cone tracing samples match the voxelization coordinate mapping.
      float halfSize = voxelizer->getVoxelGridSize() * 0.5f;
      glm::vec3 gc = voxelizer->getGridCenter();
      shader->setVec3("gridMin", gc - glm::vec3(halfSize));
      shader->setVec3("gridMax", gc + glm::vec3(halfSize));
      shader->setFloat("voxelSize", voxelizer->getVoxelGridSize() / static_cast<float>(voxelizer->getResolution()));

      // Set VCT settings
      shader->setBool("vctSettings.indirectSpecularLight",
                      vctSettings.indirectSpecularLight);
      shader->setBool("vctSettings.indirectDiffuseLight",
                      vctSettings.indirectDiffuseLight);
      shader->setInt("vctSettings.diffuseConeCount",
                     vctSettings.diffuseConeCount);
      shader->setFloat("vctSettings.tracingMaxDistance",
                       vctSettings.tracingMaxDistance);

      // Bind voxel 3D texture - using texture unit 5
      glActiveTexture(GL_TEXTURE5);
      glBindTexture(GL_TEXTURE_3D, voxelizer->getVoxelTexture());
      shader->setInt("voxelGrid", 5);

      // Set default material properties for indirect lighting
      shader->setFloat("material.diffuseReflectivity", 0.8f);
      shader->setFloat("material.specularReflectivity", 0.0f);
      shader->setFloat("material.specularDiffusion", 0.5f);
      shader->setVec3("material.specularColor", glm::vec3(1.0f));
    }

    // Set shadow settings uniform
    shader->setBool("shadowSettings.enableIndirectLighting",
                    preferences.shadowSettings.enableIndirectLighting);
  }
  // Voxel cone tracing specific setup
  else if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
    // Set voxel grid parameters -- must account for gridCenter so that
    // cone tracing samples match the voxelization coordinate mapping.
    float halfSize = voxelizer->getVoxelGridSize() * 0.5f;
    glm::vec3 gc = voxelizer->getGridCenter();
    shader->setVec3("gridMin", gc - glm::vec3(halfSize));
    shader->setVec3("gridMax", gc + glm::vec3(halfSize));
    shader->setFloat("voxelSize", voxelizer->getVoxelGridSize() / static_cast<float>(voxelizer->getResolution()));

    // Set VCT settings - only when in VCT mode
    shader->setBool("vctSettings.indirectSpecularLight",
                    vctSettings.indirectSpecularLight);
    shader->setBool("vctSettings.indirectDiffuseLight",
                    vctSettings.indirectDiffuseLight);
    shader->setBool("vctSettings.directLight", vctSettings.directLight);
    shader->setBool("vctSettings.shadows", vctSettings.shadows);

    shader->setInt("vctSettings.diffuseConeCount",
                   vctSettings.diffuseConeCount);
    shader->setFloat("vctSettings.tracingMaxDistance",
                     vctSettings.tracingMaxDistance);
    shader->setInt("vctSettings.shadowSampleCount",
                   vctSettings.shadowSampleCount);
    shader->setFloat("vctSettings.shadowStepMultiplier",
                     vctSettings.shadowStepMultiplier);

    // Set default values for point shadow uniforms (not used in VCT but need
    // to be defined)
    shader->setVec3("lightPos", glm::vec3(0.0f));
    shader->setFloat("far_plane", 50.0f);

    // Bind voxel 3D texture - using texture unit 5
    glActiveTexture(GL_TEXTURE5);
    glBindTexture(GL_TEXTURE_3D, voxelizer->getVoxelTexture());
    shader->setInt("voxelGrid", 5);

    // Set default material properties for voxel cone tracing
    shader->setFloat("material.diffuseReflectivity", 0.8f);
    shader->setFloat("material.specularReflectivity", 0.0f);
    shader->setFloat("material.specularDiffusion", 0.5f);
    shader->setFloat("material.refractiveIndex",
                     1.0f);                          // Default to no refraction
    shader->setFloat("material.transparency", 0.0f); // Default to opaque

    // Set visualization flag (for debugging)
    shader->setBool("enableVoxelVisualization",
                    voxelizer->showDebugVisualization);

    // Update point lights (still needed for direct lighting in VCT)
    updatePointLights();

    // Set point light uniforms
    for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
      std::string lightName = "lights[" + std::to_string(i) + "]";
      shader->setVec3(lightName + ".position", pointLights[i].position);
      shader->setVec3(lightName + ".color", pointLights[i].color);
      shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
      shader->setFloat(lightName + ".linear", pointLights[i].linear);
      shader->setFloat(lightName + ".quadratic", pointLights[i].quadratic);
      shader->setBool("lightsCastShadows[" + std::to_string(i) + "]",
                      pointLights[i].castShadows);
    }
    shader->setInt("numLights", std::min((int)pointLights.size(), MAX_LIGHTS));

    // Set spot light uniforms
    for (int i = 0; i < spotLights.size() && i < MAX_LIGHTS; i++) {
      std::string lightName = "spotLights[" + std::to_string(i) + "]";
      shader->setVec3(lightName + ".position", spotLights[i].position);
      shader->setVec3(lightName + ".direction", spotLights[i].direction);
      shader->setVec3(lightName + ".color", spotLights[i].color);
      shader->setFloat(lightName + ".intensity", spotLights[i].intensity);
      shader->setFloat(lightName + ".innerCutOff", spotLights[i].innerCutOff);
      shader->setFloat(lightName + ".outerCutOff", spotLights[i].outerCutOff);
    }
    shader->setInt("numSpotLights",
                   std::min((int)spotLights.size(), MAX_LIGHTS));
  }
  // Radiance rendering specific setup
  else if (currentLightingMode == GUI::LIGHTING_RADIANCE) {
    // Check if we just switched to radiance mode
    static GUI::LightingMode lastLightingMode = GUI::LIGHTING_SHADOW_MAPPING;
    if (lastLightingMode != GUI::LIGHTING_RADIANCE) {
      triangleDataUploaded =
          false; // Force triangle data upload when switching to radiance mode
    }
    lastLightingMode = currentLightingMode;

    // Set raytracing parameters from GUI settings
    shader->setBool("enableRaytracing", radianceSettings.enableRaytracing);
    shader->setInt("maxBounces", radianceSettings.maxBounces);
    shader->setInt("samplesPerPixel", radianceSettings.samplesPerPixel);
    shader->setFloat("rayMaxDistance", radianceSettings.rayMaxDistance);
    shader->setBool("enableIndirectLighting",
                    radianceSettings.enableIndirectLighting);
    shader->setBool("enableEmissiveLighting",
                    radianceSettings.enableEmissiveLighting);
    shader->setFloat("indirectIntensity", radianceSettings.indirectIntensity);
    shader->setFloat("skyIntensity", radianceSettings.skyIntensity);
    shader->setFloat("emissiveIntensity", radianceSettings.emissiveIntensity);
    shader->setFloat("materialRoughness", radianceSettings.materialRoughness);

    // Set world-space irradiance cache uniforms
    shader->setBool("enableIrradianceCache",
                    radianceSettings.enableIrradianceCache);

    if (irradianceCache && irradianceCache->isInitialized()) {
      // Bind cache SSBOs
      irradianceCache->bindBuffers();

      // Use SHARED grid bounds (same values as compute shader)
      // This ensures worldToGrid() produces identical cell indices in both
      // shaders
      shader->setVec3("gridMin", sharedGridMin);
      shader->setVec3("gridMax", sharedGridMax);
      glUniform3i(glGetUniformLocation(shader->getID(), "gridResolution"),
                  sharedGridRes.x, sharedGridRes.y, sharedGridRes.z);

      // DIAGNOSTIC: Log grid bounds for debugging - should match compute shader
      // values
      std::cout << "=== FRAGMENT SHADER GRID BOUNDS ===" << std::endl;
      std::cout << "Grid bounds: Min(" << sharedGridMin.x << ", "
                << sharedGridMin.y << ", " << sharedGridMin.z << ")"
                << std::endl;
      std::cout << "            Max(" << sharedGridMax.x << ", "
                << sharedGridMax.y << ", " << sharedGridMax.z << ")"
                << std::endl;
      std::cout << "Grid resolution: " << sharedGridRes.x << "x"
                << sharedGridRes.y << "x" << sharedGridRes.z << std::endl;
      std::cout << "Total grid cells: "
                << (sharedGridRes.x * sharedGridRes.y * sharedGridRes.z)
                << std::endl;

      // Check if bounds are degenerate
      if (sharedGridMin.x >= sharedGridMax.x ||
          sharedGridMin.y >= sharedGridMax.y ||
          sharedGridMin.z >= sharedGridMax.z) {
        std::cerr << "ERROR: Degenerate grid bounds!" << std::endl;
      }
    }

    // No camera matrices needed - using rasterized fragment positions

    // Set actual scene lights (same as other modes)
    updatePointLights();

    // Set point light uniforms
    for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
      std::string lightName = "pointLights[" + std::to_string(i) + "]";
      shader->setVec3(lightName + ".position", pointLights[i].position);
      shader->setVec3(lightName + ".color", pointLights[i].color);
      shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
      shader->setFloat(lightName + ".linear", pointLights[i].linear);
      shader->setFloat(lightName + ".quadratic", pointLights[i].quadratic);
      shader->setBool("lightsCastShadows[" + std::to_string(i) + "]",
                      pointLights[i].castShadows);
    }

    // Set spot light uniforms
    for (int i = 0; i < spotLights.size() && i < MAX_LIGHTS; i++) {
      std::string lightName = "spotLights[" + std::to_string(i) + "]";
      shader->setVec3(lightName + ".position", spotLights[i].position);
      shader->setVec3(lightName + ".direction", spotLights[i].direction);
      shader->setVec3(lightName + ".color", spotLights[i].color);
      shader->setFloat(lightName + ".intensity", spotLights[i].intensity);
      shader->setFloat(lightName + ".innerCutOff", spotLights[i].innerCutOff);
      shader->setFloat(lightName + ".outerCutOff", spotLights[i].outerCutOff);
    }
    shader->setInt("numPointLights",
                   std::min((int)pointLights.size(), MAX_LIGHTS));

    // Set sun properties
    shader->setBool("sun.enabled", sun.enabled);
    shader->setVec3("sun.direction", sun.direction);
    shader->setVec3("sun.color", sun.color);
    shader->setFloat("sun.intensity", sun.intensity);

    // DIAGNOSTIC: Verify critical uniforms are set correctly
    std::cout << "=== SHADER UNIFORMS DEBUG ===" << std::endl;
    std::cout << "enableRaytracing: " << radianceSettings.enableRaytracing
              << std::endl;
    std::cout << "enableIrradianceCache: "
              << radianceSettings.enableIrradianceCache << std::endl;
    std::cout << "samplesPerPixel: " << radianceSettings.samplesPerPixel
              << std::endl;
    std::cout << "maxBounces: " << radianceSettings.maxBounces << std::endl;

    // Verify the correct shader is active
    GLint currentProgram = 0;
    glGetIntegerv(GL_CURRENT_PROGRAM, &currentProgram);
    std::cout << "Current shader program ID: " << currentProgram << std::endl;
    std::cout << "Radiance shader ID: " << shader->getID() << std::endl;
    if (currentProgram != (GLint)shader->getID()) {
      std::cout << "ERROR: Wrong shader is active!" << std::endl;
    }

    // Check if scene has changed to determine if we need to recalculate
    // triangle data
    bool sceneChanged = lastSceneState.hasChanged(currentScene);

    // Declare triangle count outside conditional to use in shader uniforms
    static int triangleCount = 0;

    // Only extract and upload triangle data if scene changed or data hasn't
    // been uploaded
    if (sceneChanged || !triangleDataUploaded) {
      // Extract triangle data from scene models and pack into SSBO
      triangleData.clear();
      std::vector<Engine::BVHTriangle> bvhTriangles;
      triangleCount = 0;

      for (const auto &model : currentScene.models) {
        for (const auto &mesh : model.getMeshes()) {
          // Calculate model matrix for this model
          glm::mat4 modelMatrix = glm::mat4(1.0f);
          modelMatrix = glm::translate(modelMatrix, model.position);
          modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x),
                                    glm::vec3(1, 0, 0));
          modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y),
                                    glm::vec3(0, 1, 0));
          modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z),
                                    glm::vec3(0, 0, 1));
          modelMatrix = glm::scale(modelMatrix, model.scale);

          // Get mesh vertices and indices directly (they are public members)
          const auto &vertices = mesh.vertices;
          const auto &indices = mesh.indices;

          // Extract ALL triangles (no more skipping for performance)
          for (size_t i = 0; i < indices.size(); i += 3) {
            if (i + 2 < indices.size()) {
              // Get triangle vertices
              glm::vec3 v0 = glm::vec3(
                  modelMatrix * glm::vec4(vertices[indices[i]].position, 1.0f));
              glm::vec3 v1 =
                  glm::vec3(modelMatrix *
                            glm::vec4(vertices[indices[i + 1]].position, 1.0f));
              glm::vec3 v2 =
                  glm::vec3(modelMatrix *
                            glm::vec4(vertices[indices[i + 2]].position, 1.0f));

              // Calculate triangle normal
              glm::vec3 normal = normalize(cross(v1 - v0, v2 - v0));

              // Pack triangle data into buffer (matches shader Triangle
              // struct layout) struct Triangle { vec3 v0, v1, v2; vec3
              // normal; vec3 color; float emissiveness; float shininess; int
              // materialId; } std430 layout: vec3 takes 3 floats, then next
              // vec3 starts at next 4-float boundary
              triangleData.insert(triangleData.end(),
                                  {v0.x, v0.y, v0.z}); // vec3 v0
              triangleData.push_back(0.0f); // padding for vec3 alignment
              triangleData.insert(triangleData.end(),
                                  {v1.x, v1.y, v1.z}); // vec3 v1
              triangleData.push_back(0.0f); // padding for vec3 alignment
              triangleData.insert(triangleData.end(),
                                  {v2.x, v2.y, v2.z}); // vec3 v2
              triangleData.push_back(0.0f); // padding for vec3 alignment
              triangleData.insert(
                  triangleData.end(),
                  {normal.x, normal.y, normal.z}); // vec3 normal
              triangleData.push_back(0.0f);        // padding for vec3 alignment
              triangleData.insert(
                  triangleData.end(),
                  {model.color.x, model.color.y, model.color.z}); // vec3 color
              triangleData.push_back(
                  model.emissive); // float emissiveness (no padding needed,
                                   // fills vec3 slot)
              triangleData.push_back(model.shininess); // float shininess
              // For int materialId, we need to use reinterpret_cast to pack
              // as float bits
              int materialId = triangleCount;
              triangleData.push_back(*reinterpret_cast<float *>(
                  &materialId));            // int materialId packed as float
              triangleData.push_back(0.0f); // padding for next struct alignment
              triangleData.push_back(0.0f); // padding for next struct alignment

              // Create BVH triangle
              Engine::BVHTriangle bvhTri(v0, v1, v2, normal, model.color,
                                         model.emissive, model.shininess,
                                         materialId);
              bvhTriangles.push_back(bvhTri);

              triangleCount++;
            }
          }
        }
      }

      // Update the SSBO with triangle data
      if (!triangleData.empty()) {
        updateTriangleBuffer(triangleData);
        triangleDataUploaded = true;
        std::cout << "Triangle data updated: " << triangleCount << " triangles"
                  << std::endl;
      }

      // Build BVH if we have triangles and BVH is enabled
      if (!bvhTriangles.empty() && enableBVH && (sceneChanged || !bvhBuilt)) {
        std::cout << "Scene changed, rebuilding BVH..." << std::endl;
        buildBVH(bvhTriangles);
        updateBVHBuffers();
        bvhBuffersUploaded = true;

        // Clear irradiance cache when scene geometry changes
        // Cached irradiance values are no longer valid for new/moved geometry
        if (irradianceCache && irradianceCache->isInitialized()) {
          irradianceCache->clear();
          std::cout << "Irradiance cache cleared due to scene change" << std::endl;
        }

        // Mark voxelizer dirty so it re-voxelizes with new geometry
        if (voxelizer) {
          voxelizer->markDirty();
        }

        // Update debug renderer if debug is enabled
        if (showBVHDebug) {
          // Get max depth from GUI settings
          int maxDepth = preferences.radianceSettings.bvhDebugMaxDepth;
          bvhDebugRenderer.updateFromBVH(bvhBuilder.getNodes(), maxDepth);
          bvhDebugRenderer.setEnabled(true); // Enable rendering
        }

        lastSceneState.update(currentScene);
      }
    }

    // Handle cases where BVH is built but buffers not uploaded yet
    if (bvhBuilt && enableBVH && !bvhBuffersUploaded) {
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
        Engine::BVHDebugRenderer::RenderMode mode =
            static_cast<Engine::BVHDebugRenderer::RenderMode>(
                preferences.radianceSettings.bvhDebugRenderMode);
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
      bvhDebugRenderer.updateFromBVH(
          bvhBuilder.getNodes(), preferences.radianceSettings.bvhDebugMaxDepth);
      lastMaxDepth = preferences.radianceSettings.bvhDebugMaxDepth;
    }
    if (showBVHDebug &&
        (preferences.radianceSettings.bvhDebugRenderMode != lastRenderMode)) {
      // Render mode changed - update renderer
      Engine::BVHDebugRenderer::RenderMode mode =
          static_cast<Engine::BVHDebugRenderer::RenderMode>(
              preferences.radianceSettings.bvhDebugRenderMode);
      bvhDebugRenderer.setRenderMode(mode);
      lastRenderMode = preferences.radianceSettings.bvhDebugRenderMode;
    }

    shader->setInt("numTriangles", triangleCount);
    shader->setInt("numBVHNodes", static_cast<int>(gpuBVHNodes.size()));
    shader->setBool("enableBVH", enableBVH && bvhBuilt);

    // Disable ground plane for pure raytracing (was causing unwanted
    // lighting)
    shader->setBool("hasGroundPlane", false);

    // Populate irradiance cache using compute shader
    if (irradianceCacheComputeShader && irradianceCache &&
        irradianceCache->isInitialized() &&
        radianceSettings.enableIrradianceCache && triangleCount > 0) {

      // NOTE: Cache is persistent across frames - Ward's algorithm incrementally
      // populates the cache where needed. Cache is only cleared when:
      // 1. Scene geometry changes (BVH rebuild)
      // 2. User manually clears it (Ctrl+Shift+I shortcut)
      // This allows the cache to build up coverage over time for better performance.

      // Use compute shader to populate cache
      irradianceCacheComputeShader->use();

      // CRITICAL FIX: Explicitly bind ALL required SSBOs before compute shader dispatch
      // The compute shader needs access to:
      // - Triangle buffer (binding 0)
      // - BVH nodes (binding 1)
      // - Triangle indices (binding 2)
      // - Cache buffers (bindings 3, 4, 5)

      std::cout << "=== BINDING SSBOs FOR COMPUTE SHADER ===" << std::endl;

      // Bind triangle buffer (binding 0)
      if (triangleSSBO != 0) {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triangleSSBO);
        std::cout << "Bound triangle SSBO " << triangleSSBO << " to binding 0" << std::endl;
      } else {
        std::cerr << "ERROR: Triangle SSBO is 0!" << std::endl;
      }

      // Bind BVH buffers (bindings 1, 2) if BVH is enabled
      if (enableBVH && bvhBuilt) {
        if (bvhNodeSSBO != 0) {
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bvhNodeSSBO);
          std::cout << "Bound BVH node SSBO " << bvhNodeSSBO << " to binding 1" << std::endl;
        } else {
          std::cerr << "ERROR: BVH node SSBO is 0!" << std::endl;
        }
        if (triangleIndexSSBO != 0) {
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, triangleIndexSSBO);
          std::cout << "Bound triangle index SSBO " << triangleIndexSSBO << " to binding 2" << std::endl;
        } else {
          std::cerr << "ERROR: Triangle index SSBO is 0!" << std::endl;
        }
      } else {
        std::cout << "BVH not enabled or not built - using linear traversal" << std::endl;
      }

      // Bind cache SSBOs (bindings 3, 4, 5)
      irradianceCache->bindBuffers();
      std::cout << "Bound cache SSBOs to bindings 3, 4, 5" << std::endl;

      // Use SHARED grid bounds (calculated once at beginning of renderEye)
      // This ensures worldToGrid() produces identical cell indices in both
      // shaders
      irradianceCacheComputeShader->setVec3("gridMin", sharedGridMin);
      irradianceCacheComputeShader->setVec3("gridMax", sharedGridMax);
      glUniform3i(glGetUniformLocation(irradianceCacheComputeShader->getID(),
                                       "gridResolution"),
                  sharedGridRes.x, sharedGridRes.y, sharedGridRes.z);

      // DIAGNOSTIC: Log compute shader grid bounds - should match fragment
      // shader
      std::cout << "=== COMPUTE SHADER GRID BOUNDS ===" << std::endl;
      std::cout << "Grid bounds: Min(" << sharedGridMin.x << ", "
                << sharedGridMin.y << ", " << sharedGridMin.z << ")"
                << std::endl;
      std::cout << "            Max(" << sharedGridMax.x << ", "
                << sharedGridMax.y << ", " << sharedGridMax.z << ")"
                << std::endl;
      std::cout << "Grid resolution: " << sharedGridRes.x << "x"
                << sharedGridRes.y << "x" << sharedGridRes.z << std::endl;

      // CRITICAL VALIDATION: Verify uniforms are actually set in the shader
      GLuint computeProgramID = irradianceCacheComputeShader->getID();
      GLint gridMinLoc = glGetUniformLocation(computeProgramID, "gridMin");
      GLint gridMaxLoc = glGetUniformLocation(computeProgramID, "gridMax");
      GLint gridResLoc = glGetUniformLocation(computeProgramID, "gridResolution");

      std::cout << "=== COMPUTE SHADER UNIFORM VALIDATION ===" << std::endl;
      std::cout << "gridMin location: " << gridMinLoc << std::endl;
      std::cout << "gridMax location: " << gridMaxLoc << std::endl;
      std::cout << "gridResolution location: " << gridResLoc << std::endl;

      if (gridMinLoc == -1 || gridMaxLoc == -1 || gridResLoc == -1) {
        std::cerr << "ERROR: One or more grid uniforms not found in compute shader!" << std::endl;
        std::cerr << "This means the shader cannot perform worldToGrid() correctly!" << std::endl;
      }

      // Read back uniform values to verify they were set
      glm::vec3 readbackGridMin, readbackGridMax;
      glm::ivec3 readbackGridRes;
      if (gridMinLoc != -1) {
        glGetUniformfv(computeProgramID, gridMinLoc, &readbackGridMin.x);
        std::cout << "gridMin readback: (" << readbackGridMin.x << ", "
                  << readbackGridMin.y << ", " << readbackGridMin.z << ")" << std::endl;
      }
      if (gridMaxLoc != -1) {
        glGetUniformfv(computeProgramID, gridMaxLoc, &readbackGridMax.x);
        std::cout << "gridMax readback: (" << readbackGridMax.x << ", "
                  << readbackGridMax.y << ", " << readbackGridMax.z << ")" << std::endl;
      }
      if (gridResLoc != -1) {
        glGetUniformiv(computeProgramID, gridResLoc, &readbackGridRes.x);
        std::cout << "gridResolution readback: " << readbackGridRes.x << "x"
                  << readbackGridRes.y << "x" << readbackGridRes.z << std::endl;
      }

      // Set sampling parameters
      // CRITICAL FIX: Ward recommends 32-64 samples for stable harmonic mean
      // Higher sample count = more accurate local feature size estimates
      irradianceCacheComputeShader->setInt("samplesPerEntry", 32);
      irradianceCacheComputeShader->setFloat("minSpacing", 0.5f);
      irradianceCacheComputeShader->setFloat("randomSeed",
                                             static_cast<float>(glfwGetTime()));

      // Set triangle and BVH parameters
      irradianceCacheComputeShader->setInt("numTriangles", triangleCount);
      irradianceCacheComputeShader->setInt(
          "numBVHNodes", static_cast<int>(gpuBVHNodes.size()));
      irradianceCacheComputeShader->setBool("enableBVH", enableBVH && bvhBuilt);
      irradianceCacheComputeShader->setFloat("rayMaxDistance",
                                             radianceSettings.rayMaxDistance);

      std::cout << "=== COMPUTE SHADER PARAMETERS ===" << std::endl;
      std::cout << "numTriangles: " << triangleCount << std::endl;
      std::cout << "numBVHNodes: " << gpuBVHNodes.size() << std::endl;
      std::cout << "enableBVH: " << (enableBVH && bvhBuilt) << std::endl;
      std::cout << "samplesPerEntry: 32" << std::endl;

      // Declare variable that needs to be accessible after goto label
      uint32_t gpuEntryCount = 0;

      // CRITICAL CHECK: Verify we have triangles to process
      if (triangleCount == 0) {
        std::cerr << "ERROR: triangleCount is 0! Cannot populate irradiance cache." << std::endl;
        std::cerr << "This means no scene geometry was uploaded to the GPU." << std::endl;
        // Don't dispatch if there are no triangles
        goto skip_compute_dispatch;
      }

      { // Scope block to allow goto to skip variable initializations
      // Dispatch compute shader
      // Work groups process triangles in batches of 64
      GLuint numWorkGroups = (triangleCount + 63) / 64;
      std::cout << "Dispatching compute shader: " << numWorkGroups << " work groups" << std::endl;
      std::cout << "This will process " << triangleCount << " triangles (sampling every 4th)" << std::endl;
      glDispatchCompute(numWorkGroups, 1, 1);

      // Check for OpenGL errors after dispatch
      GLenum err = glGetError();
      if (err != GL_NO_ERROR) {
        std::cerr << "OpenGL error after glDispatchCompute: 0x" << std::hex << err << std::dec << std::endl;
      }

      // Wait for compute to finish before fragment shader reads cache
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

      // CRITICAL: Ensure GPU has fully completed all operations before CPU
      // reads
      glFinish();

      // DIAGNOSTIC: Read back cache entry count and debug counters from GPU
      glBindBuffer(GL_SHADER_STORAGE_BUFFER,
                   irradianceCache->getCacheBufferSSBO());
      uint32_t headerData[4] = {0};
      glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(headerData),
                         headerData);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

      gpuEntryCount = headerData[0];
      uint32_t debugGridAttempts = headerData[2];
      uint32_t debugGridSuccesses = headerData[3];

      std::cout << "=== IRRADIANCE CACHE DEBUG ===" << std::endl;
      std::cout << "Cache entry count (GPU): " << gpuEntryCount << std::endl;
      std::cout << "Triangle count: " << triangleCount << std::endl;
      std::cout << "Expected samples: ~" << (triangleCount / 4)
                << " (1 in 4 triangles, after validation)" << std::endl;
      std::cout << "DEBUG: Grid insertion attempts: " << debugGridAttempts << std::endl;
      std::cout << "DEBUG: Grid insertions with valid cellIdx: " << debugGridSuccesses << std::endl;
      if (debugGridAttempts > 0 && debugGridSuccesses == 0) {
        std::cerr << "ERROR: ALL grid cell indices are invalid (0xFFFFFFFF)!" << std::endl;
        std::cerr << "This means worldToGrid() or gridIndex() is broken!" << std::endl;
      }

      // DIAGNOSTIC STEP 10: Read back first cache entry to verify data validity
      if (gpuEntryCount > 0) {
        glBindBuffer(GL_SHADER_STORAGE_BUFFER,
                     irradianceCache->getCacheBufferSSBO());

        struct CacheEntry {
          glm::vec3 position;
          float harmonicMeanDist;
          glm::vec3 normal;
          float padding1;
          glm::vec3 irradiance;
          float padding2;
        };

        // Read first entry (skip 16-byte header: 4 uint32s)
        CacheEntry firstEntry;
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                           16 + 0 * sizeof(CacheEntry), sizeof(CacheEntry),
                           &firstEntry);

        std::cout << "=== FIRST CACHE ENTRY DEBUG ===" << std::endl;
        std::cout << "Position: (" << firstEntry.position.x << ", "
                  << firstEntry.position.y << ", " << firstEntry.position.z
                  << ")" << std::endl;
        std::cout << "Normal: (" << firstEntry.normal.x << ", "
                  << firstEntry.normal.y << ", " << firstEntry.normal.z << ")"
                  << std::endl;
        std::cout << "Irradiance: (" << firstEntry.irradiance.x << ", "
                  << firstEntry.irradiance.y << ", " << firstEntry.irradiance.z
                  << ")" << std::endl;
        std::cout << "Harmonic mean dist: " << firstEntry.harmonicMeanDist
                  << std::endl;

        // DIAGNOSTIC: Calculate harmonic mean distance distribution statistics
        // This helps verify the Ward algorithm fixes are working correctly
        float minHMD = 1e10f, maxHMD = 0.0f, avgHMD = 0.0f;
        int hmd1000Count = 0, validCount = 0;

        // Sample up to 100 entries to get distribution statistics
        uint32_t sampleCount = std::min(gpuEntryCount, 100u);
        for (uint32_t i = 0; i < sampleCount; i++) {
          CacheEntry entry;
          glGetBufferSubData(GL_SHADER_STORAGE_BUFFER,
                             16 + i * sizeof(CacheEntry), sizeof(CacheEntry),
                             &entry);

          float hmd = entry.harmonicMeanDist;

          // Skip NaN/Inf values in statistics
          if (!std::isnan(hmd) && !std::isinf(hmd) && hmd > 0.0f) {
            if (hmd >= 999.0f)
              hmd1000Count++; // Count pathologically large values
            minHMD = std::min(minHMD, hmd);
            maxHMD = std::max(maxHMD, hmd);
            avgHMD += hmd;
            validCount++;
          }
        }

        if (validCount > 0) {
          avgHMD /= validCount;

          std::cout << "\n=== HARMONIC MEAN DISTANCE STATS (sampled "
                    << sampleCount << " entries) ===" << std::endl;
          std::cout << "Min HMD: " << minHMD << std::endl;
          std::cout << "Max HMD: " << maxHMD << std::endl;
          std::cout << "Avg HMD: " << avgHMD << std::endl;
          std::cout << "Entries with HMD >= 999: " << hmd1000Count << " / "
                    << sampleCount;
          if (hmd1000Count > 0) {
            std::cout << " (" << (100.0f * hmd1000Count / sampleCount) << "%)";
          }
          std::cout << std::endl;

          // Quality assessment based on Ward's recommendations
          if (avgHMD < 1.0f) {
            std::cout << "⚠️ WARNING: Average HMD very small - may indicate too "
                         "dense sampling"
                      << std::endl;
          } else if (avgHMD > 50.0f) {
            std::cout
                << "⚠️ WARNING: Average HMD very large - patches may be too big"
                << std::endl;
          } else {
            std::cout
                << "✓ Harmonic mean distances in reasonable range (1-50 units)"
                << std::endl;
          }
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
      }

      // NOTE: With persistent caching, 0 entries on first frame is an error,
      // but slow growth on later frames is expected (isCovered() filters duplicates)
      static uint32_t lastEntryCount = 0;

      // Detect cache clear (count decreased) and reset tracking
      if (gpuEntryCount < lastEntryCount) {
        lastEntryCount = 0;
        std::cout << "Cache was cleared - resetting tracking" << std::endl;
      }

      uint32_t entriesAdded = gpuEntryCount - lastEntryCount;

      if (gpuEntryCount == 0) {
        std::cout << "WARNING: Cache still empty after compute shader!" << std::endl;
        std::cout << "Possible causes:" << std::endl;
        std::cout << "  - All triangles filtered by isCovered() check (cache may be fully populated)"
                  << std::endl;
        std::cout << "  - Grid bounds don't cover scene geometry" << std::endl;
        std::cout << "  - Compute shader not executing properly" << std::endl;
      } else {
        std::cout << "Cache population: +" << entriesAdded << " new entries this frame (total: "
                  << gpuEntryCount << ")" << std::endl;
        if (entriesAdded == 0 && lastEntryCount > 0) {
          std::cout << "  (Cache stable - all sampled positions already covered)" << std::endl;
        }
      }
      lastEntryCount = gpuEntryCount;
      } // End scope block

      skip_compute_dispatch:
      // DON'T unbind cache buffers here - fragment shader needs to read them
      // during rendering! irradianceCache->unbindBuffers();

      // CRITICAL FIX: Switch back to the radiance shader after compute shader!
      // Without this, the compute shader (which has no vertex/fragment stages)
      // remains active and geometry won't be rasterized!
      shader->use();

      // CRITICAL: Always read the current cache entry count from GPU buffer
      // This ensures we have the correct value regardless of code path taken
      glBindBuffer(GL_SHADER_STORAGE_BUFFER,
                   irradianceCache->getCacheBufferSSBO());
      glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0, sizeof(uint32_t),
                         &gpuEntryCount);
      glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

      // NOTE: cacheEntryCount is read from the SSBO header, not from a uniform
      // The fragment shader accesses it as: IrradianceCacheBuffer.cacheEntryCount
      // No need to set it as a uniform - it's already in the buffer at offset 0
      std::cout << "Fragment shader will read cacheEntryCount from SSBO: " << gpuEntryCount
                << std::endl;

      // DIAGNOSTIC: Check if grid cells are actually populated
      if (gpuEntryCount > 0) {
        struct GridCell {
          uint32_t entryStart;
          uint32_t entryCount;
          // NO padding - must match IrradianceCache.h structure exactly!
        };

        glBindBuffer(GL_SHADER_STORAGE_BUFFER,
                     irradianceCache->getGridCellsSSBO());

        uint32_t gridSize = sharedGridRes.x * sharedGridRes.y * sharedGridRes.z;
        uint32_t nonEmptyCells = 0;
        uint32_t totalEntries = 0;
        uint32_t maxEntriesInCell = 0;

        // Sample first 1000 cells to check population
        uint32_t samplesToCheck = std::min(gridSize, 1000u);
        for (uint32_t i = 0; i < samplesToCheck; i++) {
          GridCell cell;
          glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, i * sizeof(GridCell),
                             sizeof(GridCell), &cell);

          if (cell.entryCount > 0) {
            nonEmptyCells++;
            totalEntries += cell.entryCount;
            maxEntriesInCell = std::max(maxEntriesInCell, cell.entryCount);
          }
        }

        std::cout << "\n=== GRID CELL POPULATION DEBUG ===" << std::endl;
        std::cout << "Total grid cells: " << gridSize << std::endl;
        std::cout << "Non-empty cells (sampled " << samplesToCheck
                  << "): " << nonEmptyCells << std::endl;
        std::cout << "Total entries in sampled cells: " << totalEntries
                  << std::endl;
        std::cout << "Max entries in a single cell: " << maxEntriesInCell
                  << std::endl;
        std::cout << "Avg entries per non-empty cell: "
                  << (nonEmptyCells > 0 ? (float)totalEntries / nonEmptyCells
                                        : 0.0f)
                  << std::endl;

        if (nonEmptyCells == 0) {
          std::cout << "ERROR: No grid cells populated! Cache lookup will fail!"
                    << std::endl;
          std::cout << "Possible causes:" << std::endl;
          std::cout << "  - worldToGrid() returning out-of-bounds coordinates"
                    << std::endl;
          std::cout << "  - gridIndex() always returning 0xFFFFFFFF"
                    << std::endl;
          std::cout << "  - Atomic operations on cells[] failing" << std::endl;
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
      }
    }
  }

  /*
  // DIAGNOSTIC: Verify OpenGL state before rendering
  std::cout << "=== OPENGL STATE DEBUG ===" << std::endl;

  GLboolean depthTestEnabled;
  glGetBooleanv(GL_DEPTH_TEST, &depthTestEnabled);
  std::cout << "Depth test enabled: " << (depthTestEnabled ? "YES" : "NO") <<
  std::endl;

  GLboolean blendEnabled;
  glGetBooleanv(GL_BLEND, &blendEnabled);
  std::cout << "Blending enabled: " << (blendEnabled ? "YES" : "NO") <<
  std::endl;

  GLboolean cullFaceEnabled;
  glGetBooleanv(GL_CULL_FACE, &cullFaceEnabled);
  std::cout << "Culling enabled: " << (cullFaceEnabled ? "YES" : "NO") <<
  std::endl;

  GLint depthFunc;
  glGetIntegerv(GL_DEPTH_FUNC, &depthFunc);
  std::cout << "Depth func: " << depthFunc << " (GL_LESS=" << GL_LESS << ")" <<
  std::endl;

  GLboolean colorMask[4];
  glGetBooleanv(GL_COLOR_WRITEMASK, colorMask);
  std::cout << "Color mask: R=" << (int)colorMask[0] << " G=" <<
  (int)colorMask[1]
            << " B=" << (int)colorMask[2] << " A=" << (int)colorMask[3] <<
  std::endl;
    */
  // Render scene - cache buffers still bound, fragment shader can read them
  renderModels(shader);
  renderPointClouds(shader);

  // Render light visualizations when Ctrl is pressed
  if (ctrlPressed) {
    renderLightVisualizations(shader);
  }

  // Render BVH debug visualization (after main scene rendering)
  if (showBVHDebug && bvhBuilt) {
    // BVH debug lines are now rendering
    bvhDebugRenderer.render(view, projection);
  }

  // Calculate distance to nearest object AFTER scene rendering but BEFORE
  // zero plane This ensures zero plane doesn't interfere with distance
  // calculation Only calculate once per frame (left eye for stereo, or single
  // eye for mono)
  static bool distanceCalculatedThisFrame = false;
  static double lastFrameTime = 0.0;
  double currentFrameTime = glfwGetTime();

  if (currentFrameTime != lastFrameTime) {
    distanceCalculatedThisFrame = false;
    lastFrameTime = currentFrameTime;
  }

  if (!distanceCalculatedThisFrame) {
    float distanceToNearestObject = camera.getDistanceToNearestObject(
        camera, projection, view, preferences.farPlane, windowWidth,
        windowHeight);
    camera.UpdateDistanceToObject(distanceToNearestObject);
    float largestDimension = calculateLargestModelDimension();
    camera.AdjustMovementSpeed(distanceToNearestObject, largestDimension,
                               preferences.farPlane);

    // Update target convergence automatically if enabled
    if (preferences.autoConvergence) {
      float cameraDistance = camera.distanceToNearestObject;
      if (cameraDistance < preferences.farPlane * 0.95f &&
          camera.distanceUpdated) {
        float autoConvergenceValue =
            cameraDistance * preferences.convergenceDistanceFactor;

        // Apply user-defined convergence cap if enabled
        if (preferences.enableConvergenceCap) {
          autoConvergenceValue =
              glm::clamp(autoConvergenceValue, preferences.convergenceCapMin,
                         preferences.convergenceCapMax);
        } else {
          // Default clamp to reasonable bounds: Min: near plane, Max: far
          // plane
          autoConvergenceValue =
              glm::clamp(autoConvergenceValue, preferences.nearPlane,
                         preferences.farPlane);
        }

        targetConvergence = autoConvergenceValue;
      }
      // If looking at empty space, keep previous target convergence value
    }
    distanceCalculatedThisFrame = true;
  }

  // Smoothly interpolate convergence to target (always, but faster when auto
  // convergence is on)
  if (preferences.autoConvergence) {
    // Smooth interpolation when auto convergence is enabled
    float lerpFactor = 1.0f - glm::exp(-convergenceSmoothingSpeed * deltaTime);
    preferences.convergence =
        glm::mix(preferences.convergence, targetConvergence, lerpFactor);
    preferences.convergence = preferences.convergence;
  } else {
    // When auto convergence is off, keep target in sync with actual
    // convergence so when it's turned back on, it starts from the current
    // value
    targetConvergence = preferences.convergence;
  }

  // Render zero plane if enabled (AFTER distance calculation)
  if (preferences.showZeroPlane) {
    renderZeroPlane(shader, projection, view, preferences.convergence);
  }

  renderSkybox(projection, view, shader);

  // Calculate cursor position AFTER scene rendering but BEFORE cursor
  // rendering This ensures we read scene depth, not cursor depth from the
  // buffer
  cursorManager.updateCursorPosition(window, projection, view, shader, false,
                                     isStereo, leftProjection, leftView,
                                     rightProjection, rightView);

  // Update SpaceMouse cursor anchor when cursor position changes
  updateSpaceMouseCursorAnchor();

  // Update shader uniforms for cursors (use active shader, not original
  // shader)
  cursorManager.updateShaderUniforms(shader);

  // Render orbit center if needed
  if (!orbitFollowsCursor && cursorManager.isShowOrbitCenter() &&
      (camera.IsOrbiting || cursorManager.isAlwaysShowOrbitCenter())) {
    cursorManager.renderOrbitCenter(projection, view, camera.OrbitPoint);
  }

  if (camera.IsPanning == false) {
    cursorManager.renderCursors(projection, view);
  }

  // Render brush tool indicator
  if (preferences.brushToolSettings.enabled &&
      preferences.brushToolSettings.selectedModelIndex >= 0 &&
      cursorManager.isCursorPositionValid()) {

    // Get cursor position and calculate normal
    glm::vec3 cursorPos = cursorManager.getCursorPosition();

    // Ray-cast to get the surface normal at cursor position
    glm::vec3 rayOrigin, rayDirection, rayNear, rayFar;
    calculateMouseRay(lastX, lastY, rayOrigin, rayDirection, rayNear, rayFar,
                      aspectRatio);

    glm::vec3 surfaceNormal = glm::vec3(0.0f, 1.0f, 0.0f); // Default up
    float distance;

    // Find surface normal at cursor position
    for (const auto &model : currentScene.models) {
      glm::vec3 hitNormal;
      if (rayIntersectsModel(rayOrigin, rayDirection, model, distance,
                             hitNormal)) {
        surfaceNormal = hitNormal;
        break;
      }
    }

    brushTool.renderBrushIndicator(projection, view, cursorPos, surfaceNormal);
  }

  // 4. Voxel visualization (if enabled) - AFTER main rendering
  if (voxelizer->showDebugVisualization) {
    voxelizer->renderDebugVisualization(camera.Position, projection, view);
  }

  // Render UI (only if requested)
  if (showGui && renderGUIFlag) {
    renderGUI(drawBuffer == GL_BACK_LEFT, viewport, windowFlags, shader);
  }

  // Reset OpenGL state
  glUseProgram(0);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);
  glBindTexture(GL_TEXTURE_3D, 0);
}

void renderModels(Engine::Shader *shader) {
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
      shader->setInt("numLights",
                     std::min((int)pointLights.size(), MAX_LIGHTS));

      // Set sun properties
      shader->setBool("sun.enabled", sun.enabled);
      shader->setVec3("sun.direction", sun.direction);
      shader->setVec3("sun.color", sun.color);
      shader->setFloat("sun.intensity", sun.intensity);
    } else if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
      // Update point lights (still needed for direct lighting in VCT)
      updatePointLights();

      // Set point light uniforms
      for (int i = 0; i < pointLights.size() && i < MAX_LIGHTS; i++) {
        std::string lightName = "lights[" + std::to_string(i) + "]";
        shader->setVec3(lightName + ".position", pointLights[i].position);
        shader->setVec3(lightName + ".color", pointLights[i].color);
        shader->setFloat(lightName + ".intensity", pointLights[i].intensity);
      }
      shader->setInt("numLights",
                     std::min((int)pointLights.size(), MAX_LIGHTS));

      // Set sun properties
      shader->setBool("sun.enabled", sun.enabled);
      shader->setVec3("sun.direction", sun.direction);
      shader->setVec3("sun.color", sun.color);
      shader->setFloat("sun.intensity", sun.intensity);

      // Set VCT settings
      shader->setBool("vctSettings.indirectSpecularLight",
                      vctSettings.indirectSpecularLight);
      shader->setBool("vctSettings.indirectDiffuseLight",
                      vctSettings.indirectDiffuseLight);
      shader->setBool("vctSettings.directLight", vctSettings.directLight);
      shader->setBool("vctSettings.shadows", vctSettings.shadows);

      // Set voxel grid parameters -- must account for gridCenter
      float halfSize = voxelizer->getVoxelGridSize() * 0.5f;
      glm::vec3 gc = voxelizer->getGridCenter();
      shader->setVec3("gridMin", gc - glm::vec3(halfSize));
      shader->setVec3("gridMax", gc + glm::vec3(halfSize));
      shader->setFloat("voxelSize", voxelizer->getVoxelGridSize() / static_cast<float>(voxelizer->getResolution()));

      // Set visualization flag (for debugging)
      shader->setBool("enableVoxelVisualization",
                      voxelizer->showDebugVisualization);
    }
  }

  // Calculate view projection matrix for frustum culling
  glm::mat4 viewProj;
  if (shader != simpleDepthShader) {
    viewProj = camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                          preferences.farPlane) *
               camera.GetViewMatrix();
  }

  // Render each model
  for (int i = 0; i < currentScene.models.size(); i++) {
    auto &model = currentScene.models[i];
    if (!model.visible)
      continue;

    // Calculate model matrix
    glm::mat4 modelMatrix = glm::mat4(1.0f);
    modelMatrix = glm::translate(modelMatrix, model.position);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x),
                              glm::vec3(1, 0, 0));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y),
                              glm::vec3(0, 1, 0));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z),
                              glm::vec3(0, 0, 1));
    modelMatrix = glm::scale(modelMatrix, model.scale);

    // Set model matrix in shader
    shader->setMat4("model", modelMatrix);

    // Calculate and set normal matrix for proper normal transformation
    glm::mat3 normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    shader->setMat3("normalMatrix", normalMatrix);

    // Set standard material properties
    shader->setBool("material.hasNormalMap", model.hasNormalMap());
    shader->setBool("material.hasSpecularMap", model.hasSpecularMap());
    shader->setBool("material.hasAOMap", model.hasAOMap());
    shader->setFloat("material.hasTexture",
                     !model.getMeshes().empty() &&
                             !model.getMeshes()[0].textures.empty()
                         ? 1.0f
                         : 0.0f);
    shader->setVec3("material.objectColor", model.color);
    shader->setFloat("material.shininess", model.shininess);
    shader->setFloat("material.emissive", model.emissive);

    // PBR material properties
    shader->setFloat("material.metallicFactor", model.metallicFactor);
    shader->setFloat("material.roughnessFactor", model.roughnessFactor);
    shader->setVec3("material.F0", model.F0);
    shader->setFloat("material.normalScale", model.normalScale);
    shader->setFloat("material.heightScale", model.heightScale);

    // Set emissive intensity for all lighting modes
    shader->setFloat("emissiveIntensity", radianceSettings.emissiveIntensity);

    // For VCT, set additional material properties
    if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
      // Set VCT specific material properties
      shader->setFloat("material.diffuseReflectivity",
                       model.diffuseReflectivity);
      shader->setVec3("material.specularColor", model.specularColor);
      shader->setFloat("material.specularReflectivity",
                       model.specularReflectivity);
      shader->setFloat("material.specularDiffusion", model.specularDiffusion);
      shader->setFloat("material.refractiveIndex", model.refractiveIndex);
      shader->setFloat("material.transparency", model.transparency);
    }

    // Set selection state
    shader->setBool("selectionMode", selectionMode);
    shader->setBool("isSelected",
                    selectionMode && (i == currentSelectedIndex) &&
                        (currentSelectedType == SelectedType::Model));
    shader->setInt("selectedMeshIndex", currentSelectedMeshIndex);
    shader->setBool("isMeshSelected", currentSelectedMeshIndex >= 0);

    // Set current mesh index for all meshes
    for (int j = 0; j < model.getMeshes().size(); j++) {
      shader->setInt("currentMeshIndex", j);
      model.getMeshes()[j].Draw(*shader);
    }
  }

  // Render brush tool instances with instanced rendering
  // Note: Instances should always render, regardless of whether brush tool is
  // enabled
  if (instancedShader && shader != simpleDepthShader &&
      brushTool.getTotalInstanceCount() > 0) {
    instancedShader->use();

    // Set basic uniforms
    instancedShader->setMat4("view", camera.GetViewMatrix());
    instancedShader->setMat4("projection",
                             camera.GetProjectionMatrix(aspectRatio,
                                                        preferences.nearPlane,
                                                        preferences.farPlane));
    instancedShader->setVec3("viewPos", camera.Position);
    instancedShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);
    instancedShader->setBool("enableShadows", enableShadows);

    // Set sun properties
    instancedShader->setVec3("sunDirection", sun.direction);
    instancedShader->setVec3("sunColor", sun.color);
    instancedShader->setFloat("sunIntensity", sun.intensity);
    instancedShader->setBool("useInstanceColor", true);

    // Bind shadow map
    if (enableShadows) {
      glActiveTexture(GL_TEXTURE0 + Engine::SHADOW_MAP_TEXTURE_UNIT);
      glBindTexture(GL_TEXTURE_2D, depthMap);
      instancedShader->setInt("shadowMap", Engine::SHADOW_MAP_TEXTURE_UNIT);
    }

    // Render instances
    brushTool.renderInstances(instancedShader, currentScene.models);

    // Restore active shader
    shader->use();
  }
}

void renderPointClouds(Engine::Shader *shader) {
  // Skip point cloud rendering for depth pass as points don't cast good
  // shadows
  if (shader == simpleDepthShader)
    return;

  for (auto &pointCloud : currentScene.pointClouds) {

    if (!pointCloud.visible) {
      continue;
    }

    glm::mat4 modelMatrix = glm::mat4(1.0f);
    modelMatrix = glm::translate(modelMatrix, pointCloud.position);
    modelMatrix = glm::rotate(modelMatrix, glm::radians(pointCloud.rotation.x),
                              glm::vec3(1, 0, 0));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(pointCloud.rotation.y),
                              glm::vec3(0, 1, 0));
    modelMatrix = glm::rotate(modelMatrix, glm::radians(pointCloud.rotation.z),
                              glm::vec3(0, 0, 1));
    modelMatrix = glm::scale(modelMatrix, pointCloud.scale);

    shader->setMat4("model", modelMatrix);

    // Calculate and set normal matrix for consistency
    glm::mat3 normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    shader->setMat3("normalMatrix", normalMatrix);

    shader->setBool("isPointCloud", true);

    // DIAGNOSTIC: Verify isPointCloud uniform is set
    GLint isPointCloudValue;
    glGetUniformiv(shader->getID(),
                   glGetUniformLocation(shader->getID(), "isPointCloud"),
                   &isPointCloudValue);
    std::cout << "  isPointCloud uniform value: " << isPointCloudValue
              << std::endl;

    // Always use octree-based rendering (legacy system removed)
    if (pointCloud.octreeRoot) {
      // Update LOD system for current camera position
      glm::vec3 cameraPosition = camera.Position;
      OctreePointCloudManager::updateLOD(pointCloud, cameraPosition);

      // Bind VAO for octree rendering (octree nodes use their own VBOs but
      // need the VAO for attributes)
      glBindVertexArray(pointCloud.vao);

      // Render visible octree nodes
      OctreePointCloudManager::renderVisible(pointCloud, cameraPosition);

      glBindVertexArray(0);
    }

    // Visualize octree structure if enabled
    if (pointCloud.visualizeOctree && pointCloud.octreeRoot) {
      // Generate octree visualization if not already done
      if (pointCloud.chunkOutlineVertices.empty()) {
        OctreePointCloudManager::generateOctreeVisualization(
            pointCloud, pointCloud.visualizeDepth);
      }

      shader->setBool("isChunkOutline", true);
      shader->setVec3("outlineColor", glm::vec3(0.0f, 1.0f, 0.0f));

      glBindVertexArray(pointCloud.chunkOutlineVAO);
      glDrawArrays(
          GL_LINES, 0,
          static_cast<GLsizei>(pointCloud.chunkOutlineVertices.size()));
      glBindVertexArray(0);

      shader->setBool("isChunkOutline", false);
    }
  }

  shader->setBool("isPointCloud", false);
}

void renderZeroPlane(Engine::Shader *shader, const glm::mat4 &projection,
                     const glm::mat4 &view, float convergence) {
  // Skip if zero plane shader is not loaded
  if (!zeroPlaneShader)
    return;

  // Create quad geometry for zero plane (only once)
  static bool initialized = false;
  if (!initialized) {
    // Quad vertices with texture coordinates
    float vertices[] = {// positions        // texture coords
                        -1.0f, 1.0f, 0.0f, 0.0f, 1.0f,  -1.0f, -1.0f,
                        0.0f,  0.0f, 0.0f, 1.0f, -1.0f, 0.0f,  1.0f,
                        0.0f,  1.0f, 1.0f, 0.0f, 1.0f,  1.0f};

    unsigned int indices[] = {0, 1, 2, 0, 2, 3};

    glGenVertexArrays(1, &zeroPlaneVAO);
    glGenBuffers(1, &zeroPlaneVBO);
    glGenBuffers(1, &zeroPlaneEBO);

    glBindVertexArray(zeroPlaneVAO);

    glBindBuffer(GL_ARRAY_BUFFER, zeroPlaneVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, zeroPlaneEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices,
                 GL_STATIC_DRAW);

    // Position attribute
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)0);
    glEnableVertexAttribArray(0);

    // Texture coordinate attribute
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                          (void *)(3 * sizeof(float)));
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
  glm::vec3 forward = -camera.Front; // Plane normal points toward camera
  glm::vec3 right = camera.Right;    // Use camera's right vector
  glm::vec3 up = camera.Up;          // Use camera's up vector

  // Construct billboard transformation matrix
  glm::mat4 billboardMatrix = glm::mat4(1.0f);
  billboardMatrix[0] = glm::vec4(right, 0.0f);
  billboardMatrix[1] = glm::vec4(up, 0.0f);
  billboardMatrix[2] = glm::vec4(forward, 0.0f);
  billboardMatrix[3] = glm::vec4(planePosition, 1.0f);

  // Scale the plane to make it large enough to be visible
  glm::mat4 scaleMatrix =
      glm::scale(glm::mat4(1.0f), glm::vec3(10.0f, 10.0f, 1.0f));
  glm::mat4 model = billboardMatrix * scaleMatrix;

  // Set uniforms
  zeroPlaneShader->setMat4("model", model);
  zeroPlaneShader->setMat4("view", view);
  zeroPlaneShader->setMat4("projection", projection);
  zeroPlaneShader->setVec4(
      "planeColor",
      glm::vec4(0.0f, 1.0f, 0.0f, 0.5f)); // Green with transparency
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
               glm::mat4 view, glm::mat4 projection, glm::mat4 leftview,
               glm::mat4 leftprojection, glm::mat4 rightview,
               glm::mat4 rightprojection, Engine::Shader *shader,
               bool renderScene, float radarScale, glm::vec2 position) {
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

  glm::mat4 defaultView =
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),  // eye at origin
                  glm::vec3(0.0f, 0.0f, 1.0f),  // looking towards +Z
                  glm::vec3(0.0f, 1.0f, 0.0f)); // up is +Y

  glm::vec4 fd_ndc =
      projection * defaultView * glm::vec4(0, 0, focaldist, 1.0f);
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
  glBufferData(GL_ARRAY_BUFFER, sizeof(GLfloat) * 3 * numPoints, buf,
               GL_STATIC_DRAW);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat),
                        (void *)0);

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
  } else {
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
    } else {
      glDrawBuffer(GL_BACK);
      renderModels(shader);
    }
  }

  glEnable(GL_DEPTH_TEST);

  // Cleanup temporary VAO/VBO
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
}

void renderLightVisualizations(Engine::Shader *shader) {
  // Render spheres for point lights
  for (size_t i = 0; i < pointLights.size(); i++) {
    const auto &light = pointLights[i];

    // Create a small sphere to represent the point light
    Engine::Model lightSphere =
        Engine::createSphere(light.color, 1.0f, light.intensity, 8, 8);
    lightSphere.position = light.position;
    lightSphere.scale = glm::vec3(0.1f); // Small sphere

    // Set shader uniforms for this light visualization
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, lightSphere.position);
    model = glm::scale(model, lightSphere.scale);

    shader->setMat4("model", model);
    shader->setBool("isPointCloud", false);

    // Set light color and emissive properties
    shader->setVec3("material.objectColor", light.color);
    shader->setFloat("material.emissive", light.intensity * 0.5f);
    shader->setFloat("material.shininess", 1.0f);
    shader->setFloat("material.hasTexture", 0.0f);

    // Render the sphere
    auto &meshes =
        const_cast<std::vector<Engine::Mesh> &>(lightSphere.getMeshes());
    for (auto &mesh : meshes) {
      mesh.Draw(*shader);
    }
  }

  // Render cylinders (as cones) for spot lights
  for (size_t i = 0; i < spotLights.size(); i++) {
    const auto &light = spotLights[i];

    // Create a cylinder to represent the spot light cone
    Engine::Model lightCone =
        Engine::createCylinder(light.color, 1.0f, light.intensity, 8);
    lightCone.position = light.position;

    // Scale to make it cone-like and orient it in the light direction
    lightCone.scale = glm::vec3(0.05f, 0.2f, 0.05f); // Thin cone

    // Calculate rotation to align with light direction
    glm::vec3 defaultDirection =
        glm::vec3(0.0f, 1.0f, 0.0f); // Cylinder default up direction
    glm::vec3 lightDirection = glm::normalize(light.direction);

    glm::vec3 axis = glm::cross(defaultDirection, lightDirection);
    float angle = glm::acos(glm::dot(defaultDirection, lightDirection));

    if (glm::length(axis) > 0.001f) {
      axis = glm::normalize(axis);
      lightCone.rotation = glm::degrees(angle) * axis;
    }

    // Set shader uniforms for this light visualization
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, lightCone.position);

    // Apply rotation
    if (glm::length(axis) > 0.001f) {
      model = glm::rotate(model, angle, axis);
    }

    model = glm::scale(model, lightCone.scale);

    shader->setMat4("model", model);
    shader->setBool("isPointCloud", false);

    // Set light color and emissive properties
    shader->setVec3("material.objectColor", light.color);
    shader->setFloat("material.emissive", light.intensity * 0.5f);
    shader->setFloat("material.shininess", 1.0f);
    shader->setFloat("material.hasTexture", 0.0f);

    // Render the cylinder (cone)
    auto &meshes =
        const_cast<std::vector<Engine::Mesh> &>(lightCone.getMeshes());
    for (auto &mesh : meshes) {
      mesh.Draw(*shader);
    }
  }
}

void updatePointLights() {
  // Point lights are now only manually created, no auto-generation from
  // emissive objects This function is kept for compatibility but does nothing
}

void updateSpaceMouseBounds() {
  // Calculate combined bounding box for models and point clouds
  // This function is called when:
  // - Models or point clouds are loaded/deleted
  // - Model/point cloud transforms (position, scale, rotation) change
  // - Scenes are loaded
  // This ensures SpaceMouse navigation bounds stay accurate without per-frame
  // updates
  glm::vec3 modelMin(FLT_MAX), modelMax(-FLT_MAX);

  // Include models in bounding box calculation
  for (const auto &model : currentScene.models) {
    for (const auto &mesh : model.getMeshes()) {
      for (const auto &vertex : mesh.vertices) {
        glm::vec3 worldPos =
            model.position + (glm::vec3(vertex.position) * model.scale);
        modelMin = glm::min(modelMin, worldPos);
        modelMax = glm::max(modelMax, worldPos);
      }
    }
  }

  // Include point clouds in bounding box calculation
  for (const auto &pointCloud : currentScene.pointClouds) {
    if (pointCloud.octreeRoot) {
      // Use octree bounds if available
      glm::vec3 pcMin =
          pointCloud.position + (pointCloud.octreeBoundsMin * pointCloud.scale);
      glm::vec3 pcMax =
          pointCloud.position + (pointCloud.octreeBoundsMax * pointCloud.scale);
      modelMin = glm::min(modelMin, pcMin);
      modelMax = glm::max(modelMax, pcMax);
    } else if (!pointCloud.points.empty()) {
      // Fall back to calculating bounds from points
      for (const auto &point : pointCloud.points) {
        glm::vec3 worldPos =
            pointCloud.position + (point.position * pointCloud.scale);
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
  static GUI::SpaceMouseAnchorMode lastAnchorMode =
      GUI::SPACEMOUSE_ANCHOR_DISABLED;

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
      shouldUpdate =
          (glm::distance(lastCursorPosition, currentCursorPosition) > 0.001f) ||
          settingChanged;
      break;
    case GUI::SPACEMOUSE_ANCHOR_ON_START:
      shouldUpdate = !spaceMouseInput.IsNavigating() &&
                     ((glm::distance(lastCursorPosition,
                                     currentCursorPosition) > 0.001f) ||
                      settingChanged);
      break;
    case GUI::SPACEMOUSE_ANCHOR_DISABLED:
    default:
      shouldUpdate = settingChanged;
      break;
    }

    if (shouldUpdate) {
      lastCursorPosition = currentCursorPosition;
      spaceMouseInput.SetCursorAnchor(currentCursorPosition,
                                      preferences.spaceMouseAnchorMode);

      // Force NavLib to refresh the pivot position
      if (preferences.spaceMouseAnchorMode != GUI::SPACEMOUSE_ANCHOR_DISABLED) {
        spaceMouseInput.RefreshPivotPosition();
      }
    }
  } else {
    // Always update the setting state even if cursor is not valid
    spaceMouseInput.SetCursorAnchor(glm::vec3(0.0f),
                                    preferences.spaceMouseAnchorMode);
  }
}

#pragma endregion

PointCloud loadPointCloudFile(const std::string &filePath,
                              size_t downsampleFactor) {
  return Engine::PointCloudLoader::loadPointCloudFile(filePath,
                                                      downsampleFactor);
}

// ---- Ray Casting ----
#pragma region Ray Casting
void calculateMouseRay(float mouseX, float mouseY, glm::vec3 &rayOrigin,
                       glm::vec3 &rayDirection, glm::vec3 &rayNear,
                       glm::vec3 &rayFar, float aspect) {
  // Convert mouse position to normalized device coordinates
  float x = (2.0f * mouseX) / windowWidth - 1.0f;
  float y = 1.0f - (2.0f * mouseY) / windowHeight;

  // Calculate near and far points in clip space
  glm::vec4 rayNearClip = glm::vec4(x, y, -1.0, 1.0);
  glm::vec4 rayFarClip = glm::vec4(x, y, 1.0, 1.0);

  // Convert to eye space
  glm::mat4 invProj = glm::inverse(camera.GetProjectionMatrix(
      aspect, preferences.nearPlane, preferences.farPlane));
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

bool rayIntersectsModel(const glm::vec3 &rayOrigin,
                        const glm::vec3 &rayDirection,
                        const Engine::Model &model, float &distance) {
  float closestDistance = std::numeric_limits<float>::max();
  bool intersected = false;
  // Calculate model matrix
  glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), model.position);
  modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x),
                            glm::vec3(1, 0, 0));
  modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y),
                            glm::vec3(0, 1, 0));
  modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z),
                            glm::vec3(0, 0, 1));
  modelMatrix = glm::scale(modelMatrix, model.scale);

  // Transform ray to model space
  glm::mat4 invModelMatrix = glm::inverse(modelMatrix);
  glm::vec3 rayOriginModel =
      glm::vec3(invModelMatrix * glm::vec4(rayOrigin, 1.0f));
  glm::vec3 rayDirectionModel =
      glm::normalize(glm::vec3(invModelMatrix * glm::vec4(rayDirection, 0.0f)));

  // Check each mesh in the model
  for (const auto &mesh : model.getMeshes()) {
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

      if (a > -0.00001f && a < 0.00001f)
        continue; // Ray is parallel to triangle

      float f = 1.0f / a;
      glm::vec3 s = rayOriginModel - v0;
      float u = f * glm::dot(s, h);

      if (u < 0.0f || u > 1.0f)
        continue;

      glm::vec3 q = glm::cross(s, edge1);
      float v = f * glm::dot(rayDirectionModel, q);

      if (v < 0.0f || u + v > 1.0f)
        continue;

      float t = f * glm::dot(edge2, q);

      if (t > 0.00001f && t < closestDistance) {
        closestDistance = t;
        intersected = true;
      }
    }
  }

  if (intersected) {
    // Transform the intersection point back to world space
    glm::vec3 intersectionPointModel =
        rayOriginModel + rayDirectionModel * closestDistance;
    glm::vec4 intersectionPointWorld =
        modelMatrix * glm::vec4(intersectionPointModel, 1.0f);
    distance = glm::distance(rayOrigin, glm::vec3(intersectionPointWorld));
    return true;
  }

  return false;
}

// Overloaded version that also returns the surface normal
bool rayIntersectsModel(const glm::vec3 &rayOrigin,
                        const glm::vec3 &rayDirection,
                        const Engine::Model &model, float &distance,
                        glm::vec3 &outNormal) {
  float closestDistance = std::numeric_limits<float>::max();
  bool intersected = false;
  glm::vec3 hitNormal;

  // Calculate model matrix
  glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), model.position);
  modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x),
                            glm::vec3(1, 0, 0));
  modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y),
                            glm::vec3(0, 1, 0));
  modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z),
                            glm::vec3(0, 0, 1));
  modelMatrix = glm::scale(modelMatrix, model.scale);

  // Transform ray to model space
  glm::mat4 invModelMatrix = glm::inverse(modelMatrix);
  glm::vec3 rayOriginModel =
      glm::vec3(invModelMatrix * glm::vec4(rayOrigin, 1.0f));
  glm::vec3 rayDirectionModel =
      glm::normalize(glm::vec3(invModelMatrix * glm::vec4(rayDirection, 0.0f)));

  // Check each mesh in the model
  for (const auto &mesh : model.getMeshes()) {
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

      if (a > -0.00001f && a < 0.00001f)
        continue; // Ray is parallel to triangle

      float f = 1.0f / a;
      glm::vec3 s = rayOriginModel - v0;
      float u = f * glm::dot(s, h);

      if (u < 0.0f || u > 1.0f)
        continue;

      glm::vec3 q = glm::cross(s, edge1);
      float v = f * glm::dot(rayDirectionModel, q);

      if (v < 0.0f || u + v > 1.0f)
        continue;

      float t = f * glm::dot(edge2, q);

      if (t > 0.00001f && t < closestDistance) {
        closestDistance = t;

        // Calculate face normal from triangle edges
        glm::vec3 faceNormal = glm::normalize(glm::cross(edge1, edge2));

        // If mesh has vertex normals, interpolate them using barycentric
        // coordinates
        float w = 1.0f - u - v; // Barycentric coordinate for v0
        if (mesh.vertices[mesh.indices[i]].normal != glm::vec3(0.0f)) {
          glm::vec3 n0 = mesh.vertices[mesh.indices[i]].normal;
          glm::vec3 n1 = mesh.vertices[mesh.indices[i + 1]].normal;
          glm::vec3 n2 = mesh.vertices[mesh.indices[i + 2]].normal;
          hitNormal = glm::normalize(w * n0 + u * n1 + v * n2);
        } else {
          hitNormal = faceNormal;
        }

        intersected = true;
      }
    }
  }

  if (intersected) {
    // Transform the intersection point back to world space
    glm::vec3 intersectionPointModel =
        rayOriginModel + rayDirectionModel * closestDistance;
    glm::vec4 intersectionPointWorld =
        modelMatrix * glm::vec4(intersectionPointModel, 1.0f);
    distance = glm::distance(rayOrigin, glm::vec3(intersectionPointWorld));

    // Transform normal to world space (use normal matrix for proper
    // transformation)
    glm::mat3 normalMatrix =
        glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    outNormal = glm::normalize(normalMatrix * hitNormal);

    return true;
  }

  return false;
}
#pragma endregion

// ---- Callbacks ----
#pragma region Callbacks
void framebuffer_size_callback(GLFWwindow *window, int width, int height) {
  glViewport(0, 0, width, height);
  windowWidth = width;
  windowHeight = height;

  // Update GUI scaling based on new window dimensions
  UpdateGuiScale(width, height);

  // Resize bloom renderer if it exists
  if (bloomRenderer) {
    bloomRenderer->resize(width, height);
  }
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  if (!ImGui::GetIO().WantCaptureMouse && !spaceMouseActive) {
    // Check if we're currently moving a model with Ctrl+drag
    if (isMovingModel && currentSelectedType == SelectedType::Model &&
        currentSelectedIndex != -1) {
      // Apply physics-based smooth scrolling to model depth movement
      float currentTime = static_cast<float>(glfwGetTime());
      float deltaTime = currentTime - lastModelScrollTime;
      lastModelScrollTime = currentTime;

      // Add momentum to velocity (similar to camera scrolling)
      modelScrollVelocity += static_cast<float>(yoffset) * modelScrollMomentum;
      modelScrollVelocity = glm::clamp(
          modelScrollVelocity, -modelMaxScrollVelocity, modelMaxScrollVelocity);
    }
    // Check if we're currently moving a point light with Ctrl+drag
    else if (isMovingModel && currentSelectedType == SelectedType::PointLight &&
             currentSelectedIndex != -1) {
      // Apply physics-based smooth scrolling to point light depth movement
      float currentTime = static_cast<float>(glfwGetTime());
      float deltaTime = currentTime - lastModelScrollTime;
      lastModelScrollTime = currentTime;

      // Add momentum to velocity (same as models)
      modelScrollVelocity += static_cast<float>(yoffset) * modelScrollMomentum;
      modelScrollVelocity = glm::clamp(
          modelScrollVelocity, -modelMaxScrollVelocity, modelMaxScrollVelocity);
    }
    // Check if we're currently moving a spot light with Ctrl+drag
    else if (isMovingModel && currentSelectedType == SelectedType::SpotLight &&
             currentSelectedIndex != -1) {
      // Apply physics-based smooth scrolling to spot light depth movement
      float currentTime = static_cast<float>(glfwGetTime());
      float deltaTime = currentTime - lastModelScrollTime;
      lastModelScrollTime = currentTime;

      // Add momentum to velocity (same as models)
      modelScrollVelocity += static_cast<float>(yoffset) * modelScrollMomentum;
      modelScrollVelocity = glm::clamp(
          modelScrollVelocity, -modelMaxScrollVelocity, modelMaxScrollVelocity);
    } else {
      // Normal camera zoom behavior
      // Update cursor info before processing scroll
      if (cursorManager.isCursorPositionValid()) {
        camera.UpdateCursorInfo(cursorManager.getCursorPosition(), true);
      } else {
        camera.UpdateCursorInfo(glm::vec3(0.0f), false);
      }
      // Pass background cursor info to camera for zoom functionality
      camera.ProcessMouseScroll(yoffset,
                                cursorManager.getBackgroundCursorPosition(),
                                cursorManager.hasBackgroundCursorPosition());
    }
  }
}

void mouse_button_callback(GLFWwindow *window, int button, int action,
                           int mods) {
  if (ImGui::GetIO().WantCaptureMouse) {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    return;
  }

  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    if (action == GLFW_PRESS) {
      // Check if Ctrl or Alt is held down
      bool ctrlPressed = (mods & GLFW_MOD_CONTROL);
      bool altPressed = (mods & GLFW_MOD_ALT);

      // Handle brush tool painting (when enabled and no modifiers pressed)
      if (!ctrlPressed && !altPressed &&
          preferences.brushToolSettings.enabled &&
          preferences.brushToolSettings.selectedModelIndex >= 0) {

        glm::vec3 rayOrigin, rayDirection, rayNear, rayFar;
        calculateMouseRay(lastX, lastY, rayOrigin, rayDirection, rayNear,
                          rayFar, (float)windowWidth / (float)windowHeight);

        float closestDistance = std::numeric_limits<float>::max();
        glm::vec3 hitPosition;
        glm::vec3 hitNormal;
        bool hitFound = false;

        // Check intersection with all models for painting surface
        for (int i = 0; i < currentScene.models.size(); i++) {
          const auto &model = currentScene.models[i];
          float distance;
          glm::vec3 surfaceNormal;
          if (rayIntersectsModel(rayOrigin, rayDirection, model, distance,
                                 surfaceNormal)) {
            if (distance < closestDistance) {
              closestDistance = distance;
              hitPosition = rayOrigin + rayDirection * distance;
              hitNormal = surfaceNormal; // Use the actual surface normal
              hitFound = true;
            }
          }
        }

        // If we hit a surface, paint an instance
        if (hitFound) {
          // Get the source model's scale
          glm::vec3 sourceModelScale =
              currentScene
                  .models[preferences.brushToolSettings.selectedModelIndex]
                  .scale;
          brushTool.paintInstance(hitPosition, hitNormal, camera.Position,
                                  sourceModelScale);
        }
      } else if (ctrlPressed || altPressed) {
        glm::vec3 rayOrigin, rayDirection, rayNear, rayFar;
        calculateMouseRay(lastX, lastY, rayOrigin, rayDirection, rayNear,
                          rayFar, (float)windowWidth / (float)windowHeight);

        float closestDistance = std::numeric_limits<float>::max();
        int closestModelIndex = -1;
        int closestPointCloudIndex = -1;

        // Check intersection with models
        for (int i = 0; i < currentScene.models.size(); i++) {
          const auto &model = currentScene.models[i];
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
          const auto &pointCloud = currentScene.pointClouds[i];
          float distance = glm::length(pointCloud.position - rayOrigin);
          if (distance < closestDistance) {
            closestDistance = distance;
            closestPointCloudIndex = i;
            closestModelIndex = -1;
          }
        }

        int closestPointLightIndex = -1;
        int closestSpotLightIndex = -1;

        // Check intersection with point light spheres (when Ctrl is pressed)
        for (int i = 0; i < pointLights.size(); i++) {
          const auto &light = pointLights[i];
          float sphereRadius = 0.1f; // Same as visualization sphere

          // Ray-sphere intersection
          glm::vec3 oc = rayOrigin - light.position;
          float a = glm::dot(rayDirection, rayDirection);
          float b = 2.0f * glm::dot(oc, rayDirection);
          float c = glm::dot(oc, oc) - sphereRadius * sphereRadius;
          float discriminant = b * b - 4 * a * c;

          if (discriminant >= 0) {
            float t = (-b - sqrt(discriminant)) / (2.0f * a);
            if (t > 0 && t < closestDistance) {
              closestDistance = t;
              closestPointLightIndex = i;
              closestSpotLightIndex = -1;
              closestModelIndex = -1;
              closestPointCloudIndex = -1;
            }
          }
        }

        // Check intersection with spot light cones (simplified as cylinders)
        for (int i = 0; i < spotLights.size(); i++) {
          const auto &light = spotLights[i];
          float cylinderRadius = 0.05f; // Same as visualization cone
          float cylinderHeight = 0.2f;

          // Simplified ray-cylinder intersection (treat as sphere for now)
          glm::vec3 oc = rayOrigin - light.position;
          float effectiveRadius =
              cylinderRadius + cylinderHeight * 0.5f; // Approximate
          float a = glm::dot(rayDirection, rayDirection);
          float b = 2.0f * glm::dot(oc, rayDirection);
          float c = glm::dot(oc, oc) - effectiveRadius * effectiveRadius;
          float discriminant = b * b - 4 * a * c;

          if (discriminant >= 0) {
            float t = (-b - sqrt(discriminant)) / (2.0f * a);
            if (t > 0 && t < closestDistance) {
              closestDistance = t;
              closestSpotLightIndex = i;
              closestPointLightIndex = -1;
              closestModelIndex = -1;
              closestPointCloudIndex = -1;
            }
          }
        }

        if (closestModelIndex != -1) {
          // Handle Alt+drag duplication
          if (altPressed) {
            // Duplicate the model at the same position
            Engine::Model duplicatedModel =
                currentScene.models[closestModelIndex];
            duplicatedModel.name += "_Copy";
            currentScene.models.push_back(duplicatedModel);

            // Select the new duplicated model for moving
            currentSelectedIndex = currentScene.models.size() - 1;
            std::cout << "Model duplicated: " << duplicatedModel.name
                      << std::endl;

            // Mark voxelizer dirty for re-voxelization
            if (voxelizer) {
              voxelizer->markDirty();
            }
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

            // Calculate and store the grab point on the model
            // This is the world-space position where the ray intersects the
            // model
            glm::vec3 grabRayOrigin, grabRayDirection, grabRayNear, grabRayFar;
            calculateMouseRay(lastX, lastY, grabRayOrigin, grabRayDirection,
                              grabRayNear, grabRayFar,
                              (float)windowWidth / (float)windowHeight);
            float grabDistance;
            if (rayIntersectsModel(grabRayOrigin, grabRayDirection,
                                   currentScene.models[currentSelectedIndex],
                                   grabDistance)) {
              modelGrabPoint = grabRayOrigin + grabRayDirection * grabDistance;
              hasModelGrabPoint = true;
            } else {
              // Fallback: use model center as grab point
              modelGrabPoint =
                  currentScene.models[currentSelectedIndex].position;
              hasModelGrabPoint = true;
            }
          }
        } else if (closestPointLightIndex != -1) {
          currentSelectedType = SelectedType::PointLight;
          currentSelectedIndex = closestPointLightIndex;
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
        } else if (closestSpotLightIndex != -1) {
          currentSelectedType = SelectedType::SpotLight;
          currentSelectedIndex = closestSpotLightIndex;
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
        } else if (closestPointCloudIndex != -1) {
          currentSelectedType = SelectedType::PointCloud;
          currentSelectedIndex = closestPointCloudIndex;
          currentSelectedMeshIndex =
              -1; // Always reset mesh selection for point clouds
        } else {
          // Ctrl+Clicked empty space or non-model object
          isMovingModel = false;
        }
      }

      // Handle double-click (if not in selection mode and brush tool not
      // enabled)
      if (!selectionMode && !preferences.brushToolSettings.enabled) {
        double currentTime = glfwGetTime();
        if (currentTime - lastClickTime < doubleClickTime) {
          if (cursorManager.isCursorPositionValid()) {
            // Double click on geometry - center on that point
            camera.StartCenteringAnimation(cursorManager.getCursorPosition());
          } else {
            // Double click on empty space - center view at default distance
            glm::vec3 centerPoint =
                camera.Position + camera.Front * camera.OrbitDistance;
            camera.StartCenteringAnimation(centerPoint);
          }
          // Don't set cursor position here - let it be handled after
          // centering animation completes The cursor will naturally be at
          // screen center since we're centering the view on the cursor
          // position
        }
        lastClickTime = currentTime;
      }

      // Handle camera orbiting (if not animating, not in selection mode, and
      // brush tool not enabled)
      if (!camera.IsAnimating && !camera.IsOrbiting && !selectionMode &&
          !preferences.brushToolSettings.enabled) {
        leftMousePressed = true;

        if (cursorManager.isCursorPositionValid()) {
          // Different orbiting behaviors based on settings
          if (camera.orbitAroundCursor) {
            camera.UpdateCursorInfo(cursorManager.getCursorPosition(), true);
            camera.StartOrbiting(
                true); // Pass true to use current cursor position
            capturedCursorPos = cursorManager.getCursorPosition();
            cursorManager.setCapturedCursorPositionWithSync(capturedCursorPos);

            // Capture cursor state for synchronization
            Core::CursorSyncManager::getInstance().captureState(
                capturedCursorPos, Core::CameraOperationType::Orbiting,
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
                camera.GetViewMatrix(), windowWidth, windowHeight);

            // Enable mouse capture when orbiting starts
            isMouseCaptured = true;
            firstMouse = true; // Reset the first mouse flag
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            // Center cursor
            glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
          } else if (orbitFollowsCursor) {
            camera.StartCenteringAnimation(cursorManager.getCursorPosition());
            capturedCursorPos = cursorManager.getCursorPosition();
            cursorManager.setCapturedCursorPositionWithSync(capturedCursorPos);

            // Capture cursor state for synchronization
            Core::CursorSyncManager::getInstance().captureState(
                capturedCursorPos, Core::CameraOperationType::Orbiting,
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
                camera.GetViewMatrix(), windowWidth, windowHeight);

            isMouseCaptured = true;
            firstMouse = true;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
          } else {
            // Standard orbiting mode - preserve cursor 3D position and
            // convert to 2D after orbiting
            std::cout << "Standard orbit mode - will convert 3D cursor "
                         "position to 2D after orbiting"
                      << std::endl;

            // Calculate orbit point based on cursor depth
            float cursorDepth = glm::length(cursorManager.getCursorPosition() -
                                            camera.Position);
            glm::vec3 viewportCenter =
                camera.Position + camera.Front * cursorDepth;
            camera.SetOrbitPointDirectly(viewportCenter);
            camera.OrbitDistance = cursorDepth;

            // Capture the original 3D cursor position for conversion after
            // orbiting
            capturedCursorPos = cursorManager.getCursorPosition();
            cursorManager.setCapturedCursorPositionWithSync(capturedCursorPos);

            // Don't capture state for synchronization in standard mode
            // This will cause the 3D-to-2D conversion behavior to be used

            camera.StartOrbiting();
            // Enable mouse capture when orbiting starts
            isMouseCaptured = true;
            firstMouse = true; // Reset the first mouse flag
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            // Center cursor
            glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
          }
        } else {
          // When no valid cursor position, calculate a reasonable orbit point
          glm::vec3 orbitPoint =
              camera.Position + camera.Front * camera.OrbitDistance;
          camera.SetOrbitPointDirectly(orbitPoint);

          // For synchronization, use a position in front of the camera at a
          // reasonable distance This will be where the cursor appears after
          // orbiting completes
          capturedCursorPos =
              camera.Position + camera.Front * (camera.OrbitDistance * 0.8f);
          cursorManager.setCapturedCursorPositionWithSync(capturedCursorPos);

          // Capture cursor state for synchronization
          Core::CursorSyncManager::getInstance().captureState(
              capturedCursorPos, Core::CameraOperationType::Orbiting,
              camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
              camera.GetViewMatrix(), windowWidth, windowHeight);

          camera.StartOrbiting();
          // Enable mouse capture when orbiting starts
          isMouseCaptured = true;
          firstMouse = true; // Reset the first mouse flag
          glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          // Center cursor
          glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
        }
      }
    } else if (action == GLFW_RELEASE) {
      // Check if we were moving a model before processing the release
      bool wasMovingModel = isMovingModel;

      if (isMouseCaptured) {
        // Disable mouse capture when orbiting ends
        isMouseCaptured = false;
        firstMouse = true; // Reset first mouse flag for next time

        // Temporarily set to normal mode to allow cursor position changes
        // The cursor manager will set the final mode after position sync
        glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
      }

      leftMousePressed = false;
      camera.StopOrbiting();

      // Handle cursor positioning after orbiting based on orbit mode
      if (!wasMovingModel) {
        if (Core::CursorSyncManager::getInstance().needsSynchronization()) {
          // Check which mode we're in based on camera settings
          if (camera.orbitAroundCursor) {
            // Around cursor mode - use synchronization to restore cursor to
            // 3D position
            std::cout << "[CursorFix] Around cursor mode - using cursor "
                         "synchronization"
                      << std::endl;
            // Note: Pass false for stereo mode since stereo matrices aren't available here.
            // Using mono projection for cursor positioning is sufficient and avoids
            // incorrect averaging with invalid default stereo matrices.
            Core::CursorSynchronizer::synchronizeCursorPosition(
                window,
                Core::CursorSyncManager::getInstance().getWorldPosition(),
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
                camera.GetViewMatrix(), windowWidth, windowHeight,
                false);
            Core::CursorSyncManager::getInstance().markSynchronized();
          } else if (orbitFollowsCursor) {
            // Center cursor mode - cursor should be at viewport center after
            // centering animation
            glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
            Core::CursorSyncManager::getInstance().markSynchronized();
          } else {
            // This shouldn't happen, but fallback to synchronization
            // Note: Pass false for stereo mode since stereo matrices aren't available here.
            Core::CursorSynchronizer::synchronizeCursorPosition(
                window,
                Core::CursorSyncManager::getInstance().getWorldPosition(),
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
                camera.GetViewMatrix(), windowWidth, windowHeight,
                false);
            Core::CursorSyncManager::getInstance().markSynchronized();
          }
        } else {
          // Standard orbit mode - calculate new cursor position based on
          // captured 3D cursor
          if (cursorManager.isCursorPositionValid()) {
            glm::vec3 originalCursorPos =
                capturedCursorPos; // Use the captured position from before
                                   // orbiting

            // Project the original 3D cursor position to screen coordinates
            // with the new camera view after orbiting
            // Note: Pass false for stereo mode since stereo matrices aren't available here.
            Core::CursorSynchronizer::synchronizeCursorPosition(
                window, originalCursorPos,
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
                camera.GetViewMatrix(), windowWidth, windowHeight,
                false);
          } else {
            // No valid cursor position - fallback to screen center
            glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
          }
        }
      } else {
        // Model was being moved - synchronize cursor position to final grab
        // point
        if (hasModelGrabPoint) {
          // Project the final grab point position to screen coordinates
          glm::mat4 viewMatrix = camera.GetViewMatrix();
          glm::mat4 projMatrix = camera.GetProjectionMatrix(
              aspectRatio, preferences.nearPlane, preferences.farPlane);
          glm::mat4 viewProjMatrix = projMatrix * viewMatrix;

          glm::vec4 grabScreenPos =
              viewProjMatrix * glm::vec4(modelGrabPoint, 1.0f);
          if (grabScreenPos.w > 0.0f) {
            grabScreenPos /= grabScreenPos.w;

            // Convert NDC to screen coordinates
            float screenX = (grabScreenPos.x + 1.0f) * 0.5f * windowWidth;
            float screenY = (1.0f - grabScreenPos.y) * 0.5f * windowHeight;

            // Set cursor position to the final grab point location
            glfwSetCursorPos(window, screenX, screenY);

            // Update lastX/lastY
            lastX = screenX;
            lastY = screenY;
          }
        }
      }

      // Reset model grab point tracking
      hasModelGrabPoint = false;
      isMovingModel = false;
      selectionMode = false;
    }
  } else if (button == GLFW_MOUSE_BUTTON_MIDDLE) {
    if (action == GLFW_PRESS) {
      middleMousePressed = true;

      // Capture cursor state for synchronization before starting panning
      glm::vec3 cursorPos =
          cursorManager.isCursorPositionValid()
              ? cursorManager.getCursorPosition()
              : camera.Position + camera.Front * camera.OrbitDistance;

      Core::CursorSyncManager::getInstance().captureState(
          cursorPos, Core::CameraOperationType::Panning,
          camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
          camera.GetViewMatrix(), windowWidth, windowHeight);

      camera.StartPanning();
      // Enable mouse capture for middle button panning
      isMouseCaptured = true;
      firstMouse = true; // Reset the first mouse flag
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
      // Center cursor
      glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
    } else if (action == GLFW_RELEASE) {
      middleMousePressed = false;
      camera.StopPanning();

      // Synchronize cursor position after panning
      // Note: Pass false for stereo mode since stereo matrices aren't available here.
      if (Core::CursorSyncManager::getInstance().needsSynchronization()) {
        Core::CursorSynchronizer::synchronizeCursorPosition(
            window, Core::CursorSyncManager::getInstance().getWorldPosition(),
            camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
            camera.GetViewMatrix(), windowWidth, windowHeight, false);
        Core::CursorSyncManager::getInstance().markSynchronized();
      }

      // Disable mouse capture
      isMouseCaptured = false;
      firstMouse = true; // Reset first mouse flag for next time
    }
  } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    if (action == GLFW_PRESS) {
      rightMousePressed = true;

      // Capture cursor state for synchronization before starting rotation
      glm::vec3 cursorPos =
          cursorManager.isCursorPositionValid()
              ? cursorManager.getCursorPosition()
              : camera.Position + camera.Front * camera.OrbitDistance;

      Core::CursorSyncManager::getInstance().captureState(
          cursorPos, Core::CameraOperationType::Rotating,
          camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
          camera.GetViewMatrix(), windowWidth, windowHeight);

      // Enable mouse capture for right button rotation
      isMouseCaptured = true;
      firstMouse = true; // Reset the first mouse flag
      glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

      // Center cursor
      glfwSetCursorPos(window, windowWidth / 2.0f, windowHeight / 2.0f);
    } else if (action == GLFW_RELEASE) {
      rightMousePressed = false;

      // Synchronize cursor position after rotation
      // Note: Pass false for stereo mode since stereo matrices aren't available here.
      if (Core::CursorSyncManager::getInstance().needsSynchronization()) {
        Core::CursorSynchronizer::synchronizeCursorPosition(
            window, Core::CursorSyncManager::getInstance().getWorldPosition(),
            camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane),
            camera.GetViewMatrix(), windowWidth, windowHeight, false);
        Core::CursorSyncManager::getInstance().markSynchronized();
      }

      // Disable mouse capture
      isMouseCaptured = false;
      firstMouse = true; // Reset first mouse flag for next time
    }
  }
}

void mouse_callback(GLFWwindow *window, double xposIn, double yposIn) {
  // --- Early exit if window doesn't have focus ---
  if (!windowHasFocus) {
    return;
  }

  float xpos = static_cast<float>(xposIn);
  float ypos = static_cast<float>(yposIn);

  // --- Handle first mouse input OR first input after regaining focus ---
  if (firstMouse || justRegainedFocus) {
    // This is the first event, or the first one after regaining focus.
    // Treat the current position as the starting point. Don't calculate
    // offset.
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
  // If ImGui wants the mouse *after* we've calculated delta and updated
  // lastX/Y
  if (ImGui::GetIO().WantCaptureMouse) {
    // Don't accumulate the delta if ImGui wants it
    // Reset accumulators to prevent processing stale input on next non-ImGui
    // frame
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

void key_callback(GLFWwindow *window, int key, int scancode, int action,
                  int mods) {
  // Note: This handles special keys like GUI toggle, lighting mode changes
  // Movement keys (WASD, Space, Shift, Escape) are handled in
  // Input::handleKeyInput They remain hardcoded and are not customizable
  // through the shortcut manager

  // Handle Ctrl key separately (since it's used as a modifier)
  if (key == GLFW_KEY_LEFT_CONTROL || key == GLFW_KEY_RIGHT_CONTROL) {
    if (action == GLFW_PRESS) {
      ctrlPressed = true;
      selectionMode = true;
    } else if (action == GLFW_RELEASE) {
      ctrlPressed = false;
      selectionMode = false;
      // Don't stop dragging when Ctrl is released - only stop when mouse
      // button is released
    }
    // Ignore GLFW_REPEAT for Ctrl key - just keep the current state
    return;
  }

  if (action != GLFW_PRESS) {
    return; // Only handle PRESS actions below for other keys
  }

  // Check if this key press matches any shortcut action
  auto actionOpt = shortcutManager.getActionForKey(key, mods);

  if (actionOpt.has_value()) {
    StereoVista::ShortcutAction shortcutAction = actionOpt.value();

    // Dispatch to appropriate action handler
    switch (shortcutAction) {
    case StereoVista::ShortcutAction::ToggleGUI:
      showGui = !showGui;
      std::cout << "GUI visibility toggled. showGui = "
                << (showGui ? "true" : "false") << std::endl;
      break;

    case StereoVista::ShortcutAction::CycleLighting:
      // Cycle through lighting modes: Shadow Mapping -> Voxel Cone Tracing ->
      // Radiance -> Shadow Mapping
      if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
        currentLightingMode = GUI::LIGHTING_VOXEL_CONE_TRACING;
      } else if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
        currentLightingMode = GUI::LIGHTING_RADIANCE;
      } else {
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
      break;

    case StereoVista::ShortcutAction::ToggleShadows:
      enableShadows = !enableShadows;
      std::cout << "Shadows " << (enableShadows ? "enabled" : "disabled")
                << std::endl;
      preferences.enableShadows = enableShadows;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleVoxelViz:
      voxelizer->showDebugVisualization = !voxelizer->showDebugVisualization;
      std::cout << "Voxel visualization "
                << (voxelizer->showDebugVisualization ? "enabled" : "disabled")
                << std::endl;
      break;

    case StereoVista::ShortcutAction::CenterView: {
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
      for (const auto &model : currentScene.models) {
        centerPoint += model.position;
        objectCount++;
      }

      for (const auto &pointCloud : currentScene.pointClouds) {
        centerPoint += pointCloud.position;
        objectCount++;
      }

      if (objectCount > 0) {
        centerPoint /= objectCount;
        camera.StartCenteringAnimation(centerPoint);
        glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
        std::cout << "Centering on scene midpoint" << std::endl;
      } else {
        // If no objects, center on the world origin
        camera.StartCenteringAnimation(glm::vec3(0.0f));
        glfwSetCursorPos(window, windowWidth / 2, windowHeight / 2);
        std::cout << "Centering on world origin" << std::endl;
      }
    } break;

    // View/Display Controls
    case StereoVista::ShortcutAction::ToggleFPS:
      showFPS = !showFPS;
      std::cout << "FPS counter " << (showFPS ? "enabled" : "disabled")
                << std::endl;
      break;

    case StereoVista::ShortcutAction::ToggleWireframe:
      camera.wireframe = !camera.wireframe;
      std::cout << "Wireframe mode "
                << (camera.wireframe ? "enabled" : "disabled") << std::endl;
      break;

    case StereoVista::ShortcutAction::ToggleRadar:
      preferences.radarEnabled = !preferences.radarEnabled;
      std::cout << "Radar "
                << (preferences.radarEnabled ? "enabled" : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleZeroPlane:
      preferences.showZeroPlane = !preferences.showZeroPlane;
      std::cout << "Zero plane "
                << (preferences.showZeroPlane ? "enabled" : "disabled")
                << std::endl;
      savePreferences();
      break;

    // Camera Controls
    case StereoVista::ShortcutAction::ToggleZoomToCursor:
      camera.zoomToCursor = !camera.zoomToCursor;
      std::cout << "Zoom to cursor "
                << (camera.zoomToCursor ? "enabled" : "disabled") << std::endl;
      break;

    case StereoVista::ShortcutAction::ToggleOrbitAroundCursor:
      camera.orbitAroundCursor = !camera.orbitAroundCursor;
      std::cout << "Orbit around cursor "
                << (camera.orbitAroundCursor ? "enabled" : "disabled")
                << std::endl;
      break;

    case StereoVista::ShortcutAction::ToggleSpaceMouseMode:
      if (spaceMouseInitialized && preferences.spaceMouseEnabled) {
        // SpaceMouse is always in CAD (Pivot) mode
        std::cout << "SpaceMouse mode: CAD (Pivot)" << std::endl;
      } else {
        std::cout << "SpaceMouse not available" << std::endl;
      }
      break;

    // Lighting
    case StereoVista::ShortcutAction::ToggleHDR:
      preferences.hdrSettings.enabled = !preferences.hdrSettings.enabled;
      std::cout << "HDR "
                << (preferences.hdrSettings.enabled ? "enabled" : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleBloom:
      preferences.hdrSettings.enableBloom =
          !preferences.hdrSettings.enableBloom;
      std::cout << "Bloom "
                << (preferences.hdrSettings.enableBloom ? "enabled"
                                                        : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::TogglePCSS:
      preferences.shadowSettings.enablePCSS =
          !preferences.shadowSettings.enablePCSS;
      std::cout << "PCSS (soft shadows) "
                << (preferences.shadowSettings.enablePCSS ? "enabled"
                                                          : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleSunLight:
      sun.enabled = !sun.enabled;
      std::cout << "Sun light " << (sun.enabled ? "enabled" : "disabled")
                << std::endl;
      break;

    // Materials & Rendering
    case StereoVista::ShortcutAction::TogglePBR:
      preferences.materialSettings.enablePBR =
          !preferences.materialSettings.enablePBR;
      std::cout << "PBR materials "
                << (preferences.materialSettings.enablePBR ? "enabled"
                                                           : "disabled")
                << std::endl;
      savePreferences();
      break;

    // VCT
    case StereoVista::ShortcutAction::ToggleVCTIndirectDiffuse:
      preferences.vctSettings.indirectDiffuseLight =
          !preferences.vctSettings.indirectDiffuseLight;
      std::cout << "VCT indirect diffuse "
                << (preferences.vctSettings.indirectDiffuseLight ? "enabled"
                                                                 : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleVCTIndirectSpecular:
      preferences.vctSettings.indirectSpecularLight =
          !preferences.vctSettings.indirectSpecularLight;
      std::cout << "VCT indirect specular "
                << (preferences.vctSettings.indirectSpecularLight ? "enabled"
                                                                  : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleVCTDirectLight:
      preferences.vctSettings.directLight =
          !preferences.vctSettings.directLight;
      std::cout << "VCT direct lighting "
                << (preferences.vctSettings.directLight ? "enabled"
                                                        : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleVCTSoftShadows:
      preferences.vctSettings.shadows = !preferences.vctSettings.shadows;
      std::cout << "VCT soft shadows "
                << (preferences.vctSettings.shadows ? "enabled" : "disabled")
                << std::endl;
      savePreferences();
      break;

    // Raytracing
    case StereoVista::ShortcutAction::ToggleRaytracing:
      preferences.radianceSettings.enableRaytracing =
          !preferences.radianceSettings.enableRaytracing;
      std::cout << "Raytracing "
                << (preferences.radianceSettings.enableRaytracing ? "enabled"
                                                                  : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleIndirectLighting:
      preferences.radianceSettings.enableIndirectLighting =
          !preferences.radianceSettings.enableIndirectLighting;
      std::cout << "Indirect lighting "
                << (preferences.radianceSettings.enableIndirectLighting
                        ? "enabled"
                        : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleEmissiveLighting:
      preferences.radianceSettings.enableEmissiveLighting =
          !preferences.radianceSettings.enableEmissiveLighting;
      std::cout << "Emissive lighting "
                << (preferences.radianceSettings.enableEmissiveLighting
                        ? "enabled"
                        : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleBVH:
      preferences.radianceSettings.enableBVH =
          !preferences.radianceSettings.enableBVH;
      std::cout << "BVH acceleration "
                << (preferences.radianceSettings.enableBVH ? "enabled"
                                                           : "disabled")
                << std::endl;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ClearIrradianceCache:
      if (irradianceCache && irradianceCache->isInitialized()) {
        irradianceCache->clear();
        std::cout << "Irradiance cache cleared - will repopulate on next frame" << std::endl;
      } else {
        std::cout << "Irradiance cache not initialized" << std::endl;
      }
      break;

    // 3D Cursor
    case StereoVista::ShortcutAction::ToggleSphereCursor: {
      auto *sphereCursor = cursorManager.getSphereCursor();
      if (sphereCursor) {
        sphereCursor->setVisible(!sphereCursor->isVisible());
        std::cout << "3D sphere cursor "
                  << (sphereCursor->isVisible() ? "enabled" : "disabled")
                  << std::endl;
      }
    } break;

    case StereoVista::ShortcutAction::ToggleCircleCursor: {
      auto *fragmentCursor = cursorManager.getFragmentCursor();
      if (fragmentCursor) {
        fragmentCursor->setVisible(!fragmentCursor->isVisible());
        std::cout << "2D circle cursor "
                  << (fragmentCursor->isVisible() ? "enabled" : "disabled")
                  << std::endl;
      }
    } break;

    case StereoVista::ShortcutAction::TogglePlaneCursor: {
      auto *planeCursor = cursorManager.getPlaneCursor();
      if (planeCursor) {
        planeCursor->setVisible(!planeCursor->isVisible());
        std::cout << "Surface plane cursor "
                  << (planeCursor->isVisible() ? "enabled" : "disabled")
                  << std::endl;
      }
    } break;

    // Window Management
    case StereoVista::ShortcutAction::OpenSettings:
      showSettingsWindow = true;
      std::cout << "Opening settings window" << std::endl;
      break;

    case StereoVista::ShortcutAction::OpenCursorSettings:
      showCursorSettingsWindow = true;
      std::cout << "Opening cursor settings" << std::endl;
      break;

    case StereoVista::ShortcutAction::OpenBrushTool:
      showBrushToolWindow = true;
      std::cout << "Opening brush tool window" << std::endl;
      break;

    // File Operations - these will trigger file dialogs through GUI
    case StereoVista::ShortcutAction::ImportModel:
      std::cout << "Import model shortcut triggered" << std::endl;
      // File dialog will be opened via GUI system
      break;

    case StereoVista::ShortcutAction::ImportPointCloud:
      std::cout << "Import point cloud shortcut triggered" << std::endl;
      // File dialog will be opened via GUI system
      break;

    case StereoVista::ShortcutAction::SaveScene:
      std::cout << "Save scene shortcut triggered" << std::endl;
      // File dialog will be opened via GUI system
      break;

    case StereoVista::ShortcutAction::LoadScene:
      std::cout << "Load scene shortcut triggered" << std::endl;
      // File dialog will be opened via GUI system
      break;

    case StereoVista::ShortcutAction::ResetCamera:
      camera.SetState(currentScene.cameraState);
      std::cout << "Camera reset to scene default" << std::endl;
      break;

    // Object Manipulation
    case StereoVista::ShortcutAction::DeleteObject:
      if (currentSelectedType == SelectedType::Model &&
          currentSelectedIndex >= 0 &&
          currentSelectedIndex < currentScene.models.size()) {
        std::cout << "Deleting selected model: "
                  << currentScene.models[currentSelectedIndex].name
                  << std::endl;

        // Remove the selected model from the scene
        currentScene.models.erase(currentScene.models.begin() +
                                  currentSelectedIndex);

        // Adjust selection indices after deletion
        if (currentScene.models.empty()) {
          currentSelectedIndex = -1;
          currentSelectedType = SelectedType::None;
        } else if (currentSelectedIndex >= currentScene.models.size()) {
          currentSelectedIndex = currentScene.models.size() - 1;
        }

        // Also update currentModelIndex if it was pointing to the deleted
        // model
        if (currentModelIndex == currentSelectedIndex) {
          currentModelIndex = currentSelectedIndex;
        } else if (currentModelIndex > currentSelectedIndex) {
          currentModelIndex--;
        }

        std::cout << "Model deleted successfully. Remaining models: "
                  << currentScene.models.size() << std::endl;
      } else {
        std::cout << "No model selected or invalid selection" << std::endl;
      }
      break;

    default:
      break;
    }
  }
}
#pragma endregion

// ---- Cursor Synchronization Test Functions ----
#pragma region Cursor Synchronization Tests

void printCursorSyncDiagnostics() {
  if (cursorManager.isCursorPositionValid()) {
    glm::vec3 cursorPos = cursorManager.getCursorPosition();
    glm::mat4 projection =
        camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane, preferences.farPlane);
    glm::mat4 view = camera.GetViewMatrix();

    Core::CursorSynchronizer::printDiagnostics(cursorPos, projection, view,
                                               windowWidth, windowHeight);
  } else {
    std::cout << "No valid cursor position for diagnostics" << std::endl;
  }
}

#pragma endregion