#version 460
#extension GL_EXT_multiview : require
#extension GL_EXT_scalar_block_layout : require
// Needed to index uTextures[] with a VARIABLE (the material's texture index)
// even though the index is dynamically uniform per draw and therefore needs
// no nonuniformEXT qualifier.
#extension GL_EXT_nonuniform_qualifier : require

// ============================================================================
// Forward lit pass: ONE metallic-roughness Cook-Torrance path (the GL
// uber-shader's parallel Blinn-Phong implementation was deliberately not
// ported — playbook C.3). Output is LINEAR HDR only; tone mapping and sRGB
// encode live exclusively in TonemapPass (C.6).
//
// Materials are bindless (C.8): per-draw data is a material INDEX in the push
// constants; the material carries texture indices into the global texture2D
// array in set 1. Texture indices within a draw are dynamically uniform, so
// no nonuniformEXT qualifier is needed.
//
// Shadows (C.7): principled normal-offset receiver bias (scaled by the
// projected texel's world footprint) + a small depth bias expressed in WORLD
// units (never as a raw NDC constant — see the notes at each site). One
// filtering path per light type: rotated-Vogel-disk PCF through
// hardware-comparison samplers (reverse-Z GREATER_OR_EQUAL: 1 = lit) with a
// 4-tap early bail outside penumbrae, plus optional PCSS contact hardening
// (SV_FRAME_SOFT_SHADOWS): a raw-depth blocker search sizes the disk from
// the world-space receiver-to-blocker distance. The GL magic-number distance fades and
// "edge softness" hacks were dropped. Point shadows compare against real
// hardware depth reconstructed from the face projection — not the GL
// linear-distance gl_FragDepth write. The GL SpotShadowCalculation stub
// (accumulating shadow += 0.0) was deleted, not ported (C.4).
// ============================================================================

#include "scene_types.h"

layout(set = 0, binding = 0, scalar) uniform FrameBlock { FrameData uFrame; };
layout(set = 0, binding = 1, scalar) readonly buffer LightsBlock {
    PointLightData uPointLights[];
};
layout(set = 0, binding = 2, scalar) readonly buffer MaterialsBlock {
    MaterialData uMaterials[];
};
layout(set = 0, binding = 3) uniform sampler2DShadow uSunShadow;
layout(set = 0, binding = 4) uniform samplerCubeArrayShadow uPointShadow;
// Raw (non-comparison) views of the SAME depth maps — the PCSS blocker
// search needs actual depth values, not lit/unlit comparisons.
layout(set = 0, binding = 7) uniform sampler2D uSunShadowRaw;
layout(set = 0, binding = 8) uniform samplerCubeArray uPointShadowRaw;

layout(set = 1, binding = 0) uniform sampler uMaterialSampler;
layout(set = 1, binding = 1) uniform texture2D uTextures[];

layout(push_constant, scalar) uniform PushBlock { DrawPush uPush; };

layout(location = 0) in vec3 vWorldPos;
layout(location = 1) in vec3 vNormal;
layout(location = 2) in vec2 vUV;
layout(location = 3) in vec4 vTangent;

layout(location = 0) out vec4 outColor;

const float PI = 3.14159265359;

// ---- Shadow filter kernels ----
// Interleaved gradient noise: a stable per-screen-pixel rotation for the
// Vogel disks. Grid-aligned kernels alias into blocky steps; a per-pixel
// rotation trades that for fine grain the eye reads as soft.
float shadowRotation(vec2 fragCoord) {
    return 2.0 * PI *
           fract(52.9829189 * fract(dot(fragCoord, vec2(0.06711056, 0.00583715))));
}

// Vogel disk: near-uniform disk coverage at any tap count (golden angle).
vec2 vogelTap(uint i, uint count, float rotation) {
    float r = sqrt((float(i) + 0.5) / float(count));
    float theta = float(i) * 2.39996323 + rotation;
    return r * vec2(cos(theta), sin(theta));
}

