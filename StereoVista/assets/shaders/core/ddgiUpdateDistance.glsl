#version 460 core

// ===========================================================================
// DDGI - Depth / visibility probe update.
//
// One invocation per INTERIOR octahedral texel of the depth atlas. Stores
// (mean distance, mean distance^2) per direction using a sharp cosine power
// weight, for the Chebyshev (variance) visibility test at sampling time.
// All rays (including back-face hits) contribute their |distance|; misses
// contribute the clamped max distance.
// ===========================================================================

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

const float PI = 3.14159265359;

layout(rg16f, binding = 1) uniform image2D u_depth;
layout(std430, binding = 6) readonly buffer RayDataBuffer { vec4 rayData[]; };

uniform ivec3 ddgi_probeCounts;
uniform int ddgi_raysPerProbe;
uniform int ddgi_depthSide;
uniform mat3 u_randomRotation;
uniform float u_hysteresis;
uniform bool u_firstFrame;
uniform float u_depthSharpness;
uniform float u_maxDistance;

vec2 signNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec3 octDecode(vec2 o) {
    vec3 v = vec3(o.xy, 1.0 - abs(o.x) - abs(o.y));
    if (v.z < 0.0) v.xy = (1.0 - abs(v.yx)) * signNotZero(v.xy);
    return normalize(v);
}

vec3 sphericalFibonacci(float i, float n) {
    const float PHI = sqrt(5.0) * 0.5 + 0.5;
    float frac_i = fract(i * (PHI - 1.0));
    float phi = 2.0 * PI * frac_i;
    float cosTheta = 1.0 - (2.0 * i + 1.0) / n;
    float sinTheta = sqrt(clamp(1.0 - cosTheta * cosTheta, 0.0, 1.0));
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

void main() {
    int S = ddgi_depthSide;
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    int interiorW = ddgi_probeCounts.x * S;
    int interiorH = ddgi_probeCounts.y * ddgi_probeCounts.z * S;
    if (gid.x >= interiorW || gid.y >= interiorH) return;

    int tileX = gid.x / S;
    int localX = gid.x % S;
    int tileY = gid.y / S;
    int localY = gid.y % S;
    int probeIdx = tileX + tileY * ddgi_probeCounts.x;

    vec2 octUV = (vec2(localX, localY) + 0.5) / float(S);
    vec3 texelDir = octDecode(octUV * 2.0 - 1.0);

    vec2 sumMoments = vec2(0.0);
    float sumWeight = 0.0;
    int rayBase = probeIdx * ddgi_raysPerProbe;

    for (int r = 0; r < ddgi_raysPerProbe; r++) {
        vec4 data = rayData[rayBase + r];
        float dist = min(abs(data.w), u_maxDistance);
        vec3 rayDir = normalize(u_randomRotation * sphericalFibonacci(float(r), float(ddgi_raysPerProbe)));
        float w = pow(max(0.0, dot(texelDir, rayDir)), u_depthSharpness);
        if (w < 1e-8) continue;
        sumMoments += vec2(dist, dist * dist) * w;
        sumWeight += w;
    }

    vec2 result = (sumWeight > 1e-8) ? (sumMoments / sumWeight) : vec2(u_maxDistance, u_maxDistance * u_maxDistance);

    ivec2 writeCoord = ivec2(tileX * (S + 2) + 1 + localX, tileY * (S + 2) + 1 + localY);
    if (!u_firstFrame) {
        vec2 prev = imageLoad(u_depth, writeCoord).rg;
        result = mix(result, prev, u_hysteresis);
    }
    imageStore(u_depth, writeCoord, vec4(result, 0.0, 0.0));
}
