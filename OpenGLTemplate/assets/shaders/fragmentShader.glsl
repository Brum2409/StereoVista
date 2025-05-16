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
   
   // Voxel cone tracing specific properties
   float diffuseReflectivity;
   float specularReflectivity;
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

// ---- VOXEL CONE TRACING CONSTANTS ----
#define SQRT2 1.414213
#define ISQRT2 0.707106
#define MIPMAP_HARDCAP 5.4f // Prevents too high mipmap levels
#define DIFFUSE_INDIRECT_FACTOR 0.52f // Controls intensity of indirect diffuse light

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
    const float sampleCount = pow(2 * halfKernel + 1, 2);
    
    for(int x = -halfKernel; x <= halfKernel; ++x) {
        for(int y = -halfKernel; y <= halfKernel; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    return shadow / sampleCount;
}

vec3 calculatePointLight(PointLight light, vec3 normal, vec3 viewDir, float specularStrength) {
   vec3 lightDir = normalize(light.position - fs_in.FragPos);
   float diff = max(dot(normal, lightDir), 0.0);
   
   vec3 reflectDir = reflect(-lightDir, normal);
   float spec = pow(max(dot(viewDir, reflectDir), 0.0), material.shininess);
   
   float distance = length(light.position - fs_in.FragPos);
   float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * distance * distance);
   
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

// ---- VOXEL CONE TRACING FUNCTIONS ----
// Returns true if a world position is inside the voxel grid
bool isInVoxelGrid(vec3 worldPos) {
    return worldPos.x >= gridMin.x && worldPos.x <= gridMax.x &&
           worldPos.y >= gridMin.y && worldPos.y <= gridMax.y &&
           worldPos.z >= gridMin.z && worldPos.z <= gridMax.z;
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

// Returns shadow intensity using cone tracing
float traceShadowCone(vec3 from, vec3 direction, float targetDistance) {
    if (!vctSettings.shadows) return 1.0; // No shadow
    
    // Push start position slightly along normal to avoid self-shadowing
    from += normalize(fs_in.Normal) * 0.05f;
    
    float acc = 0.0;
    float dist = 3.0 * voxelSize;
    float stopDist = targetDistance - 16.0 * voxelSize;
    
    while (dist < stopDist && acc < 1.0) {
        vec3 samplePos = from + dist * direction;
        if (!isInVoxelGrid(samplePos)) break;
        
        vec3 texCoord = worldToVoxelCoord(samplePos);
        float l = pow(dist, 2.0); // Inverse square falloff
        
        // Multi-level sampling improves shadow quality
        float s1 = 0.062 * textureLod(voxelGrid, texCoord, 1.0 + 0.75 * l).a;
        float s2 = 0.135 * textureLod(voxelGrid, texCoord, min(4.5 * l, MIPMAP_HARDCAP)).a;
        float shadow = s1 + s2;
        
        acc += (1.0 - acc) * shadow;
        dist += 0.9 * voxelSize * (1.0 + 0.05 * l);
    }
    
    return 1.0 - pow(smoothstep(0.0, 1.0, acc * 1.4), 1.0 / 1.4);
}

// Traces a diffuse voxel cone
vec3 traceDiffuseVoxelCone(vec3 from, vec3 direction) {
    direction = normalize(direction);
    const float CONE_SPREAD = 0.325;
    
    vec4 acc = vec4(0.0);
    float dist = 0.1953125; // Starting distance - prevents bleeding from close surfaces
    
    while (dist < SQRT2 && acc.a < 1.0) {
        vec3 samplePos = from + dist * direction;
        if (!isInVoxelGrid(samplePos)) break;
        
        vec3 texCoord = worldToVoxelCoord(samplePos);
        float level = log2(1.0 + CONE_SPREAD * dist / voxelSize);
        float ll = (level + 1.0) * (level + 1.0);
        
        vec4 voxel = textureLod(voxelGrid, texCoord, min(MIPMAP_HARDCAP, level));
        acc += 0.075 * ll * voxel * pow(1.0 - voxel.a, 2.0);
        
        dist += ll * voxelSize * 2.0;
    }
    
    return pow(acc.rgb * 2.0, vec3(1.5));
}

// Traces a specular voxel cone
vec3 traceSpecularVoxelCone(vec3 from, vec3 direction) {
    direction = normalize(direction);
    const float OFFSET = 8.0 * voxelSize;
    const float STEP = voxelSize;
    
    // Push start position slightly along normal to avoid self-sampling
    from += OFFSET * normalize(fs_in.Normal);
    
    vec4 acc = vec4(0.0);
    float dist = OFFSET;
    float maxDist = length(gridMax - gridMin) * 0.5;
    
    while (dist < maxDist && acc.a < 1.0) {
        vec3 samplePos = from + dist * direction;
        if (!isInVoxelGrid(samplePos)) break;
        
        vec3 texCoord = worldToVoxelCoord(samplePos);
        float specDiffusion = max(0.1, material.shininess / 128.0);
        float level = 0.1 * specDiffusion * log2(1.0 + dist / voxelSize);
        
        vec4 voxel = textureLod(voxelGrid, texCoord, min(level, MIPMAP_HARDCAP));
        float f = 1.0 - acc.a;
        
        acc.rgb += 0.25 * (1.0 + specDiffusion) * voxel.rgb * voxel.a * f;
        acc.a += 0.25 * voxel.a * f;
        
        dist += STEP * (1.0 + 0.125 * level);
    }
    
    return 1.0 * pow(max(0.1, material.shininess / 128.0) + 1.0, 0.8) * acc.rgb;
}

// Calculate indirect diffuse lighting using multiple cones
vec3 calculateIndirectDiffuseLight() {
    const float ANGLE_MIX = 0.5; // Balance between normal and orthogonal directions
    const float w[3] = {1.0, 1.0, 1.0}; // Cone weights
    
    // Calculate orthogonal base vectors for cone directions
    vec3 normal = normalize(fs_in.Normal);
    vec3 ortho = normalize(orthogonal(normal));
    vec3 ortho2 = normalize(cross(ortho, normal));
    
    // Calculate corner vectors
    vec3 corner = 0.5 * (ortho + ortho2);
    vec3 corner2 = 0.5 * (ortho - ortho2);
    
    // Start position with offset
    vec3 N_OFFSET = normal * (1.0 + 4.0 * ISQRT2) * voxelSize;
    vec3 C_ORIGIN = fs_in.FragPos + N_OFFSET;
    
    // Offset in cone direction for better GI
    const float CONE_OFFSET = -0.01;
    
    // Accumulate indirect diffuse light
    vec3 acc = vec3(0.0);
    
    // Trace front cone (normal direction)
    acc += w[0] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * normal, normal);
    
    // Trace 4 side cones
    vec3 s1 = mix(normal, ortho, ANGLE_MIX);
    vec3 s2 = mix(normal, -ortho, ANGLE_MIX);
    vec3 s3 = mix(normal, ortho2, ANGLE_MIX);
    vec3 s4 = mix(normal, -ortho2, ANGLE_MIX);
    
    acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * ortho, s1);
    acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * ortho, s2);
    acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * ortho2, s3);
    acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * ortho2, s4);
    
    // Trace 4 corner cones
    vec3 c1 = mix(normal, corner, ANGLE_MIX);
    vec3 c2 = mix(normal, -corner, ANGLE_MIX);
    vec3 c3 = mix(normal, corner2, ANGLE_MIX);
    vec3 c4 = mix(normal, -corner2, ANGLE_MIX);
    
    acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * corner, c1);
    acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * corner, c2);
    acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * corner2, c3);
    acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * corner2, c4);
    
    // Get base color
    vec3 baseColor = material.hasTexture > 0.5 ? 
                    texture(material.textures[0], fs_in.TexCoords).rgb : 
                    material.objectColor;
    
    // Apply material properties and return
    float diffuseReflectivity = max(0.1, material.shininess / 128.0);
    return DIFFUSE_INDIRECT_FACTOR * diffuseReflectivity * acc * (baseColor + vec3(0.001));
}

