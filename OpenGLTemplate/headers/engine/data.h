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

    struct PointCloudChunk {
        std::vector<PointCloudPoint> points;
        glm::vec3 centerPosition;
        float boundingRadius;
        std::vector<GLuint> lodVBOs;
        std::vector<size_t> lodPointCounts;
    };

    struct PointCloud {
        std::string name;
        std::string filePath;
        std::vector<PointCloudPoint> points;
        glm::vec3 position;
        glm::vec3 rotation;
        glm::vec3 scale;
        bool visible = true;
        GLuint vao;
        GLuint vbo;

        std::vector<PointCloudChunk> chunks;
        float lodDistances[5] = { 50.0f, 100.0f, 200.0f, 400.0f, 800.0f };
        float chunkSize = 10.0f;
        float newChunkSize = 10.0f;

        GLuint chunkOutlineVAO;
        GLuint chunkOutlineVBO;
        std::vector<glm::vec3> chunkOutlineVertices;
        bool visualizeChunks;
    };

    const int MAX_LIGHTS = 180;
    struct PointLight {
        glm::vec3 position;
        glm::vec3 color;
        float intensity;
    };

}
