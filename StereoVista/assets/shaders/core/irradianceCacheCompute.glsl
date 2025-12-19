#version 460 core

layout(local_size_x = 64, local_size_y = 1, local_size_z = 1) in;

// Irradiance cache entry structure
struct IrradianceCacheEntry {
    vec3 position;
    float harmonicMeanDist;
    vec3 normal;
    float padding1;
    vec3 irradiance;
    float padding2;
};

// Grid cell structure
struct GridCell {
    uint entryStart;
    uint entryCount;
};

// Cache buffer (binding = 3)
layout(std430, binding = 3) buffer IrradianceCacheBuffer {
    uint cacheEntryCount;
    uint maxCacheEntries;
    uint padding1, padding2;
    IrradianceCacheEntry entries[];
};

// Grid cell buffer (binding = 4)
layout(std430, binding = 4) buffer GridCellBuffer {
    GridCell cells[];
};

// Indirection list (binding = 5)
layout(std430, binding = 5) buffer GridIndirectionBuffer {
    uint entryIndices[];
};

// Triangle structure (must match radianceFragmentShader.glsl)
struct Triangle {
    vec3 v0, v1, v2;    // Vertices
    vec3 normal;        // Triangle normal
    vec3 color;         // Material color
    float emissiveness; // Material emissiveness
    float shininess;    // Material shininess
    int materialId;     // Material identifier
};

// Triangle buffer (binding = 0, same as fragment shader)
layout(std430, binding = 0) readonly buffer TriangleBuffer {
    Triangle triangles[];
};

// BVH Node structure for acceleration
struct BVHNode {
    vec3 minBounds;     // AABB minimum bounds
    uint leftFirst;     // Left child index or first triangle index
    vec3 maxBounds;     // AABB maximum bounds
    uint triCount;      // Triangle count (0 for interior nodes)
};

// BVH buffers (bindings 1 and 2, same as fragment shader)
layout(std430, binding = 1) readonly buffer BVHNodeBuffer {
    BVHNode bvhNodes[];
};
layout(std430, binding = 2) readonly buffer TriangleIndexBuffer {
    uint triangleIndices[];
};

// Uniforms for grid parameters
uniform vec3 gridMin;
uniform vec3 gridMax;
uniform ivec3 gridResolution;

// Sampling parameters
uniform int samplesPerEntry;
uniform float minSpacing;
uniform int numTriangles;
uniform int numBVHNodes;
uniform bool enableBVH;
uniform float rayMaxDistance;

// Random seed for sampling
uniform float randomSeed;

// Convert world position to grid coordinates
ivec3 worldToGrid(vec3 worldPos) {
    vec3 normalized = (worldPos - gridMin) / (gridMax - gridMin);
    normalized = clamp(normalized, 0.0, 0.999);
    return ivec3(normalized * vec3(gridResolution));
}

// Get linear grid index
uint gridIndex(ivec3 coord) {
    if (any(lessThan(coord, ivec3(0))) || any(greaterThanEqual(coord, gridResolution))) {
        return 0xFFFFFFFF; // Invalid
    }
    return uint(coord.x + coord.y * gridResolution.x + coord.z * gridResolution.x * gridResolution.y);
}

// Simple random number generator
float random(inout uint seed) {
    seed = seed * 747796405u + 2891336453u;
    uint result = ((seed >> ((seed >> 28u) + 4u)) ^ seed) * 277803737u;
    result = (result >> 22u) ^ result;
    return float(result) / 4294967295.0;
}

// Sample cosine-weighted hemisphere
vec3 sampleCosineHemisphere(vec3 normal, inout uint seed) {
    float u1 = random(seed);
    float u2 = random(seed);

    float r = sqrt(u1);
    float theta = 2.0 * 3.14159265359 * u2;

    float x = r * cos(theta);
    float y = r * sin(theta);
    float z = sqrt(max(0.0, 1.0 - u1));

    // Create orthonormal basis
    vec3 tangent = abs(normal.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    tangent = normalize(cross(tangent, normal));
    vec3 bitangent = cross(normal, tangent);

    return normalize(x * tangent + y * bitangent + z * normal);
}

// Ray structure
struct Ray {
    vec3 origin;
    vec3 direction;
    vec3 invDir;
};

// Hit info structure
struct HitInfo {
    bool hit;
    float distance;
    vec3 point;
    vec3 normal;
    vec3 albedo;
    float emissiveness;
};

// Create ray with precomputed inverse direction
Ray createRay(vec3 origin, vec3 direction) {
    Ray ray;
    ray.origin = origin;
    ray.direction = direction;
    ray.invDir = 1.0 / direction;
    return ray;
}

// Ray-triangle intersection (MÃ¶llerâ€“Trumbore)
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

// Fast ray-AABB intersection test
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
        }
    }

    return hit;
}

