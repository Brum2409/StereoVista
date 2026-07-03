// point_cloud_loader.h
#pragma once
#include "../Engine/Data.h"
#include <functional>
#include <sstream>

namespace rhi {
class Device;
class UploadRing;
}

namespace Engine {

    class PointCloudLoader {
    public:
        // Provide the GPU context the loaders upload through. Call once at
        // startup after the renderer exists (device = allocations + blocking
        // loading-time uploads; streamRing = the renderer's per-frame upload
        // ring that updateStreaming stages into). Replaces the GL era's
        // implicit global context + initGLExtensions.
        static void initGpu(rhi::Device* device, rhi::UploadRing* streamRing);

        static PointCloud loadPointCloudFile(const std::string& filePath, size_t downsampleFactor = 1);
        // Exporters stream the cloud back from the GPU buffers (the CPU
        // points vector is cleared after upload), so they work for any loaded
        // cloud. applyTransform bakes position/rotation/scale into the written
        // coordinates; scene saving passes false to keep points in local space
        // (the transform is stored separately in the scene JSON).
        static bool exportToXYZ(const PointCloud& pointCloud, const std::string& filePath,
                                bool applyTransform = true);
        static bool exportToBinary(const PointCloud& pointCloud, const std::string& filePath,
                                   bool applyTransform = true);
        static bool exportToHDF5(const PointCloud& pointCloud, const std::string& filePath,
                                 bool applyTransform = true);
        // Export to the Stanford PLY format (the standard point-cloud interchange
        // format this app already imports). binary=true writes a compact
        // binary_little_endian body (x/y/z float, r/g/b uchar, intensity float);
        // binary=false writes a human-readable ascii body with the same fields.
        static bool exportToPLY(const PointCloud& pointCloud, const std::string& filePath,
                                bool applyTransform = true, bool binary = true);
        static PointCloud loadFromBinary(const std::string& filePath);
        // Header-driven reader for binary PLY (binary_little_endian /
        // binary_big_endian). Streams the fixed-stride vertex records straight
        // into the compute buffers, like the text loader. ASCII PLY is still
        // handled by the generic text parser in loadPointCloudFile.
        static PointCloud loadFromBinaryPLY(const std::string& filePath, size_t downsampleFactor = 1);
        static PointCloud loadFromHDF5(const std::string& filePath, size_t downsampleFactor = 1);
        static PointCloud loadFromLAS(const std::string& filePath, size_t downsampleFactor = 1,
                                      const glm::dvec3* globalCenter = nullptr);
        static std::vector<PointCloud> loadFromLASMultiple(const std::vector<std::string>& filePaths,
                                                           size_t downsampleFactor = 1);

        // ── Progressive (streaming) LAS/LAZ loading (Schütz compute_loop_las) ──
        // Returns immediately: the GPU buffer is pre-allocated from the LAS
        // header's exact point count, header bounds are set so the cloud can be
        // framed at once, and a background thread streams the points in.  The
        // cloud renders and fills in progressively as updateStreaming() is
        // pumped each frame on the main thread.  beginLoadLASMultipleProgressive
        // shares a global centre across tiles and loads all files concurrently.
        // mortonResort=true (default) = two-phase: instant file-order display,
        // then a background global Morton sort hot-swapped in for full render
        // speed.  false = pure file-order (literal Schütz) with bounded RAM.
        static PointCloud beginLoadLASProgressive(const std::string& filePath,
                                                  size_t downsampleFactor = 1,
                                                  const glm::dvec3* globalCenter = nullptr,
                                                  bool mortonResort = true);
        static std::vector<PointCloud> beginLoadLASMultipleProgressive(
            const std::vector<std::string>& filePaths, size_t downsampleFactor = 1,
            bool mortonResort = true);

        // Pump pending GPU uploads for a streaming cloud.  Call once per frame
        // per cloud on the main/render thread, before Renderer::renderFrame
        // (the staged copies ride that frame's command buffer through the
        // upload ring).  No-op when the cloud is not (or no longer) streaming.
        static void updateStreaming(PointCloud& pointCloud);

        // Live progress for a streaming cloud (for UI). active == false when the
        // cloud is fully loaded (no longer streaming).
        struct StreamProgress {
            bool     active          = false; // still streaming?
            bool     resorting       = false; // phase 2 (background Morton sort) running
            uint32_t pointsLoaded    = 0;     // points uploaded so far
            uint32_t pointsTotal     = 0;     // expected final point count
            float    fraction        = 0.0f;  // phase-1 load fraction [0,1]
            double   elapsedSeconds  = 0.0;
            double   pointsPerSecond = 0.0;   // average load rate
        };
        static StreamProgress getStreamProgress(const PointCloud& pointCloud);

        // Streams every point of the cloud to the callback, one decoded batch
        // at a time (peak CPU RAM = one readback span). Sources the legacy CPU
        // vector when populated, otherwise reads the quantised GPU buffers
        // back. Returns false if the cloud holds no point data.
        // Must be called on the main/render thread (blocking GPU readback).
        static bool forEachPointBatch(const PointCloud& pointCloud,
            const std::function<void(const PointCloudPoint* pts, size_t count)>& callback);

    private:
        static void setupPointCloudGpuBuffers(PointCloud& pointCloud);
        static constexpr char BINARY_MAGIC_NUMBER[4] = { 'P', 'C', 'B', '1' };
        static std::string vec3_to_string(const glm::vec3& vec) {
            std::stringstream ss;
            ss << "(" << vec.x << ", " << vec.y << ", " << vec.z << ")";
            return ss.str();
        }
    };

}
