#version 460 core
out vec4 FragColor;

in VS_OUT {
   vec3 FragPos;
   vec3 Normal;
   vec2 TexCoords;
   vec4 FragPosLightSpace;
   vec3 VertexColor;
   float Intensity;
   mat3 TBN;
   flat int meshIndex;
} fs_in;

// --------------------------------------
// Voxel Cone Tracing constants
// --------------------------------------
#define TSQRT2 2.828427
#define SQRT2 1.414213
#define ISQRT2 0.707106
#define MIPMAP_HARDCAP 5.4f
#define VOXEL_SIZE (1/64.0)
#define SHADOWS 1
#define DIFFUSE_INDIRECT_FACTOR 0.52f
#define SPECULAR_MODE 1
#define SPECULAR_FACTOR 4.0f
#define SPECULAR_POWER 65.0f
#define DIRECT_LIGHT_INTENSITY 0.96f
#define DIST_FACTOR 1.1f
#define CONSTANT 1
#define LINEAR 0
#define QUADRATIC 1
#define GAMMA_CORRECTION 1

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
   
   // Additional properties for voxel cone tracing
   float diffuseReflectivity;
   vec3 specularColor;
   float specularDiffusion;
   float specularReflectivity;
   float refractiveIndex;
   float transparency;
};

// Settings for voxel cone tracing
struct Settings {
   bool indirectSpecularLight;
   bool indirectDiffuseLight;
   bool directLight;
   bool shadows;
};

uniform Material material;
uniform Settings settings;
uniform samplerCube skybox;
uniform float skyboxIntensity;
uniform sampler3D texture3D; // Voxelization texture

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
uniform int state; // For voxel cone tracing debugging/state

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

uniform int selectedMeshIndex;
uniform bool isMeshSelected;

//------------------------------------------------------------------------------
// Voxel Cone Tracing Functions
//------------------------------------------------------------------------------

// Returns an attenuation factor given a distance.
float attenuate(float dist) { 
    dist *= DIST_FACTOR; 
    return 1.0f / (CONSTANT + LINEAR * dist + QUADRATIC * dist * dist); 
}

// Returns a vector that is orthogonal to u.
vec3 orthogonal(vec3 u) {
    u = normalize(u);
    vec3 v = vec3(0.99146, 0.11664, 0.05832); // Pick any normalized vector.
    return abs(dot(u, v)) > 0.99999f ? cross(u, vec3(0, 1, 0)) : cross(u, v);
}

// Scales and bias a given vector (i.e. from [-1, 1] to [0, 1]).
vec3 scaleAndBias(const vec3 p) { 
    return 0.5f * p + vec3(0.5f); 
}

// Returns true if the point p is inside the unity cube.
bool isInsideCube(const vec3 p, float e) { 
    return abs(p.x) < 1 + e && abs(p.y) < 1 + e && abs(p.z) < 1 + e; 
}

// Returns a soft shadow blend by using shadow cone tracing.
float traceShadowCone(vec3 from, vec3 direction, float targetDistance) {
    vec3 normal = normalize(fs_in.Normal);
    from += normal * 0.05f; // Removes artifacts

    float acc = 0;
    float dist = 3 * VOXEL_SIZE;
    const float STOP = targetDistance - 16 * VOXEL_SIZE;

    while(dist < STOP && acc < 1) {    
        vec3 c = from + dist * direction;
        if(!isInsideCube(c, 0)) break;
        c = scaleAndBias(c);
        float l = pow(dist, 2); // Inverse square falloff for shadows
        float s1 = 0.062 * textureLod(texture3D, c, 1 + 0.75 * l).a;
        float s2 = 0.135 * textureLod(texture3D, c, 4.5 * l).a;
        float s = s1 + s2;
        acc += (1 - acc) * s;
        dist += 0.9 * VOXEL_SIZE * (1 + 0.05 * l);
    }
    return 1 - pow(smoothstep(0, 1, acc * 1.4), 1.0 / 1.4);
}    

// Traces a diffuse voxel cone.
vec3 traceDiffuseVoxelCone(const vec3 from, vec3 direction) {
    direction = normalize(direction);
    
    const float CONE_SPREAD = 0.325;
    vec4 acc = vec4(0.0f);
    float dist = 0.1953125;

    // Trace
    while(dist < SQRT2 && acc.a < 1) {
        vec3 c = from + dist * direction;
        c = scaleAndBias(c);
        float l = (1 + CONE_SPREAD * dist / VOXEL_SIZE);
        float level = log2(l);
        float ll = (level + 1) * (level + 1);
        vec4 voxel = textureLod(texture3D, c, min(MIPMAP_HARDCAP, level));
        acc += 0.075 * ll * voxel * pow(1 - voxel.a, 2);
        dist += ll * VOXEL_SIZE * 2;
    }
    return pow(acc.rgb * 2.0, vec3(1.5));
}

