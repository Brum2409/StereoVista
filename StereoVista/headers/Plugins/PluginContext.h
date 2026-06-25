#pragma once

// ============================================================================
//  PluginContext  —  the services API handed to every StereoVista plugin
// ----------------------------------------------------------------------------
//  A plugin never talks to the application's globals directly. Instead, every
//  hook on Plugins::Plugin receives a PluginContext& through which it can reach
//  the common, frequently-needed functionality of the host:
//
//      • the active scene and camera
//      • picking (the mouse ray, the 3D-cursor world position, scene raycasts)
//      • user preferences and the undo system
//      • input state (mouse position, modifier keys, viewport size)
//      • user-facing toast notifications
//      • a helper to compile self-contained overlay shaders
//
//  PluginContext is an abstract interface. The concrete implementation lives in
//  main.cpp and simply forwards to the existing application globals, so the
//  plugin layer stays decoupled from the 7,000-line main translation unit.
//
//  See docs/PLUGINS.md for the full API reference and worked examples.
// ============================================================================

#include "Engine/Core.h"   // GL types (GLuint) + GLFW + glm, exactly like Tools/
#include <glm/glm.hpp>
#include <string>

// Forward declarations keep this header light: plugins only pull in the heavy
// types they actually use.
namespace Engine { struct Scene; class UndoManager; }
class Camera;
namespace GUI { struct ApplicationPreferences; }

namespace Plugins {

// Severity for PluginContext::toast(); maps onto GUI::ToastType in the host.
enum class ToastLevel { Success, Info, Warning, Error };

// A picking ray through the current OS-cursor position, in world space.
struct PickRay {
    glm::vec3 origin    = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
};

// Nearest intersection of the mouse ray with model geometry in the scene.
struct RayHit {
    bool      hit        = false;
    float     distance   = 0.0f;
    glm::vec3 position    = glm::vec3(0.0f);
    glm::vec3 normal      = glm::vec3(0.0f, 1.0f, 0.0f);
    int       modelIndex  = -1;   // index into scene().models, or -1
};

class PluginContext {
public:
    virtual ~PluginContext() = default;

    // ── Scene, camera & core services ───────────────────────────────────────
    virtual Engine::Scene&               scene()           = 0;
    virtual const Camera&                camera()    const = 0;
    virtual glm::vec3                    cameraPosition() const = 0;
    virtual GUI::ApplicationPreferences& preferences()     = 0;
    virtual Engine::UndoManager&         undo()            = 0;

    // ── Picking / 3D cursor ─────────────────────────────────────────────────
    // mouseRay():        world-space ray under the OS cursor (uses the same
    //                    projection the renderer used this frame).
    // cursorWorldPos():  the synchronized 3D-cursor world position; returns
    //                    false when the cursor is not over valid geometry.
    // raycastModels():   nearest hit of mouseRay() against scene model meshes.
    virtual PickRay mouseRay()                       const = 0;
    virtual bool    cursorWorldPos(glm::vec3& out)   const = 0;
    virtual RayHit  raycastModels()                  const = 0;

    // ── Input / viewport state ──────────────────────────────────────────────
    virtual glm::vec2 mousePos()      const = 0;   // pixels, window space
    virtual int       keyMods()       const = 0;   // current GLFW_MOD_* bitmask
    virtual glm::vec2 viewportSize()  const = 0;   // 3D viewport, pixels

    // ── User feedback ───────────────────────────────────────────────────────
    virtual void toast(const std::string& message,
                       ToastLevel level = ToastLevel::Info) = 0;

    // ── GL convenience (shared implementation, not overridable) ─────────────
    // Compile + link a tiny vertex/fragment shader program from source strings.
    // Returns the program name, or 0 on failure (errors are logged to stderr).
    // Use this for the self-contained overlay shaders an overlay tool needs,
    // instead of copy-pasting the glCreateShader/link boilerplate.
    GLuint compileOverlayProgram(const char* vertexSrc, const char* fragmentSrc,
                                 const char* label = "plugin");
};

} // namespace Plugins
