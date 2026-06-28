// ---- Core Definitions ----
#define NOMINMAX
#include "Engine/Core.h"
#include <array>
#include <atomic>
#include <iostream>
#include <thread>

// ---- Project-Specific Includes ----
#include "../headers/Engine/BVH.h"
#include "../headers/Engine/BVHDebug.h"
#include "../headers/Engine/BloomRenderer.h"
#include "../headers/Engine/ComputePointCloudRenderer.h"
#include "../headers/Engine/DDGIVolume.h"
#include "../headers/Engine/SSAORenderer.h"
#include "Core/Camera.h"
#include "Core/CursorSyncState.h"
#include "Core/CursorSynchronizer.h"
#include "Core/SceneManager.h"
#include "Core/SnapshotManager.h"
#include "Core/UndoManager.h"
#include "Core/Voxalizer.h"
#include "Cursors/Base/CursorManager.h"
#include "Cursors/CursorPresets.h"
#include "Engine/OctreePointCloudManager.h"
#include "Engine/Screenshot.h"
#include "Engine/ShortcutManager.h"
#include "Engine/SpaceMouseInput.h"
#include "Engine/ThreeDConnexionSync.h"
#include "Gui/CursorPreview3D.h"
#include "Gui/Gui.h"
#include "Gui/GuiTypes.h"
#include "Loaders/ModelLoader.h"
#include "Loaders/PointCloudLoader.h"
#include "Tools/BrushTool.h"
#include "Tools/MeasurementTool.h"
#include "Tools/ClipPlaneTool.h"
#include "Tools/TransformGizmo.h"
#include "Plugins/PluginManager.h"

// ---- OpenXR (Windows only; zero overhead when disabled) ----
#ifdef _WIN32
#include "Engine/XRSession.h"
#define GLFW_EXPOSE_NATIVE_WIN32
#define GLFW_EXPOSE_NATIVE_WGL
#include <GLFW/glfw3native.h>
#endif

// ---- GUI and Dialog ----
#include "imgui/imgui_incl.h"
#include "imgui/imgui_sytle.h"
#include <portable-file-dialogs.h>

// ---- Utility Libraries ----
#include <cmath>
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
void drop_callback(GLFWwindow *window, int count, const char **paths);

// ---- Rendering Functions ----
void renderEye(GLenum drawBuffer, const glm::mat4 &projection,
               const glm::mat4 &view, Engine::Shader *shader,
               ImGuiViewportP *viewport, ImGuiWindowFlags windowFlags,
               GLFWwindow *window, bool renderGUIFlag = true,
               bool isStereo = false, const glm::mat4 *leftProjection = nullptr,
               const glm::mat4 *leftView = nullptr,
               const glm::mat4 *rightProjection = nullptr,
               const glm::mat4 *rightView = nullptr);
void renderModels(Engine::Shader *shader, const glm::mat4 &viewProj,
                  bool enableFrustumCulling = true);
void renderPointClouds(Engine::Shader *shader, const glm::mat4 &view,
                       const glm::mat4 &projection);
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

// ---- Transform Gizmo glue (defined alongside the input callbacks) ----
static void bindGizmoTargetToSelection();
static void beginGizmoDrag(Tools::TransformGizmo::Handle handle,
                           const glm::vec3 &rayOrigin, const glm::vec3 &rayDir);
static void finishGizmoDrag();
#pragma endregion

// ---- Global Variables ----
#pragma region Global Variables
// ---- Scene Management ----
Engine::Scene currentScene;
int currentModelIndex = -1;
// Path of the .scene file backing the live scene ("" = untitled / never saved).
// Used by quick-save (Ctrl+S) so it can re-save without prompting, and by the
// Scene Manager panel / window title. g_sceneDirty tracks whether the live
// scene has unsaved edits since the last save/load (set via the UndoManager
// modified callback and the load/import paths, cleared on save/load).
std::string g_currentScenePath;
bool g_sceneDirty = false;
std::string modelPath = "D:/OBJ/motorbike.obj";
static char modelPathBuffer[256] = ""; // Buffer for ImGui model path input

// ---- Camera Configuration ----
Camera camera(glm::vec3(0.0f, 0.0f, 3.0f));
// Separate camera for SpaceMouse to prevent navlib from overriding normal input
Camera spaceMouseCamera(glm::vec3(0.0f, 0.0f, 3.0f));
std::shared_ptr<Camera> spaceMouseCameraPtr =
    std::make_shared<Camera>(spaceMouseCamera);
SpaceMouseInput spaceMouseInput;
ThreeDConnexionSync tdxSync;
int tdxPollCounter = 0;
bool spaceMouseInitialized = false;
bool spaceMouseActive = false;
glm::vec3 spaceMouseClickAnchor = glm::vec3(0.0f);
bool spaceMouseClickAnchorSet = false;
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

// ---- Screenshot / Image Export ----
// Set to request a screenshot on the next frame. When g_screenshotPath is empty
// the image is auto-saved to the "screenshots" folder with a timestamped name;
// otherwise it is written to the chosen path. Both the GUI (File > Save
// Screenshot) and the keyboard shortcut set these.
bool g_requestScreenshot = false;
std::string g_screenshotPath;

// Capture a screenshot honoring the configured stereo screenshot mode. For
// MONO (or any non-stereo window) this is the legacy single-eye capture. The
// stereo modes read both back buffers and write a combined Full-SBS /
// Above-Below image, or two separate "_L"/"_R" files. `flipEyes` accounts for
// the left/right buffer swap so the saved image always has the true left eye on
// the left (or top). Returns true on success.
static bool captureScreenshotForMode(const std::string &path, int x, int width,
                                     int height, bool isStereoWindow,
                                     bool flipEyes,
                                     GUI::StereoScreenshotMode mode) {
  if (!isStereoWindow || mode == GUI::STEREO_SHOT_MONO) {
    GLenum buf = isStereoWindow ? GL_BACK_LEFT : GL_BACK;
    return Engine::Screenshot::captureToPNG(path, x, 0, width, height, buf);
  }

  GLenum leftBuf = flipEyes ? GL_BACK_RIGHT : GL_BACK_LEFT;
  GLenum rightBuf = flipEyes ? GL_BACK_LEFT : GL_BACK_RIGHT;
  Engine::Screenshot::StereoLayout layout =
      Engine::Screenshot::StereoLayout::SideBySide;
  if (mode == GUI::STEREO_SHOT_ABOVE_BELOW)
    layout = Engine::Screenshot::StereoLayout::AboveBelow;
  else if (mode == GUI::STEREO_SHOT_SEPARATE)
    layout = Engine::Screenshot::StereoLayout::Separate;
  return Engine::Screenshot::captureStereoToPNG(path, x, 0, width, height,
                                                leftBuf, rightBuf, layout);
}

// ---- Snapshots ----
// Set by the GUI to request a snapshot capture on the next clean (GUI-free)
// frame. The flags select which aspects (camera/scene/tools) to store; the
// name labels the snapshot in the panel.
bool g_requestSnapshot = false;
std::string g_pendingSnapshotName;
uint32_t g_pendingSnapshotFlags = 0;
bool isDarkTheme = true;
bool showInfoWindow = false;
bool showSettingsWindow = false;
bool show3DCursor = true;
bool showCursorSettingsWindow = false;
bool showBrushToolWindow = false;
bool showMeasurementToolWindow = false;
bool showClipPlaneToolWindow = false;
bool showSnapshotsWindow = false;
bool showSceneManagerWindow = false;
enum class SelectedType {
  None,
  Model,
  PointCloud,
  Sun,
  PointLight,
  SpotLight,
  BrushCluster
};

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

// ---- Async depth sampling for camera distance (avoids synchronous
// glReadPixels stall) ---- Double-buffered: while PBO[writeIdx] receives the
// current frame's read, PBO[1-writeIdx] contains last frame's
// already-transferred depth value.
GLuint g_distancePBO[2] = {0, 0};
int g_distancePBOWriteIdx = 0;
float g_cachedCenterDepth = 1.0f;
bool g_distancePBOReady = false;

// ---- Cursor System ----
Cursor::CursorManager cursorManager;
glm::vec3 capturedCursorPos;
bool orbitFollowsCursor = false;

// ---- Brush Tool ----
Tools::BrushTool brushTool;

// ---- Measurement Tool ----
Tools::MeasurementTool measurementTool;

// ---- Section / Clip Plane Tool ----
Tools::ClipPlaneTool clipPlaneTool;

// ---- Transform Gizmo ----
// Visual translate/rotate/scale gizmo anchored at the selected object's pivot.
// Coexists with the legacy Ctrl/Alt body-drag free-move (which is preserved):
// clicking a gizmo handle starts a constrained transform instead.
Tools::TransformGizmo transformGizmo;
bool gizmoDragging = false;
// Undo snapshots captured at the start of a gizmo drag.
Engine::Undo::ModelEditState gizmoUndoModelBefore;
Engine::Undo::PointCloudEditState gizmoUndoPointCloudBefore;
Engine::PointLight gizmoUndoPointLightBefore;
Engine::SpotLight gizmoUndoSpotLightBefore;
SelectedType gizmoUndoType = SelectedType::None;
int gizmoUndoIndex = -1;
// True while the gizmo is driving the active clip plane (instead of a scene
// object), so the drag press/update/release routes to the clip-plane tool.
bool g_clipPlaneGizmoActive = false;

// ---- Window Configuration ----
int windowWidth = 1920;
int windowHeight = 1080;
bool isStereoWindow = false;

// ---- Drag & Drop ----
// Files dropped onto the window, queued by drop_callback and imported by the
// GUI on the next frame (where the scene-load dialog and import flow live).
std::vector<std::string> g_droppedFiles;

// ---- 3D Viewport (free area not covered by the docked GUI panels) ----
// The scene renders into this sub-rectangle of the window instead of the whole
// window, so it no longer draws behind the left Scene Hierarchy panel or the
// top menu bar. g_viewportX is the left inset (GL x origin in the default
// framebuffer); g_viewportTopInset is the window-space gap above the region;
// width/height are the region size. Updated each frame from the GUI insets.
int g_viewportX = 0;
int g_viewportTopInset = 0;
int g_viewportWidth = 1920;
int g_viewportHeight = 1080;
// Last size the offscreen render targets were sized to, for change detection.
static int g_lastViewportW = -1;
static int g_lastViewportH = -1;

// ── Plugin system ───────────────────────────────────────────────────────────
// The PluginManager owns every plugin/tool and is driven from the handful of
// integration points below (init, shutdown, per-eye render, ImGui pass, Tools
// menu and the GLFW input callbacks). MainPluginContext is the concrete
// services API handed to plugins: it simply forwards to the application globals
// declared above, keeping the plugin layer decoupled from this translation
// unit. Both g_pluginManager and g_pluginContext are referenced from GUI.cpp
// (see the externs there).
struct MainPluginContext : public Plugins::PluginContext {
  Engine::Scene &scene() override { return currentScene; }
  const Camera &camera() const override { return ::camera; }
  glm::vec3 cameraPosition() const override { return ::camera.Position; }
  GUI::ApplicationPreferences &preferences() override { return ::preferences; }
  Engine::UndoManager &undo() override { return Engine::UndoManager::instance(); }

  Plugins::PickRay mouseRay() const override {
    Plugins::PickRay r;
    glm::vec3 rayNear, rayFar;
    calculateMouseRay(lastX, lastY, r.origin, r.direction, rayNear, rayFar,
                      aspectRatio);
    return r;
  }
  bool cursorWorldPos(glm::vec3 &out) const override {
    if (!cursorManager.isCursorPositionValid())
      return false;
    out = cursorManager.getCursorPosition();
    return true;
  }
  Plugins::RayHit raycastModels() const override {
    Plugins::RayHit best;
    const Plugins::PickRay ray = mouseRay();
    float closest = FLT_MAX;
    for (int i = 0; i < static_cast<int>(currentScene.models.size()); i++) {
      float distance;
      glm::vec3 normal;
      if (rayIntersectsModel(ray.origin, ray.direction, currentScene.models[i],
                             distance, normal) &&
          distance < closest) {
        closest = distance;
        best.hit = true;
        best.distance = distance;
        best.position = ray.origin + ray.direction * distance;
        best.normal = normal;
        best.modelIndex = i;
      }
    }
    return best;
  }

  glm::vec2 mousePos() const override {
    return glm::vec2(static_cast<float>(lastX), static_cast<float>(lastY));
  }
  int keyMods() const override {
    int mods = 0;
    GLFWwindow *w = Engine::Window::nativeWindow;
    if (!w)
      return 0;
    if (glfwGetKey(w, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS ||
        glfwGetKey(w, GLFW_KEY_RIGHT_CONTROL) == GLFW_PRESS)
      mods |= GLFW_MOD_CONTROL;
    if (glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
        glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS)
      mods |= GLFW_MOD_SHIFT;
    if (glfwGetKey(w, GLFW_KEY_LEFT_ALT) == GLFW_PRESS ||
        glfwGetKey(w, GLFW_KEY_RIGHT_ALT) == GLFW_PRESS)
      mods |= GLFW_MOD_ALT;
    return mods;
  }
  glm::vec2 viewportSize() const override {
    return glm::vec2(static_cast<float>(g_viewportWidth),
                     static_cast<float>(g_viewportHeight));
  }

  void toast(const std::string &message, Plugins::ToastLevel level) override {
    GUI::ToastType type = GUI::ToastType::Info;
    switch (level) {
    case Plugins::ToastLevel::Success: type = GUI::ToastType::Success; break;
    case Plugins::ToastLevel::Warning: type = GUI::ToastType::Warning; break;
    case Plugins::ToastLevel::Error:   type = GUI::ToastType::Error;   break;
    case Plugins::ToastLevel::Info:    type = GUI::ToastType::Info;    break;
    }
    GUI::ShowToast(message, type);
  }
};

Plugins::PluginManager g_pluginManager;
static MainPluginContext g_pluginContextImpl;
Plugins::PluginContext &g_pluginContext = g_pluginContextImpl;

// Map an OS cursor position (window pixels, top-left origin) to viewport NDC.
static inline glm::vec2 WindowToViewportNDC(double mx, double my) {
  float w = static_cast<float>(g_viewportWidth > 0 ? g_viewportWidth : 1);
  float h = static_cast<float>(g_viewportHeight > 0 ? g_viewportHeight : 1);
  return glm::vec2(
      (2.0f * (static_cast<float>(mx) - static_cast<float>(g_viewportX))) / w -
          1.0f,
      1.0f -
          (2.0f * (static_cast<float>(my) -
                   static_cast<float>(g_viewportTopInset))) /
              h);
}

// Map viewport NDC to an OS cursor position (window pixels, top-left origin).
static inline glm::vec2 ViewportNDCToWindow(float ndcX, float ndcY) {
  return glm::vec2(static_cast<float>(g_viewportX) +
                       (ndcX + 1.0f) * 0.5f * static_cast<float>(g_viewportWidth),
                   static_cast<float>(g_viewportTopInset) +
                       (1.0f - ndcY) * 0.5f *
                           static_cast<float>(g_viewportHeight));
}

// ---- Lighting ----
std::vector<Engine::PointLight> pointLights;
std::vector<Engine::SpotLight> spotLights;
float zOffset = 0.5f;
Engine::Sun sun = {
    glm::normalize(glm::vec3(-1.0f, -2.0f, -1.0f)), // More vertical angle
    glm::vec3(1.0f, 0.98f, 0.95f), // Neutral daylight white (slightly warm)
    0.16f,                         // Intensity
    false};

unsigned int depthMapFBO;
unsigned int depthMap;
const unsigned int SHADOW_WIDTH = 4096, SHADOW_HEIGHT = 4096;
Engine::Shader *simpleDepthShader = nullptr;
glm::mat4 lightSpaceMatrix = glm::mat4(1.0f);
// World-space size of a single sun shadow-map texel for the current frame.
// Drives the normal-offset shadow bias so the offset scales with the actual
// projected texel footprint (set in calculateLightSpaceMatrix()).
float shadowTexelWorldSize = 0.01f;

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
bool hdrFboValid =
    false; // set true after successful FBO init/resize, reset on resize

// Schütz Phase 2: compute shader point-cloud rasterizer
Engine::ComputePointCloudRenderer *computePointCloudRenderer = nullptr;

// SSAO rendering system
Engine::SSAORenderer *ssaoRenderer = nullptr;

GUI::LightingMode currentLightingMode = GUI::LIGHTING_SHADOW_MAPPING;
bool enableShadows = true;
// Unlit view mode (albedo only): a view-shading toggle independent of the
// lighting mode, used by the View toolbar and the ToggleUnlit shortcut.
bool g_unlitMode = false;

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
bool bvhBuilt = false;
bool bvhBuffersUploaded = false;
bool triangleDataUploaded = false;
bool enableBVH = true; // BVH toggle

// BVH Debug Renderer
Engine::BVHDebugRenderer bvhDebugRenderer;
bool showBVHDebug = false;

// ---- Two-Level BVH (TLAS / BLAS) ----
// Per-object BLAS built once in LOCAL (object) space (cached, invalidated by
// geometry/material edits, NOT by transforms) + a small TLAS over per-object
// world AABBs. Lives behind the `enableTwoLevelBVH` toggle and is mutually
// exclusive with the flat single-level path above: both feed SSBO bindings
// 0/1/2, but with different contents (world-space single tree vs concatenated
// local-space BLAS), plus 3/4/5 for the TLAS + instance table.
bool enableTwoLevelBVH = true; // runtime toggle (mirrors preferences); default on

// Cached per-model BLAS in local/object space. Parallel to currentScene.models.
struct BLASCacheEntry {
  std::vector<float> triangleData;       // flat local-space triangles (16 floats each)
  std::vector<Engine::GPUBVHNode> nodes; // local-space BLAS nodes
  std::vector<uint32_t> triIndices;      // BVH permutation into this BLAS's triangles
  uint32_t triangleCount = 0;
  glm::vec3 boundsMin = glm::vec3(0.0f); // local-space root AABB
  glm::vec3 boundsMax = glm::vec3(0.0f);
  size_t geomSignature = 0;              // invalidation key (geometry, not transform)
  bool valid = false;
};
std::vector<BLASCacheEntry> blasCache;

// Concatenated GPU buffers for the two-level path.
GLuint blasTriangleSSBO = 0; // binding 0 (when two-level active)
GLuint blasNodeSSBO = 0;     // binding 1
GLuint blasIndexSSBO = 0;    // binding 2
GLuint tlasNodeSSBO = 0;     // binding 3
GLuint instanceSSBO = 0;     // binding 4
GLuint tlasIndexSSBO = 0;    // binding 5
std::vector<float> twoLevelTriangleData;           // -> binding 0
std::vector<Engine::GPUBVHNode> twoLevelBLASNodes; // -> binding 1
std::vector<uint32_t> twoLevelTriIndices;          // -> binding 2
std::vector<Engine::GPUBVHNode> gpuTLASNodes;      // -> binding 3
std::vector<Engine::GPUInstance> gpuInstances;     // -> binding 4
std::vector<uint32_t> tlasInstanceIndices;         // -> binding 5
std::vector<int> instanceToModel; // CPU-side: instance index -> scene.models index
int twoLevelTriangleCount = 0;
bool twoLevelBuilt = false;
Engine::BVHBuilder tlasBuilder; // reused to build the TLAS over instance AABBs

// ---- Dynamic Diffuse Global Illumination (DDGI) ----
Engine::DDGIVolume *ddgiVolume = nullptr;
Engine::Shader *ddgiTraceShader = nullptr;          // probe ray tracing
Engine::Shader *ddgiUpdateIrradianceShader = nullptr; // irradiance probe blend
Engine::Shader *ddgiUpdateDistanceShader = nullptr;   // depth/visibility probe blend
Engine::Shader *ddgiBorderIrradianceShader = nullptr; // irradiance border copy
Engine::Shader *ddgiBorderDistanceShader = nullptr;   // depth border copy
bool g_ddgiResetRequested = false; // set by the GUI "Reset DDGI" button

// View-independent passes (shadow maps, DDGI probe update) write to textures /
// buffers that BOTH eyes sample. renderEye() runs twice per frame in stereo, so
// without a guard these passes would run redundantly for the second eye. This
// flag is reset to false once per frame (before the eye loop) and set to true
// after the first eye's shared passes complete, so the second eye reuses them.
bool g_sharedPassesDone = false;

// ---- OpenXR integration ----
// g_xrOverrideFBO: when non-zero, renderEye() targets this FBO instead of
// GL_BACK_LEFT / GL_BACK_RIGHT / the HDR FBO.  Set to 0 when XR is inactive
// so there is truly zero overhead on every non-XR code path.
#ifdef _WIN32
static Engine::XRSession *g_xrSession    = nullptr;
static GLuint             g_xrOverrideFBO = 0;
static int                g_xrEyeWidth    = 0;
static int                g_xrEyeHeight   = 0;

// Exposed to GUI.cpp via externs.
bool        g_xrAvailable   = false;
std::string g_xrStatusMsg;
std::string g_xrRuntimeName;

// Called by the GUI toggle (see GUI.cpp extern declaration).
void xrSessionEnable(bool enable) {
    if (enable) {
        if (g_xrSession) return; // already running
        g_xrSession = new Engine::XRSession();
        HDC   hdc   = wglGetCurrentDC();
        HGLRC hglrc = wglGetCurrentContext();
        if (!g_xrSession->init(hdc, hglrc)) {
            g_xrStatusMsg   = g_xrSession->statusMessage();
            g_xrRuntimeName.clear();
            delete g_xrSession;
            g_xrSession   = nullptr;
            g_xrAvailable = false;
            // Revert the preference so the checkbox doesn't stay ticked.
            preferences.openxrSettings.enabled = false;
        } else {
            g_xrAvailable   = true;
            g_xrStatusMsg   = g_xrSession->statusMessage();
            g_xrRuntimeName = g_xrSession->runtimeName();
        }
    } else {
        if (!g_xrSession) return;
        delete g_xrSession;
        g_xrSession     = nullptr;
        g_xrAvailable   = false;
        g_xrOverrideFBO = 0;
        g_xrStatusMsg   = "OpenXR disabled.";
        g_xrRuntimeName.clear();
    }
}
#else
// Stub on non-Windows so GUI.cpp can still reference the symbols.
bool        g_xrAvailable   = false;
std::string g_xrStatusMsg   = "OpenXR not supported on this platform.";
std::string g_xrRuntimeName;
void xrSessionEnable(bool) {}
#endif

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

void drop_callback(GLFWwindow *window, int count, const char **paths) {
  // Queue only; the import runs inside the GUI frame so dropped scene files
  // can go through the regular replace/merge/ask flow.
  for (int i = 0; i < count; i++) {
    g_droppedFiles.push_back(paths[i]);
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

// Average color of a 2D texture (its 1x1 top mip level), cached per texture id.
// DDGI probe rays need the bounce albedo of textured surfaces, but diffuse GI
// integrates over the hemisphere -- a single average albedo per surface is both
// sufficient and cheap. Textures are stored as linear RGB8/RGBA8 (no sRGB
// internal format) with a full mip chain already generated in
// Model::TextureFromFile, so the box-filtered top mip is exactly the mean texel.
static glm::vec3 getAverageTextureColor(GLuint texId) {
  static std::unordered_map<GLuint, glm::vec3> cache;
  if (texId == 0)
    return glm::vec3(1.0f);
  auto it = cache.find(texId);
  if (it != cache.end())
    return it->second;

  GLint prevTex = 0;
  glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTex);
  glBindTexture(GL_TEXTURE_2D, texId);

  GLint w = 0, h = 0;
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_WIDTH, &w);
  glGetTexLevelParameteriv(GL_TEXTURE_2D, 0, GL_TEXTURE_HEIGHT, &h);

  glm::vec3 avg(1.0f);
  if (w > 0 && h > 0) {
    // Top mip level index = floor(log2(max(w, h))); that level is 1x1.
    int maxLevel = 0;
    for (int m = std::max(w, h); m > 1; m >>= 1)
      maxLevel++;
    glGenerateMipmap(GL_TEXTURE_2D); // ensure the full chain exists
    unsigned char px[4] = {255, 255, 255, 255};
    glGetTexImage(GL_TEXTURE_2D, maxLevel, GL_RGBA, GL_UNSIGNED_BYTE, px);
    avg = glm::vec3(px[0], px[1], px[2]) / 255.0f;
  }

  glBindTexture(GL_TEXTURE_2D, (GLuint)prevTex);
  cache[texId] = avg;
  return avg;
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

  bvhBuilt = true;
  bvhBuffersUploaded = false;   // Mark that buffers need to be uploaded
  triangleDataUploaded = false; // Mark that triangle data needs to be uploaded
  std::cout << "BVH built successfully" << std::endl;
}

// ============================================================================
// Two-Level BVH (TLAS / BLAS)
// ============================================================================
static Engine::GPUBVHNode toGPUNode(const Engine::BVHNode &n) {
  Engine::GPUBVHNode g;
  g.minX = n.minBounds.x;
  g.minY = n.minBounds.y;
  g.minZ = n.minBounds.z;
  g.leftFirst = n.leftFirst;
  g.maxX = n.maxBounds.x;
  g.maxY = n.maxBounds.y;
  g.maxZ = n.maxBounds.z;
  g.triCount = n.triCount;
  return g;
}

// Cheap content key that changes with geometry/material edits but NOT with
// transforms, so moving an object never invalidates its cached BLAS.
static size_t computeModelGeomSignature(const Engine::Model &model) {
  size_t h = 1469598103934665603ull; // FNV-1a offset basis
  auto mix = [&h](size_t v) {
    h ^= v;
    h *= 1099511628211ull;
  };
  auto mixF = [&mix](float f) { mix((size_t)(*reinterpret_cast<uint32_t *>(&f))); };

  mix(model.getMeshes().size());
  for (const auto &mesh : model.getMeshes()) {
    mix(mesh.vertices.size());
    mix(mesh.indices.size());
    mix(reinterpret_cast<size_t>(mesh.vertices.data()));
    if (!mesh.textures.empty())
      mix((size_t)mesh.textures[0].id);
  }
  // Material values baked per-triangle in the BLAS.
  mixF(model.emissive);
  mixF(model.shininess);
  mixF(model.color.x);
  mixF(model.color.y);
  mixF(model.color.z);
  return h;
}

// Build one model's BLAS in LOCAL (object) space. Mirrors the per-triangle data
// layout and albedo logic of the flat extraction path, minus the world transform.
static void buildModelBLAS(const Engine::Model &model, BLASCacheEntry &entry) {
  entry.triangleData.clear();
  entry.nodes.clear();
  entry.triIndices.clear();
  entry.triangleCount = 0;

  std::vector<Engine::BVHTriangle> bvhTriangles;

  for (const auto &mesh : model.getMeshes()) {
    // Albedo for GI rays: match the rasterizer (texture average or flat color).
    glm::vec3 meshAlbedo = model.color;
    if (!mesh.textures.empty()) {
      GLuint diffuseId = mesh.textures[0].id;
      for (const auto &t : mesh.textures) {
        if (t.type == "texture_diffuse") {
          diffuseId = t.id;
          break;
        }
      }
      if (diffuseId != 0)
        meshAlbedo = getAverageTextureColor(diffuseId);
    }

    const auto &vertices = mesh.vertices;
    const auto &indices = mesh.indices;
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
      // LOCAL-space vertices (no model matrix). The instance transform is
      // applied on the GPU during traversal.
      glm::vec3 v0 = vertices[indices[i]].position;
      glm::vec3 v1 = vertices[indices[i + 1]].position;
      glm::vec3 v2 = vertices[indices[i + 2]].position;
      glm::vec3 normal = normalize(cross(v1 - v0, v2 - v0));

      // Flat triangle data (same 16-float Triangle layout as the single-level
      // path, but in local space and with a local-space normal).
      entry.triangleData.insert(entry.triangleData.end(), {v0.x, v0.y, v0.z});
      entry.triangleData.push_back(0.0f);
      entry.triangleData.insert(entry.triangleData.end(), {v1.x, v1.y, v1.z});
      entry.triangleData.push_back(0.0f);
      entry.triangleData.insert(entry.triangleData.end(), {v2.x, v2.y, v2.z});
      entry.triangleData.push_back(0.0f);
      entry.triangleData.insert(entry.triangleData.end(),
                                {normal.x, normal.y, normal.z});
      entry.triangleData.push_back(0.0f);
      entry.triangleData.insert(entry.triangleData.end(),
                                {meshAlbedo.x, meshAlbedo.y, meshAlbedo.z});
      entry.triangleData.push_back(model.emissive);
      entry.triangleData.push_back(model.shininess);
      int materialId = (int)entry.triangleCount;
      entry.triangleData.push_back(*reinterpret_cast<float *>(&materialId));
      entry.triangleData.push_back(0.0f);
      entry.triangleData.push_back(0.0f);

      bvhTriangles.emplace_back(v0, v1, v2, normal, meshAlbedo, model.emissive,
                                model.shininess, materialId);
      entry.triangleCount++;
    }
  }

  if (bvhTriangles.empty()) {
    entry.boundsMin = entry.boundsMax = glm::vec3(0.0f);
    entry.valid = true; // empty model -> empty BLAS (skipped during TLAS build)
    return;
  }

  Engine::BVHBuilder builder;
  builder.build(bvhTriangles);

