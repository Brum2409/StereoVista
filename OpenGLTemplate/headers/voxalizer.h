#pragma once
#include "core.h"
#include "engine/shader.h"
#include "model_loader.h"

namespace Engine {

    class Voxelizer {
    public:
        bool showDebugVisualization = false;
        float debugVoxelSize = 0.1f;
        float voxelOpacity = 0.5f;   // Controls transparency of visualized voxels
        float voxelColorIntensity = 1.0f; // Controls brightness of voxel colors
        bool useRayCastVisualization = false; // Toggle between ray casting and individual cubes

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

        // Change visualization state (mipmap level)
        void increaseState();
        void decreaseState();
        void increaseVoxelOpacity();
        void decreaseVoxelOpacity();

    private:
        // Private member variables
        int m_resolution;
        float m_voxelGridSize;
        GLuint m_voxelTexture;

        // Voxelization shaders
        Shader* m_voxelShader;

        // Visualization variables
        int m_state = 0; // Mipmap level for visualization
        Shader* m_visualizationShader;
        Shader* m_worldPositionShader;
        Shader* m_voxelCubeShader;   // Shader for rendering individual voxel cubes

        // Screen quad for visualization
        GLuint m_quadVAO, m_quadVBO;

        // Cube for visualization
        GLuint m_cubeVAO, m_cubeVBO;

        // FBO and textures for front/back faces
        GLuint m_frontFBO, m_backFBO;
        GLuint m_frontTexture, m_backTexture;

        // Voxel data for direct rendering
        struct VoxelData {
            glm::vec3 position;
            glm::vec4 color;
        };
        std::vector<VoxelData> m_visibleVoxels;
        GLuint m_voxelInstanceVBO;
        bool m_voxelDataNeedsUpdate = true;

        // Lights for voxelization
        struct PointLight {
            glm::vec3 position;
            glm::vec3 color;
        };

        std::vector<PointLight> m_lights;

        // Private methods
        void initializeVoxelTexture();
        void initializeVisualization();
        void setupScreenQuad();
        void setupUnitCube();
        void renderCubeFaces(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);
        void renderVoxelsAsCubes(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);
        void updateVisibleVoxels();
    };

} // namespace Engine