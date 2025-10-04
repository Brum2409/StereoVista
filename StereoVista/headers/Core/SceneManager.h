#pragma once

#include <vector>
#include <string>
#include "Loaders/ModelLoader.h"
#include "Core/Camera.h"
#include "Engine/Data.h"
#include <glm/glm.hpp> // Added glm header for glm::vec2

namespace Engine {

    struct Scene {
        std::vector<Model> models;
        std::vector<PointCloud> pointClouds;
        std::vector<PointLight> pointLights;
        std::vector<SpotLight> spotLights;
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