  const auto &nodes = builder.getNodes();
  entry.nodes.reserve(nodes.size());
  for (const auto &n : nodes)
    entry.nodes.push_back(toGPUNode(n));
  entry.triIndices = builder.getTriangleIndices();

  entry.boundsMin = nodes[0].minBounds; // local-space root AABB
  entry.boundsMax = nodes[0].maxBounds;
  entry.valid = true;
}

// Assemble per-model BLAS (cached) + a TLAS over instance world AABBs, and pack
// the concatenated SSBO payloads. Does NOT upload (see updateTwoLevelBuffers).
// Pack a glm::mat4 into 16 floats (column-major; glm + std430 agree).
static void packMat4(float *dst, const glm::mat4 &m) {
  for (int col = 0; col < 4; col++)
    for (int row = 0; row < 4; row++)
      dst[col * 4 + row] = m[col][row];
}

// Object->world matrix from a model's transform (matches the rasterizer).
static glm::mat4 modelMatrixOf(const Engine::Model &model) {
  glm::mat4 m(1.0f);
  m = glm::translate(m, model.position);
  m = glm::rotate(m, glm::radians(model.rotation.x), glm::vec3(1, 0, 0));
  m = glm::rotate(m, glm::radians(model.rotation.y), glm::vec3(0, 1, 0));
  m = glm::rotate(m, glm::radians(model.rotation.z), glm::vec3(0, 0, 1));
  m = glm::scale(m, model.scale);
  return m;
}

// World-space AABB of a local AABB transformed by m (8-corner expansion).
static Engine::AABB worldAABBOf(const glm::vec3 &lmin, const glm::vec3 &lmax,
                                const glm::mat4 &m) {
  Engine::AABB world;
  for (int c = 0; c < 8; c++) {
    glm::vec3 corner((c & 1) ? lmax.x : lmin.x, (c & 2) ? lmax.y : lmin.y,
                     (c & 4) ? lmax.z : lmin.z);
    world.expand(glm::vec3(m * glm::vec4(corner, 1.0f)));
  }
  return world;
}

// Build the TLAS (bindings 3/5 payloads) over per-instance world-AABB items.
// Reuses BVHBuilder by encoding each instance as a degenerate triangle whose
// materialId carries the instance id.
static void buildTLAS(const std::vector<Engine::BVHTriangle> &tlasItems) {
  gpuTLASNodes.clear();
  tlasInstanceIndices.clear();
  if (tlasItems.empty())
    return;
  tlasBuilder.build(tlasItems);
  const auto &tnodes = tlasBuilder.getNodes();
  gpuTLASNodes.reserve(tnodes.size());
  for (const auto &n : tnodes)
    gpuTLASNodes.push_back(toGPUNode(n));

  const auto &perm = tlasBuilder.getTriangleIndices();
  const auto &items = tlasBuilder.getTriangles();
  tlasInstanceIndices.reserve(perm.size());
  for (uint32_t p : perm)
    tlasInstanceIndices.push_back((uint32_t)items[p].materialId);
}

// Full (re)build: refresh cached BLAS, concatenate the BLAS layer (bindings
// 0/1/2), build the instance table, and build the TLAS. Used when geometry or
// the model set changes -- NOT on a plain transform move (see
// refreshInstanceTransforms).
void buildTwoLevelBVH(const Engine::Scene &scene) {
  // 1) Ensure each model has an up-to-date local-space BLAS.
  blasCache.resize(scene.models.size());
  for (size_t i = 0; i < scene.models.size(); i++) {
    size_t sig = computeModelGeomSignature(scene.models[i]);
    if (!blasCache[i].valid || blasCache[i].geomSignature != sig) {
      buildModelBLAS(scene.models[i], blasCache[i]);
      blasCache[i].geomSignature = sig;
      std::cout << "BLAS built for model " << i << " ("
                << blasCache[i].triangleCount << " tris)" << std::endl;
    }
  }

  // 2) Concatenate BLAS payloads and build the per-object instance table.
  twoLevelTriangleData.clear();
  twoLevelBLASNodes.clear();
  twoLevelTriIndices.clear();
  gpuInstances.clear();
  instanceToModel.clear();
  twoLevelTriangleCount = 0;

  std::vector<Engine::BVHTriangle> tlasItems; // one degenerate item per instance

  for (size_t i = 0; i < scene.models.size(); i++) {
    const Engine::Model &model = scene.models[i];
    const BLASCacheEntry &blas = blasCache[i];
    if (!blas.valid || blas.triangleCount == 0 || blas.nodes.empty())
      continue; // skip empty models

    glm::mat4 m = modelMatrixOf(model);

    Engine::GPUInstance inst;
    packMat4(inst.model, m);
    packMat4(inst.invModel, glm::inverse(m));
    inst.blasNodeOffset = (uint32_t)twoLevelBLASNodes.size();
    inst.triOffset = (uint32_t)twoLevelTriangleCount;
    inst.triIndexOffset = (uint32_t)twoLevelTriIndices.size();
    inst.pad = 0;

    twoLevelBLASNodes.insert(twoLevelBLASNodes.end(), blas.nodes.begin(),
                             blas.nodes.end());
    twoLevelTriIndices.insert(twoLevelTriIndices.end(), blas.triIndices.begin(),
                              blas.triIndices.end());
    twoLevelTriangleData.insert(twoLevelTriangleData.end(),
                                blas.triangleData.begin(),
                                blas.triangleData.end());
    twoLevelTriangleCount += (int)blas.triangleCount;

    uint32_t instanceIndex = (uint32_t)gpuInstances.size();
    gpuInstances.push_back(inst);
    instanceToModel.push_back((int)i);

    Engine::AABB world = worldAABBOf(blas.boundsMin, blas.boundsMax, m);
    Engine::BVHTriangle item;
    item.bounds = world;
    item.centroid = world.getCenter();
    item.materialId = (int)instanceIndex;
    tlasItems.push_back(item);
  }

  // 3) Build the TLAS over instance AABBs.
  buildTLAS(tlasItems);

  twoLevelBuilt = true;
  std::cout << "Two-level BVH built: " << gpuInstances.size() << " instances, "
            << twoLevelTriangleCount << " tris, " << twoLevelBLASNodes.size()
            << " BLAS nodes, " << gpuTLASNodes.size() << " TLAS nodes"
            << std::endl;
}

// Incremental transform update (Step 2 fast path): recompute only the
// per-instance matrices and the TLAS. The BLAS layer on bindings 0/1/2 is
// untouched, so a plain object move costs O(#objects), not O(#triangles).
void refreshInstanceTransforms(const Engine::Scene &scene) {
  std::vector<Engine::BVHTriangle> tlasItems;
  tlasItems.reserve(gpuInstances.size());
  for (size_t k = 0; k < gpuInstances.size(); k++) {
    int i = instanceToModel[k];
    const BLASCacheEntry &blas = blasCache[i];
    glm::mat4 m = modelMatrixOf(scene.models[i]);
    packMat4(gpuInstances[k].model, m);
    packMat4(gpuInstances[k].invModel, glm::inverse(m));
    // BLAS offsets are unchanged.

    Engine::AABB world = worldAABBOf(blas.boundsMin, blas.boundsMax, m);
    Engine::BVHTriangle item;
    item.bounds = world;
    item.centroid = world.getCenter();
    item.materialId = (int)k;
    tlasItems.push_back(item);
  }
  buildTLAS(tlasItems);
}

void setupTwoLevelBuffers() {
  if (blasTriangleSSBO == 0) glGenBuffers(1, &blasTriangleSSBO);
  if (blasNodeSSBO == 0) glGenBuffers(1, &blasNodeSSBO);
  if (blasIndexSSBO == 0) glGenBuffers(1, &blasIndexSSBO);
  if (tlasNodeSSBO == 0) glGenBuffers(1, &tlasNodeSSBO);
  if (instanceSSBO == 0) glGenBuffers(1, &instanceSSBO);
  if (tlasIndexSSBO == 0) glGenBuffers(1, &tlasIndexSSBO);
}

// Upload the concatenated two-level payloads to bindings 0..5. Mirrors the
// single-level path's bindings 0/1/2 (with local-space BLAS contents) and adds
// 3 = TLAS nodes, 4 = instances, 5 = TLAS instance indices.
void updateTwoLevelBuffers() {
  if (!twoLevelBuilt) return;
  setupTwoLevelBuffers();

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, blasTriangleSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               twoLevelTriangleData.size() * sizeof(float),
               twoLevelTriangleData.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, blasTriangleSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, blasNodeSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               twoLevelBLASNodes.size() * sizeof(Engine::GPUBVHNode),
               twoLevelBLASNodes.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, blasNodeSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, blasIndexSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               twoLevelTriIndices.size() * sizeof(uint32_t),
               twoLevelTriIndices.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, blasIndexSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, tlasNodeSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               gpuTLASNodes.size() * sizeof(Engine::GPUBVHNode),
               gpuTLASNodes.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, tlasNodeSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, instanceSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               gpuInstances.size() * sizeof(Engine::GPUInstance),
               gpuInstances.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, instanceSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, tlasIndexSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               tlasInstanceIndices.size() * sizeof(uint32_t),
               tlasInstanceIndices.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, tlasIndexSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Upload ONLY the instance + TLAS payloads (bindings 3/4/5). The Step 2 fast
// path for a plain object move: the BLAS layer on bindings 0/1/2 is untouched,
// so only this small data (a few KB for hundreds of objects) is re-uploaded.
void updateInstanceAndTLASBuffers() {
  if (!twoLevelBuilt) return;
  setupTwoLevelBuffers();

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, tlasNodeSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               gpuTLASNodes.size() * sizeof(Engine::GPUBVHNode),
               gpuTLASNodes.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, tlasNodeSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, instanceSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               gpuInstances.size() * sizeof(Engine::GPUInstance),
               gpuInstances.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, instanceSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, tlasIndexSSBO);
  glBufferData(GL_SHADER_STORAGE_BUFFER,
               tlasInstanceIndices.size() * sizeof(uint32_t),
               tlasInstanceIndices.data(), GL_DYNAMIC_DRAW);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, tlasIndexSSBO);

  glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
}

// Bind the two-level payloads to bindings 0..5 without re-uploading. Used to
// refresh binding-point state before the lit draw (point-cloud passes reuse
// binding 1, and binding state must be re-established each frame).
void bindTwoLevelBuffers() {
  if (!twoLevelBuilt) return;
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, blasTriangleSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, blasNodeSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, blasIndexSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 3, tlasNodeSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 4, instanceSSBO);
  glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 5, tlasIndexSSBO);
}

