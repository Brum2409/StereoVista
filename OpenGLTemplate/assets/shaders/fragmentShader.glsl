#version 330 core
out vec4 FragColor;

in VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec4 FragPosLightSpace;
    vec3 VertexColor;
    float Intensity;
    mat3 TBN;
} fs_in;

// Material textures
uniform sampler2D texture_diffuse1;
uniform sampler2D texture_normal1;
uniform sampler2D texture_specular1;
uniform sampler2D texture_ao1;
uniform sampler2D shadowMap;

// Material properties
uniform bool hasNormalMap;
uniform bool hasSpecularMap;
uniform bool hasAOMap;
uniform float hasTexture;
uniform vec3 objectColor;
uniform float shininess;
uniform float emissive;

// Point light structure
struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
};

// Sun (directional light) structure
struct Sun {
    vec3 direction;
    vec3 color;
    float intensity;
    bool enabled;
};

// Lighting uniforms
#define MAX_LIGHTS 180
uniform PointLight lights[MAX_LIGHTS];
uniform int numLights;
uniform Sun sun;
uniform vec3 viewPos;

// Cursor uniforms
uniform vec4 cursorPos;
uniform float baseOuterRadius;
uniform float baseOuterBorderThickness;
uniform float baseInnerRadius;
uniform float baseInnerBorderThickness;
uniform vec4 outerCursorColor;
uniform vec4 innerCursorColor;
uniform bool showFragmentCursor;

// Render states
uniform bool isPointCloud;
uniform bool selectionMode;
uniform bool isSelected;
uniform bool isChunkOutline;

// Shadow calculation function
float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir)
{
    // Perform perspective divide
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    
    // Transform to [0,1] range
    projCoords = projCoords * 0.5 + 0.5;
    
    // Check if fragment is outside of shadow map
    if(projCoords.z > 1.0)
        return 0.0;
    
    // Get depth of current fragment from light's perspective
    float currentDepth = projCoords.z;
    
    // Calculate bias based on surface angle - REDUCED BIAS VALUES
float cosTheta = max(dot(normal, lightDir), 0.0);
    float bias = 0.000005 * tan(acos(cosTheta));
    bias = clamp(bias, 0.0, 0.001);
    
    // PCF (Percentage Closer Filtering)
    float shadow = 0.0;
    vec2 texelSize = 1.0 / textureSize(shadowMap, 0) * 2;
    for(int x = -1; x <= 1; ++x) {
        for(int y = -1; y <= 1; ++y) {
            float pcfDepth = texture(shadowMap, projCoords.xy + vec2(x, y) * texelSize).r; 
            shadow += currentDepth - bias > pcfDepth ? 1.0 : 0.0;        
        }    
    }
    shadow /= 9.0;
    
    return shadow;
}

// Light calculation functions
vec3 calculatePointLight(PointLight light, vec3 normal, vec3 viewDir, float specularStrength)
{
    vec3 lightDir = normalize(light.position - fs_in.FragPos);
    
    // Diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Specular shading
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    
    // Attenuation
    float distance = length(light.position - fs_in.FragPos);
    float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * (distance * distance));
    
    // Combine results
    vec3 ambient = 0.1 * light.color * light.intensity;
    vec3 diffuse = diff * light.color * light.intensity;
    vec3 specular = specularStrength * spec * light.color * light.intensity;
    
    return (ambient + diffuse + specular) * attenuation;
}

vec3 calculateSunLight(Sun sun, vec3 normal, vec3 viewDir, float specularStrength, float shadow)
{
    if (!sun.enabled) return vec3(0.0);
    
    vec3 lightDir = normalize(-sun.direction);
    
    // Diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Specular shading
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    
    // Combine results
    vec3 ambient = 0.1 * sun.color * sun.intensity;
    vec3 diffuse = diff * sun.color * sun.intensity;
    vec3 specular = specularStrength * spec * sun.color * sun.intensity;
    
    return ambient + (1.0 - shadow) * (diffuse + specular);
}

void main()
{
    if (isChunkOutline) {
        FragColor = vec4(1.0, 1.0, 0.0, 1.0); // Yellow outline
        return;
    }

    if (isPointCloud) {
        // For point clouds, use vertex color modulated by intensity
        FragColor = vec4(fs_in.VertexColor * fs_in.Intensity, 1.0);
        return;
    }

    // Get base color (either from texture or object color)
    vec3 baseColor = hasTexture > 0.5 ? texture(texture_diffuse1, fs_in.TexCoords).rgb : objectColor;
    
    // Normal mapping
    vec3 normal;
    if (hasNormalMap) {
        normal = texture(texture_normal1, fs_in.TexCoords).rgb;
        normal = normal * 2.0 - 1.0;
        normal = normalize(fs_in.TBN * normal);
    } else {
        normal = normalize(fs_in.Normal);
    }
    
    // View direction
    vec3 viewDir = normalize(viewPos - fs_in.FragPos);
    
    // Specular mapping
    float specularStrength = hasSpecularMap ? texture(texture_specular1, fs_in.TexCoords).r : 0.5;
    
    // Ambient Occlusion
    float aoFactor = hasAOMap ? texture(texture_ao1, fs_in.TexCoords).r : 1.0;
    
    // Calculate shadow
    float shadow = ShadowCalculation(fs_in.FragPosLightSpace, normal, normalize(-sun.direction));
    
    // Calculate lighting
    vec3 result = vec3(0.0);
    
    // Add sun contribution with shadows
    if (sun.enabled) {
        result += calculateSunLight(sun, normal, viewDir, specularStrength, shadow);
    }
    
    // Add point lights
    for(int i = 0; i < numLights; i++) {
        result += calculatePointLight(lights[i], normal, viewDir, specularStrength);
    }
    
    // Apply ambient occlusion and base color
    result *= aoFactor * baseColor;
    
    // Add emissive effect
    result += emissive * baseColor;
    
    // Apply selection highlight
    if (selectionMode && isSelected) {
        result = mix(result, vec3(1.0, 0.0, 0.0), 0.3);
    }
    
    FragColor = vec4(result, 1.0);
    
    // Fragment shader cursor rendering
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
}