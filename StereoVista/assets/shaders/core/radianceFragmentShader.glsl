#version 460
out vec4 FragColor;

in VS_OUT {
   vec3 FragPos;
   vec3 Normal;
   vec2 TexCoords;
   vec3 VertexColor;
   float Intensity;
   flat int meshIndex;
} fs_in;

const float PI = 3.14159265359;

// ---- LIGHTING MODE CONSTANTS ----
const int LIGHTING_SHADOW_MAPPING = 0;
const int LIGHTING_VOXEL_CONE_TRACING = 1;
const int LIGHTING_RADIANCE = 2;

// ---- MATERIAL STRUCTURE ----
struct Material {
   sampler2D textures[16];
   int numDiffuseTextures;
   int numSpecularTextures;
   int numNormalTextures;
   bool hasNormalMap;
   bool hasSpecularMap;
   bool hasAOMap;
   float hasTexture;
   vec3 objectColor;
   float shininess;
   float emissive;
};

// ---- DIRECTIONAL LIGHT STRUCTURE ----
struct DirLight {
   vec3 direction;
   vec3 ambient;
   vec3 diffuse;
   vec3 specular;
   float intensity;
};

// ---- UNIFORMS ----
uniform Material material;
uniform DirLight dirLight;
uniform vec3 viewPos;
uniform bool isPointCloud;
uniform bool selectionMode;
uniform bool isSelected;
uniform int selectedMeshIndex;

// Point cloud uniforms
uniform float pointCloudDotSize;
uniform float pointOpacity;
uniform bool intensityColorCoding;

// Fragment cursor uniforms
uniform vec4 cursorPos;
uniform float baseOuterRadius;
uniform float baseOuterBorderThickness;
uniform float baseInnerRadius;
uniform float baseInnerBorderThickness;
uniform vec4 outerCursorColor;
uniform vec4 innerCursorColor;
uniform bool showFragmentCursor;

// Raytracing uniforms
uniform bool enableRaytracing;
uniform int maxBounces;
uniform int samplesPerPixel;
uniform float rayMaxDistance;
uniform bool enableIndirectLighting;
uniform bool enableEmissiveLighting;
uniform float indirectIntensity;
uniform float skyIntensity;
uniform float emissiveIntensity;
uniform float materialRoughness;

// ---- DDGI (Dynamic Diffuse Global Illumination) ----
uniform bool enableDDGI;
uniform sampler2D ddgi_irradianceMap;
uniform sampler2D ddgi_depthMap;
uniform vec3 ddgi_gridStart;
uniform vec3 ddgi_gridStep;
uniform ivec3 ddgi_probeCounts;
uniform int ddgi_irradianceSide;
uniform int ddgi_depthSide;
uniform vec2 ddgi_irradianceTexels;
uniform vec2 ddgi_depthTexels;
uniform float ddgi_normalBias;
uniform float ddgi_viewBias;
uniform float ddgi_giIntensity;
uniform float ddgi_visibilityStrength; // 0 = ignore probe occlusion (no AO), 1 = full

// ---- Soft shadow controls (final shading only; probes use hard shadows) ----
uniform int shadowSamples;       // 1 = hard shadow
uniform float sunAngularRadius;  // radians; angular size of the sun disk
uniform float lightSourceRadius; // world units; radius of point/spot sources

// Scene lights
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

// Sun light
struct Sun {
    vec3 direction;
    vec3 color;
    float intensity;
    bool enabled;
};
uniform Sun sun;

// ---- RAYTRACING STRUCTURES ----
struct Ray {
    vec3 origin;
    vec3 direction;
    vec3 invDir;  // Precomputed inverse direction for AABB tests
};

struct HitInfo {
    bool hit;
    float distance;
    vec3 point;
    vec3 normal;
    vec3 albedo;
    float emissiveness;
    float shininess;
    float roughness;
    int materialId;
};

// Triangle for actual scene geometry
struct Triangle {
    vec3 v0, v1, v2;    // Vertices
    vec3 normal;        // Triangle normal
    vec3 color;         // Material color
    float emissiveness; // Material emissiveness
    float shininess;    // Material shininess
    int materialId;     // Material identifier
};

