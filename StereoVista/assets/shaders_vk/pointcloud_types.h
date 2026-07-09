#ifndef SV_POINTCLOUD_TYPES_H
#define SV_POINTCLOUD_TYPES_H

// ============================================================================
// GPU structs of the Schütz compute point-cloud pipeline (Phase 5), shared
// between C++ and GLSL exactly like scene_types.h (playbook A.1) — see the
// rules in that header. Kept SEPARATE from scene_types.h because these need
// 64-bit integers (GL_EXT_shader_explicit_arithmetic_types_int64), which the
// mesh/sky shaders must not be forced to require.
//
//   * GLSL: included via pointcloud_common.glsl; uint64_t members hold
//     buffer device addresses and are cast to buffer_reference types at use.
//   * C++: included by headers/Renderer/GpuTypes.h (after scene_types.h);
//     uint64_t members receive VkDeviceAddress values.
//
// The per-dispatch data does NOT fit in the 128-byte push-constant minimum
// (3 matrices + clip planes + 9 buffer pointers), so every dispatch reads ONE
// PointCloudDispatch through a buffer address pushed as a single pointer
// (playbook A.2: zero descriptor sets in the whole pipeline).
// ============================================================================

#define SV_PC_CLIP_PLANES 6
#define SV_PC_RASTER_WORKGROUP 128
#define SV_PC_LOOKUP_WORKGROUP 256
// cloudID packs into 8 bits of the 64-bit framebuffer entry.
#define SV_PC_MAX_CLOUDS 256

// Batch descriptor — must stay bit-identical to Engine::ComputeBatch (32
// bytes; identical under scalar and std430 rules).
struct PointCloudBatch {   // 32 bytes
    float minX, minY, minZ;
    float maxX, maxY, maxZ;
    int numPoints;
    int firstPoint;
};

// One (cloud, view) unit of work, shared by the rasterize / HQS depth / HQS
// color dispatches of that pair and read by the lookup pass. Built per frame
// by renderer::PointCloudPass into a HostUpload staging buffer and copied
// into a DEVICE-LOCAL array at the top of the frame — every geometry
// workgroup reads the whole struct and the lookup pass dereferences it per
// pixel, so these reads must hit VRAM/L2, never host memory across PCIe.
// sizeof is padded to 400 (multiple of 16) so the mat4/vec4 members of every
// array element sit on 16-byte addresses (aligned vector loads).
struct PointCloudDispatch { // 400 bytes
    mat4 mvp;               // proj * view * model of this view
    mat4 modelView;         // view * model (precision-level sphere projection)
    mat4 proj;              // this view's projection (precision level + splat)
    // Section/clip planes in the CLOUD'S LOCAL space (host pre-multiplies the
    // world planes by transpose(model)); discard when dot(n,p)+w < 0.
    vec4 clipPlanes[SV_PC_CLIP_PLANES];
    // Cloud sections (renderer::PointCloudGpuAddresses).
    uint64_t batches;       // PointCloudBatch[numBatches]
    uint64_t xyz4b;         // uint[points] — coarsest 10 bits/axis
    uint64_t xyz8b;         // uint[points] — middle 10 bits/axis
    uint64_t xyz12b;        // uint[points] — finest 10 bits/axis
    uint64_t rgba;          // uint[points] — packed RGBA8
    // Pass-owned targets, ALREADY OFFSET to this view's section.
    uint64_t framebuffer;   // uint64[pixels] — (dist24|cloudID|index), sentinel ~0
    uint64_t colorbuffer;   // uint[pixels]   — lookup output (packed RGBA8)
    uint64_t hqsDepth;      // uint[pixels]   — nearest linear depth bits, sentinel ~0
    uint64_t hqsAccum;      // uint[pixels*4] — R,G,B,count accumulators
    int imageWidth;
    int imageHeight;
    int pointsPerThread;    // ceil(kComputeBatchSize / SV_PC_RASTER_WORKGROUP)
    uint cloudID;           // < SV_PC_MAX_CLOUDS
    int splatMaxRadius;     // 0 = single-pixel; >0 = adaptive splat clamp (px)
    int clipPlaneCount;     // 0..SV_PC_CLIP_PLANES
    float hqsThreshold;     // relative depth window (0.01 = 1%)
    uint pad0;
    uint pad1;              // pad to 400 bytes: 16-aligned array stride
    uint pad2;
};

// Push constants of the three geometry-processing compute passes. baseBatch
// splits huge clouds across several vkCmdDispatch when numBatches exceeds
// maxComputeWorkGroupCount[0] (only 65535 is guaranteed). The explicit pads
// here and below make sizeof() in C++ equal the GLSL scalar block size, so
// pushing sizeof(struct) bytes never exceeds the reflected range.
struct PointCloudComputePush { // 16 bytes
    uint64_t dispatchData;     // PointCloudDispatch*
    uint baseBatch;
    uint pad0;
};

// Push constants of the colour-lookup pass. ✎ Better than GL: instead of one
// fullscreen dispatch PER CLOUD (each re-reading every framebuffer entry),
// ONE dispatch per view resolves every pixel — the packed cloudID selects the
// owning cloud's PointCloudDispatch from the dispatch array (BDA pointer
// arithmetic), which carries that cloud's rgba pointer. Requires cloudIDs to
// be in range, so PointCloudPass renders at most SV_PC_MAX_CLOUDS clouds
// (an out-of-range id would be an unguarded out-of-bounds BDA read — the GL
// "extra clouds show wrong colours" fallback is not portable to pointers).
struct PointCloudLookupPush { // 32 bytes
    uint64_t framebuffer;     // this view's section
    uint64_t colorbuffer;     // this view's section
    uint64_t dispatchBase;    // PointCloudDispatch of (cloud 0, this view)
    uint cloudStrideBytes;    // sizeof(PointCloudDispatch) * viewCount
    uint pixelCount;          // pixelsPerView
};

// Push constants of the standard fullscreen resolve (multiview: the shader
// offsets the base pointers by gl_ViewIndex * pixelsPerView entries).
struct PointCloudResolvePush { // 32 bytes
    uint64_t framebuffer;      // uint64[pixelsPerView * views], view 0 base
    uint64_t colorbuffer;      // uint[pixelsPerView * views], view 0 base
    int imageWidth;
    int imageHeight;
    uint pixelsPerView;
    uint pad0;
};

// Push constants of the HQS fullscreen resolve. projAB[view] holds
// (proj[2][2], proj[3][2]) of that view's projection: window depth of linear
// eye depth d reconstructs as  depth = -A + B / d  (reverse-Z [0,1] directly;
// both coefficients are unaffected by asymmetric-frustum x/y shear, so this
// stays correct for the Phase 7 stereo eyes).
struct PointCloudHqsResolvePush { // 64 bytes
    uint64_t hqsDepth;            // uint[pixelsPerView * views], view 0 base
    uint64_t hqsAccum;            // uint[pixelsPerView * views * 4], view 0 base
    vec4 projAB[2];               // [SV_MAX_VIEWS]; xy = (A, B), zw unused
    int imageWidth;
    int imageHeight;
    uint pixelsPerView;
    uint pad0;
};

#endif // SV_POINTCLOUD_TYPES_H
