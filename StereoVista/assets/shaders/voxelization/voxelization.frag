#version 450 core

// Lighting settings
#define POINT_LIGHT_INTENSITY 1.0
#define MAX_LIGHTS 180
#define AMBIENT_STRENGTH 0.3

// Lighting attenuation factors
#define DIST_FACTOR 1.1f
#define CONSTANT 1
#define LINEAR 0
#define QUADRATIC 1

// Returns an attenuation factor given a distance
float attenuate(float dist){ dist *= DIST_FACTOR; return 1.0f / (CONSTANT + LINEAR * dist + QUADRATIC * dist * dist); }

struct PointLight {
    vec3 position;
    vec3 color;
};

struct Material {
    vec3 diffuseColor;
    vec3 specularColor;
    float diffuseReflectivity;
    float specularReflectivity;
    float specularDiffusion;
    float emissivity;
    float refractiveIndex;
    float transparency;
    sampler2D textures[16];  // Array of textures
    bool hasTexture;
};

uniform Material material;
uniform PointLight pointLights[MAX_LIGHTS];
uniform int numberOfLights;
uniform vec3 cameraPosition;
uniform int mipmapLevel;  // Current mipmap level
uniform float gridSize;   // Actual voxel grid size

layout(rgba8, binding = 0) uniform image3D texture3D;

in vec3 worldPositionFrag;
in vec3 normalFrag;
in vec2 texCoordFrag;     // Added texture coordinates

vec3 calculatePointLight(const PointLight light) {
    vec3 lightDir = normalize(light.position - worldPositionFrag);
    float distance = length(light.position - worldPositionFrag);
    float attenuation = attenuate(distance);
    float diff = max(dot(normalize(normalFrag), lightDir), 0.0f);
    return diff * POINT_LIGHT_INTENSITY * attenuation * light.color;
}

bool isInsideCube(const vec3 p, float e) { 
    // Check if the world position is within the voxel grid bounds
    float halfGrid = gridSize * 0.5;
    return abs(p.x) < halfGrid + e && 
           abs(p.y) < halfGrid + e && 
           abs(p.z) < halfGrid + e; 
}

void main() {
    // Skip voxels outside the grid boundary
    if(!isInsideCube(worldPositionFrag, 0.0)) return;

    // Get the base color (from texture or material)
    vec3 baseColor;
    if (material.hasTexture && texCoordFrag.x >= 0.0) {
        baseColor = texture(material.textures[0], texCoordFrag).rgb;
    } else {
        baseColor = material.diffuseColor;
    }

    // Calculate ambient lighting (always present)
    vec3 ambient = AMBIENT_STRENGTH * baseColor;
    
    // Calculate diffuse lighting contribution
    vec3 diffuse = vec3(0.0f);
    const uint maxLights = min(numberOfLights, MAX_LIGHTS);
    for(uint i = 0; i < maxLights; ++i) {
        diffuse += calculatePointLight(pointLights[i]);
    }
    diffuse *= baseColor;
    
    // Add emissive component
    vec3 emissive = material.emissivity * baseColor;
    
    // Combine all lighting components
    vec3 finalColor = ambient + diffuse + emissive;
    
    // Ensure colors are in a reasonable range
    finalColor = clamp(finalColor, vec3(0.0), vec3(1.0));

    // Convert world position to normalized voxel coordinates [0,1]
    float halfGrid = gridSize * 0.5;
    vec3 normalizedPos = (worldPositionFrag + halfGrid) / gridSize;
    
    // Get texture dimensions
    ivec3 texDim = imageSize(texture3D);
    
    // Account for mipmap level - compute the effective resolution
    int effectiveRes = texDim.x >> mipmapLevel;
    if (effectiveRes < 1) effectiveRes = 1;
    
    // Scale from [0,1] to [0, effectiveRes-1]
    vec3 voxelPos = normalizedPos * float(effectiveRes);
    
    // Convert to integer coordinates with rounding
    ivec3 baseVoxelCoord = ivec3(floor(voxelPos));
    
    // Calculate the stride for this mipmap level
    int stride = 1 << mipmapLevel;
    
    // Calculate the final storage coordinates
    ivec3 storageCoord = baseVoxelCoord * stride;
    
    // Check bounds before writing
    if (storageCoord.x >= 0 && storageCoord.x < texDim.x &&
        storageCoord.y >= 0 && storageCoord.y < texDim.y &&
        storageCoord.z >= 0 && storageCoord.z < texDim.z) {
        
        // Set alpha based on transparency
        float alpha = 1.0 - material.transparency;
        vec4 voxelColor = vec4(finalColor, alpha);
        
        // Write to the voxel grid
        imageStore(texture3D, storageCoord, voxelColor);
        
        // Fill in adjacent voxels at higher mipmap levels for better coverage
        if (mipmapLevel > 0) {
            for (int x = 0; x < stride && storageCoord.x + x < texDim.x; x++) {
                for (int y = 0; y < stride && storageCoord.y + y < texDim.y; y++) {
                    for (int z = 0; z < stride && storageCoord.z + z < texDim.z; z++) {
                        if (x == 0 && y == 0 && z == 0) continue; // Skip the center voxel
                        ivec3 neighborCoord = storageCoord + ivec3(x, y, z);
                        imageStore(texture3D, neighborCoord, voxelColor);
                    }
                }
            }
        }
    }
}