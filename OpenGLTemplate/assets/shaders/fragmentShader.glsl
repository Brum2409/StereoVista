#version 330 core
out vec4 FragColor;

// Input from vertex shader
in vec3 FragPos;
in vec3 Normal;
in vec2 TexCoords;
in mat3 TBN;
in vec3 VertexColor;
in float Intensity;

// Point light structure
struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
};

// Sun structure
struct Sun {
    vec3 direction;
    vec3 color;
    float intensity;
    bool enabled;
};

// Uniforms
uniform bool isPointCloud;
uniform vec3 viewPos;
uniform sampler2D texture_diffuse1;
uniform sampler2D texture_normal1;
uniform sampler2D texture_specular1;
uniform sampler2D texture_ao1;
uniform bool hasNormalMap;
uniform bool hasSpecularMap;
uniform bool hasAOMap;
uniform float hasTexture;
uniform vec4 cursorPos;
uniform bool selectionMode;
uniform bool isSelected;
uniform vec3 objectColor;
uniform float shininess;
uniform float emissive;
uniform bool isChunkOutline;

// Cursor uniforms
uniform float baseOuterRadius;
uniform float baseOuterBorderThickness;
uniform float baseInnerRadius;
uniform float baseInnerBorderThickness;
uniform vec4 outerCursorColor;
uniform vec4 innerCursorColor;
uniform bool showFragmentCursor;

// Lighting uniforms
#define MAX_LIGHTS 180
uniform PointLight lights[MAX_LIGHTS];
uniform int numLights;
uniform Sun sun;

vec3 calculatePointLight(PointLight light, vec3 normal, vec3 fragPos, vec3 viewDir, float specularStrength)
{
    vec3 lightDir = normalize(light.position - fragPos);
    
    // Diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Specular shading
    vec3 reflectDir = reflect(-lightDir, normal);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), shininess);
    
    // Attenuation
    float distance = length(light.position - fragPos);
    float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * (distance * distance));
    
    // Combine results
    vec3 ambient = 0.1 * light.color * light.intensity;
    vec3 diffuse = diff * light.color * light.intensity;
    vec3 specular = specularStrength * spec * light.color * light.intensity;
    
    ambient *= attenuation;
    diffuse *= attenuation;
    specular *= attenuation;
    
    return (ambient + diffuse + specular);
}

vec3 calculateSunLight(Sun sun, vec3 normal, vec3 viewDir, float specularStrength)
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
    
    return (ambient + diffuse + specular);
}

void main()
{
    if (isChunkOutline) {
        FragColor = vec4(1.0, 1.0, 0.0, 1.0); // Yellow outline
        return;
    }

    if (isPointCloud) {
        // For point clouds, use the vertex color modulated by intensity
        FragColor = vec4(VertexColor * Intensity, 1.0);
    } else {
        // Determine base color (either from texture or object color)
        vec3 baseColor = hasTexture > 0.5 ? texture(texture_diffuse1, TexCoords).rgb : objectColor;

        // Normal mapping
        vec3 norm;
        if (hasNormalMap) {
            vec3 tangentNormal = texture(texture_normal1, TexCoords).xyz * 2.0 - 1.0;
            norm = normalize(TBN * tangentNormal);
        } else {
            norm = normalize(Normal);
        }

        // Specular mapping
        float specularStrength = hasSpecularMap ? texture(texture_specular1, TexCoords).r : 0.5;

        // Ambient Occlusion
        float aoFactor = hasAOMap ? texture(texture_ao1, TexCoords).r : 1.0;

        vec3 viewDir = normalize(viewPos - FragPos);
        
        vec3 result = vec3(0.0);
        
        // Add sun contribution
        if (sun.enabled) {
            result += calculateSunLight(sun, norm, viewDir, specularStrength);
        }

        // Calculate contribution from all point lights
        for(int i = 0; i < numLights; i++) {
            result += calculatePointLight(lights[i], norm, FragPos, viewDir, specularStrength);
        }
        
        // Apply ambient occlusion and base color
        result *= aoFactor * baseColor;

        // Add emissive effect
        result += emissive * baseColor;
        
        // Apply selection highlight if in selection mode and selected
        if (selectionMode && isSelected) {
            result = mix(result, vec3(1.0, 0.0, 0.0), 0.3); // Red highlight
        }
        
        FragColor = vec4(result, 1.0);
    }

    // Fragment shader cursor rendering
    if (showFragmentCursor)
    {
        float distanceToCursor = length(cursorPos.xyz - FragPos);
        float distanceFromCamera = length(cursorPos.xyz - viewPos);
        
        // Scale cursor size based on distance from camera
        float scaleFactor = distanceFromCamera;
        float outerRadius = baseOuterRadius * scaleFactor; 
        float outerBorderThickness = baseOuterBorderThickness * scaleFactor;
        float innerRadius = baseInnerRadius * scaleFactor;
        float innerBorderThickness = baseInnerBorderThickness * scaleFactor;
        
        // Create sharp transitions for cursor borders
        float tOuter = step(outerRadius - outerBorderThickness, distanceToCursor) - step(outerRadius, distanceToCursor);
        float tInner = step(innerRadius - innerBorderThickness, distanceToCursor) - step(innerRadius, distanceToCursor);
        
        // Mix cursor colors with fragment color
        vec4 inner = mix(FragColor, innerCursorColor, tInner);
        vec4 cursorEffect = mix(inner, outerCursorColor, tOuter);
        
        // Apply cursor effect if cursor is valid (cursorPos.w > 0.5)
        FragColor = mix(FragColor, cursorEffect, step(0.5, cursorPos.w));
    }
}