#version 460
layout (location = 0) out vec4 FragColor;
layout (location = 1) out vec4 BrightColor;

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
   
   // Enhanced PBR properties
   bool hasMetallicMap;
   bool hasRoughnessMap;
   bool hasHeightMap;
   float metallicFactor;
   float roughnessFactor;
   float normalScale;
   float heightScale;
   
   // Enhanced lighting properties
   vec3 emissiveColor;
   float emissiveStrength;
   vec3 F0; // Base reflectance for dielectrics/metals
   
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

// ---- HDR RENDERING SETTINGS ----
struct HDRSettings {
    bool enabled;
    float exposure;
    float bloomThreshold;
    float bloomIntensity;
    int toneMapOperator; // 0=Reinhard, 1=ACES, 2=Filmic
    bool enableBloom;
};

// ---- SHADOW QUALITY SETTINGS ----
struct ShadowSettings {
    int pcfKernelSize; // 3, 5, 7, or 9
    bool enablePCSS;
    float lightSize; // For PCSS calculations
    float shadowSoftness; // Softness multiplier for shadow filtering
    bool enableCascades;
    int numCascades;
    float cascadeSplitLambda;
};

// ---- MATERIAL SETTINGS ----
struct MaterialSettings {
    bool enablePBR;
    bool enableAO;
    bool enableNormalMapping;
    bool enableParallaxMapping;
};

// ---- LIGHTING STRUCTURES ----
struct PointLight {
   vec3 position;
   vec3 color;
   float intensity;
   float linear;
   float quadratic;
};

struct SpotLight {
   vec3 position;
   vec3 direction;
   vec3 color;
   float intensity;
   float innerCutOff;
   float outerCutOff;
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
uniform samplerCubeArray pointShadowMaps;
uniform samplerCube skybox;
uniform float skyboxIntensity;

// Voxel cone tracing uniforms
uniform sampler3D voxelGrid;
uniform float voxelSize;
uniform VCTSettings vctSettings;

// HDR rendering uniforms
uniform HDRSettings hdrSettings;

// Shadow quality uniforms
uniform ShadowSettings shadowSettings;

// Material settings uniforms
uniform MaterialSettings materialSettings;

// Emissive lighting uniform
uniform float emissiveIntensity;

// Lighting configuration
uniform int lightingMode;
uniform bool enableShadows;

#define MAX_LIGHTS 16
uniform PointLight lights[MAX_LIGHTS];
uniform int numLights;
uniform SpotLight spotLights[MAX_LIGHTS];
uniform int numSpotLights;
uniform Sun sun;
uniform vec3 viewPos;
uniform bool lightsCastShadows[MAX_LIGHTS];

// Point shadow uniforms
uniform float far_plane;

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

// ---- HDR TONE MAPPING FUNCTIONS ----

// Utility function for sRGB conversion
vec3 linearToSRGB(vec3 color) {
    return pow(color, vec3(1.0/2.2));
}

// 1. Reinhard Tone Mapping (Classic, simple)
vec3 reinhardToneMapping(vec3 hdrColor, float exposure) {
    vec3 mapped = hdrColor * exposure;
    return mapped / (1.0 + mapped);
}

// 2. ACES Filmic Tone Mapping (Industry standard, used by many AAA games)
vec3 acesToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // ACES RRT/ODT fit by Stephen Hill
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    
    return clamp((hdrColor * (a * hdrColor + b)) / (hdrColor * (c * hdrColor + d) + e), 0.0, 1.0);
}

// 3. Uncharted 2 Filmic Tone Mapping (John Hable's implementation)
vec3 uncharted2ToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // John Hable's filmic operator
    const float A = 0.15; // Shoulder Strength
    const float B = 0.50; // Linear Strength
    const float C = 0.10; // Linear Angle
    const float D = 0.20; // Toe Strength
    const float E = 0.02; // Toe Numerator
    const float F = 0.30; // Toe Denominator
    
    vec3 numerator = ((hdrColor * (A * hdrColor + C * B) + D * E));
    vec3 denominator = (hdrColor * (A * hdrColor + B) + D * F);
    vec3 mapped = numerator / denominator - E / F;
    
    // White scale
    const float W = 11.2; // Linear White Point Value
    vec3 whiteScale = vec3(1.0) / (((W * (A * W + C * B) + D * E)) / (W * (A * W + B) + D * F) - E / F);
    
    return mapped * whiteScale;
}

// 4. AgX Tone Mapping (Modern, perceptually accurate)
vec3 agxToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // AgX constants
    const mat3 agx_mat = mat3(
        0.842479062253094, 0.0423282422610123, 0.0423756549057051,
        0.0784335999999992, 0.878468636469772, 0.0784336,
        0.0792237451477643, 0.0791661274605434, 0.879142973793104
    );
    
    const mat3 agx_mat_inv = mat3(
        1.19687900512017, -0.0528968517574562, -0.0529716355144438,
        -0.0980208811401368, 1.15190312990417, -0.0980434501171241,
        -0.0918716309140477, -0.0918604049019608, 1.15131639628864
    );
    
    // Apply AgX transform
    hdrColor = agx_mat * hdrColor;
    
    // Log2 encoding
    hdrColor = clamp(log2(hdrColor), -10.0, 10.0);
    
    // Apply curve
    hdrColor = (hdrColor + 10.0) / 20.0;
    
    // S-curve approximation
    hdrColor = hdrColor * hdrColor * (3.0 - 2.0 * hdrColor);
    
    // Convert back
    hdrColor = 2.0 * hdrColor - 1.0;
    hdrColor = pow(vec3(2.0), hdrColor);
    
    // Apply inverse transform
    hdrColor = agx_mat_inv * hdrColor;
    
    return clamp(hdrColor, 0.0, 1.0);
}

// 5. Khronos PBR Neutral Tone Mapping (glTF reference implementation)
vec3 khronosPbrNeutralToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    const float startCompression = 0.8 - 0.04;
    const float desaturation = 0.15;
    
    float x = min(hdrColor.r, min(hdrColor.g, hdrColor.b));
    float offset = x < 0.08 ? x - 6.25 * x * x : 0.04;
    hdrColor -= offset;
    
    float peak = max(hdrColor.r, max(hdrColor.g, hdrColor.b));
    if (peak < startCompression) return hdrColor;
    
    const float d = 1.0 - startCompression;
    float newPeak = 1.0 - d * d / (peak + d - startCompression);
    hdrColor *= newPeak / peak;
    
    float g = 1.0 - 1.0 / (desaturation * (peak - newPeak) + 1.0);
    return mix(hdrColor, newPeak * vec3(1.0), g);
}

// 6. Tony McMapface (Modern, perceptually motivated)
vec3 tonyMcMapfaceToneMapping(vec3 hdrColor, float exposure) {
    hdrColor *= exposure;
    
    // Constants for the tone mapping curve
    const float c_r = 0.36;
    const float s = 0.25;
    const float m = 0.11;
    const float a = 0.004;
    const float c_b = 0.14;
    
    // Luminance-based processing
    float luma = dot(hdrColor, vec3(0.2126, 0.7152, 0.0722));
    
    // Toe
    float toe = exp(-(luma / a));
    
    // Shoulder  
    float shoulder = exp(-(luma - c_r) / s);
    shoulder = c_r + s * log(1.0 + shoulder);
    
    // Blend between toe and shoulder
    float t = clamp((luma - a) / (c_r - a), 0.0, 1.0);
    t = smoothstep(0.0, 1.0, t);
    
    float mapped_luma = mix(luma * toe, shoulder, t);
    
    // Preserve color ratios
    vec3 result = hdrColor * (mapped_luma / max(luma, 1e-6));
    
    return clamp(result, 0.0, 1.0);
}

// Calculate luminance for HDR processing
float calculateLuminance(vec3 color) {
    return dot(color, vec3(0.2126, 0.7152, 0.0722));
}

