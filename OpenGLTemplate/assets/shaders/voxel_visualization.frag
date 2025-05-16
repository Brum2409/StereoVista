#version 450 core

#define INV_STEP_LENGTH (1.0f/STEP_LENGTH)
#define STEP_LENGTH 0.005f

uniform sampler2D textureBack; // Unit cube back FBO.
uniform sampler2D textureFront; // Unit cube front FBO.
uniform sampler3D texture3D; // Texture in which voxelization is stored.
uniform vec3 cameraPosition; // World camera position.
uniform int state = 0; // Decides mipmap sample level.
uniform int visualizationMode = 0; // 0 = normal, 1 = luminance, 2 = alpha

in vec2 textureCoordinateFrag; 
out vec4 color;

// Scales and bias a given vector (i.e. from [-1, 1] to [0, 1]).
vec3 scaleAndBias(vec3 p) { return 0.5f * p + vec3(0.5f); }

// Returns true if p is inside the unity cube (+ e) centered on (0, 0, 0).
bool isInsideCube(vec3 p, float e) { return abs(p.x) < 1 + e && abs(p.y) < 1 + e && abs(p.z) < 1 + e; }

// Calculate luminance from RGB using the standard formula
float calculateLuminance(vec3 color) {
    return 0.2126 * color.r + 0.7152 * color.g + 0.0722 * color.b;
}

void main() {
    const float mipmapLevel = float(state);

    // Initialize ray.
    const vec3 origin = isInsideCube(cameraPosition, 0.2f) ? 
        cameraPosition : texture(textureFront, textureCoordinateFrag).xyz;
    vec3 direction = texture(textureBack, textureCoordinateFrag).xyz - origin;
    const uint numberOfSteps = uint(INV_STEP_LENGTH * length(direction));
    direction = normalize(direction);

    // Trace.
    color = vec4(0.0f);
    for(uint step = 0; step < numberOfSteps && color.a < 0.99f; ++step) {
        const vec3 currentPoint = origin + STEP_LENGTH * step * direction;
        
        // This is the critical part - we need to adjust the sampling coordinates based on the mipmap level
        // The texture coordinates need to stay centered around 0.5 regardless of mipmap level
        vec3 sampleCoord = scaleAndBias(currentPoint);
        
        vec4 currentSample = textureLod(texture3D, sampleCoord, mipmapLevel);

        // Process the sample based on visualization mode
        if (visualizationMode == 1) {
            // Luminance visualization - convert RGB to luminance
            float luminance = calculateLuminance(currentSample.rgb);
            currentSample = vec4(vec3(luminance), currentSample.a);
        }
        else if (visualizationMode == 2) {
            // Alpha visualization - show only the alpha channel
            currentSample = vec4(vec3(currentSample.a), currentSample.a);
        }

        color += (1.0f - color.a) * currentSample;
    } 
    
    // Apply gamma correction unless we're in a special visualization mode
    if (visualizationMode == 0) {
        color.rgb = pow(color.rgb, vec3(1.0 / 2.2));
    }
}