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
uniform float gridSize;   // Actual voxel grid size
uniform vec3 gridCenter;  // Grid center position in world space

layout(rgba16f, binding = 0) uniform image3D texture3D;

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
    // Check if the world position is within the voxel grid bounds (centered at gridCenter)
    float halfGrid = gridSize * 0.5;
    vec3 relativePos = p - gridCenter;
    return abs(relativePos.x) < halfGrid + e &&
           abs(relativePos.y) < halfGrid + e &&
           abs(relativePos.z) < halfGrid + e;
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
    // Grid is centered at gridCenter, so offset the position first
    float halfGrid = gridSize * 0.5;
    vec3 normalizedPos = (worldPositionFrag - gridCenter + halfGrid) / gridSize;

    // Get texture dimensions (always writes to level 0; mipmaps are
    // generated afterwards by the compute shader).
    ivec3 texDim = imageSize(texture3D);

    // Scale from [0,1] to integer voxel coordinates
    ivec3 voxelCoord = ivec3(floor(normalizedPos * float(texDim.x)));

    // Clamp to valid range
    voxelCoord = clamp(voxelCoord, ivec3(0), texDim - 1);

    // Set alpha based on transparency
    float alpha = 1.0 - material.transparency;
    vec4 voxelColor = vec4(finalColor, alpha);

    // Alpha compositing (Porter-Duff "over" operator)
    // This properly blends overlapping fragments instead of just taking max
    vec4 existingColor = imageLoad(texture3D, voxelCoord);
    float oneMinusExistingAlpha = 1.0 - existingColor.a;
    vec4 blendedColor;
    blendedColor.rgb = existingColor.rgb + voxelColor.rgb * voxelColor.a * oneMinusExistingAlpha;
    blendedColor.a = existingColor.a + voxelColor.a * oneMinusExistingAlpha;

    // Write to the voxel grid
    imageStore(texture3D, voxelCoord, blendedColor);
}