// Apply tone mapping based on settings
vec3 applyToneMapping(vec3 hdrColor) {
    if (!hdrSettings.enabled) {
        return clamp(hdrColor, 0.0, 1.0); // Simple clamp if HDR disabled
    }
    
    float exposure = hdrSettings.exposure;
    
    // Apply tone mapping operator
    if (hdrSettings.toneMapOperator == 0) {
        return reinhardToneMapping(hdrColor, exposure);
    } else if (hdrSettings.toneMapOperator == 1) {
        return acesToneMapping(hdrColor, exposure);
    } else if (hdrSettings.toneMapOperator == 2) {
        return uncharted2ToneMapping(hdrColor, exposure);
    } else if (hdrSettings.toneMapOperator == 3) {
        return agxToneMapping(hdrColor, exposure);
    } else if (hdrSettings.toneMapOperator == 4) {
        return khronosPbrNeutralToneMapping(hdrColor, exposure);
    } else if (hdrSettings.toneMapOperator == 5) {
        return tonyMcMapfaceToneMapping(hdrColor, exposure);
    } else {
        // Default to ACES (industry standard)
        return acesToneMapping(hdrColor, exposure);
    }
}

// ---- ENHANCED NORMAL MAPPING FUNCTIONS ----
// Enhanced TBN matrix calculation with Gram-Schmidt orthogonalization
mat3 calculateEnhancedTBN(vec3 normal, vec3 tangent, vec3 bitangent) {
    // Normalize input vectors
    normal = normalize(normal);
    tangent = normalize(tangent);
    
    // Gram-Schmidt orthogonalization to ensure orthogonal TBN matrix
    tangent = normalize(tangent - dot(tangent, normal) * normal);
    bitangent = cross(normal, tangent);
    
    return mat3(tangent, bitangent, normal);
}

// Sample and process normal map with intensity control
vec3 sampleNormalMap(sampler2D normalMap, vec2 texCoords, float normalScale) {
    // Sample normal map
    vec3 normalMapSample = texture(normalMap, texCoords).rgb;
    
    // Convert from [0,1] to [-1,1] range
    normalMapSample = normalMapSample * 2.0 - 1.0;
    
    // Apply normal scale for intensity control
    normalMapSample.xy *= normalScale;
    
    // Ensure the normal is normalized
    return normalize(normalMapSample);
}

// Blend multiple normal maps (for detail normal mapping)
vec3 blendNormals(vec3 baseNormal, vec3 detailNormal, float blendFactor) {
    // Reoriented Normal Mapping (RNM) blending
    vec3 t = baseNormal + vec3(0.0, 0.0, 1.0);
    vec3 u = detailNormal * vec3(-1.0, -1.0, 1.0);
    vec3 blended = normalize(t * dot(t, u) - u * t.z);
    
    return mix(baseNormal, blended, blendFactor);
}

// Calculate enhanced normal with multiple layers support
vec3 calculateEnhancedNormal(vec2 texCoords, mat3 TBN, vec3 vertexNormal) {
    // If no normal map or normalScale is 0, use vertex normal directly
    if (!material.hasNormalMap || material.numNormalTextures == 0 || material.normalScale <= 0.0) {
        return normalize(vertexNormal);
    }

    vec3 normal = vec3(0.0, 0.0, 1.0); // Default tangent space normal

    // Sample primary normal map
    normal = sampleNormalMap(material.textures[2], texCoords, material.normalScale);

    // If multiple normal textures exist, blend them
    if (material.numNormalTextures > 1) {
        vec3 detailNormal = sampleNormalMap(material.textures[3], texCoords * 4.0, material.normalScale * 0.5);
        normal = blendNormals(normal, detailNormal, 0.5);
    }

    // Transform from tangent space to world space
    return normalize(TBN * normal);
}

// ---- PBR MATERIAL FUNCTIONS ----
// Sample metallic value from texture or use factor
float getMetallicValue(vec2 texCoords) {
    if (material.hasMetallicMap) {
        return texture(material.textures[4], texCoords).b * material.metallicFactor; // Blue channel for metallic
    }
    return material.metallicFactor;
}

// Sample roughness value from texture or use factor
float getRoughnessValue(vec2 texCoords) {
    if (material.hasRoughnessMap) {
        return texture(material.textures[5], texCoords).g * material.roughnessFactor; // Green channel for roughness
    }
    return material.roughnessFactor;
}

// Calculate F0 (base reflectance) for PBR materials
vec3 calculateF0(vec3 albedo, float metallic) {
    // Dielectric materials have F0 around 0.04, metals use albedo as F0
    return mix(vec3(0.04), albedo, metallic);
}

// Convert roughness to shininess for Blinn-Phong compatibility
float roughnessToShininess(float roughness) {
    // Clamp roughness to avoid division by zero
    roughness = clamp(roughness, 0.01, 1.0);
    // Convert roughness to shininess (higher roughness = lower shininess)
    return (2.0 / (roughness * roughness)) - 2.0;
}

// ---- PBR BRDF FUNCTIONS ----
// Normal Distribution Function (GGX/Trowbridge-Reitz)
float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float num = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = 3.14159265359 * denom * denom;

    return num / denom;
}

// Geometry function using Schlick-GGX approximation
float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;

    float num = NdotV;
    float denom = NdotV * (1.0 - k) + k;

    return num / denom;
}

// Smith's method for geometry function
float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

// Fresnel equation using Schlick's approximation
vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}

// Enhanced Fresnel for roughness
vec3 fresnelSchlickRoughness(float cosTheta, vec3 F0, float roughness) {
    return F0 + (max(vec3(1.0 - roughness), F0) - F0) * pow(clamp(1.0 - cosTheta, 0.0, 1.0), 5.0);
}


// Enhanced material property calculation with PBR support
struct EnhancedMaterialProperties {
    vec3 albedo;
    vec3 specularColor;
    float metallic;
    float roughness;
    float shininess;
    vec3 F0;
    vec3 emissive;
};

EnhancedMaterialProperties calculateMaterialProperties(vec2 texCoords) {
    EnhancedMaterialProperties props;
    
    // Base albedo color
    if (material.hasTexture > 0.5) {
        props.albedo = texture(material.textures[0], texCoords).rgb;
    } else {
        props.albedo = material.objectColor;
    }
    
    // PBR material properties
    props.metallic = getMetallicValue(texCoords);
    props.roughness = getRoughnessValue(texCoords);

    // Calculate F0 based on metallic workflow (LearnOpenGL standard)
    vec3 F0 = vec3(0.04); // Base reflectance for dielectrics
    props.F0 = mix(F0, props.albedo, props.metallic); // Metals use albedo as F0

    // Calculate proper albedo for PBR (metals use albedo as F0, dielectrics keep their color)
    // For metals, the albedo becomes the F0 value, and diffuse contribution is removed
    props.albedo = props.albedo;

    // DEBUG: Clamp values to ensure they're in valid range
    props.metallic = clamp(props.metallic, 0.0, 1.0);
    props.roughness = clamp(props.roughness, 0.01, 1.0); // Avoid zero roughness
    
    // Convert roughness to shininess for Blinn-Phong
    props.shininess = max(roughnessToShininess(props.roughness), material.shininess);
    
    // Specular color handling
    if (material.hasSpecularMap && material.numSpecularTextures > 0) {
        float specIntensity = texture(material.textures[1], texCoords).r;
        props.specularColor = vec3(specIntensity);
    } else {
        props.specularColor = mix(vec3(1.0), props.albedo, props.metallic);
    }
    
    // Enhanced emissive properties
    props.emissive = material.emissiveColor * material.emissiveStrength;
    if (material.emissive > 0.0) {
        props.emissive += props.albedo * material.emissive;
    }
    
    return props;
}

// ---- AMBIENT OCCLUSION FUNCTIONS ----
// Sample ambient occlusion from texture
float getAmbientOcclusion(vec2 texCoords) {
    if (material.hasAOMap) {
        return texture(material.textures[6], texCoords).r; // Red channel for AO
    }
    return 1.0; // No occlusion if no AO map
}

