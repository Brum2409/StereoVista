#version 330 core
out vec4 FragColor;

in vec3 TexCoords;

uniform samplerCube skybox;
uniform bool hdrEnabled;
uniform bool isHDRSkybox; // True if the skybox is an HDR environment map
uniform float skyboxExposure; // Exposure control for skybox

void main() {
    vec4 color = texture(skybox, TexCoords);

    if (hdrEnabled) {
        if (isHDRSkybox) {
            // For HDR skyboxes, apply exposure control
            // Most HDR environment maps need significant reduction (typical values: 0.1-0.3)
            color.rgb *= skyboxExposure;
        } else {
            // For LDR skyboxes, use fixed scale down to prevent blown-out backgrounds
            color.rgb *= 0.25;
        }
    }

    FragColor = color;
}