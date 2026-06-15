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
   vec3 InstanceColor;
   flat int meshIndex;
} fs_in;

// Material structure (matching main shader)
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
   float metallicFactor;
   float roughnessFactor;
};

// Lighting uniforms
uniform Material material;
uniform vec3 viewPos;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform sampler2D shadowMap;
uniform bool enableShadows;
uniform bool useInstanceColor;
// Sun light-space transform + texel footprint for the normal-offset bias
// (mirrors shadowMappingFragmentShader.glsl so instances shadow identically).
uniform mat4 lightSpaceMatrix;
uniform float shadowTexelWorldSize;

// Push the receiver a few texels along its surface normal, then re-project into
// light space. Removes acne without peter panning (matches the main shader).
vec4 sunFragPosLightSpace(vec3 lightDir) {
    vec3 n = normalize(fs_in.Normal);
    float nl = clamp(dot(n, lightDir), 0.0, 1.0);
    float slope = clamp(sqrt(max(1.0 - nl * nl, 0.0)) / max(nl, 0.15), 0.0, 3.0);
    float offset = shadowTexelWorldSize * (1.5 + 1.5 * slope);
    return lightSpaceMatrix * vec4(fs_in.FragPos + n * offset, 1.0);
}

// Simple shadow calculation
float calculateShadow(vec4 fragPosLightSpace) {
    if (!enableShadows) return 0.0;

    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;

    if (projCoords.z > 1.0) return 0.0;

    float currentDepth = projCoords.z;
    float bias = 0.0005;
    float shadow = 0.0;

    // Simple PCF
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0);
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r;
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;
        }
    }
    shadow /= 9.0;

    return shadow;
}

// Simple Blinn-Phong lighting
vec3 calculateLighting(vec3 normal, vec3 viewDir, vec3 albedo) {
    vec3 lightDir = normalize(-sunDirection);

    // Ambient
    vec3 ambient = 0.2 * albedo;

    // Diffuse
    float diff = max(dot(normal, lightDir), 0.0);
    vec3 diffuse = diff * albedo * sunColor * sunIntensity;

    // Specular
    vec3 halfwayDir = normalize(lightDir + viewDir);
    float spec = pow(max(dot(normal, halfwayDir), 0.0), material.shininess);
    vec3 specular = spec * sunColor * sunIntensity;

    // Shadow (normal-offset receiver position)
    float shadow = calculateShadow(sunFragPosLightSpace(lightDir));

    vec3 lighting = ambient + (1.0 - shadow) * (diffuse + specular);
    return lighting;
}

void main() {
    // Get base color
    vec3 albedo;
    if (material.numDiffuseTextures > 0) {
        albedo = texture(material.textures[0], fs_in.TexCoords).rgb;
    } else {
        albedo = material.objectColor;
    }

    // Apply instance color if enabled
    if (useInstanceColor) {
        albedo *= fs_in.InstanceColor;
    }

    // Normal mapping (if available)
    vec3 normal;
    if (material.hasNormalMap && material.numNormalTextures > 0) {
        normal = texture(material.textures[2], fs_in.TexCoords).rgb;
        normal = normal * 2.0 - 1.0;
        normal = normalize(fs_in.TBN * normal);
    } else {
        normal = normalize(fs_in.Normal);
    }

    // Calculate lighting
    vec3 viewDir = normalize(viewPos - fs_in.FragPos);
    vec3 result = calculateLighting(normal, viewDir, albedo);

    // Add emissive
    result += albedo * material.emissive;

    // Output
    FragColor = vec4(result, 1.0);

    // Bloom (bright areas)
    float brightness = dot(result, vec3(0.2126, 0.7152, 0.0722));
    if (brightness > 1.0) {
        BrightColor = vec4(result, 1.0);
    } else {
        BrightColor = vec4(0.0, 0.0, 0.0, 1.0);
    }
}