// Apply ambient occlusion to ambient lighting
vec3 applyAmbientOcclusion(vec3 ambientLight, float aoFactor) {
    return ambientLight * aoFactor;
}

// Enhanced ambient lighting calculation with AO
vec3 calculateAmbientLighting(vec3 baseColor, vec2 texCoords) {
    float aoFactor = getAmbientOcclusion(texCoords);
    vec3 ambientLight = baseColor * 0.05; // Base ambient level
    return applyAmbientOcclusion(ambientLight, aoFactor);
}

// ---- PERFORMANCE OPTIMIZATION FUNCTIONS ----
// Distance-based light culling
bool shouldProcessLight(vec3 lightPos, vec3 fragPos, float lightRadius) {
    float distance = length(lightPos - fragPos);
    return distance <= lightRadius;
}

// Calculate light attenuation for culling decisions
float calculateLightAttenuation(vec3 lightPos, vec3 fragPos) {
    float distance = length(lightPos - fragPos);
    return 1.0 / (1.0 + 0.09 * distance + 0.032 * (distance * distance));
}

// Check if light contribution is significant enough to process
bool isLightSignificant(vec3 lightPos, vec3 fragPos, float lightIntensity, float threshold) {
    float attenuation = calculateLightAttenuation(lightPos, fragPos);
    return (lightIntensity * attenuation) > threshold;
}

// LOD-based shading quality adjustment
float getLODFactor(vec3 fragPos, vec3 viewPos) {
    float distance = length(fragPos - viewPos);
    // Reduce quality for distant objects
    return clamp(1.0 - (distance - 10.0) / 40.0, 0.1, 1.0);
}

// ---- ENHANCED SHADOW MAPPING FUNCTIONS ----
float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    if (!enableShadows) return 0.0;
    
    // Perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    // NDC -> [0,1]
    projCoords = projCoords * 0.5 + 0.5;
    
    // Outside shadow frustum
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;
        
    // Depth of current fragment from light
    float currentDepth = projCoords.z;
    
    // Adaptive bias based on surface angle to light
    float cosTheta = dot(normal, lightDir);
    cosTheta = clamp(cosTheta, 0.0, 1.0);
    float baseBias = 0.0005;
    float maxBias = 0.005;
    float adaptiveBias = baseBias + maxBias * (1.0 - cosTheta);
    
    // Enhanced PCF with variable kernel size
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    
    // Use kernel size from shadow settings (default to 3 if not available)
    int kernelSize = shadowSettings.pcfKernelSize > 0 ? shadowSettings.pcfKernelSize : 3;
    int halfKernel = kernelSize / 2;
    
    // Distance-based kernel size adjustment
    float distance = length(projCoords.xy - 0.5);
    float kernelScale = 1.0 + distance * 0.5; // Larger kernel for edges
    
    int sampleCount = 0;
    for(int x = -halfKernel; x <= halfKernel; ++x) {
        for(int y = -halfKernel; y <= halfKernel; ++y) {
            vec2 offset = vec2(x, y) * texelSize * kernelScale;
            vec2 sampleCoords = projCoords.xy + offset;
            
            // Skip samples outside shadow map
            if(sampleCoords.x < 0.0 || sampleCoords.x > 1.0 || 
               sampleCoords.y < 0.0 || sampleCoords.y > 1.0) continue;
            
            float pcfDepth = texture(shadowMap, sampleCoords).r;
            shadow += (currentDepth - adaptiveBias) > pcfDepth ? 1.0 : 0.0;
            sampleCount++;
        }    
    }
    
    // Normalize by actual sample count
    if(sampleCount > 0) {
        shadow /= float(sampleCount);
    }
    
    // Enhanced distance-based fade with smoother transition
    float fadeFactor = 1.0 - smoothstep(0.7, 1.0, projCoords.z);
    shadow *= fadeFactor;
    
    // Soft shadow edges based on distance from shadow caster
    float edgeSoftness = smoothstep(0.0, 0.1, distance) * 0.1;
    shadow = mix(shadow, shadow * 0.8, edgeSoftness);
    
    return shadow;
}

// ---- POINT SHADOW CALCULATION ----

// Poisson disk sampling pattern for PCSS and enhanced shadow sampling
const vec2 poissonDisk[64] = vec2[](
    vec2(-0.613392, 0.617481), vec2(0.170019, -0.040254), vec2(-0.299417, 0.791925),
    vec2(0.645680, 0.493210), vec2(-0.651784, 0.717887), vec2(0.421003, 0.027070),
    vec2(-0.817194, -0.271096), vec2(-0.705374, -0.668203), vec2(0.977050, -0.108615),
    vec2(0.063326, 0.142369), vec2(0.203528, 0.214331), vec2(-0.667531, 0.326090),
    vec2(-0.098422, -0.295755), vec2(-0.885922, 0.215369), vec2(0.566637, 0.605213),
    vec2(0.039766, -0.396100), vec2(0.751946, 0.453352), vec2(0.078707, -0.715323),
    vec2(-0.075838, -0.529344), vec2(0.724479, -0.580798), vec2(0.222999, -0.215125),
    vec2(-0.467574, -0.405438), vec2(-0.248268, -0.814753), vec2(0.354411, -0.887570),
    vec2(0.175817, 0.382366), vec2(0.487472, -0.063082), vec2(-0.084078, 0.898312),
    vec2(0.488876, -0.783441), vec2(0.470016, 0.217933), vec2(-0.696890, -0.549791),
    vec2(-0.149693, 0.605762), vec2(0.034211, 0.979980), vec2(0.503098, -0.308878),
    vec2(-0.016205, -0.872921), vec2(0.385784, -0.393902), vec2(-0.146886, -0.859249),
    vec2(0.643361, 0.164098), vec2(0.634388, -0.049471), vec2(-0.688894, 0.007843),
    vec2(0.464034, -0.188818), vec2(-0.440840, 0.137486), vec2(0.364483, 0.511704),
    vec2(0.034028, 0.325968), vec2(0.099094, -0.308023), vec2(0.693960, -0.366253),
    vec2(0.678884, -0.204688), vec2(0.001801, 0.780328), vec2(0.145177, -0.898984),
    vec2(0.062655, -0.611866), vec2(0.315226, -0.604297), vec2(-0.780145, 0.486251),
    vec2(-0.371868, 0.882138), vec2(0.200476, 0.494430), vec2(-0.494552, -0.711051),
    vec2(0.612476, 0.705252), vec2(-0.578845, -0.768792), vec2(-0.772454, -0.090976),
    vec2(0.504440, 0.372295), vec2(0.155736, 0.065157), vec2(0.391522, 0.849605),
    vec2(-0.620106, -0.328104), vec2(0.789239, -0.419965), vec2(-0.545396, 0.538133),
    vec2(-0.178564, -0.596057)
);

// Point light PCF filter using Poisson disk sampling
float PointPCF_Filter(vec3 fragToLight, int lightIndex, float filterRadius, float zReceiver, float bias) {
    int pcfSamples = 32;
    float sum = 0.0;

    // Normalize the light direction for consistent sampling
    vec3 lightDir = normalize(fragToLight);
    float lightDistance = length(fragToLight);

    // Create two perpendicular vectors to the light direction for 3D sampling
    vec3 up = abs(lightDir.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, lightDir));
    vec3 bitangent = cross(lightDir, tangent);

    // Scale filter radius based on distance to prevent over-sampling
    float adaptiveRadius = filterRadius * clamp(lightDistance / far_plane, 0.1, 1.0);

    for(int i = 0; i < pcfSamples; i++) {
        // Create 3D offset using Poisson disk pattern in tangent space
        vec2 diskSample = poissonDisk[i] * adaptiveRadius;

        // Convert 2D disk sample to 3D sphere sample
        vec3 offset = diskSample.x * tangent +
                     diskSample.y * bitangent +
                     (sin(float(i) * 2.43) * 0.3 * adaptiveRadius) * lightDir;

        vec3 sampleDir = fragToLight + offset;
        float shadowMapDepth = texture(pointShadowMaps, vec4(sampleDir, float(lightIndex))).r * far_plane;
        float sampleDistance = length(sampleDir);

        sum += (sampleDistance - bias > shadowMapDepth) ? 0.0 : 1.0;
    }

    return sum / float(pcfSamples);
}