// Calculate indirect specular light
vec3 calculateIndirectSpecularLight(vec3 viewDirection) {
    vec3 normal = normalize(fs_in.Normal);
    vec3 reflection = normalize(reflect(viewDirection, normal));
    
    // Get base color
    vec3 baseColor = material.hasTexture > 0.5 ? 
                    texture(material.textures[0], fs_in.TexCoords).rgb : 
                    material.objectColor;
    
    float specularReflectivity = max(0.1, material.shininess / 32.0);
    return specularReflectivity * baseColor * traceSpecularVoxelCone(fs_in.FragPos, reflection);
}

// Calculate direct light for VCT with shadows
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
    
    // Shadow calculation
    float shadow = 1.0;
    if (vctSettings.shadows && diffuseAngle > 0.0) {
        shadow = traceShadowCone(fs_in.FragPos, lightDir, distanceToLight);
    }
    
    // Apply shadow to lighting
    diffuseAngle *= shadow;
    specAngle *= shadow;
    
    // Get base color
    vec3 baseColor = material.hasTexture > 0.5 ? 
                    texture(material.textures[0], fs_in.TexCoords).rgb : 
                    material.objectColor;
    
    // Calculate final lighting contributions
    vec3 diffuse = diffuseAngle * baseColor;
    vec3 specular = specAngle * vec3(0.3);
    
    // Apply attenuation
    float attenuation = 1.0 / (1.0 + 0.09 * distanceToLight + 0.032 * distanceToLight * distanceToLight);
    
    return attenuation * light.intensity * light.color * (diffuse + specular);
}

