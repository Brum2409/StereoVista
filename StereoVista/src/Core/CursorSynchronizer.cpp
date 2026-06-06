#include "Core/CursorSynchronizer.h"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>

// Debug logging control
#define CURSOR_SYNC_DEBUG 0
#define CURSOR_SYNC_PERFORMANCE 0

#if CURSOR_SYNC_DEBUG
#define CURSOR_DEBUG_LOG(msg) std::cout << "[CursorSync] " << msg << std::endl
#else
#define CURSOR_DEBUG_LOG(msg)
#endif

#if CURSOR_SYNC_PERFORMANCE
#define CURSOR_PERF_START() auto start_time = std::chrono::high_resolution_clock::now()
#define CURSOR_PERF_END(operation) \
    auto end_time = std::chrono::high_resolution_clock::now(); \
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time); \
    std::cout << "[CursorSync Performance] " << operation << " took " << duration.count() << " microseconds" << std::endl
#else
#define CURSOR_PERF_START()
#define CURSOR_PERF_END(operation)
#endif

namespace Core {

void CursorSynchronizer::synchronizeCursorPosition(
    GLFWwindow* window,
    const glm::vec3& worldCursorPos,
    const glm::mat4& projection,
    const glm::mat4& view,
    int windowWidth,
    int windowHeight,
    bool isStereoMode,
    const glm::mat4& rightProjection,
    const glm::mat4& rightView,
    const glm::ivec4& viewport
) {
    CURSOR_PERF_START();

    CURSOR_DEBUG_LOG("Starting cursor synchronization");
    CURSOR_DEBUG_LOG("World position: (" << worldCursorPos.x << ", " << worldCursorPos.y << ", " << worldCursorPos.z << ")");
    CURSOR_DEBUG_LOG("Window size: " << windowWidth << "x" << windowHeight);
    CURSOR_DEBUG_LOG("Stereo mode: " << (isStereoMode ? "enabled" : "disabled"));

    // Validate input parameters
    if (!window || windowWidth <= 0 || windowHeight <= 0) {
        std::cerr << "[CursorSync Error] Invalid window parameters - window: " << window
                  << ", size: " << windowWidth << "x" << windowHeight << std::endl;
        return;
    }

    // Resolve the scene viewport. The projection renders into this sub-rectangle
    // (offset by the docked GUI insets), so NDC must map back into it rather than
    // the full window. A non-positive size means "use the whole window".
    glm::ivec4 vp = viewport;
    if (vp.z <= 0 || vp.w <= 0) {
        vp = glm::ivec4(0, 0, windowWidth, windowHeight);
    }
    const double viewportCenterX = vp.x + vp.z * 0.5;
    const double viewportCenterY = vp.y + vp.w * 0.5;

    // Validate world cursor position
    if (!std::isfinite(worldCursorPos.x) || !std::isfinite(worldCursorPos.y) || !std::isfinite(worldCursorPos.z)) {
        std::cerr << "[CursorSync Error] Invalid world cursor position (NaN or infinite): ("
                  << worldCursorPos.x << ", " << worldCursorPos.y << ", " << worldCursorPos.z << ")" << std::endl;
        glfwSetCursorPos(window, viewportCenterX, viewportCenterY);
        CURSOR_DEBUG_LOG("Fallback: Cursor centered on screen");
        return;
    }

    // Validate matrices
    if (!validateMatrices(projection, view)) {
        std::cerr << "[CursorSync Error] Invalid projection or view matrix" << std::endl;
        // Fallback to viewport center
        glfwSetCursorPos(window, viewportCenterX, viewportCenterY);
        CURSOR_DEBUG_LOG("Fallback: Cursor centered due to invalid matrices");
        return;
    }

    // Additional validation for stereo mode
    if (isStereoMode && (!validateMatrices(rightProjection, rightView))) {
        std::cerr << "[CursorSync Warning] Invalid stereo matrices, falling back to mono mode" << std::endl;
        isStereoMode = false; // Fallback to mono mode
        CURSOR_DEBUG_LOG("Stereo mode disabled due to invalid right eye matrices");
    }

    // Check if cursor is behind camera
    if (isBehindCamera(worldCursorPos, view)) {
        CURSOR_DEBUG_LOG("Cursor is behind camera, centering on screen");
        glfwSetCursorPos(window, viewportCenterX, viewportCenterY);
        CURSOR_PERF_END("cursor synchronization (behind camera)");
        return;
    }

    // Project world position to screen coordinates
    glm::vec2 screenPos;
    if (isStereoMode) {
        CURSOR_DEBUG_LOG("Using stereo projection");
        screenPos = worldToScreenStereo(worldCursorPos, projection, view, rightProjection, rightView, vp);
    } else {
        CURSOR_DEBUG_LOG("Using mono projection");
        screenPos = worldToScreen(worldCursorPos, projection, view, vp);
    }

    CURSOR_DEBUG_LOG("Projected screen position: (" << screenPos.x << ", " << screenPos.y << ")");

    // Clamp the projected position to the viewport boundaries (with a margin) so
    // the OS cursor never lands behind the docked panels or off-screen.
    {
        float margin = 50.0f;
        float minX = static_cast<float>(vp.x) + margin;
        float maxX = static_cast<float>(vp.x + vp.z) - margin;
        float minY = static_cast<float>(vp.y) + margin;
        float maxY = static_cast<float>(vp.y + vp.w) - margin;
        // Guard against a viewport smaller than 2*margin.
        if (minX > maxX) { minX = maxX = static_cast<float>(viewportCenterX); }
        if (minY > maxY) { minY = maxY = static_cast<float>(viewportCenterY); }
        if (screenPos.x < minX || screenPos.x > maxX ||
            screenPos.y < minY || screenPos.y > maxY) {
            CURSOR_DEBUG_LOG("Projected cursor outside viewport, clamping to bounds");
            screenPos.x = glm::clamp(screenPos.x, minX, maxX);
            screenPos.y = glm::clamp(screenPos.y, minY, maxY);
            CURSOR_DEBUG_LOG("Clamped position: (" << screenPos.x << ", " << screenPos.y << ")");
        }
    }

    // Set the Windows cursor position
    glfwSetCursorPos(window, static_cast<double>(screenPos.x), static_cast<double>(screenPos.y));
    
    CURSOR_DEBUG_LOG("Successfully synchronized cursor to (" << screenPos.x << ", " << screenPos.y << ")");
    CURSOR_PERF_END("cursor synchronization");
}

glm::vec2 CursorSynchronizer::worldToScreen(
    const glm::vec3& worldPos,
    const glm::mat4& projection,
    const glm::mat4& view,
    const glm::ivec4& viewport
) {
    CURSOR_DEBUG_LOG("WorldToScreen - Input world pos: (" << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")");

    // Transform world position to clip space
    glm::vec4 clipSpacePos = projection * view * glm::vec4(worldPos, 1.0f);

    CURSOR_DEBUG_LOG("WorldToScreen - Clip space pos: (" << clipSpacePos.x << ", " << clipSpacePos.y << ", " << clipSpacePos.z << ", " << clipSpacePos.w << ")");

    // Perform perspective divide to get normalized device coordinates (NDC)
    if (clipSpacePos.w == 0.0f) {
        std::cerr << "[CursorSync Warning] Division by zero in perspective divide, using viewport center" << std::endl;
        return glm::vec2(viewport.x + viewport.z * 0.5f, viewport.y + viewport.w * 0.5f);
    }

    glm::vec3 ndcPos = glm::vec3(clipSpacePos) / clipSpacePos.w;

    CURSOR_DEBUG_LOG("WorldToScreen - NDC pos: (" << ndcPos.x << ", " << ndcPos.y << ", " << ndcPos.z << ")");

    // Convert NDC to window pixels within the scene viewport sub-rectangle.
    // NDC range is [-1, 1]; the viewport spans [x, x+width] x [y, y+height]
    // (top-left origin), so the projection lands in the same offset area the
    // scene is actually rendered into rather than the whole window.
    float screenX = static_cast<float>(viewport.x) + (ndcPos.x + 1.0f) * 0.5f * viewport.z;
    float screenY = static_cast<float>(viewport.y) + (1.0f - ndcPos.y) * 0.5f * viewport.w; // Flip Y axis for screen coordinates

    CURSOR_DEBUG_LOG("WorldToScreen - Final screen pos: (" << screenX << ", " << screenY << ")");

    return glm::vec2(screenX, screenY);
}

bool CursorSynchronizer::isWithinViewport(
    const glm::vec2& screenPos,
    int windowWidth,
    int windowHeight,
    float margin
) {
    return screenPos.x >= margin && 
           screenPos.x <= (windowWidth - margin) &&
           screenPos.y >= margin && 
           screenPos.y <= (windowHeight - margin);
}

bool CursorSynchronizer::validateMatrices(
    const glm::mat4& projection,
    const glm::mat4& view
) {
    // Check for NaN or infinite values in matrices
    const float* projPtr = glm::value_ptr(projection);
    const float* viewPtr = glm::value_ptr(view);
    
    for (int i = 0; i < 16; ++i) {
        if (!std::isfinite(projPtr[i]) || !std::isfinite(viewPtr[i])) {
            return false;
        }
    }
    
    // Check if matrices are not zero matrices
    bool projNonZero = false;
    bool viewNonZero = false;
    
    for (int i = 0; i < 16; ++i) {
        if (std::abs(projPtr[i]) > 1e-6f) projNonZero = true;
        if (std::abs(viewPtr[i]) > 1e-6f) viewNonZero = true;
    }
    
    return projNonZero && viewNonZero;
}

glm::vec2 CursorSynchronizer::worldToScreenStereo(
    const glm::vec3& worldPos,
    const glm::mat4& leftProjection,
    const glm::mat4& leftView,
    const glm::mat4& rightProjection,
    const glm::mat4& rightView,
    const glm::ivec4& viewport
) {
    CURSOR_DEBUG_LOG("StereoProjection - Processing world pos: (" << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")");

    // Project to both left and right eye screen coordinates
    CURSOR_DEBUG_LOG("StereoProjection - Projecting left eye");
    glm::vec2 leftScreenPos = worldToScreen(worldPos, leftProjection, leftView, viewport);

    CURSOR_DEBUG_LOG("StereoProjection - Projecting right eye");
    glm::vec2 rightScreenPos = worldToScreen(worldPos, rightProjection, rightView, viewport);
    
    // Check if both projections are valid (not behind camera)
    bool leftValid = !isBehindCamera(worldPos, leftView);
    bool rightValid = !isBehindCamera(worldPos, rightView);
    
    CURSOR_DEBUG_LOG("StereoProjection - Left eye valid: " << leftValid << ", Right eye valid: " << rightValid);
    CURSOR_DEBUG_LOG("StereoProjection - Left screen pos: (" << leftScreenPos.x << ", " << leftScreenPos.y << ")");
    CURSOR_DEBUG_LOG("StereoProjection - Right screen pos: (" << rightScreenPos.x << ", " << rightScreenPos.y << ")");
    
    if (leftValid && rightValid) {
        // Average the two projections for the final cursor position
        glm::vec2 avgPos = (leftScreenPos + rightScreenPos) * 0.5f;
        CURSOR_DEBUG_LOG("StereoProjection - Using averaged position: (" << avgPos.x << ", " << avgPos.y << ")");
        return avgPos;
    } else if (leftValid) {
        // Use left eye projection only
        CURSOR_DEBUG_LOG("StereoProjection - Using left eye only");
        return leftScreenPos;
    } else if (rightValid) {
        // Use right eye projection only
        CURSOR_DEBUG_LOG("StereoProjection - Using right eye only");
        return rightScreenPos;
    } else {
        // Both behind camera, return viewport center
        CURSOR_DEBUG_LOG("StereoProjection - Both eyes invalid, using viewport center");
        return glm::vec2(viewport.x + viewport.z * 0.5f, viewport.y + viewport.w * 0.5f);
    }
}

bool CursorSynchronizer::isBehindCamera(
    const glm::vec3& worldPos,
    const glm::mat4& view
) {
    // Transform world position to view space
    glm::vec4 viewSpacePos = view * glm::vec4(worldPos, 1.0f);
    
    // In view space, negative Z means behind the camera (OpenGL convention)
    return viewSpacePos.z > 0.0f;
}

void CursorSynchronizer::printDiagnostics(
    const glm::vec3& worldPos,
    const glm::mat4& projection,
    const glm::mat4& view,
    int windowWidth,
    int windowHeight
) {
    std::cout << "\n=== Cursor Synchronization Diagnostics ===" << std::endl;
    std::cout << "World Position: (" << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")" << std::endl;
    std::cout << "Window Size: " << windowWidth << "x" << windowHeight << std::endl;
    
    // Check if behind camera
    bool behindCamera = isBehindCamera(worldPos, view);
    std::cout << "Behind Camera: " << (behindCamera ? "YES" : "NO") << std::endl;
    
    // Transform to view space
    glm::vec4 viewSpacePos = view * glm::vec4(worldPos, 1.0f);
    std::cout << "View Space: (" << viewSpacePos.x << ", " << viewSpacePos.y << ", " << viewSpacePos.z << ", " << viewSpacePos.w << ")" << std::endl;
    
    // Transform to clip space
    glm::vec4 clipSpacePos = projection * viewSpacePos;
    std::cout << "Clip Space: (" << clipSpacePos.x << ", " << clipSpacePos.y << ", " << clipSpacePos.z << ", " << clipSpacePos.w << ")" << std::endl;
    
    if (clipSpacePos.w != 0.0f) {
        // NDC coordinates
        glm::vec3 ndcPos = glm::vec3(clipSpacePos) / clipSpacePos.w;
        std::cout << "NDC: (" << ndcPos.x << ", " << ndcPos.y << ", " << ndcPos.z << ")" << std::endl;
        
        // Screen coordinates
        float screenX = (ndcPos.x + 1.0f) * 0.5f * windowWidth;
        float screenY = (1.0f - ndcPos.y) * 0.5f * windowHeight;
        std::cout << "Screen: (" << screenX << ", " << screenY << ")" << std::endl;
        
        // Check if within viewport
        bool withinViewport = isWithinViewport(glm::vec2(screenX, screenY), windowWidth, windowHeight);
        std::cout << "Within Viewport: " << (withinViewport ? "YES" : "NO") << std::endl;
    } else {
        std::cout << "ERROR: Division by zero in perspective divide!" << std::endl;
    }
    
    // Matrix validation
    bool matricesValid = validateMatrices(projection, view);
    std::cout << "Matrices Valid: " << (matricesValid ? "YES" : "NO") << std::endl;
    
    std::cout << "========================================\n" << std::endl;
}

} // namespace Core