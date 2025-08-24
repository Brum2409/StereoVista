#pragma once

#include <glad/glad.h>
#include <glm/glm.hpp>
#include <string>
#include <vector>

namespace Engine {
    class Shader;
    class CursorPreset;
}

namespace GUI {
    enum SkyboxType {
        SKYBOX_CUBEMAP,
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
        int diffuseConeCount = 9;     // Number of cones for indirect diffuse (1, 5, or 9)
        float tracingMaxDistance = 1.41421356237; // Maximum distance for cone tracing in grid units (default: SQRT2)
        int shadowSampleCount = 18;    // Number of samples for shadow cones
        float shadowStepMultiplier = 0.15f; // Step size multiplier for shadows
    };

    enum CursorScalingMode {
        CURSOR_NORMAL,
        CURSOR_FIXED,
        CURSOR_CONSTRAINED_DYNAMIC,
        CURSOR_LOGARITHMIC
    };

    enum SpaceMouseAnchorMode {
        SPACEMOUSE_ANCHOR_DISABLED,      // Use scene center (default)
        SPACEMOUSE_ANCHOR_ON_START,      // Set anchor when navigation starts, keep it fixed
        SPACEMOUSE_ANCHOR_CONTINUOUS     // Update anchor every frame during navigation
    };

    // Structure definitions
    struct SkyboxConfig {
        SkyboxType type = SKYBOX_CUBEMAP;
        glm::vec3 solidColor = glm::vec3(0.2f, 0.3f, 0.4f);
        glm::vec3 gradientTopColor = glm::vec3(0.1f, 0.1f, 0.3f);
        glm::vec3 gradientBottomColor = glm::vec3(0.7f, 0.7f, 1.0f);
        int selectedCubemap = 0;  // Index of the selected predefined cubemap
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
        Engine::Shader* shader = nullptr;
    };

    struct ApplicationPreferences {
        bool isDarkTheme = true;
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

        // Lighting settings
        LightingMode lightingMode = LIGHTING_SHADOW_MAPPING;
        bool enableShadows = true;
        VCTSettings vctSettings;
        
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
            int bvhDebugRenderMode = 1; // 0=DEPTH_TESTED, 1=ALWAYS_ON_TOP, 2=DEPTH_BIASED
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

        bool showStereoVisualization = true;

        bool radarEnabled = false;
        glm::vec2 radarPos = glm::vec2(0.8f, -0.8f);
        float radarScale = 0.03f;
        bool radarShowScene = true;

        bool showZeroPlane = false;

        // SpaceMouse settings
        bool spaceMouseEnabled = true;
        float spaceMouseDeadzone = 0.025f;
        float spaceMouseTranslationSensitivity = 1.0f;
        float spaceMouseRotationSensitivity = 1.0f;
        SpaceMouseAnchorMode spaceMouseAnchorMode = SPACEMOUSE_ANCHOR_DISABLED;
        bool spaceMouseCenterCursor = false;

        int skyboxType = SKYBOX_CUBEMAP;
        glm::vec3 skyboxSolidColor = glm::vec3(0.2f, 0.3f, 0.4f);
        glm::vec3 skyboxGradientTop = glm::vec3(0.1f, 0.1f, 0.3f);
        glm::vec3 skyboxGradientBottom = glm::vec3(0.7f, 0.7f, 1.0f);
        int selectedCubemap = 0;
    };
}