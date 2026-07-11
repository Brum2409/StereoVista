#ifndef SV_SCENE_TYPES_H
#define SV_SCENE_TYPES_H

// ============================================================================
// SINGLE SOURCE OF TRUTH for every GPU-visible scene struct (playbook A.1).
//
// Included by BOTH languages:
//   * GLSL: `#include "scene_types.h"` from any shader in this directory. All
//     blocks binding these structs must be declared `layout(..., scalar)`
//     (GL_EXT_scalar_block_layout — required+enabled in rhi::Device), which
//     uses C-like packing rules, so the layouts below match C++ 1:1.
//   * C++: included by headers/Renderer/GpuTypes.h inside namespace
//     renderer::gpu after aliasing vec2/vec3/vec4/mat4/uint to glm types.
//     That header static_asserts every sizeof against the comments here.
//
// Rules: only vec2/vec3/vec4/mat4/float/int/uint members, no functions, no
// includes. Keep members ordered largest-first per block where practical and
// pad structs to 16 bytes so arrays stride identically everywhere.
// ============================================================================

#define SV_MAX_VIEWS 2
// Dockable 3D viewports the renderer can output at once (each renders up to
// SV_MAX_VIEWS eye views into its own targets). Raising this must also raise
// SV_PC_MAX_VIEWS (pointcloud_types.h) — the point-cloud geometry dispatch
// projects every (viewport, eye) in one pass; GpuTypes.h asserts the product.
#define SV_MAX_VIEWPORTS 4
#define SV_MAX_POINT_LIGHTS 16
#define SV_MAX_SHADOWED_POINT_LIGHTS 4
#define SV_POINT_SHADOW_RESOLUTION 1024
#define SV_SUN_SHADOW_RESOLUTION 4096
#define SV_INVALID_TEXTURE 0xFFFFFFFFu
// User section/clip planes (Phase 6 ClipPlaneTool). Must stay in sync with
// Engine::MAX_CLIP_PLANES (Engine/Data.h) and SV_PC_CLIP_PLANES
// (pointcloud_types.h) — one budget across meshes and point clouds.
#define SV_MAX_CLIP_PLANES 6

// FrameData.flags bits
#define SV_FRAME_SHADOWS_ENABLED 1u
#define SV_FRAME_SUN_ENABLED 2u
#define SV_FRAME_SOFT_SHADOWS 4u // PCSS contact hardening (else fixed-width PCF)

// MaterialData.flags bits
// Cutout transparency (glTF alphaMode MASK / I3S vegetation): mesh.frag
// discards fragments whose albedo alpha < alphaCutoff before any lighting.
#define SV_MATERIAL_ALPHA_MASK 1u
// Geometry is authored double-sided (I3S doubleSided / cullFace none). The
// CPU draw path mirrors this in DrawItem::twoSided (dynamic cull mode); the
// bit rides here as well so a future ray-tracing mode can decide hit-facing
// per material without a CPU side table.
#define SV_MATERIAL_TWO_SIDED 2u

// One multiview view (mono = view 0 only; stereo fills both in Phase 7).
struct ViewData {          // 144 bytes
    mat4 viewProj;
    mat4 invViewProj;      // clip -> world, for skybox ray reconstruction
    vec4 cameraPos;        // xyz world-space eye position
};

