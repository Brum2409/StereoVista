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
// SINGLE-PASS MULTI-VIEW: one dispatch per cloud covers EVERY view. The point
// streams — the pass's dominant memory traffic — are read and decoded once,
// then the point is projected and written once per view; in stereo/XR that
// halves the stream reads, the decode ALU and the per-batch prologue instead
// of running the whole pass twice. The pushed address is the (cloud, view 0)
// dispatch struct; views > 0 sit SV_PC_DISPATCH_STRIDE apart behind it and
// only their view-varying members (matrices, target pointers) are read.
//
// The including shader must declare BEFORE including this file:
//   #version 460
//   #extension GL_EXT_shader_explicit_arithmetic_types_int64 : require
//   #extension GL_EXT_buffer_reference2 : require
//   #extension GL_EXT_scalar_block_layout : require
// and define, after the include:
//   void pcProcessPoint(uint view, ivec2 px, float ndcZ, float viewDepth,
//                       int radius, uint index);
// then call pcMain() from main(). g_d holds the view-0 dispatch data; the
// g_view* arrays hold the per-view state (write targets by view index).

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
// pass-specific pcProcessPoint. g_d is the VIEW-0 struct — its view-varying
// members (mvp/modelView/proj, target pointers, image size) are only valid
// for view 0; everything else (streams, clip planes, cloudID, splat/HQS
// parameters) is identical across views by construction (prepare() varies
// only the camera, the target offsets and the extent per view — views are
// (viewport, eye) pairs and viewports differ in extent).
PointCloudDispatch g_d;

// Per-view state filled by the pcMain prologue (indices < g_viewCount).
// The write-target addresses live here rather than in pcProcessPoint's
// signature so each pass constructs its restrict reference in the callee,
// same as before (glslang decorates pointer-typed argument temporaries
// AliasedPointer, which would defeat the restrict).
uint g_viewCount;
mat4 g_viewMvp[SV_PC_MAX_VIEWS];
bool g_viewVisible[SV_PC_MAX_VIEWS];
float g_viewSplatScale[SV_PC_MAX_VIEWS];
ivec2 g_viewImageSize[SV_PC_MAX_VIEWS];
uint64_t g_viewFramebuffer[SV_PC_MAX_VIEWS];
uint64_t g_viewHqsDepth[SV_PC_MAX_VIEWS];
uint64_t g_viewHqsAccum[SV_PC_MAX_VIEWS];

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
// packed-coordinate tiers each point read decodes. Per-view matrices and
// image size are parameters (multi-view: the merged decode uses the MOST
// precise level any visible view asks for).
int pcPrecisionLevel(mat4 modelView, mat4 proj, ivec2 imageSizeI, vec3 bmin,
                     vec3 bmax) {
    vec3 center = (bmin + bmax) * 0.5;
    float radius = distance(bmin, bmax);

    vec4 viewCenter = modelView * vec4(center, 1.0);
    vec4 viewEdge = viewCenter + vec4(radius, 0.0, 0.0, 0.0);
    vec4 projCenter = proj * viewCenter;
    vec4 projEdge = proj * viewEdge;
    if (projCenter.w <= 0.0)
        return 0;

    projCenter.xy /= projCenter.w;
    projEdge.xy /= projEdge.w;
    vec2 imageSize = vec2(imageSizeI);
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
    // Floor with the linear estimate so a degenerate (near-1D) AABB — e.g. a
    // scan-line batch — yields the along-the-line spacing instead of ~0.
    // Inert for compact batches: sqrt(d0*d1/N) >> d0/N whenever d1 > d0/N.
    return max(sqrt(area / float(numPoints)), d0 / float(numPoints));
}

// ── Density LOD (docs/POINTCLOUD_LOD.md Stage 1) ─────────────────────────────
// Smallest clip-space w over the batch AABB (w of point p is dot(row3, p, 1);
// the min over the box distributes per axis). This is the batch's nearest
// depth for perspective projections — the density estimate uses it so a batch
// spanning depth is thinned for its NEAREST (most demanding) part, never
// below target anywhere. Orthographic rows give w = 1: distance-independent
// density, still correct.
float pcNearestClipW(mat4 m, vec3 bmin, vec3 bmax) {
    float r3x = m[0][3], r3y = m[1][3], r3z = m[2][3], r3w = m[3][3];
    return min(r3x * bmin.x, r3x * bmax.x) +
           min(r3y * bmin.y, r3y * bmax.y) +
           min(r3z * bmin.z, r3z * bmax.z) + r3w;
}

