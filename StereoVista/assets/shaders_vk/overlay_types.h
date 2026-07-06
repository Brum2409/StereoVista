#ifndef SV_OVERLAY_TYPES_H
#define SV_OVERLAY_TYPES_H

// ============================================================================
// GPU structs of the unified overlay renderer (Phase 6, playbook C.9): ONE
// pipeline draws every cursor / tool / gizmo / plugin overlay. Shared between
// C++ and GLSL exactly like scene_types.h (playbook A.1) — see the rules
// there. Kept separate so overlay shaders don't pull scene bindings.
//
// Everything is a TRIANGLE LIST: wide lines are expanded to screen-space
// quads in the vertex shader (no wideLines device dependency), point markers
// and world-space billboards are camera-facing quads. A per-batch `kind`
// selects the vertex expansion + fragment shading; depth test / compare /
// write and cull mode are core-1.3 dynamic state, so overlay depth behaviour
// (always-on-top, depth-tested, x-ray ghost) never multiplies pipelines.
//
// View data travels as ONE buffer device address in the push constants
// (playbook A.2): the whole overlay pipeline binds ZERO descriptor sets.
// ============================================================================

// Batch kinds (OverlayPush.kind).
#define SV_OVERLAY_KIND_FLAT 0u       // world-space triangles, flat vertex color
#define SV_OVERLAY_KIND_LINE 1u       // screen-space expanded wide line quads
#define SV_OVERLAY_KIND_MARKER 2u     // round point-marker billboard (pixel size)
#define SV_OVERLAY_KIND_SPHERE 3u     // sphere-cursor surface shading
#define SV_OVERLAY_KIND_DISC 4u       // soft-edged camera-facing disc (world size)
#define SV_OVERLAY_KIND_RINGS 5u      // concentric ring pair billboard (preview)
#define SV_OVERLAY_KIND_BACKGROUND 6u // NDC-space gradient quad (preview backdrop)

// Per-view camera data, indexed by OverlayPush.viewIndex today and by
// gl_ViewIndex once the Phase 7 stereo backbuffer renders with multiview.
struct OverlayViewParams {  // 96 bytes
    mat4 viewProj;
    vec4 viewportPx;        // x,y = viewport size in pixels; z,w = 1/x, 1/y
    vec4 cameraPos;         // xyz world-space eye (sphere shading, billboards)
};

// Push constants: one per overlay batch (a batch = consecutive primitives
// sharing kind + depth state + shade params).
struct OverlayPush {        // 64 bytes
    uint64_t viewParams;    // OverlayViewParams[viewCount]
    // SPHERE: x transparency, y edgeSoftness, z centerTransparency,
    //         w 1 = inner sphere (skips edge/center modulation).
    // RINGS:  x outerRadius, y outerThickness, z innerRadius, w innerThickness
    //         (all normalised to the billboard's unit radius).
    vec4 paramsA;
    vec4 colorA;            // RINGS outer color / BACKGROUND top color
    vec4 colorB;            // RINGS inner color / BACKGROUND bottom color
    uint kind;              // SV_OVERLAY_KIND_*
    uint viewIndex;         // which OverlayViewParams to read (mono: 0)
};

#endif // SV_OVERLAY_TYPES_H