const uint SUN_BLOCKER_TAPS = 12u;
const uint SUN_PCF_TAPS = 16u;
const float SUN_MAX_PENUMBRA_TEXELS = 16.0;
const uint POINT_BLOCKER_TAPS = 8u;
const uint POINT_PCF_TAPS = 12u;
const float POINT_MAX_PENUMBRA_TEXELS = 12.0;

vec4 sampleTex(uint index, vec2 uv) {
    return texture(sampler2D(uTextures[index], uMaterialSampler), uv);
}

// ---- Cook-Torrance BRDF (GGX + Smith-Schlick + Schlick Fresnel) ----
float distributionGGX(float NdotH, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NdotH * NdotH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

float geometrySmith(float NdotV, float NdotL, float roughness) {
    float r = roughness + 1.0;
    float k = (r * r) / 8.0;
    float gv = NdotV / (NdotV * (1.0 - k) + k);
    float gl = NdotL / (NdotL * (1.0 - k) + k);
    return gv * gl;
}

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// radiance = light color * intensity * attenuation, L points at the light.
vec3 brdf(vec3 N, vec3 V, vec3 L, vec3 radiance, vec3 albedo, float metallic,
          float roughness, vec3 F0) {
    vec3 H = normalize(V + L);
    float NdotL = max(dot(N, L), 0.0);
    float NdotV = max(dot(N, V), 0.0);
    float D = distributionGGX(max(dot(N, H), 0.0), roughness);
    float G = geometrySmith(NdotV, NdotL, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);
    vec3 specular = (D * G * F) / (4.0 * NdotV * NdotL + 1e-4);
    vec3 kD = (vec3(1.0) - F) * (1.0 - metallic);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

// ---- Shadows ----
// Lit factor for the sun. Receiver gets a normal-offset proportional to the
// shadow texel's world footprint (steeper angle -> bigger offset), then a
// small bias on the reference depth toward the light (reverse-Z: +).
float sunLitFactor(vec3 worldPos, vec3 N, vec3 L) {
    float slope = clamp(1.0 - dot(N, L), 0.0, 1.0);
    vec3 offsetPos =
        worldPos + N * (uFrame.shadowTexelWorldSize * (1.0 + 2.0 * slope));
    vec4 clip = uFrame.sunViewProj * vec4(offsetPos, 1.0); // ortho: w == 1
    vec3 ndc = clip.xyz;
    vec2 uv = ndc.xy * 0.5 + 0.5;
    if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))) ||
        ndc.z <= 0.0 || ndc.z > 1.0)
        return 1.0; // outside the shadow frustum: lit

    // Depth bias in world units (~1-2 shadow texels), converted through the
    // linear ortho depth scale. A raw NDC constant would be worth
    // constant * (far - near) world units — Peter Panning that grows with the
    // fitted scene radius.
    float texel = uFrame.shadowTexelWorldSize;
    float ref = ndc.z + texel * (0.75 + 1.5 * slope) * uFrame.sunWorldToDepth;

    float rot = shadowRotation(gl_FragCoord.xy);
    vec2 texelUV = vec2(1.0 / float(SV_SUN_SHADOW_RESOLUTION));

    // PCSS contact hardening: average blocker distance over the max-penumbra
    // footprint (raw depth reads), then penumbra = tan(sun angular radius) *
    // blocker distance — all in WORLD units via the linear ortho depth (doing
    // this in NDC depth is where the GL version's artifacts came from).
    float radiusTexels = 1.5;
    if ((uFrame.flags & SV_FRAME_SOFT_SHADOWS) != 0u &&
        uFrame.sunPenumbraScale > 0.0) {
        // Slope slack: across the search footprint the receiver's own depth
        // varies by up to ~radius * slope; without this the tilted surface
        // counts as its own blocker and everything grows a false penumbra.
        float searchRef =
            ref + SUN_MAX_PENUMBRA_TEXELS * texel * slope * uFrame.sunWorldToDepth;
        float blockerSum = 0.0;
        uint blockerCount = 0u;
        for (uint i = 0u; i < SUN_BLOCKER_TAPS; ++i) {
            vec2 off = vogelTap(i, SUN_BLOCKER_TAPS, rot) *
                       (SUN_MAX_PENUMBRA_TEXELS * texelUV);
            float d = textureLod(uSunShadowRaw, uv + off, 0.0).r;
            if (d > searchRef) { // decidedly nearer to the sun than we are
                blockerSum += d;
                blockerCount++;
            }
        }
        if (blockerCount == 0u)
            return 1.0; // nothing between us and the sun anywhere nearby
        float blockerDistWorld =
            ((blockerSum / float(blockerCount)) - ndc.z) / uFrame.sunWorldToDepth;
        radiusTexels = clamp(uFrame.sunPenumbraScale * blockerDistWorld / texel,
                             1.0, SUN_MAX_PENUMBRA_TEXELS);
    }

    // Wide kernels need a wider bias: the receiver plane spans more depth
    // across the filter footprint (scales with slope, vanishes head-on).
    ref += radiusTexels * texel * 0.5 * slope * uFrame.sunWorldToDepth;

    // Rotated-Vogel PCF (each tap is a hardware 2x2). The first 4 taps decide
    // whether this pixel is in a penumbra at all — fully lit/shadowed pixels
    // (the vast majority) never pay for the full kernel.
    float sum = 0.0;
    for (uint i = 0u; i < 4u; ++i) {
        vec2 off = vogelTap(i, SUN_PCF_TAPS, rot) * radiusTexels * texelUV;
        sum += texture(uSunShadow, vec3(uv + off, ref));
    }
    if (sum == 0.0 || sum == 4.0)
        return sum * 0.25;
    for (uint i = 4u; i < SUN_PCF_TAPS; ++i) {
        vec2 off = vogelTap(i, SUN_PCF_TAPS, rot) * radiusTexels * texelUV;
        sum += texture(uSunShadow, vec3(uv + off, ref));
    }
    return sum / float(SUN_PCF_TAPS);
}