void cleanupTwoLevelBuffers() {
  GLuint *bufs[] = {&blasTriangleSSBO, &blasNodeSSBO, &blasIndexSSBO,
                    &tlasNodeSSBO,     &instanceSSBO, &tlasIndexSSBO};
  for (GLuint *b : bufs) {
    if (*b != 0) {
      glDeleteBuffers(1, b);
      *b = 0;
    }
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

// Apply a freshly-loaded scene's persisted environment (skybox + lighting mode)
// and sun to the live session. No-op for older scenes that did not store an
// environment block, or when the user has disabled the behavior in settings.
// Always restores the sun when the scene carries one, since old scenes simply
// keep the previous sun. Safe to call only with a current GL context (it
// rebuilds the skybox).
void applyLoadedSceneEnvironment(const Engine::Scene &scene) {
  // Restore the sun only when the scene actually carried one, so loading an
  // older scene leaves the current sun untouched.
  if (scene.hasSun) {
    sun = scene.sun;
  }

  if (!scene.environment.present || !preferences.applySceneEnvironmentOnLoad) {
    return;
  }

  const Engine::SceneEnvironment &env = scene.environment;

  currentLightingMode = static_cast<GUI::LightingMode>(env.lightingMode);
  preferences.lightingMode = currentLightingMode;

  skyboxConfig.type = static_cast<GUI::SkyboxType>(env.skyboxType);
  skyboxConfig.solidColor = env.skyboxSolidColor;
  skyboxConfig.gradientTopColor = env.skyboxGradientTop;
  skyboxConfig.gradientBottomColor = env.skyboxGradientBottom;
  skyboxConfig.selectedCubemap = env.selectedCubemap;
  if (!env.skyboxHdrPath.empty()) {
    skyboxConfig.hdrPath = env.skyboxHdrPath;
  }

  // Mirror the live skybox config into preferences so they stay fully in sync
  // (the skybox settings panel keeps both aligned, and only a complete mirror
  // avoids a half-updated persisted state on the next launch).
  preferences.skyboxType = skyboxConfig.type;
  preferences.skyboxSolidColor = skyboxConfig.solidColor;
  preferences.skyboxGradientTop = skyboxConfig.gradientTopColor;
  preferences.skyboxGradientBottom = skyboxConfig.gradientBottomColor;
  preferences.selectedCubemap = skyboxConfig.selectedCubemap;
  preferences.skyboxHdrPath = skyboxConfig.hdrPath;
  preferences.skyboxExposure = env.skyboxExposure;

  updateSkybox();
}

// Capture the live environment (skybox + lighting mode + sun) into a scene so
// it is written when the scene is saved.
void captureSceneEnvironment(Engine::Scene &scene) {
  scene.sun = sun;
  Engine::SceneEnvironment &env = scene.environment;
  env.present = true;
  env.lightingMode = static_cast<int>(currentLightingMode);
  env.skyboxType = static_cast<int>(skyboxConfig.type);
  env.skyboxSolidColor = skyboxConfig.solidColor;
  env.skyboxGradientTop = skyboxConfig.gradientTopColor;
  env.skyboxGradientBottom = skyboxConfig.gradientBottomColor;
  env.selectedCubemap = skyboxConfig.selectedCubemap;
  env.skyboxHdrPath = skyboxConfig.hdrPath;
  env.skyboxExposure = preferences.skyboxExposure;
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

  // Fit the shadow frustum to the actual scene geometry (not the camera). The
  // sun is a directional light, so a single ortho frustum that bounds every
  // caster gives stable, view-independent shadows. Including the camera here
  // (as the old code did) made the frustum bloat and slide every time the user
  // moved, which both wasted shadow-map resolution and caused the shadows to
  // shimmer/shift -- the "behaves wrongly" symptom.
  //
  // boundingSphereRadius is measured from localBoundsCenter (AABB center in
  // local space), so we account for that offset when computing world bounds.
  for (const auto &model : currentScene.models) {
    glm::vec3 worldSphereCenter =
        model.position + glm::vec3(model.scale * model.localBoundsCenter);
    float worldRadius =
        model.boundingSphereRadius *
        glm::max(model.scale.x, glm::max(model.scale.y, model.scale.z));

    sceneMin = glm::min(sceneMin, worldSphereCenter - glm::vec3(worldRadius));
    sceneMax = glm::max(sceneMax, worldSphereCenter + glm::vec3(worldRadius));
  }

  // If no models, fall back to camera-centered bounds so something still casts.
  if (currentScene.models.empty()) {
    sceneMin = camera.Position - glm::vec3(10.0f);
    sceneMax = camera.Position + glm::vec3(10.0f);
  }

  // Bounding sphere of the scene.
  glm::vec3 sceneCenter = (sceneMin + sceneMax) * 0.5f;
  glm::vec3 sceneSize = sceneMax - sceneMin;
  float sceneRadius = glm::length(sceneSize) * 0.5f;
  sceneRadius = std::max(sceneRadius, 1.0f); // Avoid a degenerate frustum.

  glm::vec3 lightDir = glm::normalize(sun.direction);

  // Pull the light back beyond the sphere so the whole scene is in front of the
  // near plane, with a little headroom for off-screen casters.
  float lightDistance = sceneRadius * 2.0f;
  glm::vec3 lightPos = sceneCenter - lightDir * lightDistance;

  // Tight ortho bounds (only ~3% padding) keep texel density high.
  float orthoSize = sceneRadius * 1.03f;
  float nearPlane = lightDistance - sceneRadius * 1.5f;
  nearPlane = std::max(nearPlane, 0.05f);
  float farPlane = lightDistance + sceneRadius * 1.5f;

  glm::mat4 lightProjection = glm::ortho(-orthoSize, orthoSize, // left, right
                                         -orthoSize, orthoSize, // bottom, top
                                         nearPlane, farPlane);

  glm::vec3 up = glm::vec3(0.0f, 1.0f, 0.0f);
  if (abs(glm::dot(lightDir, up)) > 0.99f) {
    up = glm::vec3(1.0f, 0.0f, 0.0f);
  }
  glm::mat4 lightView = glm::lookAt(lightPos, sceneCenter, up);

  glm::mat4 lightSpace = lightProjection * lightView;

  // ---- Texel snapping ----
  // Snap the projected world origin to whole shadow-map texels so the shadow
  // pattern does not crawl/shimmer as the scene bounds (and therefore the
  // frustum) change slightly between frames.
  glm::vec4 shadowOrigin = lightSpace * glm::vec4(0.0f, 0.0f, 0.0f, 1.0f);
  shadowOrigin *= float(SHADOW_WIDTH) * 0.5f; // NDC -> half-texel units
  glm::vec4 roundedOrigin = glm::round(shadowOrigin);
  glm::vec4 roundOffset = roundedOrigin - shadowOrigin;
  roundOffset *= 2.0f / float(SHADOW_WIDTH); // back to NDC
  roundOffset.z = 0.0f;
  roundOffset.w = 0.0f;
  lightProjection[3] += roundOffset;

  // World-space footprint of one texel, used by the fragment shader's
  // normal-offset bias.
  shadowTexelWorldSize = (2.0f * orthoSize) / float(SHADOW_WIDTH);

  return lightProjection * lightView;
}

void savePreferences() {
  json j;

  // UI preferences
  j["ui"]["darkTheme"] = preferences.isDarkTheme;
  j["ui"]["theme"] = preferences.guiTheme;
  j["ui"]["showFPS"] = preferences.showFPS;
  j["ui"]["vsync"] = preferences.vsyncEnabled;
  j["ui"]["show3DCursor"] = preferences.show3DCursor;
  j["ui"]["cursorKeepLastDepthOnBackground"] =
      preferences.cursorKeepLastDepthOnBackground;
  j["ui"]["cursorBackgroundCacheMode"] =
      preferences.cursorBackgroundCacheMode;
  j["ui"]["cursorBackgroundCacheTime"] =
      preferences.cursorBackgroundCacheTime;
  j["ui"]["cursorBackgroundCacheDistance"] =
      preferences.cursorBackgroundCacheDistance;
  j["ui"]["enableSpawnAnimation"] = preferences.enableSpawnAnimation;
  j["ui"]["guiScaleFactor"] = preferences.guiScaleFactor;
  j["ui"]["screenshotIncludeUI"] = preferences.screenshotIncludeUI;
  j["ui"]["stereoScreenshotMode"] =
      static_cast<int>(preferences.stereoScreenshotMode);

  // Radar settings
  j["radar"]["enabled"] = preferences.radarEnabled;
  j["radar"]["posX"] = preferences.radarPos.x;
  j["radar"]["posY"] = preferences.radarPos.y;
  j["radar"]["scale"] = preferences.radarScale;
  j["radar"]["radius"] = preferences.radarRadius;
  j["radar"]["showScene"] = preferences.radarShowScene;
  j["radar"]["autoFit"] = preferences.radarAutoFit;
  j["radar"]["sliceEnabled"] = preferences.radarSliceEnabled;
  j["radar"]["sliceOffset"] = preferences.radarSliceOffset;
  j["radar"]["frustumSpread"] = preferences.radarFrustumSpread;
  j["radar"]["sceneBrightness"] = preferences.radarSceneBrightness;

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
      preferences.orbitCenterColor.r, preferences.orbitCenterColor.g,
      preferences.orbitCenterColor.b, preferences.orbitCenterColor.a};
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

  // OpenXR settings (enabled is intentionally NOT saved — always start disabled
  // on next launch so the app never auto-connects to a headset unexpectedly).
  j["openxr"]["worldScale"]      = preferences.openxrSettings.worldScale;
  j["openxr"]["mirrorToWindow"]  = preferences.openxrSettings.mirrorToWindow;
  j["openxr"]["useScenePlanes"]  = preferences.openxrSettings.useScenePlanes;
  j["openxr"]["nearPlane"]       = preferences.openxrSettings.nearPlane;
  j["openxr"]["farPlane"]        = preferences.openxrSettings.farPlane;

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

  // 3DConnexion app sync settings
  j["spacemouse"]["tdx"]["motionModel"] = preferences.tdxSettings.motionModel;
  j["spacemouse"]["tdx"]["autoPivot"] = preferences.tdxSettings.autoPivot;
  j["spacemouse"]["tdx"]["lockHorizon"] = preferences.tdxSettings.lockHorizon;
  j["spacemouse"]["tdx"]["suspendInput"] = preferences.tdxSettings.suspendInput;
  j["spacemouse"]["tdx"]["lockTo3dViews"] =
      preferences.tdxSettings.lockTo3dViews;
  j["spacemouse"]["tdx"]["moveObjects"] = preferences.tdxSettings.moveObjects;
  j["spacemouse"]["tdx"]["autokeyAnimation"] =
      preferences.tdxSettings.autokeyAnimation;
  j["spacemouse"]["tdx"]["selectionFollower"] =
      preferences.tdxSettings.selectionFollower;
  j["spacemouse"]["tdx"]["firstPersonEaseOut"] =
      preferences.tdxSettings.firstPersonEaseOut;
  j["spacemouse"]["tdx"]["floorQueryRate"] =
      preferences.tdxSettings.floorQueryRate;
  j["spacemouse"]["tdx"]["lockSketchPlane"] =
      preferences.tdxSettings.lockSketchPlane;

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
  j["startup"]["sceneLoadingBehavior"] =
      static_cast<int>(preferences.sceneLoadingBehavior);

  // Scene save options + recent scenes + environment-on-load behavior.
  j["scene"]["save"]["includeCamera"] =
      preferences.sceneSaveSettings.includeCamera;
  j["scene"]["save"]["includeLighting"] =
      preferences.sceneSaveSettings.includeLighting;
  j["scene"]["save"]["includeEnvironment"] =
      preferences.sceneSaveSettings.includeEnvironment;
  j["scene"]["save"]["includeMeasurements"] =
      preferences.sceneSaveSettings.includeMeasurements;
  j["scene"]["save"]["includeClipPlanes"] =
      preferences.sceneSaveSettings.includeClipPlanes;
  j["scene"]["save"]["includeSnapshots"] =
      preferences.sceneSaveSettings.includeSnapshots;
  j["scene"]["save"]["compact"] = preferences.sceneSaveSettings.compact;
  j["scene"]["applyEnvironmentOnLoad"] =
      preferences.applySceneEnvironmentOnLoad;
  j["scene"]["recent"] = preferences.recentScenes;

  // Save lighting settings
  j["lighting"]["mode"] = static_cast<int>(preferences.lightingMode);
  j["lighting"]["enableShadows"] = preferences.enableShadows;
  j["lighting"]["ambientStrength"] = preferences.ambientStrengthFromSkybox;

  // Save HDR settings
  j["hdr"]["enabled"] = preferences.hdrSettings.enabled;
  j["hdr"]["exposure"] = preferences.hdrSettings.exposure;
  j["hdr"]["bloomThreshold"] = preferences.hdrSettings.bloomThreshold;
  j["hdr"]["bloomIntensity"] = preferences.hdrSettings.bloomIntensity;
  j["hdr"]["toneMapOperator"] = preferences.hdrSettings.toneMapOperator;
  j["hdr"]["enableBloom"] = preferences.hdrSettings.enableBloom;
  j["hdr"]["enableFXAA"] = preferences.hdrSettings.enableFXAA;
  j["hdr"]["fxaaSubpixel"] = preferences.hdrSettings.fxaaSubpixel;
  j["hdr"]["fxaaEdgeThreshold"] = preferences.hdrSettings.fxaaEdgeThreshold;
  j["hdr"]["contrast"] = preferences.hdrSettings.contrast;
  j["hdr"]["saturation"] = preferences.hdrSettings.saturation;
  j["hdr"]["enableVignette"] = preferences.hdrSettings.enableVignette;
  j["hdr"]["vignetteIntensity"] = preferences.hdrSettings.vignetteIntensity;
  j["hdr"]["vignetteRadius"] = preferences.hdrSettings.vignetteRadius;
  j["hdr"]["vignetteSoftness"] = preferences.hdrSettings.vignetteSoftness;

  // Save SSAO settings
  j["ssao"]["enabled"] = preferences.ssaoSettings.enabled;
  j["ssao"]["kernelSize"] = preferences.ssaoSettings.kernelSize;
  j["ssao"]["radius"] = preferences.ssaoSettings.radius;
  j["ssao"]["bias"] = preferences.ssaoSettings.bias;
  j["ssao"]["power"] = preferences.ssaoSettings.power;

  // Schütz Phase 1 – save EDL and point cloud size settings
  j["edl"]["enabled"] = preferences.edlSettings.enabled;
  j["edl"]["strength"] = preferences.edlSettings.strength;
  j["edl"]["radius"] = preferences.edlSettings.radius;
  j["pointcloud"]["baseSize"] = preferences.pointCloudBaseSize;
  j["pointcloud"]["splatEnabled"] = preferences.pointSplatSettings.enabled;
  j["pointcloud"]["splatMaxRadius"] = preferences.pointSplatSettings.maxRadius;

  // Save sun (directional light). It is an application-global light, so it
  // belongs in preferences rather than per-scene.
  j["sun"]["enabled"] = sun.enabled;
  j["sun"]["direction"] = {sun.direction.x, sun.direction.y, sun.direction.z};
  j["sun"]["color"] = {sun.color.x, sun.color.y, sun.color.z};
  j["sun"]["intensity"] = sun.intensity;

  // Save shadow settings
  j["shadows"]["pcfKernelSize"] = preferences.shadowSettings.pcfKernelSize;
  j["shadows"]["enablePCSS"] = preferences.shadowSettings.enablePCSS;
  j["shadows"]["lightSize"] = preferences.shadowSettings.lightSize;
  j["shadows"]["shadowSoftness"] = preferences.shadowSettings.shadowSoftness;
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

  // Save VCT settings (loaded from the "vct" section in loadPreferences)
  j["vct"]["indirectSpecularLight"] =
      preferences.vctSettings.indirectSpecularLight;
  j["vct"]["indirectDiffuseLight"] =
      preferences.vctSettings.indirectDiffuseLight;
  j["vct"]["directLight"] = preferences.vctSettings.directLight;
  j["vct"]["shadows"] = preferences.vctSettings.shadows;
  j["vct"]["voxelSize"] = preferences.vctSettings.voxelSize;
  j["vct"]["diffuseConeCount"] = preferences.vctSettings.diffuseConeCount;
  j["vct"]["tracingMaxDistance"] = preferences.vctSettings.tracingMaxDistance;
  j["vct"]["shadowSampleCount"] = preferences.vctSettings.shadowSampleCount;
  j["vct"]["shadowStepMultiplier"] =
      preferences.vctSettings.shadowStepMultiplier;

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
  j["radiance"]["enableTwoLevelBVH"] =
      preferences.radianceSettings.enableTwoLevelBVH;
  j["radiance"]["showBVHDebug"] = preferences.radianceSettings.showBVHDebug;
  j["radiance"]["bvhDebugMaxDepth"] =
      preferences.radianceSettings.bvhDebugMaxDepth;
  j["radiance"]["bvhDebugRenderMode"] =
      preferences.radianceSettings.bvhDebugRenderMode;

  // Save DDGI settings
  j["radiance"]["enableDDGI"] = preferences.radianceSettings.enableDDGI;
  j["radiance"]["ddgiProbeCountX"] =
      preferences.radianceSettings.ddgiProbeCounts.x;
  j["radiance"]["ddgiProbeCountY"] =
      preferences.radianceSettings.ddgiProbeCounts.y;
  j["radiance"]["ddgiProbeCountZ"] =
      preferences.radianceSettings.ddgiProbeCounts.z;
  j["radiance"]["ddgiRaysPerProbe"] =
      preferences.radianceSettings.ddgiRaysPerProbe;
  j["radiance"]["ddgiHysteresis"] =
      preferences.radianceSettings.ddgiHysteresis;
  j["radiance"]["ddgiNormalBias"] =
      preferences.radianceSettings.ddgiNormalBias;
  j["radiance"]["ddgiGIIntensity"] =
      preferences.radianceSettings.ddgiGIIntensity;
  j["radiance"]["ddgiDepthSharpness"] =
      preferences.radianceSettings.ddgiDepthSharpness;
  j["radiance"]["ddgiVisibilityStrength"] =
      preferences.radianceSettings.ddgiVisibilityStrength;
  j["radiance"]["ddgiShowProbes"] =
      preferences.radianceSettings.ddgiShowProbes;
  j["radiance"]["shadowSamples"] = preferences.radianceSettings.shadowSamples;
  j["radiance"]["shadowSoftness"] =
      preferences.radianceSettings.shadowSoftness;

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
  ApplyGuiTheme(preferences.guiTheme, 1.0f);
  isDarkTheme = IsGuiThemeDark(preferences.guiTheme);
  preferences.isDarkTheme = isDarkTheme;
  showFPS = preferences.showFPS;
  show3DCursor = preferences.show3DCursor;
  cursorManager.setKeepLastDepthOnBackground(
      preferences.cursorKeepLastDepthOnBackground);
  cursorManager.setBackgroundCacheMode(preferences.cursorBackgroundCacheMode);
  cursorManager.setBackgroundCacheTime(preferences.cursorBackgroundCacheTime);
  cursorManager.setBackgroundCacheDistance(
      preferences.cursorBackgroundCacheDistance);
  g_GuiScale.userScaleFactor = preferences.guiScaleFactor;

  // Apply radiance/BVH preferences to the globals the renderer reads.
  // Without this, loaded settings only take effect once a GUI control is
  // touched (the GUI syncs these on change).
  radianceSettings = preferences.radianceSettings;
  enableBVH = preferences.radianceSettings.enableBVH;
  enableTwoLevelBVH = preferences.radianceSettings.enableTwoLevelBVH;
  showBVHDebug = preferences.radianceSettings.showBVHDebug;
  ambientStrengthFromSkybox = preferences.ambientStrengthFromSkybox;

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
      // Fall back to the matching built-in theme for configs saved before the
      // multi-theme picker existed (0 = Modern Dark, 1 = Modern Light).
      preferences.guiTheme =
          j["ui"].value("theme", preferences.isDarkTheme ? 0 : 1);
      preferences.showFPS = j["ui"].value("showFPS", true);
      preferences.vsyncEnabled = j["ui"].value("vsync", false);
      preferences.show3DCursor = j["ui"].value("show3DCursor", true);
      preferences.cursorKeepLastDepthOnBackground =
          j["ui"].value("cursorKeepLastDepthOnBackground", false);
      preferences.cursorBackgroundCacheMode = j["ui"].value(
          "cursorBackgroundCacheMode",
          static_cast<int>(GUI::CURSOR_CACHE_INDEFINITE));
      preferences.cursorBackgroundCacheTime =
          j["ui"].value("cursorBackgroundCacheTime", 1.0f);
      preferences.cursorBackgroundCacheDistance =
          j["ui"].value("cursorBackgroundCacheDistance", 250.0f);
      preferences.enableSpawnAnimation =
          j["ui"].value("enableSpawnAnimation", true);
      preferences.guiScaleFactor = j["ui"].value("guiScaleFactor", 1.0f);
      preferences.screenshotIncludeUI =
          j["ui"].value("screenshotIncludeUI", false);
      preferences.stereoScreenshotMode = static_cast<GUI::StereoScreenshotMode>(
          j["ui"].value("stereoScreenshotMode",
                        static_cast<int>(GUI::STEREO_SHOT_MONO)));
    }

    // Radar settings
    if (j.contains("radar")) {
      preferences.radarEnabled = j["radar"].value("enabled", false);
      preferences.radarPos.x = j["radar"].value("posX", 0.8f);
      preferences.radarPos.y = j["radar"].value("posY", -0.8f);
      preferences.radarScale = j["radar"].value("scale", 0.03f);
      preferences.radarRadius = j["radar"].value("radius", 0.18f);
      preferences.radarShowScene = j["radar"].value("showScene", true);
      preferences.radarAutoFit = j["radar"].value("autoFit", true);
      preferences.radarSliceEnabled = j["radar"].value("sliceEnabled", true);
      preferences.radarSliceOffset = j["radar"].value("sliceOffset", 1.0f);
      preferences.radarFrustumSpread = j["radar"].value("frustumSpread", 0.12f);
      preferences.radarSceneBrightness =
          j["radar"].value("sceneBrightness", 8.0f);
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
      preferences.alwaysShowOrbitCenter =
          j["camera"].value("alwaysShowOrbitCenter", false);
      if (j["camera"].contains("orbitCenterColor") &&
          j["camera"]["orbitCenterColor"].is_array() &&
          j["camera"]["orbitCenterColor"].size() >= 4) {
        preferences.orbitCenterColor =
            glm::vec4(j["camera"]["orbitCenterColor"][0].get<float>(),
                      j["camera"]["orbitCenterColor"][1].get<float>(),
                      j["camera"]["orbitCenterColor"][2].get<float>(),
                      j["camera"]["orbitCenterColor"][3].get<float>());
      }
      preferences.orbitCenterSphereRadius =
          j["camera"].value("orbitCenterSphereRadius", 0.2f);

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

    // OpenXR settings (enabled is always false at startup; see savePreferences)
    if (j.contains("openxr")) {
      preferences.openxrSettings.enabled       = false; // never auto-start
      preferences.openxrSettings.worldScale     = j["openxr"].value("worldScale",     1.0f);
      preferences.openxrSettings.mirrorToWindow = j["openxr"].value("mirrorToWindow", true);
      preferences.openxrSettings.useScenePlanes = j["openxr"].value("useScenePlanes", true);
      preferences.openxrSettings.nearPlane      = j["openxr"].value("nearPlane",      0.05f);
      preferences.openxrSettings.farPlane       = j["openxr"].value("farPlane",       200.0f);
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

      // 3DConnexion app sync settings
      if (j["spacemouse"].contains("tdx")) {
        auto &tdx = j["spacemouse"]["tdx"];
        preferences.tdxSettings.motionModel =
            tdx.value("motionModel", "Helicopter");
        preferences.tdxSettings.autoPivot = tdx.value("autoPivot", false);
        preferences.tdxSettings.lockHorizon = tdx.value("lockHorizon", false);
        preferences.tdxSettings.suspendInput = tdx.value("suspendInput", false);
        preferences.tdxSettings.lockTo3dViews =
            tdx.value("lockTo3dViews", false);
        preferences.tdxSettings.moveObjects = tdx.value("moveObjects", false);
        preferences.tdxSettings.autokeyAnimation =
            tdx.value("autokeyAnimation", false);
        preferences.tdxSettings.selectionFollower =
            tdx.value("selectionFollower", true);
        preferences.tdxSettings.firstPersonEaseOut =
            tdx.value("firstPersonEaseOut", 600);
        preferences.tdxSettings.floorQueryRate = tdx.value("floorQueryRate", 1);
        preferences.tdxSettings.lockSketchPlane =
            tdx.value("lockSketchPlane", true);
      }
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
          j["startup"].value("sceneLoadingBehavior",
                             static_cast<int>(GUI::SCENE_LOAD_ALWAYS_ASK)));
    }

    // Scene save options + recent scenes + environment-on-load behavior.
    if (j.contains("scene")) {
      const auto &sj = j["scene"];
      if (sj.contains("save")) {
        const auto &sv = sj["save"];
        auto &opt = preferences.sceneSaveSettings;
        opt.includeCamera = sv.value("includeCamera", true);
        opt.includeLighting = sv.value("includeLighting", true);
        opt.includeEnvironment = sv.value("includeEnvironment", true);
        opt.includeMeasurements = sv.value("includeMeasurements", true);
        opt.includeClipPlanes = sv.value("includeClipPlanes", true);
        opt.includeSnapshots = sv.value("includeSnapshots", true);
        opt.compact = sv.value("compact", false);
      }
      preferences.applySceneEnvironmentOnLoad =
          sj.value("applyEnvironmentOnLoad", true);
      if (sj.contains("recent") && sj["recent"].is_array()) {
        preferences.recentScenes =
            sj["recent"].get<std::vector<std::string>>();
      }
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
      preferences.ambientStrengthFromSkybox =
          j["lighting"].value("ambientStrength", 0.1f);

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
          j["hdr"].value("toneMapOperator", 1);
      preferences.hdrSettings.enableBloom =
          j["hdr"].value("enableBloom", false);
      preferences.hdrSettings.enableFXAA =
          j["hdr"].value("enableFXAA", true);
      preferences.hdrSettings.fxaaSubpixel =
          j["hdr"].value("fxaaSubpixel", 0.75f);
      preferences.hdrSettings.fxaaEdgeThreshold =
          j["hdr"].value("fxaaEdgeThreshold", 0.166f);
      preferences.hdrSettings.contrast = j["hdr"].value("contrast", 1.0f);
      preferences.hdrSettings.saturation = j["hdr"].value("saturation", 1.0f);
      preferences.hdrSettings.enableVignette =
          j["hdr"].value("enableVignette", false);
      preferences.hdrSettings.vignetteIntensity =
          j["hdr"].value("vignetteIntensity", 0.35f);
      preferences.hdrSettings.vignetteRadius =
          j["hdr"].value("vignetteRadius", 0.55f);
      preferences.hdrSettings.vignetteSoftness =
          j["hdr"].value("vignetteSoftness", 0.45f);
    }

    // SSAO settings
    if (j.contains("ssao")) {
      preferences.ssaoSettings.enabled = j["ssao"].value("enabled", false);
      preferences.ssaoSettings.kernelSize = j["ssao"].value("kernelSize", 64);
      preferences.ssaoSettings.radius = j["ssao"].value("radius", 0.5f);
      preferences.ssaoSettings.bias = j["ssao"].value("bias", 0.025f);
      preferences.ssaoSettings.power = j["ssao"].value("power", 1.0f);
    }

    // Schütz Phase 1 – load EDL and point cloud size settings
    if (j.contains("edl")) {
      preferences.edlSettings.enabled = j["edl"].value("enabled", false);
      preferences.edlSettings.strength = j["edl"].value("strength", 1.0f);
      preferences.edlSettings.radius = j["edl"].value("radius", 1.5f);
    }
    if (j.contains("pointcloud")) {
      preferences.pointCloudBaseSize = j["pointcloud"].value("baseSize", 0.02f);
      preferences.pointSplatSettings.enabled =
          j["pointcloud"].value("splatEnabled", true);
      preferences.pointSplatSettings.maxRadius =
          j["pointcloud"].value("splatMaxRadius", 4);
    }

    // Sun (directional light)
    if (j.contains("sun")) {
      sun.enabled = j["sun"].value("enabled", sun.enabled);
      if (j["sun"].contains("direction") && j["sun"]["direction"].size() == 3) {
        sun.direction = glm::normalize(glm::vec3(
            j["sun"]["direction"][0].get<float>(),
            j["sun"]["direction"][1].get<float>(),
            j["sun"]["direction"][2].get<float>()));
      }
      if (j["sun"].contains("color") && j["sun"]["color"].size() == 3) {
        sun.color = glm::vec3(j["sun"]["color"][0].get<float>(),
                              j["sun"]["color"][1].get<float>(),
                              j["sun"]["color"][2].get<float>());
      }
      sun.intensity = j["sun"].value("intensity", sun.intensity);
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
      preferences.radianceSettings.enableTwoLevelBVH =
          j["radiance"].value("enableTwoLevelBVH", true);
      preferences.radianceSettings.showBVHDebug =
          j["radiance"].value("showBVHDebug", false);
      preferences.radianceSettings.bvhDebugMaxDepth =
          j["radiance"].value("bvhDebugMaxDepth", 3);
      preferences.radianceSettings.bvhDebugRenderMode =
          j["radiance"].value("bvhDebugRenderMode", 1);

      // Load DDGI settings
      preferences.radianceSettings.enableDDGI =
          j["radiance"].value("enableDDGI", false);
      preferences.radianceSettings.ddgiProbeCounts =
          glm::ivec3(j["radiance"].value("ddgiProbeCountX", 16),
                     j["radiance"].value("ddgiProbeCountY", 8),
                     j["radiance"].value("ddgiProbeCountZ", 16));
      preferences.radianceSettings.ddgiRaysPerProbe =
          j["radiance"].value("ddgiRaysPerProbe", 64);
      preferences.radianceSettings.ddgiHysteresis =
          j["radiance"].value("ddgiHysteresis", 0.97f);
      preferences.radianceSettings.ddgiNormalBias =
          j["radiance"].value("ddgiNormalBias", 0.25f);
      preferences.radianceSettings.ddgiGIIntensity =
          j["radiance"].value("ddgiGIIntensity", 0.3f);
      preferences.radianceSettings.ddgiDepthSharpness =
          j["radiance"].value("ddgiDepthSharpness", 50);
      preferences.radianceSettings.ddgiVisibilityStrength =
          j["radiance"].value("ddgiVisibilityStrength", 0.7f);
      preferences.radianceSettings.ddgiShowProbes =
          j["radiance"].value("ddgiShowProbes", false);
      preferences.radianceSettings.shadowSamples =
          j["radiance"].value("shadowSamples", 4);
      preferences.radianceSettings.shadowSoftness =
          j["radiance"].value("shadowSoftness", 0.3f);
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
  vctSettings.diffuseConeCount =
      6; // Default: high quality with 6 cones (60° aperture)
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
  ApplyGuiTheme(preferences.guiTheme, 1.0f);
  isDarkTheme = IsGuiThemeDark(preferences.guiTheme);
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

  // GLFW window hints are sticky. The stereo hint above is left set when the
  // quad-buffer main window is created successfully, so reset it now: ImGui
  // multi-viewport creates secondary GLFW windows (for dragged-out panels) with
  // glfwCreateWindow() and would otherwise inherit GLFW_STEREO, requesting a
  // quad-buffer context per panel -- wasteful, and a hard creation failure
  // (NULL window -> crash) on drivers that cap the number of stereo contexts.
  // The UI panels only need a plain mono window; they still share the main GL
  // context (and thus the font atlas) regardless of this pixel-format change.
  glfwWindowHint(GLFW_STEREO, GLFW_FALSE);

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

  // Schütz Phase 1: let the vertex shader control gl_PointSize
  glEnable(GL_PROGRAM_POINT_SIZE);

  // ---- Set GLFW Callbacks ----
  glfwSetFramebufferSizeCallback(window, framebuffer_size_callback);
  glfwSetCursorPosCallback(window, mouse_callback);
  glfwSetScrollCallback(window, scroll_callback);
  glfwSetKeyCallback(window, key_callback);
  glfwSetMouseButtonCallback(window, mouse_button_callback);
  glfwSetWindowFocusCallback(window, window_focus_callback);
  glfwSetCursorEnterCallback(window, cursor_enter_callback);
  glfwSetDropCallback(window, drop_callback);

  voxelizer = new Engine::Voxelizer(128);

  // ---- Initialize Undo/Redo ----
  // After every undo/redo, resynchronize the systems that depend on scene
  // structure and make sure the selection still points at a valid object.
  UndoManager::instance().setSceneChangedCallback([]() {
    if (voxelizer) {
      voxelizer->markDirty();
    }
    updateSpaceMouseBounds();

    int objectCount = -1;
    switch (currentSelectedType) {
    case SelectedType::Model:
      objectCount = static_cast<int>(currentScene.models.size());
      break;
    case SelectedType::PointCloud:
      objectCount = static_cast<int>(currentScene.pointClouds.size());
      break;
    case SelectedType::PointLight:
      objectCount = static_cast<int>(pointLights.size());
      break;
    case SelectedType::SpotLight:
      objectCount = static_cast<int>(spotLights.size());
      break;
    default:
      break;
    }
    if (objectCount >= 0 &&
        (currentSelectedIndex < 0 || currentSelectedIndex >= objectCount)) {
      currentSelectedType = SelectedType::None;
      currentSelectedIndex = -1;
      currentSelectedMeshIndex = -1;
    }
    if (currentSelectedType == SelectedType::Model &&
        currentSelectedMeshIndex >=
            static_cast<int>(
                currentScene.models[currentSelectedIndex].getMeshes().size())) {
      currentSelectedMeshIndex = -1;
    }
  });

  // Any edit routed through the undo system marks the scene as having unsaved
  // changes (used by the Scene Manager panel and the unsaved-changes guards).
  UndoManager::instance().setModifiedCallback([]() { g_sceneDirty = true; });

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

  // ---- Initialize DDGI Compute Shaders ----
  auto loadDDGIShader = [](const char *path) -> Engine::Shader * {
    try {
      return Engine::loadComputeShader(path);
    } catch (std::exception &e) {
      std::cout << "Warning: Failed to load DDGI compute shader " << path << ": "
                << e.what() << std::endl;
      return nullptr;
    }
  };
  ddgiTraceShader = loadDDGIShader("core/ddgiTraceRays.glsl");
  ddgiUpdateIrradianceShader = loadDDGIShader("core/ddgiUpdateIrradiance.glsl");
  ddgiUpdateDistanceShader = loadDDGIShader("core/ddgiUpdateDistance.glsl");
  ddgiBorderIrradianceShader = loadDDGIShader("core/ddgiBorderIrradiance.glsl");
  ddgiBorderDistanceShader = loadDDGIShader("core/ddgiBorderDistance.glsl");

  // ---- Initialize DDGI Volume ----
  ddgiVolume = new Engine::DDGIVolume();
  ddgiVolume->initialize(preferences.radianceSettings.ddgiProbeCounts,
                         preferences.radianceSettings.ddgiRaysPerProbe);
  ddgiVolume->setSceneBounds(glm::vec3(-10, -10, -10), glm::vec3(10, 10, 10));

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
    } else {
      hdrFboValid = true;
    }
  }

  // ---- Initialize Compute Point-Cloud Renderer (Schütz Phase 2) ----
  computePointCloudRenderer = new Engine::ComputePointCloudRenderer();
  computePointCloudRenderer->init(windowWidth, windowHeight);
  if (!computePointCloudRenderer->isInitialized()) {
    std::cerr << "Warning: Compute point-cloud renderer failed to initialise "
                 "- falling back to GL_POINTS\n";
    delete computePointCloudRenderer;
    computePointCloudRenderer = nullptr;
  }

  // ---- Initialize SSAO Renderer ----
  ssaoRenderer = new Engine::SSAORenderer();
  if (!ssaoRenderer->initialize(windowWidth, windowHeight)) {
    std::cerr << "Warning: Failed to initialize SSAO renderer - SSAO "
                 "effects will be disabled"
              << std::endl;
    delete ssaoRenderer;
    ssaoRenderer = nullptr;
  }

  // ---- Load Initial Scene ----
  // Try to load office.scene (relative to executable), fallback to simple cube
  bool sceneLoaded = false;
  try {
    currentScene = Engine::loadScene("office.scene", camera);

    // Sync lights from scene to global variables
    pointLights = currentScene.pointLights;
    spotLights = currentScene.spotLights;

    // Load any snapshots saved with this scene.
    Core::SnapshotManager::instance().loadFromScene("office.scene");

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
      // For regular double-click centering, set cursor to the centre of the
      // scene viewport (the free area), not the centre of the whole window, so
      // it doesn't jump by the docked GUI insets.
      glfwSetCursorPos(window, g_viewportX + g_viewportWidth / 2.0f,
                       g_viewportTopInset + g_viewportHeight / 2.0f);
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

  // Initialize double-buffered PBO for async camera-distance depth sampling.
  glGenBuffers(2, g_distancePBO);
  for (GLuint pbo : g_distancePBO) {
    glBindBuffer(GL_PIXEL_PACK_BUFFER, pbo);
    glBufferData(GL_PIXEL_PACK_BUFFER, sizeof(float), nullptr, GL_STREAM_READ);
  }
  glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);

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

  // Multi-viewport (docking branch): once the main window's ImGui draw data has
  // been rendered for the frame, update and draw any windows the user dragged
  // out into their own OS windows. This must run once per frame, after
  // ImGui::Render(); RenderPlatformWindowsDefault() makes each platform
  // window's GL context current, so we back up and restore the main context
  // afterwards to keep the following glfwSwapBuffers() targeting the main window.
  auto renderImGuiPlatformWindows = [&]() {
    ImGuiIO &guiIO = ImGui::GetIO();
    if (guiIO.ConfigFlags & ImGuiConfigFlags_ViewportsEnable) {
      GLFWwindow *backupContext = glfwGetCurrentContext();
      ImGui::UpdatePlatformWindows();
      ImGui::RenderPlatformWindowsDefault();
      glfwMakeContextCurrent(backupContext);
    }
  };

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

      // Load any snapshots saved with this scene.
      Core::SnapshotManager::instance().loadFromScene(
          preferences.startupScenePath);

      // Start spawn animation for all loaded models
      for (auto &model : currentScene.models) {
        glm::vec3 targetScale = model.scale;
        if (preferences.enableSpawnAnimation) {
          model.startSpawnAnimation(targetScale, 1.1f);
        }
      }
      currentModelIndex = currentScene.models.empty() ? -1 : 0;
      updateSpaceMouseBounds();
      applyLoadedSceneEnvironment(currentScene);
      g_currentScenePath = preferences.startupScenePath;
      g_sceneDirty = false;
      GUI::UpdateWindowTitleForScene(preferences.startupScenePath);
      GUI::ShowToast(
          "Scene loaded: " +
              std::filesystem::path(preferences.startupScenePath)
                  .filename()
                  .string(),
          GUI::ToastType::Success);
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

      // Center cursor if enabled (centre of the scene viewport, not the whole
      // window, so it lands in the free area and not behind the docked panels)
      if (preferences.spaceMouseCenterCursor) {
        glfwSetCursorPos(Engine::Window::nativeWindow,
                         g_viewportX + g_viewportWidth / 2.0,
                         g_viewportTopInset + g_viewportHeight / 2.0);
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

      // Auto-set the orbit anchor the moment SpaceMouse movement stops so
      // the user does not have to click to establish a new pivot.
      // Prefer the live 3D cursor hit; fall back to a point along the
      // camera look direction at the current orbit distance (same fallback
      // used by mouse_button_callback when cursor is not over geometry).
      if (preferences.spaceMouseAnchorMode != GUI::SPACEMOUSE_ANCHOR_DISABLED) {
        glm::vec3 newAnchor;
        if (cursorManager.isCursorPositionValid()) {
          newAnchor = cursorManager.getCursorPosition();
        } else {
          newAnchor = camera.Position + camera.Front * camera.OrbitDistance;
        }
        spaceMouseInput.SetCursorAnchor(newAnchor,
                                        preferences.spaceMouseAnchorMode);
        camera.OrbitPoint = newAnchor;
        camera.OrbitDistance = glm::length(camera.Position - newAnchor);
      }

      std::cout << "SpaceMouse navigation ended" << std::endl;
    };
    spaceMouseInput.OnCommandExecuted = [](const std::string &commandId) {
      if (commandId == "Fit") {
        camera.SetState(currentScene.cameraState);
        std::cout << "SpaceMouse Fit: Camera reset to scene default"
                  << std::endl;
      }
    };

    // ---- 3DConnexion settings file sync ----
    DWORD pid = GetCurrentProcessId();
    if (tdxSync.TryFindFile(pid)) {
      std::cout << "3DConnexion settings file found and loaded" << std::endl;
      // Seed preferences from what we read
      const auto &ts = tdxSync.GetSettings();
      preferences.tdxSettings.motionModel = ts.motionModel;
      preferences.tdxSettings.autoPivot = ts.autoPivot;
      preferences.tdxSettings.lockHorizon = ts.lockHorizon;
      preferences.tdxSettings.suspendInput = ts.suspendInput;
      preferences.tdxSettings.lockTo3dViews = ts.lockTo3dViews;
      preferences.tdxSettings.moveObjects = ts.moveObjects;
      preferences.tdxSettings.autokeyAnimation = ts.autokeyAnimation;
      preferences.tdxSettings.selectionFollower = ts.selectionFollower;
      preferences.tdxSettings.firstPersonEaseOut = ts.firstPersonEaseOut;
      preferences.tdxSettings.floorQueryRate = ts.floorQueryRate;
      preferences.tdxSettings.lockSketchPlane = ts.lockSketchPlane;
    } else {
      std::cout << "3DConnexion settings file not yet found (will retry)"
                << std::endl;
    }

    tdxSync.OnSettingsChanged = [](const ThreeDConnexionSync::TdxSettings &s) {
      // Apply inbound pivot visibility change
      if (s.pivotVisibility == "ShowPivot") {
        preferences.showOrbitCenter = true;
        preferences.alwaysShowOrbitCenter = true;
      } else if (s.pivotVisibility == "ShowMovingPivot") {
        preferences.showOrbitCenter = true;
        preferences.alwaysShowOrbitCenter = false;
      } else {
        preferences.showOrbitCenter = false;
        preferences.alwaysShowOrbitCenter = false;
      }
      cursorManager.setShowOrbitCenter(preferences.showOrbitCenter);
      cursorManager.setAlwaysShowOrbitCenter(preferences.alwaysShowOrbitCenter);

      // Copy remaining settings into preferences
      preferences.tdxSettings.motionModel = s.motionModel;
      preferences.tdxSettings.autoPivot = s.autoPivot;
      preferences.tdxSettings.lockHorizon = s.lockHorizon;
      preferences.tdxSettings.suspendInput = s.suspendInput;
      preferences.tdxSettings.lockTo3dViews = s.lockTo3dViews;
      preferences.tdxSettings.moveObjects = s.moveObjects;
      preferences.tdxSettings.autokeyAnimation = s.autokeyAnimation;
      preferences.tdxSettings.selectionFollower = s.selectionFollower;
      preferences.tdxSettings.firstPersonEaseOut = s.firstPersonEaseOut;
      preferences.tdxSettings.floorQueryRate = s.floorQueryRate;
      preferences.tdxSettings.lockSketchPlane = s.lockSketchPlane;

      savePreferences();
    };
  } else {
    std::cout
        << "Failed to initialize SpaceMouse - continuing without 3D navigation"
        << std::endl;
  }

  // ---- OpenGL Settings ----
  glEnable(GL_DEPTH_TEST);
  glClearColor(0.1f, 0.1f, 0.1f, 1.0f);
  glfwSwapInterval(preferences.vsyncEnabled ? 1 : 0);

  // ---- Plugins ----
  // Instantiate every statically-registered plugin (REGISTER_PLUGIN), bridge
  // the legacy MeasurementTool onto the same pipeline, then create their GL
  // resources now that a context exists. After this point new tools are driven
  // entirely through g_pluginManager from the integration points below.
  g_pluginManager.loadRegisteredPlugins(g_pluginContext);
  g_pluginManager.initializeAllGL(g_pluginContext);

  // ---- Main Loop ----
  // Screenshot capture is deferred by one frame: when a request comes in we
  // arm a capture for the *next* frame. That way the captured back buffer never
  // contains the just-clicked menu, and (when the UI is excluded) the GUI can be
  // hidden for a single clean frame before reading the pixels.
  bool screenshotArmed = false;
  std::string screenshotArmedPath;

  // Snapshot capture is deferred the same way: armed for the next frame so the
  // thumbnail is read from a clean, GUI-free viewer image.
  bool snapshotArmed = false;
  std::string snapshotArmedName;
  uint32_t snapshotArmedFlags = 0;
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

    // Skip the rest of the frame while the window is minimized. Rendering at
    // 0x0 produces incomplete framebuffer errors and wastes work; block on
    // events until the window is restored.
    if (glfwGetWindowAttrib(window, GLFW_ICONIFIED) ||
        windowWidth <= 0 || windowHeight <= 0) {
      glfwWaitEvents();
      lastFrame = static_cast<float>(glfwGetTime());
      continue;
    }

    // ---- Update SpaceMouse Input ----
    if (spaceMouseInitialized) {
      bool wasSpaceMouseActive = spaceMouseActive;
      spaceMouseInput.Update(deltaTime);

      // Poll for external 3DConnexion settings changes (~once per 60 frames)
      if (++tdxPollCounter >= 60) {
        tdxPollCounter = 0;
        tdxSync.PollForChanges();
      }

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

    // ---- Measurement Tool ----
    // Keep the tool bound to the current scene's measurement storage
    // (idempotent; the scene object is a global, so the pointer stays valid
    // across scene loads).
    measurementTool.setMeasurements(&currentScene.measurements);

    // Keep the clip-plane tool bound to the current scene's plane storage.
    clipPlaneTool.setPlanes(&currentScene.clipPlanes);

    // ---- Plugins: per-frame logic update ----
    g_pluginManager.update(g_pluginContext, deltaTime);

    // ---- Transform Gizmo: bind target + process hover / constrained drag ----
    // Safety net: if the mouse-up landed over an ImGui panel (whose callback
    // early-returns) the release can be missed, so finalize here too.
    if (gizmoDragging &&
        glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_RELEASE) {
      finishGizmoDrag();
    }
    bindGizmoTargetToSelection();
    // The gizmo is only interactive while Ctrl is held; an active drag keeps
    // running until the mouse is released even if Ctrl is let go.
    if (transformGizmo.enabled && transformGizmo.hasTarget() &&
        (ctrlPressed || gizmoDragging)) {
      glm::vec3 gRayOrigin, gRayDir, gRayNear, gRayFar;
      calculateMouseRay(lastX, lastY, gRayOrigin, gRayDir, gRayNear, gRayFar,
                        aspectRatio);
      if (gizmoDragging) {
        bool snapHeld = (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ||
                         glfwGetKey(window, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS);
        transformGizmo.updateDrag(gRayOrigin, gRayDir, camera.Position, snapHeld);
        // A clip-plane rotate drag updates the gizmo Euler scratch; mirror it
        // back into the plane's normal.
        if (g_clipPlaneGizmoActive)
          clipPlaneTool.syncActiveNormalFromGizmo();
      } else if (!camera.IsOrbiting && !camera.IsPanning && !isMovingModel &&
                 !rightMousePressed && !ImGui::GetIO().WantCaptureMouse) {
        transformGizmo.updateHover(gRayOrigin, gRayDir, camera.Position);
      }
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
          glm::vec2 screenPx =
              ViewportNDCToWindow(grabScreenPos.x, grabScreenPos.y);
          float screenX = screenPx.x;
          float screenY = screenPx.y;

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

        glm::vec2 screenPx =
            ViewportNDCToWindow(lightScreenPos.x, lightScreenPos.y);
        float screenX = screenPx.x;
        float screenY = screenPx.y;

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

        glm::vec2 screenPx =
            ViewportNDCToWindow(lightScreenPos.x, lightScreenPos.y);
        float screenX = screenPx.x;
        float screenY = screenPx.y;

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

          // Convert to screen coordinates (within the free-area viewport)
          glm::vec2 refPx = ViewportNDCToWindow(refScreenPos.x, refScreenPos.y);
          float screenX = refPx.x;
          float screenY = refPx.y; // Flip Y handled by the mapping

          // Apply mouse offset in screen space
          screenX += totalXOffset;
          screenY -=
              totalYOffset; // Invert Y to match cursor movement direction

          // Convert back to NDC (viewport-relative)
          glm::vec2 newNDC = WindowToViewportNDC(screenX, screenY);

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
              glm::vec2(totalXOffset / (g_viewportWidth * 0.5f),
                        totalYOffset / (g_viewportHeight * 0.5f));
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
              glm::vec2(totalXOffset / (g_viewportWidth * 0.5f),
                        totalYOffset / (g_viewportHeight * 0.5f));
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

    // ---- Screenshot / snapshot: handle a capture armed on the previous frame ----
    // A clean (GUI-free) image is obtained by reading the viewer sub-rectangle
    // from the freshly composited back buffer *before* the GUI is drawn on top
    // (see the capture block right after the scene composite). The GUI is no
    // longer hidden for a frame: hiding it zeroed the reserved dock insets,
    // which grew the viewport to the full window and rebuilt the HDR/bloom/SSAO
    // framebuffers every capture -- the cause of the one-frame flash and the
    // intermittent "HDR Framebuffer not complete" black captures.
    bool captureThisFrame = screenshotArmed;
    std::string captureThisFramePath = screenshotArmedPath;
    bool captureSnapshotThisFrame = snapshotArmed;
    std::string snapshotThisFrameName = snapshotArmedName;
    uint32_t snapshotThisFrameFlags = snapshotArmedFlags;
    screenshotArmed = false;
    snapshotArmed = false;

    // ---- Size the 3D viewport to the free area beside the docked GUI ----
    // g_dockLeftWidth / g_dockTopHeight are published by renderGUI each frame
    // (0 when the GUI is hidden, so the viewport fills the window then).
    {
      // When the GUI is hidden the viewport fills the whole window. (renderGUI
      // isn't called at all in that case, so its published insets can be stale.)
      int reservedLeft = showGui ? static_cast<int>(g_dockLeftWidth + 0.5f) : 0;
      int reservedTop = showGui ? static_cast<int>(g_dockTopHeight + 0.5f) : 0;
      // Keep a sane minimum viewport size.
      reservedLeft = glm::clamp(reservedLeft, 0, glm::max(0, windowWidth - 64));
      reservedTop = glm::clamp(reservedTop, 0, glm::max(0, windowHeight - 64));
      g_viewportX = reservedLeft;
      g_viewportTopInset = reservedTop;
      g_viewportWidth = glm::max(1, windowWidth - reservedLeft);
      g_viewportHeight = glm::max(1, windowHeight - reservedTop);

      // Resize the offscreen render targets to the viewport when it changes so
      // HDR/bloom, SSAO and the compute point-cloud image stay pixel-aligned.
      if (g_viewportWidth != g_lastViewportW ||
          g_viewportHeight != g_lastViewportH) {
        if (bloomRenderer) {
          bloomRenderer->resize(g_viewportWidth, g_viewportHeight);
          hdrFboValid = false; // re-validated next frame
        }
        if (ssaoRenderer) {
          ssaoRenderer->resize(g_viewportWidth, g_viewportHeight);
        }
        if (computePointCloudRenderer) {
          computePointCloudRenderer->resize(g_viewportWidth, g_viewportHeight);
        }
        g_lastViewportW = g_viewportWidth;
        g_lastViewportH = g_viewportHeight;
      }
    }

    aspectRatio = static_cast<float>(g_viewportWidth) /
                  static_cast<float>(g_viewportHeight);
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
    // Wireframe is applied per-draw (only around the model geometry pass inside
    // renderEye) so GL_LINE never leaks into the full-screen post-process /
    // composite passes — that leak is what broke wireframe under HDR.
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Reset cursor position calculation flag at start of frame
    cursorManager.resetFrameCalculationFlag();

    // Center cursor during SpaceMouse navigation if enabled (centre of the
    // scene viewport, not the whole window, so it stays in the free area)
    if (spaceMouseInput.IsNavigating() && preferences.spaceMouseCenterCursor) {
      glfwSetCursorPos(window, g_viewportX + g_viewportWidth * 0.5,
                       g_viewportTopInset + g_viewportHeight * 0.5);
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

    // Unlit view mode outputs albedo only; force the shadow-mapping shader
    // (the only one carrying the unlit path) regardless of lighting mode.
    if (g_unlitMode && shadowMappingShader) {
      activeShader = shadowMappingShader;
    }

    // ---- Rendering ----
    // Compute lightSpaceMatrix once per frame — reused in both shadow pass and
    // main render pass inside renderEye (which runs twice in stereo mode).
    if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
      lightSpaceMatrix = calculateLightSpaceMatrix();
    }

    // Reset the per-frame guard so the first renderEye() call regenerates the
    // view-independent shadow maps and DDGI probe atlases; the second eye reuses
    // them (see g_sharedPassesDone).
    g_sharedPassesDone = false;

    // ---- OpenXR render path ------------------------------------------------
    // This block is entirely skipped at zero cost when OpenXR is disabled
    // (g_xrSession == nullptr). It runs before the desktop path so that the
    // shared passes (shadow maps, DDGI) are generated by the first XR eye and
    // reused by both the second XR eye and the desktop mirror.
#ifdef _WIN32
    bool xrFrameRendered  = false;
    bool xrMirrorBlitted  = false; // true once the left-eye image was blitted to desktop
    if (g_xrSession && preferences.openxrSettings.enabled) {
      if (!g_xrSession->pollEvents()) {
        // Session ended or lost — tear it down and revert the preference.
        xrSessionEnable(false);
        preferences.openxrSettings.enabled = false;
      }

      if (g_xrSession && g_xrSession->isRunning()) {
        bool shouldRender = g_xrSession->beginFrame();

        // Calculate near/far for XR (use scene planes or custom values).
        float xrNear = preferences.openxrSettings.useScenePlanes
                           ? preferences.nearPlane
                           : preferences.openxrSettings.nearPlane;
        float xrFar  = preferences.openxrSettings.useScenePlanes
                           ? preferences.farPlane
                           : preferences.openxrSettings.farPlane;

        glm::mat4 xrProj[2], xrRefView[2];
        bool posesValid = shouldRender &&
                          g_xrSession->getEyePoses(xrProj, xrRefView,
                                                   xrNear, xrFar,
                                                   preferences.openxrSettings.worldScale);

        if (posesValid) {
          // Compose the scene camera view with the XR eye pose:
          //   eye_from_world = xrRefView (eye-from-XR-ref) * cameraView (ref-from-world)
          // This maps camera orbit/pan controls to the scene origin while head
          // tracking and per-eye IPD are supplied by the XR runtime.
          glm::mat4 xrCombinedView[2] = {
              xrRefView[0] * view,
              xrRefView[1] * view
          };

          g_xrEyeWidth  = static_cast<int>(g_xrSession->eyeWidth());
          g_xrEyeHeight = static_cast<int>(g_xrSession->eyeHeight());
          g_xrOverrideFBO = g_xrSession->fbo();

          // Left eye
          GLuint xrLeftTexture = g_xrSession->acquireSwapchainImage(0);
          if (xrLeftTexture) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_xrOverrideFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, xrLeftTexture, 0);
            renderEye(GL_COLOR_ATTACHMENT0, xrProj[0], xrCombinedView[0],
                      activeShader, viewport, windowFlags, window,
                      false /*no GUI in HMD*/, false /*not quad-buffer stereo*/);

            // Desktop mirror blit: must happen BEFORE releaseSwapchainImage so we
            // read the texture while we still own it.  After release the runtime
            // is free to composite/reclaim it, so reading afterwards is undefined.
            if (preferences.openxrSettings.mirrorToWindow) {
              glBindFramebuffer(GL_READ_FRAMEBUFFER, g_xrOverrideFBO);
              // xrLeftTexture is still attached to GL_COLOR_ATTACHMENT0 of g_xrOverrideFBO
              glBindFramebuffer(GL_DRAW_FRAMEBUFFER, 0);
              if (isStereoWindow) {
                glDrawBuffer(GL_BACK_LEFT);
                glBlitFramebuffer(0, 0, g_xrEyeWidth, g_xrEyeHeight,
                                  g_viewportX, 0,
                                  g_viewportX + g_viewportWidth, g_viewportHeight,
                                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
                glDrawBuffer(GL_BACK_RIGHT);
                glBlitFramebuffer(0, 0, g_xrEyeWidth, g_xrEyeHeight,
                                  g_viewportX, 0,
                                  g_viewportX + g_viewportWidth, g_viewportHeight,
                                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
              } else {
                glBlitFramebuffer(0, 0, g_xrEyeWidth, g_xrEyeHeight,
                                  g_viewportX, 0,
                                  g_viewportX + g_viewportWidth, g_viewportHeight,
                                  GL_COLOR_BUFFER_BIT, GL_LINEAR);
              }
              glBindFramebuffer(GL_FRAMEBUFFER, 0);
              xrMirrorBlitted = true;
            }

            g_xrSession->releaseSwapchainImage(0);
          }

          // Right eye (g_sharedPassesDone is now true → skips shadow/DDGI)
          GLuint xrRightTexture = g_xrSession->acquireSwapchainImage(1);
          if (xrRightTexture) {
            glBindFramebuffer(GL_FRAMEBUFFER, g_xrOverrideFBO);
            glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0,
                                   GL_TEXTURE_2D, xrRightTexture, 0);
            renderEye(GL_COLOR_ATTACHMENT0, xrProj[1], xrCombinedView[1],
                      activeShader, viewport, windowFlags, window,
                      false, false);
            g_xrSession->releaseSwapchainImage(1);
          }

          g_xrOverrideFBO = 0; // restore default path
          glBindFramebuffer(GL_FRAMEBUFFER, 0);
          xrFrameRendered = true;
        }

        g_xrSession->endFrame(posesValid && xrFrameRendered);
      }
    }

    // Desktop mirror: GUI overlay on top of the already-blitted left-eye image.
    if (xrMirrorBlitted) {
      if (showGui) {
        glViewport(0, 0, windowWidth, windowHeight);
        if (isStereoWindow) {
          glDrawBuffer(GL_BACK_LEFT);
          renderGUI(true, viewport, windowFlags, activeShader);
          glDrawBuffer(GL_BACK_RIGHT);
          renderGUI(true, viewport, windowFlags, activeShader);
        } else {
          renderGUI(true, viewport, windowFlags, activeShader);
        }
        renderImGuiPlatformWindows();
      }
      glfwSwapBuffers(window);
      continue; // Skip the normal desktop render path this frame.
    }
    // XR is running but mirror is disabled: clear the desktop window and show
    // the GUI overlay so the user can still interact with the settings panel.
    if (xrFrameRendered && !preferences.openxrSettings.mirrorToWindow) {
      glBindFramebuffer(GL_FRAMEBUFFER, 0);
      glViewport(0, 0, windowWidth, windowHeight);
      glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
      if (showGui) {
        if (isStereoWindow) {
          glDrawBuffer(GL_BACK_LEFT);
          renderGUI(true, viewport, windowFlags, activeShader);
          glDrawBuffer(GL_BACK_RIGHT);
          renderGUI(true, viewport, windowFlags, activeShader);
        } else {
          renderGUI(true, viewport, windowFlags, activeShader);
        }
        renderImGuiPlatformWindows();
      }
      glfwSwapBuffers(window);
      continue;
    }
#endif
    // ---- End OpenXR render path --------------------------------------------

    // Check if HDR/bloom is enabled
    bool hdrEnabled =
        preferences.hdrSettings.enabled && bloomRenderer != nullptr;
    bool bloomEnabled = preferences.hdrSettings.enableBloom && hdrEnabled;

    if (hdrEnabled) {
      // Begin HDR rendering (render to HDR framebuffer)
      bloomRenderer->beginBloomPass();

      // Validate HDR framebuffer on first use after init/resize
      if (!hdrFboValid) {
        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) !=
            GL_FRAMEBUFFER_COMPLETE) {
          std::cerr << "ERROR: HDR framebuffer not complete after resize!"
                    << std::endl;
          hdrEnabled = false; // Fall back to non-HDR rendering
          glBindFramebuffer(GL_FRAMEBUFFER, 0);
        } else {
          hdrFboValid = true;
        }
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
      bloomSettings.fxaaEnabled = preferences.hdrSettings.enableFXAA;
      bloomSettings.fxaaSubpixel = preferences.hdrSettings.fxaaSubpixel;
      bloomSettings.fxaaEdgeThreshold =
          preferences.hdrSettings.fxaaEdgeThreshold;
      bloomSettings.contrast = preferences.hdrSettings.contrast;
      bloomSettings.saturation = preferences.hdrSettings.saturation;
      bloomSettings.vignetteEnabled = preferences.hdrSettings.enableVignette;
      bloomSettings.vignetteIntensity =
          preferences.hdrSettings.vignetteIntensity;
      bloomSettings.vignetteRadius = preferences.hdrSettings.vignetteRadius;
      bloomSettings.vignetteSoftness =
          preferences.hdrSettings.vignetteSoftness;

      // Set SSAO texture for final composition
      if (ssaoRenderer && ssaoRenderer->isInitialized() &&
          preferences.ssaoSettings.enabled) {
        bloomRenderer->setSSAOTexture(ssaoRenderer->getSSAOTexture(), true);
      } else {
        bloomRenderer->setSSAOTexture(0, false);
      }

      if (isStereoWindow) {
        // Render and apply HDR/bloom separately for each eye
        // Swap eyes if flipEyes is enabled

        // Schütz Phase 1: build Engine::EDLSettings from preferences
        Engine::EDLSettings frameEDL;
        frameEDL.enabled = preferences.edlSettings.enabled;
        frameEDL.strength = preferences.edlSettings.strength;
        frameEDL.radius = preferences.edlSettings.radius;
        const Engine::EDLSettings *edlPtr =
            frameEDL.enabled ? &frameEDL : nullptr;

        if (preferences.flipEyes) {
          // Flipped: render left projection to right buffer, right projection
          // to left buffer
          renderEye(GL_BACK_LEFT, leftProjection, leftView, activeShader,
                    viewport, windowFlags, window, false, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          bloomRenderer->applyBloom(0, bloomSettings, GL_BACK_RIGHT, edlPtr,
                                    preferences.nearPlane, preferences.farPlane,
                                    g_viewportX, 0);

          renderEye(GL_BACK_RIGHT, rightProjection, rightView, activeShader,
                    viewport, windowFlags, window, false, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          bloomRenderer->applyBloom(0, bloomSettings, GL_BACK_LEFT, edlPtr,
                                    preferences.nearPlane, preferences.farPlane,
                                    g_viewportX, 0);
        } else {
          // Normal: render left to left, right to right
          renderEye(GL_BACK_LEFT, leftProjection, leftView, activeShader,
                    viewport, windowFlags, window, false, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          bloomRenderer->applyBloom(0, bloomSettings, GL_BACK_LEFT, edlPtr,
                                    preferences.nearPlane, preferences.farPlane,
                                    g_viewportX, 0);

          renderEye(GL_BACK_RIGHT, rightProjection, rightView, activeShader,
                    viewport, windowFlags, window, false, true, &leftProjection,
                    &leftView, &rightProjection, &rightView);
          bloomRenderer->applyBloom(0, bloomSettings, GL_BACK_RIGHT, edlPtr,
                                    preferences.nearPlane, preferences.farPlane,
                                    g_viewportX, 0);
        }
      } else {
        // Mono view
        renderEye(GL_BACK_LEFT, projection, view, activeShader, viewport,
                  windowFlags, window, false);
        {
          Engine::EDLSettings frameEDL;
          frameEDL.enabled = preferences.edlSettings.enabled;
          frameEDL.strength = preferences.edlSettings.strength;
          frameEDL.radius = preferences.edlSettings.radius;
          const Engine::EDLSettings *edlPtr =
              frameEDL.enabled ? &frameEDL : nullptr;
          bloomRenderer->applyBloom(0, bloomSettings, GL_BACK, edlPtr,
                                    preferences.nearPlane, preferences.farPlane,
                                    g_viewportX, 0);
        }
      }

      // ---- Capture a clean (GUI-free) viewer image before drawing the GUI ----
      // Read the freshly composited scene from the viewport sub-rectangle now,
      // before any GUI (docked panels or floating tool windows) is drawn over
      // it. Snapshots and UI-excluded screenshots use this clean image; a
      // UI-included screenshot is taken after the GUI is drawn (below). Because
      // the GUI stays visible, the viewport is not resized for the capture, so
      // the HDR/bloom/SSAO targets are not rebuilt (no flash, no black frames).
      // Stereo-3D screenshots always read both eyes here (UI is excluded), since
      // both back buffers hold the freshly composited per-eye scene.
      bool stereoShot = isStereoWindow &&
                        preferences.stereoScreenshotMode != GUI::STEREO_SHOT_MONO;
      if (captureSnapshotThisFrame ||
          (captureThisFrame &&
           (stereoShot || !preferences.screenshotIncludeUI))) {
        GLenum capBuffer = isStereoWindow ? GL_BACK_LEFT : GL_BACK;

        if (captureSnapshotThisFrame) {
          std::vector<unsigned char> px;
          int pw = 0, ph = 0;
          if (Engine::Screenshot::captureToMemory(g_viewportX, 0, g_viewportWidth,
                                                   g_viewportHeight, capBuffer,
                                                   px, pw, ph)) {
            Core::SnapshotManager::instance().create(
                snapshotThisFrameName, snapshotThisFrameFlags, camera,
                currentScene, pointLights, spotLights, sun, brushTool,
                measurementTool, clipPlaneTool, px, pw, ph);
            GUI::ShowToast("Snapshot saved: " + snapshotThisFrameName,
                           GUI::ToastType::Success);
          } else {
            GUI::ShowToast("Failed to capture snapshot", GUI::ToastType::Error);
          }
          captureSnapshotThisFrame = false;
        }

        if (captureThisFrame) {
          std::string path = captureThisFramePath;
          if (path.empty())
            path = Engine::Screenshot::makeTimestampedPath("screenshots");
          if (captureScreenshotForMode(path, g_viewportX, g_viewportWidth,
                                       g_viewportHeight, isStereoWindow,
                                       preferences.flipEyes,
                                       preferences.stereoScreenshotMode)) {
            std::cout << "Screenshot saved: " << path << std::endl;
            GUI::ShowToast("Screenshot saved: " +
                               std::filesystem::path(path).filename().string(),
                           GUI::ToastType::Success);
          } else {
            std::cerr << "Failed to save screenshot: " << path << std::endl;
            GUI::ShowToast("Failed to save screenshot", GUI::ToastType::Error);
          }
          captureThisFrame = false;
        }
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

    // ---- Screenshot: read the finished frame before swapping ----
    // Reached for UI-included screenshots (whole window, captured after the GUI)
    // and, in the non-HDR fallback path, for UI-excluded screenshots that the
    // pre-GUI capture above did not handle (viewer sub-rectangle).
    if (captureThisFrame) {
      std::string path = captureThisFramePath;
      if (path.empty()) {
        path = Engine::Screenshot::makeTimestampedPath("screenshots");
      }
      bool stereoShot =
          isStereoWindow &&
          preferences.stereoScreenshotMode != GUI::STEREO_SHOT_MONO;
      bool ok;
      if (stereoShot) {
        // Stereo modes always capture the clean 3D viewer sub-rectangle
        // (UI excluded) from both eyes.
        ok = captureScreenshotForMode(path, g_viewportX, g_viewportWidth,
                                      g_viewportHeight, isStereoWindow,
                                      preferences.flipEyes,
                                      preferences.stereoScreenshotMode);
      } else {
        // Read the left/primary color buffer (GL_BACK is invalid on a
        // quad-buffer stereo window, so pick the left eye there).
        GLenum readBuffer = isStereoWindow ? GL_BACK_LEFT : GL_BACK;
        bool includeUI = preferences.screenshotIncludeUI;
        int cx = includeUI ? 0 : g_viewportX;
        int cw = includeUI ? windowWidth : g_viewportWidth;
        int ch = includeUI ? windowHeight : g_viewportHeight;
        ok = Engine::Screenshot::captureToPNG(path, cx, 0, cw, ch, readBuffer);
      }
      if (ok) {
        std::cout << "Screenshot saved: " << path << std::endl;
        GUI::ShowToast("Screenshot saved: " +
                           std::filesystem::path(path).filename().string(),
                       GUI::ToastType::Success);
      } else {
        std::cerr << "Failed to save screenshot: " << path << std::endl;
        GUI::ShowToast("Failed to save screenshot", GUI::ToastType::Error);
      }
    }

    // ---- Snapshot (non-HDR fallback): read the clean frame and store it ----
    // The HDR path captures snapshots before the GUI is drawn (above); this
    // path remains for non-HDR rendering, which draws its GUI inside renderEye.
    // Read the viewer sub-rectangle so the docked panels are excluded.
    if (captureSnapshotThisFrame) {
      GLenum readBuffer = isStereoWindow ? GL_BACK_LEFT : GL_BACK;
      std::vector<unsigned char> px;
      int pw = 0, ph = 0;
      bool ok = Engine::Screenshot::captureToMemory(
          g_viewportX, 0, g_viewportWidth, g_viewportHeight, readBuffer, px, pw,
          ph);
      if (ok) {
        Core::SnapshotManager::instance().create(
            snapshotThisFrameName, snapshotThisFrameFlags, camera, currentScene,
            pointLights, spotLights, sun, brushTool, measurementTool,
            clipPlaneTool, px, pw, ph);
        GUI::ShowToast("Snapshot saved: " + snapshotThisFrameName,
                       GUI::ToastType::Success);
      } else {
        GUI::ShowToast("Failed to capture snapshot", GUI::ToastType::Error);
      }
    }

    // Arm a capture for the next frame if one was requested this frame.
    if (g_requestScreenshot) {
      screenshotArmed = true;
      screenshotArmedPath = g_screenshotPath;
      g_requestScreenshot = false;
      g_screenshotPath.clear();
    }

    if (g_requestSnapshot) {
      snapshotArmed = true;
      snapshotArmedName = g_pendingSnapshotName;
      snapshotArmedFlags = g_pendingSnapshotFlags;
      g_requestSnapshot = false;
      g_pendingSnapshotName.clear();
      g_pendingSnapshotFlags = 0;
    }

    // ---- Multi-viewport: draw any dragged-out windows before presenting ----
    // Only when the GUI was actually submitted this frame (matches the showGui
    // guards on the renderGUI calls in both the HDR and non-HDR paths above).
    if (showGui)
      renderImGuiPlatformWindows();

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
  // Destroy OpenXR session before GL context is torn down.
#ifdef _WIN32
  if (g_xrSession) {
    delete g_xrSession;
    g_xrSession = nullptr;
  }
#endif

  // Delete async camera-distance PBOs
  glDeleteBuffers(2, g_distancePBO);

  // Delete cursor manager resources
  cursorManager.cleanup();

  // Delete point cloud resources
  for (auto &pointCloud : currentScene.pointClouds) {
    glDeleteVertexArrays(1, &pointCloud.vao);
    glDeleteBuffers(1, &pointCloud.vbo);
    // Schütz batch SSBOs (compute rasterizer path)
    if (pointCloud.computeBatchSSBO)
      glDeleteBuffers(1, &pointCloud.computeBatchSSBO);
    if (pointCloud.computeXyz4bSSBO)
      glDeleteBuffers(1, &pointCloud.computeXyz4bSSBO);
    if (pointCloud.computeXyz8bSSBO)
      glDeleteBuffers(1, &pointCloud.computeXyz8bSSBO);
    if (pointCloud.computeXyz12bSSBO)
      glDeleteBuffers(1, &pointCloud.computeXyz12bSSBO);
    if (pointCloud.computeRGBASSBO)
      glDeleteBuffers(1, &pointCloud.computeRGBASSBO);
  }

  // Delete triangle buffer resources
  cleanupTriangleBuffer();
  cleanupBVHBuffers();
  cleanupTwoLevelBuffers();

  // Cleanup DDGI volume
  if (ddgiVolume) {
    delete ddgiVolume;
    ddgiVolume = nullptr;
  }
  delete ddgiTraceShader;
  delete ddgiUpdateIrradianceShader;
  delete ddgiUpdateDistanceShader;
  delete ddgiBorderIrradianceShader;
  delete ddgiBorderDistanceShader;

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

  // Cleanup compute point-cloud renderer (Schütz Phase 2)
  if (computePointCloudRenderer) {
    delete computePointCloudRenderer;
    computePointCloudRenderer = nullptr;
  }

  // Cleanup SSAO renderer
  if (ssaoRenderer) {
    delete ssaoRenderer;
    ssaoRenderer = nullptr;
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

  // Clean up plugin GL resources while the context is still valid.
  g_pluginManager.shutdownAllGL();

  // Clean up measurement tool GL resources while the context is still valid
  measurementTool.cleanup();
  clipPlaneTool.cleanup();
  transformGizmo.cleanup();

  // Drop undo history while the context is still valid - undo entries for
  // deleted objects own GL resources that are freed on destruction
  UndoManager::instance().clear();

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
  // ---- DDGI: size the probe grid to the scene once per frame ----
  // The probe grid spans the BVH root AABB (padded so probes surround the
  // geometry rather than sitting on its surface). Both the update passes and
  // the fragment-shader lookup read the same grid placement from ddgiVolume.
  // DDGI is available in Radiance mode (its own "Enable DDGI" toggle) and now
  // also in Shadow Mapping mode, where it replaces the old VCT-based indirect
  // GI behind the existing "Enable Indirect Lighting" toggle. Shadow Mapping
  // keeps its shadow-mapped direct lighting -- only the indirect bounce is DDGI.
  bool ddgiActive =
      ddgiVolume && ddgiVolume->isInitialized() &&
      ((radianceSettings.enableDDGI &&
        currentLightingMode == GUI::LIGHTING_RADIANCE) ||
       (preferences.shadowSettings.enableIndirectLighting &&
        currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING));

  if (ddgiActive) {
    // Keep probe count / ray budget in sync with the GUI (no-op if unchanged).
    ddgiVolume->reconfigure(radianceSettings.ddgiProbeCounts,
                            radianceSettings.ddgiRaysPerProbe);

    // Honor an explicit GUI reset request.
    if (g_ddgiResetRequested) {
      ddgiVolume->clear();
      g_ddgiResetRequested = false;
    }

    glm::vec3 boundsMin, boundsMax;
    if (enableTwoLevelBVH && twoLevelBuilt && !gpuTLASNodes.empty()) {
      // TLAS root spans the whole scene in world space.
      const auto &rootNode = gpuTLASNodes[0];
      boundsMin = glm::vec3(rootNode.minX, rootNode.minY, rootNode.minZ);
      boundsMax = glm::vec3(rootNode.maxX, rootNode.maxY, rootNode.maxZ);
    } else if (!enableTwoLevelBVH && bvhBuilt && !gpuBVHNodes.empty()) {
      const auto &rootNode = gpuBVHNodes[0];
      boundsMin = glm::vec3(rootNode.minX, rootNode.minY, rootNode.minZ);
      boundsMax = glm::vec3(rootNode.maxX, rootNode.maxY, rootNode.maxZ);
    } else {
      boundsMin = glm::vec3(-10.0f, -10.0f, -10.0f);
      boundsMax = glm::vec3(10.0f, 10.0f, 10.0f);
    }
    glm::vec3 padding = (boundsMax - boundsMin) * 0.1f + glm::vec3(0.05f);
    ddgiVolume->setSceneBounds(boundsMin - padding, boundsMax + padding);
  }

  // Set the draw buffer and clear color and depth buffers.
  // OpenXR override: when g_xrOverrideFBO is set the caller has already
  // attached the swapchain texture; just bind it and draw to attachment 0.
  // This check costs nothing in the normal (non-XR) path because g_xrOverrideFBO
  // is a plain GLuint initialised to 0.
#ifdef _WIN32
  if (g_xrOverrideFBO != 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_xrOverrideFBO);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
  } else
#endif
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

  // 1. Update the voxel grid if voxel visualization is enabled or we're using
  // voxel cone tracing.
  // Shadow Mapping mode's indirect lighting is now DDGI (probe-traced via the
  // BVH), not voxel cone tracing, so it no longer needs the voxel grid.
  bool needsVoxelization =
      (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) ||
      voxelizer->showDebugVisualization;
  if (needsVoxelization) {
    // Detect scene changes (model transforms) and mark voxelizer dirty so it
    // re-voxelizes. Only relevant when the voxel grid is actually consumed; for
    // the shadow-mapping default this scene scan would run every eye for
    // nothing. The tracked state is not advanced while voxelization is off, so
    // the first frame it is needed again still reports "changed" and refreshes.
    static SceneState lastVoxelSceneState;
    if (lastVoxelSceneState.hasChanged(currentScene)) {
      if (voxelizer) {
        voxelizer->markDirty();
      }
      lastVoxelSceneState.update(currentScene);
    }

    // Keep voxelizer lights in sync with the scene so voxelized
    // lighting matches the actual point lights (not just the default).
    voxelizer->setLights(pointLights);

    voxelizer->update(camera.Position, currentScene.models);
  }

  // 2. Shadow mapping pass (only if using shadow mapping AND shadows are
  // enabled). The shadow map is view-independent (light space), so it is
  // generated only on the first eye each frame and reused for the second.
  if (!g_sharedPassesDone && currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING &&
      enableShadows) {
    // Temporarily disable wireframe mode for shadow mapping
    // Shadow maps need filled polygons, not wireframe lines
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glViewport(0, 0, SHADOW_WIDTH, SHADOW_HEIGHT);
    glBindFramebuffer(GL_FRAMEBUFFER, depthMapFBO);
    glClear(GL_DEPTH_BUFFER_BIT);

    // lightSpaceMatrix was already computed once this frame before renderEye.
    // Use depth shader for shadow map generation
    simpleDepthShader->use();
    simpleDepthShader->setMat4("lightSpaceMatrix", lightSpaceMatrix);

    // A small slope-scaled polygon offset handles residual self-shadowing on
    // steep triangles. The bulk of the acne/peter-panning trade-off is now
    // handled by the normal-offset bias in the lighting shader, so this stays
    // intentionally light -- large values here are what produced the detached
    // "peter panning" shadows previously.
    glEnable(GL_POLYGON_OFFSET_FILL);
    glPolygonOffset(1.0f, 1.0f);

    // Render scene to depth buffer - disable culling so both faces of
    // non-watertight meshes write depth (the nearest face wins, which is what
    // we want for the caster).
    glDisable(GL_CULL_FACE);
    renderModels(simpleDepthShader, lightSpaceMatrix);
    glEnable(GL_CULL_FACE);

    // Disable polygon offset
    glDisable(GL_POLYGON_OFFSET_FILL);

    // Restore wireframe mode if it was enabled
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // wireframe is applied per-draw
  }

  // 2.5. Point shadow mapping pass for all point lights. Like the sun shadow
  // map above, these cubemaps are view-independent and generated once per frame.
  if (!g_sharedPassesDone && currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING &&
      enableShadows && !pointLights.empty()) {
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

      // The program never changes inside the loop, so activate it once here
      // instead of re-binding it for every light.
      pointShadowShader->use();

      for (int li = 0; li < pointLights.size() && li < MAX_LIGHTS; ++li) {
        // Skip generating shadow map for lights that don't cast shadows
        if (!pointLights[li].castShadows)
          continue;
        glm::vec3 lightPos = pointLights[li].position;

        // Stack array — no heap allocation per light per eye.
        std::array<glm::mat4, 6> shadowMatrices = {
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(1, 0, 0),
                                     glm::vec3(0, -1, 0)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(-1, 0, 0),
                                     glm::vec3(0, -1, 0)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 1, 0),
                                     glm::vec3(0, 0, 1)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, -1, 0),
                                     glm::vec3(0, 0, -1)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 0, 1),
                                     glm::vec3(0, -1, 0)),
            shadowProj * glm::lookAt(lightPos, lightPos + glm::vec3(0, 0, -1),
                                     glm::vec3(0, -1, 0)),
        };

        char smName[32];
        for (unsigned int i = 0; i < 6; ++i) {
          snprintf(smName, sizeof(smName), "shadowMatrices[%u]", i);
          pointShadowShader->setMat4(smName, shadowMatrices[i]);
        }
        pointShadowShader->setVec3("lightPos", lightPos);
        pointShadowShader->setFloat("far_plane", far_plane);
        pointShadowShader->setInt("lightIndex", li);

        // Render scene to this light's 6 faces in array layers via GS
        // gl_Layer.
        // Point lights are omnidirectional — no single frustum to cull against,
        // so frustum culling is disabled for this pass (see renderModels).
        renderModels(pointShadowShader, glm::mat4(0.0f), false);
      }

      glDisable(GL_POLYGON_OFFSET_FILL);
    }

    // Restore wireframe mode if it was enabled
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // wireframe is applied per-draw
  }

  // 2.7. SSAO geometry pass - render view-space positions and normals
  if (ssaoRenderer && ssaoRenderer->isInitialized() &&
      preferences.ssaoSettings.enabled && preferences.hdrSettings.enabled &&
      bloomRenderer != nullptr) {
    // Temporarily disable wireframe mode for geometry pass
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    ssaoRenderer->beginGeometryPass();

    Engine::Shader *geoShader = ssaoRenderer->getGeometryShader();
    if (geoShader) {
      geoShader->use();
      geoShader->setMat4("projection", projection);
      geoShader->setMat4("view", view);

      // Render all models with the geometry shader
      for (int i = 0; i < currentScene.models.size(); i++) {
        auto &model = currentScene.models[i];
        if (!model.visible)
          continue;

        glm::mat4 modelMatrix = glm::mat4(1.0f);
        modelMatrix = glm::translate(modelMatrix, model.position);
        modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x),
                                  glm::vec3(1, 0, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y),
                                  glm::vec3(0, 1, 0));
        modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z),
                                  glm::vec3(0, 0, 1));
        modelMatrix = glm::scale(modelMatrix, model.scale);

        geoShader->setMat4("model", modelMatrix);

        glm::mat3 normalMatrix =
            glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
        geoShader->setMat3("normalMatrix", normalMatrix);

        for (int j = 0; j < model.getMeshes().size(); j++) {
          model.getMeshes()[j].Draw(*geoShader);
        }
      }
    }

    ssaoRenderer->endGeometryPass();

    // Update SSAO settings from preferences
    Engine::SSAOSettings &ssaoSettings = ssaoRenderer->getSettings();
    ssaoSettings.enabled = preferences.ssaoSettings.enabled;
    ssaoSettings.kernelSize = preferences.ssaoSettings.kernelSize;
    ssaoSettings.radius = preferences.ssaoSettings.radius;
    ssaoSettings.bias = preferences.ssaoSettings.bias;
    ssaoSettings.power = preferences.ssaoSettings.power;

    // Compute SSAO
    glDisable(GL_DEPTH_TEST);
    ssaoRenderer->computeSSAO(projection);

    // Blur SSAO result
    ssaoRenderer->blurSSAO();
    glEnable(GL_DEPTH_TEST);

    // Restore wireframe mode if it was enabled
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL); // wireframe is applied per-draw
  }

  // 3. Regular rendering pass
  // Bind the appropriate framebuffer for the main scene rendering.
  // OpenXR override: re-bind our XR FBO (shadow/SSAO passes may have unbound it)
  // and set the viewport to the eye resolution.
