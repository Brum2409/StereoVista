#pragma once

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <algorithm>
#include <glm/glm.hpp>
#include <vector>
#include <array>
#include <cfloat>

namespace Engine {

    // Axis-aligned bounding box
    struct AABB {
        glm::vec3 minBounds;
        glm::vec3 maxBounds;
        
        AABB() : minBounds(FLT_MAX), maxBounds(-FLT_MAX) {}
        
        AABB(const glm::vec3& min, const glm::vec3& max) 
            : minBounds(min), maxBounds(max) {}
        
        void expand(const glm::vec3& point) {
            for (int i = 0; i < 3; i++) {
                if (point[i] < minBounds[i]) minBounds[i] = point[i];
                if (point[i] > maxBounds[i]) maxBounds[i] = point[i];
            }
        }
        
        void expand(const AABB& other) {
            for (int i = 0; i < 3; i++) {
                if (other.minBounds[i] < minBounds[i]) minBounds[i] = other.minBounds[i];
                if (other.maxBounds[i] > maxBounds[i]) maxBounds[i] = other.maxBounds[i];
            }
        }
        
        glm::vec3 getCenter() const {
            return (minBounds + maxBounds) * 0.5f;
        }
        
        glm::vec3 getSize() const {
            return maxBounds - minBounds;
        }
        
        float getSurfaceArea() const {
            glm::vec3 size = getSize();
            if (size.x < 0.0f || size.y < 0.0f || size.z < 0.0f) return 0.0f; // Handle degenerate case
            return 2.0f * (size.x * size.y + size.y * size.z + size.z * size.x);
        }
        
        bool isValid() const {
            return minBounds.x <= maxBounds.x && minBounds.y <= maxBounds.y && minBounds.z <= maxBounds.z;
        }
        
        int getLongestAxis() const {
            glm::vec3 size = getSize();
            if (size.x > size.y && size.x > size.z) return 0;
            if (size.y > size.z) return 1;
            return 2;
        }
    };

    // Triangle with additional data for BVH construction
    struct BVHTriangle {
        glm::vec3 v0, v1, v2;       // Triangle vertices
        glm::vec3 normal;           // Triangle normal
        glm::vec3 color;            // Material color
        float emissiveness;         // Material emissiveness
        float shininess;            // Material shininess
        int materialId;             // Material identifier
        
        // Additional data for BVH construction
        glm::vec3 centroid;         // Triangle centroid for partitioning
        AABB bounds;                // Triangle bounding box
        
        BVHTriangle() = default;
        
        BVHTriangle(const glm::vec3& vertex0, const glm::vec3& vertex1, const glm::vec3& vertex2,
                   const glm::vec3& norm, const glm::vec3& col, float emiss, float shin, int matId)
            : v0(vertex0), v1(vertex1), v2(vertex2), normal(norm), color(col), 
              emissiveness(emiss), shininess(shin), materialId(matId) {
            
            // Calculate centroid
            centroid = (v0 + v1 + v2) / 3.0f;
            
            // Calculate bounds
            bounds.expand(v0);
            bounds.expand(v1);
            bounds.expand(v2);
        }
    };

    // BVH Node structure - optimized for GPU usage
    // Total size: 32 bytes (cache-friendly)
    struct BVHNode {
        glm::vec3 minBounds;     // 12 bytes - AABB min
        uint32_t leftFirst;      // 4 bytes - left child index OR first triangle index
        glm::vec3 maxBounds;     // 12 bytes - AABB max  
        uint32_t triCount;       // 4 bytes - triangle count (0 for interior nodes)
        
        BVHNode() : minBounds(FLT_MAX), maxBounds(-FLT_MAX), leftFirst(0), triCount(0) {}
        
        bool isLeaf() const { return triCount > 0; }
        
        void setBounds(const AABB& aabb) {
            minBounds = aabb.minBounds;
            maxBounds = aabb.maxBounds;
        }
        
        AABB getBounds() const {
            return AABB(minBounds, maxBounds);
        }
    };

    class BVHBuilder {
    private:
        std::vector<BVHTriangle> triangles;
        std::vector<BVHNode> nodes;
        std::vector<uint32_t> triangleIndices;
        uint32_t rootNodeIdx;
        uint32_t nodesUsed;
        
        // SAH (Surface Area Heuristic) parameters
        static constexpr float TRAVERSAL_COST = 1.25f; // Slightly higher than intersection
        static constexpr float INTERSECTION_COST = 1.0f;
        static constexpr uint32_t MAX_TRIANGLES_PER_LEAF = 4; // Allow slightly more triangles per leaf
        static constexpr uint32_t SAH_BINS = 16; // More bins for better split quality

    public:
        BVHBuilder() : rootNodeIdx(0), nodesUsed(1) {}
        
        // Build BVH from triangle data
        void build(const std::vector<BVHTriangle>& inputTriangles);
        
        // Get the constructed BVH data for GPU upload
        const std::vector<BVHNode>& getNodes() const { return nodes; }
        const std::vector<uint32_t>& getTriangleIndices() const { return triangleIndices; }
        const std::vector<BVHTriangle>& getTriangles() const { return triangles; }
        uint32_t getRootNodeIndex() const { return rootNodeIdx; }
        
    private:
        // Recursive BVH construction
        void subdivide(uint32_t nodeIdx, uint32_t depth = 0);
        
        // Calculate node bounds from triangle indices
        AABB calculateBounds(uint32_t first, uint32_t count);
        
        // Find best split using SAH
        struct SplitResult {
            int axis;
            float position;
            float cost;
            uint32_t leftCount;
        };
        
        SplitResult findBestSplit(uint32_t first, uint32_t count, const AABB& nodeBounds);
        
        // Evaluate SAH cost for a potential split
        float evaluateSAH(uint32_t leftCount, uint32_t rightCount, 
                         const AABB& leftBounds, const AABB& rightBounds, 
                         const AABB& nodeBounds);
        
        // Partition triangles based on split
        uint32_t partition(uint32_t first, uint32_t count, int axis, float splitPos);
    };

    // GPU-friendly data structures for SSBO upload
    struct GPUBVHNode {
        float minX, minY, minZ;     // 12 bytes
        uint32_t leftFirst;         // 4 bytes
        float maxX, maxY, maxZ;     // 12 bytes
        uint32_t triCount;          // 4 bytes
        // Total: 32 bytes
    };

    struct GPUTriangle {
        float v0[4];    // vec3 + padding
        float v1[4];    // vec3 + padding  
        float v2[4];    // vec3 + padding
        float normal[4]; // vec3 + padding
        float color[4];  // vec3 + emissiveness
        float shininess; // float
        uint32_t materialId; // int (as uint32_t)
        float padding[2]; // align to 64 bytes
        // Total: 64 bytes (matches current Triangle struct)
    };

} // namespace Engine