#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Engine {
class Shader;
class CursorPreset;
} // namespace Engine

namespace GUI {
enum SkyboxType {
  SKYBOX_CUBEMAP,
  SKYBOX_HDR,
  SKYBOX_SOLID_COLOR,
  SKYBOX_GRADIENT
};

enum LightingMode {
  LIGHTING_SHADOW_MAPPING,
  LIGHTING_VOXEL_CONE_TRACING,
  LIGHTING_RADIANCE
};

struct VCTSettings {
  // Lighting Components
  bool indirectSpecularLight = true;
  bool indirectDiffuseLight = true;
  bool directLight = true;
  bool shadows = true;

  // Quality Settings
  float voxelSize = 1.0f / 64.0f;
  int diffuseConeCount = 6; // Number of cones for indirect diffuse (1, 5, or 6)
  float tracingMaxDistance = 1.41421356237; // Maximum distance for cone tracing
                                            // in grid units (default: SQRT2)
  int shadowSampleCount = 18;         // Number of samples for shadow cones
  float shadowStepMultiplier = 0.15f; // Step size multiplier for shadows
};

enum CursorScalingMode {
  CURSOR_NORMAL,
  CURSOR_FIXED,
  CURSOR_CONSTRAINED_DYNAMIC,
  CURSOR_LOGARITHMIC
};

enum SpaceMouseAnchorMode {
  SPACEMOUSE_ANCHOR_DISABLED,  // Use scene center (default)
  SPACEMOUSE_ANCHOR_ON_START,  // Set anchor when navigation starts, keep it
                               // fixed
  SPACEMOUSE_ANCHOR_CONTINUOUS // Update anchor every frame during navigation
};

enum SpaceMouseNavigationMode {
  SPACEMOUSE_NAV_CAD // Pivot-based navigation (current behavior)
};

enum SceneLoadingBehavior {
  SCENE_LOAD_ALWAYS_ASK,    // Show dialog to ask user (default)
  SCENE_LOAD_ALWAYS_REPLACE, // Always clear existing scene
  SCENE_LOAD_ALWAYS_MERGE    // Always keep existing scene
};

// Structure definitions
struct SkyboxConfig {
  SkyboxType type = SKYBOX_HDR;
  glm::vec3 solidColor = glm::vec3(0.2f, 0.3f, 0.4f);
  glm::vec3 gradientTopColor = glm::vec3(0.1f, 0.1f, 0.3f);
  glm::vec3 gradientBottomColor = glm::vec3(0.7f, 0.7f, 1.0f);
  int selectedCubemap = 0; // Index of the selected predefined cubemap
  std::string hdrPath =
      "skybox/HDR/table_mountain_2_puresky_4k.hdr"; // Path to HDR file
};

struct CubemapPreset {
  std::string name;
  std::string path;
  std::string description;
};

struct FragmentShaderCursorSettings {
  float baseOuterRadius = 0.04f;
  float baseOuterBorderThickness = 0.005f;
  float baseInnerRadius = 0.004f;
  float baseInnerBorderThickness = 0.005f;
  glm::vec4 outerColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
  glm::vec4 innerColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
};

struct PlaneCursor {
  GLuint VAO, VBO, EBO;
  glm::vec4 color = glm::vec4(0.0f, 1.0f, 0.0f, 0.7f);
  float diameter = 0.5f;
  bool show = false;
  Engine::Shader *shader = nullptr;
};

struct ApplicationPreferences {
  bool isDarkTheme = true;
  float guiScaleFactor = 1.0f;  // User-controlled GUI scale multiplier (0.5x - 2.0x)
  float separation = 0.5f;
  float convergence = 2.6f;
  float nearPlane = 0.1f;
  float farPlane = 200.0f;
  std::string currentPresetName = "Sphere";
  float cameraSpeedFactor = 1.0f;
  bool showFPS = true;
  bool show3DCursor = true;
  bool useNewStereoMethod = true;
  float fov = 45.0f;

  // Automatic convergence settings
  bool autoConvergence = false;
  float convergenceDistanceFactor = 1.0f;
  float convergenceSmoothingSpeed = 5.0f;
  bool enableConvergenceCap = false;
  float convergenceCapMin = 0.5f;
  float convergenceCapMax = 40.0f;

  // Stereoscopic display settings
  bool flipEyes = false;

