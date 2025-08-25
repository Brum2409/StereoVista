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

// Simple ground plane for basic scene setup
struct GroundPlane {
    vec3 point;
    vec3 normal;
    vec3 color;
    float roughness;
};
uniform GroundPlane groundPlane;
uniform bool hasGroundPlane;

// ---- RAYTRACING FUNCTIONS ----

// ---- RNG Functions (PCG - fast performance) ----

// PCG (permuted congruential generator) - much faster than sin()
uint nextRandom(inout uint state) {
    state = state * 747796405u + 2891336453u;
    uint result = ((state >> ((state >> 28u) + 4u)) ^ state) * 277803737u;
    result = (result >> 22u) ^ result;
    return result;
}

float randomValue(inout uint state) {
    return float(nextRandom(state)) / 4294967295.0; // 2^32 - 1
}

// Legacy wrapper functions for compatibility with existing BRDF sampling
float random(vec2 co, inout uint state) {
    // Use the co as additional entropy but rely on PCG state
    state ^= uint(dot(co, vec2(12.9898, 78.233)) * 43758.5453);
    return randomValue(state);
}

vec2 random2(vec2 co, inout uint state) {
    return vec2(
        random(co, state),
        randomValue(state)
    );
}


// Optimized ray-triangle intersection (like sample project)
bool intersectTriangle(Ray ray, Triangle tri, out float t) {
    const float EPSILON = 0.0000001;
    vec3 edge1 = tri.v1 - tri.v0;
    vec3 edge2 = tri.v2 - tri.v0;
    vec3 h = cross(ray.direction, edge2);
    float a = dot(edge1, h);
    
    // Early exit for parallel rays
    if (abs(a) < EPSILON) return false;
    
    float f = 1.0 / a;
    vec3 s = ray.origin - tri.v0;
    float u = f * dot(s, h);
    
    // Early exit for u out of bounds
    if (u < 0.0 || u > 1.0) return false;
    
    vec3 q = cross(s, edge1);
    float v = f * dot(ray.direction, q);
    
    // Early exit for v out of bounds  
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

// Create ray with precomputed inverse direction (like sample project)
Ray createRay(vec3 origin, vec3 direction) {
    Ray ray;
    ray.origin = origin;
    ray.direction = direction;
    // Precompute inverse direction with safe division
    ray.invDir = 1.0 / direction;
    return ray;
}

// Fast ray-AABB intersection test (boolean only - like sample project)
bool rayAABBIntersect(Ray ray, vec3 boxMin, vec3 boxMax) {
    vec3 tMin = (boxMin - ray.origin) * ray.invDir;
    vec3 tMax = (boxMax - ray.origin) * ray.invDir;
    vec3 t1 = min(tMin, tMax);
    vec3 t2 = max(tMin, tMax);
    float tNear = max(max(t1.x, t1.y), t1.z);
    float tFar = min(min(t2.x, t2.y), t2.z);
    
    return tFar >= tNear && tFar > 0.0;
}

// Ray-AABB intersection that returns distance (only when needed)
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

// Optimized BVH traversal matching sample project
HitInfo castRayBVH(Ray ray) {
    HitInfo result;
    result.hit = false;
    result.distance = rayMaxDistance;
    
    if (numBVHNodes == 0) return result;
    
    // Smaller stack like in sample project (32 elements is usually sufficient)
    uint stack[32];
    int stackIndex = 0;
    
    // Start with root node (index 0)
    stack[stackIndex++] = 0u;
    
    while (stackIndex > 0) {
        // Pop node from stack
        BVHNode node = bvhNodes[stack[--stackIndex]];
        bool isLeaf = node.triCount > 0u;
        
        if (isLeaf) {
            // Leaf node - test triangles
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
            // Interior node - test children with early rejection (like sample project)
            uint childIndexA = node.leftFirst + 0u;
            uint childIndexB = node.leftFirst + 1u;
            
            if (childIndexA >= numBVHNodes || childIndexB >= numBVHNodes) continue; // Safety check
            
            BVHNode childA = bvhNodes[childIndexA];
            BVHNode childB = bvhNodes[childIndexB];
            
            // Fast intersection test first (cheap early rejection)
            bool hitA = rayAABBIntersect(ray, childA.minBounds, childA.maxBounds);
            bool hitB = rayAABBIntersect(ray, childB.minBounds, childB.maxBounds);
            
            // Only compute distances for children that actually intersect
            if (hitA && hitB) {
                // Both intersect - compute distances for priority ordering
                float dstA = rayBoundingBoxDistance(ray, childA.minBounds, childA.maxBounds);
                float dstB = rayBoundingBoxDistance(ray, childB.minBounds, childB.maxBounds);
                
                // Push farther child first (LIFO stack means closer gets processed first)
                if (dstA <= dstB) {
                    // A is closer, push B first then A
                    if (dstB < result.distance && stackIndex < 31) stack[stackIndex++] = childIndexB;
                    if (dstA < result.distance && stackIndex < 31) stack[stackIndex++] = childIndexA;
                } else {
                    // B is closer, push A first then B
                    if (dstA < result.distance && stackIndex < 31) stack[stackIndex++] = childIndexA;
                    if (dstB < result.distance && stackIndex < 31) stack[stackIndex++] = childIndexB;
                }
            } else if (hitA) {
                // Only A intersects
                if (stackIndex < 31) stack[stackIndex++] = childIndexA;
            } else if (hitB) {
                // Only B intersects
                if (stackIndex < 31) stack[stackIndex++] = childIndexB;
            }
            // If neither intersects, skip both (major optimization!)
        }
    }
    
    return result;
}

// Legacy linear traversal for comparison/fallback
HitInfo castRayLinear(Ray ray) {
    HitInfo hit;
    hit.hit = false;
    hit.distance = rayMaxDistance;
    
    // Check intersection with triangles (actual scene geometry)
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

// Cast ray into scene and find closest hit
HitInfo castRay(Ray ray) {
    HitInfo hit;
    
    // Use BVH traversal if enabled and available, otherwise fall back to linear
    if (enableBVH && numBVHNodes > 0) {
        hit = castRayBVH(ray);
        // Debug: If you want to see if BVH is being used, uncomment next line
        // hit.albedo = vec3(0, 1, 0); // Green tint means BVH is active
    } else {
        hit = castRayLinear(ray);
        // Debug: If you want to see if linear is being used, uncomment next line
        // hit.albedo = vec3(1, 0, 0); // Red tint means linear fallback
    }
    
    // Continue with existing ground plane logic...
    
    // Check intersection with ground plane
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

// Evaluate direct lighting from all scene lights
vec3 evaluateDirectLighting(HitInfo hit, vec3 viewDir) {
    vec3 directLight = vec3(0.0);
    vec3 normal = normalize(hit.normal);
    
    // Sun lighting
    if (sun.enabled) {
        vec3 lightDir = normalize(-sun.direction);
        float NdotL = max(dot(normal, lightDir), 0.0);
        
        if (NdotL > 0.0) {
            bool shadowed = isInShadow(hit.point, normal, lightDir, 1000.0);
            if (!shadowed) {
                // Diffuse
                vec3 diffuse = hit.albedo * sun.color * sun.intensity * NdotL;
                
                // Specular (Blinn-Phong)
                vec3 halfVec = normalize(lightDir + viewDir);
                float NdotH = max(dot(normal, halfVec), 0.0);
                float specular = pow(NdotH, max(hit.shininess, 1.0));
                vec3 specularColor = sun.color * sun.intensity * specular * 0.2;
                
                directLight += diffuse + specularColor;
            }
        }
    }
    
    // Point lights
    for (int i = 0; i < numPointLights && i < MAX_POINT_LIGHTS; i++) {
        vec3 lightVec = pointLights[i].position - hit.point;
        float lightDistance = length(lightVec);
        vec3 lightDir = lightVec / lightDistance;
        float NdotL = max(dot(normal, lightDir), 0.0);
        
        if (NdotL > 0.0) {
            bool shadowed = isInShadow(hit.point, normal, lightDir, lightDistance);
            if (!shadowed) {
                // Attenuation
                float attenuation = 1.0 / (1.0 + 0.09 * lightDistance + 0.032 * lightDistance * lightDistance);
                
                // Diffuse
                vec3 diffuse = hit.albedo * pointLights[i].color * pointLights[i].intensity * NdotL * attenuation;
                
                // Specular (Blinn-Phong)
                vec3 halfVec = normalize(lightDir + viewDir);
                float NdotH = max(dot(normal, halfVec), 0.0);
                float specular = pow(NdotH, max(hit.shininess, 1.0));
                vec3 specularColor = pointLights[i].color * pointLights[i].intensity * specular * 0.2 * attenuation;
                
                directLight += diffuse + specularColor;
            }
        }
    }
    
    return directLight;
}

// Cosine-weighted hemisphere sampling (optimized)
vec3 sampleCosineHemisphere(vec3 normal, vec2 rand) {
    float cosTheta = sqrt(rand.x);
    float sinTheta = sqrt(1.0 - rand.x);
    float phi = 2.0 * 3.14159 * rand.y;
    
    // Build orthonormal basis around normal
    vec3 w = normal;
    vec3 u = normalize(cross(abs(w.x) > 0.1 ? vec3(0, 1, 0) : vec3(1, 0, 0), w));
    vec3 v = cross(w, u);
    
    return normalize(u * sinTheta * cos(phi) + v * sinTheta * sin(phi) + w * cosTheta);
}

// Generate random unit vector for Lambert scattering  
vec3 randomUnitVector(vec2 seed, inout uint state) {
    vec2 rand = random2(seed, state);
    float a = rand.x * 2.0 * 3.14159265;
    float z = rand.y * 2.0 - 1.0;
    float r = sqrt(1.0 - z * z);
    return vec3(r * cos(a), r * sin(a), z);
}

// Iterative ray color calculation with PCG RNG state
vec3 rayColor(Ray initialRay, int maxDepth, inout uint rngState) {
    vec3 finalColor = vec3(0.0);
    vec3 attenuation = vec3(1.0);
    Ray currentRay = initialRay;
    
    for (int depth = 0; depth < maxDepth; depth++) {
        HitInfo hit = castRay(currentRay);
        
        if (!hit.hit || hit.distance > rayMaxDistance) {
            // Hit sky or exceeded max distance - add sky color and stop
            vec3 unitDirection = normalize(currentRay.direction);
            float t = 0.5 * (unitDirection.y + 1.0);
            vec3 skyColor = (1.0 - t) * vec3(1.0, 1.0, 1.0) + t * vec3(0.5, 0.7, 1.0);
            finalColor += attenuation * skyColor * skyIntensity;
            break;
        }
        
        // Add emissive contribution (controlled by enableEmissiveLighting)
        if (enableEmissiveLighting && hit.emissiveness > 0.0) {
            // Emissive surfaces emit light in their surface color
            finalColor += attenuation * hit.albedo * hit.emissiveness * emissiveIntensity;
        }
        
        // Check if we should continue bouncing (controlled by enableIndirectLighting)
        if (!enableIndirectLighting && depth > 0) {
            // If indirect lighting is disabled, only allow direct hits (depth 0)
            break;
        }
        
        // Update attenuation using proper BRDF and Monte Carlo integration
        // For Lambertian BRDF: f_r = albedo/π
        // For cosine-weighted sampling: PDF = cos(θ)/π  
        // Monte Carlo: (f_r * cos(θ)) / PDF = (albedo/π * cos(θ)) / (cos(θ)/π) = albedo
        // This simplification occurs because cos(θ) terms cancel out
        
        // Ensure energy conservation: clamp albedo to [0,1] to prevent energy gain
        vec3 conservativeAlbedo = clamp(hit.albedo, 0.0, 1.0);
        attenuation *= conservativeAlbedo * indirectIntensity;
        
        // Set up next ray with material roughness affecting scatter - use PCG RNG
        vec2 seed = hit.point.xy + float(depth) * 123.456;
        vec2 rand = random2(seed, rngState);
        
        // Implement proper BRDF sampling with consistent weighting
        vec3 scatterDir;
        float brdfWeight = 1.0;
        
        // Use hit.shininess to determine material behavior
        float specularAmount = clamp(hit.shininess / 128.0, 0.0, 1.0);
        float diffuseAmount = 1.0 - specularAmount;
        
        // Use consistent sampling strategy based on material properties
        // This avoids variance from probabilistic sampling without MIS
        
        if (specularAmount > 0.8) {
            // Highly specular material - use reflection with roughness
            vec3 reflectedDir = reflect(currentRay.direction, hit.normal);
            
            if (hit.roughness > 0.01) {
                // Add roughness to reflection
                vec3 roughnessOffset = sampleCosineHemisphere(hit.normal, random2(rand, rngState)) * hit.roughness;
                scatterDir = normalize(reflectedDir + roughnessOffset);
            } else {
                // Perfect mirror reflection
                scatterDir = reflectedDir;
            }
            // Full specular weight for highly specular materials
            brdfWeight = 1.0;
            
        } else if (specularAmount < 0.2) {
            // Highly diffuse material - use cosine-weighted hemisphere sampling
            scatterDir = sampleCosineHemisphere(hit.normal, rand);
            // Full diffuse weight for highly diffuse materials
            brdfWeight = 1.0;
            
        } else {
            // Mixed material - use consistent weighting instead of probabilistic sampling
            // Sample diffuse direction and weight by material balance
            scatterDir = sampleCosineHemisphere(hit.normal, rand);
            
            // Apply consistent weighting based on material properties
            // This maintains energy conservation without variance from probabilistic choices
            brdfWeight = diffuseAmount + specularAmount * max(0.0, dot(scatterDir, reflect(currentRay.direction, hit.normal)));
        }
        
        // Apply BRDF weighting to attenuation
        attenuation *= brdfWeight;
        
        currentRay = createRay(hit.point + 0.001 * hit.normal, scatterDir);
        
        // Russian Roulette termination for performance (unbiased) - use PCG RNG
        // Calculate survival probability based on attenuation strength
        float maxComponent = max(max(attenuation.r, attenuation.g), attenuation.b);
        float survivalProbability = min(maxComponent, 0.95); // Cap at 95% to ensure some termination
        
        // Only apply Russian Roulette after a few bounces to avoid terminating important paths
        if (depth >= 3) {
            // Use PCG RNG for termination decision
            vec2 rrSeed = hit.point.yz + float(depth) * 456.789;
            float rrRandom = random(rrSeed, rngState);
            
            if (rrRandom > survivalProbability) {
                // Terminate path
                break;
            }
            // Path survives - compensate for termination probability to remain unbiased
            // Only divide when path continues (outside the else block)
            attenuation /= survivalProbability;
        }
    }
    
    return finalColor;
}

// Main lighting calculation function with PCG RNG state
vec3 calculateRadianceLighting(vec3 worldPos, vec3 normal, vec3 materialColor, float shininess, vec3 viewDir, inout uint rngState) {
    vec3 color = vec3(0.0);
    
    // Sample multiple rays from this point for anti-aliasing and noise reduction
    for (int i = 0; i < samplesPerPixel; i++) {
        // Create a ray from this fragment position using cosine-weighted sampling
        vec2 seed = worldPos.xy + float(i) * 0.1;
        vec2 rand = random2(seed, rngState);
        
        vec3 rayDir = sampleCosineHemisphere(normal, rand);
        
        // Trace the ray and accumulate color
        color += materialColor * rayColor(createRay(worldPos + 0.001 * normal, rayDir), maxBounces, rngState);
    }
    
    // Average the samples
    color /= float(samplesPerPixel);
    
    // Add emissive component from this fragment itself
    if (material.emissive > 0.0) {
        color += materialColor * material.emissive * emissiveIntensity;
    }
    
    return color;
}



void main() {
    if (isPointCloud) {
        // Point cloud rendering - exact same as standard shader, no modifications
        FragColor = vec4(fs_in.VertexColor * fs_in.Intensity, 1.0);
        return;
    }
    
    // Get material properties from rasterization
    vec3 materialColor = material.objectColor;
    
    // Sample diffuse texture if available
    if (material.hasTexture > 0.5 && material.numDiffuseTextures > 0) {
        vec4 texColor = texture(material.textures[0], fs_in.TexCoords);
        materialColor = texColor.rgb;
    }
    
    vec3 worldPos = fs_in.FragPos;
    vec3 normal = normalize(fs_in.Normal);
    vec3 viewDir = normalize(viewPos - worldPos);
    
    vec3 result = vec3(0.0);
    
    if (enableRaytracing) {
        // Create RNG state for PCG (like sample project)
        ivec2 pixelCoord = ivec2(gl_FragCoord.xy);
        uint pixelIndex = uint(pixelCoord.y * 1920 + pixelCoord.x); // Approximate screen width
        uint rngState = pixelIndex + uint(gl_FragCoord.x * gl_FragCoord.y) * 719393u;
        
        // Raytracing for lighting calculation starting from the rasterized fragment
        result = calculateRadianceLighting(worldPos, normal, materialColor, material.shininess, viewDir, rngState);
    } else {
        // When raytracing is disabled, just show material color with minimal ambient
        result = materialColor * 0.3; // Just ambient material color
        
        // Add emissive component
        if (material.emissive > 0.0) {
            result += materialColor * material.emissive;
        }
    }
    
    // Apply selection highlighting (matches main fragment shader behavior)
    if (selectionMode && isSelected) {
        if (selectedMeshIndex == -1 || selectedMeshIndex == fs_in.meshIndex) {
            result = mix(result, vec3(1.0, 0.0, 0.0), 0.3); // Red highlight like main shader
        }
    }
    
    // Simple tone mapping to prevent over-bright colors
    result = result / (result + vec3(1.0));
    
    // Gamma correction
    result = pow(result, vec3(1.0/2.2));
    
    FragColor = vec4(result, 1.0);
    
    // Apply fragment cursor effect
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