#pragma once

#include "Engine/Core.h"
#include "Loaders/ModelLoader.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>
#include <random>

namespace Tools {

    // Instance data for a single painted model instance
    struct InstanceData {
        glm::mat4 modelMatrix;
        glm::vec3 color;
        float padding; // Alignment padding for GPU

        InstanceData() : modelMatrix(1.0f), color(1.0f), padding(0.0f) {}
        InstanceData(const glm::mat4& mat, const glm::vec3& col)
            : modelMatrix(mat), color(col), padding(0.0f) {}
    };

    // Painted model group - all instances of a specific source model
    struct PaintedModelGroup {
        int sourceModelIndex;           // Index into scene.models
        std::vector<InstanceData> instances;
        GLuint instanceVBO;            // GPU buffer for instance data
        bool needsUpdate;              // Flag to update GPU buffer

        PaintedModelGroup(int modelIdx)
            : sourceModelIndex(modelIdx), instanceVBO(0), needsUpdate(true) {}

        ~PaintedModelGroup() {
            if (instanceVBO != 0) {
                glDeleteBuffers(1, &instanceVBO);
            }
        }
    };

    // Brush cluster - a named group of painted instances with specific settings
    struct BrushCluster {
        std::string name;
        int sourceModelIndex;           // Index into scene.models

        // Cluster-specific painting settings
        float minScale;
        float maxScale;
        float rotationRandomization;
        bool alignToNormal;
        float colorVariation;

        // Instance data
        std::vector<InstanceData> instances;
        GLuint instanceVBO;
        bool needsUpdate;

        BrushCluster(const std::string& clusterName, int modelIdx)
            : name(clusterName), sourceModelIndex(modelIdx),
              minScale(0.8f), maxScale(1.2f), rotationRandomization(0.0f),
              alignToNormal(true), colorVariation(0.0f),
              instanceVBO(0), needsUpdate(true) {}

        ~BrushCluster() {
            if (instanceVBO != 0) {
                glDeleteBuffers(1, &instanceVBO);
            }
        }
    };

    class BrushTool {
    public:
        BrushTool();
        ~BrushTool();

        // Core functionality
        void enable();
        void disable();
        bool isEnabled() const { return m_enabled; }

        // Cluster management
        int createCluster(const std::string& name, int modelIndex);
        void deleteCluster(int clusterIndex);
        void setActiveCluster(int clusterIndex);
        int getActiveCluster() const { return m_activeClusterIndex; }
        int getClusterCount() const { return static_cast<int>(m_clusters.size()); }
        BrushCluster* getCluster(int index);
        const BrushCluster* getCluster(int index) const;
        std::vector<BrushCluster>& getClusters() { return m_clusters; }
        const std::vector<BrushCluster>& getClusters() const { return m_clusters; }

        // Painting (paints to active cluster)
        void paintInstance(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& cameraPos, const glm::vec3& sourceModelScale);
        void clearAllInstances();
        void clearInstancesForCluster(int clusterIndex);

        // Instance management
        void removeLastInstance();
        int getTotalInstanceCount() const;
        int getInstanceCountForCluster(int clusterIndex) const;

        // Rendering
        void renderInstances(Engine::Shader* shader, const std::vector<Engine::Model>& models);
        void renderBrushIndicator(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cursorPos, const glm::vec3& cursorNormal);
        void updateInstanceBuffers();

        // Global settings (not cluster-specific)
        void setBrushRadius(float radius) { m_brushRadius = radius; }
        float getBrushRadius() const { return m_brushRadius; }

        void setSelectedModel(int index) { m_selectedModelIndex = index; }
        int getSelectedModel() const { return m_selectedModelIndex; }

        void setDensity(float density) { m_density = density; }
        float getDensity() const { return m_density; }

        void setMinSpacing(float spacing) { m_minSpacing = spacing; }
        float getMinSpacing() const { return m_minSpacing; }

        // Access to painted groups for scene saving/loading
        std::vector<PaintedModelGroup>& getPaintedGroups() { return m_paintedGroups; }
        const std::vector<PaintedModelGroup>& getPaintedGroups() const { return m_paintedGroups; }

    private:
        bool m_enabled;
        float m_brushRadius;
        int m_selectedModelIndex; // For creating new clusters

        // Global painting settings (not cluster-specific)
        float m_density;                // Instances per paint stroke
        float m_minSpacing;             // Minimum distance between instances

        // Cluster storage
        std::vector<BrushCluster> m_clusters;
        int m_activeClusterIndex;       // Currently selected cluster for painting/editing (-1 if none)

        // Legacy support (for backward compatibility)
        std::vector<PaintedModelGroup> m_paintedGroups;

        // Random number generation
        std::mt19937 m_rng;
        std::uniform_real_distribution<float> m_dist;

        // Helper functions
        PaintedModelGroup* findOrCreateGroup(int modelIndex);
        glm::mat4 createInstanceTransform(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& cameraPos, const glm::vec3& sourceModelScale, const BrushCluster* cluster);
        glm::vec3 generateInstanceColor(const glm::vec3& baseColor, float colorVariation);
        bool isTooCloseToExisting(const glm::vec3& position, const BrushCluster* cluster);
        void setupInstanceAttributes(GLuint vao);
    };

} // namespace Tools
