#version 450 core
out vec4 FragColor;

uniform sampler3D voxelTexture;
uniform vec3 voxelPos;

void main() {
    // Sample the voxel texture
    vec4 voxelColor = texture(voxelTexture, voxelPos);
    
    // Discard empty voxels (where alpha is very low)
    if(voxelColor.a < 0.01) {
        discard;
    }
    
    // Output the color from the voxel texture with a slightly increased alpha for visibility
    FragColor = vec4(voxelColor.rgb, 0.5);
}