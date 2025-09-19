#version 450 core

#define INV_STEP_LENGTH (1.0f/STEP_LENGTH)
#define STEP_LENGTH 0.005f

uniform sampler2D textureBack;   // back faces FBO
uniform sampler2D textureFront;  // front faces FBO
uniform sampler3D texture3D;     // voxel volume
uniform vec3 cameraPosition;     // world camera position
uniform int state = 0;           // mipmap sample level
uniform int visualizationMode = 0; // 0=normal, 1=luminance, 2=alpha

in vec2 textureCoordinateFrag; 
out vec4 color;

// Map [-1,1] to [0,1]
vec3 scaleAndBias(vec3 p) { return 0.5f * p + vec3(0.5f); }

// Inside unit cube (+e)
bool isInsideCube(vec3 p, float e) { return abs(p.x) < 1 + e && abs(p.y) < 1 + e && abs(p.z) < 1 + e; }

// Luminance (Rec. 709)
float calculateLuminance(vec3 color) {
    return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

void main() {
    const float mipmapLevel = float(state);

    // Ray setup
    const vec3 origin = isInsideCube(cameraPosition, 0.2f) ? 
        cameraPosition : texture(textureFront, textureCoordinateFrag).xyz;
    vec3 direction = texture(textureBack, textureCoordinateFrag).xyz - origin;
    const uint numberOfSteps = uint(INV_STEP_LENGTH * length(direction));
    direction = normalize(direction);

    // Ray march
    color = vec4(0.0f);
    for(uint step = 0; step < numberOfSteps && color.a < 0.99f; ++step) {
        const vec3 currentPoint = origin + STEP_LENGTH * step * direction;
        
        // Sample 3D texture at mipmapLevel
        vec3 sampleCoord = scaleAndBias(currentPoint);
        
        vec4 currentSample = textureLod(texture3D, sampleCoord, mipmapLevel);

        // Visualization modes
        if (visualizationMode == 1) {
            // Luminance
            float luminance = calculateLuminance(currentSample.rgb);
            currentSample = vec4(vec3(luminance), currentSample.a);
        }
        else if (visualizationMode == 2) {
            // Alpha only
            currentSample = vec4(vec3(currentSample.a), currentSample.a);
        }

        color += (1.0f - color.a) * currentSample;
    } 
    
    // Gamma correction (normal mode only)
    if (visualizationMode == 0) {
        color.rgb = pow(color.rgb, vec3(1.0 / 2.2));
    }
}