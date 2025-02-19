#pragma once
#include "core.h"
#include "engine/shader.h"
#include "model_loader.h"

namespace Engine {

    class Voxelizer {
    public:
        bool showDebugVisualization = false;
        float debugVoxelSize = 0.1f;

        // Constructor and destructor
        Voxelizer(int resolution = 128);
        ~Voxelizer();

        // Public methods
        void update(const glm::vec3& cameraPos, const std::vector<Model>& models);
        void renderDebugVisualization(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);

        // Getters and setters
        GLuint getVoxelTexture() const { return m_voxelTexture; }
        float getVoxelGridSize() const { return m_voxelGridSize; }
        void setVoxelGridSize(float size) { m_voxelGridSize = size; }

    private:
        // Private member variables
        int m_resolution;
        float m_voxelGridSize;
        GLuint m_voxelTexture;
        GLuint m_debugCubeVAO;
        GLuint m_debugCubeVBO;
        Shader* m_voxelShader;
        Shader* m_debugShader;

        // Private methods
        void initializeVoxelTexture();
        void initializeDebugCube();
        void loadShaders();
    };

} // namespace Engine