// BVH Node structure for acceleration
struct BVHNode {
    vec3 minBounds;     // AABB minimum bounds
    uint leftFirst;     // Left child index or first triangle index
    vec3 maxBounds;     // AABB maximum bounds
    uint triCount;      // Triangle count (0 for interior nodes)
};

// Scene geometry setup using Storage Buffer Objects
layout(std430, binding = 0) readonly buffer TriangleBuffer {
    Triangle triangles[];
};
layout(std430, binding = 1) readonly buffer BVHNodeBuffer {
    BVHNode bvhNodes[];
};
layout(std430, binding = 2) readonly buffer TriangleIndexBuffer {
    uint triangleIndices[];
};

uniform int numTriangles;
uniform int numBVHNodes;
uniform bool enableBVH;

// ---- Two-level BVH (TLAS/BLAS) ---------------------------------------------
// When enableTwoLevel is set, bindings 0/1/2 hold CONCATENATED per-model BLAS
// data in LOCAL (object) space, and the TLAS below indexes per-object instances.
// A ray is traced against the TLAS (world space), then transformed into each
// hit instance's local space to traverse that instance's BLAS sub-range.
struct Instance {
    mat4 model;            // object -> world
    mat4 invModel;         // world -> object
    uint blasNodeOffset;   // base into bvhNodes[]   for this BLAS
    uint triOffset;        // base into triangles[]   for this BLAS
    uint triIndexOffset;   // base into triangleIndices[] for this BLAS
    uint pad;
};
layout(std430, binding = 3) readonly buffer TLASNodeBuffer { BVHNode tlasNodes[]; };
layout(std430, binding = 4) readonly buffer InstanceBuffer { Instance instances[]; };
layout(std430, binding = 5) readonly buffer TLASIndexBuffer { uint tlasIndices[]; };
uniform int numTLASNodes;
uniform int numInstances;
uniform bool enableTwoLevel;

// Optional ground plane
struct GroundPlane {
    vec3 point;
    vec3 normal;
    vec3 color;
    float roughness;
};
uniform GroundPlane groundPlane;
uniform bool hasGroundPlane;

// RNG (PCG)
uint nextRandom(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint result = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    result = (result >> 22u) ^ result;
    return result;
}

float randomValue(inout uint state) {
    return float(nextRandom(state)) / 4294967295.0; // 2^32 - 1
}

float random(vec2 co, inout uint state) {
    state ^= uint(dot(co, vec2(12.9898, 78.233)) * 43758.5453);
    return randomValue(state);
}

vec2 random2(vec2 co, inout uint state) {
    return vec2(
        random(co, state),
        randomValue(state)
    );
}

// Ray-triangle intersection (Moller-Trumbore)
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

// Ray-plane intersection for ground plane
bool intersectGroundPlane(Ray ray, GroundPlane plane, out float t) {
    float denom = dot(plane.normal, ray.direction);
    if (abs(denom) < 0.0001) {
        return false; // Ray is parallel to plane
    }

    t = dot(plane.point - ray.origin, plane.normal) / denom;
    return t > 0.001;
}

// Create ray with precomputed inverse direction
Ray createRay(vec3 origin, vec3 direction) {
    Ray ray;
    ray.origin = origin;
    ray.direction = direction;
    ray.invDir = 1.0 / direction;
    return ray;
}

// Fast ray-AABB intersection test (boolean only)
bool rayAABBIntersect(Ray ray, vec3 boxMin, vec3 boxMax) {
    vec3 tMin = (boxMin - ray.origin) * ray.invDir;
    vec3 tMax = (boxMax - ray.origin) * ray.invDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);

    return tFar >= tNear && tFar > 0.0;
}

// Ray-AABB intersection that returns entry distance
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

// BVH traversal
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
                if (triIdx >= numTriangles) continue; // Safety check

                float t;
                if (intersectTriangle(ray, triangles[triIdx], t) && t < result.distance) {
                    result.hit = true;
                    result.distance = t;
                    result.point = ray.origin + ray.direction * t;
                    result.normal = triangles[triIdx].normal;
                    result.albedo = triangles[triIdx].color;
                    result.emissiveness = triangles[triIdx].emissiveness;
                    result.shininess = triangles[triIdx].shininess;
                    result.roughness = 1.0 - clamp(triangles[triIdx].shininess / 256.0, 0.0, 1.0);
                    result.materialId = triangles[triIdx].materialId;
                }
            }
        } else {
            uint childIndexA = node.leftFirst + 0u;
            uint childIndexB = node.leftFirst + 1u;

            if (childIndexA >= numBVHNodes || childIndexB >= numBVHNodes) continue; // Safety check

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

// Linear traversal (fallback)
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
            hit.shininess = triangles[i].shininess;
            hit.roughness = 1.0 - clamp(triangles[i].shininess / 256.0, 0.0, 1.0);
            hit.materialId = triangles[i].materialId;
        }
    }

    return hit;
}

