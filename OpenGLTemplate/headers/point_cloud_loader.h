// point_cloud_loader.h
#pragma once

#include <string>
#include "engine/data.h"

namespace Engine {

    class PointCloudLoader {
    public:
        static PointCloud loadPointCloudFile(const std::string& filePath, size_t downsampleFactor = 1);

    private:
        static void setupPointCloudGLBuffers(PointCloud& pointCloud);
    };

} // namespace Engine