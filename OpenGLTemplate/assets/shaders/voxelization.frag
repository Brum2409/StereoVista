#version 450 core

// Lighting settings
#define POINT_LIGHT_INTENSITY 1
#define MAX_LIGHTS 180

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
    float emissivity;
    float transparency;
};

uniform Material material;
uniform PointLight pointLights[MAX_LIGHTS];
uniform int numberOfLights;
uniform vec3 cameraPosition;
uniform int mipmapLevel; // Current mipmap level
uniform float gridSize; // Actual voxel grid size

layout(rgba8, binding = 0) uniform image3D texture3D;

in vec3 worldPositionFrag;
in vec3 normalFrag;

vec3 calculatePointLight(const PointLight light) {
    vec3 lightDir = normalize(light.position - worldPositionFrag);
    float distance = length(light.position - worldPositionFrag);
    float attenuation = attenuate(distance);
    float diff = max(dot(normalize(normalFrag), lightDir), 0.0f);
    return diff * POINT_LIGHT_INTENSITY * attenuation * light.color;
}

bool isInsideCube(const vec3 p, float e) { 
    // Scale the boundary check based on the actual grid size
    float scaleFactor = 2.0 / gridSize;
    return abs(p.x * scaleFactor) < 1 + e && 
           abs(p.y * scaleFactor) < 1 + e && 
           abs(p.z * scaleFactor) < 1 + e; 
}

void main() {
    // Skip voxels outside the grid boundary using scaled check
    if(!isInsideCube(worldPositionFrag, 0.0)) return;

    // Calculate diffuse lighting contribution
    vec3 color = vec3(0.0f);
    const uint maxLights = min(numberOfLights, MAX_LIGHTS);
    for(uint i = 0; i < maxLights; ++i) {
        color += calculatePointLight(pointLights[i]);
    }
    
    vec3 spec = material.specularReflectivity * material.specularColor;
    vec3 diff = material.diffuseReflectivity * material.diffuseColor;
    color = (diff + spec) * color + clamp(material.emissivity, 0.0, 1.0) * material.diffuseColor;

    // Get texture dimensions
    ivec3 texDim = imageSize(texture3D);
    
    // CRITICAL: Scale world position to normalized coordinates based on grid size
    // This accounts for the fact that world coordinates are always in a fixed range 
    // but the grid size can change
    vec3 scaledPos = worldPositionFrag * (2.0 / gridSize);
    
    // Map from [-1,1] to [0,1] range
    vec3 normalizedPos = scaledPos * 0.5 + 0.5;
    
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
        float alpha = pow(1.0 - material.transparency, 4.0);
        vec4 voxelColor = vec4(color, alpha);
        
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