// ---- Two-level BVH traversal ----------------------------------------------
// Traverse one instance's BLAS in its LOCAL space. `lray` is the ray already
// transformed into object space (direction left UN-normalized so the hit
// parameter t stays in the same units as the world ray -- this keeps result.distance
// directly comparable across instances). `wray` is the original world ray, used
// only to reconstruct the world-space hit point. Updates the running closest hit.
void intersectBLAS(Ray lray, Ray wray, uint nodeBase, uint triIdxBase, uint triBase,
                   mat3 normalMat, inout HitInfo result) {
    uint stack[24]; // depth-capped at 20 by the builder -> max ~21 entries
    int sp = 0;
    stack[sp++] = 0u; // BLAS-local root

    while (sp > 0) {
        BVHNode node = bvhNodes[nodeBase + stack[--sp]];
        if (node.triCount > 0u) {
            for (uint i = 0u; i < node.triCount; i++) {
                uint triIdx = triangleIndices[triIdxBase + node.leftFirst + i];
                Triangle tri = triangles[triBase + triIdx];
                float t;
                if (intersectTriangle(lray, tri, t) && t < result.distance) {
                    result.hit = true;
                    result.distance = t;
                    result.point = wray.origin + wray.direction * t;
                    result.normal = normalize(normalMat * tri.normal);
                    result.albedo = tri.color;
                    result.emissiveness = tri.emissiveness;
                    result.shininess = tri.shininess;
                    result.roughness = 1.0 - clamp(tri.shininess / 256.0, 0.0, 1.0);
                    result.materialId = tri.materialId;
                }
            }
        } else {
            uint a = node.leftFirst + 0u;
            uint b = node.leftFirst + 1u;
            BVHNode childA = bvhNodes[nodeBase + a];
            BVHNode childB = bvhNodes[nodeBase + b];
            bool hitA = rayAABBIntersect(lray, childA.minBounds, childA.maxBounds);
            bool hitB = rayAABBIntersect(lray, childB.minBounds, childB.maxBounds);
            if (hitA && hitB) {
                float dstA = rayBoundingBoxDistance(lray, childA.minBounds, childA.maxBounds);
                float dstB = rayBoundingBoxDistance(lray, childB.minBounds, childB.maxBounds);
                if (dstA <= dstB) {
                    if (dstB < result.distance && sp < 23) stack[sp++] = b;
                    if (dstA < result.distance && sp < 23) stack[sp++] = a;
                } else {
                    if (dstA < result.distance && sp < 23) stack[sp++] = a;
                    if (dstB < result.distance && sp < 23) stack[sp++] = b;
                }
            } else if (hitA) {
                if (sp < 23) stack[sp++] = a;
            } else if (hitB) {
                if (sp < 23) stack[sp++] = b;
            }
        }
    }
}

