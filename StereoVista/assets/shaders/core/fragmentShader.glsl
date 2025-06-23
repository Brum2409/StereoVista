#version 460
out vec4 FragColor;

in VS_OUT {
   vec3 FragPos;
   vec3 Normal;
   vec2 TexCoords;
   vec4 FragPosLightSpace;
   vec3 VertexColor;
   float Intensity;
   mat3 TBN;
   flat int meshIndex;
} fs_in;

// ---- LIGHTING MODE CONSTANTS ----
const int LIGHTING_SHADOW_MAPPING = 0;
const int LIGHTING_VOXEL_CONE_TRACING = 1;

// ---- VOXEL CONE TRACING CONSTANTS ----
#define SQRT2 1.414213
#define ISQRT2 0.707106
#define MIPMAP_HARDCAP 5.4f
#define DIFFUSE_INDIRECT_FACTOR 0.52f
#define SPECULAR_FACTOR 4.0f
#define SPECULAR_POWER 65.0f

// ---- MATERIAL STRUCTURE ----
struct Material {
   // Standard material properties
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
   
   // VCT specific properties
   float diffuseReflectivity;
   float specularReflectivity;
   vec3 specularColor;
   float specularDiffusion;
   float refractiveIndex;
   float transparency;
};

// ---- VOXEL CONE TRACING SETTINGS ----
struct VCTSettings {
    bool indirectSpecularLight;
    bool indirectDiffuseLight;
    bool directLight;
    bool shadows;
    // Quality settings
    int diffuseConeCount;    // Number of cones for indirect diffuse
    float tracingMaxDistance; // Maximum distance for cone tracing
    int shadowSampleCount;   // Number of samples for shadow cones
    float shadowStepMultiplier; // Step size multiplier for shadows
};

// ---- LIGHTING STRUCTURES ----
struct PointLight {
   vec3 position;
   vec3 color;
   float intensity;
};

struct Sun {
   vec3 direction;
   vec3 color;
   float intensity;
   bool enabled;
};

// ---- UNIFORMS ----
// Material and texture uniforms
uniform Material material;
uniform sampler2D shadowMap;
uniform samplerCube skybox;
uniform float skyboxIntensity;

// Voxel cone tracing uniforms
uniform sampler3D voxelGrid;
uniform float voxelSize;
uniform VCTSettings vctSettings;

// Lighting configuration
uniform int lightingMode;
uniform bool enableShadows;

#define MAX_LIGHTS 180
uniform PointLight lights[MAX_LIGHTS];
uniform int numLights;
uniform Sun sun;
uniform vec3 viewPos;

// Voxel grid bounds
uniform vec3 gridMin;
uniform vec3 gridMax;
uniform bool enableVoxelVisualization;

// UI and selection uniforms
uniform vec4 cursorPos;
uniform float baseOuterRadius;
uniform float baseOuterBorderThickness;
uniform float baseInnerRadius;
uniform float baseInnerBorderThickness;
uniform vec4 outerCursorColor;
uniform vec4 innerCursorColor;
uniform bool showFragmentCursor;

// Render state uniforms
uniform bool isPointCloud;
uniform bool selectionMode;
uniform bool isSelected;
uniform bool isChunkOutline;
uniform int selectedMeshIndex;
uniform bool isMeshSelected;

// ---- HELPER FUNCTIONS ----
// Returns true if a world position is inside the voxel grid
bool isInVoxelGrid(vec3 worldPos) {
    return all(greaterThanEqual(worldPos, gridMin)) && all(lessThanEqual(worldPos, gridMax));
}

// Convert world position to voxel grid texture coordinates [0,1]
vec3 worldToVoxelCoord(vec3 worldPos) {
    return (worldPos - gridMin) / (gridMax - gridMin);
}

// Returns an orthogonal vector to the input vector
vec3 orthogonal(vec3 u) {
    u = normalize(u);
    vec3 v = vec3(0.99146, 0.11664, 0.05832); // Arbitrary vector
    return abs(dot(u, v)) > 0.99999f ? cross(u, vec3(0, 1, 0)) : cross(u, v);
}