// Cast ray and find closest hit
HitInfo castRay(Ray ray) {
    HitInfo hit;

    if (enableBVH && numBVHNodes > 0) {
        hit = castRayBVH(ray);
    } else {
        hit = castRayLinear(ray);
    }

    return hit;
}

// Check if position is already covered by existing cache entries
bool isCovered(vec3 position, vec3 normal) {
    ivec3 cellCoord = worldToGrid(position);

    // Check 3x3x3 neighborhood
    for (int dz = -1; dz <= 1; dz++) {
        for (int dy = -1; dy <= 1; dy++) {
            for (int dx = -1; dx <= 1; dx++) {
                ivec3 neighborCell = cellCoord + ivec3(dx, dy, dz);
                uint cellIdx = gridIndex(neighborCell);

                if (cellIdx == 0xFFFFFFFF) continue;

                GridCell cell = cells[cellIdx];
                
                // Limit iterations and validate
                uint maxIter = min(cell.entryCount, 16u);
                for (uint i = 0; i < maxIter; i++) {
                    uint entryIdx = entryIndices[cell.entryStart + i];
                    
                    // Skip invalid entries (0xFFFFFFFF is sentinel for uninitialized)
                    if (entryIdx == 0xFFFFFFFF || entryIdx >= cacheEntryCount) continue;
                    
                    IrradianceCacheEntry entry = entries[entryIdx];

                    // Validate entry data
                    if (entry.harmonicMeanDist <= 0.0 || isnan(entry.harmonicMeanDist)) continue;

                    float dist = length(position - entry.position);
                    float normalDot = dot(normal, entry.normal);

                    // Ward's coverage test
                    // CRITICAL FIX: Tighter threshold for better cache density
                    // r < 0.5 = only mark as covered if within 0.5× harmonicMeanDist
                    // normalDot > 0.707 = normals must be within ~45° (stricter than 60°)
                    // This allows more cache entries to be created for better coverage
                    float r = dist / entry.harmonicMeanDist;
                    if (r < 0.5 && normalDot > 0.707 && dist > 0.001) {
                        return true;
                    }
                }
            }
        }
    }

    return false;
}

// Trace ray and get color with emissive contribution
vec3 traceRay(vec3 origin, vec3 direction, out float hitDistance) {
    Ray ray = createRay(origin, direction);
    HitInfo hit = castRay(ray);

    if (hit.hit) {
        hitDistance = hit.distance;

        // Return surface color with emissive contribution
        vec3 color = hit.albedo;
        if (hit.emissiveness > 0.0) {
            color += hit.albedo * hit.emissiveness;
        }
        return color;
    } else {
        // Sky/miss - use simple gradient
        hitDistance = 1000.0;
        float t = direction.y * 0.5 + 0.5;
        return mix(vec3(0.7, 0.7, 1.0), vec3(0.1, 0.1, 0.3), t);
    }
}

