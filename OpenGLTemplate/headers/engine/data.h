#pragma once
#include "core.h"

namespace Engine {

    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoords;
        glm::vec3 tangent;
        glm::vec3 bitangent;
        int materialID; 
    };

    struct PointCloudPoint {
        glm::vec3 position;
        float intensity;
        glm::vec3 color;
    };

    struct PointCloud {
        std::string name;
        std::vector<PointCloudPoint> points;
        glm::vec3 position;
        glm::vec3 rotation;
        glm::vec3 scale;
        GLuint vao;
        GLuint vbo;
    };

    const int MAX_LIGHTS = 180;
    struct PointLight {
        glm::vec3 position;
        glm::vec3 color;
        float intensity;
    };

}
