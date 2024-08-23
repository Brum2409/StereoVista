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


    const int MAX_LIGHTS = 180;
    struct PointLight {
        glm::vec3 position;
        glm::vec3 color;
        float intensity;
    };

}