#ifdef _WIN32
  if (g_xrOverrideFBO != 0) {
    glBindFramebuffer(GL_FRAMEBUFFER, g_xrOverrideFBO);
    glDrawBuffer(GL_COLOR_ATTACHMENT0);
    glViewport(0, 0, g_xrEyeWidth, g_xrEyeHeight);
  } else
#endif
  if (preferences.hdrSettings.enabled && bloomRenderer != nullptr) {
    // Rebind HDR framebuffer (it may have been unbound during shadow mapping)
    // Don't call beginBloomPass() again as it would clear the buffer
    Engine::BloomSettings &bloomSettings = bloomRenderer->getSettings();
    glBindFramebuffer(GL_FRAMEBUFFER, bloomSettings.hdrFBO);

    // FBO completeness is validated once after init/resize via hdrFboValid in
    // the main loop; re-querying glCheckFramebufferStatus here (twice) on every
    // eye every frame is a redundant driver sync. Just set up the MRT for the
    // two color attachments (HDR color + bright/bloom).
    GLuint attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
    glDrawBuffers(2, attachments);
    glViewport(0, 0, g_viewportWidth, g_viewportHeight);
  } else {
    // Bind default framebuffer for non-HDR rendering
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    // Render the scene into the free-area sub-viewport.
    glViewport(g_viewportX, 0, g_viewportWidth, g_viewportHeight);
  }
  // NOTE: viewport is now set inside each branch above (removed the separate
  // block that was here) so the XR override above is self-contained.

  shader->use();
  shader->setMat4("projection", projection);
  shader->setMat4("view", view);
  shader->setVec3("viewPos", camera.Position);

  // Frame-constant scene uniforms: identical for both eyes and preserved in the
  // program object between them (nothing in the intervening post-process pass nor
  // renderModels() overwrites these), so set them once per frame on the first
  // eye. g_sharedPassesDone is false during the first eye and is set true (near
  // the end of renderEye) before the second eye reaches this point -- the same
  // guard the shared shadow-map / DDGI passes use.
  if (!g_sharedPassesDone) {
    // Schütz Phase 1: point cloud per-frame uniforms for screen-space point size
    shader->setFloat("pointCloudBaseSize", preferences.pointCloudBaseSize);
    shader->setFloat("screenHeight", static_cast<float>(g_viewportHeight));
    shader->setFloat("fieldOfView", glm::radians(preferences.fov));

    // Unlit view mode (albedo only). Harmless no-op on shaders lacking the
    // uniform; only the shadow-mapping shader implements the unlit path.
    shader->setBool("unlitMode", g_unlitMode);

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
  }

  // lightingMode / enableShadows / sun are pushed per-eye by renderModels() (the
  // shared authority used by the radar pass too) and, for Radiance, by its branch
  // below -- so they are intentionally not set here.

  // Shadow mapping specific setup
  if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING) {
    // lightSpaceMatrix was already computed once this frame before renderEye.
    shader->setMat4("lightSpaceMatrix", lightSpaceMatrix);

    // Bind shadow map if shadows are enabled
    if (enableShadows) {
      glActiveTexture(GL_TEXTURE4); // Using texture unit 4 for shadow map
      glBindTexture(GL_TEXTURE_2D, depthMap);
      shader->setInt("shadowMap", 4);

      // World footprint of a sun shadow texel, drives the normal-offset bias.
      shader->setFloat("shadowTexelWorldSize", shadowTexelWorldSize);

      // Bind point shadow cubemap array
      glActiveTexture(GL_TEXTURE6); // Use texture unit 6 for point shadow maps
      glBindTexture(GL_TEXTURE_CUBE_MAP_ARRAY, depthCubemap);
      shader->setInt("pointShadowMaps", 6);
      shader->setFloat("far_plane", far_plane);
    }

    // Point/spot light uniforms are frame-constant; set them once per frame on
    // the first eye (they persist in the program object for the second eye).
    if (!g_sharedPassesDone) {
      // Set point light uniforms
      {
        char buf[64];
        for (int i = 0; i < (int)pointLights.size() && i < MAX_LIGHTS; i++) {
          snprintf(buf, sizeof(buf), "lights[%d].position", i);
          shader->setVec3(buf, pointLights[i].position);
          snprintf(buf, sizeof(buf), "lights[%d].color", i);
          shader->setVec3(buf, pointLights[i].color);
          snprintf(buf, sizeof(buf), "lights[%d].intensity", i);
          shader->setFloat(buf, pointLights[i].intensity);
          snprintf(buf, sizeof(buf), "lights[%d].linear", i);
          shader->setFloat(buf, pointLights[i].linear);
          snprintf(buf, sizeof(buf), "lights[%d].quadratic", i);
          shader->setFloat(buf, pointLights[i].quadratic);
          snprintf(buf, sizeof(buf), "lightsCastShadows[%d]", i);
          shader->setBool(buf, pointLights[i].castShadows);
        }
      }
      shader->setInt("numLights",
                     std::min((int)pointLights.size(), MAX_LIGHTS));

      // Set spot light uniforms
      {
        char buf[64];
        for (int i = 0; i < (int)spotLights.size() && i < MAX_LIGHTS; i++) {
          snprintf(buf, sizeof(buf), "spotLights[%d].position", i);
          shader->setVec3(buf, spotLights[i].position);
          snprintf(buf, sizeof(buf), "spotLights[%d].direction", i);
          shader->setVec3(buf, spotLights[i].direction);
          snprintf(buf, sizeof(buf), "spotLights[%d].color", i);
          shader->setVec3(buf, spotLights[i].color);
          snprintf(buf, sizeof(buf), "spotLights[%d].intensity", i);
          shader->setFloat(buf, spotLights[i].intensity);
          snprintf(buf, sizeof(buf), "spotLights[%d].innerCutOff", i);
          shader->setFloat(buf, spotLights[i].innerCutOff);
          snprintf(buf, sizeof(buf), "spotLights[%d].outerCutOff", i);
          shader->setFloat(buf, spotLights[i].outerCutOff);
        }
      }
      shader->setInt("numSpotLights",
                     std::min((int)spotLights.size(), MAX_LIGHTS));
    }

    // ---- DDGI indirect diffuse (replaces the old VCT GI in shadow mapping) ----
    // Shadow Mapping keeps its shadow-mapped direct lighting; only the indirect
    // bounce now comes from the DDGI probe volume. The probe trace/update itself
    // runs in the shared geometry+DDGI block after this lighting if/else; here we
    // just bind the atlases and set the sampling uniforms for the rasterizer.
    shader->setBool("enableDDGI", ddgiActive);
    if (ddgiActive) {
      // Probe atlases sample on units 16/17 (material uses texture units 0-15).
      ddgiVolume->bindTexturesForSampling(16, 17);
      ddgiVolume->setSamplingUniforms(shader, 16, 17);

      glm::vec3 step = ddgiVolume->getGridStep();
      float minStep = glm::min(glm::min(step.x, step.y), step.z);
      shader->setFloat("ddgi_normalBias",
                       radianceSettings.ddgiNormalBias * minStep);
      shader->setFloat("ddgi_viewBias", 0.15f * minStep);
      shader->setFloat("ddgi_giIntensity", radianceSettings.ddgiGIIntensity);
      shader->setFloat("ddgi_visibilityStrength",
                       radianceSettings.ddgiVisibilityStrength);
    }
  }
  // Voxel cone tracing specific setup
  else if (currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
    // Set voxel grid parameters -- must account for gridCenter so that
    // cone tracing samples match the voxelization coordinate mapping.
    float halfSize = voxelizer->getVoxelGridSize() * 0.5f;
    glm::vec3 gc = voxelizer->getGridCenter();
    shader->setVec3("gridMin", gc - glm::vec3(halfSize));
    shader->setVec3("gridMax", gc + glm::vec3(halfSize));
    shader->setFloat("voxelSize",
                     voxelizer->getVoxelGridSize() /
                         static_cast<float>(voxelizer->getResolution()));

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

    // Point/spot light uniforms are frame-constant; set them once per frame on
    // the first eye (they persist in the program object for the second eye).
    if (!g_sharedPassesDone) {
      // Set point light uniforms
      {
        char buf[64];
        for (int i = 0; i < (int)pointLights.size() && i < MAX_LIGHTS; i++) {
          snprintf(buf, sizeof(buf), "lights[%d].position", i);
          shader->setVec3(buf, pointLights[i].position);
          snprintf(buf, sizeof(buf), "lights[%d].color", i);
          shader->setVec3(buf, pointLights[i].color);
          snprintf(buf, sizeof(buf), "lights[%d].intensity", i);
          shader->setFloat(buf, pointLights[i].intensity);
          snprintf(buf, sizeof(buf), "lights[%d].linear", i);
          shader->setFloat(buf, pointLights[i].linear);
          snprintf(buf, sizeof(buf), "lights[%d].quadratic", i);
          shader->setFloat(buf, pointLights[i].quadratic);
          snprintf(buf, sizeof(buf), "lightsCastShadows[%d]", i);
          shader->setBool(buf, pointLights[i].castShadows);
        }
      }
      shader->setInt("numLights",
                     std::min((int)pointLights.size(), MAX_LIGHTS));

      // Set spot light uniforms
      {
        char buf[64];
        for (int i = 0; i < (int)spotLights.size() && i < MAX_LIGHTS; i++) {
          snprintf(buf, sizeof(buf), "spotLights[%d].position", i);
          shader->setVec3(buf, spotLights[i].position);
          snprintf(buf, sizeof(buf), "spotLights[%d].direction", i);
          shader->setVec3(buf, spotLights[i].direction);
          snprintf(buf, sizeof(buf), "spotLights[%d].color", i);
          shader->setVec3(buf, spotLights[i].color);
          snprintf(buf, sizeof(buf), "spotLights[%d].intensity", i);
          shader->setFloat(buf, spotLights[i].intensity);
          snprintf(buf, sizeof(buf), "spotLights[%d].innerCutOff", i);
          shader->setFloat(buf, spotLights[i].innerCutOff);
          snprintf(buf, sizeof(buf), "spotLights[%d].outerCutOff", i);
          shader->setFloat(buf, spotLights[i].outerCutOff);
        }
      }
      shader->setInt("numSpotLights",
                     std::min((int)spotLights.size(), MAX_LIGHTS));
    }
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

    // ---- DDGI sampling uniforms ----
    shader->setBool("enableDDGI", ddgiActive);
    if (ddgiActive) {
      // Probe atlases sample on units 16/17 (material uses texture units 0-15).
      ddgiVolume->bindTexturesForSampling(16, 17);
      ddgiVolume->setSamplingUniforms(shader, 16, 17);

      glm::vec3 step = ddgiVolume->getGridStep();
      float minStep = glm::min(glm::min(step.x, step.y), step.z);
      shader->setFloat("ddgi_normalBias",
                       radianceSettings.ddgiNormalBias * minStep);
      shader->setFloat("ddgi_viewBias", 0.15f * minStep);
      shader->setFloat("ddgi_giIntensity", radianceSettings.ddgiGIIntensity);
      shader->setFloat("ddgi_visibilityStrength",
                       radianceSettings.ddgiVisibilityStrength);
    }

    // ---- Soft shadow uniforms (direct lighting, always active in Radiance) ----
    // Keep the penumbra modest so few samples stay clean: sun softness maps to
    // a near-realistic angular radius, point/spot to a small world radius.
    shader->setInt("shadowSamples", radianceSettings.shadowSamples);
    shader->setFloat("sunAngularRadius",
                     radianceSettings.shadowSoftness * 0.02f);
    shader->setFloat("lightSourceRadius",
                     radianceSettings.shadowSoftness * 0.2f);

    // No camera matrices needed - using rasterized fragment positions

    // Point/spot light + sun uniforms are frame-constant; set them once per
    // frame on the first eye (they persist in the program object for the second
    // eye). Radiance does not get its sun pushed by renderModels(), so it is set
    // here.
    if (!g_sharedPassesDone) {
      // Set point light uniforms
      {
        char buf[64];
        for (int i = 0; i < (int)pointLights.size() && i < MAX_LIGHTS; i++) {
          snprintf(buf, sizeof(buf), "pointLights[%d].position", i);
          shader->setVec3(buf, pointLights[i].position);
          snprintf(buf, sizeof(buf), "pointLights[%d].color", i);
          shader->setVec3(buf, pointLights[i].color);
          snprintf(buf, sizeof(buf), "pointLights[%d].intensity", i);
          shader->setFloat(buf, pointLights[i].intensity);
          snprintf(buf, sizeof(buf), "pointLights[%d].linear", i);
          shader->setFloat(buf, pointLights[i].linear);
          snprintf(buf, sizeof(buf), "pointLights[%d].quadratic", i);
          shader->setFloat(buf, pointLights[i].quadratic);
          snprintf(buf, sizeof(buf), "lightsCastShadows[%d]", i);
          shader->setBool(buf, pointLights[i].castShadows);
        }
      }

      // Set spot light uniforms
      {
        char buf[64];
        for (int i = 0; i < (int)spotLights.size() && i < MAX_LIGHTS; i++) {
          snprintf(buf, sizeof(buf), "spotLights[%d].position", i);
          shader->setVec3(buf, spotLights[i].position);
          snprintf(buf, sizeof(buf), "spotLights[%d].direction", i);
          shader->setVec3(buf, spotLights[i].direction);
          snprintf(buf, sizeof(buf), "spotLights[%d].color", i);
          shader->setVec3(buf, spotLights[i].color);
          snprintf(buf, sizeof(buf), "spotLights[%d].intensity", i);
          shader->setFloat(buf, spotLights[i].intensity);
          snprintf(buf, sizeof(buf), "spotLights[%d].innerCutOff", i);
          shader->setFloat(buf, spotLights[i].innerCutOff);
          snprintf(buf, sizeof(buf), "spotLights[%d].outerCutOff", i);
          shader->setFloat(buf, spotLights[i].outerCutOff);
        }
      }
      shader->setInt("numPointLights",
                     std::min((int)pointLights.size(), MAX_LIGHTS));
      shader->setInt("numSpotLights",
                     std::min((int)spotLights.size(), MAX_LIGHTS));

      // Set sun properties
      shader->setBool("sun.enabled", sun.enabled);
      shader->setVec3("sun.direction", sun.direction);
      shader->setVec3("sun.color", sun.color);
      shader->setFloat("sun.intensity", sun.intensity);
    }

    // DIAGNOSTIC: Verify critical uniforms once on first frame
    static bool shaderDebugLogged = false;
    if (!shaderDebugLogged) {
      std::cout << "=== SHADER UNIFORMS DEBUG ===" << std::endl;
      std::cout << "enableRaytracing: " << radianceSettings.enableRaytracing
                << std::endl;
      std::cout << "enableDDGI: " << radianceSettings.enableDDGI << std::endl;
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
      shaderDebugLogged = true;
    }
  }

  // ---- Shared scene geometry + DDGI update (runs after the lighting if/else) ----
  // The triangle SSBO + BVH feed both the Radiance path tracer and the DDGI
  // probe trace, and DDGI's per-frame trace/update is identical regardless of
  // lighting mode. So this block runs for Radiance (always, for the path tracer)
  // and for any mode where DDGI is active -- including Shadow Mapping with
  // indirect lighting enabled. `shader` is whichever rasterization shader is
  // active this frame; its path-tracer geometry uniforms (numTriangles, etc.)
  // are harmless no-ops on shaders that don't declare them.
  if (currentLightingMode == GUI::LIGHTING_RADIANCE || ddgiActive) {
    // Check if scene has changed to determine if we need to recalculate
    // triangle data
    bool sceneChanged = lastSceneState.hasChanged(currentScene);

    // Declare triangle count outside conditional to use in shader uniforms
    static int triangleCount = 0;

    // Force the newly-active path to (re)build when the toggle flips, since the
    // two paths share SSBO bindings 0/1/2 with different contents.
    static bool lastTwoLevelMode = false;
    if (enableTwoLevelBVH != lastTwoLevelMode) {
      triangleDataUploaded = false;
      bvhBuffersUploaded = false;
      twoLevelBuilt = false;
      lastTwoLevelMode = enableTwoLevelBVH;
    }

    if (enableTwoLevelBVH) {
      // ---- Two-level (TLAS/BLAS) geometry path ----
      // Distinguish a geometry/structure change (model added/removed or a mesh
      // edited) from a plain transform move. Only the former rebuilds the BLAS
      // layer (bindings 0/1/2); a move just refreshes the instance table + TLAS.
      bool structureChanged =
          !twoLevelBuilt || blasCache.size() != currentScene.models.size();
      if (!structureChanged) {
        for (size_t i = 0; i < currentScene.models.size(); i++) {
          if (!blasCache[i].valid ||
              blasCache[i].geomSignature !=
                  computeModelGeomSignature(currentScene.models[i])) {
            structureChanged = true;
            break;
          }
        }
      }

      if (structureChanged) {
        // Geometry topology changed: rebuild the BLAS layer + instances + TLAS
        // and re-upload everything. Cached probe irradiance/visibility is no
        // longer valid, so reset DDGI and re-voxelize.
        buildTwoLevelBVH(currentScene);
        updateTwoLevelBuffers();
        if (ddgiVolume && ddgiVolume->isInitialized())
          ddgiVolume->clear();
        if (voxelizer)
          voxelizer->markDirty();
        lastSceneState.update(currentScene);
      } else if (sceneChanged) {
        // Transform-only move (the Step 2 fast path): O(#objects) refresh of the
        // instance matrices + TLAS, uploading just bindings 3/4/5. DDGI is NOT
        // cleared -- the probes adapt over a few frames via the normal per-frame
        // update, which looks smoother (and is cheaper) than a full reset.
        refreshInstanceTransforms(currentScene);
        updateInstanceAndTLASBuffers();
        lastSceneState.update(currentScene);
      }
      triangleCount = twoLevelTriangleCount;
    } else {
    // ---- Single-level (flat world-space BVH) geometry path ----
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

          // Albedo for GI/path-tracing rays: match what the rasterizer shows.
          // When the mesh has a diffuse texture the rasterizer samples it and
          // ignores objectColor, so feed the GI the texture's average color;
          // otherwise fall back to the flat model color. (Diffuse GI integrates
          // over the hemisphere, so one average albedo per surface is enough.)
          glm::vec3 meshAlbedo = model.color;
          if (!mesh.textures.empty()) {
            GLuint diffuseId = mesh.textures[0].id;
            for (const auto &t : mesh.textures) {
              if (t.type == "texture_diffuse") {
                diffuseId = t.id;
                break;
              }
            }
            if (diffuseId != 0)
              meshAlbedo = getAverageTextureColor(diffuseId);
          }

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
                  {meshAlbedo.x, meshAlbedo.y, meshAlbedo.z}); // vec3 color
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
              Engine::BVHTriangle bvhTri(v0, v1, v2, normal, meshAlbedo,
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

        // Reset DDGI probes when scene geometry changes - cached irradiance and
        // visibility are no longer valid; probes reconverge over a few frames.
        if (ddgiVolume && ddgiVolume->isInitialized()) {
          ddgiVolume->clear();
          std::cout << "DDGI volume cleared due to scene change" << std::endl;
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

    } // end single-level geometry path

    // ---- Geometry uniforms (shared by the radiance draw + DDGI trace) ----
    shader->setInt("numTriangles", triangleCount);
    shader->setInt("numBVHNodes",
                   enableTwoLevelBVH
                       ? static_cast<int>(twoLevelBLASNodes.size())
                       : static_cast<int>(gpuBVHNodes.size()));
    shader->setBool("enableBVH",
                    enableTwoLevelBVH ? true : (enableBVH && bvhBuilt));
    shader->setInt("numTLASNodes", enableTwoLevelBVH
                                       ? static_cast<int>(gpuTLASNodes.size())
                                       : 0);
    shader->setInt("numInstances", enableTwoLevelBVH
                                       ? static_cast<int>(gpuInstances.size())
                                       : 0);
    shader->setBool("enableTwoLevel", enableTwoLevelBVH && twoLevelBuilt);

    // Disable ground plane for pure raytracing (was causing unwanted
    // lighting)
    shader->setBool("hasGroundPlane", false);

    // Re-establish geometry SSBO binding-point state for the upcoming lit draw.
    // Point-cloud passes reuse binding 1, and the active path's buffers are only
    // bound during a build/upload (which may not happen this frame), so rebind
    // here every frame/eye to keep the radiance fragment shader reading the
    // correct geometry.
    if (enableTwoLevelBVH) {
      bindTwoLevelBuffers();
    } else if (bvhBuilt) {
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triangleSSBO);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bvhNodeSSBO);
      glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, triangleIndexSSBO);
    }

    // ---- DDGI: trace probe rays and update the probe atlases ----
    // Runs entirely on the GPU each frame: (1) trace rays from every probe
    // through the BVH, (2) blend the results into the irradiance + visibility
    // octahedral atlases with temporal hysteresis, (3) copy the octahedral
    // borders so bilinear sampling is seamless. The fragment shader then
    // samples the atlases (bound on texture units 16/17 above).
    // The probe atlases are view-independent (world-space ray tracing), so the
    // trace + update is done once per frame and reused for the second eye.
    if (!g_sharedPassesDone && ddgiActive && ddgiTraceShader &&
        ddgiUpdateIrradianceShader && ddgiUpdateDistanceShader &&
        ddgiBorderIrradianceShader && ddgiBorderDistanceShader &&
        triangleCount > 0) {

      const int IRR_SIDE = Engine::DDGIVolume::IRRADIANCE_SIDE;
      const int DEP_SIDE = Engine::DDGIVolume::DEPTH_SIDE;
      glm::ivec3 pc = ddgiVolume->getProbeCounts();
      int rays = ddgiVolume->getRaysPerProbe();
      int probeCount = ddgiVolume->getProbeCount();
      glm::vec3 step = ddgiVolume->getGridStep();
      float minStep = glm::min(glm::min(step.x, step.y), step.z);
      float normalBiasWorld = radianceSettings.ddgiNormalBias * minStep;
      float maxDist = 2.0f * glm::length(step);
      bool firstFrame = ddgiVolume->consumeFirstFrame();

      // Per-frame random rotation so the spherical-Fibonacci probe rays cover
      // all directions over many frames (must match between trace + update).
      auto frameRand = []() {
        static uint32_t s = 2463534242u;
        s ^= s << 13;
        s ^= s >> 17;
        s ^= s << 5;
        return float(s & 0x00FFFFFFu) / float(0x01000000u);
      };
      glm::mat4 rotM = glm::rotate(glm::mat4(1.0f), frameRand() * 6.2831853f,
                                   glm::vec3(1, 0, 0));
      rotM = glm::rotate(rotM, frameRand() * 6.2831853f, glm::vec3(0, 1, 0));
      rotM = glm::rotate(rotM, frameRand() * 6.2831853f, glm::vec3(0, 0, 1));
      glm::mat3 randomRotation = glm::mat3(rotM);

      auto groupsOf = [](int total, int local) -> GLuint {
        return (GLuint)((total + local - 1) / local);
      };

      // --- Pass 1: trace rays into the ray-data SSBO (binding 6) ---
      ddgiTraceShader->use();
      if (enableTwoLevelBVH) {
        bindTwoLevelBuffers();
      } else {
        glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 0, triangleSSBO);
        if (enableBVH && bvhBuilt) {
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 1, bvhNodeSSBO);
          glBindBufferBase(GL_SHADER_STORAGE_BUFFER, 2, triangleIndexSSBO);
        }
      }
      ddgiVolume->bindRayBuffer(6);
      ddgiVolume->bindTexturesForSampling(0, 1); // previous-frame atlases
      ddgiVolume->setSamplingUniforms(ddgiTraceShader, 0, 1);
      ddgiTraceShader->setFloat("ddgi_normalBias", normalBiasWorld);
      ddgiTraceShader->setInt("numTriangles", triangleCount);
      ddgiTraceShader->setInt("numBVHNodes",
                              enableTwoLevelBVH
                                  ? static_cast<int>(twoLevelBLASNodes.size())
                                  : static_cast<int>(gpuBVHNodes.size()));
      ddgiTraceShader->setBool(
          "enableBVH", enableTwoLevelBVH ? true : (enableBVH && bvhBuilt));
      ddgiTraceShader->setInt(
          "numTLASNodes",
          enableTwoLevelBVH ? static_cast<int>(gpuTLASNodes.size()) : 0);
      ddgiTraceShader->setInt(
          "numInstances",
          enableTwoLevelBVH ? static_cast<int>(gpuInstances.size()) : 0);
      ddgiTraceShader->setBool("enableTwoLevel",
                               enableTwoLevelBVH && twoLevelBuilt);
      ddgiTraceShader->setFloat("rayMaxDistance",
                                radianceSettings.rayMaxDistance);
      ddgiTraceShader->setFloat("emissiveIntensity",
                                radianceSettings.emissiveIntensity);
      ddgiTraceShader->setFloat("skyIntensity", radianceSettings.skyIntensity);
      ddgiTraceShader->setMat3("u_randomRotation", randomRotation);
      {
        char buf[64];
        int np = std::min((int)pointLights.size(), MAX_LIGHTS);
        for (int i = 0; i < np; i++) {
          snprintf(buf, sizeof(buf), "pointLights[%d].position", i);
          ddgiTraceShader->setVec3(buf, pointLights[i].position);
          snprintf(buf, sizeof(buf), "pointLights[%d].color", i);
          ddgiTraceShader->setVec3(buf, pointLights[i].color);
          snprintf(buf, sizeof(buf), "pointLights[%d].intensity", i);
          ddgiTraceShader->setFloat(buf, pointLights[i].intensity);
          snprintf(buf, sizeof(buf), "pointLights[%d].linear", i);
          ddgiTraceShader->setFloat(buf, pointLights[i].linear);
          snprintf(buf, sizeof(buf), "pointLights[%d].quadratic", i);
          ddgiTraceShader->setFloat(buf, pointLights[i].quadratic);
        }
        ddgiTraceShader->setInt("numPointLights", np);

        int ns = std::min((int)spotLights.size(), MAX_LIGHTS);
        for (int i = 0; i < ns; i++) {
          snprintf(buf, sizeof(buf), "spotLights[%d].position", i);
          ddgiTraceShader->setVec3(buf, spotLights[i].position);
          snprintf(buf, sizeof(buf), "spotLights[%d].direction", i);
          ddgiTraceShader->setVec3(buf, spotLights[i].direction);
          snprintf(buf, sizeof(buf), "spotLights[%d].color", i);
          ddgiTraceShader->setVec3(buf, spotLights[i].color);
          snprintf(buf, sizeof(buf), "spotLights[%d].intensity", i);
          ddgiTraceShader->setFloat(buf, spotLights[i].intensity);
          snprintf(buf, sizeof(buf), "spotLights[%d].innerCutOff", i);
          ddgiTraceShader->setFloat(buf, spotLights[i].innerCutOff);
          snprintf(buf, sizeof(buf), "spotLights[%d].outerCutOff", i);
          ddgiTraceShader->setFloat(buf, spotLights[i].outerCutOff);
        }
        ddgiTraceShader->setInt("numSpotLights", ns);

        ddgiTraceShader->setBool("sun.enabled", sun.enabled);
        ddgiTraceShader->setVec3("sun.direction", sun.direction);
        ddgiTraceShader->setVec3("sun.color", sun.color);
        ddgiTraceShader->setFloat("sun.intensity", sun.intensity);
      }
      glDispatchCompute(groupsOf(probeCount * rays, 64), 1, 1);
      glMemoryBarrier(GL_SHADER_STORAGE_BARRIER_BIT);

      // --- Pass 2: blend ray results into the probe atlases (images 0/1) ---
      ddgiVolume->bindImagesForUpdate();
      ddgiVolume->bindRayBuffer(6);

      ddgiUpdateIrradianceShader->use();
      ddgiVolume->setGridUniforms(ddgiUpdateIrradianceShader);
      ddgiUpdateIrradianceShader->setMat3("u_randomRotation", randomRotation);
      ddgiUpdateIrradianceShader->setFloat("u_hysteresis",
                                           radianceSettings.ddgiHysteresis);
      ddgiUpdateIrradianceShader->setBool("u_firstFrame", firstFrame);
      glDispatchCompute(groupsOf(pc.x * IRR_SIDE, 8),
                        groupsOf(pc.y * pc.z * IRR_SIDE, 8), 1);

      ddgiUpdateDistanceShader->use();
      ddgiVolume->setGridUniforms(ddgiUpdateDistanceShader);
      ddgiUpdateDistanceShader->setMat3("u_randomRotation", randomRotation);
      ddgiUpdateDistanceShader->setFloat("u_hysteresis",
                                         radianceSettings.ddgiHysteresis);
      ddgiUpdateDistanceShader->setBool("u_firstFrame", firstFrame);
      ddgiUpdateDistanceShader->setFloat(
          "u_depthSharpness",
          static_cast<float>(radianceSettings.ddgiDepthSharpness));
      ddgiUpdateDistanceShader->setFloat("u_maxDistance", maxDist);
      glDispatchCompute(groupsOf(pc.x * DEP_SIDE, 8),
                        groupsOf(pc.y * pc.z * DEP_SIDE, 8), 1);

      glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

      // --- Pass 3: octahedral border copy ---
      ddgiBorderIrradianceShader->use();
      ddgiBorderIrradianceShader->setIVec3("ddgi_probeCounts", pc.x, pc.y, pc.z);
      ddgiBorderIrradianceShader->setInt("ddgi_side", IRR_SIDE);
      glDispatchCompute(groupsOf(pc.x * (IRR_SIDE + 2), 8),
                        groupsOf(pc.y * pc.z * (IRR_SIDE + 2), 8), 1);

      ddgiBorderDistanceShader->use();
      ddgiBorderDistanceShader->setIVec3("ddgi_probeCounts", pc.x, pc.y, pc.z);
      ddgiBorderDistanceShader->setInt("ddgi_side", DEP_SIDE);
      glDispatchCompute(groupsOf(pc.x * (DEP_SIDE + 2), 8),
                        groupsOf(pc.y * pc.z * (DEP_SIDE + 2), 8), 1);

      // Make image writes visible to fragment-shader texture fetches, unbind.
      glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT |
                      GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);
      glBindImageTexture(0, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RGBA16F);
      glBindImageTexture(1, 0, 0, GL_FALSE, 0, GL_READ_WRITE, GL_RG16F);

      // Switch back to the active rasterization shader (radiance or shadow map).
      shader->use();
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
  // All view-independent passes (shadow maps, DDGI probe atlases) for this frame
  // have now run. Mark them done so the second eye reuses the results instead of
  // regenerating them.
  g_sharedPassesDone = true;

  // Render scene - cache buffers still bound, fragment shader can read them.
  // Wireframe is scoped to the model pass only: the skybox stays solid and,
  // crucially, GL_LINE is reset before the post-process/composite passes so it
  // can't turn the full-screen HDR quads into stray lines.
  // Section / clip planes: push the active planes to the scene shader and
  // enable the matching GL_CLIP_DISTANCE slots (index 0 stays reserved for the
  // radar slice, so user planes use slots 1..N). Scoped to the model pass only
  // and disabled immediately after so the clip state can't leak into the skybox,
  // point-cloud, or post-process passes (whose shaders don't write
  // gl_ClipDistance, which would otherwise clip them with undefined values).
  int activeClipPlanes = clipPlaneTool.applyToShader(shader);
  for (int i = 0; i < activeClipPlanes; ++i)
    glEnable(GL_CLIP_DISTANCE0 + 1 + i);

  if (camera.wireframe)
    glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
  renderModels(shader, projection * view);
  if (camera.wireframe)
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

  for (int i = 0; i < activeClipPlanes; ++i)
    glDisable(GL_CLIP_DISTANCE0 + 1 + i);

  renderSkybox(projection, view, shader);
  renderPointClouds(shader, view, projection);

  // Render light gizmos (spheres/cones) while Ctrl is held
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
    // Explicitly bind the correct read framebuffer before depth sampling.
    // The scene was rendered into the HDR FBO (when HDR is on) or the default
    // framebuffer (non-HDR). Using GL_READ_FRAMEBUFFER leaves the draw
    // framebuffer untouched so ongoing rendering is not disrupted.
    if (preferences.hdrSettings.enabled && bloomRenderer != nullptr) {
      Engine::BloomSettings &bloomSettings = bloomRenderer->getSettings();
      glBindFramebuffer(GL_READ_FRAMEBUFFER, bloomSettings.hdrFBO);
    } else {
      glBindFramebuffer(GL_READ_FRAMEBUFFER, 0);
    }

    // Async double-buffered PBO depth sampling — eliminates 9 synchronous
    // glReadPixels stalls per frame that were caused by
    // Camera::getDistanceToNearestObject. Pattern: consume previous frame's
    // result (no stall), then kick off this frame's read (GPU-to-PBO transfer
    // happens while the CPU continues). 1-frame latency is imperceptible for
    // movement speed and auto-convergence.
    {
      int readIdx = 1 - g_distancePBOWriteIdx;
      int writeIdx = g_distancePBOWriteIdx;

      // Consume previous frame's result — GPU has already finished by now.
      if (g_distancePBOReady) {
        glBindBuffer(GL_PIXEL_PACK_BUFFER, g_distancePBO[readIdx]);
        float *ptr = (float *)glMapBuffer(GL_PIXEL_PACK_BUFFER, GL_READ_ONLY);
        if (ptr) {
          g_cachedCenterDepth = *ptr;
          glUnmapBuffer(GL_PIXEL_PACK_BUFFER);
        }
        glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      }

      // Kick off this frame's async read (nullptr → GPU writes directly to
      // PBO). Sample the centre of the free-area viewport, not the window
      // centre. For HDR the read FBO is viewport-sized (origin 0,0); for
      // non-HDR it's the full-window default buffer (scene offset by
      // g_viewportX).
      bool usingHDRRead =
          preferences.hdrSettings.enabled && bloomRenderer != nullptr;
      int centerX =
          (usingHDRRead ? 0 : g_viewportX) + g_viewportWidth / 2;
      int centerY = g_viewportHeight / 2;
      glBindBuffer(GL_PIXEL_PACK_BUFFER, g_distancePBO[writeIdx]);
      glReadPixels(centerX, centerY, 1, 1, GL_DEPTH_COMPONENT,
                   GL_FLOAT, nullptr);
      glBindBuffer(GL_PIXEL_PACK_BUFFER, 0);
      g_distancePBOWriteIdx = readIdx; // swap for next frame
      g_distancePBOReady = true;

      // Reconstruct world-space distance from cached (previous-frame) depth.
      float distanceToNearestObject = preferences.farPlane;
      if (g_cachedCenterDepth < 1.0f) {
        glm::vec4 ndc =
            glm::vec4(0.0f, 0.0f, g_cachedCenterDepth * 2.0f - 1.0f, 1.0f);
        glm::mat4 invPV = glm::inverse(projection * view);
        glm::vec4 wp = invPV * ndc;
        wp /= wp.w;
        distanceToNearestObject = glm::distance(camera.Position, glm::vec3(wp));
      }

      camera.UpdateDistanceToObject(distanceToNearestObject);
      float largestDimension = calculateLargestModelDimension();
      camera.AdjustMovementSpeed(distanceToNearestObject, largestDimension,
                                 preferences.farPlane);
    }

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

  // Tell the cursor system where the scene was drawn so it samples depth and
  // maps the mouse within the free-area sub-viewport. For HDR the scene fills
  // the (viewport-sized) HDR FBO at the origin; for non-HDR it sits at the free
  // offset inside the full-window default framebuffer.
  {
    bool usingHDR = preferences.hdrSettings.enabled && bloomRenderer != nullptr;
    cursorManager.setViewport(usingHDR ? 0 : g_viewportX, 0, g_viewportWidth,
                              g_viewportHeight, g_viewportX, g_viewportTopInset);
  }

  // Calculate cursor position AFTER scene rendering but BEFORE cursor
  // rendering This ensures we read scene depth, not cursor depth from the
  // buffer
  cursorManager.updateCursorPosition(window, projection, view, shader, false,
                                     isStereo, leftProjection, leftView,
                                     rightProjection, rightView);

  // Update SpaceMouse cursor anchor when cursor position changes (uses the raw
  // scene-depth cursor, before any gizmo snap below, so navigation isn't pulled
  // onto a transient hover target).
  updateSpaceMouseCursorAnchor();

  // While the transform gizmo is hovered or being dragged, glue the 3D cursor
  // onto the handle at its true depth. The gizmo is drawn always-on-top, so
  // without this the cursor would sink to whatever geometry sits behind it and
  // the two tools would disagree about where the handle is in space.
  if (transformGizmo.enabled && transformGizmo.hasTarget() &&
      (ctrlPressed || gizmoDragging) && transformGizmo.hasInteractionPoint()) {
    cursorManager.setForcedCursorPosition(transformGizmo.interactionPoint());
  }

  // Update shader uniforms for cursors (use active shader, not original
  // shader)
  cursorManager.updateShaderUniforms(shader);

  // Render orbit center if needed (during mouse orbit or SpaceMouse navigation)
  if (!orbitFollowsCursor && cursorManager.isShowOrbitCenter() &&
      (camera.IsOrbiting || spaceMouseInput.IsNavigating() ||
       cursorManager.isAlwaysShowOrbitCenter())) {
    // Compute the authoritative display point for each mode/state:
    // - SpaceMouse navigating : NavLib-driven camera (always up-to-date)
    // - Left-mouse orbiting   : camera.OrbitPoint (set by orbit-start code)
    // - CLICK idle            : spaceMouseClickAnchor (click-set, never drifts)
    // - CONTINUOUS/ON_START   : cursor position (current / predicted pivot)
    // - DISABLED              : camera.OrbitPoint (best available fallback)
    glm::vec3 orbitPointToDisplay;
    if (spaceMouseInput.IsNavigating()) {
      orbitPointToDisplay = spaceMouseCameraPtr->OrbitPoint;
    } else if (camera.IsOrbiting) {
      orbitPointToDisplay = camera.OrbitPoint;
    } else {
      switch (preferences.spaceMouseAnchorMode) {
      case GUI::SPACEMOUSE_ANCHOR_CLICK:
        orbitPointToDisplay = spaceMouseClickAnchorSet ? spaceMouseClickAnchor
                                                       : camera.OrbitPoint;
        break;
      case GUI::SPACEMOUSE_ANCHOR_CONTINUOUS:
      case GUI::SPACEMOUSE_ANCHOR_ON_START:
        orbitPointToDisplay = cursorManager.isCursorPositionValid()
                                  ? cursorManager.getCursorPosition()
                                  : camera.OrbitPoint;
        break;
      case GUI::SPACEMOUSE_ANCHOR_DISABLED:
      default:
        orbitPointToDisplay = camera.OrbitPoint;
        break;
      }
    }
    cursorManager.renderOrbitCenter(projection, view, orbitPointToDisplay);
  }

  if (camera.IsPanning == false) {
    cursorManager.renderCursors(projection, view);
  }

  // Plugin world-space overlays (called once per eye with this eye's matrices).
  // The migrated MeasurementTool draws its measurement overlay here via its
  // MeasurementPlugin adapter; other plugins (e.g. the Crosshair example) draw
  // alongside it.
  g_pluginManager.renderViewport(g_pluginContext, projection, view,
                                 camera.Position);

  // Render the transform gizmo overlay for the selected object (per eye).
  // Only shown while Ctrl is held (or while an in-progress drag keeps it alive).
  if (ctrlPressed || gizmoDragging) {
    transformGizmo.render(projection, view, camera.Position);
  }

  // Render the section/clip-plane overlay (translucent quad + normal arrow);
  // only while the tool is active (panel/toolbar open).
  clipPlaneTool.render(projection, view, camera.Position);

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

