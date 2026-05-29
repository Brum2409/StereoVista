#version 460 core

// ===========================================================================
// DDGI - Probe ray tracing pass.
//
// One invocation per (probe, ray). Each ray is fired from a probe's world
// position in a spherical-Fibonacci direction (rotated by a per-frame random
// rotation so the probe sees all directions over time). The ray is traced
// through the scene BVH; the radiance returned to the probe is:
//     emissive + albedo/pi * directIrradiance + albedo * prevFrameDDGI
// The last term samples the PREVIOUS frame's probes, giving multi-bounce GI
// that converges over time. Results land in the ray-data SSBO (binding 6),
// which the probe-update passes then blend into the octahedral atlases.
// ===========================================================================

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

const float PI = 3.14159265359;

// ---- Scene geometry (shared SSBO bindings with the radiance shader) --------
struct Triangle {
    vec3 v0, v1, v2;
    vec3 normal;
    vec3 color;
    float emissiveness;
    float shininess;
    int materialId;
};
layout(std430, binding = 0) readonly buffer TriangleBuffer { Triangle triangles[]; };

struct BVHNode {
    vec3 minBounds;
    uint leftFirst;
    vec3 maxBounds;
    uint triCount;
};
layout(std430, binding = 1) readonly buffer BVHNodeBuffer { BVHNode bvhNodes[]; };
layout(std430, binding = 2) readonly buffer TriangleIndexBuffer { uint triangleIndices[]; };

// ---- DDGI ray output (rgb = radiance, w = signed hit distance) -------------
// Negative distance flags a back-face hit (probe likely inside geometry).
layout(std430, binding = 6) writeonly buffer RayDataBuffer { vec4 rayData[]; };

// ---- Previous-frame probe atlases (sampled for the recursive bounce) -------
uniform sampler2D ddgi_irradianceMap; // texture unit set from C++
uniform sampler2D ddgi_depthMap;

// ---- DDGI volume layout ----------------------------------------------------
uniform vec3 ddgi_gridStart;
uniform vec3 ddgi_gridStep;
uniform ivec3 ddgi_probeCounts;
uniform int ddgi_raysPerProbe;
uniform int ddgi_irradianceSide;
uniform int ddgi_depthSide;
uniform vec2 ddgi_irradianceTexels;
uniform vec2 ddgi_depthTexels;
uniform float ddgi_normalBias;

// ---- Scene/trace parameters ------------------------------------------------
uniform int numTriangles;
uniform int numBVHNodes;
uniform bool enableBVH;
uniform float rayMaxDistance;
uniform float emissiveIntensity;
uniform float skyIntensity;
uniform mat3 u_randomRotation;

// ---- Lights ----------------------------------------------------------------
const int MAX_POINT_LIGHTS = 16;
struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
    float linear;
    float quadratic;
};
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform int numPointLights;

struct SpotLight {
    vec3 position;
    vec3 direction;
    vec3 color;
    float intensity;
    float innerCutOff;
    float outerCutOff;
};
uniform SpotLight spotLights[MAX_POINT_LIGHTS];
uniform int numSpotLights;

struct Sun {
    vec3 direction;
    vec3 color;
    float intensity;
    bool enabled;
};
uniform Sun sun;

// ===========================================================================
// Ray casting (mirrors radianceFragmentShader.glsl)
// ===========================================================================
struct Ray {
    vec3 origin;
    vec3 direction;
    vec3 invDir;
};

struct HitInfo {
    bool hit;
    float distance;
    vec3 point;
    vec3 normal;
    vec3 albedo;
    float emissiveness;
};

Ray createRay(vec3 origin, vec3 direction) {
    Ray ray;
    ray.origin = origin;
    ray.direction = direction;
    ray.invDir = 1.0 / direction;
    return ray;
}

bool intersectTriangle(Ray ray, Triangle tri, out float t) {
    const float EPSILON = 0.0000001;
    vec3 edge1 = tri.v1 - tri.v0;
    vec3 edge2 = tri.v2 - tri.v0;
    vec3 h = cross(ray.direction, edge2);
    float a = dot(edge1, h);
    if (abs(a) < EPSILON) return false;
    float f = 1.0 / a;
    vec3 s = ray.origin - tri.v0;
    float u = f * dot(s, h);
    if (u < 0.0 || u > 1.0) return false;
    vec3 q = cross(s, edge1);
    float v = f * dot(ray.direction, q);
    if (v < 0.0 || u + v > 1.0) return false;
    t = f * dot(edge2, q);
    return t > EPSILON;
}

