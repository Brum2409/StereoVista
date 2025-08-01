#ifndef SPACEMOUSEINPUT_H
#define SPACEMOUSEINPUT_H

#include "Engine/Core.h"
#include "Core/Camera.h"
#include "Gui/GuiTypes.h"
#include <memory>
#include <functional>

// Forward declarations for 3DConnexion types
struct nlHandle_s;
typedef struct nlHandle_s* nlHandle_t;
typedef const char* property_t;
struct value_s;
typedef struct value_s value_t;

namespace SpaceMouse {
    namespace Navigation3D {
        class CNavigation3D;
    }
}

// SpaceMouse navigation class for StereoVista
class SpaceMouseInput {
public:
    SpaceMouseInput();
    ~SpaceMouseInput();

    // Initialize the SpaceMouse connection
    bool Initialize(const std::string& appName = "StereoVista");
    
    // Cleanup and close connection
    void Shutdown();
    
    // Set the camera that will be controlled by the SpaceMouse
    void SetCamera(std::shared_ptr<Camera> camera);
    
    // Set the model extents for proper navigation scaling
    void SetModelExtents(const glm::vec3& min, const glm::vec3& max);
    
    // Update function to be called in the main loop
    void Update(float deltaTime);
    
    // Enable/disable SpaceMouse input
    void SetEnabled(bool enabled);
    bool IsEnabled() const { return m_enabled; }
    
    // Check if SpaceMouse is currently navigating (blocking other input)
    bool IsNavigating() const { return m_isNavigating; }
    
    
    
    // Set perspective/orthographic view mode
    void SetPerspectiveMode(bool perspective);
    
    // Set field of view for perspective mode
    void SetFieldOfView(float fov);
    
    // Set navigation sensitivity
    void SetSensitivity(float translationSensitivity, float rotationSensitivity);
    
    // Set deadzone threshold (movement below this threshold is ignored)
    void SetDeadzone(float deadzone);
    float GetDeadzone() const { return m_deadzone; }
    
    // Window size for proper coordinate transformations
    void SetWindowSize(int width, int height);
    
    // Set cursor anchor point for navigation pivot
    void SetCursorAnchor(const glm::vec3& cursorPosition, GUI::SpaceMouseAnchorMode anchorMode);
    
    // Force refresh of pivot position in NavLib
    void RefreshPivotPosition();
    
    // Set anchor mode and cursor centering options
    void SetAnchorMode(GUI::SpaceMouseAnchorMode mode);
    void SetCenterCursor(bool centerCursor);
    
    // Callbacks for extended functionality
    std::function<void()> OnNavigationStarted;
    std::function<void()> OnNavigationEnded;
    std::function<void(const std::string&)> OnCommandExecuted;
    

private:
    // Internal navigation model implementation
    class NavigationModel;
    std::unique_ptr<NavigationModel> m_navigationModel;
    
    // Navigation state
    std::shared_ptr<Camera> m_camera;
    bool m_enabled;
    bool m_perspectiveMode;
    float m_fieldOfView;
    int m_windowWidth, m_windowHeight;
    
    // Model bounds for navigation
    glm::vec3 m_modelMin, m_modelMax;
    
    // Sensitivity settings
    float m_translationSensitivity;
    float m_rotationSensitivity;
    float m_deadzone;
    
    // Navigation timing
    bool m_isNavigating;
    float m_lastUpdateTime;
    
    // Cursor anchor settings
    glm::vec3 m_cursorAnchor;
    GUI::SpaceMouseAnchorMode m_anchorMode;
    bool m_centerCursor;
    glm::vec3 m_navigationStartAnchor;
    
    // Helper functions
    void UpdateCameraFromNavlib();
    glm::mat4 ConvertNavlibMatrix(const double* navlibMatrix) const;
    void ConvertToNavlibMatrix(const glm::mat4& glmMatrix, double* navlibMatrix) const;
    glm::vec3 ConvertNavlibPoint(const double* navlibPoint) const;
    void ConvertToNavlibPoint(const glm::vec3& glmPoint, double* navlibPoint) const;
};

#endif // SPACEMOUSEINPUT_H