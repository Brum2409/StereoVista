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

struct Material {
   sampler2D textures[32];  // Array to hold multiple textures
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
};

uniform Material material;
uniform sampler2D shadowMap;
uniform samplerCube skybox;
uniform float skyboxIntensity;

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

#define MAX_LIGHTS 180
uniform PointLight lights[MAX_LIGHTS];
uniform int numLights;
uniform Sun sun;
uniform vec3 viewPos;

uniform vec4 cursorPos;
uniform float baseOuterRadius;
uniform float baseOuterBorderThickness;
uniform float baseInnerRadius;
uniform float baseInnerBorderThickness;
uniform vec4 outerCursorColor;
uniform vec4 innerCursorColor;
uniform bool showFragmentCursor;

uniform bool isPointCloud;
uniform bool selectionMode;
uniform bool isSelected;
uniform bool isChunkOutline;



float ShadowCalculation(vec4 fragPosLightSpace, vec3 normal, vec3 lightDir) {
    vec3 projCoords = fragPosLightSpace.xyz / fragPosLightSpace.w;
    projCoords = projCoords * 0.5 + 0.5;
    
    if(projCoords.z > 1.0)
        return 0.0;
        
    float currentDepth = projCoords.z;
    float bias = max(0.001 * (1.0 - dot(normal, lightDir)), 0.0005);  // Reduced bias values
    
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
   float attenuation = 1.0 / (1.0 + 0.09 * distance + 0.032 * (distance * distance));
   
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
    // Sample the skybox in the direction of the normal for ambient light
    return texture(skybox, normal).rgb * skyboxIntensity;
}

void main() {
    if (isChunkOutline) {
        FragColor = vec4(1.0, 1.0, 0.0, 1.0);
        return;
    }

    if (isPointCloud) {
        FragColor = vec4(fs_in.VertexColor * fs_in.Intensity, 1.0);
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

    // Get base color and normal
    vec3 baseColor = material.hasTexture > 0.5 ? 
        texture(material.textures[0], fs_in.TexCoords).rgb : 
        material.objectColor;
   
    // Normal mapping
    vec3 normal;
    if (material.hasNormalMap) {
        normal = texture(material.textures[2], fs_in.TexCoords).rgb;
        normal = normal * 2.0 - 1.0;
        normal = normalize(fs_in.TBN * normal);
    } else {
        normal = normalize(fs_in.Normal);
    }
   
    vec3 viewDir = normalize(viewPos - fs_in.FragPos);
   
    // Specular mapping
    float specularStrength = material.hasSpecularMap ? 
        texture(material.textures[1], fs_in.TexCoords).r : 0.5;
   
    // AO mapping
    float aoFactor = material.hasAOMap ? 
        texture(material.textures[3], fs_in.TexCoords).r : 1.0;
   
    float shadow = ShadowCalculation(fs_in.FragPosLightSpace, normal, normalize(-sun.direction));
   
    // Calculate all lighting components
    vec3 result = vec3(0.0);
    
    // Environment lighting from skybox
    vec3 reflectionColor = calculateEnvironmentReflection(normal, material.shininess / 128.0);
    vec3 ambientLight = calculateAmbientFromSkybox(normal);
    
    // Direct lighting
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
    result *= aoFactor;
   
    if (selectionMode && isSelected) {
        result = mix(result, vec3(1.0, 0.0, 0.0), 0.3);
    }
   
    FragColor = vec4(result, 1.0);
   
    // Cursor rendering
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