// Point light blocker search
float findPointBlockerDistance(vec3 fragToLight, int lightIndex, float zReceiver, float lightSize) {
    int blockerSearchSamples = 16;
    float blockerSum = 0.0;
    int numBlockers = 0;

    vec3 lightDir = normalize(fragToLight);
    float lightDistance = length(fragToLight);

    // Create tangent space for consistent sampling
    vec3 up = abs(lightDir.z) < 0.999 ? vec3(0, 0, 1) : vec3(1, 0, 0);
    vec3 tangent = normalize(cross(up, lightDir));
    vec3 bitangent = cross(lightDir, tangent);

    // Much smaller search radius for blocker search
    float searchRadius = lightSize * 0.02;
    searchRadius = clamp(searchRadius, 0.005, 0.03);

    for(int i = 0; i < blockerSearchSamples; i++) {
        vec2 diskSample = poissonDisk[i] * searchRadius;

        vec3 offset = diskSample.x * tangent +
                     diskSample.y * bitangent +
                     (sin(float(i) * 2.43) * 0.2 * searchRadius) * lightDir;

        vec3 sampleDir = fragToLight + offset;
        float shadowMapDepth = texture(pointShadowMaps, vec4(sampleDir, float(lightIndex))).r * far_plane;

        if(shadowMapDepth < zReceiver) {
            blockerSum += shadowMapDepth;
            numBlockers++;
        }
    }

    return (numBlockers > 0) ? blockerSum / float(numBlockers) : -1.0;
}

// Point light penumbra size calculation
float pointPenumbraSize(float zReceiver, float zBlocker, float lightSize) {
    return lightSize * (zReceiver - zBlocker) / zBlocker;
}

float PointShadowCalculation(vec3 fragPos, vec3 lightPosition, int lightIndex) {
    if (!enableShadows) return 0.0;

    // Get vector between fragment and light position
    vec3 fragToLight = fragPos - lightPosition;
    vec3 lightDir = normalize(fragToLight);
    vec3 normal = normalize(fs_in.Normal);

    // Calculate bias
    float cosTheta = dot(normal, -lightDir);
    cosTheta = clamp(cosTheta, 0.0, 1.0);
    float baseBias = 0.002;
    float maxBias = 0.02;
    float adaptiveBias = baseBias + maxBias * (1.0 - cosTheta);

    // Normal offset to reduce self-shadowing
    float normalOffsetScale = 0.02 + 0.08 * (1.0 - cosTheta);
    vec3 offsetPos = fragPos + normal * normalOffsetScale;
    vec3 offsetFragToLight = offsetPos - lightPosition;

    float currentDepth = length(offsetFragToLight);

    // Use PCSS approach similar to directional lights
    if(shadowSettings.enablePCSS) {
        // Step 1: Blocker search
        float avgBlockerDistance = findPointBlockerDistance(offsetFragToLight, lightIndex, currentDepth, shadowSettings.lightSize);

        if(avgBlockerDistance == -1.0) return 0.0; // No shadow

        // Step 2: Penumbra size
        float penumbraRatio = pointPenumbraSize(currentDepth, avgBlockerDistance, shadowSettings.lightSize);

        // Step 3: PCF with variable filter size
        float filterRadius = penumbraRatio * shadowSettings.lightSize * 0.015 * shadowSettings.shadowSoftness;
        filterRadius = clamp(filterRadius, 0.004 * shadowSettings.shadowSoftness, 0.025 * shadowSettings.shadowSoftness);

        float shadow = 1.0 - PointPCF_Filter(offsetFragToLight, lightIndex, filterRadius, currentDepth, adaptiveBias);

        // Distance-based fade near far plane
        float fadeFactor = 1.0 - smoothstep(far_plane * 0.85, far_plane, currentDepth);
        shadow *= fadeFactor;

        return shadow;
    } else {
        // Fallback to improved standard PCF using Poisson disk
        int kernelSize = shadowSettings.pcfKernelSize > 0 ? shadowSettings.pcfKernelSize : 3;
        float diskRadius = (0.008 + (kernelSize - 3) * 0.003) * shadowSettings.shadowSoftness;

        // Distance-based radius scaling
        float distance = currentDepth / far_plane;
        diskRadius *= (1.0 + distance);

        float shadow = 1.0 - PointPCF_Filter(offsetFragToLight, lightIndex, diskRadius, currentDepth, adaptiveBias);

        // Distance-based fade near far plane
        float fadeFactor = 1.0 - smoothstep(far_plane * 0.85, far_plane, currentDepth);
        shadow *= fadeFactor;

        return shadow;
    }
}

// PCSS for point lights (simplified version)
float calculatePointPCSSShadow(vec3 fragPos, vec3 lightPosition, int lightIndex) {
    if(!shadowSettings.enablePCSS) {
        return PointShadowCalculation(fragPos, lightPosition, lightIndex);
    }
    
    vec3 fragToLight = fragPos - lightPosition;
    vec3 lightDir = normalize(fragToLight);
    vec3 normal = normalize(fs_in.Normal);
    
    float currentDepth = length(fragToLight);
    
    // Simplified PCSS for point lights - use variable sample count based on distance
    float shadow = 0.0;
    int sampleCount = 16; // Base sample count for PCSS
    
    // Increase sample count for closer surfaces (more detailed shadows)
    float distanceFactor = clamp(1.0 - (currentDepth / far_plane), 0.0, 1.0);
    sampleCount = int(mix(8.0, 32.0, distanceFactor));
    
    // Variable disk radius based on light size and distance
    float diskRadius = shadowSettings.lightSize * (currentDepth / far_plane) * 0.05 * shadowSettings.shadowSoftness;
    diskRadius = clamp(diskRadius, 0.01 * shadowSettings.shadowSoftness, 0.1 * shadowSettings.shadowSoftness);
    
    // Bias calculation
    float cosTheta = dot(normal, -lightDir);
    cosTheta = clamp(cosTheta, 0.0, 1.0);
    float baseBias = 0.002;
    float maxBias = 0.02;
    float slopeBias = baseBias + maxBias * (1.0 - cosTheta);
    
    // Sample around the light direction with variable radius
    for(int i = 0; i < sampleCount; ++i) {
        // Use Poisson disk pattern for better distribution
        vec2 diskSample = poissonDisk[i % 64];
        vec3 sampleOffset = vec3(diskSample.x, diskSample.y, 0.0) * diskRadius;
        
        // Rotate sample offset to align with light direction
        vec3 sampleDir = fragToLight + sampleOffset;
        float pcfDepth = texture(pointShadowMaps, vec4(sampleDir, float(lightIndex))).r * far_plane;
        shadow += currentDepth - slopeBias > pcfDepth ? 1.0 : 0.0;
    }
    
    return shadow / float(sampleCount);
}

// ---- PCSS (Percentage Closer Soft Shadows) IMPLEMENTATION ----
// Step 1: Blocker search - find average depth of blockers
float findBlockerDistance(sampler2D shadowMap, vec2 uv, float zReceiver, float lightSize) {
    int blockerSearchSamples = 16;
    float searchRadius = lightSize * (zReceiver - 0.1) / zReceiver;
    
    float blockerSum = 0.0;
    int numBlockers = 0;
    
    for(int i = 0; i < blockerSearchSamples; i++) {
        vec2 offset = poissonDisk[i] * searchRadius;
        vec2 sampleCoords = uv + offset;
        
        // Skip samples outside shadow map
        if(sampleCoords.x < 0.0 || sampleCoords.x > 1.0 || 
           sampleCoords.y < 0.0 || sampleCoords.y > 1.0) continue;
        
        float shadowMapDepth = texture(shadowMap, sampleCoords).r;
        
        if(shadowMapDepth < zReceiver) {
            blockerSum += shadowMapDepth;
            numBlockers++;
        }
    }
    
    if(numBlockers == 0) return -1.0; // No blockers found
    return blockerSum / numBlockers;
}