void renderModels(Engine::Shader *shader, const glm::mat4 &viewProj,
                  bool enableFrustumCulling) {
  // Don't do lighting setup for the depth shader
  if (shader != simpleDepthShader) {
    // Bind skybox for reflections
    bindSkyboxUniforms(shader);

    // Set lighting mode
    shader->setInt("lightingMode", static_cast<int>(currentLightingMode));
    shader->setBool("enableShadows", enableShadows);

    // Set sun properties (not set in renderEye for any mode)
    if (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING ||
        currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING) {
      shader->setBool("sun.enabled", sun.enabled);
      shader->setVec3("sun.direction", sun.direction);
      shader->setVec3("sun.color", sun.color);
      shader->setFloat("sun.intensity", sun.intensity);
    }
  }

  // Uniforms constant across all models — set once before the loop
  shader->setBool("selectionMode", selectionMode);
  shader->setInt("selectedMeshIndex", currentSelectedMeshIndex);
  shader->setBool("isMeshSelected", currentSelectedMeshIndex >= 0);
  shader->setFloat("emissiveIntensity", radianceSettings.emissiveIntensity);

  // Extract frustum planes once — reused for every model below instead of
  // recomputing 6 sqrt-normalizations per model per renderModels call.
  // Only extract when culling is actually enabled.
  std::array<glm::vec4, 6> frustumPlanes;
  if (enableFrustumCulling)
    frustumPlanes = Camera::extractFrustumPlanes(viewProj);

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

    // Frustum culling: skip models whose bounding sphere is outside the view
    // frustum
    if (enableFrustumCulling && model.boundingSphereRadius > 0.0f) {
      glm::vec3 worldCenter =
          glm::vec3(modelMatrix * glm::vec4(model.localBoundsCenter, 1.0f));
      float maxScale =
          glm::max(model.scale.x, glm::max(model.scale.y, model.scale.z));
      float worldRadius = model.boundingSphereRadius * maxScale;
      if (!Camera::isInFrustumPlanes(worldCenter, worldRadius, frustumPlanes))
        continue;
    }

    // Set model matrix in shader
    shader->setMat4("model", modelMatrix);

    // Calculate and set normal matrix for proper normal transformation.
    // For uniform-scale models (the common case) the inverse-transpose
    // simplifies to mat3(modelMatrix), saving an expensive matrix inversion per
    // model.
    glm::mat3 normalMatrix;
    if (std::abs(model.scale.x - model.scale.y) < 1e-6f &&
        std::abs(model.scale.y - model.scale.z) < 1e-6f) {
      normalMatrix = glm::mat3(modelMatrix);
    } else {
      normalMatrix = glm::transpose(glm::inverse(glm::mat3(modelMatrix)));
    }
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

    // Set per-model selection state (isSelected depends on model index i)
    shader->setBool("isSelected",
                    selectionMode && (i == currentSelectedIndex) &&
                        (currentSelectedType == SelectedType::Model));

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

    // Section/clip planes: the matching GL_CLIP_DISTANCE slots are already
    // enabled by renderEye for the model pass, so feed the instanced program the
    // same planes (otherwise its gl_ClipDistance writes default to "keep").
    clipPlaneTool.applyToShader(instancedShader);

    // Bind shadow map
    if (enableShadows) {
      glActiveTexture(GL_TEXTURE0 + Engine::SHADOW_MAP_TEXTURE_UNIT);
      glBindTexture(GL_TEXTURE_2D, depthMap);
      instancedShader->setInt("shadowMap", Engine::SHADOW_MAP_TEXTURE_UNIT);
      instancedShader->setFloat("shadowTexelWorldSize", shadowTexelWorldSize);
    }

    // Render instances
    brushTool.renderInstances(instancedShader, currentScene.models);

    // Restore active shader
    shader->use();
  }
}