// Reverse-Z [0,1] reference depth for a cube-face-space depth z, and its
// inverse (what world-space depth does a stored map value correspond to).
float pointZToRef(float z) {
    float nearP = uFrame.pointShadowNear;
    float farP = uFrame.pointShadowFar;
    return nearP * (farP - z) / (z * (farP - nearP));
}

float pointRefToZ(float ref) {
    float nearP = uFrame.pointShadowNear;
    float farP = uFrame.pointShadowFar;
    return nearP * farP / (ref * (farP - nearP) + nearP);
}

// Lit factor for a shadowed point light. Reference depth is the hardware
// depth the caster pass wrote: reverse-Z projection of the cube-face-space
// depth z = max(|d.x|,|d.y|,|d.z|).
float pointLitFactor(vec3 worldPos, vec3 N, vec3 lightPos, int slot,
                     float lightRadius) {
    vec3 toFrag = worldPos - lightPos;
    float dist = length(toFrag);
    float nearP = uFrame.pointShadowNear;
    float farP = uFrame.pointShadowFar;
    if (dist <= nearP || dist >= farP)
        return 1.0;
    vec3 L = -toFrag / dist;
    // Cube texel world footprint at this distance (90-degree face: the image
    // plane spans 2z across SV_POINT_SHADOW_RESOLUTION texels).
    float texelWorld = 2.0 * dist / float(SV_POINT_SHADOW_RESOLUTION);
    float slope = clamp(1.0 - dot(N, L), 0.0, 1.0);
    vec3 dir = (worldPos + N * texelWorld * (1.0 + 1.5 * slope)) - lightPos;
    float z = max(max(abs(dir.x), abs(dir.y)), abs(dir.z));
    // Depth bias in WORLD units, applied to z BEFORE the reverse-Z
    // projection. A constant added to ref instead is worth
    // ref * z^2 * (far - near) / (near * far) world units — tens of
    // centimeters a few meters out, growing quadratically with distance from
    // the light: textbook Peter Panning.
    float zRecv = max(z - texelWorld * (0.5 + slope), nearP);

    // Disk basis perpendicular to the lookup direction; disk offsets are in
    // world units at the receiver's distance.
    vec3 up = abs(dir.z) < 0.9 * z ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 t0 = normalize(cross(up, dir));
    vec3 t1 = cross(normalize(dir), t0);
    float rot = shadowRotation(gl_FragCoord.xy + vec2(31.0 * float(slot + 1)));
    float layer = float(slot);

    // PCSS contact hardening: penumbra = lightRadius * (recv - blocker) /
    // blocker (similar triangles), with recv/blocker as WORLD-space face
    // depths recovered from the stored reverse-Z values.
    float radiusWorld = 1.5 * texelWorld;
    if ((uFrame.flags & SV_FRAME_SOFT_SHADOWS) != 0u && lightRadius > 0.0) {
        float searchWorld = max(lightRadius, 2.0 * texelWorld);
        // Slope slack, same reasoning as the sun path.
        float searchRef = pointZToRef(max(zRecv - searchWorld * slope, nearP));
        float blockerSum = 0.0;
        uint blockerCount = 0u;
        for (uint i = 0u; i < POINT_BLOCKER_TAPS; ++i) {
            vec2 off = vogelTap(i, POINT_BLOCKER_TAPS, rot) * searchWorld;
            vec3 sdir = dir + t0 * off.x + t1 * off.y;
            float d = textureLod(uPointShadowRaw, vec4(sdir, layer), 0.0).r;
            if (d > searchRef) { // decidedly nearer to the light than we are
                blockerSum += pointRefToZ(d);
                blockerCount++;
            }
        }
        if (blockerCount == 0u)
            return 1.0; // no occluder anywhere in the light's footprint
        float zBlocker = blockerSum / float(blockerCount);
        float penumbra = lightRadius * (zRecv - zBlocker) / max(zBlocker, nearP);
        radiusWorld = clamp(penumbra, 0.75 * texelWorld,
                            POINT_MAX_PENUMBRA_TEXELS * texelWorld);
    }

    // Kernel-width bias (receiver depth varies across the disk), then
    // project the receiver depth once.
    float ref = pointZToRef(max(zRecv - radiusWorld * 0.5 * slope, nearP));

    // Rotated-Vogel PCF with the same 4-tap early bail as the sun.
    float sum = 0.0;
    for (uint i = 0u; i < 4u; ++i) {
        vec2 off = vogelTap(i, POINT_PCF_TAPS, rot) * radiusWorld;
        sum += texture(uPointShadow, vec4(dir + t0 * off.x + t1 * off.y, layer), ref);
    }
    if (sum == 0.0 || sum == 4.0)
        return sum * 0.25;
    for (uint i = 4u; i < POINT_PCF_TAPS; ++i) {
        vec2 off = vogelTap(i, POINT_PCF_TAPS, rot) * radiusWorld;
        sum += texture(uPointShadow, vec4(dir + t0 * off.x + t1 * off.y, layer), ref);
    }
    return sum / float(POINT_PCF_TAPS);
}

