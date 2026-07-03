#version 460
#extension GL_EXT_multiview : require
#extension GL_EXT_scalar_block_layout : require

// Sky background. The view direction is reconstructed per pixel by
// unprojecting the NDC position at the near plane (reverse-Z: z = 1) through
// the per-view inverse view-projection — no cube vertex buffer, multiview-
// correct by construction. Output is linear HDR (tonemap handles the rest).

#include "scene_types.h"

layout(set = 0, binding = 0, scalar) uniform FrameBlock { FrameData uFrame; };
layout(set = 0, binding = 5) uniform samplerCube uSkyCube;
layout(set = 0, binding = 6) uniform sampler2D uSkyEquirect;

// Modes match renderer::SkyMode.
#define SKY_MODE_CUBEMAP 0
#define SKY_MODE_EQUIRECT 1
#define SKY_MODE_SOLID 2
#define SKY_MODE_GRADIENT 3

layout(push_constant) uniform SkyPush {
    vec4 colorA;   // solid color / gradient bottom
    vec4 colorB;   // gradient top
    int mode;
    float intensity;
} uSky;

layout(location = 0) in vec2 vNdc;
layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

void main() {
    ViewData view = uFrame.views[gl_ViewIndex];
    vec4 nearPoint = view.invViewProj * vec4(vNdc, 1.0, 1.0);
    vec3 dir = normalize(nearPoint.xyz / nearPoint.w - view.cameraPos.xyz);

    vec3 color;
    if (uSky.mode == SKY_MODE_CUBEMAP) {
        color = texture(uSkyCube, dir).rgb;
    } else if (uSky.mode == SKY_MODE_EQUIRECT) {
        vec2 uv = vec2(atan(dir.z, dir.x) / (2.0 * PI) + 0.5,
                       acos(clamp(dir.y, -1.0, 1.0)) / PI);
        color = texture(uSkyEquirect, uv).rgb;
    } else if (uSky.mode == SKY_MODE_GRADIENT) {
        color = mix(uSky.colorA.rgb, uSky.colorB.rgb, dir.y * 0.5 + 0.5);
    } else {
        color = uSky.colorA.rgb;
    }

    outColor = vec4(color * uSky.intensity, 1.0);
}
