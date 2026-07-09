// pointcloud_common.glsl — shared geometry stage of the Schütz compute
// point-cloud passes (standard rasterize + HQS depth + HQS color).
//
// Ported from assets/shaders/core/pointcloud_rasterize.comp (GL reference,
// itself adapted from m-schuetz/compute_rasterizer, MIT) with the §C fixes:
//   C.1  core int64 only — GL_EXT_shader_atomic_int64 via the required
//        shaderBufferInt64Atomics device feature; the GL shaders' NV-only
//        extensions (GL_NV_shader_atomic_int64 / GL_NV_gpu_shader5) are gone.
//   C.2  Vulkan [0,1] clip volume + house reverse-Z: the NDC containment test
//        checks z in [0,1] (not [-1,1]) and the frustum plane extraction uses
//        the [0,1] rows (z >= 0 → row2; z <= w → row3 - row2).
//   A.2  All buffers arrive as buffer_reference pointers inside ONE
//        PointCloudDispatch struct read through a pushed address — the whole
//        pipeline binds zero descriptor sets.
//
// The including shader must declare BEFORE including this file:
//   #version 460
//   #extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
//   #extension GL_EXT_buffer_reference2 : require
//   #extension GL_EXT_scalar_block_layout : require
// and define, after the include:
//   void pcProcessPoint(ivec2 px, float ndcZ, float viewDepth, int radius,
//                       uint index);
// then call pcMain() from main(). g_d holds the dispatch data.

#include "pointcloud_types.h"

layout(local_size_x = SV_PC_RASTER_WORKGROUP, local_size_y = 1, local_size_z = 1) in;

// Read-only views of the cloud sections. The pass-owned write targets
// (framebuffer / HQS buffers) are declared by each including shader with the
// qualifiers it needs (coherent for the early-reject pre-read).
// `restrict` everywhere: glslang decorates physical-storage-buffer pointers
// Aliased by default, which forbids the driver from hoisting these reads
// across the framebuffer atomics in the hot loop. No two references in any
// of these shaders touch overlapping memory, so restrict is sound.
layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer PcUints {
    uint v[];
};
layout(buffer_reference, scalar, buffer_reference_align = 4) restrict readonly buffer PcBatches {
    PointCloudBatch b[];
};
// The dispatch array lives device-local with a 400-byte (16-aligned) stride.
layout(buffer_reference, scalar, buffer_reference_align = 16) restrict readonly buffer PcDispatchRef {
    PointCloudDispatch d;
};

layout(push_constant, scalar) uniform PcPush {
    PointCloudComputePush pc;
};

// Loaded once at the top of pcMain; read by the shared code and the
// pass-specific pcProcessPoint.
PointCloudDispatch g_d;

// ── Constants (Schütz 10/20/30-bit quantisation) ─────────────────────────────
#define STEPS_30BIT 1073741824.0 // 2^30
#define STEPS_10BIT 1024.0       // 2^10
#define MASK_10BIT 1023u

// ── Frustum culling (Gribb/Hartmann on the local-space MVP) ──────────────────
// Vulkan clip volume: |x| <= w, |y| <= w, 0 <= z <= w. The z >= 0 half-space
// is row2 itself (NOT row3+row2 — that was GL's -w <= z). With the house
// reverse-Z projections row2 encodes the far plane; an infinite-far projection
// makes it degenerate (zero normal), which the length guard treats as
// always-inside instead of dividing by zero.
struct PcPlane {
    vec3 normal;
    float constant;
};

PcPlane pcCreatePlane(float x, float y, float z, float w) {
    float len = length(vec3(x, y, z));
    if (len < 1e-12)
        return PcPlane(vec3(0.0), 1.0); // degenerate: always passes
    return PcPlane(vec3(x, y, z) / len, w / len);
}

