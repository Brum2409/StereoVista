#pragma once
// Engine data types shared by the loaders, scene management and (from Phase 5
// on) the point-cloud renderer. GPU-API-free since the Vulkan migration: GPU
// residency lives behind renderer::PointCloudGpu (RHI buffers), so everything
// here is plain CPU state and the types are default-movable — the GL era's
// hand-rolled move constructors existed only to zero raw GL handles.

#include <glm/glm.hpp>

#include <array>
#include <cfloat>
#include <chrono>
#include <cstdint>
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace renderer {
struct PointCloudGpu;
}

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
    // packed-coordinate streams.  The bounding box (local space) lets the
    // shader cull the whole batch with a single frustum test and decode
    // quantised coordinates back to float.
    //
    // Layout must match the GLSL Batch struct consumed by the Phase 5
    // rasterizer (tightly packed scalars: 8 x 4 = 32 bytes, identical under
    // scalar and std430 rules).
    struct ComputeBatch {
        float min_x, min_y, min_z;
        float max_x, max_y, max_z;
        int   numPoints;
        int   firstPoint;
    };
    static_assert(sizeof(ComputeBatch) == 32,
                  "ComputeBatch size mismatch – GLSL struct alignment will break");

    // Octree-based point cloud node (CPU/disk-cache structure; the GL-era
    // GL_POINTS LOD fallback and its per-node VBOs are gone — the compute
    // rasterizer path is guaranteed on the Vulkan 1.3 feature set).
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
            isLoaded(false), memoryUsage(0), isLeaf(true) {}
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

    // Background streaming state for a progressively-loaded cloud (defined in
    // PointCloudLoader.cpp).  Held by shared_ptr so PointCloud stays movable and
    // the worker thread is joined when the last owner is destroyed.
    struct PointCloudStream;

    struct PointCloud {
        std::string name;
        std::string filePath;
        std::string sourceScenePath = ""; // Path to the scene file this object was loaded from (empty = manually created)
        std::vector<PointCloudPoint> points; // Raw points for initial loading (cleared after GPU build)
        glm::vec3 position{ 0.0f };
        glm::vec3 rotation{ 0.0f };
        glm::vec3 scale{ 1.0f };
        bool visible = true;

        // ── GPU residency (compute rasterizer streams + batch descriptors) ───
        // One suballocated device-local buffer behind an RHI type; null until a
        // loader builds it. shared_ptr keeps PointCloud cheaply movable and
        // lets in-flight frames extend the lifetime later (Phase 5).
        // DECLARED BEFORE `stream`: members destroy in reverse order, so the
        // stream's worker thread is joined before the GPU buffers die.
        std::shared_ptr<renderer::PointCloudGpu> gpu;

        // Total points uploaded so far (grows while streaming).
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
        uint32_t numBatches           = 0;
        int      computePointsPerThread = 0; // ceil(kComputeBatchSize / 128)

        // ── Progressive streaming (compute_loop_las style) ───────────────────
        // While `stream` is non-null the cloud is still loading: the GPU buffer
        // is pre-allocated for streamTargetCount points and numBatches/
        // totalPointCount grow each frame as PointCloudLoader::updateStreaming()
        // stages chunks through the upload ring.  `stream` resets to null when
        // loading completes.
        std::shared_ptr<PointCloudStream> stream;
        uint32_t streamTargetCount = 0; // expected final point count (0 = N/A)

        float basePointSize = 2.0f;

        // Octree-based system (legacy; kept for the disk-cache logic)
        std::unique_ptr<PointCloudOctreeNode> octreeRoot;
        glm::vec3 octreeBoundsMin{ 0.0f };
        glm::vec3 octreeBoundsMax{ 0.0f };
        glm::vec3 octreeCenter{ 0.0f };
        float octreeSize = 0.0f;
        int maxOctreeDepth = 12;
        size_t maxPointsPerNode = 5000;

        // Memory and disk management
        PointCloudChunkCache chunkCache;
        bool useOctree = true;
        bool useDiskCache = true;
        size_t totalLoadedNodes = 0;

        // Octree visualization (CPU outline vertices; drawing returns with the
        // Phase 6 overlay renderer)
        std::vector<glm::vec3> chunkOutlineVertices;
        bool visualizeOctree = false;
        int visualizeDepth = 3;

        PointCloud() {
            chunkCache.cacheDirectory = "pointcloud_cache";
        }

        // Every member is RAII — default moves are correct (the moved-from
        // object's gpu/stream/points are null/empty, so it destroys cleanly).
        PointCloud(PointCloud&&) = default;
        PointCloud& operator=(PointCloud&&) = default;
        PointCloud(const PointCloud&) = delete;
        PointCloud& operator=(const PointCloud&) = delete;

        // Returns true if point cloud data is loaded and ready to render.
        // A streaming cloud counts as loaded as soon as its background load has
        // started (stream != null), so it is kept in the scene and rendered as
        // its batches fill in.
        bool isLoaded() const {
            return numBatches > 0 || !points.empty() || stream != nullptr;
        }

        // True while the cloud is still streaming in from disk.
        bool isStreaming() const { return stream != nullptr; }

        // Returns true if the bounds fields are valid (set by the loader).
        bool hasBounds() const {
            return boundsMin.x != FLT_MAX;
        }
    };



    // A single measurement annotation created with the measurement tool.
    // Points are stored in world space; measurements are part of the scene and
    // serialized to/from scene files by SceneManager.
    struct Measurement {
        enum class Type {
            Distance = 0, // polyline: per-segment + total length
            Angle    = 1, // exactly 3 points: angle at the middle point
            Point    = 2, // single point: world-coordinate annotation
            Area     = 3  // closed polygon (>= 3 points): planar surface area
        };

        Type type = Type::Distance;
        std::string name;
        std::vector<glm::vec3> points;
        glm::vec3 color = glm::vec3(1.0f, 0.76f, 0.03f);
        bool visible = true;

        float totalLength() const {
            float len = 0.0f;
            for (size_t i = 1; i < points.size(); i++)
                len += glm::length(points[i] - points[i - 1]);
            return len;
        }

        // Angle (degrees) at the middle point of a 3-point measurement.
        float angleDegrees() const {
            if (points.size() < 3) return 0.0f;
            const glm::vec3 a = points[0] - points[1];
            const glm::vec3 b = points[2] - points[1];
            const float la = glm::length(a), lb = glm::length(b);
            if (la < 1e-6f || lb < 1e-6f) return 0.0f;
            const float c = glm::clamp(glm::dot(a, b) / (la * lb), -1.0f, 1.0f);
            return glm::degrees(glm::acos(c));
        }

        // Planar surface area of an Area polygon, treating the points as a
        // closed loop (the last vertex connects back to the first). Uses
        // Newell's method, so it returns the true area for any planar polygon
        // regardless of orientation and degrades gracefully (projected area)
        // for slightly non-planar input. Returns 0 for fewer than 3 points.
        float area() const {
            const size_t count = points.size();
            if (count < 3) return 0.0f;
            glm::vec3 n(0.0f);
            for (size_t i = 0; i < count; i++) {
                const glm::vec3& cur = points[i];
                const glm::vec3& nxt = points[(i + 1) % count];
                n.x += (cur.y - nxt.y) * (cur.z + nxt.z);
                n.y += (cur.z - nxt.z) * (cur.x + nxt.x);
                n.z += (cur.x - nxt.x) * (cur.y + nxt.y);
            }
            return glm::length(n) * 0.5f;
        }

        // Perimeter of the closed Area polygon (total length plus the closing
        // segment back to the first vertex). Returns 0 for fewer than 3 points.
        float perimeter() const {
            const size_t count = points.size();
            if (count < 3) return 0.0f;
            float len = totalLength();
            len += glm::length(points.front() - points.back());
            return len;
        }

        // Average of the vertices, used to place the area label at the polygon
        // centre. Returns the origin for an empty point set.
        glm::vec3 centroid() const {
            if (points.empty()) return glm::vec3(0.0f);
            glm::vec3 c(0.0f);
            for (const auto& p : points) c += p;
            return c / static_cast<float>(points.size());
        }
    };

    // Maximum number of simultaneous user section/clip planes. The GL scene
    // vertex shaders wrote these to gl_ClipDistance[1..MAX_CLIP_PLANES]; the
    // Vulkan pipelines re-introduce them in Phase 5/6 (point clouds first).
    static constexpr int MAX_CLIP_PLANES = 6;

    // A user-controllable section / clipping plane. Geometry on the negative
    // side of the plane (dot(normal, p - position) < 0) is hidden in the main
    // lit pass; the "kept" side is the one the normal points to. Planes are part
    // of the scene and serialized to/from scene files by SceneManager, exactly
    // like Measurement.
    struct ClipPlane {
        glm::vec3   position = glm::vec3(0.0f);            // a point on the plane
        glm::vec3   normal   = glm::vec3(0.0f, 1.0f, 0.0f); // unit normal (kept side)
        bool        enabled  = true;
        std::string name;
        glm::vec3   color    = glm::vec3(0.25f, 0.60f, 1.0f); // overlay tint

        // Unit normal, guarded against a degenerate (zero-length) normal.
        glm::vec3 unitNormal() const {
            const float len = glm::length(normal);
            return (len > 1e-6f) ? (normal / len) : glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // Plane packed for the shader / point-cloud test: (normal.xyz, d) with
        // d = -dot(n, position). A point p is kept while dot(n, p) + d >= 0.
        glm::vec4 packed() const {
            const glm::vec3 n = unitNormal();
            return glm::vec4(n, -glm::dot(n, position));
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