// ---- SHADOW MAPPING FUNCTIONS ----
float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    if (!enableShadows) return 0.0;
    
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0)
        return 0.0;
        
    float currentDepth = projCoords.z;
    float bias = max(0.001 * (1.0 - dot(normal, lightDir)), 0.0005);    
    
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    const int halfKernel = 2;
    
    for(int x = -halfKernel; x <= halfKernel; ++x) {
        for(int y = -halfKernel; y <= halfKernel; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    
    const float sampleCount = pow(2 * halfKernel + 1, 2);
    return shadow / sampleCount;
}

// ---- TRADITIONAL LIGHTING FUNCTIONS ----
vec3 calculatePointLight(PointLight light, vec3 normal, vec3 viewDir, float specularStrength) {
   vec3 lightDir = normalize(light.position - fs_in.FragPos);
   float diff = max(dot(normal, lightDir), 0.0);
   
   vec3 reflectDir = reflect(-lightDir, normal);
   float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
   
   float distance = length(light.position - fs_in.FragPos);
   float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * (distance * distance));
   
   vec3 ambient = 0.1 * light.color * light.intensity;
   vec3 diffuse = diff * light.color * light.intensity;
   vec3 specular = specularStrength * spec * light.color * light.intensity;
   
   return (ambient + diffuse + specular) * attenuation;
}

vec3 calculateSunLight(Sun sun, vec3 normal, vec3 viewDir, float specularStrength, float shadow) {
   if (!sun.enabled) return vec3(0.0);
   
   vec3 lightDir = normalize(-sun.direction);
   float diff = max(dot(normal, lightDir), 0.0);
   
   vec3 reflectDir = reflect(-lightDir, normal);
   float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
   
   vec3 ambient = 0.1 * sun.color * sun.intensity;
   vec3 diffuse = diff * sun.color * sun.intensity;
   vec3 specular = specularStrength * spec * sun.color * sun.intensity;
   
   return ambient + (1.0 - shadow) * (diffuse + specular);
}

vec3 calculateEnvironmentReflection(vec3 normal, float reflectivity) {
    vec3 I = normalize(fs_in.FragPos - viewPos);
    vec3 R = reflect(I, normalize(normal));
    return texture(skybox, R).rgb * reflectivity;
}

vec3 calculateAmbientFromSkybox(vec3 normal) {
    return texture(skybox, normal).rgb * skyboxIntensity;
}

// ---- VOXEL CONE TRACING CORE FUNCTIONS ----
// Returns shadow intensity using cone tracing - ORIGINAL implementation
float traceShadowCone(vec3 from, vec3 direction, float targetDistance) {
    if (!vctSettings.shadows) return 1.0; // No shadow
    
    // Push start position slightly along normal to avoid self-shadowing
    from += normalize(fs_in.Normal) * 0.05f;
    
    float acc = 0.0;
    float dist = 3.0 * voxelSize;
    float stopDist = targetDistance - 16.0 * voxelSize;
    
    // Get configurable parameters
    int maxSamples = vctSettings.shadowSampleCount;
    float stepMultiplier = vctSettings.shadowStepMultiplier;
    
    for (int i = 0; i < maxSamples && dist < stopDist && acc < 0.95; i++) {
        vec3 samplePos = from + dist * direction;
        if (!isInVoxelGrid(samplePos)) break;
        
        vec3 texCoord = worldToVoxelCoord(samplePos);
        float l = pow(dist, 2.0); // Inverse square falloff
        
        // Multi-level sampling improves shadow quality
        float s1 = 0.062 * textureLod(voxelGrid, texCoord, 1.0 + 0.75 * l).a;
        float s2 = 0.135 * textureLod(voxelGrid, texCoord, min(4.5 * l, MIPMAP_HARDCAP)).a;
        float shadow = s1 + s2;
        
        acc += (1.0 - acc) * shadow;
        dist += 0.9 * voxelSize * (1.0 + stepMultiplier * l); // Use configurable step size multiplier
    }
    
    return 1.0 - pow(smoothstep(0.0, 1.0, acc * 1.4), 1.0 / 1.4);
}

