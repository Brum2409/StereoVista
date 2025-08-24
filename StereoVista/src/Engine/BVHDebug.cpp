#ifndef NOMINMAX
#define NOMINMAX
#endif
#include "../../headers/Engine/BVHDebug.h"
#include <iostream>
#include <algorithm>

namespace Engine {

    BVHDebugRenderer::BVHDebugRenderer() : VAO(0), VBO(0), debugShader(nullptr), initialized(false) {
    }

    BVHDebugRenderer::~BVHDebugRenderer() {
        cleanup();
    }

    void BVHDebugRenderer::initialize() {
        if (initialized) return;

        // Generate OpenGL objects
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);

        // Position attribute (location 0)
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        // Color attribute (location 1)  
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
        glEnableVertexAttribArray(1);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        // Create simple debug shader program manually
        const char* vertexShaderSource = R"(
#version 330 core
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aColor;

uniform mat4 view;
uniform mat4 projection;

out vec3 color;

void main() {
    gl_Position = projection * view * vec4(aPos, 1.0);
    color = aColor;
}
)";

        const char* fragmentShaderSource = R"(
#version 330 core
in vec3 color;
out vec4 FragColor;

void main() {
    FragColor = vec4(color, 1.0);
}
)";

        // Compile shaders manually
        GLuint vertexShader = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertexShader, 1, &vertexShaderSource, NULL);
        glCompileShader(vertexShader);
        
        GLuint fragmentShader = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragmentShader, 1, &fragmentShaderSource, NULL);
        glCompileShader(fragmentShader);
        
        GLuint shaderProgram = glCreateProgram();
        glAttachShader(shaderProgram, vertexShader);
        glAttachShader(shaderProgram, fragmentShader);
        glLinkProgram(shaderProgram);
        
        glDeleteShader(vertexShader);
        glDeleteShader(fragmentShader);
        
        debugShader = new Engine::Shader(shaderProgram);

        initialized = true;
        std::cout << "BVH debug renderer initialized" << std::endl;
    }

    void BVHDebugRenderer::cleanup() {
        if (VAO != 0) {
            glDeleteVertexArrays(1, &VAO);
            VAO = 0;
        }
        if (VBO != 0) {
            glDeleteBuffers(1, &VBO);
            VBO = 0;
        }
        if (debugShader) {
            delete debugShader;
            debugShader = nullptr;
        }
        initialized = false;
    }

    void BVHDebugRenderer::addAABBLines(const glm::vec3& minBounds, const glm::vec3& maxBounds, int depth) {
        // Enhanced color palette for better level differentiation
        // Using HSV-like color wheel for distinct colors per level
        float r, g, b;
        
        switch (depth % 8) { // Cycle through 8 distinct colors
        case 0: r = 1.0f; g = 0.2f; b = 0.2f; break; // Red
        case 1: r = 1.0f; g = 0.6f; b = 0.0f; break; // Orange  
        case 2: r = 1.0f; g = 1.0f; b = 0.0f; break; // Yellow
        case 3: r = 0.0f; g = 1.0f; b = 0.2f; break; // Green
        case 4: r = 0.0f; g = 0.8f; b = 1.0f; break; // Cyan
        case 5: r = 0.2f; g = 0.2f; b = 1.0f; break; // Blue
        case 6: r = 0.8f; g = 0.2f; b = 1.0f; break; // Purple
        case 7: r = 1.0f; g = 0.2f; b = 0.8f; break; // Magenta
        default: r = 1.0f; g = 1.0f; b = 1.0f; break; // White fallback
        }
        
        // Add slight brightness variation and ensure minimum visibility
        float brightness = 1.0f - (depth * 0.04f); // Slightly dimmer for deeper levels
        brightness = (brightness < 0.7f) ? 0.7f : brightness; // Keep reasonably bright
        
        // Apply brightness while maintaining color distinction
        r *= brightness;
        g *= brightness;
        b *= brightness;
        
        // Ensure minimum color values for visibility
        r = (r < 0.1f) ? 0.1f : r;
        g = (g < 0.1f) ? 0.1f : g;
        b = (b < 0.1f) ? 0.1f : b;

        // 8 corners of the AABB
        glm::vec3 corners[8] = {
            {minBounds.x, minBounds.y, minBounds.z}, // 0
            {maxBounds.x, minBounds.y, minBounds.z}, // 1
            {maxBounds.x, maxBounds.y, minBounds.z}, // 2
            {minBounds.x, maxBounds.y, minBounds.z}, // 3
            {minBounds.x, minBounds.y, maxBounds.z}, // 4
            {maxBounds.x, minBounds.y, maxBounds.z}, // 5
            {maxBounds.x, maxBounds.y, maxBounds.z}, // 6
            {minBounds.x, maxBounds.y, maxBounds.z}  // 7
        };

        // 12 edges of the cube
        int edges[12][2] = {
            {0, 1}, {1, 2}, {2, 3}, {3, 0}, // Bottom face
            {4, 5}, {5, 6}, {6, 7}, {7, 4}, // Top face
            {0, 4}, {1, 5}, {2, 6}, {3, 7}  // Vertical edges
        };

        // Add lines for each edge
        for (int i = 0; i < 12; i++) {
            // First vertex
            lineVertices.push_back(corners[edges[i][0]].x);
            lineVertices.push_back(corners[edges[i][0]].y);
            lineVertices.push_back(corners[edges[i][0]].z);
            lineVertices.push_back(r);
            lineVertices.push_back(g);
            lineVertices.push_back(b);

            // Second vertex
            lineVertices.push_back(corners[edges[i][1]].x);
            lineVertices.push_back(corners[edges[i][1]].y);
            lineVertices.push_back(corners[edges[i][1]].z);
            lineVertices.push_back(r);
            lineVertices.push_back(g);
            lineVertices.push_back(b);
        }
    }

    void BVHDebugRenderer::updateFromBVH(const std::vector<BVHNode>& nodes, int maxDepth) {
        if (!initialized) initialize();

        lineVertices.clear();

        // Traverse BVH and collect AABB line data
        std::function<void(uint32_t, int)> traverse = [&](uint32_t nodeIdx, int depth) {
            if (nodeIdx >= nodes.size() || depth > maxDepth) return;

            const BVHNode& node = nodes[nodeIdx];
            
            // Add lines for this node's AABB
            addAABBLines(node.minBounds, node.maxBounds, depth);

            // Recurse to children if this is an interior node
            if (!node.isLeaf() && depth < maxDepth) {
                traverse(node.leftFirst, depth + 1);     // Left child
                traverse(node.leftFirst + 1, depth + 1); // Right child
            }
        };

        if (!nodes.empty()) {
            traverse(0, 0); // Start from root
        }

        // Upload to GPU
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, lineVertices.size() * sizeof(float), 
                     lineVertices.data(), GL_DYNAMIC_DRAW);
        glBindBuffer(GL_ARRAY_BUFFER, 0);

        std::cout << "BVH debug updated: " << lineVertices.size() / 6 << " vertices, " 
                  << (lineVertices.size() / 12) << " lines" << std::endl;
        std::cout << "BVH nodes processed: " << nodes.size() << std::endl;
    }

    void BVHDebugRenderer::render(const glm::mat4& view, const glm::mat4& projection) {
        if (!initialized || !renderEnabled || lineVertices.empty()) return;

        // Save current OpenGL state
        GLboolean depthMask;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMask);
        GLboolean blendEnabled = glIsEnabled(GL_BLEND);
        GLboolean depthTestEnabled = glIsEnabled(GL_DEPTH_TEST);
        
        // Set up rendering based on mode
        switch (renderMode) {
        case ALWAYS_ON_TOP:
            glDisable(GL_DEPTH_TEST);
            break;
        case DEPTH_BIASED:
            glEnable(GL_POLYGON_OFFSET_LINE);
            glPolygonOffset(-1.0f, -1.0f);
            break;
        case DEPTH_TESTED:
        default:
            // Keep depth test enabled
            break;
        }
        
        glDepthMask(GL_FALSE); // Don't write to depth buffer
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glLineWidth(2.0f); // Make lines more visible
        
        // Use shader and set uniforms
        debugShader->use();
        debugShader->setMat4("view", view);
        debugShader->setMat4("projection", projection);
        
        // Draw lines
        glBindVertexArray(VAO);
        glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(lineVertices.size() / 6));
        glBindVertexArray(0);
        
        // Restore OpenGL state
        switch (renderMode) {
        case ALWAYS_ON_TOP:
            if (depthTestEnabled) glEnable(GL_DEPTH_TEST);
            break;
        case DEPTH_BIASED:
            glDisable(GL_POLYGON_OFFSET_LINE);
            break;
        case DEPTH_TESTED:
        default:
            break;
        }
        
        glDepthMask(depthMask);
        if (!blendEnabled) glDisable(GL_BLEND);
        glLineWidth(1.0f); // Reset line width
    }

} // namespace Engine