#pragma once

// ============================================================================
//  PluginContext  —  the services API handed to every StereoVista plugin
// ----------------------------------------------------------------------------
//  A plugin never talks to the application's internals directly. Every hook on
//  Plugins::Plugin receives a PluginContext& through which it reaches the
//  common host services:
//
//      • the active scene and camera
//      • the unified RHI overlay draw list (world-space annotations/gizmos)
//      • picking: the mouse ray, the depth-picked 3D-cursor world position,
//        and object identification (model + sub-mesh) under the cursor
//      • model/sub-mesh selection
//      • the undo system
//      • input state (mouse position, modifier keys, viewport size)
//      • user-facing toast notifications
//
//  Vulkan rewrite of the GL PluginContext: the GL `compileOverlayProgram`
//  helper (every tool owned its own shader + VAO) is gone — plugins now append
//  geometry to a single shared renderer::OverlayDrawList (playbook C.9), so the
//  per-eye onRenderViewport collapses to one world-space overlay build.
//
//  PluginContext is an abstract interface; the concrete MainPluginContext lives
//  in App/Application.cpp and forwards to the application state.
// ============================================================================

#include <glm/glm.hpp>
#include <string>

// Forward declarations keep this header light and GL-free.
namespace scene { struct Scene; struct RayHit; }
namespace renderer { class OverlayDrawList; }
namespace core { class UndoManager; }
namespace Gui { struct Settings; }
class Camera;

namespace Plugins {

// Severity for PluginContext::toast(); maps onto the host's toast overlay.
enum class ToastLevel { Success, Info, Warning, Error };

// A picking ray through the current OS-cursor position, in world space.
struct PickRay {
    glm::vec3 origin    = glm::vec3(0.0f);
    glm::vec3 direction = glm::vec3(0.0f, 0.0f, -1.0f);
};

class PluginContext {
public:
    virtual ~PluginContext() = default;

    // ── Scene, camera & core services ───────────────────────────────────────
    virtual scene::Scene&      scene()               = 0;
    virtual const Camera&      camera()        const = 0;
    virtual glm::vec3          cameraPosition() const = 0;
    virtual core::UndoManager& undo()                = 0;

    // ── Preferences (restored in UI redesign Pass 0) ─────────────────────────
    // The app's persisted user settings (Gui/Settings.h) — read for behaviour
    // flags, write to change them; persistence to preferences.json is
    // automatic (debounced save + save-on-exit). Include Gui/Settings.h in
    // your plugin .cpp to use it.
    virtual Gui::Settings& preferences() = 0;

    // ── Overlay (replaces the GL compileOverlayProgram) ─────────────────────
    // Append world-space geometry once per frame; the renderer draws it on the
    // backbuffer after tonemap, depth-tested against the scene. One build feeds
    // both stereo eyes — no per-eye matrices.
    virtual renderer::OverlayDrawList& overlay() = 0;

    // ── Picking / 3D cursor ─────────────────────────────────────────────────
    // mouseRay():        world-space ray under the OS cursor (same projection
    //                    the renderer used this frame).
    // cursorWorldPos():  the depth-picked 3D-cursor world position; false when
    //                    the cursor is not over valid geometry.
    // raycastModels():   the model + sub-mesh whose surface is under the cursor,
    //                    resolved from the GPU depth point (no per-triangle work).
    virtual PickRay mouseRay()                       const = 0;
    virtual bool    cursorWorldPos(glm::vec3& out)   const = 0;
    virtual bool    raycastModels(scene::RayHit& out) const = 0;

    // ── Selection (index into scene().models; mesh -1 = whole model) ─────────
    virtual int  selectedModel() const = 0;
    virtual int  selectedMesh()  const = 0;
    virtual void setSelection(int model, int mesh = -1) = 0;

    // ── Input / viewport state ──────────────────────────────────────────────
    // The 3D view is a dockable GUI window since the viewport rework, so all
    // pointer coordinates are 3D-VIEWPORT-LOCAL render-target pixels (identical
    // to window pixels on the classic fullscreen path).
    virtual glm::vec2 mousePos()      const = 0;   // render-target pixels
    virtual int       keyMods()       const = 0;   // current GLFW_MOD_* bitmask
    virtual glm::vec2 viewportSize()  const = 0;   // render-target size, pixels
    // Where the 3D view sits on screen (ImGui screen coordinates): for HUD
    // drawing over the viewport via ImGui draw lists (labels, readouts). NDC
    // from viewProj() maps into exactly this rectangle.
    virtual glm::vec2 viewportScreenPos()  const = 0;
    virtual glm::vec2 viewportScreenSize() const = 0;
    // proj*view for this frame's camera (for world->screen label projection).
    virtual glm::mat4 viewProj()      const = 0;

    // ── User feedback ───────────────────────────────────────────────────────
    virtual void toast(const std::string& message,
                       ToastLevel level = ToastLevel::Info) = 0;
};

} // namespace Plugins