// Traces a diffuse voxel cone - optimized with configurable parameters
vec3 traceDiffuseVoxelCone(vec3 from, vec3 direction) {
    direction = normalize(direction);
    const float CONE_SPREAD = 0.325;
    
    vec4 acc = vec4(0.0);
    float dist = 0.25;
    
    // Add jitter to starting distance to reduce banding
    dist += voxelSize * 0.1 * fract(sin(dot(direction.xy, vec2(12.9898, 78.233))) * 43758.5453);
    
    // Use configurable maximum tracing distance
    float maxDist = min(vctSettings.tracingMaxDistance, SQRT2); 
    
    // Fewer iterations with more adaptive step size for better performance
    for (int i = 0; i < 6 && dist < maxDist && acc.a < 0.95; i++) {
        vec3 samplePos = from + dist * direction;
        if (!isInVoxelGrid(samplePos)) break;
        
        vec3 texCoord = worldToVoxelCoord(samplePos);
        float level = log2(1.0 + CONE_SPREAD * dist / voxelSize);
        level = min(level, MIPMAP_HARDCAP);
        
        // Single sample per iteration for better performance
        vec4 voxel = textureLod(voxelGrid, texCoord, level);
        
        // Improved blending factor to reduce cone visibility
        float blendFactor = 0.1 * (1.0 + level);
        acc += blendFactor * voxel * (1.0 - acc.a);
        
        // More graduated step size adjustment
        dist += voxelSize * (1.5 + 0.8 * level);
    }
    
    // Simple tone mapping 
    return pow(acc.rgb * 2.0, vec3(1.4));
}

// Traces a specular voxel cone - optimized with configurable parameters
vec3 traceSpecularVoxelCone(vec3 from, vec3 direction) {
    direction = normalize(direction);

    // We need a larger offset based on the object size to avoid self-reflection
    // This was a key issue - the offset wasn't enough to avoid self-intersection
    const float OFFSET = 16.0 * voxelSize;
    const float STEP = voxelSize;

    // IMPORTANT: Push start position along the reflection direction, not normal
    // This is critical to avoid self-intersection and get the right reflection direction
    from += OFFSET * direction;
    
    vec4 acc = vec4(0.0f);
    float dist = 0.0;  // Start at zero since we already added the offset
    
    // Use configurable maximum tracing distance
    float maxDist = min(vctSettings.tracingMaxDistance, length(gridMax - gridMin) * 0.5);

    // Trace from the offset position along the reflection direction
    while(dist < maxDist && acc.a < 1.0) {
        vec3 samplePos = from + dist * direction;
        if(!isInVoxelGrid(samplePos)) break;
        
        vec3 texCoord = worldToVoxelCoord(samplePos);
        float level = 0.1 * material.specularDiffusion * log2(1.0 + dist / voxelSize);
        
        vec4 voxel = textureLod(voxelGrid, texCoord, min(level, MIPMAP_HARDCAP));
        float f = 1.0 - acc.a;
        
        // This matches the example but with better weighting for distance
        float weight = 0.25 * (1.0 + material.specularDiffusion);
        acc.rgb += weight * voxel.rgb * voxel.a * f;
        acc.a += weight * voxel.a * f;
        
        // Adaptive step size - step size increases with distance for efficiency
        dist += STEP * (1.0 + 0.125 * level);
    }
    
    // Use example formula for final multiplier
    return 1.0 * pow(material.specularDiffusion + 1.0, 0.8) * acc.rgb;
}

vec3 traceRefractiveVoxelCone(vec3 from, vec3 direction) {
    direction = normalize(direction);
    
    // Use larger offset and smaller step size for better quality
    const float OFFSET = 12.0 * voxelSize;
    const float STEP = voxelSize * 0.7;  // Smaller steps for better quality
    
    // Move starting point along the refraction direction to avoid self-intersection
    from += OFFSET * direction;
    
    vec4 acc = vec4(0.0f);
    float dist = 0.0;
    
    // Use configurable maximum tracing distance
    float maxDist = min(vctSettings.tracingMaxDistance, length(gridMax - gridMin) * 0.5);
    
    // Sample larger area for smoother refraction
    while(dist < maxDist && acc.a < 0.95) {
        vec3 samplePos = from + dist * direction;
        if(!isInVoxelGrid(samplePos)) break;
        
        vec3 texCoord = worldToVoxelCoord(samplePos);
        
        // Use lower mipmap levels for refraction for sharper results
        // Reduce specular diffusion influence for cleaner refractions
        float specDiffusion = max(0.05, 0.6 * material.specularDiffusion);
        float level = 0.08 * specDiffusion * log2(1.0 + dist / voxelSize);
        
        // Sample surrounding points and average to reduce noise
        vec4 voxelCenter = textureLod(voxelGrid, texCoord, min(level, MIPMAP_HARDCAP));
        
        // Calculate blending weights
        float weight = 0.3 * (1.0 + 0.5 * specDiffusion);
        float f = 1.0 - acc.a;
        
        // Accumulate color with distance-based weighting to favor closer samples
        float distFactor = 1.0 / (1.0 + 0.01 * dist);
        acc.rgb += weight * distFactor * voxelCenter.rgb * voxelCenter.a * f;
        acc.a += weight * voxelCenter.a * f;
        
        // Adaptive step size - smaller steps at start, larger steps as we go further
        dist += STEP * (1.0 + 0.08 * level + 0.002 * dist);
    }
    
    // For transparency, mix with skybox for cleaner results
    if (acc.a < 0.4) {
        // If we didn't hit much in the voxel grid, sample the skybox
        vec3 skyColor = texture(skybox, direction).rgb;
        acc.rgb = mix(skyColor, acc.rgb, min(1.0, acc.a * 2.5));
        acc.a = 1.0;
    }
    
    return acc.rgb;
}

