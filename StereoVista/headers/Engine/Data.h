#pragma once
#include "Core.h"
#include <memory>
#include <array>
#include <unordered_map>
#include <unordered_set>
#include <list>
#include <chrono>
#include <filesystem>
#include <numeric>
#include <mutex>
#include <thread>
#include <future>
#include <atomic>

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

    // Octree-based point cloud node
    struct PointCloudOctreeNode {
        // Node identification
        uint64_t nodeId;
        int depth;
        glm::vec3 center;
        glm::vec3 bounds; // half-size of the node

        // Point storage - either in memory or on disk
        std::vector<PointCloudPoint> points; // In-memory points (for active nodes)
        size_t totalPointCount;

        // Disk storage information
        bool isOnDisk;
        std::string diskFilePath;
        size_t diskFileOffset;

        // LOD information (used by GL_POINTS fallback path only)
        std::vector<size_t> lodPointCounts;
        std::vector<GLuint> lodVBOs;
        bool vbosGenerated;

        // Memory management
        bool isLoaded;
        std::chrono::steady_clock::time_point lastAccessed;
        size_t memoryUsage; // Bytes used by this node

        // Octree structure
        bool isLeaf;
        std::array<std::unique_ptr<PointCloudOctreeNode>, 8> children;

        PointCloudOctreeNode() :
            nodeId(0), depth(0), center(0.0f), bounds(0.0f),
            totalPointCount(0), isOnDisk(false), diskFileOffset(0),
            vbosGenerated(false), isLoaded(false), memoryUsage(0), isLeaf(true) {
            lodPointCounts.resize(5);
            lodVBOs.resize(5, 0);
        }

        ~PointCloudOctreeNode() {
            cleanup();
        }

        void cleanup() {
            for (GLuint vbo : lodVBOs) {
                if (vbo != 0) {
                    glDeleteBuffers(1, &vbo);
                }
            }
            std::fill(lodVBOs.begin(), lodVBOs.end(), 0);
            vbosGenerated = false;
        }
    };

    // Memory and disk cache management for the octree
    struct PointCloudChunkCache {
        size_t maxMemoryMB;
        size_t currentMemoryMB;
        std::string cacheDirectory;
        std::unordered_map<uint64_t, std::weak_ptr<PointCloudOctreeNode>> nodeCache;
        std::list<uint64_t> accessOrder; // LRU tracking

        PointCloudChunkCache() : maxMemoryMB(8192), currentMemoryMB(0) {} // Default 8GB limit
    };

    struct PointCloud {
        std::string name;
        std::string filePath;
        std::string sourceScenePath = ""; // Path to the scene file this object was loaded from (empty = manually created)
        std::vector<PointCloudPoint> points; // Raw points for initial loading (cleared after octree build)
        glm::vec3 position;
        glm::vec3 rotation;
        glm::vec3 scale;
        bool visible = true;
        GLuint vao = 0;
        GLuint vbo = 0;
        // Total points uploaded to vbo. Set by setupPointCloudGLBuffers() before
        // buildOctree() clears the cpu-side points vector.
        uint32_t totalPointCount = 0;

        float basePointSize = 2.0f;

        // Octree-based system
        std::unique_ptr<PointCloudOctreeNode> octreeRoot;
        glm::vec3 octreeBoundsMin;
        glm::vec3 octreeBoundsMax;
        glm::vec3 octreeCenter;
        float octreeSize;
        int maxOctreeDepth = 12;
        size_t maxPointsPerNode = 5000;

        // LOD distances (used by GL_POINTS fallback path only)
        float lodDistances[5] = { 10.0f, 25.0f, 50.0f, 100.0f, 200.0f };
        float lodMultiplier = 1.0f;

        // Memory and disk management
        PointCloudChunkCache chunkCache;
        bool useOctree = true;
        bool useDiskCache = true;
        size_t totalLoadedNodes = 0;

        // Octree visualization
        GLuint chunkOutlineVAO = 0;
        GLuint chunkOutlineVBO = 0;
        std::vector<glm::vec3> chunkOutlineVertices;
        bool visualizeOctree = false;
        int visualizeDepth = 3;

        PointCloud() {
            chunkCache.cacheDirectory = "pointcloud_cache";
        }

        // Move constructor
        PointCloud(PointCloud&& other) noexcept
            : name(std::move(other.name)), filePath(std::move(other.filePath)),
              points(std::move(other.points)), position(other.position),
              rotation(other.rotation), scale(other.scale), visible(other.visible),
              vao(other.vao), vbo(other.vbo), totalPointCount(other.totalPointCount),
              basePointSize(other.basePointSize), octreeRoot(std::move(other.octreeRoot)),
              octreeBoundsMin(other.octreeBoundsMin), octreeBoundsMax(other.octreeBoundsMax),
              octreeCenter(other.octreeCenter), octreeSize(other.octreeSize),
              maxOctreeDepth(other.maxOctreeDepth), maxPointsPerNode(other.maxPointsPerNode),
              lodMultiplier(other.lodMultiplier), chunkCache(std::move(other.chunkCache)),
              useOctree(other.useOctree), useDiskCache(other.useDiskCache),
              totalLoadedNodes(other.totalLoadedNodes), chunkOutlineVAO(other.chunkOutlineVAO),
              chunkOutlineVBO(other.chunkOutlineVBO), chunkOutlineVertices(std::move(other.chunkOutlineVertices)),
              visualizeOctree(other.visualizeOctree), visualizeDepth(other.visualizeDepth) {

            // Copy lodDistances array
            for (int i = 0; i < 5; i++) {
                lodDistances[i] = other.lodDistances[i];
            }

            // Reset other object
            other.vao = 0;
            other.vbo = 0;
            other.totalPointCount = 0;
            other.chunkOutlineVAO = 0;
            other.chunkOutlineVBO = 0;
        }

        // Move assignment operator
        PointCloud& operator=(PointCloud&& other) noexcept {
            if (this != &other) {
                cleanup();

                name = std::move(other.name);
                filePath = std::move(other.filePath);
                points = std::move(other.points);
                position = other.position;
                rotation = other.rotation;
                scale = other.scale;
                visible = other.visible;
                vao = other.vao;
                vbo = other.vbo;
                totalPointCount = other.totalPointCount;
                basePointSize = other.basePointSize;

                octreeRoot = std::move(other.octreeRoot);
                octreeBoundsMin = other.octreeBoundsMin;
                octreeBoundsMax = other.octreeBoundsMax;
                octreeCenter = other.octreeCenter;
                octreeSize = other.octreeSize;
                maxOctreeDepth = other.maxOctreeDepth;
                maxPointsPerNode = other.maxPointsPerNode;

                for (int i = 0; i < 5; i++) {
                    lodDistances[i] = other.lodDistances[i];
                }
                lodMultiplier = other.lodMultiplier;

                chunkCache = std::move(other.chunkCache);
                useOctree = other.useOctree;
                useDiskCache = other.useDiskCache;
                totalLoadedNodes = other.totalLoadedNodes;

                chunkOutlineVAO = other.chunkOutlineVAO;
                chunkOutlineVBO = other.chunkOutlineVBO;
                chunkOutlineVertices = std::move(other.chunkOutlineVertices);
                visualizeOctree = other.visualizeOctree;
                visualizeDepth = other.visualizeDepth;

                // Reset other object
                other.vao = 0;
                other.vbo = 0;
                other.totalPointCount = 0;
                other.chunkOutlineVAO = 0;
                other.chunkOutlineVBO = 0;
            }
            return *this;
        }

        // Disable copy constructor and copy assignment
        PointCloud(const PointCloud&) = delete;
        PointCloud& operator=(const PointCloud&) = delete;

        ~PointCloud() {
            cleanup();
        }

        void cleanup() {
            if (octreeRoot) {
                octreeRoot.reset();
            }
        }
    };



    struct Sun {
        glm::vec3 direction;
        glm::vec3 color;
        float intensity;
        bool enabled;
    };

    const int MAX_LIGHTS = 16;
    struct PointLight {
        std::string sourceScenePath = ""; // Path to the scene file this object was loaded from (empty = manually created)
        glm::vec3 position;
        glm::vec3 color;
        float intensity;
        float linear = 0.09f;        // Linear attenuation coefficient
        float quadratic = 0.032f;    // Quadratic attenuation coefficient
        glm::mat4 lightSpaceMatrix;
        bool castShadows = true;
    };

    struct SpotLight {
        std::string sourceScenePath = ""; // Path to the scene file this object was loaded from (empty = manually created)
        glm::vec3 position;
        glm::vec3 direction;
        glm::vec3 color;
        float intensity;
        float innerCutOff;  // Inner cone angle (cosine)
        float outerCutOff;  // Outer cone angle (cosine)
        glm::mat4 lightSpaceMatrix;
        bool castShadows = true;
    };

}