  // Lighting settings
  LightingMode lightingMode = LIGHTING_SHADOW_MAPPING;
  bool enableShadows = true;
  VCTSettings vctSettings;

  // HDR rendering settings
  struct HDRSettings {
    bool enabled = true;
    float exposure = 1.0f;
    float bloomThreshold = 1.0f;
    float bloomIntensity = 0.04f;
    int toneMapOperator = 0; // 0=Reinhard, 1=ACES, 2=Filmic
    bool enableBloom = false;
  } hdrSettings;

  // SSAO settings
  struct SSAOSettings {
    bool enabled = false;
    int kernelSize = 64;  // Number of samples (16-64)
    float radius = 0.5f;  // Sample radius in view space
    float bias = 0.025f;  // Depth comparison bias
    float power = 1.0f;   // Occlusion intensity curve
  } ssaoSettings;

  // Schütz Phase 1 – Eye-Dome Lighting
  struct EDLSettings {
    bool  enabled  = false;
    float strength = 1.0f;  // edge-shading intensity (0 = off, 5 = very strong)
    float radius   = 1.5f;  // neighbourhood-sampling radius in pixels
  } edlSettings;

  // Schütz Phase 1 – point cloud base size (world-space radius in metres).
  // The vertex shader projects this to screen-space pixels via perspective.
  float pointCloudBaseSize = 0.02f;

  // Shadow quality settings
  struct ShadowSettings {
    int pcfKernelSize = 3; // 3, 5, 7, or 9
    bool enablePCSS = false;
    float lightSize = 0.1f;      // For PCSS calculations
    float shadowSoftness = 1.0f; // Softness multiplier for shadow filtering
    bool enableCascades = false;
    int numCascades = 4;
    float cascadeSplitLambda = 0.5f;
    bool enableIndirectLighting =
        false; // Enable voxel-based indirect lighting in shadow mapping mode
  } shadowSettings;

  // Enhanced material settings
  struct MaterialSettings {
    bool enablePBR = true;
    bool enableAO = true;
    bool enableNormalMapping = true;
    bool enableParallaxMapping = false;
    float normalScale = 1.0f;
    float heightScale = 0.02f;
    float metallicFactor = 0.0f;
    float roughnessFactor = 0.5f;
  } materialSettings;

  // Radiance raytracing settings
  struct RadianceSettings {
    bool enableRaytracing = true;
    int maxBounces = 2;
    int samplesPerPixel = 1;
    float rayMaxDistance = 50.0f;
    bool enableIndirectLighting = true;
    bool enableEmissiveLighting = true;
    float indirectIntensity = 0.3f;
    float skyIntensity = 1.0f;
    float emissiveIntensity = 1.0f;
    float materialRoughness = 0.5f;
    bool enableBVH = true;
    bool showBVHDebug = false;
    int bvhDebugMaxDepth = 3;
    int bvhDebugRenderMode =
        1; // 0=DEPTH_TESTED, 1=ALWAYS_ON_TOP, 2=DEPTH_BIASED

    // Irradiance caching settings
    bool enableIrradianceCache = false;
    int irradianceCacheDivisor = 4; // Cache resolution divisor (2, 4, 8)
    int irradianceCacheSamplesPerPixel =
        4; // Samples per cache pixel (higher quality)
    float irradianceCacheMaxDistance =
        2.0f; // Max world-space interpolation distance
    float irradianceCacheNormalThreshold = 0.5f; // Min dot(N,N) for validity
  } radianceSettings;

  // Scroll and movement settings
  float scrollMomentum = 0.5f;
  float maxScrollVelocity = 3.0f;
  float scrollDeceleration = 10.0f;
  bool useSmoothScrolling = true;
  bool zoomToCursor = true;
  bool orbitAroundCursor = true;
  bool orbitFollowsCursor = false;
  float mouseSmoothingFactor = 1.0f;
  float mouseSensitivity = 0.17f;

  // Orbit center visualization settings
  bool showOrbitCenter = false;
  bool alwaysShowOrbitCenter = false;
  glm::vec4 orbitCenterColor = glm::vec4(0.0f, 1.0f, 0.0f, 0.7f);
  float orbitCenterSphereRadius = 0.2f;

  bool showStereoVisualization = true;

