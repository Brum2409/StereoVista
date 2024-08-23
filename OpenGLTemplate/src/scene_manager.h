#pragma once

#include <vector>
#include <string>
#include "obj_loader.h"

namespace Engine {

    struct SceneSettings {
        float separation= 0.02;
        float convergence = 1.0f;
        float nearPlane = 0.1f;
        float farPlane = 100.0f;
    };

    struct Scene {
        std::vector<ObjModel> models;
        SceneSettings settings;
    };

    void saveScene(const std::string& filename, const Scene& scene);
    Scene loadScene(const std::string& filename);

}  // namespace Engine