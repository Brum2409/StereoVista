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

    // GPU-side batch descriptor for the Schütz compute rasterizer.
    // Each batch covers up to kComputeBatchSize contiguous points in the flat
    // packed-coordinate SSBOs.  The bounding box (local space) lets the shader
    // cull the whole batch with a single frustum test and decode quantised
    // coordinates back to float.
    //
    // Layout must match the GLSL Batch struct in pointcloud_rasterize.comp
    // (std430, 8 × 4 = 32 bytes, no padding required).
    struct ComputeBatch {
        float min_x, min_y, min_z;
        float max_x, max_y, max_z;
        int   numPoints;
        int   firstPoint;
    };
    static_assert(sizeof(ComputeBatch) == 32,
                  "ComputeBatch size mismatch – GLSL struct alignment will break");

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

        // ── World-space bounding box (set by streaming loaders) ───────────────
        // Used for frame-all, SpaceMouse navigation, and frustum optimisation.
        // Stored in local object space (before position/scale transform).
        glm::vec3 boundsMin = glm::vec3( FLT_MAX);
        glm::vec3 boundsMax = glm::vec3(-FLT_MAX);

        // ── Schütz batch system (compute rasterizer) ─────────────────────────
        // Points are partitioned into batches of kComputeBatchSize.  Per-batch
        // bounding boxes enable a single frustum-cull test per workgroup, and
        // coordinates are quantised to 10/20/30-bit integers to cut bandwidth.
        static constexpr int kComputeBatchSize = 10240; // multiple of 128
        GLuint computeBatchSSBO  = 0; // binding 40 – ComputeBatch descriptors
        GLuint computeXyz12bSSBO = 0; // binding 41 – finest 10 bits per axis
        GLuint computeXyz8bSSBO  = 0; // binding 42 – middle 10 bits per axis
        GLuint computeXyz4bSSBO  = 0; // binding 43 – coarsest 10 bits per axis
        GLuint computeRGBASSBO   = 0; // binding 44 – pre-packed uint RGBA
        uint32_t numBatches           = 0;
        int      computePointsPerThread = 0; // ceil(kComputeBatchSize / 128)

        float basePointSize = 2.0f;

        // Octree-based system (legacy; kept for binary compatibility)
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
              boundsMin(other.boundsMin), boundsMax(other.boundsMax),
              basePointSize(other.basePointSize), octreeRoot(std::move(other.octreeRoot)),
              octreeBoundsMin(other.octreeBoundsMin), octreeBoundsMax(other.octreeBoundsMax),
              octreeCenter(other.octreeCenter), octreeSize(other.octreeSize),
              maxOctreeDepth(other.maxOctreeDepth), maxPointsPerNode(other.maxPointsPerNode),
              lodMultiplier(other.lodMultiplier), chunkCache(std::move(other.chunkCache)),
              useOctree(other.useOctree), useDiskCache(other.useDiskCache),
              totalLoadedNodes(other.totalLoadedNodes), chunkOutlineVAO(other.chunkOutlineVAO),
              chunkOutlineVBO(other.chunkOutlineVBO), chunkOutlineVertices(std::move(other.chunkOutlineVertices)),
              visualizeOctree(other.visualizeOctree), visualizeDepth(other.visualizeDepth),
              computeBatchSSBO(other.computeBatchSSBO),
              computeXyz12bSSBO(other.computeXyz12bSSBO),
              computeXyz8bSSBO(other.computeXyz8bSSBO),
              computeXyz4bSSBO(other.computeXyz4bSSBO),
              computeRGBASSBO(other.computeRGBASSBO),
              numBatches(other.numBatches),
              computePointsPerThread(other.computePointsPerThread) {

            // Copy lodDistances array
            for (int i = 0; i < 5; i++) {
                lodDistances[i] = other.lodDistances[i];
            }

            // Reset other object
            other.vao = 0;
            other.vbo = 0;
            other.totalPointCount = 0;
            other.boundsMin = glm::vec3( FLT_MAX);
            other.boundsMax = glm::vec3(-FLT_MAX);
            other.chunkOutlineVAO = 0;
            other.chunkOutlineVBO = 0;
            other.computeBatchSSBO   = 0;
            other.computeXyz12bSSBO  = 0;
            other.computeXyz8bSSBO   = 0;
            other.computeXyz4bSSBO   = 0;
            other.computeRGBASSBO    = 0;
            other.numBatches         = 0;
            other.computePointsPerThread = 0;
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
                boundsMin = other.boundsMin;
                boundsMax = other.boundsMax;
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

                computeBatchSSBO        = other.computeBatchSSBO;
                computeXyz12bSSBO       = other.computeXyz12bSSBO;
                computeXyz8bSSBO        = other.computeXyz8bSSBO;
                computeXyz4bSSBO        = other.computeXyz4bSSBO;
                computeRGBASSBO         = other.computeRGBASSBO;
                numBatches              = other.numBatches;
                computePointsPerThread  = other.computePointsPerThread;

                // Reset other object
                other.vao = 0;
                other.vbo = 0;
                other.totalPointCount = 0;
                other.boundsMin = glm::vec3( FLT_MAX);
                other.boundsMax = glm::vec3(-FLT_MAX);
                other.chunkOutlineVAO = 0;
                other.chunkOutlineVBO = 0;
                other.computeBatchSSBO   = 0;
                other.computeXyz12bSSBO  = 0;
                other.computeXyz8bSSBO   = 0;
                other.computeXyz4bSSBO   = 0;
                other.computeRGBASSBO    = 0;
                other.numBatches         = 0;
                other.computePointsPerThread = 0;
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

        // Returns true if point cloud data is loaded and ready to render.
        bool isLoaded() const {
            return numBatches > 0 || !points.empty();
        }

        // Returns true if the bounds fields are valid (set by the loader).
        bool hasBounds() const {
            return boundsMin.x != FLT_MAX;
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