bool pcIntersectsFrustum(mat4 m, vec3 bmin, vec3 bmax) {
    // GLSL mat4 is column-major: m[col][row]; row i = (m[0][i], m[1][i], ...).
    float r0x = m[0][0], r0y = m[1][0], r0z = m[2][0], r0w = m[3][0];
    float r1x = m[0][1], r1y = m[1][1], r1z = m[2][1], r1w = m[3][1];
    float r2x = m[0][2], r2y = m[1][2], r2z = m[2][2], r2w = m[3][2];
    float r3x = m[0][3], r3y = m[1][3], r3z = m[2][3], r3w = m[3][3];

    PcPlane planes[6];
    planes[0] = pcCreatePlane(r3x - r0x, r3y - r0y, r3z - r0z, r3w - r0w); // x <= w
    planes[1] = pcCreatePlane(r3x + r0x, r3y + r0y, r3z + r0z, r3w + r0w); // -w <= x
    planes[2] = pcCreatePlane(r3x + r1x, r3y + r1y, r3z + r1z, r3w + r1w); // -w <= y
    planes[3] = pcCreatePlane(r3x - r1x, r3y - r1y, r3z - r1z, r3w - r1w); // y <= w
    planes[4] = pcCreatePlane(r2x, r2y, r2z, r2w);                         // 0 <= z
    planes[5] = pcCreatePlane(r3x - r2x, r3y - r2y, r3z - r2z, r3w - r2w); // z <= w

    for (int i = 0; i < 6; i++) {
        // Positive-vertex test: if even the AABB corner closest to the plane
        // normal is behind the plane, the whole box is outside.
        vec3 pv;
        pv.x = planes[i].normal.x > 0.0 ? bmax.x : bmin.x;
        pv.y = planes[i].normal.y > 0.0 ? bmax.y : bmin.y;
        pv.z = planes[i].normal.z > 0.0 ? bmax.z : bmin.z;
        if (dot(planes[i].normal, pv) + planes[i].constant < 0.0)
            return false;
    }
    return true;
}

// ── Precision level (Schütz getPrecisionLevel, verbatim thresholds) ───────────
// Projects the batch bounding sphere to screen pixels and picks how many
// packed-coordinate tiers each point read decodes.
int pcPrecisionLevel(vec3 bmin, vec3 bmax) {
    vec3 center = (bmin + bmax) * 0.5;
    float radius = distance(bmin, bmax);

    vec4 viewCenter = g_d.modelView * vec4(center, 1.0);
    vec4 viewEdge = viewCenter + vec4(radius, 0.0, 0.0, 0.0);
    vec4 projCenter = g_d.proj * viewCenter;
    vec4 projEdge = g_d.proj * viewEdge;
    if (projCenter.w <= 0.0)
        return 0;

    projCenter.xy /= projCenter.w;
    projEdge.xy /= projEdge.w;
    vec2 imageSize = vec2(float(g_d.imageWidth), float(g_d.imageHeight));
    float pixelSize = distance(projEdge.xy * imageSize, projCenter.xy * imageSize) * 0.5;

    if (pixelSize < 100.0) return 4;
    else if (pixelSize < 200.0) return 3;
    else if (pixelSize < 500.0) return 2;
    else if (pixelSize < 10000.0) return 1;
    else return 0;
}

// ── Adaptive splat sizing (close-up / sparse hole filling) ────────────────────
// Estimate the local world-space point spacing from the batch AABB (surface
// scans: the two largest extents cover ~numPoints points).
float pcBatchSpacing(vec3 boxSize, int numPoints) {
    if (numPoints <= 1)
        return 0.0;
    float d0 = max(boxSize.x, max(boxSize.y, boxSize.z));
    float d2 = min(boxSize.x, min(boxSize.y, boxSize.z));
    float d1 = (boxSize.x + boxSize.y + boxSize.z) - d0 - d2;
    float area = max(d0 * d1, 1e-12);
    return sqrt(area / float(numPoints));
}

// ── Per-point callback the including pass defines ─────────────────────────────
void pcProcessPoint(ivec2 px, float ndcZ, float viewDepth, int radius, uint index);