// Step 2: Penumbra size estimation
float penumbraSize(float zReceiver, float zBlocker, float lightSize) {
    return lightSize * (zReceiver - zBlocker) / zBlocker;
}

// Step 3: PCF with variable kernel size
float PCF_Filter(sampler2D shadowMap, vec2 uv, float zReceiver, float filterRadius, vec3 normal, vec3 lightDir) {
    int pcfSamples = 32; // Reduced for performance
    float sum = 0.0;
    
    // Adaptive bias
    float cosTheta = dot(normal, lightDir);
    cosTheta = clamp(cosTheta, 0.0, 1.0);
    float baseBias = 0.0005;
    float maxBias = 0.005;
    float adaptiveBias = baseBias + maxBias * (1.0 - cosTheta);
    
    for(int i = 0; i < pcfSamples; i++) {
        vec2 offset = poissonDisk[i] * filterRadius;
        vec2 sampleCoords = uv + offset;
        
        // Skip samples outside shadow map
        if(sampleCoords.x < 0.0 || sampleCoords.x > 1.0 || 
           sampleCoords.y < 0.0 || sampleCoords.y > 1.0) {
            sum += 1.0; // Assume no shadow outside map
            continue;
        }
        
        float shadowMapDepth = texture(shadowMap, sampleCoords).r;
        sum += (zReceiver - adaptiveBias > shadowMapDepth) ? 0.0 : 1.0;
    }
    
    return sum / pcfSamples;
}

// Main PCSS function for directional lights
float calculatePCSSShadow(sampler2D shadowMap, vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    if(!shadowSettings.enablePCSS) {
        return ShadowCalculation(fragPosLightSpace, normal, lightDir);
    }
    
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    // Outside shadow frustum
    if(projCoords.z > 1.0 || projCoords.x < 0.0 || projCoords.x > 1.0 || 
       projCoords.y < 0.0 || projCoords.y > 1.0)
        return 0.0;
    
    float zReceiver = projCoords.z;
    
    // Step 1: Blocker search
    float avgBlockerDistance = findBlockerDistance(shadowMap, projCoords.xy, zReceiver, shadowSettings.lightSize);
    
    if(avgBlockerDistance == -1.0) return 0.0; // No shadow
    
    // Step 2: Penumbra size
    float penumbraRatio = penumbraSize(zReceiver, avgBlockerDistance, shadowSettings.lightSize);
    
    // Step 3: PCF with variable filter size
    float filterRadius = penumbraRatio * shadowSettings.lightSize / zReceiver;
    filterRadius = clamp(filterRadius, 0.001, 0.1); // Clamp to reasonable range
    
    return 1.0 - PCF_Filter(shadowMap, projCoords.xy, zReceiver, filterRadius, normal, lightDir);
}

// ---- SPOT SHADOW CALCULATION ----
float SpotShadowCalculation(vec3 fragPos, vec3 lightPosition, vec3 lightDirection, int lightIndex) {
    if (!enableShadows) return 0.0;

    // For now, use a simplified approach similar to point lights
    // In a full implementation, spot lights would have their own shadow maps
    vec3 fragToLight = fragPos - lightPosition;
    vec3 lightDir = normalize(fragToLight);
    vec3 normal = normalize(fs_in.Normal);

    // Calculate bias
    float cosTheta = dot(normal, -lightDir);
    cosTheta = clamp(cosTheta, 0.0, 1.0);
    float baseBias = 0.002;
    float maxBias = 0.02;
    float slopeBias = baseBias + maxBias * (1.0 - cosTheta);

    // Use point shadow map technique for now (simplified)
    // In practice, spot lights should have their own directional shadow maps
    float currentDepth = length(fragToLight);
    
    // Variable quality PCF based on shadow settings (same as point lights)
    float shadow = 0.0;
    int kernelSize = shadowSettings.pcfKernelSize > 0 ? shadowSettings.pcfKernelSize : 3;
    int sampleCount = (kernelSize <= 3) ? 4 : (kernelSize <= 5) ? 8 : (kernelSize <= 7) ? 12 : 16;
    
    // Simplified sampling for spot lights
    float diskRadius = 0.01 + (kernelSize - 3) * 0.005;
    
    // Basic directional sampling pattern
    for(int i = 0; i < sampleCount; ++i) {
        float angle = float(i) * 6.28318 / float(sampleCount);
        vec3 offset = vec3(cos(angle), sin(angle), 0.0) * diskRadius;
        vec3 sampleDir = fragToLight + offset;
        
        // For now, return 0 shadow since we don't have proper spot light shadow maps
        // This is a placeholder for future implementation
        shadow += 0.0;
    }
    
    return shadow / float(sampleCount);
}

// ---- ENHANCED LIGHTING FUNCTIONS ----
// Enhanced Blinn-Phong with energy conservation and Fresnel
vec3 CalcDirLight(Sun dirLight, vec3 normal, vec3 viewDir, vec3 diffuseTexColor, vec3 specularTexColor) {
    vec3 lightDir = normalize(-dirLight.direction);
    
    // Diffuse component (Lambertian)
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // Specular component (Blinn-Phong with energy conservation)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfwayDir), 0.0);
    float clampedShininess = max(material.shininess, 4.0);
    
    // Energy conservation normalization factor
    const float PI = 3.14159265359;
    float normalizationFactor = (clampedShininess + 8.0) / (8.0 * PI);
    float specularPower = normalizationFactor * pow(NdotH, clampedShininess);
    
    // Fresnel approximation (Schlick's approximation)
    float VdotH = max(dot(viewDir, halfwayDir), 0.0);
    vec3 F0 = mix(vec3(0.04), specularTexColor, material.metallicFactor); // Use metallic factor if available
    vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    
    // Energy-conserving lighting calculation with AO
    float aoFactor = getAmbientOcclusion(fs_in.TexCoords);
    vec3 ambient = dirLight.color * diffuseTexColor * 0.05 * aoFactor; // Apply AO to ambient
    vec3 diffuse = dirLight.color * NdotL * diffuseTexColor * (1.0 - fresnel); // Diffuse reduced by Fresnel
    vec3 specular = dirLight.color * specularPower * fresnel;
    
    return (ambient + diffuse + specular) * dirLight.intensity;
}

vec3 CalcPointLight(PointLight light, int lightIndex, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 diffuseTexColor, vec3 specularTexColor) {
    vec3 lightDir = normalize(light.position - fragPos);
    
    // Diffuse component (Lambertian)
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // Specular component (Blinn-Phong with energy conservation)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfwayDir), 0.0);
    float clampedShininess = max(material.shininess, 4.0);
    
    // Energy conservation normalization factor
    const float PI = 3.14159265359;
    float normalizationFactor = (clampedShininess + 8.0) / (8.0 * PI);
    float specularPower = normalizationFactor * pow(NdotH, clampedShininess);
    
    // Fresnel approximation
    float VdotH = max(dot(viewDir, halfwayDir), 0.0);
    vec3 F0 = mix(vec3(0.04), specularTexColor, material.metallicFactor);
    vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    
    // Attenuation
    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * (distance * distance));
    
    // Calculate point shadow (respect per-light toggle)
    float shadow = 0.0;
    if (lightsCastShadows[lightIndex]) {
        shadow = calculatePointPCSSShadow(fragPos, light.position, lightIndex);
    }
    
    // Energy-conserving point light calculation with AO
    float aoFactor = getAmbientOcclusion(fs_in.TexCoords);
    vec3 ambient = light.color * diffuseTexColor * 0.02 * aoFactor; // Apply AO to ambient
    vec3 diffuse = light.color * NdotL * diffuseTexColor * (1.0 - fresnel);
    vec3 specular = light.color * specularPower * fresnel;
    
    // Apply shadow to diffuse and specular (but not ambient)
    diffuse *= (1.0 - shadow);
    specular *= (1.0 - shadow);
    
    // Apply attenuation and intensity
    return (ambient + diffuse + specular) * attenuation * light.intensity;
}

