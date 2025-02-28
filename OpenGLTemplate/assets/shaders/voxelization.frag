// Fixed fragment shader for voxelization
#version 450 core

// Lighting settings.
#define POINT_LIGHT_INTENSITY 1
#define MAX_LIGHTS 180

// Lighting attenuation factors.
#define DIST_FACTOR 1.1f /* Distance is multiplied by this when calculating attenuation. */
#define CONSTANT 1
#define LINEAR 0
#define QUADRATIC 1

// Returns an attenuation factor given a distance.
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

vec3 scaleAndBias(vec3 p) {
    return 0.5 * p + vec3(0.5f);
}

bool isInsideCube(const vec3 p, float e) { return abs(p.x) < 1 + e && abs(p.y) < 1 + e && abs(p.z) < 1 + e; }

void main() {
    vec3 color = vec3(0.0f);
    
    if(!isInsideCube(worldPositionFrag, 0.0)) return;

    // Calculate diffuse lighting contribution
    const uint maxLights = min(numberOfLights, MAX_LIGHTS);
    for(uint i = 0; i < maxLights; ++i) {
        color += calculatePointLight(pointLights[i]);
    }
    
    vec3 spec = material.specularReflectivity * material.specularColor;
    vec3 diff = material.diffuseReflectivity * material.diffuseColor;
    color = (diff + spec) * color + clamp(material.emissivity, 0.0, 1.0) * material.diffuseColor;

    // Output lighting to 3D texture
    vec3 voxel = scaleAndBias(worldPositionFrag);
    ivec3 dim = imageSize(texture3D);
    float alpha = pow(1.0 - material.transparency, 4.0); // For soft shadows with transparency
    vec4 res = alpha * vec4(color, 1.0);
    
    // Check that coordinates are valid
    ivec3 voxelCoord = ivec3(dim * voxel);
    if (voxelCoord.x >= 0 && voxelCoord.x < dim.x && 
        voxelCoord.y >= 0 && voxelCoord.y < dim.y && 
        voxelCoord.z >= 0 && voxelCoord.z < dim.z) {
        imageStore(texture3D, voxelCoord, res);
    }
}