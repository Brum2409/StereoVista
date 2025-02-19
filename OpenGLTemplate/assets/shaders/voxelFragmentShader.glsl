// voxelFragmentShader.glsl
#version 450 core
uniform sampler2D diffuseTexture;
uniform vec3 objectColor;
uniform bool hasTexture;
layout(RGBA8) uniform image3D voxelTexture;

in vec3 worldPositionFrag;
in vec3 normalFrag;
in vec2 texCoordsFrag;

vec3 scaleAndBias(vec3 p) {
    return 0.5f * p + vec3(0.5f);
}

bool isInsideCube(const vec3 p, float e) {
    return abs(p.x) < 1 + e && abs(p.y) < 1 + e && abs(p.z) < 1 + e;
}

void main() {
    if(!isInsideCube(worldPositionFrag, 0)) {
        return;
    }
    
    // Get base color from texture or object color
    vec3 color = hasTexture ? texture(diffuseTexture, texCoordsFrag).rgb : objectColor;
    
    // Add simple lighting
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(normalize(normalFrag), lightDir), 0.2); // 0.2 is ambient
    color *= diff;
    
    // Store in voxel texture
    vec3 voxelPos = scaleAndBias(worldPositionFrag);
    ivec3 dim = imageSize(voxelTexture);
    vec4 finalColor = vec4(color, 1.0);
    
    imageStore(voxelTexture, ivec3(dim * voxelPos), finalColor);
}