vec3 CalcSpotLight(SpotLight light, int lightIndex, vec3 normal, vec3 fragPos, vec3 viewDir, vec3 diffuseTexColor, vec3 specularTexColor) {
    vec3 lightDir = normalize(light.position - fragPos);
    
    // Diffuse component (Lambertian)
    float NdotL = max(dot(normal, lightDir), 0.0);
    
    // Specular component (Blinn-Phong with energy conservation)
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float NdotH = max(dot(normal, halfwayDir), 0.0);
    float clampedShininess = max(material.shininess, 4.0);
    
    // Energy conservation normalization factor
    const float PI = 3.14159265359;
    float normalizationFactor = (clampedShininess + 8.0) / (8.0 * PI);
    float specularPower = normalizationFactor * pow(NdotH, clampedShininess);
    
    // Fresnel approximation
    float VdotH = max(dot(viewDir, halfwayDir), 0.0);
    vec3 F0 = mix(vec3(0.04), specularTexColor, material.metallicFactor);
    vec3 fresnel = F0 + (1.0 - F0) * pow(1.0 - VdotH, 5.0);
    
    // Attenuation
    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * (distance * distance));
    
    // Spotlight (soft edges)
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = light.innerCutOff - light.outerCutOff;
    float spotlightIntensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    
    // Energy-conserving spot light calculation with AO
    float aoFactor = getAmbientOcclusion(fs_in.TexCoords);
    vec3 ambient = light.color * diffuseTexColor * 0.02 * aoFactor; // Apply AO to ambient
    vec3 diffuse = light.color * NdotL * diffuseTexColor * (1.0 - fresnel);
    vec3 specular = light.color * specularPower * fresnel;
    
    // Calculate spot light shadow
    float shadow = SpotShadowCalculation(fragPos, light.position, light.direction, lightIndex);
    
    // Apply spotlight intensity to diffuse and specular (but not ambient)
    diffuse *= spotlightIntensity;
    specular *= spotlightIntensity;
    
    // Apply shadow to diffuse and specular (but not ambient)
    diffuse *= (1.0 - shadow);
    specular *= (1.0 - shadow);
    
    // Apply attenuation and light intensity
    return (ambient + diffuse + specular) * attenuation * light.intensity;
}

vec3 calculateEnvironmentReflection(vec3 normal, float reflectivity) {
    vec3 I = normalize(fs_in.FragPos - viewPos);
    vec3 R = reflect(I, normalize(normal));
    return texture(skybox, R).rgb * reflectivity;
}

vec3 calculateAmbientFromSkybox(vec3 normal) {
    return texture(skybox, normal).rgb * skyboxIntensity;
}

// ---- PBR LIGHTING FUNCTIONS ----
// Calculate PBR lighting for a single light source
vec3 calculatePBRLighting(vec3 lightColor, vec3 lightDir, vec3 viewDir, vec3 normal,
                         vec3 albedo, float metallic, float roughness, vec3 F0, float attenuation) {
    vec3 N = normalize(normal);
    vec3 V = normalize(viewDir);
    vec3 L = normalize(lightDir);
    vec3 H = normalize(V + L);

    // Calculate radiance
    vec3 radiance = lightColor * attenuation;

    // Calculate BRDF components
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    // Calculate specular BRDF
    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.0001; // Add small value to prevent divide by zero
    vec3 specular = numerator / denominator;

    // Calculate diffuse component
    vec3 kS = F; // Specular contribution
    vec3 kD = vec3(1.0) - kS; // Diffuse contribution
    kD *= 1.0 - metallic; // Metals don't have diffuse


    // Lambert BRDF
    float NdotL = max(dot(N, L), 0.0);
    vec3 diffuse = kD * albedo / 3.14159265359;

    // Final lighting contribution
    return (diffuse + specular) * radiance * NdotL;
}

// PBR point light calculation
vec3 CalcPBRPointLight(PointLight light, int lightIndex, vec3 normal, vec3 fragPos, vec3 viewDir,
                       vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 lightDir = light.position - fragPos;
    float distance = length(lightDir);
    lightDir = normalize(lightDir);

    // Calculate attenuation
    float attenuation = 1.0 / (1.0 + light.linear * distance + light.quadratic * (distance * distance));

    // Calculate shadow
    float shadow = 0.0;
    if (enableShadows) {
        if(shadowSettings.enablePCSS) {
            shadow = calculatePointPCSSShadow(fragPos, light.position, lightIndex);
        } else {
            shadow = PointShadowCalculation(fragPos, light.position, lightIndex);
        }
    }

    // Apply shadow and intensity
    vec3 lighting = calculatePBRLighting(light.color, lightDir, viewDir, normal,
                                        albedo, metallic, roughness, F0, attenuation);

    return lighting * (1.0 - shadow) * light.intensity;
}

// PBR directional light calculation
vec3 CalcPBRDirLight(Sun sun, vec3 normal, vec3 viewDir,
                     vec3 albedo, float metallic, float roughness, vec3 F0) {
    vec3 lightDir = normalize(-sun.direction);

    // Calculate shadow
    float shadow = 0.0;
    if (enableShadows) {
        if(shadowSettings.enablePCSS) {
            shadow = calculatePCSSShadow(shadowMap, fs_in.FragPosLightSpace, normal, lightDir);
        } else {
            shadow = ShadowCalculation(fs_in.FragPosLightSpace, normal, lightDir);
        }
    }

    // Apply lighting
    vec3 lighting = calculatePBRLighting(sun.color, lightDir, viewDir, normal,
                                        albedo, metallic, roughness, F0, 1.0);

    return lighting * (1.0 - shadow) * sun.intensity;
}

// ---- VOXEL CONE TRACING CORE FUNCTIONS ----
// Improved shadow cone tracing with proper aperture and sampling
float traceShadowCone(vec3 from, vec3 direction, float targetDistance) {
    if (!vctSettings.shadows) return 1.0; // No shadow
    
    // Better self-shadow avoidance - use surface normal
    vec3 normal = normalize(fs_in.Normal);
    from += normal * max(2.0 * voxelSize, 0.01);
    
    // Cone parameters for soft shadows
    const float CONE_APERTURE = 0.1; // Controls shadow softness (0.1 = ~6 degrees)
    const float MIN_DIAMETER = voxelSize;
    
    float occlusion = 0.0;
    float dist = voxelSize; // Start closer for better detail
    float maxDist = min(targetDistance * 0.95, vctSettings.tracingMaxDistance);
    
    // Dynamic step size based on distance
    for (int i = 0; i < vctSettings.shadowSampleCount && dist < maxDist && occlusion < 0.98; i++) {
        vec3 samplePos = from + dist * direction;
        if (!isInVoxelGrid(samplePos)) break;
        
        // Calculate cone radius at current distance
        float coneRadius = max(MIN_DIAMETER, CONE_APERTURE * dist);
        
        // Determine mipmap level based on cone radius
        float mipmapLevel = max(0.0, log2(coneRadius / voxelSize));
        mipmapLevel = min(mipmapLevel, MIPMAP_HARDCAP);
        
        vec3 texCoord = worldToVoxelCoord(samplePos);
        
        // Multi-sample within cone for better quality
        vec4 samples = vec4(0.0);
        samples.x = textureLod(voxelGrid, texCoord, mipmapLevel).a;
        
        // Add offset samples for cone aperture (jittered sampling)
        if (coneRadius > voxelSize * 1.5) {
            vec3 offset1 = orthogonal(direction) * coneRadius * 0.3;
            vec3 offset2 = cross(direction, offset1) * coneRadius * 0.3;
            
            samples.y = textureLod(voxelGrid, worldToVoxelCoord(samplePos + offset1), mipmapLevel).a;
            samples.z = textureLod(voxelGrid, worldToVoxelCoord(samplePos + offset2), mipmapLevel).a;
            samples.w = textureLod(voxelGrid, worldToVoxelCoord(samplePos - offset1), mipmapLevel).a;
        }
        
        // Average samples for smoother shadows
        float shadowValue = (samples.x + samples.y + samples.z + samples.w) * 0.25;
        
        // Distance-based attenuation for realistic falloff
        float attenuation = 1.0 - smoothstep(0.0, maxDist, dist);
        shadowValue *= attenuation;
        
        // Volumetric accumulation
        occlusion += (1.0 - occlusion) * shadowValue;
        
        // Adaptive step size - larger steps for distant samples
        float stepSize = voxelSize * (1.0 + coneRadius / voxelSize * 0.5);
        dist += stepSize * vctSettings.shadowStepMultiplier;
    }
    
    // Smooth shadow transition
    return 1.0 - smoothstep(0.0, 1.0, occlusion);
}

