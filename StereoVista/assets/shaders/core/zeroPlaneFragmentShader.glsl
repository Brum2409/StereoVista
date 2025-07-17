#version 330 core

in vec2 TexCoord;

uniform vec4 planeColor;
uniform float convergence;
uniform vec3 cameraPos;

out vec4 FragColor;

void main()
{
    // Create a grid pattern on the plane
    vec2 gridPos = TexCoord * 20.0; // Scale grid
    vec2 grid = abs(fract(gridPos - 0.5) - 0.5) / fwidth(gridPos);
    float line = min(grid.x, grid.y);
    
    // Make grid lines more visible
    float gridAlpha = 1.0 - min(line, 1.0);
    
    // Distance-based fading
    float distance = length(vec3(TexCoord * 2.0 - 1.0, 0.0));
    float fadeAlpha = 1.0 - smoothstep(0.5, 1.0, distance);
    
    // Combine grid and fade
    float finalAlpha = mix(planeColor.a * 0.3, planeColor.a, gridAlpha) * fadeAlpha;
    
    FragColor = vec4(planeColor.rgb, finalAlpha);
}