// Per-frame constants, one UBO bound at set 0 binding 0 for every scene pass.
struct FrameData {         // 2144 bytes
    ViewData views[SV_MAX_VIEWS];
    mat4 sunViewProj;      // world -> sun shadow clip (reverse-Z ortho)
    // Reverse-Z 90-degree face projections for every shadowed point light,
    // indexed [lightSlot * 6 + faceIndex(gl_ViewIndex)].
    mat4 pointShadowFaceVP[SV_MAX_SHADOWED_POINT_LIGHTS * 6];
    vec4 sunDirection;     // xyz: direction the light TRAVELS (world)
    vec4 sunColor;         // rgb color, w intensity
    vec4 ambientColor;     // rgb flat ambient (albedo multiplier)
    // Section/clip planes, world space, packed (n.xyz, d); a point p is kept
    // while dot(n, p) + d >= 0. mesh.vert writes them to gl_ClipDistance
    // (hardware clipping — the Vulkan equivalent of the GL scene shaders'
    // gl_ClipDistance path). Shadow casters deliberately do NOT clip,
    // matching the GL app (clipped-away geometry still casts its shadow).
    vec4 clipPlanes[SV_MAX_CLIP_PLANES];
    // Fragment (ring) cursor, drawn by mesh.frag ON scene surfaces around the
    // 3D cursor (port of the GL uber-shader's cursor rings). cursorPos.w > 0.5
    // = the cursor is on valid geometry this frame.
    vec4 cursorPos;
    vec4 cursorOuterColor;
    vec4 cursorInnerColor;
    // x outerRadius, y outerThickness, z innerRadius, w innerThickness — all
    // scaled by camera distance in the shader like the GL original.
    vec4 cursorRingParams;
    uint clipPlaneCount;   // 0..SV_MAX_CLIP_PLANES
    uint showFragmentCursor;
    uint pointLightCount;
    uint flags;            // SV_FRAME_* bits
    float shadowTexelWorldSize; // world footprint of one sun shadow texel
    float pointShadowNear;
    float pointShadowFar;
    float sunWorldToDepth; // ndc.z gained per world unit moved toward the sun
    // tan(sun angular radius): world penumbra width per world unit of
    // receiver-to-blocker distance (0 disables sun contact hardening).
    float sunPenumbraScale;
    uint pad0;
    uint pad1;
    uint pad2;
};

struct PointLightData {    // 48 bytes
    vec4 position;         // xyz world position, w intensity
    vec4 color;            // rgb color
    float attenLinear;     // 1 / (1 + linear*d + quadratic*d^2)
    float attenQuadratic;
    int shadowIndex;       // cube-array slot, -1 = does not cast shadows
    float radius;          // emitter world radius, drives PCSS penumbra width
};

// Bindless material: texture INDICES into the global texture2D array (set 1),
// SV_INVALID_TEXTURE = "not present" (replaces GL's material.textures[16] +
// hasTexture-as-float model, playbook C.8). Albedo textures are sRGB-format
// views (hardware decode, C.6); the data textures are UNORM.
struct MaterialData {      // 64 bytes
    vec4 baseColor;        // rgb flat albedo when albedoTexture is invalid; a
                           //   multiplies the texture alpha (alpha-mask test)
    float metallic;        // factor, multiplied with metallicTexture.b
    float roughness;       // factor, multiplied with roughnessTexture.g
    float emissive;        // emitted = albedo * emissive
    float normalScale;
    uint albedoTexture;
    uint normalTexture;
    uint metallicTexture;
    uint roughnessTexture;
    uint aoTexture;        // .r
    uint flags;            // SV_MATERIAL_* bits
    float alphaCutoff;     // SV_MATERIAL_ALPHA_MASK discard threshold
    uint pad0;
};

// Per-draw push constants (128 bytes — exactly the core minimum; do not grow).
struct DrawPush {
    mat4 model;
    vec4 normalMatCol0;    // columns of the world-space normal matrix
    vec4 normalMatCol1;
    vec4 normalMatCol2;
    uint materialIndex;
    // Per-draw albedo multiplier (linear). 1,1,1 for normal draws; the
    // BrushTool's per-instance color variation rides here (Phase 6).
    vec3 tint;
};

struct DepthSunPush {      // sun shadow casters (sunViewProj is in FrameData)
    mat4 model;
};

struct DepthPointPush {    // point shadow casters (multiview, 6 faces at once)
    mat4 model;
    uint lightSlot;        // index into pointShadowFaceVP / cube-array layer
};

// Alpha-masked caster twins: the masked shadow pipelines add a fragment stage
// that discards below the material's alphaCutoff (depth_masked.frag), so
// cutout geometry shadows its silhouette instead of its full card. The shared
// fragment shader declares the COMMON PREFIX (model at 0, materialIndex at
// 64) — keep materialIndex directly after model in both.
struct DepthMaskedPush {   // sun, 68 bytes
    mat4 model;
    uint materialIndex;
};

struct DepthPointMaskedPush { // point (multiview), 72 bytes
    mat4 model;
    uint materialIndex;
    uint lightSlot;
};

#endif // SV_SCENE_TYPES_H