void renderPointClouds(Engine::Shader *shader, const glm::mat4 &view,
                       const glm::mat4 &projection) {
  // Skip point cloud rendering for depth pass as points don't cast good
  // shadows
  if (shader == simpleDepthShader)
    return;

  // Schütz compute rasterizer: one clear + one dispatch per cloud + one resolve.
  // Only engage it when the scene actually has a visible point cloud to draw.
  // beginFrame()/endFrame() always cost a memory barrier plus a full-viewport
  // resolve pass that writes gl_FragDepth for every pixel, so running them for a
  // model-only scene (the common default) is pure waste. The framebuffer SSBO is
  // pre-cleared at allocation and re-cleared by every endFrame(), and skipped
  // frames never touch it, so the "already cleared" invariant beginFrame() relies
  // on still holds whenever a point cloud reappears.
  bool hasVisibleCloud = false;
  for (const auto &pc : currentScene.pointClouds) {
    if (pc.visible) {
      hasVisibleCloud = true;
      break;
    }
  }
  bool useCompute = computePointCloudRenderer &&
                    computePointCloudRenderer->isInitialized() &&
                    hasVisibleCloud;
  if (useCompute) {
    // Feed the active section/clip planes to the compute rasterizer so clipped
    // points are discarded (point clouds don't use gl_ClipDistance).
    glm::vec4 worldPlanes[Engine::MAX_CLIP_PLANES];
    int nClip = clipPlaneTool.collectEnabledPlanes(worldPlanes);
    computePointCloudRenderer->setClipPlanes(nClip, worldPlanes);
    computePointCloudRenderer->beginFrame();
  }

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

    if (useCompute) {
      // Schütz compute path: one workgroup per batch, with per-batch frustum
      // culling and packed 10/20/30-bit coordinate decoding.
      if (pointCloud.computeBatchSSBO != 0 && pointCloud.numBatches > 0) {
        // Adaptive splatting: 0 disables it (single-pixel rasterize), otherwise
        // pass the user's max radius clamp so close-up/sparse views fill gaps.
        int splatMaxRadius = preferences.pointSplatSettings.enabled
                                 ? preferences.pointSplatSettings.maxRadius
                                 : 0;
        computePointCloudRenderer->renderNode(
            pointCloud.computeBatchSSBO, pointCloud.computeXyz12bSSBO,
            pointCloud.computeXyz8bSSBO, pointCloud.computeXyz4bSSBO,
            pointCloud.computeRGBASSBO, pointCloud.numBatches,
            pointCloud.computePointsPerThread,
            projection * view * modelMatrix, // uMVP
            view * modelMatrix,              // uModelView (for precision level)
            projection,                      // uProj      (for precision level)
            modelMatrix,                     // model (for local-space clip planes)
            splatMaxRadius);                 // adaptive splat radius clamp (px)
      }
    } else if (pointCloud.octreeRoot) {
      // GL_POINTS fallback: octree-based rendering unchanged
      glm::vec3 cameraPosition = camera.Position;
      OctreePointCloudManager::updateLOD(pointCloud, cameraPosition);
      glBindVertexArray(pointCloud.vao);
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
      shader->setVec4("outlineColor", glm::vec4(0.0f, 1.0f, 0.0f, 1.0f));

      glBindVertexArray(pointCloud.chunkOutlineVAO);
      glDrawArrays(
          GL_LINES, 0,
          static_cast<GLsizei>(pointCloud.chunkOutlineVertices.size()));
      glBindVertexArray(0);

      shader->setBool("isChunkOutline", false);
    }
  }

  // renderNode() calls m_rasterShader->use() internally, leaving the compute
  // shader active.  Re-activate the scene shader so isPointCloud=false lands
  // in the right program; without this, meshes render normals-as-colour on
  // every subsequent frame.
  shader->use();
  shader->setBool("isPointCloud", false);

  // Composite compute result into the HDR framebuffer.
  // endFrame() internally calls m_colorLookupShader->use() then
  // m_resolveShader->use(), leaving the resolve shader as the active program.
  // Re-bind the scene shader afterward so callers (e.g. renderLightVisualizations)
  // get correct glUniform* routing.
  if (useCompute) {
    computePointCloudRenderer->endFrame();
    shader->use(); // restore scene shader after resolve pass
  }
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
  // ---- Radar coordinate spaces -------------------------------------------
  // "content" space: the scene seen from straight above the camera, scaled by
  //   radarScale.  +Y here is the direction the camera is currently looking.
  // "scope" space: a screen-fixed unit circle of radius R used for the
  //   background, range rings, heading marker and camera dot.
  // Both share the same aspect-corrected projection so the scope stays round
  // and lands in the same screen corner regardless of window aspect ratio.
  const float TWO_PI = 6.28318530718f;
  float aspect =
      (windowHeight > 0) ? (float)windowWidth / (float)windowHeight : 1.0f;
  float R = preferences.radarRadius;

  // Keep the whole scope on-screen regardless of the configured position.
  // (x is divided by aspect on screen, and the heading marker pokes a little
  // past the top of the ring.)
  float halfX = R / aspect;
  position.x = glm::clamp(position.x, -1.0f + halfX, 1.0f - halfX);
  position.y = glm::clamp(position.y, -1.0f + R, 1.0f - R * 1.25f);

  // Aspect-correct orthographic projection, offset to the radar position. The
  // position offset is applied *after* the ortho so it is not squashed by the
  // aspect correction.
  glm::mat4 orthoMat =
      glm::ortho(-aspect, aspect, -1.0f, 1.0f, -1000.0f, 1000.0f);
  glm::mat4 radarProj =
      glm::translate(glm::mat4(1.0f), glm::vec3(position, 0.0f)) * orthoMat;

  // Content zoom (world units -> radar units).  Auto-fit frames the convergence
  // plane at a fixed fraction of the scope radius so the green convergence line
  // stays inside the radar no matter how far out the convergence is set.
  const float CONV_FIT = 0.5f; // convergence sits at 50% of the scope radius
  float effScale = radarScale;
  if (preferences.radarAutoFit)
    effScale = R * CONV_FIT / glm::max(focaldist, 0.001f);

  // world -> camera eye space -> top-down -> scaled.  The +90 deg rotation
  // about X (camera forward becomes +Y / up) replaces the previous broken
  // rotations: glm::rotate expects radians, the old code passed raw degrees.
  glm::mat4 contentView =
      glm::rotate(glm::mat4(1.0f), glm::radians(90.0f),
                  glm::vec3(1.0f, 0.0f, 0.0f)) *
      glm::scale(glm::mat4(1.0f), glm::vec3(effScale)) * view;

  // World-space height of the radar slice plane: a bit above the camera so the
  // roof / ceiling is cut away and interiors are visible from above.  Camera
  // world position is recovered from the view matrix so it always matches the
  // matrix actually being rendered (e.g. during SpaceMouse navigation).
  glm::vec3 radarCamPos = glm::vec3(glm::inverse(view)[3]);
  float radarClipHeight = radarCamPos.y + preferences.radarSliceOffset;

  // ---- Build the stereo frustum outline in world space -------------------
  glm::vec2 frust_ndc[6]{};
  glm::vec4 frust_world[12]{}; // idx 0-5 left eye, idx 6-11 right eye

  glm::mat4 defaultView =
      glm::lookAt(glm::vec3(0.0f, 0.0f, 0.0f),  // eye at origin
                  glm::vec3(0.0f, 0.0f, 1.0f),  // looking towards +Z
                  glm::vec3(0.0f, 1.0f, 0.0f)); // up is +Y

  glm::vec4 fd_ndc =
      projection * defaultView * glm::vec4(0, 0, focaldist, 1.0f);
  fd_ndc = divw(fd_ndc);

  // Cap the visualized far plane to a few times the convergence distance.
  // Inverse-projecting the true far plane (NDC z ~ 1, e.g. 1000 units) is
  // numerically ill-conditioned, so once the disparity is exaggerated the far
  // corners jitter wildly frame to frame. A capped far is well within the
  // scope's precision and is off-scope (clipped) anyway.
  float radarFarDist =
      glm::min(preferences.farPlane, glm::max(focaldist, 0.1f) * 2.5f);
  glm::vec4 far_ndc =
      divw(projection * defaultView * glm::vec4(0, 0, radarFarDist, 1.0f));

  frust_ndc[0] = glm::vec2(-1.0f, -1.0f);      // near left
  frust_ndc[1] = glm::vec2(1.0f, -1.0f);       // near right
  frust_ndc[2] = glm::vec2(-1.0f, far_ndc.z);  // far left
  frust_ndc[3] = glm::vec2(1.0f, far_ndc.z);   // far right
  frust_ndc[4] = glm::vec2(-1.0f, fd_ndc.z);   // focal/convergence left
  frust_ndc[5] = glm::vec2(1.0f, fd_ndc.z);    // focal/convergence right

  glm::mat4 inv_left = glm::inverse(leftprojection * leftview);
  glm::mat4 inv_right = glm::inverse(rightprojection * rightview);
  for (int k = 0; k < 6; k++) {
    glm::vec4 q(frust_ndc[k].x, 0.0f, frust_ndc[k].y, 1.0f);
    frust_world[k] = divw(inv_left * q);
    frust_world[k + 6] = divw(inv_right * q);
  }

  // At a comfortable eye separation the two frustums overlap on the radar.
  // Exaggerate the baseline by scaling each left/right corner pair apart from
  // its midpoint (the mono frustum). This keeps the convergence crossing exact
  // (left == right there, so it is unaffected) while spreading the near and far
  // ends so the two eyes are clearly distinguishable.  The factor is chosen so
  // the near-plane gap is radarFrustumSpread * scope-radius regardless of zoom.
  if (preferences.radarFrustumSpread > 0.0f) {
    float eyeSepRadar = preferences.separation * effScale;
    if (eyeSepRadar > 1e-6f) {
      float spreadF =
          glm::max(1.0f, preferences.radarFrustumSpread * R / eyeSepRadar);
      for (int k = 0; k < 6; k++) {
        glm::vec4 mid = 0.5f * (frust_world[k] + frust_world[k + 6]);
        frust_world[k] = mid + (frust_world[k] - mid) * spreadF;
        frust_world[k + 6] = mid + (frust_world[k + 6] - mid) * spreadF;
      }
    }
  }

  // Pack each eye as 4 outline edges (8 verts) followed by the
  // focal/convergence line (2 verts).  Left eye -> verts [0,10), right -> [10,20).
  const int numPoints = 20;
  GLfloat buf[numPoints * 3];
  auto packEye = [](GLfloat *dst, const glm::vec4 *w) {
    int n = 0;
    auto put = [&](int idx) {
      dst[n++] = w[idx].x;
      dst[n++] = w[idx].y;
      dst[n++] = w[idx].z;
    };
    put(0); put(2); // near left  -> far left
    put(1); put(3); // near right -> far right
    put(0); put(1); // near plane
    put(2); put(3); // far plane
    put(4); put(5); // focal / convergence line
  };
  packEye(buf, frust_world);          // left eye
  packEye(buf + 30, frust_world + 6); // right eye

  // ---- Build screen-fixed scope geometry ---------------------------------
  const int SEGS = 72;
  std::vector<glm::vec3> disc;
  disc.reserve(SEGS + 2);
  disc.push_back(glm::vec3(0.0f));
  for (int s = 0; s <= SEGS; ++s) {
    float a = (float)s / SEGS * TWO_PI;
    disc.push_back(glm::vec3(R * std::cos(a), R * std::sin(a), 0.0f));
  }
  auto makeRing = [&](float r) {
    std::vector<glm::vec3> ring;
    ring.reserve(SEGS);
    for (int s = 0; s < SEGS; ++s) {
      float a = (float)s / SEGS * TWO_PI;
      ring.push_back(glm::vec3(r * std::cos(a), r * std::sin(a), 0.0f));
    }
    return ring;
  };
  std::vector<glm::vec3> ringOuter = makeRing(R);
  std::vector<glm::vec3> ringMid = makeRing(R * 0.5f);
  std::vector<glm::vec3> spokes = {
      glm::vec3(0.0f, -R, 0.0f), glm::vec3(0.0f, R, 0.0f),
      glm::vec3(-R, 0.0f, 0.0f), glm::vec3(R, 0.0f, 0.0f)};
  float hb = R * 0.10f, ht = R * 0.16f, hy = R + R * 0.03f;
  std::vector<glm::vec3> heading = {glm::vec3(-hb, hy, 0.0f),
                                    glm::vec3(hb, hy, 0.0f),
                                    glm::vec3(0.0f, hy + ht, 0.0f)};
  std::vector<glm::vec3> centerDot;
  centerDot.push_back(glm::vec3(0.0f));
  for (int s = 0; s <= 14; ++s) {
    float a = (float)s / 14 * TWO_PI;
    float cr = R * 0.045f;
    centerDot.push_back(glm::vec3(cr * std::cos(a), cr * std::sin(a), 0.0f));
  }

  // ---- Colors ------------------------------------------------------------
  glm::vec4 bgColor(0.03f, 0.05f, 0.08f, 0.66f);      // translucent dark scope
  glm::vec4 ringColor(0.45f, 0.85f, 1.00f, 0.85f);    // outer border
  glm::vec4 ringMidColor(0.35f, 0.55f, 0.70f, 0.30f); // faint range ring
  glm::vec4 spokeColor(0.35f, 0.55f, 0.70f, 0.25f);   // faint crosshair
  glm::vec4 leftEyeColor(0.30f, 0.70f, 1.00f, 0.95f);  // left frustum
  glm::vec4 rightEyeColor(1.00f, 0.55f, 0.30f, 0.95f); // right frustum
  glm::vec4 focalColor(0.35f, 1.00f, 0.45f, 0.95f);    // convergence (green)
  glm::vec4 headingColor(0.45f, 0.85f, 1.00f, 0.95f);
  glm::vec4 centerColor(1.00f, 1.00f, 1.00f, 0.95f);

  // ---- GL state ----------------------------------------------------------
  glUseProgram(0);
  glBindVertexArray(0);
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, 0);

  glViewport(0, 0, windowWidth, windowHeight);
  glDisable(GL_DEPTH_TEST);
  glEnable(GL_BLEND);
  glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

  GLuint vao, vbo;
  glGenVertexArrays(1, &vao);
  glGenBuffers(1, &vbo);
  glBindVertexArray(vao);
  glBindBuffer(GL_ARRAY_BUFFER, vbo);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(GLfloat),
                        (void *)0);
  glEnableVertexAttribArray(0);

  shader->use();
  shader->setBool("isPointCloud", false);
  shader->setBool("isChunkOutline", true);
  shader->setMat4("model", glm::mat4(1.0f));

  auto uploadVec = [&](const std::vector<glm::vec3> &v) {
    glBufferData(GL_ARRAY_BUFFER, (GLsizeiptr)(v.size() * sizeof(glm::vec3)),
                 v.data(), GL_DYNAMIC_DRAW);
  };

  std::vector<GLenum> targets;
  if (isStereoWindow) {
    targets.push_back(GL_BACK_LEFT);
    targets.push_back(GL_BACK_RIGHT);
  } else {
    targets.push_back(GL_BACK);
  }

  // The scope is clipped to a circle using the stencil buffer.
  glDisable(GL_SCISSOR_TEST); // ensure the clear covers the whole buffer
  glEnable(GL_STENCIL_TEST);
  glStencilMask(0xFF);
  glClearStencil(0);
  glClear(GL_STENCIL_BUFFER_BIT);

  for (GLenum target : targets) {
    glDrawBuffer(target);

    // (1) translucent background disc; also writes the circular stencil mask.
    shader->setMat4("projection", radarProj);
    shader->setMat4("view", glm::mat4(1.0f));
    glStencilFunc(GL_ALWAYS, 1, 0xFF);
    glStencilOp(GL_KEEP, GL_KEEP, GL_REPLACE);
    glStencilMask(0xFF);
    uploadVec(disc);
    shader->setVec4("outlineColor", bgColor);
    glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)disc.size());

    // From here on, draw only inside the circle and leave the mask untouched.
    glStencilFunc(GL_EQUAL, 1, 0xFF);
    glStencilMask(0x00);

    // (2) the scene from above (clipped to the scope).
    if (renderScene) {
      shader->setMat4("projection", radarProj);
      shader->setMat4("view", contentView);
      shader->setMat4("model", glm::mat4(1.0f));
      shader->setBool("isChunkOutline", false);

      // Slice the scene horizontally just above the camera so we see the
      // interior from above instead of the roof.
      if (preferences.radarSliceEnabled) {
        shader->setBool("radarClipEnabled", true);
        shader->setFloat("radarClipHeight", radarClipHeight);
        glEnable(GL_CLIP_DISTANCE0);
      }

      // Depth-test the radar scene so the top-down view shows the *topmost*
      // surfaces.  Without it (painter's order) the surfaces drawn last win,
      // which are typically undersides, making the scene look like it is
      // viewed from below.  The radar ortho looks straight down, so higher-up
      // geometry maps nearer and GL_LESS keeps it.  Clear depth first so the
      // radar only occludes against itself, not the main frame.  Culling is
      // disabled so the highest surface always wins regardless of winding.
      glEnable(GL_DEPTH_TEST);
      glDepthFunc(GL_LESS);
      glDepthMask(GL_TRUE);
      glClear(GL_DEPTH_BUFFER_BIT);
      glDisable(GL_CULL_FACE);

      // The shadow map is built from the full scene (roof included), so a
      // sliced interior would stay in the roof's shadow and render black.
      // Disable shadows for the radar pass so the floor plan is fully lit.
      bool savedShadows = enableShadows;
      enableShadows = false;
      // Crank the exposure for the radar scene. The radar draws straight to the
      // back buffer (no HDR/bloom tone-map pass), so the shader tone-maps and
      // gamma-corrects in-place with this big boost; > 0 selects the radar path.
      shader->setFloat("radarBrightness",
                       glm::max(0.001f, preferences.radarSceneBrightness));
      renderModels(shader, radarProj * contentView, /*frustumCulling=*/false);
      enableShadows = savedShadows;

      glEnable(GL_CULL_FACE);
      glDisable(GL_DEPTH_TEST);
      glDisable(GL_CLIP_DISTANCE0);

      // renderModels rebinds buffers and may switch programs - restore ours.
      // Reset radarBrightness to 0 so the next (main scene) pass is unaffected.
      shader->use();
      shader->setFloat("radarBrightness", 0.0f);
      shader->setBool("radarClipEnabled", false);
      shader->setBool("isPointCloud", false);
      shader->setBool("isChunkOutline", true);
      shader->setMat4("model", glm::mat4(1.0f));
      glBindVertexArray(vao);
      glBindBuffer(GL_ARRAY_BUFFER, vbo);
    }

    // (3) faint range ring + crosshair, on top of the scene (screen-fixed).
    shader->setMat4("projection", radarProj);
    shader->setMat4("view", glm::mat4(1.0f));
    glLineWidth(1.0f);
    uploadVec(ringMid);
    shader->setVec4("outlineColor", ringMidColor);
    glDrawArrays(GL_LINE_LOOP, 0, (GLsizei)ringMid.size());
    uploadVec(spokes);
    shader->setVec4("outlineColor", spokeColor);
    glDrawArrays(GL_LINES, 0, (GLsizei)spokes.size());

    // (4) stereo frustum: per-eye outlines + green convergence lines.
    shader->setMat4("view", contentView);
    glLineWidth(1.5f);
    glBufferData(GL_ARRAY_BUFFER, sizeof(buf), buf, GL_DYNAMIC_DRAW);
    shader->setVec4("outlineColor", leftEyeColor);
    glDrawArrays(GL_LINES, 0, 8);
    shader->setVec4("outlineColor", rightEyeColor);
    glDrawArrays(GL_LINES, 10, 8);
    shader->setVec4("outlineColor", focalColor);
    glDrawArrays(GL_LINES, 8, 2);
    glDrawArrays(GL_LINES, 18, 2);

    // (5) on-top overlays (not circle-clipped): border, heading, camera dot.
    glDisable(GL_STENCIL_TEST);
    shader->setMat4("projection", radarProj);
    shader->setMat4("view", glm::mat4(1.0f));

    glLineWidth(2.0f);
    uploadVec(ringOuter);
    shader->setVec4("outlineColor", ringColor);
    glDrawArrays(GL_LINE_LOOP, 0, (GLsizei)ringOuter.size());

    uploadVec(heading);
    shader->setVec4("outlineColor", headingColor);
    glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)heading.size());

    uploadVec(centerDot);
    shader->setVec4("outlineColor", centerColor);
    glDrawArrays(GL_TRIANGLE_FAN, 0, (GLsizei)centerDot.size());

    glEnable(GL_STENCIL_TEST); // restore for the next target
  }

  // ---- restore GL state --------------------------------------------------
  shader->setBool("isChunkOutline", false);
  glLineWidth(1.0f);
  glDisable(GL_CLIP_DISTANCE0);
  glDisable(GL_STENCIL_TEST);
  glStencilMask(0xFF);
  glStencilFunc(GL_ALWAYS, 0, 0xFF);
  glStencilOp(GL_KEEP, GL_KEEP, GL_KEEP);
  glDisable(GL_BLEND);
  glEnable(GL_DEPTH_TEST);

  glBindVertexArray(0);
  glBindBuffer(GL_ARRAY_BUFFER, 0);
  glDeleteVertexArrays(1, &vao);
  glDeleteBuffers(1, &vbo);
}