// Calculate indirect diffuse lighting with configurable number of cones
vec3 calculateIndirectDiffuseLight(vec3 normal, vec3 baseColor) {
    // Weight values for different cone types
    const float w[3] = {1.5, 0.8, 0.6}; // weights (center, side, corner)
    const float ANGLE_MIX = 0.6; // Angle for side cones
    
    // Calculate orthogonal base vectors
    vec3 ortho = normalize(orthogonal(normal));
    vec3 ortho2 = normalize(cross(ortho, normal));
    
    // Calculate corner vectors if needed
    vec3 corner = normalize(0.5 * (ortho + ortho2));
    vec3 corner2 = normalize(0.5 * (ortho - ortho2));
    
    // Start position with offset
    const float CONE_OFFSET = -0.015;
    vec3 N_OFFSET = normal * (1.0 + 3.8 * ISQRT2) * voxelSize;
    vec3 C_ORIGIN = fs_in.FragPos + N_OFFSET;
    
    // Accumulate indirect diffuse light
    vec3 acc = vec3(0.0);
    
    // Get the cone count from settings
    int coneCount = vctSettings.diffuseConeCount;
    
    // Always trace the center cone
    acc += w[0] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * normal, normal);
    
    // Trace side cones if using 5 or more cones
    if (coneCount >= 5) {
        vec3 s1 = normalize(mix(normal, ortho, ANGLE_MIX));
        vec3 s2 = normalize(mix(normal, -ortho, ANGLE_MIX));
        vec3 s3 = normalize(mix(normal, ortho2, ANGLE_MIX));
        vec3 s4 = normalize(mix(normal, -ortho2, ANGLE_MIX));
        
        acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * ortho, s1);
        acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * ortho, s2);
        acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * ortho2, s3);
        acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * ortho2, s4);
    }
    
    // Trace corner cones if using 9 cones
    if (coneCount >= 9) {
        vec3 c1 = normalize(mix(normal, corner, ANGLE_MIX));
        vec3 c2 = normalize(mix(normal, -corner, ANGLE_MIX));
        vec3 c3 = normalize(mix(normal, corner2, ANGLE_MIX));
        vec3 c4 = normalize(mix(normal, -corner2, ANGLE_MIX));
        
        acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * corner, c1);
        acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * corner, c2);
        acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * corner2, c3);
        acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * corner2, c4);
    }
    
    // Add slight noise to break up patterns
    float noise = fract(sin(dot(fs_in.FragPos.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float noiseAmount = 0.03;
    
    // Apply material properties with subtle noise to break patterns
    return DIFFUSE_INDIRECT_FACTOR * material.diffuseReflectivity * 
           acc * baseColor * (1.0 + noise * noiseAmount - noiseAmount/2.0);
}

vec3 calculateIndirectSpecularLight(vec3 viewDirection) {
    // Make sure to use the negative view direction for reflection
    // This was the key issue - viewDirection was not negated properly
    vec3 normal = normalize(fs_in.Normal);
    
    // IMPORTANT: viewDirection should point FROM the camera TO the fragment
    // So we need to make sure it's correctly oriented
    if (dot(viewDirection, fs_in.FragPos - viewPos) < 0.0) {
        viewDirection = -viewDirection;  // Fix direction if needed
    }
    
    // Correctly calculate reflection vector
    vec3 reflection = reflect(viewDirection, normal);
    
    // Now trace the cone in the reflection direction from the fragment position
    return material.specularReflectivity * material.specularColor * 
           traceSpecularVoxelCone(fs_in.FragPos, reflection);
}

vec3 calculateRefractiveLight(vec3 viewDirection) {
    if (material.transparency < 0.01) return vec3(0.0);
    
    vec3 normal = normalize(fs_in.Normal);
    
    // Make sure view direction is coming from the camera
    if (dot(viewDirection, fs_in.FragPos - viewPos) < 0.0) {
        viewDirection = -viewDirection;
    }
    
    // Calculate refraction direction using IOR
    vec3 refraction = refract(viewDirection, normal, 1.0/material.refractiveIndex);
    
    // Handle total internal reflection
    if (length(refraction) < 0.01) {
        // If refraction failed (total internal reflection), use reflection instead
        refraction = reflect(viewDirection, normal);
    }
    
    // Calculate back-facing normals to handle transmitted light properly
    bool isEntering = dot(viewDirection, normal) < 0.0;
    if (!isEntering) {
        // Exiting the medium - flip normal and adjust IOR
        normal = -normal;
        refraction = refract(viewDirection, normal, material.refractiveIndex);
        
        if (length(refraction) < 0.01) {
            refraction = reflect(viewDirection, normal);
        }
    }
    
    // Mix refraction color with material colors
    vec3 baseColor = material.hasTexture > 0.5 ? 
                    texture(material.textures[0], fs_in.TexCoords).rgb : 
                    material.objectColor;
    
    // Get refracted light
    vec3 refractedLight = traceRefractiveVoxelCone(fs_in.FragPos, refraction);
    
    // For glass-like materials, reduce color influence for more realistic look
    float colorInfluence = 0.3;  // How much the material color affects the refraction
    vec3 tintColor = mix(vec3(1.0), material.specularColor, colorInfluence);
    
    // Attenuate based on thickness
    float attenuation = 0.7 + 0.3 * material.transparency; // Higher transparency = less attenuation
    
    return tintColor * refractedLight * attenuation;
}

vec3 calculateVCTSunLight(Sun sun, vec3 viewDirection) {
    if (!sun.enabled) return vec3(0.0);
    
    vec3 normal = normalize(fs_in.Normal);
    vec3 lightDir = normalize(-sun.direction);
    
    // Diffuse lighting
    float diffuseAngle = max(dot(normal, lightDir), 0.0);
    
    // Specular lighting
    vec3 halfwayDir = normalize(lightDir + (-viewDirection));
    float specAngle = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    
    // Refraction
    float refractiveAngle = 0.0;
    if (material.transparency > 0.01) {
        vec3 refraction = refract(viewDirection, normal, 1.0/material.refractiveIndex);
        if (length(refraction) > 0.01) {
            refractiveAngle = max(0.0, material.transparency * dot(refraction, lightDir));
        }
    }
    
    // Shadow calculation
    float shadow = 1.0;
    if (vctSettings.shadows && diffuseAngle * (1.0 - material.transparency) > 0.0) {
        // Sun is directional, so use a large distance
        shadow = traceShadowCone(fs_in.FragPos, lightDir, 100.0);
    }
    
    // Apply shadow to lighting
    diffuseAngle = min(shadow, diffuseAngle);
    specAngle = min(shadow, max(specAngle, refractiveAngle));
    
    // Get base color
    vec3 baseColor = material.hasTexture > 0.5 ? 
                    texture(material.textures[0], fs_in.TexCoords).rgb : 
                    material.objectColor;
    
    // Calculate with material properties
    float df = 1.0 / (1.0 + 0.25 * material.specularDiffusion); // Diffusion factor
    float diffuse = diffuseAngle * (1.0 - material.transparency);
    float specular = 3.0 * pow(specAngle, df * SPECULAR_POWER);
    
    vec3 ambient = 0.1 * sun.color * sun.intensity * baseColor;
    vec3 diff = material.diffuseReflectivity * baseColor * diffuse * sun.intensity;
    vec3 spec = material.specularReflectivity * material.specularColor * specular * sun.intensity;
    
    return ambient + sun.color * (diff + spec);
}

// Calculate VCT direct lighting for point lights
vec3 calculateVCTDirectLight(PointLight light, vec3 viewDirection) {
    vec3 normal = normalize(fs_in.Normal);
    vec3 lightDir = light.position - fs_in.FragPos;
    float distanceToLight = length(lightDir);
    lightDir = normalize(lightDir);
    
    // Diffuse lighting (Lambertian)
    float diffuseAngle = max(dot(normal, lightDir), 0.0);
    
    // Specular lighting
    vec3 halfwayDir = normalize(lightDir + (-viewDirection));
    float specAngle = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    
    // Refraction
    float refractiveAngle = 0.0;
    if (material.transparency > 0.01) {
        vec3 refraction = refract(viewDirection, normal, 1.0/material.refractiveIndex);
        if (length(refraction) > 0.01) {
            refractiveAngle = max(0.0, material.transparency * dot(refraction, lightDir));
        }
    }
    
    // Shadow calculation
    float shadow = 1.0;
    if (vctSettings.shadows && diffuseAngle * (1.0 - material.transparency) > 0.0) {
        shadow = traceShadowCone(fs_in.FragPos, lightDir, distanceToLight);
    }
    
    // Apply shadow to lighting
    diffuseAngle = min(shadow, diffuseAngle);
    specAngle = min(shadow, max(specAngle, refractiveAngle));
    
    // Get base color
    vec3 baseColor = material.hasTexture > 0.5 ? 
                    texture(material.textures[0], fs_in.TexCoords).rgb : 
                    material.objectColor;
    
    // Calculate final lighting with material properties
    float df = 1.0 / (1.0 + 0.25 * material.specularDiffusion); // Diffusion factor
    float diffuse = diffuseAngle * (1.0 - material.transparency);
    float specular = SPECULAR_FACTOR * pow(specAngle, df * SPECULAR_POWER);
    
    vec3 diff = material.diffuseReflectivity * baseColor * diffuse;
    vec3 spec = material.specularReflectivity * material.specularColor * specular;
    
    // Apply attenuation
    float attenuation = 1.0 / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
    
    return attenuation * light.intensity * light.color * (diff + spec);
}

void main() {
    // --- EARLY EXITS for special rendering modes ---
    // Check for point cloud rendering first - quick exit path
    if (isPointCloud) {
        FragColor = vec4(fs_in.VertexColor * fs_in.Intensity, 1.0);
        
        // Handle cursor for point clouds
        if (showFragmentCursor) {
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
            FragColor = mix(inner, outerCursorColor, tOuter * step(0.5, cursorPos.w));
        }
        return;
    }
    
    if (isChunkOutline) {
        FragColor = vec4(1.0, 1.0, 0.0, 1.0);
        return;
    }
    
    // --- COMMON CALCULATIONS - Calculate once, use many times ---
    // Get base color and material properties
    vec3 baseColor = material.hasTexture > 0.5 ? 
                    texture(material.textures[0], fs_in.TexCoords).rgb : 
                    material.objectColor;
    
    float specularStrength = material.hasSpecularMap ? 
                           texture(material.textures[1], fs_in.TexCoords).r : 0.5;
    
    // Calculate normal - do this once
    vec3 normal;
    if (material.hasNormalMap) {
        normal = texture(material.textures[2], fs_in.TexCoords).rgb;
        normal = normalize(fs_in.TBN * (normal * 2.0 - 1.0));
    } else {
        normal = normalize(fs_in.Normal);
    }
    
    // Calculate view direction once
    vec3 viewDir = normalize(fs_in.FragPos - viewPos);
    vec3 result = vec3(0.0);
    
    // --- LIGHTING CALCULATION BASED ON MODE ---
    if (lightingMode == LIGHTING_SHADOW_MAPPING) {
        // ------ SHADOW MAPPING LIGHTING ------
        
        // Calculate shadow factor (only if shadows enabled and sun is active)
        float shadow = 0.0;
        if (enableShadows && sun.enabled) {
            shadow = ShadowCalculation(fs_in.FragPosLightSpace, normal, normalize(-sun.direction));
        }
        
        // Calculate environment lighting
        vec3 reflectionColor = calculateEnvironmentReflection(normal, material.shininess / 128.0);
        vec3 ambientLight = calculateAmbientFromSkybox(normal);
        
        // Sun lighting
        if (sun.enabled) {
            result += calculateSunLight(sun, normal, viewDir, specularStrength, shadow);
        }
        
        // Point lights - limit the number of lights processed based on distance
        int processedLights = min(numLights, MAX_LIGHTS);
        for(int i = 0; i < processedLights; i++) {
            // Skip distant lights for performance
            float lightDistance = distance(lights[i].position, fs_in.FragPos);
            if (lightDistance > 50.0) continue; 
            
            result += calculatePointLight(lights[i], normal, viewDir, specularStrength);
        }
        
        // Combine all lighting
        result *= baseColor;
        result += reflectionColor * specularStrength;
        result += ambientLight * baseColor;
        result += material.emissive * baseColor;
    }
    else if (lightingMode == LIGHTING_VOXEL_CONE_TRACING) {
        // ------ VOXEL CONE TRACING LIGHTING ------
        
        // Start with skybox ambient contribution
        result = calculateAmbientFromSkybox(normal) * baseColor * 0.1;
        
        // Add voxel visualization if enabled
        if (enableVoxelVisualization && isInVoxelGrid(fs_in.FragPos)) {
            vec3 voxelCoord = worldToVoxelCoord(fs_in.FragPos);
            vec4 voxelValue = texture(voxelGrid, voxelCoord);
            if (voxelValue.a > 0.0) {
                result = mix(result, voxelValue.rgb, 0.6);
            }
        }
        else {
            // Add indirect diffuse lighting (global illumination)
            if (vctSettings.indirectDiffuseLight) {
                float diffuseContrib = material.diffuseReflectivity * (1.0 - material.transparency);
                if (diffuseContrib > 0.01) {
                    // Apply a smoother blend with the existing lighting
                    vec3 indirectDiffuse = calculateIndirectDiffuseLight(normal, baseColor);
                    // Use a softer blend to avoid harsh transitions
                    result += indirectDiffuse * (1.0 - 0.2 * length(result));
                }
            }
            
            // Add indirect specular lighting (reflections)
            if (vctSettings.indirectSpecularLight) {
                // Even objects with zero specularContrib should have a very small amount
                // Just to check if the reflection system is working
                float minSpecular = 0.001;
                float specularContrib = max(minSpecular, material.specularReflectivity * (1.0 - material.transparency));
                
                // Add specular reflection - ensure it's visible on all objects for testing
                vec3 specReflection = calculateIndirectSpecularLight(viewDir);
                
                // Simply add the specular light 
                result += specReflection * specularContrib;
            }
            
            // Add transparency/refraction
            if (material.transparency > 0.01) {
                // Calculate transparency effect
                vec3 refractedResult = calculateRefractiveLight(viewDir);
                
                // Smoother transition based on transparency value
                float blendFactor = material.transparency;
                
                // Fresnel-like effect - edges more reflective, center more transparent
                float fresnelFactor = pow(1.0 - max(0.0, dot(normal, -viewDir)), 3.0);
                float reflectMix = 0.1 + 0.3 * fresnelFactor; // How much reflection to mix in
                
                // Add subtle reflection on the edges
                if (vctSettings.indirectSpecularLight && material.specularReflectivity > 0.01) {
                    vec3 reflectionColor = calculateIndirectSpecularLight(viewDir);
                    refractedResult = mix(refractedResult, reflectionColor, reflectMix);
                }
                
                // Apply smoother transition
                result = mix(result, refractedResult, smoothstep(0.0, 1.0, blendFactor));
                
                // Add a subtle fresnel highlight at edges
                result += fresnelFactor * material.specularColor * 0.1;
            }
            
            // Add direct lighting with shadows
            if (vctSettings.directLight) {
                // Add sun contribution
                if (sun.enabled) {
                    result += calculateVCTSunLight(sun, viewDir);
                }
                
                // Add point light contributions
                for (int i = 0; i < numLights && i < MAX_LIGHTS; i++) {
                    // Skip distant lights
                    float lightDistance = distance(lights[i].position, fs_in.FragPos);
                    if (lightDistance > 30.0) continue;
                    
                    result += calculateVCTDirectLight(lights[i], viewDir);
                }
            }
            
            // Add emissive contribution
            result += material.emissive * baseColor;
        }
    }
    
    // Selection highlighting (works in both lighting modes)
    if (selectionMode) {
        if (isSelected) {
            if (selectedMeshIndex == -1 || selectedMeshIndex == fs_in.meshIndex) { 
                result = mix(result, vec3(1.0, 0.0, 0.0), 0.3);
            }
        }
    }
    
    // Final output
    FragColor = vec4(result, 1.0);
    
    // Apply cursor effect
    if (showFragmentCursor) {
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
        FragColor = mix(inner, outerCursorColor, tOuter * step(0.5, cursorPos.w));
    }
    
    // Apply gamma correction
    FragColor.rgb = pow(FragColor.rgb, vec3(1.0 / 2.2));
}