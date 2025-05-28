#pragma once

#include <vector>
#include <string>
#include "Loaders/ModelLoader.h"
#include <glm/glm.hpp> // Added glm header for glm::vec2

namespace Engine {

    struct SceneSettings {
        float separation = 0.02f;
        float convergence = 1.0f;
        float nearPlane = 0.1f;
        float farPlane = 200.0f;
        int msaaSamples = 2;
        
        // Added radar fields:
        bool radarEnabled = false;
        glm::vec2 radarPos = glm::vec2(0.8f, -0.8f);
        float radarScale = 0.2f;
        bool radarShowScene = true;
    };

    struct Scene {
        std::vector<Model> models;
        std::vector<PointCloud> pointClouds;
        SceneSettings settings;
    };

    void saveScene(const std::string& filename, const Scene& scene);
    void saveModelData(const Model& model, const std::string& filename);
    Scene loadScene(const std::string& filename);
    void loadModelData(Model& model, const std::string& filename);

} 