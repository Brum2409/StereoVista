#pragma once

// ============================================================================
//  Cursor types — the Vulkan-native home of the 3D-cursor enums/settings.
// ----------------------------------------------------------------------------
//  Moved out of the GL-era Gui/GuiTypes.h (UI redesign Pass 0 follow-up):
//  these three definitions were the last thing the LIVE build consumed from
//  the excluded-reference headers. GuiTypes.h is now included only by the
//  ExcludedFromBuild GL sources; everything compiled ships Vulkan-native.
//  Plain data — no ImGui, no GPU types.
// ============================================================================

#include <glm/glm.hpp>

namespace Cursor {

// How a cursor's on-screen size follows the camera distance.
enum ScalingMode {
    CURSOR_NORMAL,              // world-space size (perspective shrink)
    CURSOR_FIXED,               // constant screen-space size
    CURSOR_CONSTRAINED_DYNAMIC, // world-space, clamped between min/max diff
    CURSOR_LOGARITHMIC          // eased log falloff with distance
};

// When the 3D cursor would fall off geometry onto the background (skybox /
// empty space), this controls how long it keeps following the mouse at the
// last valid depth before giving up and reverting to the OS cursor. Avoids
// the cursor flickering between 2D and 3D over sparse models / point clouds
// where the depth buffer is mostly background.
enum BackgroundCacheMode {
    CURSOR_CACHE_INDEFINITE, // never give up; stay at last depth until a new hit
    CURSOR_CACHE_TIMED,      // give up after a time limit (seconds off geometry)
    CURSOR_CACHE_DISTANCE    // give up after the mouse travels too far (pixels)
};

// The fragment (ring) cursor's appearance. Drawn by the scene shader
// (mesh.frag) on geometry surfaces; FragmentCursor publishes these into the
// renderer's FragmentCursorState each frame.
struct FragmentCursorSettings {
    float baseOuterRadius = 0.04f;
    float baseOuterBorderThickness = 0.005f;
    float baseInnerRadius = 0.004f;
    float baseInnerBorderThickness = 0.005f;
    glm::vec4 outerColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
    glm::vec4 innerColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
};

} // namespace Cursor