// lowbias32 integer hash (Chris Wellons) → [0,1) jitter for the stride
// sampling. Deterministic in the point ordinal so the HQS depth and colour
// passes select the SAME subset, and the standard pass's framebuffer indices
// stay valid for the lookup pass.
uint pcHash(uint x) {
    x ^= x >> 16; x *= 0x7feb352du;
    x ^= x >> 15; x *= 0x846ca68bu;
    x ^= x >> 16;
    return x;
}
float pcHash01(uint x) {
    return float(pcHash(x)) * (1.0 / 4294967296.0);
}

// ── Per-point callback the including pass defines ─────────────────────────────
void pcProcessPoint(uint view, ivec2 px, float ndcZ, float viewDepth, int radius,
                    uint index);

// ── Shared main: one workgroup per batch, all views ───────────────────────────
void pcMain() {
    g_d = PcDispatchRef(pc.dispatchData).d;

    uint batchIndex = pc.baseBatch + gl_WorkGroupID.x;
    PointCloudBatch batch = PcBatches(g_d.batches).b[batchIndex];

    vec3 bmin = vec3(batch.minX, batch.minY, batch.minZ);
    vec3 bmax = vec3(batch.maxX, batch.maxY, batch.maxZ);
    vec3 boxSize = bmax - bmin;

    // View-invariant part of the per-point radius formula (the per-view rest
    // is radius_px = splatScale_v / clip.w, folded below). The density LOD
    // shares the spacing estimate.
    const bool lodOn = g_d.lodPointsPerPixel > 0.0;
    float worldSpacing = (g_d.splatMaxRadius > 0 || lodOn)
                             ? pcBatchSpacing(boxSize, batch.numPoints)
                             : 0.0;

    // Per-view prologue: cull, precision level, splat scale, density LOD keep
    // fraction, write targets. One AABB test per view replaces up to
    // kComputeBatchSize per-point clip tests; the decode level is the MOST
    // precise any visible view needs and the keep fraction is the LARGEST any
    // visible view needs (stereo eyes almost always agree) — the merged
    // subset must be one set, because points are decoded once and written to
    // every eye.
    g_viewCount = min(pc.viewCount, SV_PC_MAX_VIEWS);
    int level = 4;
    bool anyVisible = false;
    float lodKeep = lodOn ? 0.0 : 1.0;
    for (uint v = 0u; v < g_viewCount; ++v) {
        mat4 mvp, modelView, proj;
        if (v == 0u) {
            mvp = g_d.mvp;
            modelView = g_d.modelView;
            proj = g_d.proj;
            g_viewImageSize[v] = ivec2(g_d.imageWidth, g_d.imageHeight);
            g_viewFramebuffer[v] = g_d.framebuffer;
            g_viewHqsDepth[v] = g_d.hqsDepth;
            g_viewHqsAccum[v] = g_d.hqsAccum;
        } else {
            restrict PcDispatchRef dv = PcDispatchRef(
                pc.dispatchData + uint64_t(v) * uint64_t(SV_PC_DISPATCH_STRIDE));
            mvp = dv.d.mvp;
            modelView = dv.d.modelView;
            proj = dv.d.proj;
            g_viewImageSize[v] = ivec2(dv.d.imageWidth, dv.d.imageHeight);
            g_viewFramebuffer[v] = dv.d.framebuffer;
            g_viewHqsDepth[v] = dv.d.hqsDepth;
            g_viewHqsAccum[v] = dv.d.hqsAccum;
        }
        g_viewMvp[v] = mvp;
        g_viewSplatScale[v] = 0.0;
        g_viewVisible[v] = pcIntersectsFrustum(mvp, bmin, bmax);
        if (!g_viewVisible[v])
            continue;
        anyVisible = true;
        level = min(level, pcPrecisionLevel(modelView, proj, g_viewImageSize[v],
                                            bmin, bmax));

        // proj[1][1] carries the house Y-flip, hence abs() (the GL reference
        // had it positive).
        if (g_d.splatMaxRadius > 0 || lodOn) {
            float modelScale = length(vec3(modelView[0]));
            float pxPerUnit = abs(proj[1][1]) * float(g_viewImageSize[v].y) * 0.5;
            // Screen-space spacing of neighbouring points at clip w = 1.
            float spacingPxAtW1 = worldSpacing * modelScale * pxPerUnit;
            if (g_d.splatMaxRadius > 0)
                g_viewSplatScale[v] = 0.5 * spacingPxAtW1;
            if (lodOn) {
                // On-screen density at the batch's NEAREST depth is
                // 1/spacingPx² points per pixel; keep the fraction that
                // thins it to the lodPointsPerPixel budget. wNear <= 0
                // (camera beside/inside the box) → full detail.
                float wNear = pcNearestClipW(mvp, bmin, bmax);
                if (wNear <= 0.0) {
                    lodKeep = 1.0;
                } else {
                    float spacingPx = spacingPxAtW1 / wNear;
                    lodKeep = max(lodKeep,
                                  clamp(g_d.lodPointsPerPixel * spacingPx * spacingPx,
                                        0.0, 1.0));
                }
            }
        }
    }
    if (!anyVisible)
        return;

    // Density LOD thinning: sample nKeep of the batch's points with one
    // jittered pick per stride window. Points inside a batch are in Morton
    // order (loader phase 2), so a stride across the index range is an
    // approximately uniform spatial subsample and the jitter breaks the
    // Z-curve's structured patterns. The splat scale grows by 1/sqrt(keep) —
    // pcBatchSpacing of the surviving subset — so adaptive splats close the
    // thinned holes automatically.
    uint nKeep = uint(batch.numPoints);
    float lodStride = 1.0;
    if (lodKeep < 1.0 && batch.numPoints > 1) {
        nKeep = clamp(uint(lodKeep * float(batch.numPoints) + 0.5), 1u,
                      uint(batch.numPoints));
        lodStride = float(batch.numPoints) / float(nKeep);
        float splatGrow = inversesqrt(max(lodKeep, 1e-4));
        for (uint v = 0u; v < g_viewCount; ++v)
            g_viewSplatScale[v] *= splatGrow;
    }

    // restrict on the locals too: glslang otherwise decorates the pointer
    // VARIABLES AliasedPointer even when the block type is restrict.
    restrict PcUints xyz4b = PcUints(g_d.xyz4b);
    restrict PcUints xyz8b = PcUints(g_d.xyz8b);
    restrict PcUints xyz12b = PcUints(g_d.xyz12b);

    // Strided point loop: thread T handles ordinals T, T+128, ... (coalesced
    // reads at full density; at reduced density ordinal e maps to the point
    // picked uniformly at random from stride window [e, e+1)·lodStride, so
    // every window contributes exactly one point). e >= nKeep also covers the
    // old numPoints bound: nKeep == numPoints when the LOD is off.
    for (int i = 0; i < g_d.pointsPerThread; i++) {
        uint e = uint(i) * gl_WorkGroupSize.x + gl_LocalInvocationID.x;
        if (e >= nKeep)
            return;
        uint localIndex = e;
        if (nKeep < uint(batch.numPoints)) {
            float jitter = pcHash01(e * 0x9E3779B9u ^ batchIndex * 0x85EBCA6Bu);
            localIndex = min(uint((float(e) + jitter) * lodStride),
                             uint(batch.numPoints) - 1u);
        }
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

        // User section/clip planes (cloud-local space, like the decoded
        // point — view-invariant, so tested once per point).
        bool clipped = false;
        for (int cp = 0; cp < g_d.clipPlaneCount; ++cp) {
            if (dot(g_d.clipPlanes[cp].xyz, point) + g_d.clipPlanes[cp].w < 0.0) {
                clipped = true;
                break;
            }
        }
        if (clipped)
            continue;

        // Project the decoded point once per view; a point outside one eye's
        // frustum can still land in the other, so per-view rejects skip the
        // VIEW, not the point.
        for (uint v = 0u; v < g_viewCount; ++v) {
            if (!g_viewVisible[v])
                continue;

            // Vulkan NDC containment (z in [0,1] — C.2).
            vec4 clip = g_viewMvp[v] * vec4(point, 1.0);
            if (clip.w <= 0.0)
                continue;
            vec3 ndc = clip.xyz / clip.w;
            if (ndc.x < -1.0 || ndc.x > 1.0 || ndc.y < -1.0 || ndc.y > 1.0 ||
                ndc.z < 0.0 || ndc.z > 1.0)
                continue;

            // Framebuffer pixel (this view's extent — viewports differ). The
            // house projections bake the Y-flip, so NDC y already increases
            // downward like the buffer rows — same formula as GL, now in
            // framebuffer orientation.
            ivec2 px = ivec2((ndc.xy * 0.5 + 0.5) * vec2(g_viewImageSize[v]));

            int splatRadius = 0;
            if (g_d.splatMaxRadius > 0)
                splatRadius =
                    clamp(int(g_viewSplatScale[v] / clip.w), 0, g_d.splatMaxRadius);

            pcProcessPoint(v, px, ndc.z, clip.w, splatRadius, index);
        }
    }
}
