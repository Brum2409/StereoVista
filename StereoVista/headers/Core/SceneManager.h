#pragma once

#include <vector>
#include <string>
#include "Loaders/ModelLoader.h"
#include "Core/Camera.h"
#include <glm/glm.hpp> // Added glm header for glm::vec2

namespace Engine {

    struct SceneSettings {
        float separation = 0.5f;
        float convergence = 2.6f;
        float nearPlane = 0.1f;
        float farPlane = 200.0f;
        int msaaSamples = 2;
        
        // Added radar fields:
        bool radarEnabled = false;
        glm::vec2 radarPos = glm::vec2(0.8f, -0.8f);
        float radarScale = 0.2f;
        bool radarShowScene = true;
        
        // Zero plane visualization
        bool showZeroPlane = false;
    };

    struct Scene {
        std::vector<Model> models;
        std::vector<PointCloud> pointClouds;
        SceneSettings settings;
        Camera::CameraState cameraState;
        
        // Default constructor with default camera state
        Scene() {
            // Initialize with default camera values matching Camera.h
            cameraState.position = glm::vec3(0.0f, 0.0f, 3.0f);
            cameraState.front = glm::vec3(0.0f, 0.0f, -1.0f);
            cameraState.up = glm::vec3(0.0f, 1.0f, 0.0f);
            cameraState.yaw = -90.0f;
            cameraState.pitch = 0.0f;
            cameraState.zoom = 45.0f;
        }
    };

    void saveScene(const std::string& filename, const Scene& scene, const Camera& camera);
    void saveModelData(const Model& model, const std::string& filename);
    Scene loadScene(const std::string& filename, Camera& camera);
    void loadModelData(Model& model, const std::string& filename);

} 