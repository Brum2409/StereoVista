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

// Scene geometry setup
const int MAX_TRIANGLES = 64; // Limited for performance
uniform Triangle triangles[MAX_TRIANGLES];
uniform int numTriangles;

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

// Enhanced random function for sampling
float random(vec2 co) {
    return fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453);
}

vec2 random2(vec2 co) {
    return vec2(
        fract(sin(dot(co.xy, vec2(12.9898, 78.233))) * 43758.5453),
        fract(sin(dot(co.yx, vec2(39.9898, 58.233))) * 43758.5453)
    );
}


// Ray-triangle intersection using Möller-Trumbore algorithm
bool intersectTriangle(Ray ray, Triangle tri, out float t) {
    const float EPSILON = 0.0000001;
    vec3 edge1 = tri.v1 - tri.v0;
    vec3 edge2 = tri.v2 - tri.v0;
    vec3 h = cross(ray.direction, edge2);
    float a = dot(edge1, h);
    
    if (a > -EPSILON && a < EPSILON) {
        return false; // Ray is parallel to triangle
    }
    
    float f = 1.0 / a;
    vec3 s = ray.origin - tri.v0;
    float u = f * dot(s, h);
    
    if (u < 0.0 || u > 1.0) {
        return false;
    }
    
    vec3 q = cross(s, edge1);
    float v = f * dot(ray.direction, q);
    
    if (v < 0.0 || u + v > 1.0) {
        return false;
    }
    
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

// Cast ray into scene and find closest hit
HitInfo castRay(Ray ray) {
    HitInfo hit;
    hit.hit = false;
    hit.distance = rayMaxDistance;
    
    // Check intersection with triangles (actual scene geometry)
    for (int i = 0; i < numTriangles && i < MAX_TRIANGLES; i++) {
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
    Ray shadowRay;
    shadowRay.origin = point + 0.001 * normal; // Offset to avoid self-intersection
    shadowRay.direction = lightDir;
    
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

// Cosine-weighted hemisphere sampling
vec3 sampleCosineHemisphere(vec3 normal, vec2 rand) {
    float cosTheta = sqrt(1.0 - rand.x);
    float sinTheta = sqrt(rand.x);
    float phi = 2.0 * 3.14159 * rand.y;
    
    vec3 w = normal;
    vec3 u = normalize(cross(abs(w.x) > 0.1 ? vec3(0, 1, 0) : vec3(1, 0, 0), w));
    vec3 v = cross(w, u);
    
    return normalize(u * sinTheta * cos(phi) + v * sinTheta * sin(phi) + w * cosTheta);
}

// Generate random unit vector for Lambert scattering  
vec3 randomUnitVector(vec2 seed) {
    vec2 rand = random2(seed);
    float a = rand.x * 2.0 * 3.14159265;
    float z = rand.y * 2.0 - 1.0;
    float r = sqrt(1.0 - z * z);
    return vec3(r * cos(a), r * sin(a), z);
}

// Iterative ray color calculation with proper GUI setting support
vec3 rayColor(Ray initialRay, int maxDepth) {
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
            finalColor += attenuation * hit.albedo * hit.emissiveness * emissiveIntensity;
        }
        
        // Check if we should continue bouncing (controlled by enableIndirectLighting)
        if (!enableIndirectLighting && depth > 0) {
            // If indirect lighting is disabled, only allow direct hits (depth 0)
            break;
        }
        
        // Update attenuation for next bounce with indirect intensity control
        attenuation *= hit.albedo * 0.5 * indirectIntensity;
        
        // Set up next ray with material roughness affecting scatter
        vec2 seed = hit.point.xy + float(depth) * 123.456;
        vec3 randomDir = randomUnitVector(seed);
        // Blend between perfect reflection and Lambert based on roughness
        vec3 reflectedDir = reflect(currentRay.direction, hit.normal);
        vec3 lambertDir = hit.normal + randomDir;
        vec3 scatterDir = mix(reflectedDir, lambertDir, materialRoughness);
        vec3 target = hit.point + normalize(scatterDir);
        
        currentRay.origin = hit.point + 0.001 * hit.normal;
        currentRay.direction = normalize(scatterDir);
        
        // Russian roulette termination for performance
        float maxComponent = max(max(attenuation.r, attenuation.g), attenuation.b);
        if (maxComponent < 0.1) {
            break;
        }
    }
    
    return finalColor;
}

// Main lighting calculation function
vec3 calculateRadianceLighting(vec3 worldPos, vec3 normal, vec3 materialColor, float shininess, vec3 viewDir) {
    vec3 color = vec3(0.0);
    
    // Sample multiple rays from this point for anti-aliasing and noise reduction
    for (int i = 0; i < samplesPerPixel; i++) {
        // Create a ray from this fragment position
        vec2 seed = worldPos.xy + float(i) * 0.1;
        vec3 target = worldPos + normal + randomUnitVector(seed);
        
        Ray rayBounce;
        rayBounce.origin = worldPos + 0.001 * normal;
        rayBounce.direction = normalize(target - worldPos);
        
        // Trace the ray and accumulate color
        color += materialColor * rayColor(rayBounce, maxBounces);
    }
    
    // Average the samples
    color /= float(samplesPerPixel);
    
    // Add emissive component from this fragment itself
    if (material.emissive > 0.0) {
        color += materialColor * material.emissive;
    }
    
    // Pure raytracing - no direct sun lighting, light only comes from:
    // 1. Emissive objects hit by rays
    // 2. Sky light from rays that miss geometry
    // 3. This fragment's own emissive contribution
    
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
        // Raytracing for lighting calculation starting from the rasterized fragment
        result = calculateRadianceLighting(worldPos, normal, materialColor, material.shininess, viewDir);
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