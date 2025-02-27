#pragma once
#include "core.h"
#include "engine/shader.h"
#include "model_loader.h"

namespace Engine {

    // Lighting structure for voxelization
    struct VoxelLight {
        glm::vec3 position;
        glm::vec3 color;
    };

    // Material settings for voxel cone tracing
    struct VoxelMaterial {
        glm::vec3 diffuseColor;
        float diffuseReflectivity;
        glm::vec3 specularColor;
        float specularReflectivity;
        float specularDiffusion;
        float emissivity;
        float refractiveIndex;
        float transparency;
    };

    // Settings for voxel cone tracing
    struct VoxelConeTracingSettings {
        bool indirectSpecularLight = true;
        bool indirectDiffuseLight = true;
        bool directLight = true;
        bool shadows = true;
        float mipMapHardcap = 5.4f;
        float diffuseIndirectFactor = 0.52f;
        float specularFactor = 4.0f;
        float specularPower = 65.0f;
    };

    class Voxelizer {
    public:
        bool showDebugVisualization = false;
        float debugVoxelSize = 0.1f;
        float voxelOpacity = 0.5f;
        float voxelColorIntensity = 1.0f;
        bool useRayCastVisualization = false;
        VoxelConeTracingSettings coneTracingSettings;

        // Constructor and destructor
        Voxelizer(int resolution = 128);
        ~Voxelizer();

        // Public methods
        void update(const glm::vec3& cameraPos, const std::vector<Model>& models);
        void renderDebugVisualization(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);

        // Getters and setters for voxel properties
        GLuint getVoxelTexture() const { return m_voxelTexture; }
        float getVoxelGridSize() const { return m_voxelGridSize; }
        void setVoxelGridSize(float size) { m_voxelGridSize = size; }
        int getResolution() const { return m_resolution; }
        int getState() const { return m_state; }

        // Methods to manage voxel settings
        void increaseState();
        void decreaseState();
        void setAmbientOcclusion(bool enabled) { m_enableAmbientOcclusion = enabled; }
        bool isAmbientOcclusionEnabled() const { return m_enableAmbientOcclusion; }

        // Set the global ambient light color
        void setAmbientLight(const glm::vec3& color) { m_ambientLight = color; }

        // Add a point light for voxelization
        void addLight(const glm::vec3& position, const glm::vec3& color);
        void clearLights();

        // Configure voxel cone tracing
        void setMipmapFilteringQuality(int quality);
        void setAnisotropicFiltering(bool enabled);

    private:
        // Core voxelization properties
        int m_resolution;
        float m_voxelGridSize;
        GLuint m_voxelTexture;
        bool m_enableAmbientOcclusion = true;
        glm::vec3 m_ambientLight = glm::vec3(0.1f);
        int m_mipmapFilteringQuality = 2; // 0=low, 1=medium, 2=high
        bool m_anisotropicFiltering = true;

        // Voxelization shaders
        Shader* m_voxelShader;

        // Visualization variables
        int m_state = 0; // Mipmap level for visualization
        Shader* m_visualizationShader;
        Shader* m_worldPositionShader;
        Shader* m_voxelCubeShader;

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
        std::vector<VoxelLight> m_lights;

        // Private methods for initialization
        void initializeVoxelTexture();
        void initializeVisualization();
        void setupScreenQuad();
        void setupUnitCube();

        // Methods for voxelization
        void voxelizeScene(const std::vector<Model>& models);
        void applyMaterialToVoxelShader(const Model& model);

        // Methods for visualization
        void renderCubeFaces(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);
        void renderVoxelsAsCubes(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);
        void updateVisibleVoxels();

        // Methods for mipmap generation and texture management
        void generateHighQualityMipmaps();
        void configureTextureFiltering();
    };

} // namespace Engine