#version 460
#extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
#extension GL_EXT_buffer_reference2 : require
#extension GL_EXT_scalar_block_layout : require

// Unified overlay renderer (Phase 6, playbook C.9) — fragment shading.
// Overlays draw on the BACKBUFFER after tonemap (matching the GL app, where
// tonemap lived inside the mesh shader so overlays also blended over
// display-ready colors): output is straight sRGB-space color, alpha-blended.

#include "overlay_types.h"

layout(buffer_reference, scalar, buffer_reference_align = 8) readonly buffer ViewParamsRO {
    OverlayViewParams v[];
};

layout(push_constant, scalar) uniform Push { OverlayPush pc; };

layout(location = 0) in vec4 vColor;
layout(location = 1) in vec2 vUV;
layout(location = 2) in vec3 vWorldPos;
layout(location = 3) in vec3 vNormal;

layout(location = 0) out vec4 outColor;

void main() {
    if (pc.kind == SV_OVERLAY_KIND_MARKER) {
        // Round marker with a dark rim for contrast on any background
        // (port of the GL MeasurementTool gl_PointCoord shader).
        float r2 = dot(vUV, vUV);
        if (r2 > 1.0)
            discard;
        float rim = smoothstep(0.45, 0.85, r2);
        outColor = vec4(mix(vColor.rgb, vec3(0.05), rim), vColor.a);
        return;
    }

    if (pc.kind == SV_OVERLAY_KIND_SPHERE) {
        // Port of assets/shaders/cursors/sphereFragmentShader.glsl: soft view-
        // dependent transparency with a rim-light so the cursor reads as a
        // glassy ball. paramsA = (transparency, edgeSoftness,
        // centerTransparency, isInner).
        vec3 viewPos = ViewParamsRO(pc.viewParams).v[pc.viewIndex].cameraPos.xyz;
        vec3 norm = normalize(vNormal);
        vec3 viewDir = normalize(viewPos - vWorldPos);
        float alpha = vColor.a * pc.paramsA.x;
        if (pc.paramsA.w < 0.5) {
            // GL note: the reference shader's distFromCenter/radius term is
            // constant across the sphere surface, so the visible "center
            // transparency" really comes from the view-angle edge factor —
            // this is the same curve the GL cursor produced.
            float edgeFactor = pow(1.0 - abs(dot(viewDir, norm)), pc.paramsA.y);
            alpha *= mix(pc.paramsA.z, 1.0, edgeFactor);
        }
        float rimFactor = 1.0 - max(0.0, dot(viewDir, norm));
        vec3 rgb = vColor.rgb + vColor.rgb * pow(rimFactor, 3.0) * 0.3;
        outColor = vec4(rgb, alpha);
        return;
    }

    if (pc.kind == SV_OVERLAY_KIND_DISC) {
        // Camera-facing disc. paramsA.x = 1: hard-edged flat color (in-scene
        // PlaneCursor); 0: soft edge + brighter rim (CursorPreview3D look).
        float r = length(vUV);
        float aa = max(fwidth(r), 1e-4);
        float disc = 1.0 - smoothstep(1.0 - aa * 1.5, 1.0, r);
        if (disc <= 0.001)
            discard;
        if (pc.paramsA.x > 0.5) {
            outColor = vec4(vColor.rgb, vColor.a * disc);
            return;
        }
        float rim = smoothstep(0.80, 0.96, r) * (1.0 - smoothstep(0.985, 1.0, r));
        outColor = vec4(vColor.rgb + rim * 0.55, vColor.a * disc);
        return;
    }

    if (pc.kind == SV_OVERLAY_KIND_RINGS) {
        // Concentric ring pair (CursorPreview3D fragment cursor):
        // paramsA = (outerR, outerT, innerR, innerT), colors from the push.
        float r = length(vUV);
        float aa = max(fwidth(r), 1e-4);
        float ot = max(pc.paramsA.y, aa);
        float lo = pc.paramsA.x - ot;
        float ob = clamp(smoothstep(lo - aa, lo, r) -
                             smoothstep(pc.paramsA.x - aa, pc.paramsA.x, r),
                         0.0, 1.0);
        float it = max(pc.paramsA.w, aa);
        float li = pc.paramsA.z - it;
        float ib = clamp(smoothstep(li - aa, li, r) -
                             smoothstep(pc.paramsA.z - aa, pc.paramsA.z, r),
                         0.0, 1.0);
        float oa = ob * pc.colorA.a;
        float ia = ib * pc.colorB.a;
        vec3 rgb = pc.colorB.rgb * ia;
        float a = ia;
        rgb = pc.colorA.rgb * oa + rgb * (1.0 - oa);
        a = oa + a * (1.0 - oa);
        if (a <= 0.001)
            discard;
        // Convert premultiplied composite back to straight alpha for the
        // standard blend state.
        outColor = vec4(rgb / max(a, 1e-4), a);
        return;
    }

    if (pc.kind == SV_OVERLAY_KIND_BACKGROUND) {
        // Vertical gradient + gentle vignette (CursorPreview3D backdrop).
        vec3 c = mix(pc.colorB.rgb, pc.colorA.rgb, vUV.y);
        float d = distance(vUV, vec2(0.5));
        c *= 1.0 - smoothstep(0.45, 0.95, d) * 0.35;
        outColor = vec4(c, 1.0);
        return;
    }

    // FLAT / LINE: plain vertex color.
    outColor = vColor;
}
