#pragma once

#include "Engine/Core.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <GLFW/glfw3.h>

namespace Core {
    /**
     * Utility class for synchronizing 3D cursor positions with Windows cursor positions
     * after camera operations to prevent cursor jumping and maintain spatial consistency.
     */
    class CursorSynchronizer {
    public:
        /**
         * Main synchronization function that projects 3D world position to screen coordinates
         * and repositions the Windows cursor accordingly.
         * 
         * @param window GLFW window handle
         * @param worldCursorPos 3D world position of the cursor
         * @param projection Projection matrix (or left eye projection in stereo)
         * @param view View matrix (or left eye view in stereo)
         * @param windowWidth Window width in pixels
         * @param windowHeight Window height in pixels
         * @param isStereoMode Whether stereo rendering is active
         * @param rightProjection Right eye projection matrix (used only in stereo mode)
         * @param rightView Right eye view matrix (used only in stereo mode)
         * @param viewport Scene sub-rectangle (x, topInset, width, height) in window
         *                 pixels that the projection renders into. When width/height
         *                 are <= 0 the full window is used (origin 0,0). This is what
         *                 the NDC->pixel mapping uses, so the OS cursor lands inside
         *                 the offset viewport instead of the whole window.
         */
        static void synchronizeCursorPosition(
            GLFWwindow* window,
            const glm::vec3& worldCursorPos,
            const glm::mat4& projection,
            const glm::mat4& view,
            int windowWidth,
            int windowHeight,
            bool isStereoMode = false,
            const glm::mat4& rightProjection = glm::mat4(1.0f),
            const glm::mat4& rightView = glm::mat4(1.0f),
            const glm::ivec4& viewport = glm::ivec4(0, 0, 0, 0)
        );

        /**
         * Projects a 3D world position to 2D screen coordinates.
         * 
         * @param worldPos 3D world position to project
         * @param projection Projection matrix
         * @param view View matrix
         * @param windowWidth Window width in pixels
         * @param windowHeight Window height in pixels
         * @return 2D screen coordinates (x, y) in pixels
         */
        static glm::vec2 worldToScreen(
            const glm::vec3& worldPos,
            const glm::mat4& projection,
            const glm::mat4& view,
            const glm::ivec4& viewport
        );

        /**
         * Projects a 3D world position to 2D screen coordinates for stereo rendering.
         * Uses both left and right eye projections to find the optimal cursor position.
         * 
         * @param worldPos 3D world position to project
         * @param leftProjection Left eye projection matrix
         * @param leftView Left eye view matrix
         * @param rightProjection Right eye projection matrix
         * @param rightView Right eye view matrix
         * @param windowWidth Window width in pixels
         * @param windowHeight Window height in pixels
         * @return 2D screen coordinates (x, y) in pixels averaged between eyes
         */
        static glm::vec2 worldToScreenStereo(
            const glm::vec3& worldPos,
            const glm::mat4& leftProjection,
            const glm::mat4& leftView,
            const glm::mat4& rightProjection,
            const glm::mat4& rightView,
            const glm::ivec4& viewport
        );

        /**
         * Checks if screen coordinates are within the viewport boundaries.
         * 
         * @param screenPos 2D screen coordinates to check
         * @param windowWidth Window width in pixels
         * @param windowHeight Window height in pixels
         * @param margin Safety margin from screen edges in pixels
         * @return True if coordinates are within viewport bounds
         */
        static bool isWithinViewport(
            const glm::vec2& screenPos,
            int windowWidth,
            int windowHeight,
            float margin = 50.0f
        );

        /**
         * Validates that projection and view matrices are valid for calculations.
         * 
         * @param projection Projection matrix to validate
         * @param view View matrix to validate
         * @return True if matrices are valid and non-degenerate
         */
        static bool validateMatrices(
            const glm::mat4& projection,
            const glm::mat4& view
        );

        /**
         * Checks if a 3D world position is behind the camera.
         * 
         * @param worldPos 3D world position to check
         * @param view View matrix
         * @return True if position is behind the camera
         */
        static bool isBehindCamera(
            const glm::vec3& worldPos,
            const glm::mat4& view
        );

        /**
         * Diagnostic function to print detailed information about cursor synchronization state.
         * Useful for debugging cursor positioning issues.
         * 
         * @param worldPos 3D world position
         * @param projection Projection matrix
         * @param view View matrix
         * @param windowWidth Window width
         * @param windowHeight Window height
         */
        static void printDiagnostics(
            const glm::vec3& worldPos,
            const glm::mat4& projection,
            const glm::mat4& view,
            int windowWidth,
            int windowHeight
        );

    private:
        // Private constructor - this is a utility class with only static methods
        CursorSynchronizer() = delete;
    };
}