// Walk the TLAS (world space); at each leaf, transform the ray into each
// instance's local space and traverse its BLAS.
HitInfo castRayTwoLevel(Ray ray) {
    HitInfo result;
    result.hit = false;
    result.distance = rayMaxDistance;
    if (numTLASNodes == 0) return result;

    uint stack[24]; // depth-capped at 20 by the builder -> max ~21 entries
    int sp = 0;
    stack[sp++] = 0u;

    while (sp > 0) {
        BVHNode node = tlasNodes[stack[--sp]];
        if (node.triCount > 0u) {
            for (uint i = 0u; i < node.triCount; i++) {
                uint instIdx = tlasIndices[node.leftFirst + i];
                Instance inst = instances[instIdx];
                vec3 lo = (inst.invModel * vec4(ray.origin, 1.0)).xyz;
                vec3 ld = (inst.invModel * vec4(ray.direction, 0.0)).xyz; // not normalized
                Ray lray;
                lray.origin = lo;
                lray.direction = ld;
                lray.invDir = 1.0 / ld;
                mat3 normalMat = transpose(mat3(inst.invModel));
                intersectBLAS(lray, ray, inst.blasNodeOffset, inst.triIndexOffset,
                              inst.triOffset, normalMat, result);
            }
        } else {
            uint a = node.leftFirst + 0u;
            uint b = node.leftFirst + 1u;
            if (a >= numTLASNodes || b >= numTLASNodes) continue;
            BVHNode childA = tlasNodes[a];
            BVHNode childB = tlasNodes[b];
            bool hitA = rayAABBIntersect(ray, childA.minBounds, childA.maxBounds);
            bool hitB = rayAABBIntersect(ray, childB.minBounds, childB.maxBounds);
            if (hitA && hitB) {
                float dstA = rayBoundingBoxDistance(ray, childA.minBounds, childA.maxBounds);
                float dstB = rayBoundingBoxDistance(ray, childB.minBounds, childB.maxBounds);
                if (dstA <= dstB) {
                    if (dstB < result.distance && sp < 23) stack[sp++] = b;
                    if (dstA < result.distance && sp < 23) stack[sp++] = a;
                } else {
                    if (dstA < result.distance && sp < 23) stack[sp++] = a;
                    if (dstB < result.distance && sp < 23) stack[sp++] = b;
                }
            } else if (hitA) {
                if (sp < 23) stack[sp++] = a;
            } else if (hitB) {
                if (sp < 23) stack[sp++] = b;
            }
        }
    }
    return result;
}

// Cast ray and find closest hit
HitInfo castRay(Ray ray) {
    HitInfo hit;

    if (enableTwoLevel && numTLASNodes > 0) {
        hit = castRayTwoLevel(ray);
    } else if (enableBVH && numBVHNodes > 0) {
        hit = castRayBVH(ray);
    } else {
        hit = castRayLinear(ray);
    }

    if (hasGroundPlane) {
        float t;
        if (intersectGroundPlane(ray, groundPlane, t) && t < hit.distance) {
            hit.hit = true;
            hit.distance = t;
            hit.point = ray.origin + ray.direction * t;
            hit.normal = groundPlane.normal;
            hit.albedo = groundPlane.color;
            hit.emissiveness = 0.0;
            hit.shininess = 1.0 / groundPlane.roughness;
            hit.roughness = groundPlane.roughness;
            hit.materialId = -1;
        }
    }

    return hit;
}

// Test for shadow occlusion
bool isInShadow(vec3 point, vec3 normal, vec3 lightDir, float lightDistance) {
    Ray shadowRay = createRay(point + 0.002 * normal, lightDir); // Offset to avoid self-intersection

    HitInfo shadowHit = castRay(shadowRay);
    return shadowHit.hit && shadowHit.distance < lightDistance;
}

// Cosine-weighted hemisphere sampling
vec3 sampleCosineHemisphere(vec3 normal, vec2 rand) {
    float cosTheta = sqrt(rand.x);
    float sinTheta = sqrt(1.0 - rand.x);
    float phi = 2.0 * 3.14159 * rand.y;

    vec3 w = normal;
    vec3 u = normalize(cross(abs(w.x) > 0.1 ? vec3(0, 1, 0) : vec3(1, 0, 0), w));
    vec3 v = cross(w, u);

    return normalize(u * sinTheta * cos(phi) + v * sinTheta * sin(phi) + w * cosTheta);
}

// Random unit vector for Lambert scattering
vec3 randomUnitVector(vec2 seed, inout uint state) {
    vec2 rand = random2(seed, state);
    float a = rand.x * 2.0 * 3.14159265;
    float z = rand.y * 2.0 - 1.0;
    float r = sqrt(1.0 - z * z);
    return vec3(r * cos(a), r * sin(a), z);
}