void main() {
    MaterialData mat = uMaterials[uPush.materialIndex];

    vec3 albedo = mat.baseColor.rgb;
    if (mat.albedoTexture != SV_INVALID_TEXTURE)
        albedo = sampleTex(mat.albedoTexture, vUV).rgb; // sRGB view -> linear
    albedo *= uPush.tint; // per-draw tint (BrushTool color variation)

    float metallic = mat.metallic;
    if (mat.metallicTexture != SV_INVALID_TEXTURE)
        metallic *= sampleTex(mat.metallicTexture, vUV).b;
    metallic = clamp(metallic, 0.0, 1.0);

    float roughness = mat.roughness;
    if (mat.roughnessTexture != SV_INVALID_TEXTURE)
        roughness *= sampleTex(mat.roughnessTexture, vUV).g;
    roughness = clamp(roughness, 0.03, 1.0);

    float ao = 1.0;
    if (mat.aoTexture != SV_INVALID_TEXTURE)
        ao = sampleTex(mat.aoTexture, vUV).r;

    vec3 N = normalize(vNormal);
    if (mat.normalTexture != SV_INVALID_TEXTURE) {
        vec3 T = normalize(vTangent.xyz - dot(vTangent.xyz, N) * N);
        vec3 B = cross(N, T) * vTangent.w;
        vec3 tn = sampleTex(mat.normalTexture, vUV).xyz * 2.0 - 1.0;
        tn.xy *= mat.normalScale;
        N = normalize(mat3(T, B, N) * normalize(tn));
    }

    vec3 camPos = uFrame.views[gl_ViewIndex].cameraPos.xyz;
    vec3 V = normalize(camPos - vWorldPos);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    bool shadows = (uFrame.flags & SV_FRAME_SHADOWS_ENABLED) != 0u;

    vec3 color = uFrame.ambientColor.rgb * albedo * ao;

    if ((uFrame.flags & SV_FRAME_SUN_ENABLED) != 0u) {
        vec3 L = normalize(-uFrame.sunDirection.xyz);
        if (dot(N, L) > 0.0) { // backfacing: BRDF is zero, skip the taps too
            float lit = shadows ? sunLitFactor(vWorldPos, N, L) : 1.0;
            vec3 radiance = uFrame.sunColor.rgb * uFrame.sunColor.w;
            color +=
                brdf(N, V, L, radiance, albedo, metallic, roughness, F0) * lit;
        }
    }

    for (uint i = 0u; i < uFrame.pointLightCount; ++i) {
        PointLightData light = uPointLights[i];
        vec3 toLight = light.position.xyz - vWorldPos;
        float dist = length(toLight);
        float atten = 1.0 / (1.0 + light.attenLinear * dist +
                             light.attenQuadratic * dist * dist);
        float intensity = light.position.w;
        if (intensity * atten < 0.005)
            continue; // below perceptible contribution
        vec3 L = toLight / max(dist, 1e-5);
        if (dot(N, L) <= 0.0)
            continue; // backfacing: BRDF is zero, skip the shadow taps too
        float lit = 1.0;
        if (shadows && light.shadowIndex >= 0)
            lit = pointLitFactor(vWorldPos, N, light.position.xyz,
                                 light.shadowIndex, light.radius);
        vec3 radiance = light.color.rgb * intensity * atten;
        color += brdf(N, V, L, radiance, albedo, metallic, roughness, F0) * lit;
    }

    color += albedo * mat.emissive;

    // Fragment (ring) cursor: two rings painted directly on the surface
    // around the 3D cursor position (port of the GL uber-shader's cursor
    // rings; same camera-distance scaling). Colors arrive linearized, the mix
    // happens pre-tonemap — a small tonal delta vs the GL rings (which mixed
    // after GL's in-shader tonemap) is expected and accepted.
    if (uFrame.showFragmentCursor != 0u && uFrame.cursorPos.w > 0.5) {
        float distanceToCursor = length(uFrame.cursorPos.xyz - vWorldPos);
        float scaleFactor = length(uFrame.cursorPos.xyz - camPos);
        float outerRadius = uFrame.cursorRingParams.x * scaleFactor;
        float outerThickness = uFrame.cursorRingParams.y * scaleFactor;
        float innerRadius = uFrame.cursorRingParams.z * scaleFactor;
        float innerThickness = uFrame.cursorRingParams.w * scaleFactor;
        float tOuter = step(outerRadius - outerThickness, distanceToCursor) -
                       step(outerRadius, distanceToCursor);
        float tInner = step(innerRadius - innerThickness, distanceToCursor) -
                       step(innerRadius, distanceToCursor);
        color = mix(color, uFrame.cursorInnerColor.rgb,
                    tInner * uFrame.cursorInnerColor.a);
        color = mix(color, uFrame.cursorOuterColor.rgb,
                    tOuter * uFrame.cursorOuterColor.a);
    }

    outColor = vec4(color, 1.0);
}
