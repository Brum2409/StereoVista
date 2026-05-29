#version 460 core

// ===========================================================================
// DDGI - Depth atlas border copy. Same analytic octahedral wrap as the
// irradiance border pass, operating on the RG16F depth/visibility atlas.
// ===========================================================================

layout(local_size_x = 8, local_size_y = 8, local_size_z = 1) in;

layout(rg16f, binding = 1) uniform image2D u_atlas;

uniform ivec3 ddgi_probeCounts;
uniform int ddgi_side; // interior side length (excludes border)

void main() {
    int S = ddgi_side;
    int full = S + 2;
    ivec2 gid = ivec2(gl_GlobalInvocationID.xy);
    int atlasW = ddgi_probeCounts.x * full;
    int atlasH = ddgi_probeCounts.y * ddgi_probeCounts.z * full;
    if (gid.x >= atlasW || gid.y >= atlasH) return;

    int localX = gid.x % full;
    int localY = gid.y % full;
    bool borderX = (localX == 0 || localX == full - 1);
    bool borderY = (localY == 0 || localY == full - 1);
    if (!borderX && !borderY) return;

    ivec2 tileOrigin = ivec2(gid.x - localX, gid.y - localY);
    int srcX, srcY;
    if (borderX && borderY) {
        srcX = (localX == 0) ? S : 1;
        srcY = (localY == 0) ? S : 1;
    } else if (borderY) {
        srcX = S + 1 - localX;
        srcY = (localY == 0) ? 1 : S;
    } else {
        srcY = S + 1 - localY;
        srcX = (localX == 0) ? 1 : S;
    }

    vec4 v = imageLoad(u_atlas, tileOrigin + ivec2(srcX, srcY));
    imageStore(u_atlas, gid, v);
}
