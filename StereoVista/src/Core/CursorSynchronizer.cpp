#include "Core/CursorSynchronizer.h"
#include <iostream>
#include <glm/gtc/type_ptr.hpp>
#include <chrono>

// Debug logging control
#define CURSOR_SYNC_DEBUG 1
#define CURSOR_SYNC_PERFORMANCE 1

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
    const glm::mat4& rightView
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
    
    // Validate world cursor position
    if (!std::isfinite(worldCursorPos.x) || !std::isfinite(worldCursorPos.y) || !std::isfinite(worldCursorPos.z)) {
        std::cerr << "[CursorSync Error] Invalid world cursor position (NaN or infinite): (" 
                  << worldCursorPos.x << ", " << worldCursorPos.y << ", " << worldCursorPos.z << ")" << std::endl;
        glfwSetCursorPos(window, windowWidth / 2.0, windowHeight / 2.0);
        CURSOR_DEBUG_LOG("Fallback: Cursor centered on screen");
        return;
    }

    // Validate matrices
    if (!validateMatrices(projection, view)) {
        std::cerr << "[CursorSync Error] Invalid projection or view matrix" << std::endl;
        // Fallback to screen center
        glfwSetCursorPos(window, windowWidth / 2.0, windowHeight / 2.0);
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
        glfwSetCursorPos(window, windowWidth / 2.0, windowHeight / 2.0);
        CURSOR_PERF_END("cursor synchronization (behind camera)");
        return;
    }

    // Project world position to screen coordinates
    glm::vec2 screenPos;
    if (isStereoMode) {
        CURSOR_DEBUG_LOG("Using stereo projection");
        screenPos = worldToScreenStereo(worldCursorPos, projection, view, rightProjection, rightView, windowWidth, windowHeight);
    } else {
        CURSOR_DEBUG_LOG("Using mono projection");
        screenPos = worldToScreen(worldCursorPos, projection, view, windowWidth, windowHeight);
    }
    
    CURSOR_DEBUG_LOG("Projected screen position: (" << screenPos.x << ", " << screenPos.y << ")");

    // Check if projected position is within viewport
    if (!isWithinViewport(screenPos, windowWidth, windowHeight)) {
        CURSOR_DEBUG_LOG("Projected cursor outside viewport, clamping to bounds");
        CURSOR_DEBUG_LOG("Original position: (" << screenPos.x << ", " << screenPos.y << ")");
        
        // Clamp to viewport boundaries with margin
        float margin = 50.0f;
        screenPos.x = glm::clamp(screenPos.x, margin, static_cast<float>(windowWidth) - margin);
        screenPos.y = glm::clamp(screenPos.y, margin, static_cast<float>(windowHeight) - margin);
        
        CURSOR_DEBUG_LOG("Clamped position: (" << screenPos.x << ", " << screenPos.y << ")");
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
    int windowWidth,
    int windowHeight
) {
    CURSOR_DEBUG_LOG("WorldToScreen - Input world pos: (" << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")");
    
    // Transform world position to clip space
    glm::vec4 clipSpacePos = projection * view * glm::vec4(worldPos, 1.0f);
    
    CURSOR_DEBUG_LOG("WorldToScreen - Clip space pos: (" << clipSpacePos.x << ", " << clipSpacePos.y << ", " << clipSpacePos.z << ", " << clipSpacePos.w << ")");
    
    // Perform perspective divide to get normalized device coordinates (NDC)
    if (clipSpacePos.w == 0.0f) {
        std::cerr << "[CursorSync Warning] Division by zero in perspective divide, using screen center" << std::endl;
        return glm::vec2(windowWidth / 2.0f, windowHeight / 2.0f);
    }
    
    glm::vec3 ndcPos = glm::vec3(clipSpacePos) / clipSpacePos.w;
    
    CURSOR_DEBUG_LOG("WorldToScreen - NDC pos: (" << ndcPos.x << ", " << ndcPos.y << ", " << ndcPos.z << ")");
    
    // Convert NDC to screen coordinates
    // NDC range is [-1, 1], screen coordinates are [0, width] and [0, height]
    float screenX = (ndcPos.x + 1.0f) * 0.5f * windowWidth;
    float screenY = (1.0f - ndcPos.y) * 0.5f * windowHeight; // Flip Y axis for screen coordinates
    
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
    int windowWidth,
    int windowHeight
) {
    CURSOR_DEBUG_LOG("StereoProjection - Processing world pos: (" << worldPos.x << ", " << worldPos.y << ", " << worldPos.z << ")");
    
    // Project to both left and right eye screen coordinates
    CURSOR_DEBUG_LOG("StereoProjection - Projecting left eye");
    glm::vec2 leftScreenPos = worldToScreen(worldPos, leftProjection, leftView, windowWidth, windowHeight);
    
    CURSOR_DEBUG_LOG("StereoProjection - Projecting right eye");
    glm::vec2 rightScreenPos = worldToScreen(worldPos, rightProjection, rightView, windowWidth, windowHeight);
    
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
        // Both behind camera, return screen center
        CURSOR_DEBUG_LOG("StereoProjection - Both eyes invalid, using screen center");
        return glm::vec2(windowWidth / 2.0f, windowHeight / 2.0f);
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