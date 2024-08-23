#version 330 core
out vec4 FragColor;

in vec3 Normal;
in vec3 FragPos;
in vec3 ModelPos;

uniform vec3 viewPos;
uniform vec4 sphereColor;
uniform float transparency;
uniform float centerTransparencyFactor;
uniform float edgeSoftness;
uniform bool isInnerSphere;
uniform float innerSphereFactor;

void main()
{
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);
    
    float distFromCenter = length(ModelPos);
    float radius = isInnerSphere ? innerSphereFactor : 1.0;
    
    // Lighting calculations
    vec3 lightDir = normalize(vec3(1.0, 1.0, 1.0));
    float diff = max(dot(norm, lightDir), 0.0);
    vec3 reflectDir = reflect(-lightDir, norm);
    float spec = pow(max(dot(viewDir, reflectDir), 0.0), 32);
    
    vec3 finalColor = sphereColor.rgb;
    
    // Calculate base alpha
    float alpha = sphereColor.a * transparency;
    
    if (!isInnerSphere) {
        // Calculate edge factor
        float edgeFactor = pow(1.0 - abs(dot(viewDir, norm)), edgeSoftness);
        
        // Calculate center transparency
        float centerFactor = smoothstep(0.0, 1.0, distFromCenter / radius);
        float centerAlpha = mix(centerTransparencyFactor, 1.0, centerFactor);
        
        // Combine edge and center effects
        alpha *= mix(centerAlpha, 1.0, edgeFactor);
    }
    
    // Add a subtle rim effect
    float rimFactor = 1.0 - max(0.0, dot(viewDir, norm));
    finalColor += sphereColor.rgb * pow(rimFactor, 3.0) * 0.3;
    
    FragColor = vec4(finalColor, alpha);
}