bool rayAABBIntersect(Ray ray, vec3 boxMin, vec3 boxMax) {
    vec3 tMin = (boxMin - ray.origin) * ray.invDir;
    vec3 tMax = (boxMax - ray.origin) * ray.invDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);
    return tFar >= tNear && tFar > 0.0;
}

float rayBoundingBoxDistance(Ray ray, vec3 boxMin, vec3 boxMax) {
    vec3 tMin = (boxMin - ray.origin) * ray.invDir;
    vec3 tMax = (boxMax - ray.origin) * ray.invDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);
    bool hit = tFar >= tNear && tFar > 0.0;
    return hit ? (tNear > 0.0 ? tNear : 0.0) : 1.0e30;
}

HitInfo castRayBVH(Ray ray) {
    HitInfo result;
    result.hit = false;
    result.distance = rayMaxDistance;
    if (numBVHNodes == 0) return result;

    uint stack[32];
    int stackIndex = 0;
    stack[stackIndex++] = 0u;

    while (stackIndex > 0) {
        BVHNode node = bvhNodes[stack[--stackIndex]];
        bool isLeaf = node.triCount > 0u;
        if (isLeaf) {
            for (uint i = 0u; i < node.triCount; i++) {
                uint triIdx = triangleIndices[node.leftFirst + i];
                if (triIdx >= numTriangles) continue;
                float t;
                if (intersectTriangle(ray, triangles[triIdx], t) && t < result.distance) {
                    result.hit = true;
                    result.distance = t;
                    result.point = ray.origin + ray.direction * t;
                    result.normal = triangles[triIdx].normal;
                    result.albedo = triangles[triIdx].color;
                    result.emissiveness = triangles[triIdx].emissiveness;
                }
            }
        } else {
            uint childIndexA = node.leftFirst + 0u;
            uint childIndexB = node.leftFirst + 1u;
            if (childIndexA >= numBVHNodes || childIndexB >= numBVHNodes) continue;
            BVHNode childA = bvhNodes[childIndexA];
            BVHNode childB = bvhNodes[childIndexB];
            bool hitA = rayAABBIntersect(ray, childA.minBounds, childA.maxBounds);
            bool hitB = rayAABBIntersect(ray, childB.minBounds, childB.maxBounds);
            if (hitA && hitB) {
                float dstA = rayBoundingBoxDistance(ray, childA.minBounds, childA.maxBounds);
                float dstB = rayBoundingBoxDistance(ray, childB.minBounds, childB.maxBounds);
                if (dstA <= dstB) {
                    if (dstB < result.distance && stackIndex < 31) stack[stackIndex++] = childIndexB;
                    if (dstA < result.distance && stackIndex < 31) stack[stackIndex++] = childIndexA;
                } else {
                    if (dstA < result.distance && stackIndex < 31) stack[stackIndex++] = childIndexA;
                    if (dstB < result.distance && stackIndex < 31) stack[stackIndex++] = childIndexB;
                }
            } else if (hitA) {
                if (stackIndex < 31) stack[stackIndex++] = childIndexA;
            } else if (hitB) {
                if (stackIndex < 31) stack[stackIndex++] = childIndexB;
            }
        }
    }
    return result;
}

HitInfo castRayLinear(Ray ray) {
    HitInfo hit;
    hit.hit = false;
    hit.distance = rayMaxDistance;
    for (int i = 0; i < numTriangles; i++) {
        float t;
        if (intersectTriangle(ray, triangles[i], t) && t < hit.distance) {
            hit.hit = true;
            hit.distance = t;
            hit.point = ray.origin + ray.direction * t;
            hit.normal = triangles[i].normal;
            hit.albedo = triangles[i].color;
            hit.emissiveness = triangles[i].emissiveness;
        }
    }
    return hit;
}

HitInfo castRay(Ray ray) {
    if (enableBVH && numBVHNodes > 0) return castRayBVH(ray);
    return castRayLinear(ray);
}

bool isInShadow(vec3 point, vec3 normal, vec3 lightDir, float lightDistance) {
    Ray shadowRay = createRay(point + 0.002 * normal, lightDir);
    HitInfo shadowHit = castRay(shadowRay);
    return shadowHit.hit && shadowHit.distance < lightDistance;
}