// Iterative path tracing with PCG RNG (fallback indirect when DDGI is off)
vec3 rayColor(Ray initialRay, int maxDepth, inout uint rngState) {
    vec3 finalColor = vec3(0.0);
    vec3 attenuation = vec3(1.0);
    Ray currentRay = initialRay;

    for (int depth = 0; depth < maxDepth; depth++) {
        HitInfo hit = castRay(currentRay);

        if (!hit.hit || hit.distance > rayMaxDistance) {
            vec3 unitDirection = normalize(currentRay.direction);
            float t = 0.5 * (unitDirection.y + 1.0);
            vec3 skyCol = (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
            finalColor += attenuation * skyCol * skyIntensity;
            break;
        }

        if (enableEmissiveLighting && hit.emissiveness > 0.0) {
            finalColor += attenuation * hit.albedo * hit.emissiveness * emissiveIntensity;
        }

        if (!enableIndirectLighting && depth > 0) {
            break;
        }

        vec3 conservativeAlbedo = clamp(hit.albedo, 0.0, 1.0);
        attenuation *= conservativeAlbedo * indirectIntensity;

        vec2 seed = hit.point.xy + float(depth) * 123.456;
        vec2 rand = random2(seed, rngState);

        vec3 scatterDir;
        float brdfWeight = 1.0;

        float specularAmount = clamp(hit.shininess / 128.0, 0.0, 1.0);
        float diffuseAmount = 1.0 - specularAmount;

        if (specularAmount > 0.8) {
            vec3 reflectedDir = reflect(currentRay.direction, hit.normal);

            if (hit.roughness > 0.01) {
                vec3 roughnessOffset = sampleCosineHemisphere(hit.normal, random2(rand, rngState)) * hit.roughness;
                scatterDir = normalize(reflectedDir + roughnessOffset);
            } else {
                scatterDir = reflectedDir;
            }
            brdfWeight = 1.0;

        } else if (specularAmount < 0.2) {
            scatterDir = sampleCosineHemisphere(hit.normal, rand);
            brdfWeight = 1.0;

        } else {
            scatterDir = sampleCosineHemisphere(hit.normal, rand);

            brdfWeight = diffuseAmount + specularAmount * max(0.0, dot(scatterDir, reflect(currentRay.direction, hit.normal)));
        }

        attenuation *= brdfWeight;

        currentRay = createRay(hit.point + 0.001 * hit.normal, scatterDir);

        float maxComponent = max(max(attenuation.r, attenuation.g), attenuation.b);
        float survivalProbability = min(maxComponent, 0.95);

        if (depth >= 3) {
            vec2 rrSeed = hit.point.yz + float(depth) * 456.789;
            float rrRandom = random(rrSeed, rngState);

            if (rrRandom > survivalProbability) {
                break;
            }
            attenuation /= survivalProbability;
        }
    }

    return finalColor;
}

// ===========================================================================
// DDGI sampling (matches ddgiTraceRays.glsl exactly so the recursive bounce
// captured by the probes equals the final on-screen shading).
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

int ddgiProbeIndex(ivec3 c) {
    return c.x + c.y * ddgi_probeCounts.x + c.z * ddgi_probeCounts.x * ddgi_probeCounts.y;
}

vec2 ddgiProbeAtlasUV(int idx, vec3 dir, float side, vec2 atlasTexels) {
    vec2 octUV = octEncode(normalize(dir)) * 0.5 + 0.5;
    vec2 tile = vec2(float(idx % ddgi_probeCounts.x), float(idx / ddgi_probeCounts.x));
    vec2 origin = tile * (side + 2.0) + 1.0;
    return (origin + octUV * side) / atlasTexels;
}

vec3 sampleDDGIIrradiance(vec3 worldPos, vec3 normal, vec3 viewDir) {
    vec3 biased = worldPos + normal * ddgi_normalBias + viewDir * ddgi_viewBias;
    vec3 gridF = (biased - ddgi_gridStart) / ddgi_gridStep;
    ivec3 baseCoord = clamp(ivec3(floor(gridF)), ivec3(0), ddgi_probeCounts - 1);
    vec3 alpha = clamp(gridF - vec3(baseCoord), 0.0, 1.0);

    vec3 sumIrr = vec3(0.0);
    float sumW = 0.0;

    for (int i = 0; i < 8; i++) {
        ivec3 offset = ivec3(i & 1, (i >> 1) & 1, (i >> 2) & 1);
        ivec3 probeCoord = clamp(baseCoord + offset, ivec3(0), ddgi_probeCounts - 1);
        int pIdx = ddgiProbeIndex(probeCoord);
        vec3 probePos = ddgi_gridStart + vec3(probeCoord) * ddgi_gridStep;

        vec3 tri = mix(1.0 - alpha, alpha, vec3(offset));
        float weight = tri.x * tri.y * tri.z;

        vec3 dirToProbe = normalize(probePos - worldPos);
        float wrap = (dot(dirToProbe, normal) + 1.0) * 0.5;
        weight *= (wrap * wrap) + 0.2;

        vec3 probeToPoint = biased - probePos;
        float distToProbe = length(probeToPoint);
        vec2 depthUV = ddgiProbeAtlasUV(pIdx, normalize(probeToPoint), float(ddgi_depthSide), ddgi_depthTexels);
        vec2 moments = texture(ddgi_depthMap, depthUV).rg;
        float mean = moments.x;
        float variance = abs(moments.y - mean * mean);
        float cheb = 1.0;
        if (distToProbe > mean) {
            float v = distToProbe - mean;
            cheb = variance / (variance + v * v);
            cheb = cheb * cheb; // softer falloff than the classic cubed term
        }
        // Dial occlusion strength down so probe visibility doesn't read as
        // harsh AO; ddgi_visibilityStrength=0 disables it entirely.
        cheb = mix(1.0, max(cheb, 0.05), ddgi_visibilityStrength);
        weight *= cheb;

        // Gentle weight crushing: only suppresses near-zero contributors.
        const float crush = 0.05;
        if (weight < crush) weight *= (weight * weight) / (crush * crush);
        weight = max(weight, 0.0);

        vec2 irrUV = ddgiProbeAtlasUV(pIdx, normal, float(ddgi_irradianceSide), ddgi_irradianceTexels);
        vec3 irr = texture(ddgi_irradianceMap, irrUV).rgb;

        sumIrr += irr * weight;
        sumW += weight;
    }

    if (sumW > 0.0) return sumIrr / sumW;
    return vec3(0.0);
}

// ===========================================================================
// Soft shadows (final shading): jitter the shadow ray over the light's solid
// angle using a low-discrepancy Vogel disk so few samples stay smooth.
// ===========================================================================
vec2 vogelDisk(int i, int n, float phase) {
    float r = sqrt((float(i) + 0.5) / float(n));
    float theta = float(i) * 2.39996323 + phase; // golden angle
    return vec2(r * cos(theta), r * sin(theta));
}

// Returns light visibility in [0,1]. coneRadius is the perpendicular offset
// added to the unit light direction (== tan(half-angle) of the source).
float softShadowVisibility(vec3 pos, vec3 N, vec3 L, float lightDist,
                           float coneRadius, int samples, float phase) {
    if (samples <= 1 || coneRadius <= 0.0) {
        return isInShadow(pos, N, L, lightDist) ? 0.0 : 1.0;
    }
    vec3 up = abs(L.y) < 0.99 ? vec3(0.0, 1.0, 0.0) : vec3(1.0, 0.0, 0.0);
    vec3 t = normalize(cross(up, L));
    vec3 b = cross(L, t);
    float vis = 0.0;
    for (int i = 0; i < samples; i++) {
        vec2 d = vogelDisk(i, samples, phase) * coneRadius;
        vec3 jL = normalize(L + t * d.x + b * d.y);
        vis += isInShadow(pos, N, jL, lightDist) ? 0.0 : 1.0;
    }
    return vis / float(samples);
}

// ===========================================================================
// Direct lighting (returns irradiance E, before the /pi Lambert term)
// ===========================================================================
vec3 directIrradiance(vec3 pos, vec3 N, inout uint rngState) {
    vec3 E = vec3(0.0);
    float phase = randomValue(rngState) * 6.28318530718;

    if (sun.enabled) {
        vec3 L = normalize(-sun.direction);
        float ndl = max(dot(N, L), 0.0);
        if (ndl > 0.0) {
            float vis = softShadowVisibility(pos, N, L, 1.0e30,
                                             sunAngularRadius, shadowSamples, phase);
            E += sun.color * sun.intensity * ndl * vis;
        }
    }

    for (int i = 0; i < numPointLights; i++) {
        vec3 d = pointLights[i].position - pos;
        float dist = length(d);
        vec3 L = d / max(dist, 1e-4);
        float ndl = max(dot(N, L), 0.0);
        if (ndl <= 0.0) continue;
        float atten = 1.0 / (1.0 + pointLights[i].linear * dist + pointLights[i].quadratic * dist * dist);
        // Penumbra grows with source size relative to distance.
        float coneRadius = lightSourceRadius / max(dist, 1e-3);
        float vis = softShadowVisibility(pos, N, L, dist, coneRadius, shadowSamples, phase);
        E += pointLights[i].color * pointLights[i].intensity * atten * ndl * vis;
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
        float coneRadius = lightSourceRadius / max(dist, 1e-3);
        float vis = softShadowVisibility(pos, N, L, dist, coneRadius, shadowSamples, phase);
        E += spotLights[i].color * spotLights[i].intensity * atten * ndl * cone * vis;
    }

    return E;
}

// Main lighting: direct + indirect (DDGI or path-traced fallback) + emissive
vec3 calculateRadianceLighting(vec3 worldPos, vec3 normal, vec3 materialColor, vec3 viewDir, inout uint rngState) {
    // Direct diffuse lighting (Lambert outgoing = albedo/pi * E)
    vec3 result = materialColor / PI * directIrradiance(worldPos, normal, rngState);

    // Indirect diffuse
    if (enableDDGI) {
        vec3 indirect = sampleDDGIIrradiance(worldPos, normal, viewDir);
        result += materialColor * indirect * ddgi_giIntensity;
    } else if (enableIndirectLighting) {
        // Path-traced fallback
        int samples = max(samplesPerPixel, 1);
        vec3 indirect = vec3(0.0);
        for (int i = 0; i < samples; i++) {
            vec2 seed = worldPos.xy + float(i) * 0.1;
            vec2 rand = random2(seed, rngState);
            vec3 rayDir = sampleCosineHemisphere(normal, rand);
            indirect += materialColor * rayColor(createRay(worldPos + 0.001 * normal, rayDir), maxBounces, rngState);
        }
        result += indirect / float(samples);
    }

    // Emissive
    if (material.emissive > 0.0) {
        result += materialColor * material.emissive * emissiveIntensity;
    }

    return result;
}

void main() {
    if (isPointCloud) {
        FragColor = vec4(fs_in.VertexColor * fs_in.Intensity, 1.0);
        return;
    }

    vec3 materialColor = material.objectColor;

    if (material.hasTexture > 0.5 && material.numDiffuseTextures > 0) {
        vec4 texColor = texture(material.textures[0], fs_in.TexCoords);
        materialColor = texColor.rgb;
    }

    vec3 worldPos = fs_in.FragPos;
    vec3 normal = normalize(fs_in.Normal);
    vec3 viewDir = normalize(viewPos - worldPos);

    vec3 result = vec3(0.0);

    if (enableRaytracing) {
        ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
        uint pixelIndex = uint(pixelCoord.y * 1920 + pixelCoord.x); // Approx. screen width
        uint rngState = pixelIndex + uint(gl_FragCoord.x * gl_FragCoord.y) * 719393u;

        result = calculateRadianceLighting(worldPos, normal, materialColor, viewDir, rngState);
    } else {
        result = materialColor * 0.3;

        if (material.emissive > 0.0) {
            result += materialColor * material.emissive;
        }
    }

    if (selectionMode && isSelected) {
        if (selectedMeshIndex == -1 || selectedMeshIndex == fs_in.meshIndex) {
            result = mix(result, vec3(1.0, 0.0, 0.0), 0.3);
        }
    }

    result = result / (result + vec3(1.0));
    result = pow(result, vec3(1.0/2.2));

    FragColor = vec4(result, 1.0);

    if (showFragmentCursor && cursorPos.w > 0.5) {
        float distanceToCursor = length(cursorPos.xyz - fs_in.FragPos);
        float distanceFromCamera = length(cursorPos.xyz - viewPos);

        float scaleFactor = distanceFromCamera;
        float outerRadius = baseOuterRadius * scaleFactor;
        float outerBorderThickness = baseOuterBorderThickness * scaleFactor;
        float innerRadius = baseInnerRadius * scaleFactor;
        float innerBorderThickness = baseInnerBorderThickness * scaleFactor;

        float tOuter = step(outerRadius - outerBorderThickness, distanceToCursor) -
                      step(outerRadius, distanceToCursor);
        float tInner = step(innerRadius - innerBorderThickness, distanceToCursor) -
                      step(innerRadius, distanceToCursor);

        vec4 inner = mix(FragColor, innerCursorColor, tInner);
        FragColor = mix(inner, outerCursorColor, tOuter);
    }
}
