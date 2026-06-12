// point_cloud_loader.h
#pragma once
#include "../Engine/Data.h"
#include <functional>
#include <sstream>

namespace Engine {

    class PointCloudLoader {
    public:
        static PointCloud loadPointCloudFile(const std::string& filePath, size_t downsampleFactor = 1);
        // Exporters stream the cloud back from the GPU compute SSBOs (the CPU
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
        static PointCloud loadFromBinary(const std::string& filePath);
        static PointCloud loadFromHDF5(const std::string& filePath, size_t downsampleFactor = 1);
        static PointCloud loadFromLAS(const std::string& filePath, size_t downsampleFactor = 1,
                                      const glm::dvec3* globalCenter = nullptr);
        static std::vector<PointCloud> loadFromLASMultiple(const std::vector<std::string>& filePaths,
                                                           size_t downsampleFactor = 1);

        // Streams every point of the cloud to the callback, one decoded batch
        // at a time (peak CPU RAM = one batch). Sources the legacy CPU vector
        // when populated, otherwise reads the quantised compute SSBOs back
        // from the GPU. Returns false if the cloud holds no point data.
        // Must be called on the thread that owns the GL context.
        static bool forEachPointBatch(const PointCloud& pointCloud,
            const std::function<void(const PointCloudPoint* pts, size_t count)>& callback);

    private:
        static void setupPointCloudGLBuffers(PointCloud& pointCloud);
        static constexpr char BINARY_MAGIC_NUMBER[4] = { 'P', 'C', 'B', '1' };
        static std::string vec3_to_string(const glm::vec3& vec) {
            std::stringstream ss;
            ss << "(" << vec.x << ", " << vec.y << ", " << vec.z << ")";
            return ss.str();
        }
    };

}