  bool radarEnabled = false;
  glm::vec2 radarPos = glm::vec2(0.8f, -0.8f);
  float radarScale = 0.03f;
  bool radarShowScene = true;

  bool showZeroPlane = false;

  bool enableSpawnAnimation = true;

  // SpaceMouse settings
  bool spaceMouseEnabled = true;
  float spaceMouseDeadzone = 0.025f;
  float spaceMouseTranslationSensitivity = 1.0f;
  float spaceMouseRotationSensitivity = 1.0f;
  SpaceMouseNavigationMode spaceMouseNavigationMode = SPACEMOUSE_NAV_CAD;
  SpaceMouseAnchorMode spaceMouseAnchorMode = SPACEMOUSE_ANCHOR_DISABLED;
  bool spaceMouseCenterCursor = false;

  // 3DConnexion App Sync Settings (synced to/from the 3DxWare cfg XML)
  struct TdxAppSettings {
    std::string motionModel    = "Helicopter";
    bool autoPivot             = false;
    bool lockHorizon           = false;
    bool suspendInput          = false;
    bool lockTo3dViews         = false;
    bool moveObjects           = false;
    bool autokeyAnimation      = false;
    bool selectionFollower     = true;
    int  firstPersonEaseOut    = 600;
    int  floorQueryRate        = 1;
    bool lockSketchPlane       = true;
  } tdxSettings;
  // Note: pivotVisibility is derived from showOrbitCenter / alwaysShowOrbitCenter at write time.

  int skyboxType = SKYBOX_HDR;
  glm::vec3 skyboxSolidColor = glm::vec3(0.2f, 0.3f, 0.4f);
  glm::vec3 skyboxGradientTop = glm::vec3(0.1f, 0.1f, 0.3f);
  glm::vec3 skyboxGradientBottom = glm::vec3(0.7f, 0.7f, 1.0f);
  int selectedCubemap = 0;
  std::string skyboxHdrPath = "skybox/HDR/table_mountain_2_puresky_4k.hdr";
  float skyboxExposure =
      0.2f; // Exposure for HDR skyboxes (typical range: 0.1-0.5)

  // Startup scene settings
  bool loadStartupScene = false;
  std::string startupScenePath = "";

  // Scene loading behavior
  SceneLoadingBehavior sceneLoadingBehavior = SCENE_LOAD_ALWAYS_ASK;

  // Model Import Settings
  struct ModelImportSettings {
    bool flipUVs = false;        // Toggle UV coordinate flipping
    bool flipNormals = false;    // Flip normals (invert direction)
    bool generateNormals = true; // Generate normals if missing
    bool generateSmoothNormals =
        false; // Generate smooth normals instead of flat normals
    bool calculateTangentSpace =
        true; // Calculate tangent and bitangent vectors
    bool joinIdenticalVertices =
        true; // Join identical vertices to reduce redundancy
    bool sortByPrimitiveType =
        true; // Sort by primitive type for better performance
    bool fixInfacingNormals = false;      // Fix inward-facing normals
    bool removeRedundantMaterials = true; // Remove redundant materials
    bool optimizeMeshes = false; // Optimize meshes for better performance
    bool pretransformVertices =
        false; // Pre-transform vertices by node hierarchy
    bool autoScaleLargeModels =
        true; // Automatically scale down oversized models to fit scene
    float maxModelRadius =
        5.0f; // Maximum bounding sphere radius before auto-scaling
  } modelImportSettings;

  // Brush Tool Settings
  struct BrushToolSettings {
    bool enabled = false;        // Brush tool enabled/disabled
    float brushRadius = 0.5f;    // Radius of the brush area
    int selectedModelIndex = -1; // Currently selected model for painting
    float minScale = 0.8f;       // Minimum scale for painted instances
    float maxScale = 1.2f;       // Maximum scale for painted instances
    float rotationRandomization =
        1.0f;                  // Rotation randomization amount (0.0 - 1.0)
    bool alignToNormal = true; // Align instances to surface normal
    float colorVariation =
        0.1f;             // Color variation for painted instances (0.0 - 1.0)
    float density = 1.0f; // Instances per paint stroke
    float minSpacing = 0.1f;     // Minimum distance between instances
    bool showBrushCursor = true; // Show brush cursor indicator
  } brushToolSettings;
};
} // namespace GUI