// Traces a specular voxel cone.
vec3 traceSpecularVoxelCone(vec3 from, vec3 direction) {
    direction = normalize(direction);
    vec3 normal = normalize(fs_in.Normal);
    float MAX_DISTANCE = distance(vec3(abs(fs_in.FragPos)), vec3(-1));

    const float OFFSET = 8 * VOXEL_SIZE;
    const float STEP = VOXEL_SIZE;

    from += OFFSET * normal;
    
    vec4 acc = vec4(0.0f);
    float dist = OFFSET;

    // Trace.
    while(dist < MAX_DISTANCE && acc.a < 1) { 
        vec3 c = from + dist * direction;
        if(!isInsideCube(c, 0)) break;
        c = scaleAndBias(c);
        
        float level = 0.1 * material.specularDiffusion * log2(1 + dist / VOXEL_SIZE);
        vec4 voxel = textureLod(texture3D, c, min(level, MIPMAP_HARDCAP));
        float f = 1 - acc.a;
        acc.rgb += 0.25 * (1 + material.specularDiffusion) * voxel.rgb * voxel.a * f;
        acc.a += 0.25 * voxel.a * f;
        dist += STEP * (1.0f + 0.125f * level);
    }
    return 1.0 * pow(material.specularDiffusion + 1, 0.8) * acc.rgb;
}

// Calculates indirect diffuse light using voxel cone tracing.
vec3 indirectDiffuseLight() {
    vec3 normal = normalize(fs_in.Normal);
    const float ANGLE_MIX = 0.5f;
    const float w[3] = {1.0, 1.0, 1.0}; // Cone weights

    // Find a base for the side cones with the normal as one of its base vectors.
    const vec3 ortho = normalize(orthogonal(normal));
    const vec3 ortho2 = normalize(cross(ortho, normal));

    // Find base vectors for the corner cones too.
    const vec3 corner = 0.5f * (ortho + ortho2);
    const vec3 corner2 = 0.5f * (ortho - ortho2);

    // Find start position of trace (start with a bit of offset).
    const vec3 N_OFFSET = normal * (1 + 4 * ISQRT2) * VOXEL_SIZE;
    const vec3 C_ORIGIN = fs_in.FragPos + N_OFFSET;

    // Accumulate indirect diffuse light.
    vec3 acc = vec3(0);

    // We offset forward in normal direction, and backward in cone direction.
    const float CONE_OFFSET = -0.01;

    // Trace front cone
    acc += w[0] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * normal, normal);

    // Trace 4 side cones.
    const vec3 s1 = mix(normal, ortho, ANGLE_MIX);
    const vec3 s2 = mix(normal, -ortho, ANGLE_MIX);
    const vec3 s3 = mix(normal, ortho2, ANGLE_MIX);
    const vec3 s4 = mix(normal, -ortho2, ANGLE_MIX);

    acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * ortho, s1);
    acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * ortho, s2);
    acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * ortho2, s3);
    acc += w[1] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * ortho2, s4);

    // Trace 4 corner cones.
    const vec3 c1 = mix(normal, corner, ANGLE_MIX);
    const vec3 c2 = mix(normal, -corner, ANGLE_MIX);
    const vec3 c3 = mix(normal, corner2, ANGLE_MIX);
    const vec3 c4 = mix(normal, -corner2, ANGLE_MIX);

    acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * corner, c1);
    acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * corner, c2);
    acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN + CONE_OFFSET * corner2, c3);
    acc += w[2] * traceDiffuseVoxelCone(C_ORIGIN - CONE_OFFSET * corner2, c4);

    // Return result.
    return DIFFUSE_INDIRECT_FACTOR * material.diffuseReflectivity * acc * (material.objectColor + vec3(0.001f));
}

// Calculates indirect specular light using voxel cone tracing.
vec3 indirectSpecularLight(vec3 viewDirection) {
    vec3 normal = normalize(fs_in.Normal);
    const vec3 reflection = normalize(reflect(viewDirection, normal));
    return material.specularReflectivity * material.specularColor * traceSpecularVoxelCone(fs_in.FragPos, reflection);
}

// Calculates refractive light using voxel cone tracing.
vec3 indirectRefractiveLight(vec3 viewDirection) {
    vec3 normal = normalize(fs_in.Normal);
    const vec3 refraction = refract(viewDirection, normal, 1.0 / material.refractiveIndex);
    const vec3 cmix = mix(material.specularColor, 0.5 * (material.specularColor + vec3(1)), material.transparency);
    return cmix * traceSpecularVoxelCone(fs_in.FragPos, refraction);
}

