#pragma once

#include <vector>
#include <string>
#include "model_loader.h"

namespace Engine {

    struct SceneSettings {
        float separation = 0.02f;
        float convergence = 1.0f;
        float nearPlane = 0.1f;
        float farPlane = 200.0f;
        int msaaSamples = 4;
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

}  // namespace Engine