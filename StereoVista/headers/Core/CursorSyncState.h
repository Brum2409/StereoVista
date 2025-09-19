#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <cmath>

namespace Core {
    /**
     * Enumeration of different camera operation types for cursor synchronization.
     */
    enum class CameraOperationType {
        None,       // No camera operation in progress
        Orbiting,   // Camera orbiting around a point
        Panning,    // Camera panning (translation in view plane)
        Rotating,   // Camera free rotation
        FreeLook    // Free look camera movement
    };

    /**
     * Structure to track cursor synchronization state during camera operations.
     * This maintains the 3D cursor position and operation context needed for
     * proper cursor synchronization after camera operations complete.
     */
    struct CursorSyncState {
        glm::vec3 worldPosition;                    // 3D cursor world position
        bool isValid;                               // Whether the stored position is valid
        bool needsSynchronization;                  // Flag indicating pending synchronization
        CameraOperationType operation;              // Type of camera operation in progress
        
        // Additional context for synchronization
        glm::mat4 capturedProjection;              // Projection matrix when operation started
        glm::mat4 capturedView;                    // View matrix when operation started
        int windowWidth;                           // Window dimensions when captured
        int windowHeight;
        
        // Default constructor
        CursorSyncState() 
            : worldPosition(0.0f)
            , isValid(false)
            , needsSynchronization(false)
            , operation(CameraOperationType::None)
            , capturedProjection(1.0f)
            , capturedView(1.0f)
            , windowWidth(0)
            , windowHeight(0)
        {}
        
        /**
         * Captures the current cursor state for synchronization.
         * 
         * @param cursorPos 3D world position of the cursor
         * @param opType Type of camera operation starting
         * @param projection Current projection matrix
         * @param view Current view matrix
         * @param winWidth Current window width
         * @param winHeight Current window height
         */
        void capture(
            const glm::vec3& cursorPos,
            CameraOperationType opType,
            const glm::mat4& projection,
            const glm::mat4& view,
            int winWidth,
            int winHeight
        ) {
            worldPosition = cursorPos;
            operation = opType;
            capturedProjection = projection;
            capturedView = view;
            windowWidth = winWidth;
            windowHeight = winHeight;
            isValid = true;
            needsSynchronization = true;
        }
        
        /**
         * Resets the cursor sync state.
         */
        void reset() {
            worldPosition = glm::vec3(0.0f);
            isValid = false;
            needsSynchronization = false;
            operation = CameraOperationType::None;
            capturedProjection = glm::mat4(1.0f);
            capturedView = glm::mat4(1.0f);
            windowWidth = 0;
            windowHeight = 0;
        }
        
        /**
         * Marks synchronization as completed.
         */
        void markSynchronized() {
            needsSynchronization = false;
            operation = CameraOperationType::None;
        }
        
        /**
         * Validates the cursor sync state data.
         * 
         * @return True if the state data is valid and ready for synchronization
         */
        bool validate() const {
            // Check basic validity
            if (!isValid || !needsSynchronization || operation == CameraOperationType::None) {
                return false;
            }
            
            // Check window dimensions
            if (windowWidth <= 0 || windowHeight <= 0) {
                return false;
            }
            
            // Check world position for NaN or infinite values
            if (!std::isfinite(worldPosition.x) || !std::isfinite(worldPosition.y) || !std::isfinite(worldPosition.z)) {
                return false;
            }
            
            // Check matrices for validity (basic check)
            const float* projPtr = glm::value_ptr(capturedProjection);
            const float* viewPtr = glm::value_ptr(capturedView);
            
            for (int i = 0; i < 16; ++i) {
                if (!std::isfinite(projPtr[i]) || !std::isfinite(viewPtr[i])) {
                    return false;
                }
            }
            
            return true;
        }
    };

    /**
     * Manager class for cursor synchronization state.
     * Provides centralized management of cursor state during camera operations.
     */
    class CursorSyncManager {
    public:
        /**
         * Gets the singleton instance of the cursor sync manager.
         */
        static CursorSyncManager& getInstance() {
            static CursorSyncManager instance;
            return instance;
        }
        
        /**
         * Captures cursor state at the start of a camera operation.
         */
        void captureState(
            const glm::vec3& cursorPos,
            CameraOperationType opType,
            const glm::mat4& projection,
            const glm::mat4& view,
            int windowWidth,
            int windowHeight
        ) {
            m_state.capture(cursorPos, opType, projection, view, windowWidth, windowHeight);
        }
        
        /**
         * Gets the current cursor sync state.
         */
        const CursorSyncState& getState() const {
            return m_state;
        }
        
        /**
         * Checks if synchronization is needed.
         */
        bool needsSynchronization() const {
            if (!m_state.needsSynchronization) {
                return false;
            }
            
            if (!m_state.validate()) {
                // Invalid state detected, reset to prevent issues
                const_cast<CursorSyncManager*>(this)->reset();
                return false;
            }
            
            return true;
        }
        
        /**
         * Marks synchronization as completed.
         */
        void markSynchronized() {
            m_state.markSynchronized();
        }
        
        /**
         * Resets the cursor sync state.
         */
        void reset() {
            m_state.reset();
        }
        
        /**
         * Gets the stored world position for synchronization.
         */
        const glm::vec3& getWorldPosition() const {
            return m_state.worldPosition;
        }
        
        /**
         * Gets the current camera operation type.
         */
        CameraOperationType getOperationType() const {
            return m_state.operation;
        }
        
    private:
        CursorSyncState m_state;
        
        // Private constructor for singleton
        CursorSyncManager() = default;
        
        // Prevent copying
        CursorSyncManager(const CursorSyncManager&) = delete;
        CursorSyncManager& operator=(const CursorSyncManager&) = delete;
    };
}