// Improved diffuse voxel cone tracing with better sampling
vec3 traceDiffuseVoxelCone(vec3 from, vec3 direction) {
    direction = normalize(direction);

    // Cone parameters - 60 degree aperture for proper diffuse GI coverage
    // tan(30°) = 0.577 gives us a 60° full cone aperture (30° half-angle)
    const float CONE_SPREAD = 0.577;
    const float MIN_CONE_RADIUS = voxelSize;
    
    vec4 acc = vec4(0.0);
    float dist = voxelSize * 0.5; // Start closer to surface
    
    // Add temporal jitter to reduce banding artifacts
    float jitter = fract(sin(dot(fs_in.FragPos.xy + direction.xy, vec2(12.9898, 78.233))) * 43758.5453);
    dist += voxelSize * 0.2 * jitter;
    
    float maxDist = min(vctSettings.tracingMaxDistance, SQRT2 * 1.2);

    // More steps for better quality with wider 60° cones
    const int maxSteps = 16;
    for (int i = 0; i < maxSteps && dist < maxDist && acc.a < 0.95; i++) {
        vec3 samplePos = from + dist * direction;
        if (!isInVoxelGrid(samplePos)) break;

        // Calculate cone radius and appropriate mipmap level
        float coneRadius = max(MIN_CONE_RADIUS, CONE_SPREAD * dist);
        // Use cone diameter for mipmap level calculation (more accurate)
        float diameter = 2.0 * coneRadius;
        float level = log2(diameter / voxelSize);
        level = clamp(level, 0.0, MIPMAP_HARDCAP);

        // Single sample with hardware trilinear filtering
        // Mipmaps already provide proper filtering for the cone footprint
        vec3 texCoord = worldToVoxelCoord(samplePos);
        vec4 voxel = textureLod(voxelGrid, texCoord, level);

        // Distance-based attenuation for realistic lighting falloff
        float distAttenuation = 1.0 / (1.0 + 0.1 * dist);

        // Front-to-back alpha blending with energy conservation
        float blendFactor = 0.1 * (1.0 + 0.4 * level) * distAttenuation;
        acc += blendFactor * voxel * (1.0 - acc.a);

        // Adaptive step size based on mipmap level (larger steps at higher LOD)
        float stepMultiplier = 1.5 + 0.5 * level;
        dist += voxelSize * stepMultiplier;
    }

    // Return accumulated radiance (tone mapping happens in final output stage)
    return acc.rgb * 2.0;
}

