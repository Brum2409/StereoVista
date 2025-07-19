#version 460
out vec4 FragColor;

in VS_OUT {
   vec3 FragPos;
   vec3 Normal;
   vec2 TexCoords;
   vec3 VertexColor;
   float Intensity;
   flat int meshIndex;
} fs_in;

// ---- LIGHTING MODE CONSTANTS ----
const int LIGHTING_SHADOW_MAPPING = 0;
const int LIGHTING_VOXEL_CONE_TRACING = 1;
const int LIGHTING_RADIANCE = 2;

// ---- MATERIAL STRUCTURE ----
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
};

// ---- DIRECTIONAL LIGHT STRUCTURE ----
struct DirLight {
   vec3 direction;
   vec3 ambient;
   vec3 diffuse;
   vec3 specular;
   float intensity;
};

// ---- UNIFORMS ----
uniform Material material;
uniform DirLight dirLight;
uniform vec3 viewPos;
uniform bool isPointCloud;
uniform bool selectionMode;
uniform bool isSelected;
uniform int selectedMeshIndex;

// Point cloud uniforms
uniform float pointCloudDotSize;
uniform float pointOpacity;
uniform bool intensityColorCoding;

// Fragment cursor uniforms
uniform vec4 cursorPos;
uniform float baseOuterRadius;
uniform float baseOuterBorderThickness;
uniform float baseInnerRadius;
uniform float baseInnerBorderThickness;
uniform vec4 outerCursorColor;
uniform vec4 innerCursorColor;
uniform bool showFragmentCursor;

// Simple directional light calculation
vec3 CalcDirLight(DirLight light, vec3 normal, vec3 viewDir, vec3 materialColor, float shininess) {
    vec3 lightDir = normalize(-light.direction);
    
    // Diffuse shading
    float diff = max(dot(normal, lightDir), 0.0);
    
    // Specular shading with proper shininess handling
    vec3 reflectDir = reflect(-lightDir, normal);
    float dotProduct = max(dot(viewDir, reflectDir), 0.0);
    
    // Handle low shininess values properly to prevent artifacts
    float spec = 0.0;
    if (shininess > 0.1 && dotProduct > 0.0) {
        float clampedShininess = max(shininess, 1.0);
        spec = pow(dotProduct, clampedShininess);
    }
    
    // Combine results
    vec3 ambient = light.ambient * materialColor;
    vec3 diffuse = light.diffuse * diff * materialColor;
    vec3 specular = light.specular * spec * vec3(0.3); // Simple white specular
    
    return (ambient + diffuse + specular) * light.intensity;
}

void main() {
    vec3 result = vec3(0.0);
    
    if (isPointCloud) {
        // Point cloud rendering - exact same as standard shader, no modifications
        FragColor = vec4(fs_in.VertexColor * fs_in.Intensity, 1.0);
    } else {
        // Model rendering
        vec3 materialColor = material.objectColor;
        
        // Sample diffuse texture if available
        if (material.hasTexture > 0.5 && material.numDiffuseTextures > 0) {
            vec4 texColor = texture(material.textures[0], fs_in.TexCoords);
            materialColor = texColor.rgb;
        }
        
        // Calculate lighting
        vec3 norm = normalize(fs_in.Normal);
        vec3 viewDir = normalize(viewPos - fs_in.FragPos);
        
        // Apply directional light
        result = CalcDirLight(dirLight, norm, viewDir, materialColor, material.shininess);
        
        // Add emissive component
        if (material.emissive > 0.0) {
            result += materialColor * material.emissive;
        }
        
        // Apply selection highlighting (matches main fragment shader behavior)
        if (selectionMode && isSelected) {
            if (selectedMeshIndex == -1 || selectedMeshIndex == fs_in.meshIndex) {
                result = mix(result, vec3(1.0, 0.0, 0.0), 0.3); // Red highlight like main shader
            }
        }
        
        FragColor = vec4(result, 1.0);
    }
    
    // Simple tone mapping to prevent over-bright colors
    FragColor.rgb = FragColor.rgb / (FragColor.rgb + vec3(1.0));
    
    // Gamma correction
    FragColor.rgb = pow(FragColor.rgb, vec3(1.0/2.2));
    
    // Apply fragment cursor effect
    if (showFragmentCursor && cursorPos.w > 0.5) {
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
        FragColor = mix(inner, outerCursorColor, tOuter);
    }
}