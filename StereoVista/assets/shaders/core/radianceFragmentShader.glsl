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

// No camera uniforms needed - we use rasterized fragment position

// Scene lights
const int MAX_POINT_LIGHTS = 8;
struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
};
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform int numPointLights;

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

// Ray-triangle intersection (Möller–Trumbore)
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

// Cast ray and find closest hit
HitInfo castRay(Ray ray) {
    HitInfo hit;
    
    if (enableBVH && numBVHNodes > 0) {
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
    Ray shadowRay = createRay(point + 0.001 * normal, lightDir); // Offset to avoid self-intersection
    
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

// Iterative path tracing with PCG RNG
vec3 rayColor(Ray initialRay, int maxDepth, inout uint rngState) {
    vec3 finalColor = vec3(0.0);
    vec3 attenuation = vec3(1.0);
    Ray currentRay = initialRay;
    
    for (int depth = 0; depth < maxDepth; depth++) {
        HitInfo hit = castRay(currentRay);
        
        if (!hit.hit || hit.distance > rayMaxDistance) {
            vec3 unitDirection = normalize(currentRay.direction);
            float t = 0.5 * (unitDirection.y + 1.0);
            vec3 skyColor = (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
            finalColor += attenuation * skyColor * skyIntensity;
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

// Main lighting calculation with PCG RNG
vec3 calculateRadianceLighting(vec3 worldPos, vec3 normal, vec3 materialColor, float shininess, vec3 viewDir, inout uint rngState) {
    vec3 color = vec3(0.0);
    
    for (int i = 0; i < samplesPerPixel; i++) {
        vec2 seed = worldPos.xy + float(i) * 0.1;
        vec2 rand = random2(seed, rngState);
        
        vec3 rayDir = sampleCosineHemisphere(normal, rand);
        
        color += materialColor * rayColor(createRay(worldPos + 0.001 * normal, rayDir), maxBounces, rngState);
    }
    
    color /= float(samplesPerPixel);
    
    if (material.emissive > 0.0) {
        color += materialColor * material.emissive * emissiveIntensity;
    }
    
    return color;
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
        
        result = calculateRadianceLighting(worldPos, normal, materialColor, material.shininess, viewDir, rngState);
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