void renderLightVisualizations(Engine::Shader *shader) {
  // Ensure the scene shader is the active program before setting any uniforms
  // or submitting draw calls.  renderPointClouds (and other subsystems) may
  // leave a different program bound via their own internal shader->use() calls.
  shader->use();

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
    shader->setMat3("normalMatrix", glm::mat3(model)); // uniform scale — no need for inverse-transpose
    shader->setBool("isPointCloud", false);
    shader->setBool("isSelected", false);

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
    shader->setMat3("normalMatrix", glm::transpose(glm::inverse(glm::mat3(model)))); // non-uniform scale
    shader->setBool("isPointCloud", false);
    shader->setBool("isSelected", false);

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

// Compute the combined world-space axis-aligned bounding box of every model and
// point cloud in the current scene. Returns false (leaving outMin/outMax
// untouched) when the scene has no measurable content. Translation and scale are
// applied; rotation is intentionally ignored to keep this cheap (matching the
// SpaceMouse-bounds convention this used to inline).
bool getSceneWorldBounds(glm::vec3 &outMin, glm::vec3 &outMax) {
  glm::vec3 mn(FLT_MAX), mx(-FLT_MAX);

  // Include models in bounding box calculation
  for (const auto &model : currentScene.models) {
    for (const auto &mesh : model.getMeshes()) {
      for (const auto &vertex : mesh.vertices) {
        glm::vec3 worldPos =
            model.position + (glm::vec3(vertex.position) * model.scale);
        mn = glm::min(mn, worldPos);
        mx = glm::max(mx, worldPos);
      }
    }
  }

  // Include point clouds in bounding box calculation
  for (const auto &pointCloud : currentScene.pointClouds) {
    if (pointCloud.hasBounds()) {
      // Fast path: use the pre-computed bounds stored at load time
      const glm::vec3 pcMin =
          pointCloud.position + (pointCloud.boundsMin * pointCloud.scale);
      const glm::vec3 pcMax =
          pointCloud.position + (pointCloud.boundsMax * pointCloud.scale);
      mn = glm::min(mn, pcMin);
      mx = glm::max(mx, pcMax);
    } else if (pointCloud.octreeRoot) {
      // Legacy: octree was built (e.g. by an older code path)
      const glm::vec3 pcMin =
          pointCloud.position + (pointCloud.octreeBoundsMin * pointCloud.scale);
      const glm::vec3 pcMax =
          pointCloud.position + (pointCloud.octreeBoundsMax * pointCloud.scale);
      mn = glm::min(mn, pcMin);
      mx = glm::max(mx, pcMax);
    } else if (!pointCloud.points.empty()) {
      // Last resort: iterate CPU-side points (only used by very old load paths)
      for (const auto &point : pointCloud.points) {
        const glm::vec3 worldPos =
            pointCloud.position + (point.position * pointCloud.scale);
        mn = glm::min(mn, worldPos);
        mx = glm::max(mx, worldPos);
      }
    }
  }

  if (mn.x == FLT_MAX)
    return false; // no content

  outMin = mn;
  outMax = mx;
  return true;
}

void updateSpaceMouseBounds() {
  // Recalculate the combined bounding box for SpaceMouse navigation. Called when
  // models/point clouds are loaded, deleted, transformed, or a scene is loaded,
  // so the bounds stay accurate without per-frame updates.
  glm::vec3 modelMin, modelMax;
  if (!getSceneWorldBounds(modelMin, modelMax)) {
    // Fallback if no content found
    modelMin = glm::vec3(-5.0f);
    modelMax = glm::vec3(5.0f);
  }

  // Update SpaceMouse with new bounds
  spaceMouseInput.SetModelExtents(modelMin, modelMax);
}

// Smoothly frame the whole scene from a standard axis-aligned or isometric
// angle, the way a CAD/inspection viewer's numpad views work. viewId:
//   0 Front (-Z)  1 Back (+Z)  2 Right (-X)  3 Left (+X)
//   4 Top  (-Y)   5 Bottom (+Y)            6 Isometric
// The camera distance is chosen so the scene's bounding sphere fits the current
// vertical field of view with a small margin. The orbit pivot is re-anchored to
// the scene centre so subsequent orbiting turns around what was just framed.
void applyStandardView(int viewId) {
  glm::vec3 mn, mx, center;
  float radius;
  if (getSceneWorldBounds(mn, mx)) {
    center = (mn + mx) * 0.5f;
    radius = glm::length(mx - mn) * 0.5f; // bounding-sphere radius
  } else {
    center = glm::vec3(0.0f);
    radius = 5.0f;
  }
  if (radius < 0.001f)
    radius = 1.0f;

  // Front = direction from the camera toward the scene centre. Pick an up vector
  // that is never parallel to Front so the look-at basis stays well defined.
  glm::vec3 front;
  glm::vec3 up(0.0f, 1.0f, 0.0f);
  switch (viewId) {
  case 1:
    front = glm::vec3(0.0f, 0.0f, 1.0f);
    break; // Back
  case 2:
    front = glm::vec3(-1.0f, 0.0f, 0.0f);
    break; // Right (camera on +X)
  case 3:
    front = glm::vec3(1.0f, 0.0f, 0.0f);
    break; // Left (camera on -X)
  case 4:
    front = glm::vec3(0.0f, -1.0f, 0.0f);
    up = glm::vec3(0.0f, 0.0f, -1.0f);
    break; // Top (look down)
  case 5:
    front = glm::vec3(0.0f, 1.0f, 0.0f);
    up = glm::vec3(0.0f, 0.0f, 1.0f);
    break; // Bottom (look up)
  case 6:
    front = glm::normalize(glm::vec3(-1.0f, -1.0f, -1.0f));
    break; // Isometric
  case 0:
  default:
    front = glm::vec3(0.0f, 0.0f, -1.0f);
    break; // Front
  }
  front = glm::normalize(front);

  // Distance so the bounding sphere fits inside the vertical FOV (+20% margin).
  const float halfFov = glm::radians(glm::max(camera.Zoom, 1.0f) * 0.5f);
  const float distance = (radius * 1.2f) / glm::max(glm::sin(halfFov), 0.01f);

  // Look-at basis, matching the engine convention used elsewhere:
  // columns are (Right, Up, -Front).
  const glm::vec3 right = glm::normalize(glm::cross(front, up));
  const glm::vec3 trueUp = glm::normalize(glm::cross(right, front));
  const glm::mat3 rot(right, trueUp, -front);

  Camera::CameraState target = camera.GetState();
  target.position = center - front * distance;
  target.orientation = glm::normalize(glm::quat_cast(rot));
  target.zoom = camera.Zoom; // standard views do not change the FOV

  camera.StartStateAnimation(target, 0.5f);
  // Re-anchor the orbit pivot to the scene centre. StartStateAnimation keeps
  // OrbitDistance and rebuilds OrbitPoint = Position + Front * OrbitDistance on
  // completion, so setting the distance here lands the pivot on `center`.
  camera.OrbitDistance = distance;
}

// ---- Snapshot glue (declared in Core/SnapshotManager.h) ----
namespace Core {

void RequestSnapshotCapture(const std::string &name, uint32_t flags) {
  g_pendingSnapshotName = name;
  g_pendingSnapshotFlags = flags;
  g_requestSnapshot = true;
}

void RestoreSnapshot(int index) {
  auto &mgr = Core::SnapshotManager::instance();
  if (index < 0 || index >= static_cast<int>(mgr.snapshots().size()))
    return;

  const Core::Snapshot &snap = mgr.snapshots()[index];
  std::string name = snap.name;
  mgr.restore(snap, camera, currentScene, pointLights, spotLights, sun,
              brushTool, measurementTool, clipPlaneTool);

  // Resync the systems that depend on scene structure. BVH / triangle data and
  // DDGI rebuild automatically via the per-frame scene-change check; the
  // voxelizer and SpaceMouse bounds are refreshed explicitly here (mirroring
  // the undo/redo resync).
  if (voxelizer)
    voxelizer->markDirty();
  updateSpaceMouseBounds();

  // Restoring lights can change their counts, so make sure the current
  // selection still points at a valid object (same guard as undo/redo).
  int objectCount = -1;
  switch (currentSelectedType) {
  case SelectedType::Model:
    objectCount = static_cast<int>(currentScene.models.size());
    break;
  case SelectedType::PointCloud:
    objectCount = static_cast<int>(currentScene.pointClouds.size());
    break;
  case SelectedType::PointLight:
    objectCount = static_cast<int>(pointLights.size());
    break;
  case SelectedType::SpotLight:
    objectCount = static_cast<int>(spotLights.size());
    break;
  default:
    break;
  }
  if (objectCount >= 0 &&
      (currentSelectedIndex < 0 || currentSelectedIndex >= objectCount)) {
    currentSelectedType = SelectedType::None;
    currentSelectedIndex = -1;
    currentSelectedMeshIndex = -1;
  }

  GUI::ShowToast("Restored snapshot: " + name, GUI::ToastType::Info);
}

} // namespace Core

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

  // When the mouse is actively orbiting, the authoritative orbit center is
  // camera.OrbitPoint (set in mouse_button_callback). In standard orbit mode
  // this differs from the raw cursor hit-point because it is projected along
  // the camera front direction. Always feed it directly to the SpaceMouse so
  // that if the user switches to the SpaceMouse mid-orbit the pivot is exact.
  // For CLICK mode the anchor is owned by the left-click handler; normal
  // mouse orbit and cursor-hover logic must not overwrite it.
  if (camera.IsOrbiting &&
      preferences.spaceMouseAnchorMode != GUI::SPACEMOUSE_ANCHOR_DISABLED &&
      preferences.spaceMouseAnchorMode != GUI::SPACEMOUSE_ANCHOR_CLICK) {
    if (glm::distance(lastCursorPosition, camera.OrbitPoint) > 0.001f ||
        settingChanged) {
      lastCursorPosition = camera.OrbitPoint;
      spaceMouseInput.SetCursorAnchor(camera.OrbitPoint,
                                      preferences.spaceMouseAnchorMode);
      spaceMouseInput.RefreshPivotPosition();
    }
    return;
  }

  if (cursorManager.isCursorPositionValid()) {
    glm::vec3 currentCursorPosition = cursorManager.getCursorPosition();

    // For CONTINUOUS mode, always update cursor position
    // For ON_START mode, only update when not navigating
    // For DISABLED mode, don't update cursor anchor
    // For CLICK mode, anchor is managed by left-click, not cursor hover
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
    case GUI::SPACEMOUSE_ANCHOR_CLICK:
      shouldUpdate = false;
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

      // Keep camera.OrbitPoint in sync with m_cursorAnchor so the idle
      // orbit center visualization always shows the real SpaceMouse pivot.
      // Without this, camera.OrbitPoint lags and the displayed sphere jumps
      // when SpaceMouse navigation begins (pivot switches to m_cursorAnchor).
      if (preferences.spaceMouseAnchorMode != GUI::SPACEMOUSE_ANCHOR_DISABLED) {
        camera.OrbitPoint = currentCursorPosition;
        camera.OrbitDistance =
            glm::length(camera.Position - currentCursorPosition);
        spaceMouseInput.RefreshPivotPosition();
      }
    }
  } else if (preferences.spaceMouseAnchorMode != GUI::SPACEMOUSE_ANCHOR_CLICK) {
    // Always update the setting state even if cursor is not valid.
    // Skip for CLICK mode so the manually set anchor is not erased.
    spaceMouseInput.SetCursorAnchor(glm::vec3(0.0f),
                                    preferences.spaceMouseAnchorMode);
  }

  // CLICK mode: anchor is driven exclusively by left-click, not cursor hover.
  if (preferences.spaceMouseAnchorMode == GUI::SPACEMOUSE_ANCHOR_CLICK) {
    if (spaceMouseClickAnchorSet) {
      if (glm::distance(lastCursorPosition, spaceMouseClickAnchor) > 0.001f ||
          settingChanged) {
        lastCursorPosition = spaceMouseClickAnchor;
        spaceMouseInput.SetCursorAnchor(spaceMouseClickAnchor,
                                        preferences.spaceMouseAnchorMode);
        camera.OrbitPoint = spaceMouseClickAnchor;
        camera.OrbitDistance =
            glm::length(camera.Position - spaceMouseClickAnchor);
        spaceMouseInput.RefreshPivotPosition();
      }
    } else if (settingChanged) {
      // No click yet — fall back to scene center until the user clicks.
      spaceMouseInput.SetCursorAnchor(glm::vec3(0.0f),
                                      GUI::SPACEMOUSE_ANCHOR_DISABLED);
      spaceMouseInput.RefreshPivotPosition();
    }
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
  // Convert mouse position to normalized device coordinates within the
  // free-area viewport (so picking matches what is rendered there).
  glm::vec2 ndc = WindowToViewportNDC(mouseX, mouseY);
  float x = ndc.x;
  float y = ndc.y;

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
  windowWidth = width;
  windowHeight = height;

  // When the window is minimized GLFW reports a 0x0 framebuffer. Recreating
  // renderer framebuffers at that size fails the GL_FRAMEBUFFER_COMPLETE check
  // and spams errors until the window is restored. Keep the existing
  // framebuffers and skip the viewport/GUI update; GLFW will invoke this
  // callback again with valid dimensions on restore.
  if (width <= 0 || height <= 0) {
    return;
  }

  glViewport(0, 0, width, height);

  // Update GUI scaling based on new window dimensions
  UpdateGuiScale(width, height);

  // Note: the offscreen render targets (bloom/HDR, SSAO, compute point cloud)
  // are sized to the free-area 3D viewport, not the full window. That resize is
  // driven from the render loop's per-frame viewport change-detection (which
  // also fires when the docked panels change width), so it is intentionally not
  // done here.
}