// Calculate sun light for VCT with shadows
vec3 calculateVCTSunLight(Sun sun, vec3 viewDirection) {
    if (!sun.enabled) return vec3(0.0);
    
    vec3 normal = normalize(fs_in.Normal);
    vec3 lightDir = normalize(-sun.direction);
    
    // Diffuse lighting
    float diffuseAngle = max(dot(normal, lightDir), 0.0);
    
    // Specular lighting
    vec3 halfwayDir = normalize(lightDir + (-viewDirection));
    float specAngle = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    
    // Shadow calculation
    float shadow = 1.0;
    if (vctSettings.shadows && diffuseAngle > 0.0) {
        // Sun is directional, so use a large distance
        shadow = traceShadowCone(fs_in.FragPos, lightDir, 100.0);
    }
    
    // Apply shadow to lighting
    diffuseAngle *= shadow;
    specAngle *= shadow;
    
    // Get base color
    vec3 baseColor = material.hasTexture > 0.5 ? 
                    texture(material.textures[0], fs_in.TexCoords).rgb : 
                    material.objectColor;
    
    // Calculate final lighting contributions
    vec3 ambient = 0.1 * sun.color * sun.intensity;
    vec3 diffuse = diffuseAngle * baseColor * sun.intensity;
    vec3 specular = specAngle * vec3(0.3) * sun.intensity;
    
    return ambient + sun.color * (diffuse + specular);
}

void main() {
    // Check for special rendering modes
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
            vec4 cursorEffect = mix(inner, outerCursorColor, tOuter);
           
            FragColor = mix(FragColor, cursorEffect, step(0.5, cursorPos.w));
        }
        return;
    }
    
    if (isChunkOutline) {
        FragColor = vec4(1.0, 1.0, 0.0, 1.0);
        return;
    }
    
    // Get base color and material properties
    vec3 baseColor = material.hasTexture > 0.5 ? 
                    texture(material.textures[0], fs_in.TexCoords).rgb : 
                    material.objectColor;
    
    float specularStrength = material.hasSpecularMap ? 
                           texture(material.textures[1], fs_in.TexCoords).r : 0.5;
    
    // Calculate normal
    vec3 normal;
    if (material.hasNormalMap) {
        normal = texture(material.textures[2], fs_in.TexCoords).rgb;
        normal = normal * 2.0 - 1.0;
        normal = normalize(fs_in.TBN * normal);
    } else {
        normal = normalize(fs_in.Normal);
    }
    
    vec3 viewDir = normalize(viewPos - fs_in.FragPos);
    vec3 result = vec3(0.0);
    
    // IMPORTANT: Apply different lighting calculations based on mode
    if (lightingMode == LIGHTING_SHADOW_MAPPING) {
        // Traditional shadow mapping lighting
        
        // Calculate shadow factor (only if shadows enabled)
        float shadow = 0.0;
        if (enableShadows && sun.enabled) {
            shadow = ShadowCalculation(fs_in.FragPosLightSpace, normal, normalize(-sun.direction));
        }
        
        // Calculate environment lighting
        vec3 reflectionColor = calculateEnvironmentReflection(normal, material.shininess / 128.0);
        vec3 ambientLight = calculateAmbientFromSkybox(normal);
        
        // Direct lighting
        result = vec3(0.0);
        if (sun.enabled) {
            result += calculateSunLight(sun, normal, viewDir, specularStrength, shadow);
        }
        
        for(int i = 0; i < numLights; i++) {
            result += calculatePointLight(lights[i], normal, viewDir, specularStrength);
        }
        
        // Combine all lighting
        result *= baseColor;
        result += reflectionColor * specularStrength;
        result += ambientLight * baseColor;
        result += material.emissive * baseColor;
    }
    else if (lightingMode == LIGHTING_VOXEL_CONE_TRACING) {
        // Start with ambient contribution
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
                result += calculateIndirectDiffuseLight();
            }
            
            // Add indirect specular lighting (reflections)
            if (vctSettings.indirectSpecularLight && material.shininess > 10.0) {
                result += calculateIndirectSpecularLight(viewDir);
            }
            
            // Add direct lighting with shadows
            if (vctSettings.directLight) {
                // Add sun contribution
                if (sun.enabled) {
                    result += calculateVCTSunLight(sun, viewDir);
                }
                
                // Add point light contributions
                for (int i = 0; i < numLights && i < MAX_LIGHTS; i++) {
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
        vec4 cursorEffect = mix(inner, outerCursorColor, tOuter);
       
        FragColor = mix(FragColor, cursorEffect, step(0.5, cursorPos.w));
    }
    
    // Apply gamma correction
    FragColor.rgb = pow(FragColor.rgb, vec3(1.0 / 2.2));
}