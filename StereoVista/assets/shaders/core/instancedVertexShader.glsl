#version 460

// Per-vertex attributes
layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aTexCoords;
layout (location = 3) in vec3 aTangent;
layout (location = 4) in vec3 aBitangent;

// Per-instance attributes
layout (location = 5) in mat4 instanceModel;  // Locations 5, 6, 7, 8
layout (location = 9) in vec3 instanceColor;

out VS_OUT {
    vec3 FragPos;
    vec3 Normal;
    vec2 TexCoords;
    vec4 FragPosLightSpace;
    vec3 VertexColor;
    float Intensity;
    mat3 TBN;
    vec3 InstanceColor;
    flat int meshIndex;
} vs_out;

// Transformation matrices
uniform mat4 view;
uniform mat4 projection;
uniform mat4 lightSpaceMatrix;
uniform int currentMeshIndex;

// User-controllable section/clip planes (world space); see the comment in
// shadowMappingVertexShader.glsl. Brush instances are exempt from the radar
// slice (index 0) but ARE cut by user section planes (indices 1..6).
const int MAX_CLIP_PLANES = 6;
uniform int  clipPlaneCount;
uniform vec4 clipPlanes[MAX_CLIP_PLANES];

void main() {
    // Use instance model matrix instead of uniform model matrix
    vs_out.FragPos = vec3(instanceModel * vec4(aPos, 1.0));

    // Calculate normal matrix from instance model matrix
    mat3 instanceNormalMatrix = transpose(inverse(mat3(instanceModel)));

    // Transform normal, tangent, and bitangent with instance normal matrix
    vs_out.Normal = instanceNormalMatrix * aNormal;
    vs_out.TexCoords = aTexCoords;

    // TBN matrix for normal mapping
    vec3 T = normalize(instanceNormalMatrix * aTangent);
    vec3 B = normalize(instanceNormalMatrix * aBitangent);
    vec3 N = normalize(vs_out.Normal);

    // Re-orthogonalize T and B (Gram-Schmidt)
    T = normalize(T - dot(T, N) * N);
    B = normalize(B - dot(B, N) * N - dot(B, T) * T);

    vs_out.TBN = mat3(T, B, N);

    // Pass through instance color
    vs_out.InstanceColor = instanceColor;
    vs_out.VertexColor = vec3(1.0);
    vs_out.Intensity = 1.0;
    vs_out.meshIndex = currentMeshIndex;

    // Light-space position for shadow mapping
    vs_out.FragPosLightSpace = lightSpaceMatrix * vec4(vs_out.FragPos, 1.0);

    // Clip-space position
    gl_Position = projection * view * vec4(vs_out.FragPos, 1.0);

    // Brush instances are never sliced by the radar clip plane; keep them when
    // GL_CLIP_DISTANCE0 is enabled during the radar scene pass.
    gl_ClipDistance[0] = 1.0;

    // User section planes occupy gl_ClipDistance[1..6]. Inactive slots stay
    // positive (+1.0). Constant indices keep the implicit gl_ClipDistance[]
    // array sized correctly.
    gl_ClipDistance[1] = (clipPlaneCount > 0) ? dot(clipPlanes[0].xyz, vs_out.FragPos) + clipPlanes[0].w : 1.0;
    gl_ClipDistance[2] = (clipPlaneCount > 1) ? dot(clipPlanes[1].xyz, vs_out.FragPos) + clipPlanes[1].w : 1.0;
    gl_ClipDistance[3] = (clipPlaneCount > 2) ? dot(clipPlanes[2].xyz, vs_out.FragPos) + clipPlanes[2].w : 1.0;
    gl_ClipDistance[4] = (clipPlaneCount > 3) ? dot(clipPlanes[3].xyz, vs_out.FragPos) + clipPlanes[3].w : 1.0;
    gl_ClipDistance[5] = (clipPlaneCount > 4) ? dot(clipPlanes[4].xyz, vs_out.FragPos) + clipPlanes[4].w : 1.0;
    gl_ClipDistance[6] = (clipPlaneCount > 5) ? dot(clipPlanes[5].xyz, vs_out.FragPos) + clipPlanes[5].w : 1.0;
}