// Specular voxel cone tracing with material-based aperture
vec3 traceSpecularVoxelCone(vec3 from, vec3 direction) {
    direction = normalize(direction);

    // Self-intersection avoidance - offset along reflection direction
    float offset = max(3.0 * voxelSize, 0.02);
    from += offset * direction;

    // Cone aperture based on material roughness
    // Tighter cone for sharp reflections, wider for rough materials
    float coneAperture = 0.02 + 0.15 * material.specularDiffusion; // ~1-9 degrees
    const float MIN_CONE_RADIUS = voxelSize * 0.5;

    vec4 acc = vec4(0.0);
    float dist = 0.0;
    float maxDist = min(vctSettings.tracingMaxDistance, length(gridMax - gridMin) * 0.6);

    // Adaptive step count - more steps for sharper reflections (tighter cones)
    int maxSteps = int(8.0 + 4.0 / (material.specularDiffusion + 0.1));
    maxSteps = min(maxSteps, 16);

    for (int i = 0; i < maxSteps && dist < maxDist && acc.a < 0.98; i++) {
        vec3 samplePos = from + dist * direction;
        if (!isInVoxelGrid(samplePos)) break;

        // Calculate cone radius based on distance and material roughness
        float coneRadius = max(MIN_CONE_RADIUS, coneAperture * dist);

        // Use cone diameter for mipmap level (consistent with diffuse)
        float diameter = 2.0 * coneRadius;
        float level = log2(diameter / voxelSize);
        level = clamp(level, 0.0, MIPMAP_HARDCAP);

        // Single sample - hardware trilinear filtering handles the cone footprint
        vec3 texCoord = worldToVoxelCoord(samplePos);
        vec4 voxel = textureLod(voxelGrid, texCoord, level);

        // Distance-based attenuation for realistic reflections
        float distAttenuation = 1.0 / (1.0 + 0.02 * dist);

        // Front-to-back alpha blending
        float f = 1.0 - acc.a;
        float weight = 0.15 * (1.0 + 0.6 * material.specularDiffusion) * distAttenuation;

        acc.rgb += weight * voxel.rgb * voxel.a * f;
        acc.a += weight * voxel.a * f;

        // Adaptive step size based on mipmap level
        float stepSize = voxelSize * (0.8 + 0.5 * level);
        dist += stepSize;
    }

    // Final specular contribution with material properties
    float specularStrength = material.specularReflectivity * (2.0 - material.specularDiffusion);
    return acc.rgb * specularStrength * material.specularColor;
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
        
        // Faster refraction LOD transition for better performance
        // Use higher mipmap levels sooner to improve performance
        float specDiffusion = max(0.05, 0.6 * material.specularDiffusion);
        float level = 0.12 * specDiffusion * log2(1.0 + dist / voxelSize * 1.4); // 1.4x faster falloff
        
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

// Calculate indirect diffuse lighting using standard 6-cone hemispherical distribution
// Based on Crassin's original VCT paper - 6 cones with 60° aperture provide good hemisphere coverage
vec3 calculateIndirectDiffuseLight(vec3 normal, vec3 baseColor) {
    // Build orthonormal basis around the normal (tangent space)
    vec3 tangent = normalize(orthogonal(normal));
    vec3 bitangent = normalize(cross(tangent, normal));

    // Standard 6-cone directions in tangent space
    // These directions are pre-computed to evenly cover the hemisphere with 60° cones
    // Cone 0: pointing along normal (up)
    // Cones 1-5: arranged in a ring at ~60° from normal, 72° apart
    const float CONE_ANGLE = 0.866025; // cos(30°) - cones tilted 60° from normal base
    const float RING_ANGLE = 0.5;      // sin(30°) - radial distance in tangent plane

    // Pre-computed directions for the 5 side cones (72° apart = 2π/5)
    // These are in tangent space: (tangent_x, bitangent_y, normal_z)
    const vec3 sideDirections[5] = vec3[](
        vec3(0.0,          RING_ANGLE,  CONE_ANGLE),  // 0°
        vec3(0.4755,       0.1545,      CONE_ANGLE),  // 72°  (sin(72°), cos(72°) * RING_ANGLE)
        vec3(0.2939,      -0.4045,      CONE_ANGLE),  // 144°
        vec3(-0.2939,     -0.4045,      CONE_ANGLE),  // 216°
        vec3(-0.4755,      0.1545,      CONE_ANGLE)   // 288°
    );

    // Cone weights - center cone gets slightly more weight
    const float centerWeight = 0.25;
    const float sideWeight = 0.15;

    // Starting position offset along normal to avoid self-intersection
    vec3 startOffset = normal * (1.0 + 4.0 * ISQRT2) * voxelSize;
    vec3 traceOrigin = fs_in.FragPos + startOffset;

    // Accumulate indirect diffuse light
    vec3 acc = vec3(0.0);

    // Get cone count from settings (1, 5, or 6 for new system)
    int coneCount = vctSettings.diffuseConeCount;

    // Trace center cone (always)
    acc += centerWeight * traceDiffuseVoxelCone(traceOrigin, normal);

    // Trace side cones for better hemisphere coverage
    if (coneCount >= 5) {
        // Determine how many side cones to trace
        int sideCones = (coneCount >= 6) ? 5 : 4;

        for (int i = 0; i < sideCones; i++) {
            // Transform direction from tangent space to world space
            vec3 localDir = sideDirections[i];
            vec3 worldDir = normalize(
                localDir.x * tangent +
                localDir.y * bitangent +
                localDir.z * normal
            );

            acc += sideWeight * traceDiffuseVoxelCone(traceOrigin, worldDir);
        }
    }

    // Add subtle noise to reduce banding artifacts
    float noise = fract(sin(dot(fs_in.FragPos.xy, vec2(12.9898, 78.233))) * 43758.5453);
    float noiseAmount = 0.02;

    // Apply material properties with energy conservation
    return DIFFUSE_INDIRECT_FACTOR * material.diffuseReflectivity *
           acc * baseColor * (1.0 + noise * noiseAmount - noiseAmount * 0.5);
}

vec3 calculateIndirectSpecularLight(vec3 viewDirection) {
    // Use view direction from camera to fragment
    vec3 normal = normalize(fs_in.Normal);
    
    // Ensure orientation
    if (dot(viewDirection, fs_in.FragPos - viewPos) < 0.0) {
        viewDirection = -viewDirection;
    }
    
    // Reflection vector
    vec3 reflection = reflect(viewDirection, normal);
    
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

vec3 calculateVCTSpotLight(SpotLight light, vec3 viewDirection) {
    vec3 normal = normalize(fs_in.Normal);
    vec3 lightDir = light.position - fs_in.FragPos;
    float distanceToLight = length(lightDir);
    lightDir = normalize(lightDir);
    
    // Spotlight intensity calculation
    float theta = dot(lightDir, normalize(-light.direction));
    float epsilon = light.innerCutOff - light.outerCutOff;
    float spotlightIntensity = clamp((theta - light.outerCutOff) / epsilon, 0.0, 1.0);
    
    if (spotlightIntensity <= 0.0) {
        return vec3(0.0); // Outside spotlight cone
    }
    
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
    float df = 1.0 / (1.0 + 0.25 * material.specularDiffusion);
    float diffuse = diffuseAngle * (1.0 - material.transparency);
    float specular = SPECULAR_FACTOR * pow(specAngle, df * SPECULAR_POWER);
    
    vec3 diff = material.diffuseReflectivity * baseColor * diffuse;
    vec3 spec = material.specularReflectivity * material.specularColor * specular;
    
    // Apply attenuation and spotlight intensity
    float attenuation = 1.0 / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
    
    return attenuation * spotlightIntensity * light.intensity * light.color * (diff + spec);
}
void main() {
    // --- EARLY EXITS for special rendering modes ---
    // Point cloud rendering
    if (isPointCloud) {
        vec3 pointColor = fs_in.VertexColor * fs_in.Intensity;
        
        // For HDR rendering, output raw color without gamma correction
        if (hdrSettings.enabled) {
            FragColor = vec4(pointColor, 1.0);
        } else {
            // Apply gamma correction for non-HDR rendering
            FragColor = vec4(pow(pointColor, vec3(1.0 / 2.2)), 1.0);
        }
        
        // Extract bright areas for bloom
        float brightness = calculateLuminance(pointColor);
        if (brightness > hdrSettings.bloomThreshold && hdrSettings.enableBloom) {
            BrightColor = vec4(pointColor, 1.0);
        } else {
            BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
        
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
        vec3 outlineColor = vec3(1.0, 1.0, 0.0);
        
        // For HDR rendering, output raw color
        if (hdrSettings.enabled) {
            FragColor = vec4(outlineColor, 1.0);
        } else {
            FragColor = vec4(outlineColor, 1.0);
        }
        
        // Extract bright areas for bloom
        float brightness = calculateLuminance(outlineColor);
        if (brightness > hdrSettings.bloomThreshold && hdrSettings.enableBloom) {
            BrightColor = vec4(outlineColor, 1.0);
        } else {
            BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
        }
        return;
    }
    
    // --- Common calculations ---
    // Base color
    vec3 baseColor;
    if (material.hasTexture > 0.5) {
        // Sample texture color
        baseColor = texture(material.textures[0], fs_in.TexCoords).rgb;
    } else {
        baseColor = material.objectColor;
    }
    
    float specularStrength = material.hasSpecularMap ? 
                           texture(material.textures[1], fs_in.TexCoords).r : 0.2;
    
    // Calculate enhanced normal with improved TBN and multi-layer support
    // Calculate normal (handles both normal mapping and vertex normals correctly)
    vec3 normal = calculateEnhancedNormal(fs_in.TexCoords, fs_in.TBN, fs_in.Normal);
    
    // Calculate view direction once (FROM fragment TO camera)
    vec3 viewDir = normalize(viewPos - fs_in.FragPos);
    
    // HDR color accumulation
    vec3 hdrColor = vec3(0.0);
    vec3 result = vec3(0.0);

    // --- VOXEL CONE TRACING LIGHTING ---
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
            
            // Indirect specular (reflections)
            if (vctSettings.indirectSpecularLight) {
                float minSpecular = 0.001;
                float specularContrib = max(minSpecular, material.specularReflectivity * (1.0 - material.transparency));
                vec3 specReflection = calculateIndirectSpecularLight(viewDir);
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
                
                // Add a subtle fresnel highlight at edges - limit color tinting
                vec3 safeSpecularColor = clamp(material.specularColor, vec3(0.5), vec3(1.0));
                result += fresnelFactor * safeSpecularColor * 0.05;
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
                
                // Add spot light contributions
                for (int i = 0; i < numSpotLights && i < MAX_LIGHTS; i++) {
                    // Skip distant lights
                    float lightDistance = distance(spotLights[i].position, fs_in.FragPos);
                    if (lightDistance > 30.0) continue;
                    
                    result += calculateVCTSpotLight(spotLights[i], viewDir);
                }
            }
            
            // Add emissive contribution
            if (material.emissive > 0.0) {
                result += baseColor * material.emissive * emissiveIntensity;
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
    
    // Extract bright areas for bloom before tone mapping
    float brightness = calculateLuminance(result);
    if (brightness > hdrSettings.bloomThreshold && hdrSettings.enableBloom) {
        BrightColor = vec4(result, 1.0);
    } else {
        BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
    
    // For HDR rendering, output raw HDR color (tone mapping happens in bloom renderer)
    if (hdrSettings.enabled) {
        FragColor = vec4(result, 1.0);
    } else {
        // Apply tone mapping and gamma correction for non-HDR rendering
        vec3 toneMappedColor = applyToneMapping(result);
        const float gamma = 2.2;
        toneMappedColor = pow(toneMappedColor, vec3(1.0 / gamma));
        FragColor = vec4(toneMappedColor, 1.0);
    }
    
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
}