// Calculates diffuse and specular direct light for a given point light.
vec3 calculateVCTDirectLight(const PointLight light, const vec3 viewDirection) {
    vec3 normal = normalize(fs_in.Normal);
    vec3 lightDirection = light.position - fs_in.FragPos;
    const float distanceToLight = length(lightDirection);
    lightDirection = lightDirection / distanceToLight;
    const float lightAngle = dot(normal, lightDirection);
    
    // Diffuse lighting.
    float diffuseAngle = max(lightAngle, 0.0f); // Lambertian.    
    
    // Specular lighting.
    #if (SPECULAR_MODE == 0) /* Blinn-Phong. */
        const vec3 halfwayVector = normalize(lightDirection + viewDirection);
        float specularAngle = max(dot(normal, halfwayVector), 0.0f);
    #endif
    
    #if (SPECULAR_MODE == 1) /* Perfect reflection. */
        const vec3 reflection = normalize(reflect(viewDirection, normal));
        float specularAngle = max(0, dot(reflection, lightDirection));
    #endif

    float refractiveAngle = 0;
    if(material.transparency > 0.01) {
        vec3 refraction = refract(viewDirection, normal, 1.0 / material.refractiveIndex);
        refractiveAngle = max(0, material.transparency * dot(refraction, lightDirection));
    }

    // Shadows.
    float shadowBlend = 1;
    #if (SHADOWS == 1)
        if(diffuseAngle * (1.0f - material.transparency) > 0 && settings.shadows)
            shadowBlend = traceShadowCone(fs_in.FragPos, lightDirection, distanceToLight);
    #endif

    // Add it all together.
    diffuseAngle = min(shadowBlend, diffuseAngle);
    specularAngle = min(shadowBlend, max(specularAngle, refractiveAngle));
    const float df = 1.0f / (1.0f + 0.25f * material.specularDiffusion); // Diffusion factor.
    const float specular = SPECULAR_FACTOR * pow(specularAngle, df * SPECULAR_POWER);
    const float diffuse = diffuseAngle * (1.0f - material.transparency);

    const vec3 diff = material.diffuseReflectivity * material.objectColor * diffuse;
    const vec3 spec = material.specularReflectivity * material.specularColor * specular;
    const vec3 total = light.color * (diff + spec);
    return attenuate(distanceToLight) * total;
}

// Sums up all direct light from point lights (both diffuse and specular).
vec3 vctDirectLight(vec3 viewDirection) {
    vec3 direct = vec3(0.0f);
    const uint maxLights = min(numLights, MAX_LIGHTS);
    for(uint i = 0; i < maxLights; ++i) direct += calculateVCTDirectLight(lights[i], viewDirection);
    direct *= DIRECT_LIGHT_INTENSITY;
    return direct;
}

vec3 calculateEnvironmentReflection(vec3 normal, float reflectivity) {
    vec3 I = normalize(fs_in.FragPos - viewPos);
    vec3 R = reflect(I, normalize(normal));
    return texture(skybox, R).rgb * reflectivity;
}

vec3 calculateAmbientFromSkybox(vec3 normal) {
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

    // Get base color
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
    
    // AO mapping
    float aoFactor = material.hasAOMap ? 
        texture(material.textures[3], fs_in.TexCoords).r : 1.0;

    // Voxel cone tracing lighting calculation
    vec3 result = vec3(0.0);
    
    // Indirect diffuse lighting
    if (settings.indirectDiffuseLight && material.diffuseReflectivity * (1.0f - material.transparency) > 0.01f) {
        result += indirectDiffuseLight();
    }
    
    // Indirect specular lighting
    if (settings.indirectSpecularLight && material.specularReflectivity * (1.0f - material.transparency) > 0.01f) {
        result += indirectSpecularLight(viewDir);
    }
    
    // Emissive lighting
    result += material.emissive * baseColor;
    
    // Transparency/refraction
    if (material.transparency > 0.01f) {
        result = mix(result, indirectRefractiveLight(viewDir), material.transparency);
    }
    
    // Direct lighting using VCT
    if (settings.directLight) {
        result += vctDirectLight(viewDir);
    }
    
    // Environment lighting from skybox
    vec3 reflectionColor = calculateEnvironmentReflection(normal, material.shininess / 128.0);
    vec3 ambientLight = calculateAmbientFromSkybox(normal);
    
    // Add skybox contributions
    result += reflectionColor * material.specularReflectivity;
    result += ambientLight * baseColor;
    
    // Apply AO
    result *= aoFactor;
    
    // Selection highlighting
    if (selectionMode) {
        if (isSelected) {
            if (selectedMeshIndex == -1 || selectedMeshIndex == fs_in.meshIndex) { 
                result = mix(result, vec3(1.0, 0.0, 0.0), 0.3);
            }
        }
    }
    
    // Apply gamma correction
    #if (GAMMA_CORRECTION == 1)
        result = pow(result, vec3(1.0 / 2.2));
    #endif
    
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