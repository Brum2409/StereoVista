#pragma once

#include <vector>
#include <string>
#include "Loaders/ModelLoader.h"
#include "Core/Camera.h"
#include "Engine/Data.h"
#include <glm/glm.hpp> // Added glm header for glm::vec2

namespace Engine {

    // Current on-disk scene format version. Bumped whenever the schema changes
    // so loadScene can apply backward-compatible defaults for older files.
    //   v1: original (untagged) format - models / point clouds / lights /
    //       measurements / clip planes / camera.
    //   v2: adds a "formatVersion" tag, a "metadata" header, an optional
    //       "environment" block (skybox + lighting mode), and a serialized sun.
    constexpr int kSceneFormatVersion = 2;

    // Human-readable / bookkeeping metadata stored in the scene header. All
    // fields are optional; timestamps are ISO-8601-ish strings produced at save
    // time and round-trip through load so "created" is preserved.
    struct SceneMetadata {
        std::string description;
        std::string author;
        std::string createdAt;   // first time the scene was saved
        std::string modifiedAt;  // most recent save
        std::string appVersion;  // app build that wrote the file
    };

    // Per-scene environment / lighting settings persisted alongside geometry so
    // a scene reopens looking the way it was authored. `present` records whether
    // the loaded file actually contained this block (older v1 files do not).
    struct SceneEnvironment {
        bool present = false;
        int  lightingMode = 0;          // GUI::LightingMode
        int  skyboxType = 1;            // GUI::SkyboxType (HDR default)
        glm::vec3 skyboxSolidColor     = glm::vec3(0.2f, 0.3f, 0.4f);
        glm::vec3 skyboxGradientTop    = glm::vec3(0.1f, 0.1f, 0.3f);
        glm::vec3 skyboxGradientBottom = glm::vec3(0.7f, 0.7f, 1.0f);
        int       selectedCubemap      = 0;
        std::string skyboxHdrPath;
        float     skyboxExposure       = 0.2f;
    };

    // Controls which optional blocks saveScene writes. Geometry (models, point
    // clouds) is always written; everything else is opt-out.
    struct SceneSaveOptions {
        bool includeCamera       = true;  // viewpoint
        bool includeLighting     = true;  // sun + point/spot lights
        bool includeEnvironment  = true;  // skybox + lighting mode
        bool includeMeasurements = true;
        bool includeClipPlanes   = true;
        bool compact             = false; // minified JSON (no indentation)
    };

    // Lightweight scene header, read by loadSceneInfo without loading geometry.
    // Used to populate the recent-scenes list and the Scene Manager preview.
    struct SceneInfo {
        bool valid = false;
        std::string path;
        int formatVersion = 0;
        SceneMetadata metadata;
        int modelCount = 0;
        int pointCloudCount = 0;
        int pointLightCount = 0;
        int spotLightCount = 0;
        int measurementCount = 0;
        int clipPlaneCount = 0;
    };

    // Per-load diagnostics. Populated when a non-null report is passed to
    // loadScene so the UI can surface partial loads (e.g. missing assets)
    // instead of silently dropping objects.
    struct SceneLoadReport {
        std::vector<std::string> warnings;
        int modelsLoaded = 0,  modelsFailed = 0;
        int pointCloudsLoaded = 0, pointCloudsFailed = 0;
        int pointLightsLoaded = 0;
        int spotLightsLoaded = 0;
        int measurementsLoaded = 0;
        int clipPlanesLoaded = 0;
        int formatVersion = 0;

        bool hasWarnings() const { return !warnings.empty(); }
    };

    struct Scene {
        std::vector<Model> models;
        std::vector<PointCloud> pointClouds;
        std::vector<PointLight> pointLights;
        std::vector<SpotLight> spotLights;
        std::vector<Measurement> measurements;
        std::vector<ClipPlane> clipPlanes;
        Camera::CameraState cameraState;

        // Persisted scene-level state (since v2).
        Sun sun;
        bool hasSun = false; // transient: did the loaded file carry a sun?
        SceneEnvironment environment;
        SceneMetadata metadata;

        // Default constructor with default camera state
        Scene() {
            // Initialize with default camera values matching Camera.h
            cameraState.position = glm::vec3(0.0f, 0.0f, 3.0f);
            cameraState.front = glm::vec3(0.0f, 0.0f, -1.0f);
            cameraState.up = glm::vec3(0.0f, 1.0f, 0.0f);
            cameraState.yaw = -90.0f;
            cameraState.pitch = 0.0f;
            cameraState.zoom = 45.0f;

            // Neutral default sun (overwritten by the live sun before save).
            sun.direction = glm::vec3(-0.2f, -1.0f, -0.3f);
            sun.color = glm::vec3(1.0f);
            sun.intensity = 1.0f;
            sun.enabled = true;
        }
    };

    void saveScene(const std::string& filename, const Scene& scene,
                   const Camera& camera,
                   const SceneSaveOptions& options = SceneSaveOptions());
    void saveModelData(const Model& model, const std::string& filename);
    Scene loadScene(const std::string& filename, Camera& camera,
                    SceneLoadReport* report = nullptr);
    void loadModelData(Model& model, const std::string& filename);

    // Parse just the header of a .scene file (version, metadata, object counts)
    // without loading geometry. Returns SceneInfo with valid == false if the
    // file cannot be read or parsed.
    SceneInfo loadSceneInfo(const std::string& filename);

}