void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  if (!ImGui::GetIO().WantCaptureMouse && !spaceMouseActive) {
    // Offer the scroll to plugins first; a plugin that consumes it stops the
    // host's built-in scroll handling (clip-plane nudge, model depth, zoom).
    if (g_pluginManager.dispatchScroll(g_pluginContext, xoffset, yoffset))
      return;

    // Clip-plane scrubbing: while the tool is active with a selected plane,
    // scroll nudges the plane along its normal (mirrors model scroll-to-depth)
    // so the user can quickly slice through the model.
    if (clipPlaneTool.isEnabled() && clipPlaneTool.hasActivePlane()) {
      clipPlaneTool.nudgeActive(static_cast<float>(yoffset) *
                                clipPlaneTool.nudgeStep);
      return;
    }
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

// ---- Drag-Move Undo Tracking ----
// Captures the dragged object's position when a Ctrl/Alt+drag starts so the
// whole drag is recorded as a single undo entry on mouse release.
static bool dragUndoActive = false;
static SelectedType dragUndoType = SelectedType::None;
static int dragUndoIndex = -1;
static glm::vec3 dragUndoStartPosition(0.0f);

static void beginDragUndo() {
  dragUndoActive = false;
  dragUndoType = currentSelectedType;
  dragUndoIndex = currentSelectedIndex;

  if (dragUndoType == SelectedType::Model && dragUndoIndex >= 0 &&
      dragUndoIndex < static_cast<int>(currentScene.models.size())) {
    dragUndoStartPosition = currentScene.models[dragUndoIndex].position;
    dragUndoActive = true;
  } else if (dragUndoType == SelectedType::PointLight && dragUndoIndex >= 0 &&
             dragUndoIndex < static_cast<int>(pointLights.size())) {
    dragUndoStartPosition = pointLights[dragUndoIndex].position;
    dragUndoActive = true;
  } else if (dragUndoType == SelectedType::SpotLight && dragUndoIndex >= 0 &&
             dragUndoIndex < static_cast<int>(spotLights.size())) {
    dragUndoStartPosition = spotLights[dragUndoIndex].position;
    dragUndoActive = true;
  }
}

static void endDragUndo() {
  if (!dragUndoActive)
    return;
  dragUndoActive = false;

  if (dragUndoType == SelectedType::Model && dragUndoIndex >= 0 &&
      dragUndoIndex < static_cast<int>(currentScene.models.size())) {
    Undo::recordModelMoved(dragUndoIndex, dragUndoStartPosition,
                           currentScene.models[dragUndoIndex].position);
  } else if (dragUndoType == SelectedType::PointLight && dragUndoIndex >= 0 &&
             dragUndoIndex < static_cast<int>(pointLights.size())) {
    Undo::recordPointLightMoved(dragUndoIndex, dragUndoStartPosition,
                                pointLights[dragUndoIndex].position);
  } else if (dragUndoType == SelectedType::SpotLight && dragUndoIndex >= 0 &&
             dragUndoIndex < static_cast<int>(spotLights.size())) {
    Undo::recordSpotLightMoved(dragUndoIndex, dragUndoStartPosition,
                               spotLights[dragUndoIndex].position);
  }
}

// ── Transform gizmo glue ────────────────────────────────────────────────────
// Bind the gizmo to the currently selected object's transform every frame.
// Models and point clouds expose position/rotation/scale; lights expose only a
// position (which restricts the gizmo to translation). Pointers are refreshed
// each frame so container growth between frames can't leave them dangling.
static void bindGizmoTargetToSelection() {
  if (gizmoDragging)
    return; // keep the live target stable for the duration of a drag

  // The clip-plane tool takes over the gizmo when active with a plane selected,
  // so the user can slide (move) and rotate the section plane directly.
  if (clipPlaneTool.isEnabled() && clipPlaneTool.hasActivePlane()) {
    if (clipPlaneTool.bindGizmo(transformGizmo))
      return;
  }

  switch (currentSelectedType) {
  case SelectedType::Model:
    if (currentSelectedIndex >= 0 &&
        currentSelectedIndex < static_cast<int>(currentScene.models.size())) {
      Engine::Model &mdl = currentScene.models[currentSelectedIndex];
      transformGizmo.setTarget(&mdl.position, &mdl.rotation, &mdl.scale);
      return;
    }
    break;
  case SelectedType::PointCloud:
    if (currentSelectedIndex >= 0 &&
        currentSelectedIndex <
            static_cast<int>(currentScene.pointClouds.size())) {
      Engine::PointCloud &pc = currentScene.pointClouds[currentSelectedIndex];
      transformGizmo.setTarget(&pc.position, &pc.rotation, &pc.scale);
      return;
    }
    break;
  case SelectedType::PointLight:
    if (currentSelectedIndex >= 0 &&
        currentSelectedIndex < static_cast<int>(pointLights.size())) {
      transformGizmo.setTarget(&pointLights[currentSelectedIndex].position,
                               nullptr, nullptr);
      return;
    }
    break;
  case SelectedType::SpotLight:
    if (currentSelectedIndex >= 0 &&
        currentSelectedIndex < static_cast<int>(spotLights.size())) {
      transformGizmo.setTarget(&spotLights[currentSelectedIndex].position,
                               nullptr, nullptr);
      return;
    }
    break;
  default:
    break;
  }
  transformGizmo.clearTarget();
}

// Capture the before-state so the whole gizmo drag becomes one undo entry.
static void beginGizmoDrag(Tools::TransformGizmo::Handle handle,
                           const glm::vec3 &rayOrigin,
                           const glm::vec3 &rayDir) {
  // Clip-plane gizmo drag: the plane (not a scene object) is the target.
  if (clipPlaneTool.isEnabled() && clipPlaneTool.hasActivePlane()) {
    gizmoUndoType = SelectedType::None;
    clipPlaneTool.captureGizmoUndo();
    gizmoDragging =
        transformGizmo.beginDrag(handle, rayOrigin, rayDir, camera.Position);
    g_clipPlaneGizmoActive = gizmoDragging;
    return;
  }

  gizmoUndoType = currentSelectedType;
  gizmoUndoIndex = currentSelectedIndex;
  switch (gizmoUndoType) {
  case SelectedType::Model:
    gizmoUndoModelBefore = Engine::Undo::ModelEditState::capture(
        currentScene.models[gizmoUndoIndex]);
    break;
  case SelectedType::PointCloud:
    gizmoUndoPointCloudBefore = Engine::Undo::PointCloudEditState::capture(
        currentScene.pointClouds[gizmoUndoIndex]);
    break;
  case SelectedType::PointLight:
    gizmoUndoPointLightBefore = pointLights[gizmoUndoIndex];
    break;
  case SelectedType::SpotLight:
    gizmoUndoSpotLightBefore = spotLights[gizmoUndoIndex];
    break;
  default:
    break;
  }
  gizmoDragging =
      transformGizmo.beginDrag(handle, rayOrigin, rayDir, camera.Position);
  if (!gizmoDragging)
    gizmoUndoType = SelectedType::None;
}

// Commit the drag: record undo, refresh dependent systems.
static void finishGizmoDrag() {
  if (!gizmoDragging)
    return;
  transformGizmo.endDrag();
  gizmoDragging = false;

  // Clip-plane drag: record the position/normal change as one undo entry.
  if (g_clipPlaneGizmoActive) {
    g_clipPlaneGizmoActive = false;
    clipPlaneTool.recordGizmoUndo();
    return;
  }

  switch (gizmoUndoType) {
  case SelectedType::Model:
    if (gizmoUndoIndex >= 0 &&
        gizmoUndoIndex < static_cast<int>(currentScene.models.size())) {
      Undo::recordModelEdit(
          gizmoUndoIndex, gizmoUndoModelBefore,
          Engine::Undo::ModelEditState::capture(
              currentScene.models[gizmoUndoIndex]));
      if (voxelizer)
        voxelizer->markDirty();
    }
    break;
  case SelectedType::PointCloud:
    if (gizmoUndoIndex >= 0 &&
        gizmoUndoIndex < static_cast<int>(currentScene.pointClouds.size())) {
      Undo::recordPointCloudEdit(
          gizmoUndoIndex, gizmoUndoPointCloudBefore,
          Engine::Undo::PointCloudEditState::capture(
              currentScene.pointClouds[gizmoUndoIndex]));
    }
    break;
  case SelectedType::PointLight:
    if (gizmoUndoIndex >= 0 &&
        gizmoUndoIndex < static_cast<int>(pointLights.size())) {
      Undo::recordPointLightEdit(gizmoUndoIndex, gizmoUndoPointLightBefore,
                                 pointLights[gizmoUndoIndex]);
    }
    break;
  case SelectedType::SpotLight:
    if (gizmoUndoIndex >= 0 &&
        gizmoUndoIndex < static_cast<int>(spotLights.size())) {
      Undo::recordSpotLightEdit(gizmoUndoIndex, gizmoUndoSpotLightBefore,
                                spotLights[gizmoUndoIndex]);
    }
    break;
  default:
    break;
  }
  gizmoUndoType = SelectedType::None;
  gizmoUndoIndex = -1;
  updateSpaceMouseBounds();
}

// ── Clip-plane creation helpers (invoked from the GUI panel) ────────────────
// Place a clip plane at the 3D cursor, oriented by the surface normal under the
// cursor (camera-facing fallback). No-op when the cursor isn't on geometry.
void addClipPlaneAtCursor() {
  if (!cursorManager.isCursorPositionValid())
    return;
  const glm::vec3 pos = cursorManager.getCursorPosition();

  // Default to a camera-facing normal, then refine with the surface normal of
  // the NEAREST model under the cursor (so overlapping models pick the visible
  // surface, matching the cursor's own hit point).
  glm::vec3 normal = glm::normalize(camera.Position - pos);
  glm::vec3 rayOrigin, rayDir, rayNear, rayFar;
  calculateMouseRay(lastX, lastY, rayOrigin, rayDir, rayNear, rayFar,
                    aspectRatio);
  float closest = std::numeric_limits<float>::max();
  for (const auto &model : currentScene.models) {
    float distance;
    glm::vec3 hitNormal;
    if (rayIntersectsModel(rayOrigin, rayDir, model, distance, hitNormal) &&
        distance < closest) {
      closest = distance;
      normal = hitNormal;
    }
  }
  clipPlaneTool.addPlane(pos, normal);
  clipPlaneTool.setEnabled(true);
}

// Add an axis-aligned plane (0 = X, 1 = Y, 2 = Z) through the selection centre,
// else the 3D cursor, else the world origin.
void addClipPlaneAxisAligned(int axis) {
  glm::vec3 center(0.0f);
  if (currentSelectedType == SelectedType::Model && currentSelectedIndex >= 0 &&
      currentSelectedIndex < static_cast<int>(currentScene.models.size())) {
    center = currentScene.models[currentSelectedIndex].position;
  } else if (currentSelectedType == SelectedType::PointCloud &&
             currentSelectedIndex >= 0 &&
             currentSelectedIndex <
                 static_cast<int>(currentScene.pointClouds.size())) {
    center = currentScene.pointClouds[currentSelectedIndex].position;
  } else if (cursorManager.isCursorPositionValid()) {
    center = cursorManager.getCursorPosition();
  }
  clipPlaneTool.addAxisAlignedPlane(axis, center);
  clipPlaneTool.setEnabled(true);
}

void mouse_button_callback(GLFWwindow *window, int button, int action,
                           int mods) {
  if (ImGui::GetIO().WantCaptureMouse) {
    glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    return;
  }

  // Offer the event to plugins first; a plugin that consumes it (e.g. the
  // MeasurementTool placing a point, or the Crosshair example dropping a
  // marker) stops the host's built-in selection / navigation handling.
  if (g_pluginManager.dispatchMouseButton(g_pluginContext, button, action, mods))
    return;

  if (button == GLFW_MOUSE_BUTTON_LEFT) {
    if (action == GLFW_PRESS) {
      // Check if Ctrl or Alt is held down
      bool ctrlPressed = (mods & GLFW_MOD_CONTROL);
      bool altPressed = (mods & GLFW_MOD_ALT);

      // Transform gizmo: only active while Ctrl is held (the gizmo is hidden
      // otherwise). If a handle is under the cursor, start a constrained drag
      // and consume the click; a Ctrl-click on empty space or the object body
      // still falls through to selection / body-drag free-move below.
      // Re-bind first so the target pointer reflects the current selection even
      // if a scene container was reallocated since the last frame.
      bindGizmoTargetToSelection();
      if (ctrlPressed && transformGizmo.enabled && transformGizmo.hasTarget()) {
        glm::vec3 gRayOrigin, gRayDir, gRayNear, gRayFar;
        calculateMouseRay(lastX, lastY, gRayOrigin, gRayDir, gRayNear, gRayFar,
                          aspectRatio);
        Tools::TransformGizmo::Handle hit =
            transformGizmo.hitTest(gRayOrigin, gRayDir, camera.Position);
        if (hit != Tools::TransformGizmo::Handle::None) {
          beginGizmoDrag(hit, gRayOrigin, gRayDir);
          if (gizmoDragging)
            return;
        }
      }

      // (Measurement-tool point placement is now handled by MeasurementPlugin
      // via the plugin mouse-button dispatch at the top of this callback.)

      // Handle brush tool painting (when enabled and no modifiers pressed)
      if (!ctrlPressed && !altPressed &&
          preferences.brushToolSettings.enabled &&
          preferences.brushToolSettings.selectedModelIndex >= 0) {

        glm::vec3 rayOrigin, rayDirection, rayNear, rayFar;
        calculateMouseRay(lastX, lastY, rayOrigin, rayDirection, rayNear,
                          rayFar, aspectRatio);

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
                          rayFar, aspectRatio);

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
            Undo::recordModelAdded(
                static_cast<int>(currentScene.models.size()) - 1);

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
            beginDragUndo();

            // Calculate and store the grab point on the model
            // This is the world-space position where the ray intersects the
            // model
            glm::vec3 grabRayOrigin, grabRayDirection, grabRayNear, grabRayFar;
            calculateMouseRay(lastX, lastY, grabRayOrigin, grabRayDirection,
                              grabRayNear, grabRayFar, aspectRatio);
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
            beginDragUndo();
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
            beginDragUndo();
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

      // CLICK mode: fix the SpaceMouse pivot at the clicked 3D point.
      if (!selectionMode && !preferences.brushToolSettings.enabled &&
          preferences.spaceMouseAnchorMode == GUI::SPACEMOUSE_ANCHOR_CLICK) {
        glm::vec3 anchor;
        if (cursorManager.isCursorPositionValid()) {
          anchor = cursorManager.getCursorPosition();
        } else {
          anchor = camera.Position + camera.Front * camera.OrbitDistance;
        }
        spaceMouseClickAnchor = anchor;
        spaceMouseClickAnchorSet = true;
        spaceMouseInput.SetCursorAnchor(anchor, GUI::SPACEMOUSE_ANCHOR_CLICK);
        camera.OrbitPoint = anchor;
        camera.OrbitDistance = glm::length(camera.Position - anchor);
        spaceMouseInput.RefreshPivotPosition();
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
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                           preferences.farPlane),
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
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                           preferences.farPlane),
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
              camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                         preferences.farPlane),
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
      // Finalize a gizmo drag (records one undo entry) and consume the release.
      if (gizmoDragging) {
        finishGizmoDrag();
        return;
      }

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
            // Note: Pass false for stereo mode since stereo matrices aren't
            // available here. Using mono projection for cursor positioning is
            // sufficient and avoids incorrect averaging with invalid default
            // stereo matrices.
            Core::CursorSynchronizer::synchronizeCursorPosition(
                window,
                Core::CursorSyncManager::getInstance().getWorldPosition(),
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                           preferences.farPlane),
                camera.GetViewMatrix(), windowWidth, windowHeight, false,
                glm::mat4(1.0f), glm::mat4(1.0f),
                glm::ivec4(g_viewportX, g_viewportTopInset, g_viewportWidth,
                           g_viewportHeight));
            Core::CursorSyncManager::getInstance().markSynchronized();
          } else if (orbitFollowsCursor) {
            // Center cursor mode - cursor should be at viewport center after
            // centering animation
            glfwSetCursorPos(window, g_viewportX + g_viewportWidth / 2.0f,
                             g_viewportTopInset + g_viewportHeight / 2.0f);
            Core::CursorSyncManager::getInstance().markSynchronized();
          } else {
            // This shouldn't happen, but fallback to synchronization
            // Note: Pass false for stereo mode since stereo matrices aren't
            // available here.
            Core::CursorSynchronizer::synchronizeCursorPosition(
                window,
                Core::CursorSyncManager::getInstance().getWorldPosition(),
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                           preferences.farPlane),
                camera.GetViewMatrix(), windowWidth, windowHeight, false,
                glm::mat4(1.0f), glm::mat4(1.0f),
                glm::ivec4(g_viewportX, g_viewportTopInset, g_viewportWidth,
                           g_viewportHeight));
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
            // Note: Pass false for stereo mode since stereo matrices aren't
            // available here.
            Core::CursorSynchronizer::synchronizeCursorPosition(
                window, originalCursorPos,
                camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                           preferences.farPlane),
                camera.GetViewMatrix(), windowWidth, windowHeight, false,
                glm::mat4(1.0f), glm::mat4(1.0f),
                glm::ivec4(g_viewportX, g_viewportTopInset, g_viewportWidth,
                           g_viewportHeight));
          } else {
            // No valid cursor position - fallback to viewport center
            glfwSetCursorPos(window, g_viewportX + g_viewportWidth / 2.0f,
                             g_viewportTopInset + g_viewportHeight / 2.0f);
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

            // Convert NDC to screen coordinates (within the free-area viewport)
            glm::vec2 grabPx =
                ViewportNDCToWindow(grabScreenPos.x, grabScreenPos.y);
            float screenX = grabPx.x;
            float screenY = grabPx.y;

            // Set cursor position to the final grab point location
            glfwSetCursorPos(window, screenX, screenY);

            // Update lastX/lastY
            lastX = screenX;
            lastY = screenY;
          }
        }
      }

      // Record the completed drag as one undo entry
      if (wasMovingModel) {
        endDragUndo();
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
          camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                     preferences.farPlane),
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
      // Note: Pass false for stereo mode since stereo matrices aren't available
      // here.
      if (Core::CursorSyncManager::getInstance().needsSynchronization()) {
        Core::CursorSynchronizer::synchronizeCursorPosition(
            window, Core::CursorSyncManager::getInstance().getWorldPosition(),
            camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                       preferences.farPlane),
            camera.GetViewMatrix(), windowWidth, windowHeight, false);
        Core::CursorSyncManager::getInstance().markSynchronized();
      }

      // Disable mouse capture
      isMouseCaptured = false;
      firstMouse = true; // Reset first mouse flag for next time
    }
  } else if (button == GLFW_MOUSE_BUTTON_RIGHT) {
    if (action == GLFW_PRESS) {
      // (Measurement-tool right-click finish is now handled by MeasurementPlugin
      // via the plugin mouse-button dispatch at the top of this callback.)

      rightMousePressed = true;

      // Capture cursor state for synchronization before starting rotation
      glm::vec3 cursorPos =
          cursorManager.isCursorPositionValid()
              ? cursorManager.getCursorPosition()
              : camera.Position + camera.Front * camera.OrbitDistance;

      Core::CursorSyncManager::getInstance().captureState(
          cursorPos, Core::CameraOperationType::Rotating,
          camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                     preferences.farPlane),
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
      // Note: Pass false for stereo mode since stereo matrices aren't available
      // here.
      if (Core::CursorSyncManager::getInstance().needsSynchronization()) {
        Core::CursorSynchronizer::synchronizeCursorPosition(
            window, Core::CursorSyncManager::getInstance().getWorldPosition(),
            camera.GetProjectionMatrix(aspectRatio, preferences.nearPlane,
                                       preferences.farPlane),
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

// Reports a shortcut-driven toggle through both the console and a GUI toast,
// so keyboard changes are visible without watching the console.
static void reportToggle(const char *name, bool enabled) {
  std::cout << name << " " << (enabled ? "enabled" : "disabled") << std::endl;
  GUI::ShowToast(std::string(name) + (enabled ? " enabled" : " disabled"),
                 GUI::ToastType::Info);
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

  // Offer the (PRESS) key event to plugins. MeasurementPlugin consumes its
  // editing keys (Enter finishes, Delete cancels, Backspace removes the last
  // point) while a measurement is in progress.
  // Note: Escape can't be used to cancel — Input::handleKeyInput closes the
  // application on Escape.
  if (g_pluginManager.dispatchKey(g_pluginContext, key, scancode, action, mods))
    return;

  // Check if this key press matches any shortcut action. Translate the physical
  // key to its layout label first so letter/number shortcuts trigger on the key
  // the user actually sees (e.g. Ctrl+Z = Undo on the key labeled "Z" on QWERTY,
  // QWERTZ, AZERTY, ...) rather than the physical US-layout position.
  int labelKey =
      StereoVista::ShortcutManager::normalizeKeyToLayout(key, scancode);
  auto actionOpt = shortcutManager.getActionForKey(labelKey, mods);

  if (actionOpt.has_value()) {
    StereoVista::ShortcutAction shortcutAction = actionOpt.value();

    // Dispatch to appropriate action handler
    switch (shortcutAction) {
    case StereoVista::ShortcutAction::ToggleGUI:
      showGui = !showGui;
      std::cout << "GUI visibility toggled. showGui = "
                << (showGui ? "true" : "false") << std::endl;
      break;

    case StereoVista::ShortcutAction::TakeScreenshot:
      // Auto-save a timestamped screenshot to the "screenshots" folder.
      g_screenshotPath.clear();
      g_requestScreenshot = true;
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

      GUI::ShowToast(
          std::string("Lighting mode: ") +
              (currentLightingMode == GUI::LIGHTING_SHADOW_MAPPING
                   ? "Shadow Mapping"
                   : currentLightingMode == GUI::LIGHTING_VOXEL_CONE_TRACING
                         ? "Voxel Cone Tracing"
                         : "Radiance"),
          GUI::ToastType::Info);
      break;

    case StereoVista::ShortcutAction::ToggleShadows:
      enableShadows = !enableShadows;
      reportToggle("Shadows", enableShadows);
      preferences.enableShadows = enableShadows;
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleVoxelViz:
      voxelizer->showDebugVisualization = !voxelizer->showDebugVisualization;
      reportToggle("Voxel visualization", voxelizer->showDebugVisualization);
      break;

    case StereoVista::ShortcutAction::CenterView: {
      // Try to find a good center point
      glm::vec3 centerPoint(0.0f);
      int objectCount = 0;

      // First, try to use the current cursor position if valid
      if (cursorManager.isCursorPositionValid()) {
        glfwSetCursorPos(window, g_viewportX + g_viewportWidth / 2.0,
                         g_viewportTopInset + g_viewportHeight / 2.0);
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
        glfwSetCursorPos(window, g_viewportX + g_viewportWidth / 2.0,
                         g_viewportTopInset + g_viewportHeight / 2.0);
        std::cout << "Centering on scene midpoint" << std::endl;
      } else {
        // If no objects, center on the world origin
        camera.StartCenteringAnimation(glm::vec3(0.0f));
        glfwSetCursorPos(window, g_viewportX + g_viewportWidth / 2.0,
                         g_viewportTopInset + g_viewportHeight / 2.0);
        std::cout << "Centering on world origin" << std::endl;
      }
    } break;

    // Standard Views (frame the whole scene from a fixed angle)
    case StereoVista::ShortcutAction::ViewFront:
      applyStandardView(0);
      break;
    case StereoVista::ShortcutAction::ViewBack:
      applyStandardView(1);
      break;
    case StereoVista::ShortcutAction::ViewRight:
      applyStandardView(2);
      break;
    case StereoVista::ShortcutAction::ViewLeft:
      applyStandardView(3);
      break;
    case StereoVista::ShortcutAction::ViewTop:
      applyStandardView(4);
      break;
    case StereoVista::ShortcutAction::ViewBottom:
      applyStandardView(5);
      break;
    case StereoVista::ShortcutAction::ViewIso:
      applyStandardView(6);
      break;

    // View/Display Controls
    case StereoVista::ShortcutAction::ToggleFPS:
      showFPS = !showFPS;
      reportToggle("Performance overlay", showFPS);
      break;

    case StereoVista::ShortcutAction::ToggleWireframe:
      camera.wireframe = !camera.wireframe;
      reportToggle("Wireframe mode", camera.wireframe);
      break;

    case StereoVista::ShortcutAction::ToggleUnlit:
      g_unlitMode = !g_unlitMode;
      reportToggle("Unlit shading", g_unlitMode);
      break;

    case StereoVista::ShortcutAction::ToggleRadar:
      preferences.radarEnabled = !preferences.radarEnabled;
      reportToggle("Radar", preferences.radarEnabled);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleZeroPlane:
      preferences.showZeroPlane = !preferences.showZeroPlane;
      reportToggle("Zero plane", preferences.showZeroPlane);
      savePreferences();
      break;

    // Camera Controls
    case StereoVista::ShortcutAction::ToggleZoomToCursor:
      camera.zoomToCursor = !camera.zoomToCursor;
      reportToggle("Zoom to cursor", camera.zoomToCursor);
      break;

    case StereoVista::ShortcutAction::ToggleOrbitAroundCursor:
      camera.orbitAroundCursor = !camera.orbitAroundCursor;
      reportToggle("Orbit around cursor", camera.orbitAroundCursor);
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
      reportToggle("HDR", preferences.hdrSettings.enabled);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleBloom:
      preferences.hdrSettings.enableBloom =
          !preferences.hdrSettings.enableBloom;
      reportToggle("Bloom", preferences.hdrSettings.enableBloom);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::TogglePCSS:
      preferences.shadowSettings.enablePCSS =
          !preferences.shadowSettings.enablePCSS;
      reportToggle("PCSS (soft shadows)", preferences.shadowSettings.enablePCSS);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleSunLight:
      sun.enabled = !sun.enabled;
      reportToggle("Sun light", sun.enabled);
      break;

    // Materials & Rendering
    case StereoVista::ShortcutAction::TogglePBR:
      preferences.materialSettings.enablePBR =
          !preferences.materialSettings.enablePBR;
      reportToggle("PBR materials", preferences.materialSettings.enablePBR);
      savePreferences();
      break;

    // VCT
    case StereoVista::ShortcutAction::ToggleVCTIndirectDiffuse:
      preferences.vctSettings.indirectDiffuseLight =
          !preferences.vctSettings.indirectDiffuseLight;
      reportToggle("VCT indirect diffuse",
                   preferences.vctSettings.indirectDiffuseLight);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleVCTIndirectSpecular:
      preferences.vctSettings.indirectSpecularLight =
          !preferences.vctSettings.indirectSpecularLight;
      reportToggle("VCT indirect specular",
                   preferences.vctSettings.indirectSpecularLight);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleVCTDirectLight:
      preferences.vctSettings.directLight =
          !preferences.vctSettings.directLight;
      reportToggle("VCT direct lighting", preferences.vctSettings.directLight);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleVCTSoftShadows:
      preferences.vctSettings.shadows = !preferences.vctSettings.shadows;
      reportToggle("VCT soft shadows", preferences.vctSettings.shadows);
      savePreferences();
      break;

    // Raytracing
    case StereoVista::ShortcutAction::ToggleRaytracing:
      preferences.radianceSettings.enableRaytracing =
          !preferences.radianceSettings.enableRaytracing;
      radianceSettings.enableRaytracing =
          preferences.radianceSettings.enableRaytracing;
      reportToggle("Raytracing", preferences.radianceSettings.enableRaytracing);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleIndirectLighting:
      preferences.radianceSettings.enableIndirectLighting =
          !preferences.radianceSettings.enableIndirectLighting;
      radianceSettings.enableIndirectLighting =
          preferences.radianceSettings.enableIndirectLighting;
      reportToggle("Indirect lighting",
                   preferences.radianceSettings.enableIndirectLighting);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleEmissiveLighting:
      preferences.radianceSettings.enableEmissiveLighting =
          !preferences.radianceSettings.enableEmissiveLighting;
      radianceSettings.enableEmissiveLighting =
          preferences.radianceSettings.enableEmissiveLighting;
      reportToggle("Emissive lighting",
                   preferences.radianceSettings.enableEmissiveLighting);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ToggleBVH:
      preferences.radianceSettings.enableBVH =
          !preferences.radianceSettings.enableBVH;
      radianceSettings.enableBVH = preferences.radianceSettings.enableBVH;
      enableBVH = preferences.radianceSettings.enableBVH;
      reportToggle("BVH acceleration", preferences.radianceSettings.enableBVH);
      savePreferences();
      break;

    case StereoVista::ShortcutAction::ClearIrradianceCache:
      if (ddgiVolume && ddgiVolume->isInitialized()) {
        ddgiVolume->clear();
        std::cout << "DDGI probes reset - will reconverge over the next frames"
                  << std::endl;
        GUI::ShowToast("DDGI probes reset", GUI::ToastType::Info);
      } else {
        std::cout << "DDGI volume not initialized" << std::endl;
        GUI::ShowToast("DDGI volume not initialized", GUI::ToastType::Warning);
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

    case StereoVista::ShortcutAction::OpenMeasurementTool:
      showMeasurementToolWindow = true;
      std::cout << "Opening measurement tool window" << std::endl;
      break;

    case StereoVista::ShortcutAction::OpenClipPlaneTool:
      showClipPlaneToolWindow = true;
      clipPlaneTool.setEnabled(true);
      std::cout << "Opening clip plane tool window" << std::endl;
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

        // Remove the selected model from the scene (undoable)
        Undo::deleteModel(currentSelectedIndex);

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

    // Edit History
    case StereoVista::ShortcutAction::Undo:
      // Let ImGui text fields keep their own Ctrl+Z handling
      if (!ImGui::GetIO().WantTextInput) {
        if (!UndoManager::instance().undo()) {
          std::cout << "Nothing to undo" << std::endl;
        }
      }
      break;

    case StereoVista::ShortcutAction::Redo:
      if (!ImGui::GetIO().WantTextInput) {
        if (!UndoManager::instance().redo()) {
          std::cout << "Nothing to redo" << std::endl;
        }
      }
      break;

    // Transform gizmo mode/space. Switching mode is a persistent setting even
    // with nothing selected; it takes visible effect once an object is picked.
    // Guarded so digits typed into numeric fields don't switch modes.
    case StereoVista::ShortcutAction::GizmoModeMove:
      if (!ImGui::GetIO().WantTextInput) {
        transformGizmo.setMode(Tools::TransformGizmo::Mode::Translate);
        GUI::ShowToast("Gizmo: Move", GUI::ToastType::Info);
      }
      break;
    case StereoVista::ShortcutAction::GizmoModeRotate:
      if (!ImGui::GetIO().WantTextInput) {
        transformGizmo.setMode(Tools::TransformGizmo::Mode::Rotate);
        GUI::ShowToast("Gizmo: Rotate", GUI::ToastType::Info);
      }
      break;
    case StereoVista::ShortcutAction::GizmoModeScale:
      if (!ImGui::GetIO().WantTextInput) {
        transformGizmo.setMode(Tools::TransformGizmo::Mode::Scale);
        GUI::ShowToast("Gizmo: Scale", GUI::ToastType::Info);
      }
      break;
    case StereoVista::ShortcutAction::GizmoToggleSpace:
      if (!ImGui::GetIO().WantTextInput) {
        transformGizmo.toggleSpace();
        GUI::ShowToast(transformGizmo.space ==
                               Tools::TransformGizmo::Space::World
                           ? "Gizmo: World space"
                           : "Gizmo: Local space",
                       GUI::ToastType::Info);
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
    glm::mat4 projection = camera.GetProjectionMatrix(
        aspectRatio, preferences.nearPlane, preferences.farPlane);
    glm::mat4 view = camera.GetViewMatrix();

    Core::CursorSynchronizer::printDiagnostics(cursorPos, projection, view,
                                               windowWidth, windowHeight);
  } else {
    std::cout << "No valid cursor position for diagnostics" << std::endl;
  }
}

#pragma endregion