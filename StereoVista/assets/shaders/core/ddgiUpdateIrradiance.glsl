#version 460 core

// ===========================================================================
// DDGI - Irradiance probe update.
//
// One invocation per INTERIOR octahedral texel of the irradiance atlas. Each
// texel integrates this probe's rays weighted by max(0, dot(texelDir, rayDir))
// (the cosine lobe around the texel's direction), normalizes by the weight
// sum, applies a small energy-loss factor, then blends into the previous
// value with temporal hysteresis. Stored value == cosine-weighted average
// incident radiance == E / pi, so final diffuse = albedo * sampledValue.
// ===========================================================================

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

const float PI = 3.14159265359;

layout(rgba16f, binding = 0) uniform image2D u_irradiance;
layout(std430, binding = 6) readonly buffer RayDataBuffer { vec4 rayData[]; };

uniform ivec3 ddgi_probeCounts;
uniform int ddgi_raysPerProbe;
uniform int ddgi_irradianceSide;
uniform mat3 u_randomRotation;
uniform float u_hysteresis;
uniform bool u_firstFrame;

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
    int S = ddgi_irradianceSide;
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    int interiorW = ddgi_probeCounts.x * S;
    int interiorH = ddgi_probeCounts.y * ddgi_probeCounts.z * S;
    if (gid.x >= interiorW || gid.y >= interiorH) return;

    int tileX = gid.x / S;
    int localX = gid.x % S;
    int tileY = gid.y / S;
    int localY = gid.y % S;
    int probeIdx = tileX + tileY * ddgi_probeCounts.x;

    // Texel center direction on the octahedron.
    vec2 octUV = (vec2(localX, localY) + 0.5) / float(S);
    vec3 texelDir = octDecode(octUV * 2.0 - 1.0);

    vec3 sumRadiance = vec3(0.0);
    float sumWeight = 0.0;
    int rayBase = probeIdx * ddgi_raysPerProbe;

    for (int r = 0; r < ddgi_raysPerProbe; r++) {
        vec4 data = rayData[rayBase + r];
        // Skip back-face hits (flagged with negative distance) for irradiance.
        if (data.w < 0.0) continue;
        vec3 rayDir = normalize(u_randomRotation * sphericalFibonacci(float(r), float(ddgi_raysPerProbe)));
        float w = max(0.0, dot(texelDir, rayDir));
        sumRadiance += data.rgb * w;
        sumWeight += w;
    }

    vec3 result = (sumWeight > 1e-8) ? (sumRadiance / sumWeight) : vec3(0.0);
    result *= 0.95; // energy loss per bounce -> prevents runaway feedback

    ivec2 writeCoord = ivec2(tileX * (S + 2) + 1 + localX, tileY * (S + 2) + 1 + localY);
    if (!u_firstFrame) {
        vec3 prev = imageLoad(u_irradiance, writeCoord).rgb;
        result = mix(result, prev, u_hysteresis);
    }
    imageStore(u_irradiance, writeCoord, vec4(result, 1.0));
}
