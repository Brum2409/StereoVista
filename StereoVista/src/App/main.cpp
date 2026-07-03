// StereoVista entry point (Vulkan).
//
// The old OpenGL main.cpp (src/main.cpp, ~9800 lines) is excluded from the
// build and serves as the behaviour reference while its systems are migrated
// phase by phase — see docs/VULKAN_MIGRATION_STATUS.md.

#include "App/Application.h"

#include <exception>
#include <iostream>

int main() {
    try {
        app::Application application;
        application.init();
        application.run();
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "\nFATAL: " << e.what() << std::endl;
        return 1;
    }
}
