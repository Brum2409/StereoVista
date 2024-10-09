#pragma once

#include <string>
#include <vector>
#include "obj_loader.h"  // Include this for the PointCloud struct

namespace Engine {

    class LAZConverter {
    public:
        static bool convertLAZToSimpleFormat(const std::string& inputFile, const std::string& outputFile);
        static PointCloud loadLAZDirectly(const std::string& inputFile);
    };

}  // namespace Engine