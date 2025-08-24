#pragma once

#include <glm/glm.hpp>
#include <vector>
#include "../engine/shader.h"
#include "BVH.h"

namespace Engine {

    class BVHDebugRenderer {
    private:
        GLuint VAO, VBO;
        Engine::Shader* debugShader;
        std::vector<float> lineVertices;
        bool initialized;

        // Generate line vertices for an AABB
        void addAABBLines(const glm::vec3& minBounds, const glm::vec3& maxBounds, int depth);

    public:
        enum RenderMode {
            DEPTH_TESTED,    // Lines respect depth (only visible above models)
            ALWAYS_ON_TOP,   // Lines always visible (ignore depth)
            DEPTH_BIASED     // Lines slightly in front (compromise)
        };
        
        BVHDebugRenderer();
        ~BVHDebugRenderer();

        // Initialize OpenGL resources
        void initialize();
        void cleanup();

        // Update debug geometry from BVH
        void updateFromBVH(const std::vector<BVHNode>& nodes, int maxDepth = 3);

        // Render debug visualization
        void render(const glm::mat4& view, const glm::mat4& projection);

        // Toggle rendering
        void setEnabled(bool enabled) { renderEnabled = enabled; }
        bool isEnabled() const { return renderEnabled; }
        
        // Set render mode
        void setRenderMode(RenderMode mode) { renderMode = mode; }
        RenderMode getRenderMode() const { return renderMode; }

    private:
        bool renderEnabled = false;
        RenderMode renderMode = ALWAYS_ON_TOP;
    };

} // namespace Engine