// ===========================================================================
// Octahedral probe addressing (mirrors radiance shader / gi_common.glsl)
// ===========================================================================
vec2 signNotZero(vec2 v) {
    return vec2(v.x >= 0.0 ? 1.0 : -1.0, v.y >= 0.0 ? 1.0 : -1.0);
}

vec2 octEncode(vec3 v) {
    float l1 = abs(v.x) + abs(v.y) + abs(v.z);
    vec2 r = v.xy * (1.0 / l1);
    if (v.z < 0.0) r = (1.0 - abs(r.yx)) * signNotZero(r);
    return r;
}

int probeIndexFromCoord(ivec3 c) {
    return c.x + c.y * ddgi_probeCounts.x + c.z * ddgi_probeCounts.x * ddgi_probeCounts.y;
}

ivec3 probeCoordFromIndex(int idx) {
    int x = idx % ddgi_probeCounts.x;
    int t = idx / ddgi_probeCounts.x;
    int y = t % ddgi_probeCounts.y;
    int z = t / ddgi_probeCounts.y;
    return ivec3(x, y, z);
}

// Atlas UV for a probe's octahedral tile in a given direction.
vec2 probeAtlasUV(int idx, vec3 dir, float side, vec2 atlasTexels) {
    vec2 octUV = octEncode(normalize(dir)) * 0.5 + 0.5; // [0,1]
    vec2 tile = vec2(float(idx % ddgi_probeCounts.x), float(idx / ddgi_probeCounts.x));
    vec2 origin = tile * (side + 2.0) + 1.0; // first interior texel of this tile
    return (origin + octUV * side) / atlasTexels;
}

// ===========================================================================
// DDGI irradiance lookup (previous frame). Mirrors the radiance fragment
// shader's sampleDDGIIrradiance so the recursive bounce matches the final
// shading exactly.
// ===========================================================================
vec3 sampleDDGIIrradiance(vec3 worldPos, vec3 normal) {
    vec3 biased = worldPos + normal * ddgi_normalBias;
    vec3 gridF = (biased - ddgi_gridStart) / ddgi_gridStep;
    ivec3 baseCoord = clamp(ivec3(floor(gridF)), ivec3(0), ddgi_probeCounts - 1);
    vec3 alpha = clamp(gridF - vec3(baseCoord), 0.0, 1.0);

    vec3 sumIrr = vec3(0.0);
    float sumW = 0.0;

    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 probeCoord = clamp(baseCoord + offset, ivec3(0), ddgi_probeCounts - 1);
        int pIdx = probeIndexFromCoord(probeCoord);
        vec3 probePos = ddgi_gridStart + vec3(probeCoord) * ddgi_gridStep;

        vec3 tri = mix(1.0 - alpha, alpha, vec3(offset));
        float weight = tri.x * tri.y * tri.z;

        vec3 dirToProbe = normalize(probePos - worldPos);
        float wrap = (dot(dirToProbe, normal) + 1.0) * 0.5;
        weight *= (wrap * wrap) + 0.2;

        vec3 probeToPoint = biased - probePos;
        float distToProbe = length(probeToPoint);
        vec2 depthUV = probeAtlasUV(pIdx, normalize(probeToPoint), float(ddgi_depthSide), ddgi_depthTexels);
        vec2 moments = texture(ddgi_depthMap, depthUV).rg;
        float mean = moments.x;
        float variance = abs(moments.y - mean * mean);
        float cheb = 1.0;
        if (distToProbe > mean) {
            float v = distToProbe - mean;
            cheb = variance / (variance + v * v);
            cheb = max(cheb * cheb * cheb, 0.0);
        }
        weight *= max(cheb, 0.05);

        const float crush = 0.2;
        if (weight < crush) weight *= (weight * weight) / (crush * crush);
        weight = max(weight, 0.0);

        vec2 irrUV = probeAtlasUV(pIdx, normal, float(ddgi_irradianceSide), ddgi_irradianceTexels);
        vec3 irr = texture(ddgi_irradianceMap, irrUV).rgb;

        sumIrr += irr * weight;
        sumW += weight;
    }

    if (sumW > 0.0) return sumIrr / sumW;
    return vec3(0.0);
}

