#pragma once
#include "Engine/Core.h"
#include "Engine/Shader.h"
#include "Loaders/ModelLoader.h"

namespace Engine {

    class Voxelizer {
    public:
        enum VisualizationMode {
            VISUALIZATION_NORMAL,
            VISUALIZATION_LUMINANCE,
            VISUALIZATION_ALPHA
        };

        bool showDebugVisualization = false;
        float debugVoxelSize = 0.1f;
        float voxelOpacity = 0.5f;   // Controls transparency of visualized voxels
        float voxelColorIntensity = 1.0f; // Controls brightness of voxel colors
        bool useRayCastVisualization = false; // Toggle between ray casting(currently broken) and individual cubes
        VisualizationMode visualizationMode = VISUALIZATION_NORMAL;

        Voxelizer(int resolution = 128);
        ~Voxelizer();

        void update(const glm::vec3& cameraPos, const std::vector<Model>& models);
        void renderDebugVisualization(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);

        GLuint getVoxelTexture() const { return m_voxelTexture; }
        float getVoxelGridSize() const { return m_voxelGridSize; }
        void setVoxelGridSize(float size) { m_voxelGridSize = size; }

        // Change visualization state (mipmap level)
        void increaseState();
        void decreaseState();

        void cycleVisualizationMode() {
            visualizationMode = static_cast<VisualizationMode>((static_cast<int>(visualizationMode) + 1) % 3);
        }

        void clearVoxelTexture() {
            glClearTexImage(m_voxelTexture, 0, GL_RGBA, GL_FLOAT, nullptr);
        }

        void generateMipmaps() {
            glBindTexture(GL_TEXTURE_3D, m_voxelTexture);
            glGenerateMipmap(GL_TEXTURE_3D);
        }

        // Method to re-initialize the voxel texture with a new resolution
        void resizeVoxelTexture(int newResolution) {
            glDeleteTextures(1, &m_voxelTexture);

            m_resolution = newResolution;

            initializeVoxelTexture();
        }

        // Method to set voxel material properties for rendering
        void setVoxelMaterial(Engine::Shader* shader, const Engine::Model& model) {
            shader->setVec3("material.diffuseColor", model.color);
            shader->setVec3("material.specularColor", model.specularColor);
            shader->setFloat("material.diffuseReflectivity", model.diffuseReflectivity);
            shader->setFloat("material.specularReflectivity", model.specularReflectivity);
            shader->setFloat("material.specularDiffusion", model.specularDiffusion);
            shader->setFloat("material.emissivity", model.emissive);
            shader->setFloat("material.refractiveIndex", model.refractiveIndex);
            shader->setFloat("material.transparency", model.transparency);
        }

    private:
        int m_resolution;
        float m_voxelGridSize;
        GLuint m_voxelTexture;

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

        void initializeVoxelTexture();
        void initializeVisualization();
        void setupScreenQuad();
        void setupUnitCube();
        void renderCubeFaces(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);
        void renderVoxelsAsCubes(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);
        void updateVisibleVoxels();
    };

} 