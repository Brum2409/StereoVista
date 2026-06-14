#version 460
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;


out VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec3 VertexColor;
    float Intensity;
    flat int meshIndex;
} vs_out;

// Transformation matrices
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;

// Lighting mode constants
const int LIGHTING_SHADOW_MAPPING = 0;
const int LIGHTING_VOXEL_CONE_TRACING = 1;
const int LIGHTING_RADIANCE = 2;

// Render state
uniform bool isPointCloud;
uniform int currentMeshIndex;

// Radar top-down slice clip plane (world space). When enabled, geometry above
// radarClipHeight (world Y) is clipped so building interiors are visible from
// above instead of just the roof. Only takes effect while GL_CLIP_DISTANCE0
// is enabled (the radar scene pass); ignored otherwise.
uniform bool radarClipEnabled;
uniform float radarClipHeight;

// User-controllable section/clip planes (world space). Each active plane i is
// written to gl_ClipDistance[i + 1] (index 0 is the radar slice above). A plane
// is vec4(normal.xyz, d) with d = -dot(normal, pointOnPlane); a fragment is kept
// while dot(normal, worldPos) + d >= 0 (the +normal side). clipPlaneCount is the
// number of active planes; the host enables the matching GL_CLIP_DISTANCE slots
// only for the main lit pass, so these have no effect in other passes.
const int MAX_CLIP_PLANES = 6;
uniform int  clipPlaneCount;
uniform vec4 clipPlanes[MAX_CLIP_PLANES];

void main() {
    // Normal matrix from model
    mat3 normalMatrix = transpose(inverse(mat3(model)));
    
    // World-space position
    vs_out.FragPos = vec3(model * vec4(aPos, 1.0));
    
    if (isPointCloud) {
        // Point cloud: pack color/intensity in attributes
        vs_out.VertexColor = aNormal;         // pack normal as color
        vs_out.Intensity = aTexCoords.x;      // intensity from texcoord.x
        vs_out.Normal = vec3(0.0);            // not used
        vs_out.TexCoords = vec2(0.0);         // not used
    } else {
        // Mesh attributes
        vs_out.Normal = normalize(normalMatrix * aNormal);
        vs_out.TexCoords = aTexCoords;
        vs_out.VertexColor = vec3(1.0);
        vs_out.Intensity = 1.0;
        vs_out.meshIndex = currentMeshIndex;
    }
    
    // Clip-space position
    gl_Position = projection * view * vec4(vs_out.FragPos, 1.0);

    // Radar slice clip plane (no effect unless GL_CLIP_DISTANCE0 is enabled)
    gl_ClipDistance[0] =
        radarClipEnabled ? (radarClipHeight - vs_out.FragPos.y) : 1.0;

    // User section planes occupy gl_ClipDistance[1..6]. Inactive slots stay
    // positive (+1.0) so they never clip. Constant indices keep the implicit
    // gl_ClipDistance[] array sized correctly (a dynamic loop index would leave
    // its size undefined).
    gl_ClipDistance[1] = (clipPlaneCount > 0) ? dot(clipPlanes[0].xyz, vs_out.FragPos) + clipPlanes[0].w : 1.0;
    gl_ClipDistance[2] = (clipPlaneCount > 1) ? dot(clipPlanes[1].xyz, vs_out.FragPos) + clipPlanes[1].w : 1.0;
    gl_ClipDistance[3] = (clipPlaneCount > 2) ? dot(clipPlanes[2].xyz, vs_out.FragPos) + clipPlanes[2].w : 1.0;
    gl_ClipDistance[4] = (clipPlaneCount > 3) ? dot(clipPlanes[3].xyz, vs_out.FragPos) + clipPlanes[3].w : 1.0;
    gl_ClipDistance[5] = (clipPlaneCount > 4) ? dot(clipPlanes[4].xyz, vs_out.FragPos) + clipPlanes[4].w : 1.0;
    gl_ClipDistance[6] = (clipPlaneCount > 5) ? dot(clipPlanes[5].xyz, vs_out.FragPos) + clipPlanes[5].w : 1.0;
}