// ===========================================================================
// Direct lighting (irradiance E, before the /pi BRDF term)
// ===========================================================================
vec3 directIrradiance(vec3 pos, vec3 N) {
    vec3 E = vec3(0.0);

    if (sun.enabled) {
        vec3 L = normalize(-sun.direction);
        float ndl = max(dot(N, L), 0.0);
        if (ndl > 0.0 && !isInShadow(pos, N, L, 1.0e30)) {
            E += sun.color * sun.intensity * ndl;
        }
    }

    for (int i = 0; i < numPointLights; i++) {
        vec3 d = pointLights[i].position - pos;
        float dist = length(d);
        vec3 L = d / max(dist, 1e-4);
        float ndl = max(dot(N, L), 0.0);
        if (ndl <= 0.0) continue;
        float atten = 1.0 / (1.0 + pointLights[i].linear * dist + pointLights[i].quadratic * dist * dist);
        if (!isInShadow(pos, N, L, dist)) {
            E += pointLights[i].color * pointLights[i].intensity * atten * ndl;
        }
    }

    for (int i = 0; i < numSpotLights; i++) {
        vec3 d = spotLights[i].position - pos;
        float dist = length(d);
        vec3 L = d / max(dist, 1e-4);
        float ndl = max(dot(N, L), 0.0);
        if (ndl <= 0.0) continue;
        float theta = dot(normalize(-spotLights[i].direction), L);
        float epsilon = max(spotLights[i].innerCutOff - spotLights[i].outerCutOff, 1e-4);
        float cone = clamp((theta - spotLights[i].outerCutOff) / epsilon, 0.0, 1.0);
        if (cone <= 0.0) continue;
        float atten = 1.0 / (1.0 + 0.09 * dist + 0.032 * dist * dist);
        if (!isInShadow(pos, N, L, dist)) {
            E += spotLights[i].color * spotLights[i].intensity * atten * ndl * cone;
        }
    }

    return E;
}

// Spherical Fibonacci direction distribution (Majercik 2019).
vec3 sphericalFibonacci(float i, float n) {
    const float PHI = sqrt(5.0) * 0.5 + 0.5;
    float frac_i = fract(i * (PHI - 1.0));
    float phi = 2.0 * PI * frac_i;
    float cosTheta = 1.0 - (2.0 * i + 1.0) / n;
    float sinTheta = sqrt(clamp(1.0 - cosTheta * cosTheta, 0.0, 1.0));
    return vec3(cos(phi) * sinTheta, sin(phi) * sinTheta, cosTheta);
}

vec3 skyColor(vec3 dir) {
    float t = 0.5 * (normalize(dir).y + 1.0);
    vec3 col = (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
    return col * skyIntensity;
}

void main() {
    uint gid = gl_GlobalInvocationID.x;
    uint totalProbes = uint(ddgi_probeCounts.x * ddgi_probeCounts.y * ddgi_probeCounts.z);
    uint totalRays = totalProbes * uint(ddgi_raysPerProbe);
    if (gid >= totalRays) return;

    int probeIdx = int(gid / uint(ddgi_raysPerProbe));
    int rayIdx = int(gid % uint(ddgi_raysPerProbe));

    ivec3 pc = probeCoordFromIndex(probeIdx);
    vec3 probePos = ddgi_gridStart + vec3(pc) * ddgi_gridStep;

    vec3 dir = normalize(u_randomRotation * sphericalFibonacci(float(rayIdx), float(ddgi_raysPerProbe)));

    Ray ray = createRay(probePos, dir);
    HitInfo hit = castRay(ray);

    vec4 outData;
    if (!hit.hit) {
        outData = vec4(skyColor(dir), 1.0e4);
    } else {
        bool backface = dot(dir, hit.normal) > 0.0;
        vec3 radiance = vec3(0.0);
        if (!backface) {
            if (hit.emissiveness > 0.0) {
                radiance += hit.albedo * hit.emissiveness * emissiveIntensity;
            }
            radiance += hit.albedo / PI * directIrradiance(hit.point, hit.normal);
            radiance += hit.albedo * sampleDDGIIrradiance(hit.point, hit.normal);
        }
        // Back-face: negate distance so the update shortens perceived geometry
        // distance and skips the irradiance contribution.
        outData = vec4(radiance, backface ? -hit.distance : hit.distance);
    }

    rayData[gid] = outData;
}