// ── Shared main: one workgroup per batch ──────────────────────────────────────
void pcMain() {
    g_d = PcDispatchRef(pc.dispatchData).d;

    uint batchIndex = pc.baseBatch + gl_WorkGroupID.x;
    PointCloudBatch batch = PcBatches(g_d.batches).b[batchIndex];

    vec3 bmin = vec3(batch.minX, batch.minY, batch.minZ);
    vec3 bmax = vec3(batch.maxX, batch.maxY, batch.maxZ);
    vec3 boxSize = bmax - bmin;

    // One AABB test replaces up to kComputeBatchSize per-point clip tests.
    if (!pcIntersectsFrustum(g_d.mvp, bmin, bmax))
        return;

    int level = pcPrecisionLevel(bmin, bmax);

    // Everything of the per-point radius formula that does not depend on the
    // point's own depth: radius_px = splatScale / clip.w. proj[1][1] carries
    // the house Y-flip, hence abs() (the GL reference had it positive).
    float splatScale = 0.0;
    if (g_d.splatMaxRadius > 0) {
        float worldSpacing = pcBatchSpacing(boxSize, batch.numPoints);
        float modelScale = length(vec3(g_d.modelView[0]));
        float pxPerUnit = abs(g_d.proj[1][1]) * float(g_d.imageHeight) * 0.5;
        splatScale = 0.5 * worldSpacing * modelScale * pxPerUnit;
    }

    // restrict on the locals too: glslang otherwise decorates the pointer
    // VARIABLES AliasedPointer even when the block type is restrict.
    restrict PcUints xyz4b = PcUints(g_d.xyz4b);
    restrict PcUints xyz8b = PcUints(g_d.xyz8b);
    restrict PcUints xyz12b = PcUints(g_d.xyz12b);
    vec2 imageSize = vec2(float(g_d.imageWidth), float(g_d.imageHeight));

    // Strided point loop: thread T reads points T, T+128, ... (coalesced).
    for (int i = 0; i < g_d.pointsPerThread; i++) {
        uint localIndex = uint(i) * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (int(localIndex) >= batch.numPoints)
            return;
        uint index = uint(batch.firstPoint) + localIndex;

        // Decode the 10/20/30-bit quantised local position.
        vec3 point;
        if (level == 0) {
            uint b4 = xyz4b.v[index];
            uint b8 = xyz8b.v[index];
            uint b12 = xyz12b.v[index];
            uint X = (((b4 >> 0) & MASK_10BIT) << 20) | (((b8 >> 0) & MASK_10BIT) << 10) | ((b12 >> 0) & MASK_10BIT);
            uint Y = (((b4 >> 10) & MASK_10BIT) << 20) | (((b8 >> 10) & MASK_10BIT) << 10) | ((b12 >> 10) & MASK_10BIT);
            uint Z = (((b4 >> 20) & MASK_10BIT) << 20) | (((b8 >> 20) & MASK_10BIT) << 10) | ((b12 >> 20) & MASK_10BIT);
            point = vec3(float(X), float(Y), float(Z)) * (boxSize / STEPS_30BIT) + bmin;
        } else if (level == 1) {
            uint b4 = xyz4b.v[index];
            uint b8 = xyz8b.v[index];
            uint X = (((b4 >> 0) & MASK_10BIT) << 20) | (((b8 >> 0) & MASK_10BIT) << 10);
            uint Y = (((b4 >> 10) & MASK_10BIT) << 20) | (((b8 >> 10) & MASK_10BIT) << 10);
            uint Z = (((b4 >> 20) & MASK_10BIT) << 20) | (((b8 >> 20) & MASK_10BIT) << 10);
            point = vec3(float(X), float(Y), float(Z)) * (boxSize / STEPS_30BIT) + bmin;
        } else {
            uint b4 = xyz4b.v[index];
            uint X = (b4 >> 0) & MASK_10BIT;
            uint Y = (b4 >> 10) & MASK_10BIT;
            uint Z = (b4 >> 20) & MASK_10BIT;
            point = vec3(float(X), float(Y), float(Z)) * (boxSize / STEPS_10BIT) + bmin;
        }

        // User section/clip planes (cloud-local space, like the decoded point).
        bool clipped = false;
        for (int cp = 0; cp < g_d.clipPlaneCount; ++cp) {
            if (dot(g_d.clipPlanes[cp].xyz, point) + g_d.clipPlanes[cp].w < 0.0) {
                clipped = true;
                break;
            }
        }
        if (clipped)
            continue;

        // Project; Vulkan NDC containment (z in [0,1] — C.2).
        vec4 clip = g_d.mvp * vec4(point, 1.0);
        if (clip.w <= 0.0)
            continue;
        vec3 ndc = clip.xyz / clip.w;
        if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 ||
            ndc.z < 0.0 || ndc.z > 1.0)
            continue;

        // Framebuffer pixel. The house projections bake the Y-flip, so NDC y
        // already increases downward like the buffer rows — same formula as
        // GL, now in framebuffer orientation.
        ivec2 px = ivec2((ndc.xy * 0.5 + 0.5) * imageSize);

        int splatRadius = 0;
        if (g_d.splatMaxRadius > 0)
            splatRadius = clamp(int(splatScale / clip.w), 0, g_d.splatMaxRadius);

        pcProcessPoint(px, ndc.z, clip.w, splatRadius, index);
    }
}