// Compute irradiance with harmonic mean distance
// Fixed to properly implement Ward's algorithm - only use actual geometry hits
vec3 computeIrradiance(vec3 position, vec3 normal, out float harmonicMeanDist) {
    uint seed = uint(gl_GlobalInvocationID.x * 1973 + gl_GlobalInvocationID.y * 9277 + randomSeed * 26699);

    vec3 irradiance = vec3(0.0);
    float harmonicSum = 0.0;
    int validSamples = 0;

    for (int i = 0; i < samplesPerEntry; i++) {
        vec3 direction = sampleCosineHemisphere(normal, seed);

        float hitDist;
        vec3 rayColor = traceRay(position + normal * 0.001, direction, hitDist);

        float cosTheta = max(0.0, dot(direction, normal));
        irradiance += rayColor * cosTheta;

        // CRITICAL FIX: Only count hits to actual geometry for harmonic mean
        // Sky hits (>= 100 units) are ignored to prevent artificial large patch sizes
        // This follows Ward's algorithm - harmonic mean represents local feature size
        if (hitDist < 100.0) {
            harmonicSum += 1.0 / hitDist;
            validSamples++;
        }
    }

    irradiance /= float(samplesPerEntry);

    // Compute harmonic mean from valid geometry hits only
    if (validSamples > 0) {
        harmonicMeanDist = float(validSamples) / harmonicSum;
        // Cap at reasonable maximum to prevent pathological values
        harmonicMeanDist = min(harmonicMeanDist, 50.0);
    } else {
        // No nearby geometry found - use moderate default value
        // This prevents huge patches in open areas
        harmonicMeanDist = 20.0;
    }

    return irradiance;
}

void main() {
    uint triangleIdx = gl_GlobalInvocationID.x;

    // Bounds check
    if (triangleIdx >= numTriangles) {
        return;
    }

    // CRITICAL FIX: Sample every 4th triangle for better coverage
    // Was 8th, but this left too many gaps - doubling density improves quality
    if ((triangleIdx % 4) != 0) {
        return;
    }

    Triangle tri = triangles[triangleIdx];

    // Use triangle centroid as cache entry position
    vec3 position = (tri.v0 + tri.v1 + tri.v2) / 3.0;

    // CRITICAL FIX: Validate triangle normal BEFORE normalizing to prevent NaN
    // Degenerate triangles (zero-length normals) would produce NaN when normalized
    float normalLength = length(tri.normal);
    if (normalLength < 0.0001) {
        // Skip degenerate triangle - cannot compute valid irradiance
        return;
    }
    vec3 normal = tri.normal / normalLength;  // Safe normalization

    // Validate position is not NaN/Inf
    if (any(isnan(position)) || any(isinf(position))) {
        return;
    }

    // Check if this position needs a cache entry
    if (isCovered(position, normal)) {
        return;
    }

    // Compute irradiance
    float harmonicMeanDist;
    vec3 irradiance = computeIrradiance(position, normal, harmonicMeanDist);

    // CRITICAL: Validate computed values before storing
    // Reject entries with NaN/Inf values that would corrupt interpolation
    if (any(isnan(irradiance)) || any(isinf(irradiance)) ||
        isnan(harmonicMeanDist) || isinf(harmonicMeanDist)) {
        return;
    }

    // If ray tracing produced no lighting, use ambient fallback to prevent invisible geometry
    if (length(irradiance) < 0.001) {
        irradiance = vec3(0.1, 0.1, 0.1); // 10% ambient light
    }

    // Allocate cache entry
    uint entryIdx = atomicAdd(cacheEntryCount, 1);

    if (entryIdx >= maxCacheEntries) {
        return; // Cache full
    }

    // Store entry - initialize ALL fields to prevent garbage in padding
    entries[entryIdx].position = position;
    entries[entryIdx].harmonicMeanDist = harmonicMeanDist;
    entries[entryIdx].normal = normal;
    entries[entryIdx].padding1 = 0.0;
    entries[entryIdx].irradiance = irradiance;
    entries[entryIdx].padding2 = 0.0;

    // Add to grid with proper indirection buffer management
    ivec3 cellCoord = worldToGrid(position);
    uint cellIdx = gridIndex(cellCoord);

    if (cellIdx != 0xFFFFFFFF) {
        // Pre-allocated indirection buffer: each cell gets maxEntriesPerCell slots
        const uint maxEntriesPerCell = 16;  // Maximum entries per grid cell
        uint indirectionBase = cellIdx * maxEntriesPerCell;

        // Atomically allocate space for this entry within the cell
        uint localIdx = atomicAdd(cells[cellIdx].entryCount, 1);

        if (localIdx < maxEntriesPerCell) {
            // Always set entryStart to the fixed indirection base for this cell
            // This is idempotent and safe from race conditions since indirectionBase
            // is always the same for a given cellIdx
            cells[cellIdx].entryStart = indirectionBase;

            // Write entry index to indirection buffer
            entryIndices[indirectionBase + localIdx] = entryIdx;
        }
    }
}
