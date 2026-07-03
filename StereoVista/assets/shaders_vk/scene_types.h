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
#define SV_MAX_POINT_LIGHTS 16
#define SV_MAX_SHADOWED_POINT_LIGHTS 4
#define SV_POINT_SHADOW_RESOLUTION 1024
#define SV_SUN_SHADOW_RESOLUTION 4096
#define SV_INVALID_TEXTURE 0xFFFFFFFFu

// FrameData.flags bits
#define SV_FRAME_SHADOWS_ENABLED 1u
#define SV_FRAME_SUN_ENABLED 2u
#define SV_FRAME_SOFT_SHADOWS 4u // PCSS contact hardening (else fixed-width PCF)

// One multiview view (mono = view 0 only; stereo fills both in Phase 7).
struct ViewData {          // 144 bytes
    mat4 viewProj;
    mat4 invViewProj;      // clip -> world, for skybox ray reconstruction
    vec4 cameraPos;        // xyz world-space eye position
};

// Per-frame constants, one UBO bound at set 0 binding 0 for every scene pass.
struct FrameData {         // 1968 bytes
    ViewData views[SV_MAX_VIEWS];
    mat4 sunViewProj;      // world -> sun shadow clip (reverse-Z ortho)
    // Reverse-Z 90-degree face projections for every shadowed point light,
    // indexed [lightSlot * 6 + faceIndex(gl_ViewIndex)].
    mat4 pointShadowFaceVP[SV_MAX_SHADOWED_POINT_LIGHTS * 6];
    vec4 sunDirection;     // xyz: direction the light TRAVELS (world)
    vec4 sunColor;         // rgb color, w intensity
    vec4 ambientColor;     // rgb flat ambient (albedo multiplier)
    uint pointLightCount;
    uint flags;            // SV_FRAME_* bits
    float shadowTexelWorldSize; // world footprint of one sun shadow texel
    float pointShadowNear;
    float pointShadowFar;
    float sunWorldToDepth; // ndc.z gained per world unit moved toward the sun
    // tan(sun angular radius): world penumbra width per world unit of
    // receiver-to-blocker distance (0 disables sun contact hardening).
    float sunPenumbraScale;
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
    vec4 baseColor;        // rgb flat albedo when albedoTexture is invalid
    float metallic;        // factor, multiplied with metallicTexture.b
    float roughness;       // factor, multiplied with roughnessTexture.g
    float emissive;        // emitted = albedo * emissive
    float normalScale;
    uint albedoTexture;
    uint normalTexture;
    uint metallicTexture;
    uint roughnessTexture;
    uint aoTexture;        // .r
    uint pad0;
    uint pad1;
    uint pad2;
};

// Per-draw push constants (116 bytes, within the 128-byte core minimum).
struct DrawPush {
    mat4 model;
    vec4 normalMatCol0;    // columns of the world-space normal matrix
    vec4 normalMatCol1;
    vec4 normalMatCol2;
    uint materialIndex;
};

struct DepthSunPush {      // sun shadow casters (sunViewProj is in FrameData)
    mat4 model;
};

struct DepthPointPush {    // point shadow casters (multiview, 6 faces at once)
    mat4 model;
    uint lightSlot;        // index into pointShadowFaceVP / cube-array layer
};

#endif // SV_SCENE_TYPES_H
