#pragma once

// ============================================================================
//  Gui::Settings  —  the single, serialization-ready home for the app's
//  user-tunable settings.
// ----------------------------------------------------------------------------
//  This replaces the scattered `Application` settings members the interim debug
//  panel used to edit in place. The GUI panels edit one `Settings` instance
//  (owned by Application) through the Gui::Services facade; the render loop
//  reads from it. Group-by-group nesting keeps it readable AND makes it the
//  obvious target for the later preferences.json load/save task (D in
//  docs/TODO.md) — do NOT resurrect the GL `GUI::ApplicationPreferences` blob
//  with its dead VCT/Radiance/SpaceMouse fields; grow this instead.
//
//  Design notes
//   * PLAIN DATA ONLY — no ImGui, no Vulkan calls. It reuses renderer::SunState
//     / renderer::SkyState so the loop can hand them straight to the
//     FrameSubmission (they are simple aggregates).
//   * Defaults mirror the values Application shipped with, so switching the loop
//     onto Settings changes no behaviour.
//   * Transient / lifecycle state (the applied stereo mode, the live VR session
//     toggle, present-mode + swapchain bookkeeping) intentionally stays on
//     Application — it is not a "setting" and must not be serialized.
// ============================================================================

#include "Renderer/FrameSubmission.h" // renderer::SunState, renderer::SkyState

namespace Gui {

struct Settings {
    // ── Camera & navigation ─────────────────────────────────────────────────
    struct Camera {
        float fovDeg    = 60.0f;
        float speed     = 4.0f;   // WASDQE fly speed (world units/s)
        float nearPlane = 0.1f;
        float farPlane  = 500.0f;

        // Look / zoom feel — pushed onto Core::Camera each frame.
        float sensitivity   = 0.06f; // mouse-look degrees per pixel
        float speedFactor   = 1.0f;  // multiplier for the (future) adaptive fly speed
        bool  adaptiveSpeed = false; // distance-adaptive fly/zoom speed (needs the
                                     // per-frame depth feed — see docs/TODO.md A4)

        bool  useSmoothScrolling = true;
        float scrollMomentum     = 0.5f;
        float scrollDeceleration = 5.0f;
        float maxScrollVelocity  = 3.0f;

        bool  zoomToCursor      = true; // scroll zooms toward the 3D/background cursor
        bool  orbitAroundCursor = true; // LMB orbit pivots on the cursor point
    } camera;

    // ── Stereo (quad-buffer / side-by-side) ─────────────────────────────────
    // The applied StereoMode lives on Application (it drives swapchain/renderer
    // rebuilds); these are the per-mode tunables the panel edits live.
    struct Stereo {
        float separation           = 0.5f; // eye separation, world units
        float convergence          = 2.6f; // zero-parallax distance, world units
        bool  autoConvergence      = false;
        float convergenceFactor    = 1.0f; // convergence = focus distance * factor
        float convergenceSmoothing = 5.0f; // exp smoothing rate (higher = snappier)
        bool  flipEyes             = false;
    } stereo;

    // ── VR / OpenXR (comfort + projection; the live session toggle is on the app) ─
    struct Vr {
        float worldScale     = 1.0f;  // metres per scene unit
        bool  mirrorToWindow = true;  // draw the left eye behind ImGui on the desktop
        bool  useScenePlanes = true;  // inherit camera near/far for the XR projection
        float nearPlane      = 0.05f;
        float farPlane       = 200.0f;
    } vr;

    // ── Lighting (sun + shadows + ambient) ──────────────────────────────────
    struct Lighting {
        bool  shadows     = true;
        bool  softShadows = true;  // PCSS contact hardening (else fixed-width PCF)
        // Point-light shadow reach in world units (cube-map far plane).
        float pointShadowRange = 50.0f;
        float ambient     = 0.03f; // flat ambient albedo multiplier
        renderer::SunState sun;    // enabled defaults false; Application turns it on
    } lighting;

    // ── Sky / environment ───────────────────────────────────────────────────
    renderer::SkyState sky;

    // ── Point clouds ────────────────────────────────────────────────────────
    struct PointCloud {
        int   downsample     = 1;     // load-time stride (>=1)
        bool  mortonResort   = true;  // LAS/LAZ two-phase resort for render speed
        bool  hqs            = false; // high-quality shading (3-pass averaging)
        float hqsThreshold   = 0.01f; // relative depth window
        bool  splatEnabled   = true;  // adaptive splats (close-up hole filling)
        int   splatMaxRadius = 4;     // upper clamp on splat radius, px (1-8)
    } pointCloud;

    // ── 3D cursor (only the fields that gate the loop; the detailed appearance
    //    lives on Cursor::CursorManager / the cursor objects, edited via the
    //    cursor panel through Services) ────────────────────────────────────────
    struct Cursor {
        bool show = true;
        int  type = 0; // 0=Sphere, 1=Plane, 2=Fragment ring
    } cursor;

    // ── Render toggles that aren't yet owned elsewhere ──────────────────────
    struct Render {
        bool wireframe = false; // line-mode debug pipeline over every draw (M4);
                                // no-op when the GPU lacks fillModeNonSolid.
    } render;
};

} // namespace Gui
