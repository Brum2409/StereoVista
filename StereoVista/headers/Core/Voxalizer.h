#pragma once
#include "Engine/Core.h"
#include "Engine/Shader.h"
#include "Engine/BVH.h"
#include "Engine/Data.h"
#include "Loaders/ModelLoader.h"

namespace Engine {

    class Voxelizer {
    public:
        enum VisualizationMode {
            VISUALIZATION_NORMAL,
            VISUALIZATION_LUMINANCE,
            VISUALIZATION_ALPHA,
            VISUALIZATION_EMISSIVE
        };

        bool showDebugVisualization = false;
        float debugVoxelSize = 0.02f;  // Fixed size for debug voxel display (independent of grid size)
        float voxelOpacity = 1.0f;   // Controls transparency of visualized voxels
        float voxelColorIntensity = 1.0f; // Controls brightness of voxel colors
        bool debugWireframe = false;  // Render voxel cubes as wireframe
        VisualizationMode visualizationMode = VISUALIZATION_NORMAL;

        Voxelizer(int resolution = 128);
        ~Voxelizer();

        void update(const glm::vec3& cameraPos, const std::vector<Model>& models);
        void renderDebugVisualization(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);

        GLuint getVoxelTexture() const { return m_voxelTexture; }
        float getVoxelGridSize() const { return m_voxelGridSize; }
        int getResolution() const { return m_resolution; }
        void setVoxelGridSize(float size) {
            if (m_voxelGridSize != size) {
                m_voxelGridSize = size;
                m_needsRevoxelization = true;
            }
        }
        glm::vec3 getGridCenter() const { return m_gridCenter; }
        void setGridCenter(const glm::vec3& center) {
            if (m_gridCenter != center) {
                m_gridCenter = center;
                m_needsRevoxelization = true;
            }
        }

        // Dirty flag for caching - skip re-voxelization if scene hasn't changed
        void markDirty() { m_needsRevoxelization = true; }
        bool isDirty() const { return m_needsRevoxelization; }

        // Sync scene lights into the voxelizer so that voxelized lighting
        // matches the actual scene.  Intensity is baked into color.
        // Only marks dirty when the light list actually changed.
        void setLights(const std::vector<Engine::PointLight>& sceneLights) {
            // Build the new list and compare
            std::vector<VoxelLight> newLights;
            newLights.reserve(sceneLights.size());
            for (const auto& sl : sceneLights) {
                newLights.push_back({ sl.position, sl.color * sl.intensity });
            }
            if (newLights.size() != m_lights.size()) {
                m_lights = std::move(newLights);
                m_needsRevoxelization = true;
                return;
            }
            for (size_t i = 0; i < newLights.size(); ++i) {
                if (newLights[i].position != m_lights[i].position ||
                    newLights[i].color != m_lights[i].color) {
                    m_lights = std::move(newLights);
                    m_needsRevoxelization = true;
                    return;
                }
            }
        }

        // Auto-fit grid to scene bounds with optional padding factor (1.1 = 10% padding)
        void autoFitGridToScene(const std::vector<Model>& models, float paddingFactor = 1.1f);

        // Mipmap level for debug visualization
        int getDebugMipLevel() const { return m_state; }
        void setDebugMipLevel(int level) {
            int maxLevels = getMaxMipLevels();
            int clamped = std::clamp(level, 0, maxLevels - 1);
            if (m_state != clamped) {
                m_state = clamped;
                m_voxelDataNeedsUpdate = true;
            }
        }
        int getMaxMipLevels() const {
            return static_cast<int>(std::floor(std::log2(m_resolution))) + 1;
        }

        // Change visualization state (mipmap level)
        void increaseState();
        void decreaseState();

        void cycleVisualizationMode() {
            visualizationMode = static_cast<VisualizationMode>((static_cast<int>(visualizationMode) + 1) % 4);
        }

        // Calculate appropriate mipmap level based on distance from camera
        int calculateMipmapLevel(float distanceFromCamera) const {
            // Distance-based LOD selection with faster falloff
            // Only very close to camera uses highest detail (level 0)
            // Further distances quickly scale up to lower detail
            float lodFactor = distanceFromCamera / 2.0f; // Smaller reference = faster falloff
            int level = static_cast<int>(std::floor(std::log2(std::max(0.5f, lodFactor))));
            int maxLevels = static_cast<int>(std::log2(m_resolution));
            return std::clamp(level, 0, maxLevels);
        }

        void clearVoxelTexture() {
            glClearTexImage(m_voxelTexture, 0, GL_RGBA, GL_FLOAT, nullptr);
        }

        // Generate mipmaps using compute shader for alpha-weighted averaging
        // This prevents light bleeding by properly weighting colors by alpha
        void generateMipmaps();

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
        glm::vec3 m_gridCenter = glm::vec3(0.0f);  // Grid center position in world space
        GLuint m_voxelTexture;
        bool m_needsRevoxelization = true;  // Dirty flag - true on first run

        Shader* m_voxelShader;
        Shader* m_mipmapComputeShader;  // Compute shader for alpha-weighted mipmap generation

        // Visualization variables
        int m_state = 0; // Mipmap level for visualization
        Shader* m_voxelCubeShader;   // Shader for rendering individual voxel cubes

        // Cube for visualization
        GLuint m_cubeVAO, m_cubeVBO;

        // Voxel data for direct rendering
        struct VoxelData {
            glm::vec3 position;
            glm::vec4 color;
            int mipmapLevel;  // Store the mipmap level for proper size rendering
        };
        std::vector<VoxelData> m_visibleVoxels;
        GLuint m_voxelInstanceVBO;
        bool m_voxelDataNeedsUpdate = true;

        // Lights for voxelization (renamed to avoid collision with Engine::PointLight)
        struct VoxelLight {
            glm::vec3 position;
            glm::vec3 color;
        };

        std::vector<VoxelLight> m_lights;

        void initializeVoxelTexture();
        void initializeVisualization();
        void setupUnitCube();
        void renderVoxelsAsCubes(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view);
        void updateVisibleVoxels(const glm::vec3& cameraPos);
    };

} 