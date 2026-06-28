// point_cloud_loader.cpp
#include "Loaders/PointCloudLoader.h"
#include "Engine/OctreePointCloudManager.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <queue>
#include <condition_variable>
#include <glad/glad.h>
#include <cstring>
#include <filesystem>
#include <future>
#include <map>
#include <omp.h>
#include <glm/gtx/norm.hpp>
#include <utility>

#include <random>
#include <execution>
#include <algorithm>
#include <cmath>

#include <Utils/octree.h>

// HDF5 includes
#include <hdf5/H5Cpp.h>
#include <chrono>
#include <ctime>
using namespace H5;

// LASzip – handles both LAS and LAZ transparently
#ifndef LASZIP_DYN_LINK
#  define LASZIP_DYN_LINK   // required to get __declspec(dllimport) on Windows
#endif
#include <laszip/laszip_api.h>


namespace Engine {

    // =========================================================================
    // Streaming compute SSBO infrastructure
    // =========================================================================
    // These helpers let every loader stream points directly to GPU SSBOs one
    // batch (kComputeBatchSize = 10240 points) at a time.
    // Peak CPU RAM = one batch (~280 KB) regardless of point cloud size.
    //
    // Coordinate quantisation matches the Schütz compute rasterizer exactly:
    //   normalised = (pos - batchMin) / batchSize   ∈ [0,1]
    //   bits30     = uint32( normalised * 2^30 )
    //   ssXyz_4b   packs bits30[29:20]  (coarsest; always read by shader)
    //   ssXyz_8b   packs bits30[19:10]
    //   ssXyz_12b  packs bits30[ 9: 0]  (finest; level-0 only)
    // =========================================================================

    static void deleteComputeSSBOs(PointCloud& pc) {
        auto del = [](GLuint& id) { if (id) { glDeleteBuffers(1, &id); id = 0; } };
        del(pc.computeBatchSSBO);
        del(pc.computeXyz12bSSBO);
        del(pc.computeXyz8bSSBO);
        del(pc.computeXyz4bSSBO);
        del(pc.computeRGBASSBO);
    }


    // ── libmorton "magic bits" 3D Morton (Z-order) encode ─────────────────────
    // Spread the low 21 bits of an axis to every third bit of a 63-bit value, then
    // interleave x/y/z (after libmorton / Fabian Giesen).  Sorting points by this
    // code lays them out along a Z-order curve so spatially-near points are
    // adjacent in memory.
    static inline uint64_t mortonSplitBy3(uint32_t a) {
        uint64_t x = static_cast<uint64_t>(a) & 0x1fffffULL;        // keep 21 bits
        x = (x | x << 32) & 0x1f00000000ffffULL;
        x = (x | x << 16) & 0x1f0000ff0000ffULL;
        x = (x | x << 8)  & 0x100f00f00f00f00fULL;
        x = (x | x << 4)  & 0x10c30c30c30c30c3ULL;
        x = (x | x << 2)  & 0x1249249249249249ULL;
        return x;
    }
    static inline uint64_t mortonEncode3D(uint32_t x, uint32_t y, uint32_t z) {
        return mortonSplitBy3(x) | (mortonSplitBy3(y) << 1) | (mortonSplitBy3(z) << 2);
    }

    // ── Build the compute SSBOs from a cloud's full in-memory point list ──────
    // Schütz's vertex-order optimisation, applied once at load:
    //   1. sort points along a 3D Morton (Z-order) curve   → spatial locality;
    //   2. cut the sorted sequence into fixed-size batches  → tight batch AABBs
    //      (so the per-batch frustum cull and precision-LOD actually work, and
    //       neighbouring points share cache lines);
    //   3. randomly relocate whole batches, keeping each batch's internal order,
    //      so that spatially-adjacent batches are no longer dispatched together —
    //      this is what removes the atomicMin contention they would otherwise
    //      cause (the dominant cost of the rasterize pass).
    // The shuffled layout is written straight into the coordinate/colour SSBOs, so
    // batch t owns the contiguous range [firstPoint, firstPoint+count) and the
    // rasterizer needs no separate dispatch permutation.
    //
    // This replaces the old per-batch streaming uploads (and the post-load GPU
    // decode round-trip) with a parallel quantise plus one bulk upload per SSBO —
    // the bulk of the load-time speed-up.  `points` is consumed (freed) on success;
    // quantisation matches the rasterize shader exactly (per-batch 30-bit, three
    // 10-bit tiers; the RGBA alpha byte carries point intensity).
    static void buildComputeCloud(PointCloud& pc, std::vector<PointCloudPoint>& points) {
        deleteComputeSSBOs(pc);
        if (pc.vbo) { glDeleteBuffers(1, &pc.vbo); pc.vbo = 0; }
        if (pc.vao) { glDeleteVertexArrays(1, &pc.vao); pc.vao = 0; }
        pc.numBatches      = 0;
        pc.totalPointCount = 0;

        const size_t N = points.size();
        if (N == 0) return;

        // ── 1. Global bounds ──────────────────────────────────────────────────
        glm::vec3 gMin( FLT_MAX), gMax(-FLT_MAX);
        for (const auto& p : points) { gMin = glm::min(gMin, p.position); gMax = glm::max(gMax, p.position); }
        pc.boundsMin = gMin;
        pc.boundsMax = gMax;

        // ── 2. Morton-code sort (parallel) ────────────────────────────────────
        const glm::vec3 ext = glm::max(gMax - gMin, glm::vec3(1e-9f));
        constexpr uint32_t MAXQ = (1u << 21) - 1u;     // 21 bits/axis → 63-bit code
        std::vector<std::pair<uint64_t, uint32_t>> order(N);
        #pragma omp parallel for schedule(static)
        for (long long i = 0; i < static_cast<long long>(N); ++i) {
            const glm::vec3 n = glm::clamp((points[i].position - gMin) / ext, 0.0f, 1.0f);
            const uint32_t qx = static_cast<uint32_t>(n.x * static_cast<float>(MAXQ));
            const uint32_t qy = static_cast<uint32_t>(n.y * static_cast<float>(MAXQ));
            const uint32_t qz = static_cast<uint32_t>(n.z * static_cast<float>(MAXQ));
            order[static_cast<size_t>(i)] =
                { mortonEncode3D(qx, qy, qz), static_cast<uint32_t>(i) };
        }
        std::sort(std::execution::par, order.begin(), order.end(),
                  [](const std::pair<uint64_t, uint32_t>& a,
                     const std::pair<uint64_t, uint32_t>& b) { return a.first < b.first; });

        // ── 3. Partition into batches and randomly relocate them ──────────────
        const int      B          = PointCloud::kComputeBatchSize;
        const uint32_t numBatches = static_cast<uint32_t>((N + B - 1) / B);
        std::vector<uint32_t> batchPerm(numBatches);
        for (uint32_t i = 0; i < numBatches; ++i) batchPerm[i] = i;
        std::mt19937 rng(0x5eed1234u);                 // fixed seed → reproducible
        std::shuffle(batchPerm.begin(), batchPerm.end(), rng);

        // Output offset of each post-shuffle batch (handles the partial last one).
        std::vector<uint32_t> outFirst(numBatches);
        for (uint32_t t = 0, off = 0; t < numBatches; ++t) {
            const uint32_t src   = batchPerm[t];
            const uint32_t count = static_cast<uint32_t>(
                std::min<size_t>(static_cast<size_t>(B), N - static_cast<size_t>(src) * B));
            outFirst[t] = off;
            off += count;
        }

        // ── 4. Quantise into flat arrays, in final order (parallel over batches)
        constexpr uint32_t STEPS_30BIT = 1u << 30;     // 2^30
        constexpr uint32_t MASK_10BIT  = 1023u;
        std::vector<uint32_t>     xyz4b(N), xyz8b(N), xyz12b(N), rgba(N);
        std::vector<ComputeBatch> batches(numBatches);

        #pragma omp parallel for schedule(dynamic)
        for (long long t = 0; t < static_cast<long long>(numBatches); ++t) {
            const uint32_t src      = batchPerm[static_cast<size_t>(t)];
            const size_t   srcStart = static_cast<size_t>(src) * B;
            const uint32_t count    = static_cast<uint32_t>(std::min<size_t>(static_cast<size_t>(B), N - srcStart));
            const uint32_t first    = outFirst[static_cast<size_t>(t)];

            glm::vec3 bMin( FLT_MAX), bMax(-FLT_MAX);
            for (uint32_t k = 0; k < count; ++k) {
                const glm::vec3& p = points[order[srcStart + k].second].position;
                bMin = glm::min(bMin, p);
                bMax = glm::max(bMax, p);
            }
            const glm::vec3 sz = glm::max(bMax - bMin, glm::vec3(1e-6f));
            batches[static_cast<size_t>(t)] = { bMin.x, bMin.y, bMin.z, bMax.x, bMax.y, bMax.z,
                                                static_cast<int>(count), static_cast<int>(first) };

            for (uint32_t k = 0; k < count; ++k) {
                const PointCloudPoint& P = points[order[srcStart + k].second];
                auto q = [&](float v, float lo, float s) -> uint32_t {
                    const float nn = glm::clamp((v - lo) / s, 0.0f, 1.0f);
                    return std::min(static_cast<uint32_t>(nn * static_cast<float>(STEPS_30BIT)),
                                    STEPS_30BIT - 1u);
                };
                const uint32_t Xb = q(P.position.x, bMin.x, sz.x);
                const uint32_t Yb = q(P.position.y, bMin.y, sz.y);
                const uint32_t Zb = q(P.position.z, bMin.z, sz.z);
                const uint32_t o  = first + k;
                xyz4b [o] = ((Xb>>20)&MASK_10BIT) | (((Yb>>20)&MASK_10BIT)<<10) | (((Zb>>20)&MASK_10BIT)<<20);
                xyz8b [o] = ((Xb>>10)&MASK_10BIT) | (((Yb>>10)&MASK_10BIT)<<10) | (((Zb>>10)&MASK_10BIT)<<20);
                xyz12b[o] = (Xb&MASK_10BIT) | ((Yb&MASK_10BIT)<<10) | ((Zb&MASK_10BIT)<<20);
                const uint32_t r8 = static_cast<uint32_t>(glm::clamp(P.color.r,    0.f, 1.f) * 255.f + 0.5f);
                const uint32_t g8 = static_cast<uint32_t>(glm::clamp(P.color.g,    0.f, 1.f) * 255.f + 0.5f);
                const uint32_t b8 = static_cast<uint32_t>(glm::clamp(P.color.b,    0.f, 1.f) * 255.f + 0.5f);
                const uint32_t a8 = static_cast<uint32_t>(glm::clamp(P.intensity, 0.f, 1.f) * 255.f + 0.5f);
                rgba[o] = (a8<<24)|(b8<<16)|(g8<<8)|r8;   // alpha carries intensity
            }
        }

        // ── 5. Bulk upload: one glBufferData per SSBO ─────────────────────────
        auto upload = [](GLuint& id, const void* data, GLsizeiptr bytes) {
            glGenBuffers(1, &id);
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
            glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, data, GL_STATIC_DRAW);
        };
        upload(pc.computeBatchSSBO,  batches.data(), static_cast<GLsizeiptr>(batches.size() * sizeof(ComputeBatch)));
        upload(pc.computeXyz4bSSBO,  xyz4b.data(),   static_cast<GLsizeiptr>(N * sizeof(uint32_t)));
        upload(pc.computeXyz8bSSBO,  xyz8b.data(),   static_cast<GLsizeiptr>(N * sizeof(uint32_t)));
        upload(pc.computeXyz12bSSBO, xyz12b.data(),  static_cast<GLsizeiptr>(N * sizeof(uint32_t)));
        upload(pc.computeRGBASSBO,   rgba.data(),    static_cast<GLsizeiptr>(N * sizeof(uint32_t)));
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        pc.numBatches             = numBatches;
        pc.totalPointCount        = static_cast<uint32_t>(N);
        pc.computePointsPerThread = (B + 127) / 128;

        std::vector<PointCloudPoint>().swap(points);   // release CPU-side points

        std::cout << "[ComputePC] Built " << N << " points into " << numBatches
                  << " Morton-sorted, shuffled batches\n";
    }

    // =========================================================================
    // Progressive (streaming) LAS/LAZ loading – Schütz "compute_loop_las" style
    // =========================================================================
    // Mirrors m-schuetz/compute_rasterizer's LasLoaderSparse, with a two-phase
    // ordering strategy:
    //
    //   Phase 1 (instant display, Schütz's literal loader): a background worker
    //     reads the file in chunks and produces ready-to-upload GPU staging
    //     ("StreamChunk"s) in *file order* (no sort). The GL/main thread drains
    //     them each frame in updateStreaming() via glBufferSubData into SSBOs
    //     pre-allocated to the exact header point count, growing pc.numBatches as
    //     data arrives so the compute rasterizer draws the cloud while it loads.
    //
    //   Phase 2 (full speed, optional): once the whole file is in, the worker
    //     runs a single global per-file Morton sort + batch shuffle (exactly the
    //     buildComputeCloud transform) over the accumulated points, producing
    //     re-ordered arrays that the main thread swaps into the SSBOs in place.
    //     Re-ordering points never changes the rendered image, only batch AABB
    //     tightness, so the swap is visually transparent. Both phases keep the
    //     partial last batch in the final slot, so phase-1 and phase-2 batch
    //     boundaries align (multiples of kComputeBatchSize) and the swap is
    //     point/descriptor-consistent. Phase 2 is the default; it can be disabled
    //     (mortonResort=false) for the pure file-order Schütz path.
    //
    // The LAS header is exploited up front: exact point count (precise SSBO
    // pre-allocation + progress, no counting pass) and exact bounds (camera
    // framing and SpaceMouse extents are valid before a single point is read).

    // ── Optional GL_ARB_sparse_buffer support ────────────────────────────────
    // Schütz allocates the point SSBOs as *sparse* buffers and commits physical
    // pages on demand as data streams in, so VRAM use tracks the loaded portion
    // rather than the full reservation. Our vendored GLAD does not expose the
    // extension, so we load the entry point manually (set up from main once the
    // GL context exists) and fall back to a normal glBufferData reservation when
    // it is unavailable.
#ifndef GL_SPARSE_STORAGE_BIT_ARB
#define GL_SPARSE_STORAGE_BIT_ARB 0x0400
#endif
#ifndef GL_SPARSE_BUFFER_PAGE_SIZE_ARB
#define GL_SPARSE_BUFFER_PAGE_SIZE_ARB 0x82F8
#endif
    typedef void (APIENTRYP PFN_glBufferPageCommitmentARB_t)(GLenum target, GLintptr offset, GLsizeiptr size, GLboolean commit);
    static PFN_glBufferPageCommitmentARB_t s_glBufferPageCommitmentARB = nullptr;
    static void* (*s_glProcLoader)(const char*) = nullptr;
    static GLint s_sparsePageSize  = 0;
    static bool  s_sparseChecked   = false;
    static bool  s_sparseAvailable = false;

    // True if sparse buffers can be used (entry point loaded + page size known).
    static bool sparseBuffersAvailable() {
        if (s_sparseChecked) return s_sparseAvailable;
        s_sparseChecked = true;
        if (s_glProcLoader) {
            s_glBufferPageCommitmentARB = reinterpret_cast<PFN_glBufferPageCommitmentARB_t>(
                s_glProcLoader("glBufferPageCommitmentARB"));
        }
        if (s_glBufferPageCommitmentARB) {
            glGetIntegerv(GL_SPARSE_BUFFER_PAGE_SIZE_ARB, &s_sparsePageSize);
            if (glGetError() == GL_NO_ERROR && s_sparsePageSize > 0)
                s_sparseAvailable = true;
        }
        std::cout << "[LAS-Stream] Sparse buffers "
                  << (s_sparseAvailable ? "available (page size "
                      + std::to_string(s_sparsePageSize) + ")" : "unavailable – using full reservation")
                  << "\n";
        return s_sparseAvailable;
    }

    // Commit the sparse pages covering [byteOffset, byteOffset+byteSize) of the
    // currently-bound SSBO (Schütz's alignment).  No-op unless this cloud's
    // buffers were actually created sparse (sparse==true). The entry point and
    // page size are stable once probed, so this stays correct even if a later
    // cloud falls back to non-sparse.
    static void commitSparseRange(bool sparse, GLintptr byteOffset, GLsizeiptr byteSize, GLsizeiptr bufferBytes) {
        if (!sparse || !s_glBufferPageCommitmentARB || s_sparsePageSize <= 0 || byteSize <= 0) return;
        const GLintptr  page  = static_cast<GLintptr>(s_sparsePageSize);
        const GLintptr  aOff  = byteOffset - (byteOffset % page);
        GLsizeiptr      aSize = byteSize + (byteOffset - aOff);
        aSize = ((aSize + page - 1) / page) * page;             // round up to page
        if (aOff + aSize > bufferBytes) aSize = bufferBytes - aOff; // clamp to buffer
        if (aSize > 0)
            s_glBufferPageCommitmentARB(GL_SHADER_STORAGE_BUFFER, aOff, aSize, GL_TRUE);
    }

    struct StreamChunk {
        uint32_t firstPoint = 0;   // base offset into the per-point SSBOs
        uint32_t firstBatch = 0;   // base offset into the batch-descriptor SSBO
        uint32_t numPoints  = 0;
        std::vector<ComputeBatch> batches;
        std::vector<uint32_t>     xyz4b, xyz8b, xyz12b, rgba; // chunk-local arrays
    };

    // Background-thread + producer/consumer state for one streaming cloud.
    // Heap-allocated and owned by PointCloud via shared_ptr; the worker holds a
    // raw pointer that stays valid because ~PointCloudStream joins the thread
    // before the object is destroyed.  PointCloud only ever moves the shared_ptr,
    // never this object, so the raw pointer survives vector reallocations.
    struct PointCloudStream {
        std::thread             worker;
        std::mutex              mtx;
        std::condition_variable cv;          // signals both produce and consume
        std::queue<StreamChunk> ready;       // phase-1 chunks awaiting upload
        std::atomic<bool>       stop{false}; // request worker exit (cloud removed)
        std::atomic<bool>       phase1Done{false};
        std::atomic<bool>       finished{false};
        std::atomic<bool>       failed{false};
        size_t                  maxInFlight = 4; // backpressure → bounds CPU RAM
        bool                    resort = true;   // do phase 2 (global Morton sort)?
        bool                    sparse = false;  // were the SSBOs created sparse?
        uint32_t                targetPoints  = 0;
        uint32_t                targetBatches = 0;

        // ── Phase 2 result (built by the worker, swapped in by the GL thread) ──
        // Re-ordered, fully-quantised arrays for the whole cloud. Boundaries are
        // aligned to kComputeBatchSize so they overwrite the phase-1 SSBOs in
        // place. resortReady flips true when these are populated.
        std::atomic<bool>         resortReady{false};
        std::vector<uint32_t>     r_xyz4b, r_xyz8b, r_xyz12b, r_rgba;
        std::vector<ComputeBatch> r_batches;

        ~PointCloudStream() {
            stop.store(true);
            cv.notify_all();
            if (worker.joinable()) worker.join();
        }
    };

    // Quantise one point relative to its batch bbox [bMin, bMin+sz] into the
    // three 10-bit tiers + packed RGBA (alpha carries intensity), writing output
    // index o.  Identical encoding to buildComputeCloud.
    static inline void quantizePoint(const PointCloudPoint& P,
                                     const glm::vec3& bMin, const glm::vec3& sz, uint32_t o,
                                     uint32_t* xyz4b, uint32_t* xyz8b, uint32_t* xyz12b, uint32_t* rgba) {
        constexpr uint32_t STEPS_30BIT = 1u << 30;
        constexpr uint32_t MASK_10BIT  = 1023u;
        auto q = [](float v, float lo, float ss) -> uint32_t {
            const float nn = glm::clamp((v - lo) / ss, 0.0f, 1.0f);
            return std::min(static_cast<uint32_t>(nn * static_cast<float>(STEPS_30BIT)), STEPS_30BIT - 1u);
        };
        const uint32_t Xb = q(P.position.x, bMin.x, sz.x);
        const uint32_t Yb = q(P.position.y, bMin.y, sz.y);
        const uint32_t Zb = q(P.position.z, bMin.z, sz.z);
        xyz4b [o] = ((Xb>>20)&MASK_10BIT) | (((Yb>>20)&MASK_10BIT)<<10) | (((Zb>>20)&MASK_10BIT)<<20);
        xyz8b [o] = ((Xb>>10)&MASK_10BIT) | (((Yb>>10)&MASK_10BIT)<<10) | (((Zb>>10)&MASK_10BIT)<<20);
        xyz12b[o] = (Xb&MASK_10BIT) | ((Yb&MASK_10BIT)<<10) | ((Zb&MASK_10BIT)<<20);
        const uint32_t r8 = static_cast<uint32_t>(glm::clamp(P.color.r,    0.f, 1.f) * 255.f + 0.5f);
        const uint32_t g8 = static_cast<uint32_t>(glm::clamp(P.color.g,    0.f, 1.f) * 255.f + 0.5f);
        const uint32_t b8 = static_cast<uint32_t>(glm::clamp(P.color.b,    0.f, 1.f) * 255.f + 0.5f);
        const uint32_t a8 = static_cast<uint32_t>(glm::clamp(P.intensity, 0.f, 1.f) * 255.f + 0.5f);
        rgba[o] = (a8<<24)|(b8<<16)|(g8<<8)|r8;
    }

    // Phase 1: build a file-order StreamChunk from pts[0..N) (no sort) — Schütz's
    // literal loader layout. Batches are consecutive runs of kComputeBatchSize,
    // each with its own bbox; the last batch of a chunk may be partial. The very
    // last chunk's tail is the only globally-partial batch, so global batch
    // boundaries are multiples of kComputeBatchSize (matching phase 2).
    static void buildFileOrderChunk(const PointCloudPoint* pts, size_t N,
                                    uint32_t globalFirstPoint, uint32_t globalFirstBatch,
                                    StreamChunk& out) {
        out.firstPoint = globalFirstPoint;
        out.firstBatch = globalFirstBatch;
        out.numPoints  = static_cast<uint32_t>(N);
        if (N == 0) return;
        const int      B    = PointCloud::kComputeBatchSize;
        const uint32_t numB = static_cast<uint32_t>((N + B - 1) / B);
        out.xyz4b.resize(N); out.xyz8b.resize(N); out.xyz12b.resize(N); out.rgba.resize(N);
        out.batches.resize(numB);
        for (uint32_t t = 0; t < numB; ++t) {
            const size_t   first = static_cast<size_t>(t) * B;
            const uint32_t count = static_cast<uint32_t>(std::min<size_t>(static_cast<size_t>(B), N - first));
            glm::vec3 bMin( FLT_MAX), bMax(-FLT_MAX);
            for (uint32_t k = 0; k < count; ++k) { const glm::vec3& p = pts[first+k].position; bMin = glm::min(bMin,p); bMax = glm::max(bMax,p); }
            const glm::vec3 sz = glm::max(bMax - bMin, glm::vec3(1e-6f));
            out.batches[t] = { bMin.x,bMin.y,bMin.z, bMax.x,bMax.y,bMax.z,
                               static_cast<int>(count),
                               static_cast<int>(globalFirstPoint + static_cast<uint32_t>(first)) };
            for (uint32_t k = 0; k < count; ++k)
                quantizePoint(pts[first+k], bMin, sz, static_cast<uint32_t>(first) + k,
                              out.xyz4b.data(), out.xyz8b.data(), out.xyz12b.data(), out.rgba.data());
        }
    }

    // Phase 2: global per-file Morton sort + batch shuffle + quantise the whole
    // cloud into the given arrays (pure CPU, no GL). Mirrors buildComputeCloud,
    // except the partial batch (if any) is kept LAST so every output offset is a
    // multiple of kComputeBatchSize — i.e. phase-2 batch boundaries match phase-1
    // exactly, letting the GL thread overwrite the SSBOs in place.
    static void computeCloudArrays(const std::vector<PointCloudPoint>& pts,
                                   std::vector<uint32_t>& xyz4b, std::vector<uint32_t>& xyz8b,
                                   std::vector<uint32_t>& xyz12b, std::vector<uint32_t>& rgba,
                                   std::vector<ComputeBatch>& batches) {
        const size_t N = pts.size();
        if (N == 0) return;
        glm::vec3 gMin( FLT_MAX), gMax(-FLT_MAX);
        for (const auto& p : pts) { gMin = glm::min(gMin, p.position); gMax = glm::max(gMax, p.position); }
        const glm::vec3 ext = glm::max(gMax - gMin, glm::vec3(1e-9f));
        constexpr uint32_t MAXQ = (1u << 21) - 1u;

        std::vector<std::pair<uint64_t, uint32_t>> order(N);
        #pragma omp parallel for schedule(static)
        for (long long i = 0; i < static_cast<long long>(N); ++i) {
            const glm::vec3 n = glm::clamp((pts[static_cast<size_t>(i)].position - gMin) / ext, 0.0f, 1.0f);
            order[static_cast<size_t>(i)] =
                { mortonEncode3D(static_cast<uint32_t>(n.x * static_cast<float>(MAXQ)),
                                 static_cast<uint32_t>(n.y * static_cast<float>(MAXQ)),
                                 static_cast<uint32_t>(n.z * static_cast<float>(MAXQ))),
                  static_cast<uint32_t>(i) };
        }
        std::sort(std::execution::par, order.begin(), order.end(),
                  [](const std::pair<uint64_t,uint32_t>& a,
                     const std::pair<uint64_t,uint32_t>& b) { return a.first < b.first; });

        const int      B       = PointCloud::kComputeBatchSize;
        const uint32_t numB    = static_cast<uint32_t>((N + B - 1) / B);
        const uint32_t numFull = static_cast<uint32_t>(N / B); // size-B batches
        // Shuffle the full batches only; the partial batch (sorted index numFull)
        // stays in the final slot so output offsets remain multiples of B.
        std::vector<uint32_t> perm(numB);
        for (uint32_t i = 0; i < numB; ++i) perm[i] = i;
        std::mt19937 rng(0x5eed1234u);
        if (numFull > 1) std::shuffle(perm.begin(), perm.begin() + numFull, rng);

        xyz4b.resize(N); xyz8b.resize(N); xyz12b.resize(N); rgba.resize(N);
        batches.resize(numB);

        #pragma omp parallel for schedule(dynamic)
        for (long long t = 0; t < static_cast<long long>(numB); ++t) {
            const uint32_t src   = perm[static_cast<size_t>(t)];
            const size_t   s     = static_cast<size_t>(src) * B;
            const uint32_t count = static_cast<uint32_t>(std::min<size_t>(static_cast<size_t>(B), N - s));
            const uint32_t first = static_cast<uint32_t>(t) * B; // aligned (full batches first)
            glm::vec3 bMin( FLT_MAX), bMax(-FLT_MAX);
            for (uint32_t k = 0; k < count; ++k) { const glm::vec3& p = pts[order[s+k].second].position; bMin=glm::min(bMin,p); bMax=glm::max(bMax,p); }
            const glm::vec3 sz = glm::max(bMax - bMin, glm::vec3(1e-6f));
            batches[static_cast<size_t>(t)] = { bMin.x,bMin.y,bMin.z, bMax.x,bMax.y,bMax.z,
                                                static_cast<int>(count), static_cast<int>(first) };
            for (uint32_t k = 0; k < count; ++k)
                quantizePoint(pts[order[s+k].second], bMin, sz, first + k,
                              xyz4b.data(), xyz8b.data(), xyz12b.data(), rgba.data());
        }
    }

    // Worker thread body. Pure CPU work – no GL calls happen here.
    //   Phase 1: decode the file in chunks and hand file-order StreamChunks to
    //            the consumer with backpressure (instant display).
    //   Phase 2: if resorting, run one global Morton sort over the points kept
    //            during phase 1 and publish the re-ordered arrays for an in-place
    //            SSBO swap by the GL thread.
    static void streamWorkerLAS(PointCloudStream* S, std::string filePath, size_t downsample,
                                double sx, double sy, double sz,
                                double ox, double oy, double oz,
                                double cx, double cy, double cz,
                                bool hasColor, float colorScale, int64_t nPoints) {
        auto bail = [&]() { S->failed.store(true); S->phase1Done.store(true); S->finished.store(true); S->cv.notify_all(); };

        laszip_POINTER reader = nullptr; laszip_BOOL comp = 0;
        if (laszip_create(&reader)) { bail(); return; }
        if (laszip_open_reader(reader, filePath.c_str(), &comp)) { laszip_destroy(reader); bail(); return; }
        laszip_point_struct* lp = nullptr; laszip_get_point_pointer(reader, &lp);

        const int    B      = PointCloud::kComputeBatchSize;
        const size_t CHUNK  = static_cast<size_t>(64) * B;   // ~655k points / chunk
        bool         resort = S->resort;

        // resort: keep every decoded point so the global Morton sort can run after
        // phase 1 (reserve up front → no reallocation, so chunk pointers stay
        // valid). non-resort: a small bounded buffer keeps peak RAM tiny.
        // If the big reserve fails (file larger than RAM), fall back to bounded
        // file-order streaming so the cloud still loads (just without phase 2).
        std::vector<PointCloudPoint> allPoints;
        std::vector<PointCloudPoint> buf;
        try {
            if (resort) allPoints.reserve(static_cast<size_t>(std::max<int64_t>(nPoints, 0)));
            else        buf.reserve(CHUNK);
        } catch (const std::bad_alloc&) {
            std::cerr << "[LAS-Stream] not enough RAM to keep points for resort; "
                         "falling back to file-order streaming for '" << filePath << "'\n";
            resort = false;
            buf.reserve(CHUNK);
        }

        uint32_t outPoint = 0, outBatch = 0;
        size_t   pendingStart = 0; // resort mode: first allPoints index not yet flushed

        auto pushChunk = [&](StreamChunk& ck) -> bool {
            std::unique_lock<std::mutex> lk(S->mtx);
            S->cv.wait(lk, [&] { return S->stop.load() || S->ready.size() < S->maxInFlight; });
            if (S->stop.load()) return false;
            S->ready.push(std::move(ck));
            lk.unlock();
            S->cv.notify_all();
            return true;
        };
        auto flushBounded = [&]() -> bool {           // !resort path
            if (buf.empty()) return true;
            StreamChunk ck;
            buildFileOrderChunk(buf.data(), buf.size(), outPoint, outBatch, ck);
            outPoint += ck.numPoints; outBatch += static_cast<uint32_t>(ck.batches.size());
            buf.clear();
            return pushChunk(ck);
        };
        auto flushAccumulated = [&](bool force) -> bool {  // resort path
            while ((allPoints.size() - pendingStart) >= CHUNK ||
                   (force && pendingStart < allPoints.size())) {
                const size_t n = std::min(CHUNK, allPoints.size() - pendingStart);
                StreamChunk ck;
                buildFileOrderChunk(allPoints.data() + pendingStart, n, outPoint, outBatch, ck);
                outPoint += ck.numPoints; outBatch += static_cast<uint32_t>(ck.batches.size());
                pendingStart += n;
                if (!pushChunk(ck)) return false;
            }
            return true;
        };

        // ── Phase 1: decode + stream file-order chunks ───────────────────────
        try {
            for (int64_t i = 0; i < nPoints; ++i) {
                if (S->stop.load()) break;
                if (laszip_read_point(reader)) break;
                if (downsample > 1 && (static_cast<size_t>(i) % downsample) != 0) continue;

                PointCloudPoint pt;
                pt.position.x = static_cast<float>(lp->X * sx + ox - cx);
                pt.position.y = static_cast<float>(lp->Y * sy + oy - cy);
                pt.position.z = static_cast<float>(lp->Z * sz + oz - cz);
                pt.intensity  = lp->intensity / 65535.0f;
                if (hasColor) {
                    pt.color.r = lp->rgb[0] / colorScale;
                    pt.color.g = lp->rgb[1] / colorScale;
                    pt.color.b = lp->rgb[2] / colorScale;
                } else {
                    pt.color = glm::vec3(pt.intensity);
                }

                if (resort) {
                    allPoints.push_back(pt);
                    if ((allPoints.size() - pendingStart) >= CHUNK) { if (!flushAccumulated(false)) break; }
                } else {
                    buf.push_back(pt);
                    if (buf.size() >= CHUNK) { if (!flushBounded()) break; }
                }
            }
            if (!S->stop.load()) { if (resort) flushAccumulated(true); else flushBounded(); }
        } catch (const std::exception& e) {
            std::cerr << "[LAS-Stream] phase-1 exception for '" << filePath << "': " << e.what() << "\n";
            S->failed.store(true);
        }

        laszip_close_reader(reader);
        laszip_destroy(reader);
        S->phase1Done.store(true);
        S->cv.notify_all();

        // ── Phase 2: one global Morton sort, published for an in-place swap ───
        if (resort && !S->stop.load() && !S->failed.load() && !allPoints.empty()) {
            try {
                computeCloudArrays(allPoints, S->r_xyz4b, S->r_xyz8b, S->r_xyz12b, S->r_rgba, S->r_batches);
                std::vector<PointCloudPoint>().swap(allPoints); // free phase-1 copy
                S->resortReady.store(true);
                std::cout << "[LAS-Stream] phase 2 (Morton resort) ready for '" << filePath << "'\n";
            } catch (const std::exception& e) {
                std::cerr << "[LAS-Stream] phase-2 exception for '" << filePath << "': " << e.what()
                          << " – keeping file-order data\n";
            }
        }

        S->finished.store(true);
        S->cv.notify_all();
    }

    // Fast single-pass scan: count data lines in a text point cloud file.
    // A "data line" starts with a digit, '-', or '+'.
    static size_t countTextLines(const std::string& filePath) {
        std::ifstream f(filePath, std::ios::binary);
        if (!f) return 0;
        constexpr size_t BUF = 32 * 1024 * 1024;
        std::vector<char> buf(BUF);
        size_t count = 0;
        bool startOfLine = true, lineIsData = false;
        while (f.read(buf.data(), static_cast<std::streamsize>(BUF)) || f.gcount() > 0) {
            const std::streamsize n = f.gcount();
            for (std::streamsize i = 0; i < n; i++) {
                const char c = buf[i];
                if (c == '\n') {
                    if (lineIsData) count++;
                    startOfLine = true; lineIsData = false;
                } else if (c == '\r') {
                    // ignore CR in CRLF
                } else if (startOfLine) {
                    startOfLine = false;
                    if ((c>='0'&&c<='9')||c=='-'||c=='+') lineIsData = true;
                }
            }
        }
        if (lineIsData) count++; // file without trailing newline
        return count;
    }

    // =========================================================================
    // End of streaming infrastructure
    // =========================================================================

    PointCloud PointCloudLoader::loadPointCloudFile(const std::string& filePath, size_t downsampleFactor) {
        std::cout << "[DEBUG] PointCloudLoader::loadPointCloudFile() called with file: " << filePath << std::endl;
        std::cout << "[DEBUG] Downsample factor: " << downsampleFactor << std::endl;
        
        std::filesystem::path file_path(filePath);
        std::string extension = file_path.extension().string();
        std::transform(extension.begin(), extension.end(), extension.begin(), ::tolower);
        
        std::cout << "[DEBUG] File extension detected: " << extension << std::endl;

        // Check file extension and delegate to appropriate loader
        if (extension == ".h5" || extension == ".hdf5" || extension == ".f5") {
            std::cout << "[DEBUG] Loading as HDF5 file" << std::endl;
            return loadFromHDF5(filePath, downsampleFactor);
        }
        else if (extension == ".pcb") {
            std::cout << "[DEBUG] Loading as binary file" << std::endl;
            return loadFromBinary(filePath);
        }
        else if (extension == ".las" || extension == ".laz") {
            std::cout << "[DEBUG] Loading as LAS/LAZ file" << std::endl;
            return loadFromLAS(filePath, downsampleFactor);
        }
        else if (extension == ".ply") {
            // PLY comes in ASCII and binary variants. ASCII PLY falls through to
            // the text parser below, which skips the (non-numeric) header lines
            // and reads the "x y z [r g b]" vertex rows. Binary PLY is dispatched
            // to a dedicated header-driven reader (loadFromBinaryPLY).
            std::ifstream header(filePath, std::ios::binary);
            std::string line;
            bool isBinaryPly = false;
            if (header.is_open() && std::getline(header, line)) {
                // First non-empty line of a PLY file is the "ply" magic; the
                // "format" line that follows states ascii vs binary_*.
                while (std::getline(header, line)) {
                    if (line.rfind("format", 0) == 0) {
                        if (line.find("binary") != std::string::npos) isBinaryPly = true;
                        break;
                    }
                }
            }
            header.close();
            if (isBinaryPly) {
                std::cout << "[DEBUG] Loading as binary PLY file" << std::endl;
                return loadFromBinaryPLY(filePath, downsampleFactor);
            }
            // ASCII PLY continues into the default text handling below.
        }

        // ── Default handling: XYZ, PLY, TXT, and other text formats ─────────
        // Streaming load: points are quantised and uploaded to GPU one batch
        // (kComputeBatchSize = 10240 points) at a time.  Peak CPU RAM is a
        // single batch (~280 KB) regardless of file size.
        PointCloud pointCloud;
        pointCloud.name = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale    = glm::vec3(1.0f);

        // ── Pass 1: count data lines to pre-allocate SSBOs exactly ───────────
        std::cout << "[TextPC] Counting lines in: " << filePath << " ..." << std::endl;
        const size_t estimatedPoints = countTextLines(filePath);
        // Apply downsampling factor to the estimate
        const size_t allocPoints = (downsampleFactor > 1)
                                 ? (estimatedPoints / downsampleFactor + 1)
                                 : estimatedPoints;
        std::cout << "[TextPC] Estimated " << estimatedPoints
                  << " data lines (allocating for "
                  << allocPoints << " after downsample x" << downsampleFactor << ")\n";

        if (allocPoints == 0) {
            std::cerr << "[TextPC] No data lines found in " << filePath << "\n";
            return std::move(pointCloud);
        }


        // ── Pass 2: stream-parse and upload ─────────────────────────────────
        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[TextPC] Failed to open: " << filePath << "\n";
            deleteComputeSSBOs(pointCloud);
            return std::move(pointCloud);
        }

        // Large IO buffer – reduces syscall overhead on big files
        constexpr size_t IO_BUF = 64 * 1024 * 1024; // 64 MB
        std::vector<char> ioBuf(IO_BUF);
        file.rdbuf()->pubsetbuf(ioBuf.data(), static_cast<std::streamsize>(IO_BUF));

        // Point accumulator – the whole cloud is collected here, then Morton-
        // sorted, batch-shuffled, quantised and bulk-uploaded by buildComputeCloud.
        std::vector<PointCloudPoint> batchBuf;
        batchBuf.reserve(allocPoints);

        size_t lineCounter = 0;   // global line counter for downsampling

        // Global bounds tracking (updated per-point, negligible cost)
        glm::vec3 gMin( FLT_MAX), gMax(-FLT_MAX);

        // Carry-over buffer for partial lines at IO block boundaries
        std::string carryOver;
        carryOver.reserve(512);

        // No per-batch flush any more: points accumulate in batchBuf and are
        // built in a single pass below.  Kept as a no-op so the existing
        // size-threshold call sites stay valid.
        auto flushBatch = [&]() {};

        constexpr size_t READ_CHUNK = 8 * 1024 * 1024; // 8 MB per read
        std::vector<char> readBuf(READ_CHUNK);

        while (file.read(readBuf.data(), static_cast<std::streamsize>(READ_CHUNK))
               || file.gcount() > 0)
        {
            const std::streamsize bytesRead = file.gcount();
            const char* src   = readBuf.data();
            const char* end   = src + bytesRead;
            const char* lineStart = src;

            while (lineStart < end) {
                // Find end of this line
                const char* nl = static_cast<const char*>(
                    std::memchr(lineStart, '\n', static_cast<size_t>(end - lineStart)));

                if (!nl) {
                    // Partial line at end of read chunk – carry it over
                    carryOver.append(lineStart, end - lineStart);
                    break;
                }

                // Build full line (prefix with carry-over if any)
                const char* lineData;
                size_t       lineLen;
                std::string  fullLine; // only allocated when carry-over is present

                if (!carryOver.empty()) {
                    fullLine  = std::move(carryOver);
                    carryOver.clear();
                    fullLine.append(lineStart, nl - lineStart);
                    lineData = fullLine.c_str();
                    lineLen  = fullLine.size();
                } else {
                    lineData = lineStart;
                    lineLen  = static_cast<size_t>(nl - lineStart);
                }

                lineStart = nl + 1;

                // Skip empty / whitespace-only / comment lines
                if (lineLen == 0) continue;
                const char first = lineData[0];
                if (!((first >= '0' && first <= '9') || first == '-' || first == '+'))
                    continue;

                // Apply downsampling
                const size_t myLine = lineCounter++;
                if (downsampleFactor > 1 && (myLine % downsampleFactor) != 0)
                    continue;

                // ── Parse line ───────────────────────────────────────────
                // Strategy: try from most-specific to least-specific format.
                //
                //  7 fields → XYZIRGB  (x y z intensity r g b)
                //  6 fields → XYZRGB   (x y z r g b, no intensity)
                //  4 fields → XYZI     (x y z intensity)
                //  3 fields → XYZ      (x y z)
                //
                // The naive single-format approach (sscanf "%f %f %f %f %d %d %d")
                // misreads XYZRGB (6 cols) as XYZI+partial-RGB because %f happily
                // consumes an integer (128 → 128.0) as the intensity field,
                // shifting R/G/B one column to the right and dropping B entirely.
                // The two-step below avoids that shift.
                float x, y, z, intensity = 1.0f;
                int   r = 255, g = 255, b = 255;

                const int n7 = sscanf_s(lineData,
                                        "%f %f %f %f %d %d %d",
                                        &x, &y, &z, &intensity, &r, &g, &b);
                if (n7 == 7) {
                    // XYZIRGB – all fields present, nothing to do
                } else {
                    // Re-try with integer 4th field to detect XYZRGB
                    int ri = 255;
                    const int n6 = sscanf_s(lineData,
                                            "%f %f %f %d %d %d",
                                            &x, &y, &z, &ri, &g, &b);
                    if (n6 == 6) {
                        // XYZRGB (no intensity column)
                        r = ri;
                        intensity = 1.0f;
                    } else if (n7 >= 4) {
                        // XYZI – intensity was parsed correctly by first sscanf,
                        // but there were no valid integer colour fields.
                        r = g = b = 255;
                    } else if (n7 == 3 || n6 >= 3) {
                        // XYZ only
                        intensity = 1.0f;
                        r = g = b = 255;
                    } else {
                        continue; // unparseable line
                    }
                }

                PointCloudPoint pt;
                pt.position  = glm::vec3(x, y, z);
                pt.intensity = intensity;
                pt.color     = glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);

                gMin = glm::min(gMin, pt.position);
                gMax = glm::max(gMax, pt.position);
                batchBuf.push_back(pt);

                if (static_cast<int>(batchBuf.size()) == PointCloud::kComputeBatchSize)
                    flushBatch();
            }
        }

        // Handle trailing carry-over without newline (same multi-format logic)
        if (!carryOver.empty()) {
            const char* lineData = carryOver.c_str();
            const char  first    = lineData[0];
            if ((first >= '0' && first <= '9') || first == '-' || first == '+') {
                float x, y, z, intensity = 1.0f;
                int   r = 255, g = 255, b = 255;
                const int n7 = sscanf_s(lineData, "%f %f %f %f %d %d %d",
                                        &x, &y, &z, &intensity, &r, &g, &b);
                bool valid = true;
                if (n7 < 7) {
                    int ri = 255;
                    const int n6 = sscanf_s(lineData, "%f %f %f %d %d %d",
                                            &x, &y, &z, &ri, &g, &b);
                    if (n6 == 6)      { r = ri; intensity = 1.0f; }
                    else if (n7 >= 4) { r = g = b = 255; }
                    else if (n7 == 3 || n6 >= 3) { intensity = 1.0f; r = g = b = 255; }
                    else              { valid = false; }
                }
                if (valid) {
                    PointCloudPoint pt;
                    pt.position  = glm::vec3(x, y, z);
                    pt.intensity = intensity;
                    pt.color     = glm::vec3(r / 255.0f, g / 255.0f, b / 255.0f);
                    gMin = glm::min(gMin, pt.position);
                    gMax = glm::max(gMax, pt.position);
                    batchBuf.push_back(pt);
                }
            }
        }

        file.close();

        // Morton-sort + batch-shuffle + quantise + bulk-upload in one pass.
        buildComputeCloud(pointCloud, batchBuf);

        std::cout << "[TextPC] Loaded " << pointCloud.totalPointCount << " points\n";

        return std::move(pointCloud);
    }

    // =========================================================================
    // Binary PLY support
    // =========================================================================
    // Most point clouds exported by scanners and tools (CloudCompare, MeshLab,
    // PDAL, …) use binary PLY, not ASCII. We parse the ASCII header to learn the
    // vertex record layout, then stream-read the fixed-stride binary records
    // straight into the compute SSBOs, exactly like the text loader.
    namespace {
        enum class PlyType { Invalid, Int8, Uint8, Int16, Uint16, Int32, Uint32, Float32, Float64 };

        int plyTypeSize(PlyType t) {
            switch (t) {
                case PlyType::Int8:  case PlyType::Uint8:                       return 1;
                case PlyType::Int16: case PlyType::Uint16:                      return 2;
                case PlyType::Int32: case PlyType::Uint32: case PlyType::Float32: return 4;
                case PlyType::Float64:                                          return 8;
                default:                                                        return 0;
            }
        }

        PlyType parsePlyType(const std::string& s) {
            if (s == "char"   || s == "int8")    return PlyType::Int8;
            if (s == "uchar"  || s == "uint8")   return PlyType::Uint8;
            if (s == "short"  || s == "int16")   return PlyType::Int16;
            if (s == "ushort" || s == "uint16")  return PlyType::Uint16;
            if (s == "int"    || s == "int32")   return PlyType::Int32;
            if (s == "uint"   || s == "uint32")  return PlyType::Uint32;
            if (s == "float"  || s == "float32") return PlyType::Float32;
            if (s == "double" || s == "float64") return PlyType::Float64;
            return PlyType::Invalid;
        }

        // Read one scalar of type t from p, byte-swapping when the file's
        // endianness differs from the host, and return it as a double.
        double readPlyValue(const char* p, PlyType t, bool swap) {
            const int n = plyTypeSize(t);
            char b[8];
            if (swap) { for (int i = 0; i < n; i++) b[i] = p[n - 1 - i]; p = b; }
            switch (t) {
                case PlyType::Int8:    { int8_t   v; std::memcpy(&v, p, 1); return static_cast<double>(v); }
                case PlyType::Uint8:   { uint8_t  v; std::memcpy(&v, p, 1); return static_cast<double>(v); }
                case PlyType::Int16:   { int16_t  v; std::memcpy(&v, p, 2); return static_cast<double>(v); }
                case PlyType::Uint16:  { uint16_t v; std::memcpy(&v, p, 2); return static_cast<double>(v); }
                case PlyType::Int32:   { int32_t  v; std::memcpy(&v, p, 4); return static_cast<double>(v); }
                case PlyType::Uint32:  { uint32_t v; std::memcpy(&v, p, 4); return static_cast<double>(v); }
                case PlyType::Float32: { float    v; std::memcpy(&v, p, 4); return static_cast<double>(v); }
                case PlyType::Float64: { double   v; std::memcpy(&v, p, 8); return v; }
                default:               return 0.0;
            }
        }

        struct PlyProp    { std::string name; PlyType type = PlyType::Invalid; bool isList = false; PlyType countType = PlyType::Invalid; };
        struct PlyElement { std::string name; size_t count = 0; std::vector<PlyProp> props; };

        // Convert a colour/intensity channel to [0,1] based on its storage type.
        // Integer channels are normalised by their full range; floating-point
        // channels are assumed to already be in [0,1] by PLY convention.
        float plyChannelToUnit(double v, PlyType t) {
            switch (t) {
                case PlyType::Uint8:   return static_cast<float>(v / 255.0);
                case PlyType::Int8:    return static_cast<float>(v / 127.0);
                case PlyType::Uint16:  return static_cast<float>(v / 65535.0);
                case PlyType::Int16:   return static_cast<float>(v / 32767.0);
                case PlyType::Float32:
                case PlyType::Float64: return static_cast<float>(v);
                default:               return static_cast<float>(v / 255.0);
            }
        }
    } // anonymous namespace

    PointCloud PointCloudLoader::loadFromBinaryPLY(const std::string& filePath, size_t downsampleFactor) {
        std::cout << "[PLY] Loading binary PLY: " << filePath << std::endl;
        std::ifstream f(filePath, std::ios::binary);
        if (!f.is_open()) {
            std::cerr << "[PLY] Failed to open: " << filePath << "\n";
            return PointCloud{};
        }

        // ── Parse the ASCII header ──────────────────────────────────────────
        bool sawMagic      = false;
        bool isBinary      = false;
        bool fileBigEndian = false;
        std::vector<PlyElement> elements;
        std::string line;

        while (std::getline(f, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            std::istringstream ss(line);
            std::string tok;
            if (!(ss >> tok)) continue;                       // blank line
            if (tok == "ply")     { sawMagic = true; continue; }
            if (tok == "comment" || tok == "obj_info") continue;
            if (tok == "format") {
                std::string fmt; ss >> fmt;
                if      (fmt == "binary_little_endian") { isBinary = true;  fileBigEndian = false; }
                else if (fmt == "binary_big_endian")    { isBinary = true;  fileBigEndian = true;  }
                else                                    { isBinary = false; } // ascii
                continue;
            }
            if (tok == "element") {
                PlyElement e;
                ss >> e.name >> e.count;
                elements.push_back(std::move(e));
                continue;
            }
            if (tok == "property") {
                if (elements.empty()) continue;               // malformed; ignore
                std::string t1; ss >> t1;
                PlyProp p;
                if (t1 == "list") {
                    std::string ctype, vtype, name;
                    ss >> ctype >> vtype >> name;
                    p.isList    = true;
                    p.countType = parsePlyType(ctype);
                    p.type      = parsePlyType(vtype);
                    p.name      = name;
                } else {
                    std::string name; ss >> name;
                    p.type = parsePlyType(t1);
                    p.name = name;
                }
                elements.back().props.push_back(std::move(p));
                continue;
            }
            if (tok == "end_header") break;
        }

        if (!sawMagic) { std::cerr << "[PLY] Not a PLY file: " << filePath << "\n"; return PointCloud{}; }
        if (!isBinary) { std::cerr << "[PLY] loadFromBinaryPLY called on non-binary PLY: " << filePath << "\n"; return PointCloud{}; }

        const std::streampos dataStart = f.tellg();
        if (dataStart == std::streampos(-1)) {
            std::cerr << "[PLY] Could not locate binary data start in " << filePath << "\n";
            return PointCloud{};
        }

        // ── Locate the vertex element and its layout ────────────────────────
        int vertexIdx = -1;
        for (size_t i = 0; i < elements.size(); i++)
            if (elements[i].name == "vertex") { vertexIdx = static_cast<int>(i); break; }
        if (vertexIdx < 0) { std::cerr << "[PLY] No 'vertex' element in " << filePath << "\n"; return PointCloud{}; }

        // Skip any fixed-size elements that precede vertex (rare). A list
        // property before vertex makes the offset un-computable -> bail out.
        std::streamoff skipBytes = 0;
        for (int i = 0; i < vertexIdx; i++) {
            size_t es = 0;
            for (const auto& p : elements[i].props) {
                if (p.isList) {
                    std::cerr << "[PLY] Unsupported: list property in element '" << elements[i].name
                              << "' before vertex in " << filePath << "\n";
                    return PointCloud{};
                }
                es += plyTypeSize(p.type);
            }
            skipBytes += static_cast<std::streamoff>(elements[i].count) * static_cast<std::streamoff>(es);
        }

        const PlyElement& V = elements[vertexIdx];
        int     offX=-1, offY=-1, offZ=-1, offR=-1, offG=-1, offB=-1, offI=-1;
        PlyType tX=PlyType::Invalid, tY=PlyType::Invalid, tZ=PlyType::Invalid;
        PlyType tR=PlyType::Invalid, tG=PlyType::Invalid, tB=PlyType::Invalid, tI=PlyType::Invalid;
        size_t  cur = 0;
        for (const auto& p : V.props) {
            if (p.isList) {
                std::cerr << "[PLY] Unsupported: list property in vertex element of " << filePath << "\n";
                return PointCloud{};
            }
            const int sz = plyTypeSize(p.type);
            if (sz == 0) { std::cerr << "[PLY] Unknown property type for '" << p.name << "' in " << filePath << "\n"; return PointCloud{}; }
            std::string n = p.name;
            std::transform(n.begin(), n.end(), n.begin(), ::tolower);
            if      (n == "x") { offX = static_cast<int>(cur); tX = p.type; }
            else if (n == "y") { offY = static_cast<int>(cur); tY = p.type; }
            else if (n == "z") { offZ = static_cast<int>(cur); tZ = p.type; }
            else if (n == "red"   || n == "r" || n == "diffuse_red")   { offR = static_cast<int>(cur); tR = p.type; }
            else if (n == "green" || n == "g" || n == "diffuse_green") { offG = static_cast<int>(cur); tG = p.type; }
            else if (n == "blue"  || n == "b" || n == "diffuse_blue")  { offB = static_cast<int>(cur); tB = p.type; }
            else if (n == "intensity" || n == "scalar_intensity")      { offI = static_cast<int>(cur); tI = p.type; }
            cur += static_cast<size_t>(sz);
        }
        const size_t stride = cur;
        if (offX < 0 || offY < 0 || offZ < 0 || stride == 0) {
            std::cerr << "[PLY] vertex element missing x/y/z in " << filePath << "\n";
            return PointCloud{};
        }
        const bool hasColor     = (offR >= 0 && offG >= 0 && offB >= 0);
        const bool hasIntensity = (offI >= 0);

        // Detect host endianness at runtime; byte-swap only when it differs.
        const uint16_t endianProbe = 1;
        const bool hostBig = (*reinterpret_cast<const char*>(&endianProbe) == 0);
        const bool swap    = (fileBigEndian != hostBig);

        // ── Set up the cloud and pre-allocate SSBOs ─────────────────────────
        PointCloud pointCloud;
        pointCloud.name     = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale    = glm::vec3(1.0f);

        const size_t vertexCount = V.count;
        if (vertexCount == 0) {
            std::cerr << "[PLY] vertex element has 0 points in " << filePath << "\n";
            return std::move(pointCloud);
        }
        const size_t allocPoints = (downsampleFactor > 1)
                                  ? (vertexCount / downsampleFactor + 1)
                                  : vertexCount;

        f.clear();
        f.seekg(dataStart + skipBytes);
        if (!f) {
            std::cerr << "[PLY] Failed to seek to vertex data in " << filePath << "\n";
            deleteComputeSSBOs(pointCloud);
            return std::move(pointCloud);
        }

        // ── Read fixed-stride vertex records into one point accumulator ─────
        std::vector<PointCloudPoint> batchBuf;
        batchBuf.reserve(allocPoints);
        glm::vec3 gMin( FLT_MAX), gMax(-FLT_MAX);

        // No per-batch flush: points accumulate in batchBuf and are built below.
        auto flushBatch = [&]() {};

        // Read whole records only: buffer holds an integral number of records
        // plus one record of headroom for any partial record carried over a
        // read boundary (truncated files).
        const size_t recordsPerRead = 65536;
        const size_t bufBytes       = recordsPerRead * stride;
        std::vector<char> buf(bufBytes + stride);
        size_t carryLen    = 0;
        size_t recordsRead = 0;

        while (recordsRead < vertexCount) {
            f.read(buf.data() + carryLen, static_cast<std::streamsize>(bufBytes));
            const std::streamsize got = f.gcount();
            const size_t avail = carryLen + static_cast<size_t>(got);
            size_t nRec = avail / stride;
            if (recordsRead + nRec > vertexCount) nRec = vertexCount - recordsRead; // ignore trailing (e.g. face) data

            for (size_t i = 0; i < nRec; i++) {
                const size_t globalIdx = recordsRead + i;
                if (downsampleFactor > 1 && (globalIdx % downsampleFactor) != 0) continue;

                const char* rec = buf.data() + i * stride;
                PointCloudPoint pt;
                pt.position = glm::vec3(
                    static_cast<float>(readPlyValue(rec + offX, tX, swap)),
                    static_cast<float>(readPlyValue(rec + offY, tY, swap)),
                    static_cast<float>(readPlyValue(rec + offZ, tZ, swap)));
                pt.color = hasColor
                    ? glm::vec3(plyChannelToUnit(readPlyValue(rec + offR, tR, swap), tR),
                                plyChannelToUnit(readPlyValue(rec + offG, tG, swap), tG),
                                plyChannelToUnit(readPlyValue(rec + offB, tB, swap), tB))
                    : glm::vec3(1.0f);
                pt.intensity = hasIntensity
                    ? plyChannelToUnit(readPlyValue(rec + offI, tI, swap), tI)
                    : 1.0f;

                gMin = glm::min(gMin, pt.position);
                gMax = glm::max(gMax, pt.position);
                batchBuf.push_back(pt);
                if (static_cast<int>(batchBuf.size()) == PointCloud::kComputeBatchSize)
                    flushBatch();
            }

            recordsRead += nRec;
            const size_t consumed = nRec * stride;
            carryLen = avail - consumed;
            if (carryLen > 0 && recordsRead < vertexCount)
                std::memmove(buf.data(), buf.data() + consumed, carryLen);
            if (got == 0) break; // EOF / truncated file
        }

        f.close();

        // Morton-sort + batch-shuffle + quantise + bulk-upload in one pass.
        buildComputeCloud(pointCloud, batchBuf);

        std::cout << "[PLY] Loaded " << pointCloud.totalPointCount << " of " << vertexCount
                  << " binary-PLY points"
                  << (hasColor ? " (with colour)" : "")
                  << (hasIntensity ? " (with intensity)" : "") << "\n";

        return std::move(pointCloud);
    }

    // Build the local→world matrix from a cloud's transform fields (identity
    // when applyTransform is false, i.e. when exporting local-space data).
    static glm::mat4 pointCloudTransform(const PointCloud& pointCloud, bool applyTransform) {
        if (!applyTransform) return glm::mat4(1.0f);
        glm::mat4 transform = glm::mat4(1.0f);
        transform = glm::translate(transform, pointCloud.position);
        transform = glm::rotate(transform, glm::radians(pointCloud.rotation.x), glm::vec3(1, 0, 0));
        transform = glm::rotate(transform, glm::radians(pointCloud.rotation.y), glm::vec3(0, 1, 0));
        transform = glm::rotate(transform, glm::radians(pointCloud.rotation.z), glm::vec3(0, 0, 1));
        transform = glm::scale(transform, pointCloud.scale);
        return transform;
    }

    bool PointCloudLoader::forEachPointBatch(const PointCloud& pointCloud,
        const std::function<void(const PointCloudPoint*, size_t)>& callback)
    {
        // Legacy path: CPU vector still populated (loader hasn't streamed yet).
        if (!pointCloud.points.empty()) {
            const size_t batchSz = PointCloud::kComputeBatchSize;
            for (size_t first = 0; first < pointCloud.points.size(); first += batchSz) {
                const size_t count = std::min(batchSz, pointCloud.points.size() - first);
                callback(pointCloud.points.data() + first, count);
            }
            return true;
        }

        if (pointCloud.numBatches == 0 || pointCloud.totalPointCount == 0 ||
            pointCloud.computeBatchSSBO == 0 || pointCloud.computeXyz4bSSBO == 0 ||
            pointCloud.computeXyz8bSSBO == 0 || pointCloud.computeXyz12bSSBO == 0 ||
            pointCloud.computeRGBASSBO == 0) {
            std::cerr << "[PCExport] Point cloud '" << pointCloud.name
                      << "' has no point data to read back\n";
            return false;
        }

        // Read the batch descriptors once (32 bytes per batch).
        std::vector<ComputeBatch> batches(pointCloud.numBatches);
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, pointCloud.computeBatchSSBO);
        glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, 0,
                           static_cast<GLsizeiptr>(batches.size() * sizeof(ComputeBatch)),
                           batches.data());

        constexpr double   STEPS_30BIT = 1073741824.0; // 2^30
        constexpr uint32_t MASK_10BIT  = 1023u;

        std::vector<uint32_t> xyz4b, xyz8b, xyz12b, rgba;
        std::vector<PointCloudPoint> decoded;

        for (const ComputeBatch& batch : batches) {
            if (batch.numPoints <= 0) continue;
            const size_t count = static_cast<size_t>(batch.numPoints);

            xyz4b.resize(count); xyz8b.resize(count); xyz12b.resize(count); rgba.resize(count);
            const GLintptr   off = static_cast<GLintptr>(batch.firstPoint) * sizeof(uint32_t);
            const GLsizeiptr byt = static_cast<GLsizeiptr>(count) * sizeof(uint32_t);

            glBindBuffer(GL_SHADER_STORAGE_BUFFER, pointCloud.computeXyz4bSSBO);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, off, byt, xyz4b.data());
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, pointCloud.computeXyz8bSSBO);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, off, byt, xyz8b.data());
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, pointCloud.computeXyz12bSSBO);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, off, byt, xyz12b.data());
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, pointCloud.computeRGBASSBO);
            glGetBufferSubData(GL_SHADER_STORAGE_BUFFER, off, byt, rgba.data());

            // Decode the 30-bit quantised coordinates back to floats using the
            // batch bounds (inverse of buildComputeCloud's quantiser). +0.5 centres
            // the value inside its quantisation step to minimise round-trip error.
            const glm::dvec3 bMin(batch.min_x, batch.min_y, batch.min_z);
            const glm::dvec3 bSize(
                std::max(static_cast<double>(batch.max_x) - bMin.x, 1e-6),
                std::max(static_cast<double>(batch.max_y) - bMin.y, 1e-6),
                std::max(static_cast<double>(batch.max_z) - bMin.z, 1e-6));

            decoded.resize(count);
            for (size_t i = 0; i < count; i++) {
                const uint32_t X = ((xyz4b[i]      ) & MASK_10BIT) << 20 |
                                   ((xyz8b[i]      ) & MASK_10BIT) << 10 |
                                   ((xyz12b[i]     ) & MASK_10BIT);
                const uint32_t Y = ((xyz4b[i] >> 10) & MASK_10BIT) << 20 |
                                   ((xyz8b[i] >> 10) & MASK_10BIT) << 10 |
                                   ((xyz12b[i]>> 10) & MASK_10BIT);
                const uint32_t Z = ((xyz4b[i] >> 20) & MASK_10BIT) << 20 |
                                   ((xyz8b[i] >> 20) & MASK_10BIT) << 10 |
                                   ((xyz12b[i]>> 20) & MASK_10BIT);

                PointCloudPoint& pt = decoded[i];
                pt.position = glm::vec3(
                    static_cast<float>(bMin.x + (X + 0.5) / STEPS_30BIT * bSize.x),
                    static_cast<float>(bMin.y + (Y + 0.5) / STEPS_30BIT * bSize.y),
                    static_cast<float>(bMin.z + (Z + 0.5) / STEPS_30BIT * bSize.z));
                pt.color = glm::vec3(( rgba[i]        & 0xFFu) / 255.0f,
                                     ((rgba[i] >>  8) & 0xFFu) / 255.0f,
                                     ((rgba[i] >> 16) & 0xFFu) / 255.0f);
                pt.intensity = ((rgba[i] >> 24) & 0xFFu) / 255.0f;
            }

            callback(decoded.data(), count);
        }

        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
        return true;
    }

    bool PointCloudLoader::exportToXYZ(const PointCloud& pointCloud, const std::string& filePath,
                                       bool applyTransform) {
        std::ofstream file(filePath);
        if (!file.is_open()) {
            std::cerr << "Failed to open file for writing: " << filePath << std::endl;
            return false;
        }

        const glm::mat4 transform = pointCloudTransform(pointCloud, applyTransform);

        size_t written = 0;
        const bool ok = forEachPointBatch(pointCloud,
            [&](const PointCloudPoint* pts, size_t count) {
                for (size_t i = 0; i < count; i++) {
                    const glm::vec3 pos = applyTransform
                        ? glm::vec3(transform * glm::vec4(pts[i].position, 1.0f))
                        : pts[i].position;

                    // x y z intensity r g b — intensity written as a float so
                    // the text loader round-trips it unchanged.
                    file << std::fixed << std::setprecision(4)
                        << pos.x << " " << pos.y << " " << pos.z << " "
                        << pts[i].intensity << " "
                        << static_cast<int>(glm::clamp(pts[i].color.r, 0.0f, 1.0f) * 255.0f + 0.5f) << " "
                        << static_cast<int>(glm::clamp(pts[i].color.g, 0.0f, 1.0f) * 255.0f + 0.5f) << " "
                        << static_cast<int>(glm::clamp(pts[i].color.b, 0.0f, 1.0f) * 255.0f + 0.5f) << "\n";
                }
                written += count;
            });

        file.close();
        if (!ok || written == 0) {
            std::cerr << "[PCExport] XYZ export failed - no point data written to "
                      << filePath << std::endl;
            std::error_code ec;
            std::filesystem::remove(filePath, ec); // don't leave an empty stub behind
            return false;
        }
        std::cout << "[PCExport] Wrote " << written << " points to " << filePath << std::endl;
        return true;
    }

    bool PointCloudLoader::exportToBinary(const PointCloud& pointCloud, const std::string& filePath,
                                          bool applyTransform) {
        std::ofstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "Failed to open file for writing: " << filePath << std::endl;
            return false;
        }

        const glm::mat4 transform = pointCloudTransform(pointCloud, applyTransform);

        // Write header. The point count is patched after streaming in case a
        // batch read fails partway through.
        file.write(BINARY_MAGIC_NUMBER, 4);
        uint32_t numPoints = 0;
        file.write(reinterpret_cast<const char*>(&numPoints), sizeof(numPoints));

        // Per-point record: vec3 position | uint32 intensity*1000 | u8vec3 color
        constexpr size_t PT_SIZE = sizeof(glm::vec3) + sizeof(uint32_t) + sizeof(glm::u8vec3);
        std::vector<char> recordBuf;

        size_t written = 0;
        const bool ok = forEachPointBatch(pointCloud,
            [&](const PointCloudPoint* pts, size_t count) {
                recordBuf.resize(count * PT_SIZE);
                char* dst = recordBuf.data();
                for (size_t i = 0; i < count; i++) {
                    const glm::vec3 pos = applyTransform
                        ? glm::vec3(transform * glm::vec4(pts[i].position, 1.0f))
                        : pts[i].position;
                    std::memcpy(dst, &pos, sizeof(pos));
                    dst += sizeof(pos);

                    const uint32_t intensity = static_cast<uint32_t>(
                        glm::max(pts[i].intensity, 0.0f) * 1000.0f);
                    std::memcpy(dst, &intensity, sizeof(intensity));
                    dst += sizeof(intensity);

                    const glm::u8vec3 color(glm::clamp(pts[i].color, 0.0f, 1.0f) * 255.0f);
                    std::memcpy(dst, &color, sizeof(color));
                    dst += sizeof(color);
                }
                file.write(recordBuf.data(), static_cast<std::streamsize>(recordBuf.size()));
                written += count;
            });

        if (!ok || written == 0) {
            file.close();
            std::cerr << "[PCExport] Binary export failed - no point data written to "
                      << filePath << std::endl;
            std::error_code ec;
            std::filesystem::remove(filePath, ec); // don't leave an empty stub behind
            return false;
        }

        // Patch the header with the actual point count.
        numPoints = static_cast<uint32_t>(written);
        file.seekp(4, std::ios::beg);
        file.write(reinterpret_cast<const char*>(&numPoints), sizeof(numPoints));
        file.close();

        std::cout << "[PCExport] Wrote " << written << " points to " << filePath << std::endl;
        return true;
    }

    struct IVec3Comparator{
    bool operator()(const glm::ivec3 & lhs, const glm::ivec3 & rhs) const {
        if (lhs.x != rhs.x) return lhs.x < rhs.x;
        if (lhs.y != rhs.y) return lhs.y < rhs.y;
        return lhs.z < rhs.z;
    }
    };

    PointCloud PointCloudLoader::loadFromBinary(const std::string& filePath) {
        PointCloud pointCloud;
        pointCloud.name     = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale    = glm::vec3(1.0f);

        std::ifstream file(filePath, std::ios::binary);
        if (!file.is_open()) {
            std::cerr << "[PCB] Failed to open: " << filePath << "\n";
            return std::move(pointCloud);
        }

        try {
            // ── Header ───────────────────────────────────────────────────────
            char magic[4];
            file.read(magic, 4);
            if (std::memcmp(magic, BINARY_MAGIC_NUMBER, 4) != 0)
                throw std::runtime_error("Invalid binary point cloud file format (bad magic)");

            uint32_t numPoints = 0;
            file.read(reinterpret_cast<char*>(&numPoints), sizeof(numPoints));
            std::cout << "[PCB] " << numPoints << " points in " << filePath << "\n";


            // ── Stream-read in batches ────────────────────────────────────────
            // The binary format stores each point as: vec3 | uint32_t | u8vec3
            constexpr size_t PT_SIZE = sizeof(glm::vec3) + sizeof(uint32_t) + sizeof(glm::u8vec3);

            std::vector<PointCloudPoint> points;
            points.reserve(numPoints);

            // Raw read buffer: holds one chunk of kComputeBatchSize raw points
            const size_t rawChunkBytes = static_cast<size_t>(PointCloud::kComputeBatchSize) * PT_SIZE;
            std::vector<char> rawBuf(rawChunkBytes);

            size_t remaining = numPoints;

            while (remaining > 0) {
                const size_t toRead      = std::min(remaining,
                                                    static_cast<size_t>(PointCloud::kComputeBatchSize));
                const size_t bytesToRead = toRead * PT_SIZE;

                file.read(rawBuf.data(), static_cast<std::streamsize>(bytesToRead));
                const size_t actualBytes = static_cast<size_t>(file.gcount());
                const size_t actualPts   = actualBytes / PT_SIZE;

                if (actualPts == 0) break;

                const char* src = rawBuf.data();
                for (size_t i = 0; i < actualPts; i++) {
                    PointCloudPoint pt;
                    std::memcpy(&pt.position, src, sizeof(pt.position));
                    src += sizeof(pt.position);

                    uint32_t rawIntensity;
                    std::memcpy(&rawIntensity, src, sizeof(rawIntensity));
                    pt.intensity = rawIntensity / 1000.0f;
                    src += sizeof(rawIntensity);

                    glm::u8vec3 c;
                    std::memcpy(&c, src, sizeof(c));
                    pt.color = glm::vec3(c) / 255.0f;
                    src += sizeof(c);

                    points.push_back(pt);
                }

                remaining -= actualPts;
            }

            file.close();

            buildComputeCloud(pointCloud, points);

            std::cout << "[PCB] Loaded " << pointCloud.totalPointCount << " points\n";
        }
        catch (const std::exception& e) {
            std::cerr << "[PCB] Error: " << e.what() << "\n";
            deleteComputeSSBOs(pointCloud);
        }

        return std::move(pointCloud);
    }


    void PointCloudLoader::setupPointCloudGLBuffers(PointCloud& pointCloud) {
        // The compute rasterizer path does not use a VAO/VBO; the SSBOs are
        // built by buildComputeCloud.  This function is kept for the rare case
        // where a loader has already populated pointCloud.points (e.g. the f5
        // separate-arrays path).  For loaders that build directly, it is a no-op.
        if (!pointCloud.points.empty() && pointCloud.numBatches == 0) {
            const size_t N = pointCloud.points.size();

            // Morton-sort + batch-shuffle + quantise + bulk-upload (consumes the
            // CPU-side points vector, releasing it on success).
            buildComputeCloud(pointCloud, pointCloud.points);

            std::cout << "[ComputePC] setupPointCloudGLBuffers: built "
                      << pointCloud.numBatches << " batches (" << N << " points)\n";
        }
    }

    PointCloud PointCloudLoader::loadFromHDF5(const std::string& filePath, size_t downsampleFactor) {
        PointCloud pointCloud;
        pointCloud.name = "PointCloud_" + std::filesystem::path(filePath).filename().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(0.0f);
        pointCloud.scale = glm::vec3(1.0f);
        
        // Declare coordinate storage variables at function scope
        std::vector<float> xCoords, yCoords, zCoords;
        std::vector<float> rColors, gColors, bColors;
        std::vector<float> intensities;

        try {
            std::cout << "Loading HDF5 point cloud from: " << filePath << std::endl;
            
            // Open the HDF5 file
            H5File file(filePath, H5F_ACC_RDONLY);
            
            // Define the HDF5 compound type for PointCloudPoint
            CompType pointType(sizeof(PointCloudPoint));
            pointType.insertMember("position_x", HOFFSET(PointCloudPoint, position.x), PredType::NATIVE_FLOAT);
            pointType.insertMember("position_y", HOFFSET(PointCloudPoint, position.y), PredType::NATIVE_FLOAT);
            pointType.insertMember("position_z", HOFFSET(PointCloudPoint, position.z), PredType::NATIVE_FLOAT);
            pointType.insertMember("intensity", HOFFSET(PointCloudPoint, intensity), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_r", HOFFSET(PointCloudPoint, color.r), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_g", HOFFSET(PointCloudPoint, color.g), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_b", HOFFSET(PointCloudPoint, color.b), PredType::NATIVE_FLOAT);

            // Try to open the main dataset (common names)
            DataSet dataset;
            std::vector<std::string> datasetNames = {"points", "point_cloud", "data", "vertices"};
            std::string timeSeriesDataset; // Declare at broader scope
            
            bool datasetFound = false;
            for (const auto& name : datasetNames) {
                try {
                    dataset = file.openDataSet(name);
                    datasetFound = true;
                    std::cout << "Found dataset: " << name << std::endl;
                    break;
                } catch (const H5::Exception&) {
                    continue;
                }
            }

            if (!datasetFound) {
                // List available datasets
                std::cout << "Available datasets in file:" << std::endl;
                hsize_t numObjs = file.getNumObjs();
                for (hsize_t i = 0; i < numObjs; i++) {
                    std::string objName = file.getObjnameByIdx(i);
                    std::cout << "  - " << objName << std::endl;
                }
                
                // Look for time-series data (t=timestamp format)
                for (hsize_t i = 0; i < numObjs; i++) {
                    std::string objName = file.getObjnameByIdx(i);
                    if (objName.substr(0, 2) == "t=") {
                        timeSeriesDataset = objName;
                        break;
                    }
                }
                
                if (!timeSeriesDataset.empty()) {
                    std::cout << "Found time-series dataset: " << timeSeriesDataset << std::endl;
                    try {
                        // Try to open the time-series dataset as a group first
                        Group timeGroup = file.openGroup(timeSeriesDataset);
                        
                        // Look for point cloud data within the time group
                        hsize_t numGroupObjs = timeGroup.getNumObjs();
                        std::cout << "Objects in " << timeSeriesDataset << ":" << std::endl;
                        
                        bool foundPointData = false;
                        for (hsize_t j = 0; j < numGroupObjs; j++) {
                            std::string groupObjName = timeGroup.getObjnameByIdx(j);
                            std::cout << "  - " << groupObjName << std::endl;
                            
                            // Look for common point cloud data names
                            if (groupObjName == "vertices" || groupObjName == "points" || 
                                groupObjName == "coordinates" || groupObjName == "positions" ||
                                groupObjName == "Mesh" || groupObjName == "mesh") {
                                try {
                                    dataset = timeGroup.openDataSet(groupObjName);
                                    datasetFound = true;
                                    foundPointData = true;
                                    std::cout << "Using dataset: " << timeSeriesDataset << "/" << groupObjName << std::endl;
                                    break;
                                } catch (const H5::Exception&) {
                                    continue;
                                }
                            }
                        }
                        
                        if (!foundPointData && numGroupObjs > 0) {
                            // Try the first object in the time group
                            std::string firstGroupObj = timeGroup.getObjnameByIdx(0);
                            try {
                                dataset = timeGroup.openDataSet(firstGroupObj);
                                datasetFound = true;
                                std::cout << "Using first object in time group: " << timeSeriesDataset << "/" << firstGroupObj << std::endl;
                            } catch (const H5::Exception&) {
                                std::cout << "First object is not a dataset, trying as nested group" << std::endl;
                                
                                // Try to open as a nested group
                                try {
                                    Group nestedGroup = timeGroup.openGroup(firstGroupObj);
                                    hsize_t numNestedObjs = nestedGroup.getNumObjs();
                                    std::cout << "Objects in " << timeSeriesDataset << "/" << firstGroupObj << ":" << std::endl;
                                    
                                    // Look for datasets in the nested group
                                    for (hsize_t k = 0; k < numNestedObjs; k++) {
                                        std::string nestedObjName = nestedGroup.getObjnameByIdx(k);
                                        std::cout << "  - " << nestedObjName << std::endl;
                                        
                                        // Look for common mesh/point cloud names
                                        if (nestedObjName == "vertices" || nestedObjName == "points" || 
                                            nestedObjName == "coordinates" || nestedObjName == "positions" ||
                                            nestedObjName == "Mesh" || nestedObjName == "mesh" ||
                                            nestedObjName == "geometry" || nestedObjName == "cells" ||
                                            nestedObjName == "topology" || nestedObjName == "Points" ||
                                            nestedObjName == "VerticesSet") {
                                            try {
                                                dataset = nestedGroup.openDataSet(nestedObjName);
                                                datasetFound = true;
                                                foundPointData = true;
                                                std::cout << "Using nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << std::endl;
                                                break;
                                            } catch (const H5::Exception&) {
                                                // Try opening as another nested group
                                                try {
                                                    Group deepNestedGroup = nestedGroup.openGroup(nestedObjName);
                                                    hsize_t numDeepObjs = deepNestedGroup.getNumObjs();
                                                    std::cout << "Objects in " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << ":" << std::endl;
                                                    
                                                    for (hsize_t l = 0; l < numDeepObjs; l++) {
                                                        std::string deepObjName = deepNestedGroup.getObjnameByIdx(l);
                                                        std::cout << "    - " << deepObjName << std::endl;
                                                        
                                                        try {
                                                            dataset = deepNestedGroup.openDataSet(deepObjName);
                                                            datasetFound = true;
                                                            foundPointData = true;
                                                            std::cout << "Using deep nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << std::endl;
                                                            break;
                                                        } catch (const H5::Exception&) {
                                                            // Try one more level - maybe it's a group too
                                                            try {
                                                                Group veryDeepGroup = deepNestedGroup.openGroup(deepObjName);
                                                                hsize_t numVeryDeepObjs = veryDeepGroup.getNumObjs();
                                                                std::cout << "Objects in " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << ":" << std::endl;
                                                                
                                                                for (hsize_t m = 0; m < numVeryDeepObjs; m++) {
                                                                    std::string veryDeepObjName = veryDeepGroup.getObjnameByIdx(m);
                                                                    std::cout << "      - " << veryDeepObjName << std::endl;
                                                                    
                                                                    try {
                                                                        dataset = veryDeepGroup.openDataSet(veryDeepObjName);
                                                                        datasetFound = true;
                                                                        foundPointData = true;
                                                                        std::cout << "Using very deep nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << "/" << veryDeepObjName << std::endl;
                                                                        break;
                                                                    } catch (const H5::Exception&) {
                                                                        // Try one more level - 5th level deep
                                                                        try {
                                                                            Group ultraDeepGroup = veryDeepGroup.openGroup(veryDeepObjName);
                                                                            hsize_t numUltraDeepObjs = ultraDeepGroup.getNumObjs();
                                                                            std::cout << "Objects in " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << "/" << veryDeepObjName << ":" << std::endl;
                                                                            
                                                                            for (hsize_t n = 0; n < numUltraDeepObjs; n++) {
                                                                                std::string ultraDeepObjName = ultraDeepGroup.getObjnameByIdx(n);
                                                                                std::cout << "        - " << ultraDeepObjName << std::endl;
                                                                                
                                                                                try {
                                                                                    dataset = ultraDeepGroup.openDataSet(ultraDeepObjName);
                                                                                    datasetFound = true;
                                                                                    foundPointData = true;
                                                                                    std::cout << "Using ultra deep nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << nestedObjName << "/" << deepObjName << "/" << veryDeepObjName << "/" << ultraDeepObjName << std::endl;
                                                                                    break;
                                                                                } catch (const H5::Exception&) {
                                                                                    continue;
                                                                                }
                                                                            }
                                                                            if (foundPointData) break;
                                                                        } catch (const H5::Exception&) {
                                                                            continue;
                                                                        }
                                                                    }
                                                                }
                                                                if (foundPointData) break;
                                                            } catch (const H5::Exception&) {
                                                                continue;
                                                            }
                                                        }
                                                    }
                                                    if (foundPointData) break;
                                                } catch (const H5::Exception&) {
                                                    continue;
                                                }
                                            }
                                        }
                                    }
                                    
                                    // If no specific names found, try the first object in nested group
                                    if (!foundPointData && numNestedObjs > 0) {
                                        std::string firstNestedObj = nestedGroup.getObjnameByIdx(0);
                                        try {
                                            dataset = nestedGroup.openDataSet(firstNestedObj);
                                            datasetFound = true;
                                            std::cout << "Using first nested dataset: " << timeSeriesDataset << "/" << firstGroupObj << "/" << firstNestedObj << std::endl;
                                        } catch (const H5::Exception&) {
                                            std::cout << "First nested object is also not a dataset" << std::endl;
                                        }
                                    }
                                } catch (const H5::Exception&) {
                                    std::cout << "Could not open first object as nested group either" << std::endl;
                                }
                            }
                        }
                    } catch (const H5::Exception&) {
                        std::cout << "Time-series object is not a group, trying as dataset" << std::endl;
                        try {
                            dataset = file.openDataSet(timeSeriesDataset);
                            datasetFound = true;
                            std::cout << "Using time-series dataset: " << timeSeriesDataset << std::endl;
                        } catch (const H5::Exception&) {
                            std::cout << "Could not open time-series as dataset either" << std::endl;
                        }
                    }
                }
                
                // Special handler for f5 files with known structure: Selection/Points/StandardCartesianChart3D/
                if (!datasetFound && !timeSeriesDataset.empty() && filePath.substr(filePath.find_last_of(".") + 1) == "f5") {
                    std::cout << "Attempting f5-specific structure navigation..." << std::endl;
                    try {
                        // Navigate the known f5 structure: t=timestamp/Selection/Points/StandardCartesianChart3D/
                        Group timeGroup = file.openGroup(timeSeriesDataset);
                        Group selectionGroup = timeGroup.openGroup("Selection");
                        Group pointsGroup = selectionGroup.openGroup("Points");
                        Group chartGroup = pointsGroup.openGroup("StandardCartesianChart3D");
                        
                        std::cout << "Successfully navigated to StandardCartesianChart3D group" << std::endl;
                        
                        // Comprehensive exploration of the StandardCartesianChart3D structure
                        hsize_t numChartObjs = chartGroup.getNumObjs();
                        std::cout << "\n=== F5 FILE STRUCTURE ANALYSIS ===" << std::endl;
                        std::cout << "Found " << numChartObjs << " objects in StandardCartesianChart3D:" << std::endl;
                        
                        // Collect all available objects and analyze their structure
                        std::vector<std::string> allObjects;
                        for (hsize_t i = 0; i < numChartObjs; i++) {
                            std::string objName = chartGroup.getObjnameByIdx(i);
                            allObjects.push_back(objName);
                            std::cout << "\n[" << (i+1) << "] Object: " << objName << std::endl;
                            
                            // Try to explore this object as a group
                            try {
                                Group subGroup = chartGroup.openGroup(objName);
                                hsize_t numSubObjs = subGroup.getNumObjs();
                                std::cout << "    Type: Group (" << numSubObjs << " sub-objects)" << std::endl;
                                
                                // List sub-objects and check if they contain datasets
                                for (hsize_t j = 0; j < numSubObjs; j++) {
                                    std::string subObjName = subGroup.getObjnameByIdx(j);
                                    std::cout << "    ├─ " << subObjName;
                                    
                                    // Check if this sub-object is a dataset
                                    try {
                                        DataSet subDataset = subGroup.openDataSet(subObjName);
                                        DataSpace subSpace = subDataset.getSpace();
                                        int subRank = subSpace.getSimpleExtentNdims();
                                        hsize_t subDims[3];
                                        subSpace.getSimpleExtentDims(subDims, NULL);
                                        
                                        std::cout << " (Dataset: ";
                                        for (int k = 0; k < subRank; k++) {
                                            std::cout << subDims[k];
                                            if (k < subRank - 1) std::cout << "×";
                                        }
                                        std::cout << ")" << std::endl;
                                        
                                        // Check data type and compression
                                        DataType subType = subDataset.getDataType();
                                        H5T_class_t subTypeClass = subType.getClass();
                                        std::cout << "      Data type: ";
                                        switch(subTypeClass) {
                                            case H5T_FLOAT: std::cout << "Float"; break;
                                            case H5T_INTEGER: std::cout << "Integer"; break;
                                            case H5T_COMPOUND: std::cout << "Compound"; break;
                                            default: std::cout << "Other"; break;
                                        }
                                        
                                        // Check if dataset has filters (compression)
                                        try {
                                            DSetCreatPropList plist = subDataset.getCreatePlist();
                                            H5D_layout_t layout = plist.getLayout();
                                            if (layout == H5D_CHUNKED) {
                                                std::cout << " (Chunked - possibly compressed)";
                                            }
                                        } catch (const H5::Exception&) {
                                            // Can't determine layout
                                        }
                                        std::cout << std::endl;
                                        
                                    } catch (const H5::Exception&) {
                                        // Not a dataset, might be another group
                                        try {
                                            Group subSubGroup = subGroup.openGroup(subObjName);
                                            hsize_t numSubSubObjs = subSubGroup.getNumObjs();
                                            std::cout << " (Group: " << numSubSubObjs << " objects)" << std::endl;
                                        } catch (const H5::Exception&) {
                                            std::cout << " (Unknown type)" << std::endl;
                                        }
                                    }
                                }
                            } catch (const H5::Exception&) {
                                // Not a group, try as dataset
                                try {
                                    DataSet objDataset = chartGroup.openDataSet(objName);
                                    DataSpace objSpace = objDataset.getSpace();
                                    int objRank = objSpace.getSimpleExtentNdims();
                                    hsize_t objDims[3];
                                    objSpace.getSimpleExtentDims(objDims, NULL);
                                    
                                    std::cout << "    Type: Dataset (";
                                    for (int k = 0; k < objRank; k++) {
                                        std::cout << objDims[k];
                                        if (k < objRank - 1) std::cout << "×";
                                    }
                                    std::cout << ")" << std::endl;
                                } catch (const H5::Exception&) {
                                    std::cout << "    Type: Unknown" << std::endl;
                                }
                            }
                        }
                        
                        std::cout << "\n=== COMPRESSION ANALYSIS ===" << std::endl;
                        std::cout << "This f5 file appears to use LZ4 compression which requires additional HDF5 plugins." << std::endl;
                        std::cout << "To read this file, you have several options:" << std::endl;
                        std::cout << "1. Install HDF5 LZ4 plugin from: https://github.com/HDFGroup/hdf5_plugins" << std::endl;
                        std::cout << "2. Convert the file to uncompressed HDF5 format using h5repack:" << std::endl;
                        std::cout << "   h5repack -f NONE input.f5 output.h5" << std::endl;
                        std::cout << "3. Use a different tool to export point cloud data to a supported format (PLY, XYZ, etc.)" << std::endl;
                        std::cout << "==============================" << std::endl;
                        
                        // Check what kinds of groups are actually available
                        bool hasPositions = false, hasRGB = false, hasIntensity = false;
                        
                        // Look for known data groups
                        for (hsize_t i = 0; i < numChartObjs; i++) {
                            std::string objName = chartGroup.getObjnameByIdx(i);
                            if (objName == "Positions" || objName.find("Position") != std::string::npos) {
                                hasPositions = true;
                            } else if (objName == "RGB" || objName.find("Color") != std::string::npos) {
                                hasRGB = true;
                            } else if (objName == "Intensity" || objName.find("Intensity") != std::string::npos) {
                                hasIntensity = true;
                            }
                        }
                        
                        std::cout << "Available data types: ";
                        if (hasPositions) std::cout << "Positions ";
                        if (hasRGB) std::cout << "RGB ";
                        if (hasIntensity) std::cout << "Intensity ";
                        std::cout << std::endl;
                        
                        // Try to read from any available group
                        for (hsize_t i = 0; i < numChartObjs; i++) {
                            std::string objName = chartGroup.getObjnameByIdx(i);
                            
                            try {
                                Group dataGroup = chartGroup.openGroup(objName);
                                hsize_t numDataObjs = dataGroup.getNumObjs();
                                
                                std::cout << "Trying to read from " << objName << " group..." << std::endl;
                                
                                for (hsize_t j = 0; j < numDataObjs; j++) {
                                    std::string dataObjName = dataGroup.getObjnameByIdx(j);
                                    
                                    try {
                                        DataSet dataDataset = dataGroup.openDataSet(dataObjName);
                                        
                                        // Check if this dataset is readable (not compressed with unsupported filter)
                                        DataSpace dataSpace = dataDataset.getSpace();
                                        int dataRank = dataSpace.getSimpleExtentNdims();
                                        hsize_t dataDims[3];
                                        dataSpace.getSimpleExtentDims(dataDims, NULL);
                                        
                                        std::cout << "  Dataset " << dataObjName << " - dimensions: ";
                                        for (int k = 0; k < dataRank; k++) {
                                            std::cout << dataDims[k];
                                            if (k < dataRank - 1) std::cout << " x ";
                                        }
                                        std::cout << std::endl;
                                        
                                        // Try a small test read to check if data is accessible
                                        if (dataRank == 2 && dataDims[1] == 3 && dataDims[0] > 0) {
                                            std::cout << "  Attempting to read sample data..." << std::endl;
                                            
                                            // Try to read just the first few points as a test
                                            hsize_t testPoints = std::min((hsize_t)10, dataDims[0]);
                                            std::vector<float> testData(testPoints * 3);
                                            
                                            // Create a hyperslab for just the first few points
                                            hsize_t start[2] = {0, 0};
                                            hsize_t count[2] = {testPoints, 3};
                                            DataSpace memSpace(2, count);
                                            dataSpace.selectHyperslab(H5S_SELECT_SET, count, start);
                                            
                                            try {
                                                dataDataset.read(testData.data(), PredType::NATIVE_FLOAT, memSpace, dataSpace);
                                                std::cout << "  Successfully read test data! First point: (" 
                                                         << testData[0] << ", " << testData[1] << ", " << testData[2] << ")" << std::endl;
                                                
                                                // If we can read test data, try to read all data
                                                dataSpace.selectAll();
                                                std::vector<float> allData(dataDims[0] * 3);
                                                DataSpace fullMemSpace(2, dataDims);
                                                dataDataset.read(allData.data(), PredType::NATIVE_FLOAT, fullMemSpace, dataSpace);
                                                
                                                // Store the coordinates
                                                xCoords.resize(dataDims[0]);
                                                yCoords.resize(dataDims[0]);
                                                zCoords.resize(dataDims[0]);
                                                
                                                for (hsize_t k = 0; k < dataDims[0]; k++) {
                                                    xCoords[k] = allData[k * 3 + 0];
                                                    yCoords[k] = allData[k * 3 + 1];
                                                    zCoords[k] = allData[k * 3 + 2];
                                                }
                                                
                                                std::cout << "Successfully read all coordinate data: " << dataDims[0] << " points" << std::endl;
                                                datasetFound = true;
                                                break;
                                                
                                            } catch (const H5::Exception& readEx) {
                                                std::cout << "  Could not read data (compression/filter issue): " << readEx.getDetailMsg() << std::endl;
                                                if (readEx.getDetailMsg().find("lz4") != std::string::npos || 
                                                    readEx.getDetailMsg().find("filter") != std::string::npos) {
                                                    std::cout << "  ERROR: This f5 file uses LZ4 compression which requires additional HDF5 plugins." << std::endl;
                                                    std::cout << "  Please install the HDF5 LZ4 plugin or convert the file to an uncompressed format." << std::endl;
                                                }
                                            }
                                        }
                                        
                                    } catch (const H5::Exception&) {
                                        continue;
                                    }
                                }
                                
                                if (datasetFound) break;
                                
                            } catch (const H5::Exception&) {
                                continue;
                            }
                        }
                        
                    } catch (const H5::Exception& e) {
                        std::cout << "Failed to navigate f5 structure: " << e.getDetailMsg() << std::endl;
                    }
                }
                
                if (!datasetFound && numObjs > 0) {
                    std::string firstDatasetName = file.getObjnameByIdx(0);
                    try {
                        dataset = file.openDataSet(firstDatasetName);
                        datasetFound = true;
                        std::cout << "Using first object as dataset: " << firstDatasetName << std::endl;
                    } catch (const H5::Exception&) {
                        throw std::runtime_error("No valid datasets found in HDF5 file");
                    }
                }
                
                if (!datasetFound) {
                    throw std::runtime_error("No datasets found in HDF5 file");
                }
            }

            // Get dataset dimensions
            DataSpace dataspace = dataset.getSpace();
            int rank = dataspace.getSimpleExtentNdims();
            hsize_t dims[2];
            dataspace.getSimpleExtentDims(dims, NULL);
            
            hsize_t totalPoints = (rank == 1) ? dims[0] : dims[0];
            std::cout << "Dataset contains " << totalPoints << " points" << std::endl;

            // Handle downsampling
            hsize_t pointsToRead = totalPoints;
            if (downsampleFactor > 1) {
                pointsToRead = totalPoints / downsampleFactor;
                std::cout << "Downsampling by factor " << downsampleFactor 
                         << ", reading " << pointsToRead << " points" << std::endl;
            }

            // Try different data layouts
            DataType dtype = dataset.getDataType();
            H5T_class_t typeClass = dtype.getClass();

            if (typeClass == H5T_COMPOUND) {
                // ── Compound-type read ───────────────────────────────────────
                // Read the dataset in chunks of kComputeBatchSize and collect
                // them into one point vector for buildComputeCloud.
                constexpr hsize_t CHUNK = static_cast<hsize_t>(PointCloud::kComputeBatchSize);
                std::vector<PointCloudPoint> chunkBuf(CHUNK);

                std::vector<PointCloudPoint> points;
                points.reserve(static_cast<size_t>(pointsToRead));

                for (hsize_t readSoFar = 0; readSoFar < pointsToRead; ) {
                    hsize_t toRead = std::min(CHUNK, pointsToRead - readSoFar);

                    // File-side hyperslab: skip every downsampleFactor-th element
                    hsize_t fileStart[1] = { readSoFar * static_cast<hsize_t>(downsampleFactor) };
                    hsize_t cnt[1]       = { toRead };
                    hsize_t stride[1]    = { static_cast<hsize_t>(downsampleFactor) };
                    hsize_t block[1]     = { 1 };
                    hsize_t memDims[1]   = { toRead };

                    DataSpace memspace(1, memDims);
                    dataspace.selectHyperslab(H5S_SELECT_SET, cnt, fileStart, stride, block);
                    dataset.read(chunkBuf.data(), pointType, memspace, dataspace);

                    points.insert(points.end(), chunkBuf.begin(),
                                  chunkBuf.begin() + static_cast<std::ptrdiff_t>(toRead));
                    readSoFar += toRead;
                }

                // Morton-sort + batch-shuffle + quantise + bulk-upload in one pass.
                buildComputeCloud(pointCloud, points);

                std::cout << "[HDF5] Loaded " << pointCloud.totalPointCount << " points\n";

            } else {
                // Handle separate arrays format (like f5 files)
                std::cout << "Reading data from separate arrays format..." << std::endl;
                
                // For f5 files, try to find the Positions, RGB, and Intensity groups
                if (filePath.substr(filePath.find_last_of(".") + 1) == "f5" && !timeSeriesDataset.empty()) {
                    try {
                        Group timeGroup = file.openGroup(timeSeriesDataset);
                        Group selectionGroup = timeGroup.openGroup("Selection");
                        Group pointsGroup = selectionGroup.openGroup("Points");
                        Group chartGroup = pointsGroup.openGroup("StandardCartesianChart3D");
                        
                        // Try to read positions data
                        
                        try {
                            Group positionsGroup = chartGroup.openGroup("Positions");
                            hsize_t numPosObjs = positionsGroup.getNumObjs();
                            
                            // Look for coordinate datasets
                            for (hsize_t i = 0; i < numPosObjs; i++) {
                                std::string posObjName = positionsGroup.getObjnameByIdx(i);
                                try {
                                    DataSet posDataset = positionsGroup.openDataSet(posObjName);
                                    DataSpace posSpace = posDataset.getSpace();
                                    hsize_t posDims[2];
                                    posSpace.getSimpleExtentDims(posDims, NULL);
                                    
                                    // Read the dataset - assume it contains 3D coordinates
                                    if (posDims[0] == totalPoints) {
                                        std::vector<float> tempData(totalPoints * 3);
                                        posDataset.read(tempData.data(), PredType::NATIVE_FLOAT);
                                        
                                        // Extract X, Y, Z coordinates
                                        xCoords.resize(totalPoints);
                                        yCoords.resize(totalPoints);
                                        zCoords.resize(totalPoints);
                                        
                                        for (hsize_t j = 0; j < totalPoints; j++) {
                                            xCoords[j] = tempData[j * 3 + 0];
                                            yCoords[j] = tempData[j * 3 + 1];
                                            zCoords[j] = tempData[j * 3 + 2];
                                        }
                                        std::cout << "Successfully read position data: " << totalPoints << " points" << std::endl;
                                        break;
                                    }
                                } catch (const H5::Exception&) {
                                    continue;
                                }
                            }
                        } catch (const H5::Exception& e) {
                            std::cout << "Could not read Positions group: " << e.getDetailMsg() << std::endl;
                        }
                        
                        // Try to read RGB data
                        try {
                            Group rgbGroup = chartGroup.openGroup("RGB");
                            hsize_t numRgbObjs = rgbGroup.getNumObjs();
                            
                            for (hsize_t i = 0; i < numRgbObjs; i++) {
                                std::string rgbObjName = rgbGroup.getObjnameByIdx(i);
                                try {
                                    DataSet rgbDataset = rgbGroup.openDataSet(rgbObjName);
                                    DataSpace rgbSpace = rgbDataset.getSpace();
                                    hsize_t rgbDims[2];
                                    rgbSpace.getSimpleExtentDims(rgbDims, NULL);
                                    
                                    if (rgbDims[0] == totalPoints) {
                                        std::vector<float> tempRgbData(totalPoints * 3);
                                        rgbDataset.read(tempRgbData.data(), PredType::NATIVE_FLOAT);
                                        
                                        rColors.resize(totalPoints);
                                        gColors.resize(totalPoints);
                                        bColors.resize(totalPoints);
                                        
                                        for (hsize_t j = 0; j < totalPoints; j++) {
                                            rColors[j] = tempRgbData[j * 3 + 0] / 255.0f; // Normalize if needed
                                            gColors[j] = tempRgbData[j * 3 + 1] / 255.0f;
                                            bColors[j] = tempRgbData[j * 3 + 2] / 255.0f;
                                        }
                                        std::cout << "Successfully read RGB data" << std::endl;
                                        break;
                                    }
                                } catch (const H5::Exception&) {
                                    continue;
                                }
                            }
                        } catch (const H5::Exception& e) {
                            std::cout << "Could not read RGB group: " << e.getDetailMsg() << std::endl;
                        }
                        
                        // Try to read Intensity data
                        try {
                            Group intensityGroup = chartGroup.openGroup("Intensity");
                            hsize_t numIntObjs = intensityGroup.getNumObjs();
                            
                            for (hsize_t i = 0; i < numIntObjs; i++) {
                                std::string intObjName = intensityGroup.getObjnameByIdx(i);
                                try {
                                    DataSet intDataset = intensityGroup.openDataSet(intObjName);
                                    DataSpace intSpace = intDataset.getSpace();
                                    hsize_t intDims[2];
                                    intSpace.getSimpleExtentDims(intDims, NULL);
                                    
                                    if (intDims[0] == totalPoints) {
                                        intensities.resize(totalPoints);
                                        intDataset.read(intensities.data(), PredType::NATIVE_FLOAT);
                                        std::cout << "Successfully read Intensity data" << std::endl;
                                        break;
                                    }
                                } catch (const H5::Exception&) {
                                    continue;
                                }
                            }
                        } catch (const H5::Exception& e) {
                            std::cout << "Could not read Intensity group: " << e.getDetailMsg() << std::endl;
                        }
                        
                        // If we successfully read coordinate data from f5, stream to GPU
                        if (!xCoords.empty() && !yCoords.empty() && !zCoords.empty()) {
                            const hsize_t numCoordPoints = static_cast<hsize_t>(xCoords.size());
                            const hsize_t ptsToUpload   = (downsampleFactor > 1)
                                                         ? numCoordPoints / static_cast<hsize_t>(downsampleFactor)
                                                         : numCoordPoints;


                            std::vector<PointCloudPoint> batchBuf;
                            batchBuf.reserve(static_cast<size_t>(ptsToUpload));
                            glm::vec3 gMin( FLT_MAX), gMax(-FLT_MAX);

                            // Points accumulate in batchBuf; built in one pass below.
                            auto flushF5Batch = [&]() {};

                            for (hsize_t i = 0; i < ptsToUpload; i++) {
                                const hsize_t src = i * static_cast<hsize_t>(downsampleFactor);

                                PointCloudPoint pt;
                                pt.position.x = xCoords[src];
                                pt.position.y = yCoords[src];
                                pt.position.z = zCoords[src];

                                if (!rColors.empty() && src < rColors.size()) {
                                    pt.color.r = rColors[src];
                                    pt.color.g = gColors[src];
                                    pt.color.b = bColors[src];
                                } else {
                                    pt.color = glm::vec3(1.0f);
                                }

                                pt.intensity = (!intensities.empty() && src < intensities.size())
                                             ? intensities[src] : 1.0f;

                                gMin = glm::min(gMin, pt.position);
                                gMax = glm::max(gMax, pt.position);
                                batchBuf.push_back(pt);
                                if (static_cast<int>(batchBuf.size()) == PointCloud::kComputeBatchSize)
                                    flushF5Batch();
                            }
                            // Release the large coordinate vectors before building.
                            { std::vector<float>().swap(xCoords); }
                            { std::vector<float>().swap(yCoords); }
                            { std::vector<float>().swap(zCoords); }
                            { std::vector<float>().swap(rColors); }
                            { std::vector<float>().swap(gColors); }
                            { std::vector<float>().swap(bColors); }
                            { std::vector<float>().swap(intensities); }

                            // Morton-sort + batch-shuffle + quantise + bulk-upload.
                            buildComputeCloud(pointCloud, batchBuf);

                            std::cout << "[HDF5-f5] Loaded " << pointCloud.totalPointCount
                                      << " points\n";

                            file.close();
                            return std::move(pointCloud);

                        } else {
                            std::cout << "Could not find valid coordinate data in f5 file" << std::endl;
                        }
                        
                    } catch (const H5::Exception& e) {
                        std::cout << "Error reading f5 separate arrays: " << e.getDetailMsg() << std::endl;
                    }
                } else {
                    std::cout << "Separate arrays format not fully supported for non-f5 files" << std::endl;
                }
            }

            file.close();

            std::cout << "[HDF5] Load complete: " << pointCloud.totalPointCount
                      << " points in " << pointCloud.numBatches << " batches\n";

        } catch (const H5::Exception& e) {
            std::cerr << "HDF5 error loading point cloud: " << e.getDetailMsg() << std::endl;
            return std::move(pointCloud);
        } catch (const std::exception& e) {
            std::cerr << "Error loading HDF5 point cloud: " << e.what() << std::endl;
            return std::move(pointCloud);
        }

        // If the compound-type path already built the SSBOs, we're done.
        // If pointCloud.points was somehow populated (fallback paths), run the
        // legacy setup which will also stream-upload and clear the CPU vector.
        setupPointCloudGLBuffers(pointCloud);

        return std::move(pointCloud);
    }

    bool PointCloudLoader::exportToHDF5(const PointCloud& pointCloud, const std::string& filePath,
                                        bool applyTransform) {
        try {
            std::cout << "Exporting point cloud to HDF5: " << filePath << std::endl;

            const size_t totalPoints = !pointCloud.points.empty()
                                     ? pointCloud.points.size()
                                     : static_cast<size_t>(pointCloud.totalPointCount);
            if (totalPoints == 0) {
                std::cerr << "[PCExport] HDF5 export failed - point cloud '"
                          << pointCloud.name << "' has no point data\n";
                return false;
            }

            // Create the HDF5 file
            H5File file(filePath, H5F_ACC_TRUNC);

            // Define the HDF5 compound type for PointCloudPoint
            CompType pointType(sizeof(PointCloudPoint));
            pointType.insertMember("position_x", HOFFSET(PointCloudPoint, position.x), PredType::NATIVE_FLOAT);
            pointType.insertMember("position_y", HOFFSET(PointCloudPoint, position.y), PredType::NATIVE_FLOAT);
            pointType.insertMember("position_z", HOFFSET(PointCloudPoint, position.z), PredType::NATIVE_FLOAT);
            pointType.insertMember("intensity", HOFFSET(PointCloudPoint, intensity), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_r", HOFFSET(PointCloudPoint, color.r), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_g", HOFFSET(PointCloudPoint, color.g), PredType::NATIVE_FLOAT);
            pointType.insertMember("color_b", HOFFSET(PointCloudPoint, color.b), PredType::NATIVE_FLOAT);

            const glm::mat4 transform = pointCloudTransform(pointCloud, applyTransform);

            // Create dataspace and dataset sized for the full cloud, then
            // stream the points into it one decoded batch (hyperslab) at a
            // time — peak CPU RAM stays at one batch regardless of cloud size.
            hsize_t dims[1] = { static_cast<hsize_t>(totalPoints) };
            DataSpace dataspace(1, dims);
            DataSet dataset = file.createDataSet("points", pointType, dataspace);

            std::vector<PointCloudPoint> chunkBuf;
            hsize_t writeOffset = 0;
            const bool ok = forEachPointBatch(pointCloud,
                [&](const PointCloudPoint* pts, size_t count) {
                    chunkBuf.assign(pts, pts + count);
                    if (applyTransform) {
                        for (auto& point : chunkBuf) {
                            point.position = glm::vec3(transform * glm::vec4(point.position, 1.0f));
                        }
                    }

                    hsize_t start[1] = { writeOffset };
                    hsize_t cnt[1]   = { static_cast<hsize_t>(count) };
                    DataSpace memspace(1, cnt);
                    DataSpace filespace = dataset.getSpace();
                    filespace.selectHyperslab(H5S_SELECT_SET, cnt, start);
                    dataset.write(chunkBuf.data(), pointType, memspace, filespace);
                    writeOffset += count;
                });

            if (!ok || writeOffset == 0) {
                std::cerr << "[PCExport] HDF5 export failed - no point data written to "
                          << filePath << std::endl;
                return false;
            }

            // Add metadata attributes
            DataSpace scalarSpace(H5S_SCALAR);

            // Add point count attribute
            Attribute pointCountAttr = dataset.createAttribute("point_count", PredType::NATIVE_HSIZE, scalarSpace);
            hsize_t pointCount = writeOffset;
            pointCountAttr.write(PredType::NATIVE_HSIZE, &pointCount);
            
            // Add name attribute
            StrType stringType(PredType::C_S1, pointCloud.name.length() + 1);
            Attribute nameAttr = dataset.createAttribute("name", stringType, scalarSpace);
            nameAttr.write(stringType, pointCloud.name.c_str());
            
            // Add creation timestamp
            auto now = std::chrono::system_clock::now();
            std::time_t time = std::chrono::system_clock::to_time_t(now);
            
            char timeBuffer[26] = {};
            struct tm tmBuf = {};
            localtime_s(&tmBuf, &time);
            std::strftime(timeBuffer, sizeof(timeBuffer), "%c", &tmBuf);
            std::string timeStr(timeBuffer);
            
            StrType timeStringType(PredType::C_S1, timeStr.length() + 1);
            Attribute timeAttr = dataset.createAttribute("created", timeStringType, scalarSpace);
            timeAttr.write(timeStringType, timeStr.c_str());

            file.close();

            std::cout << "Successfully exported " << writeOffset
                     << " points to HDF5 file: " << filePath << std::endl;
            return true;

        } catch (const H5::Exception& e) {
            std::cerr << "HDF5 error exporting point cloud: " << e.getDetailMsg() << std::endl;
            return false;
        } catch (const std::exception& e) {
            std::cerr << "Error exporting HDF5 point cloud: " << e.what() << std::endl;
            return false;
        }
    }

    // -------------------------------------------------------------------------
    // LAS / LAZ loader  (uses LASzip – handles all LAS 1.0-1.4 and LAZ)
    // Streaming version: points are quantised and uploaded to GPU one batch at
    // a time.  Peak CPU RAM ≈ one batch (~280 KB) + one tiny sample buffer.
    // -------------------------------------------------------------------------
    PointCloud PointCloudLoader::loadFromLAS(const std::string& filePath, size_t downsampleFactor,
                                              const glm::dvec3* globalCenter)
    {
        PointCloud pointCloud;
        pointCloud.name     = std::filesystem::path(filePath).stem().string();
        pointCloud.position = glm::vec3(0.0f);
        pointCloud.rotation = glm::vec3(-90.0f, 0.0f, 0.0f);
        pointCloud.scale    = glm::vec3(0.1f);

        // ── Helper: open a LASzip reader ─────────────────────────────────────
        auto openReader = [&](laszip_POINTER& r, laszip_BOOL& compressed) -> bool {
            if (laszip_create(&r)) {
                std::cerr << "[LAS] laszip_create failed\n";
                return false;
            }
            if (laszip_open_reader(r, filePath.c_str(), &compressed)) {
                laszip_CHAR* msg = nullptr;
                laszip_get_error(r, &msg);
                std::cerr << "[LAS] Cannot open '" << filePath << "': "
                          << (msg ? msg : "unknown") << "\n";
                laszip_destroy(r);
                r = nullptr;
                return false;
            }
            return true;
        };

        // ── Pass 0: open and read header ─────────────────────────────────────
        laszip_POINTER reader      = nullptr;
        laszip_BOOL    is_compressed = 0;
        if (!openReader(reader, is_compressed)) return pointCloud;

        laszip_header_struct* hdr = nullptr;
        laszip_get_header_pointer(reader, &hdr);

        const double sx = hdr->x_scale_factor, sy = hdr->y_scale_factor, sz = hdr->z_scale_factor;
        const double ox = hdr->x_offset,       oy = hdr->y_offset,       oz = hdr->z_offset;

        const laszip_U8 fmt      = hdr->point_data_format;
        const bool      hasColor = (fmt == 2 || fmt == 3 || fmt == 5 ||
                                    fmt == 7 || fmt == 8 || fmt == 10);

        const int64_t nPoints =
            (hdr->version_minor >= 4 && hdr->extended_number_of_point_records > 0)
                ? static_cast<int64_t>(hdr->extended_number_of_point_records)
                : static_cast<int64_t>(hdr->number_of_point_records);

        // Subtract a centre point to keep float coordinates near origin.
        // When loading multiple files as one scene, globalCenter is the shared
        // centre across all files so they stay correctly positioned relative to
        // each other.  For a single-file load it falls back to this file's own
        // bounding-box midpoint.
        const double cx = globalCenter ? globalCenter->x : (hdr->min_x + hdr->max_x) * 0.5;
        const double cy = globalCenter ? globalCenter->y : (hdr->min_y + hdr->max_y) * 0.5;
        const double cz = globalCenter ? globalCenter->z : (hdr->min_z + hdr->max_z) * 0.5;

        std::cout << "[LAS] " << (is_compressed ? "LAZ" : "LAS")
                  << " v1." << static_cast<int>(hdr->version_minor)
                  << "  fmt=" << static_cast<int>(fmt)
                  << "  nPoints=" << nPoints
                  << "  hasColor=" << hasColor << "\n"
                  << "[LAS] Bounds X[" << hdr->min_x << ".." << hdr->max_x << "]"
                  << " Y[" << hdr->min_y << ".." << hdr->max_y << "]"
                  << " Z[" << hdr->min_z << ".." << hdr->max_z << "]\n"
                  << "[LAS] Centre (" << cx << "," << cy << "," << cz
                  << ") – subtracted to bring cloud to origin\n";

        // ── Pass 1 (colour only): sample up to 2000 points to decide whether
        //    RGB channels are 8-bit (0-255) or 16-bit (0-65535).
        //    This is a tiny seek, not a full re-read.
        float colorScale = 255.0f;
        if (hasColor) {
            laszip_point_struct* lp = nullptr;
            laszip_get_point_pointer(reader, &lp);
            const int64_t sampleLimit = std::min((int64_t)2000, nPoints);
            laszip_U16 sampleMax = 0;
            for (int64_t i = 0; i < sampleLimit; i++) {
                if (laszip_read_point(reader)) break;
                sampleMax = std::max({ sampleMax, lp->rgb[0], lp->rgb[1], lp->rgb[2] });
                if (sampleMax > 255) break; // confirmed 16-bit early
            }
            colorScale = (sampleMax > 255) ? 65535.0f : 255.0f;
            std::cout << "[LAS] Colour scale detected: " << colorScale
                      << " (sample max=" << sampleMax << ")\n";
        }

        // Close and reopen so we read from point 0 again
        laszip_close_reader(reader);
        laszip_destroy(reader);
        reader = nullptr;
        if (!openReader(reader, is_compressed)) return pointCloud;
        laszip_get_header_pointer(reader, &hdr); // refresh pointer after reopen

        // ── Pre-allocate SSBOs using header point count ──────────────────────
        const int64_t expectedPts = (downsampleFactor > 1)
                                  ? (nPoints / static_cast<int64_t>(downsampleFactor) + 1)
                                  : nPoints;

        // ── Pass 2: stream-read → batch → upload ─────────────────────────────
        laszip_point_struct* lp = nullptr;
        laszip_get_point_pointer(reader, &lp);

        std::vector<PointCloudPoint> batchBuf;
        batchBuf.reserve(static_cast<size_t>(expectedPts));

        // Points accumulate in batchBuf; built in one pass below.
        auto flushBatch = [&]() {};

        for (int64_t i = 0; i < nPoints; ++i) {
            if (laszip_read_point(reader)) {
                std::cerr << "[LAS] Read error at point " << i << "\n";
                break;
            }

            if (downsampleFactor > 1 && (static_cast<size_t>(i) % downsampleFactor) != 0)
                continue;

            PointCloudPoint pt;
            pt.position.x = static_cast<float>(lp->X * sx + ox - cx);
            pt.position.y = static_cast<float>(lp->Y * sy + oy - cy);
            pt.position.z = static_cast<float>(lp->Z * sz + oz - cz);
            pt.intensity  = lp->intensity / 65535.0f;

            if (hasColor) {
                pt.color.r = lp->rgb[0] / colorScale;
                pt.color.g = lp->rgb[1] / colorScale;
                pt.color.b = lp->rgb[2] / colorScale;
            } else {
                pt.color = glm::vec3(pt.intensity);
            }

            batchBuf.push_back(pt);
            if (static_cast<int>(batchBuf.size()) == PointCloud::kComputeBatchSize)
                flushBatch();
        }

        flushBatch(); // Upload last partial batch

        // Save bounds BEFORE closing/destroying the reader – hdr is an internal
        // pointer inside the laszip reader and becomes dangling after destroy.
        const glm::vec3 lasMin(
            static_cast<float>(hdr->min_x - cx),
            static_cast<float>(hdr->min_y - cy),
            static_cast<float>(hdr->min_z - cz));
        const glm::vec3 lasMax(
            static_cast<float>(hdr->max_x - cx),
            static_cast<float>(hdr->max_y - cy),
            static_cast<float>(hdr->max_z - cz));

        laszip_close_reader(reader);
        laszip_destroy(reader);
        reader = nullptr; // prevent any accidental re-use

        // Morton-sort + batch-shuffle + quantise + bulk-upload in one pass.
        // (boundsMin/Max are recomputed from the actual points by buildComputeCloud;
        //  the header bounds lasMin/lasMax are no longer needed.)
        (void)lasMin; (void)lasMax;
        buildComputeCloud(pointCloud, batchBuf);

        std::cout << "[LAS] Loaded " << pointCloud.totalPointCount << " points\n";
        return pointCloud;
    }

    // -------------------------------------------------------------------------
    // Multi-file LAS/LAZ loader
    // Reads all file headers first to compute a single shared bounding-box
    // centre, then loads each file with that centre so the tiles stay
    // correctly positioned relative to each other.
    // -------------------------------------------------------------------------
    std::vector<PointCloud> PointCloudLoader::loadFromLASMultiple(
        const std::vector<std::string>& filePaths, size_t downsampleFactor)
    {
        if (filePaths.empty()) return {};

        // Pass 1: scan every header to build the global bounding box
        double gMinX =  DBL_MAX, gMinY =  DBL_MAX, gMinZ =  DBL_MAX;
        double gMaxX = -DBL_MAX, gMaxY = -DBL_MAX, gMaxZ = -DBL_MAX;
        int    validHeaders = 0;

        for (const auto& path : filePaths) {
            laszip_POINTER r = nullptr;
            laszip_BOOL    compressed = 0;
            if (laszip_create(&r)) continue;
            if (laszip_open_reader(r, path.c_str(), &compressed)) {
                laszip_destroy(r); continue;
            }
            laszip_header_struct* hdr = nullptr;
            laszip_get_header_pointer(r, &hdr);

            gMinX = std::min(gMinX, hdr->min_x);  gMaxX = std::max(gMaxX, hdr->max_x);
            gMinY = std::min(gMinY, hdr->min_y);  gMaxY = std::max(gMaxY, hdr->max_y);
            gMinZ = std::min(gMinZ, hdr->min_z);  gMaxZ = std::max(gMaxZ, hdr->max_z);
            ++validHeaders;

            laszip_close_reader(r);
            laszip_destroy(r);
        }

        if (validHeaders == 0) {
            std::cerr << "[LAS-Multi] No valid headers found\n";
            return {};
        }

        const glm::dvec3 globalCenter((gMinX + gMaxX) * 0.5,
                                       (gMinY + gMaxY) * 0.5,
                                       (gMinZ + gMaxZ) * 0.5);

        std::cout << "[LAS-Multi] " << filePaths.size() << " file(s), "
                  << validHeaders << " valid\n"
                  << "[LAS-Multi] Global bounds X[" << gMinX << ".." << gMaxX << "]"
                  << " Y[" << gMinY << ".." << gMaxY << "]"
                  << " Z[" << gMinZ << ".." << gMaxZ << "]\n"
                  << "[LAS-Multi] Shared centre ("
                  << globalCenter.x << "," << globalCenter.y << "," << globalCenter.z << ")\n";

        // Pass 2: load each file using the shared global centre
        std::vector<PointCloud> result;
        result.reserve(filePaths.size());
        for (const auto& path : filePaths) {
            PointCloud pc = loadFromLAS(path, downsampleFactor, &globalCenter);
            if (pc.isLoaded()) {
                pc.filePath = path;
                result.push_back(std::move(pc));
            } else {
                std::cerr << "[LAS-Multi] Failed to load: " << path << "\n";
            }
        }
        return result;
    }

    // -------------------------------------------------------------------------
    // Progressive single-file LAS/LAZ loader
    // Reads the header on the calling (GL) thread to learn the exact point count
    // and bounds, pre-allocates the compute SSBOs to that size, then spawns a
    // background worker that streams the points in.  Returns immediately with a
    // PointCloud whose bounds are already valid and whose batches grow each frame
    // (via updateStreaming).  globalCenter shares a centre across a multi-file
    // load so tiles stay correctly positioned relative to each other.
    // -------------------------------------------------------------------------
    PointCloud PointCloudLoader::beginLoadLASProgressive(const std::string& filePath,
                                                         size_t downsampleFactor,
                                                         const glm::dvec3* globalCenter,
                                                         bool mortonResort) {
        PointCloud pc;
        pc.name     = std::filesystem::path(filePath).stem().string();
        pc.filePath = filePath;
        pc.position  = glm::vec3(0.0f);
        pc.rotation  = glm::vec3(-90.0f, 0.0f, 0.0f);
        pc.scale     = glm::vec3(0.1f);

        laszip_POINTER reader = nullptr; laszip_BOOL comp = 0;
        if (laszip_create(&reader)) { std::cerr << "[LAS-Stream] laszip_create failed\n"; return pc; }
        if (laszip_open_reader(reader, filePath.c_str(), &comp)) {
            std::cerr << "[LAS-Stream] Cannot open '" << filePath << "'\n";
            laszip_destroy(reader); return pc;
        }
        laszip_header_struct* hdr = nullptr; laszip_get_header_pointer(reader, &hdr);

        const double sx = hdr->x_scale_factor, sy = hdr->y_scale_factor, sz = hdr->z_scale_factor;
        const double ox = hdr->x_offset,       oy = hdr->y_offset,       oz = hdr->z_offset;
        const laszip_U8 fmt = hdr->point_data_format;
        const bool hasColor = (fmt == 2 || fmt == 3 || fmt == 5 || fmt == 7 || fmt == 8 || fmt == 10);
        const int64_t nPoints =
            (hdr->version_minor >= 4 && hdr->extended_number_of_point_records > 0)
                ? static_cast<int64_t>(hdr->extended_number_of_point_records)
                : static_cast<int64_t>(hdr->number_of_point_records);
        const double cx = globalCenter ? globalCenter->x : (hdr->min_x + hdr->max_x) * 0.5;
        const double cy = globalCenter ? globalCenter->y : (hdr->min_y + hdr->max_y) * 0.5;
        const double cz = globalCenter ? globalCenter->z : (hdr->min_z + hdr->max_z) * 0.5;

        // Header bounds → valid extents before any point is read (camera framing,
        // SpaceMouse) in the cloud's local space (world − centre).
        pc.boundsMin = glm::vec3(static_cast<float>(hdr->min_x - cx),
                                 static_cast<float>(hdr->min_y - cy),
                                 static_cast<float>(hdr->min_z - cz));
        pc.boundsMax = glm::vec3(static_cast<float>(hdr->max_x - cx),
                                 static_cast<float>(hdr->max_y - cy),
                                 static_cast<float>(hdr->max_z - cz));

        // Detect 8- vs 16-bit colour from a tiny sample (matches loadFromLAS).
        float colorScale = 255.0f;
        if (hasColor) {
            laszip_point_struct* lp = nullptr; laszip_get_point_pointer(reader, &lp);
            const int64_t lim = std::min(static_cast<int64_t>(2000), nPoints);
            laszip_U16 mx = 0;
            for (int64_t i = 0; i < lim; ++i) {
                if (laszip_read_point(reader)) break;
                mx = std::max({ mx, lp->rgb[0], lp->rgb[1], lp->rgb[2] });
                if (mx > 255) break;
            }
            colorScale = (mx > 255) ? 65535.0f : 255.0f;
        }
        laszip_close_reader(reader);
        laszip_destroy(reader);
        reader = nullptr;

        if (nPoints <= 0) { std::cerr << "[LAS-Stream] No points in " << filePath << "\n"; return pc; }

        // Header count is exact; size the allocation for the kept points.
        const uint32_t allocPoints = (downsampleFactor > 1)
            ? static_cast<uint32_t>((nPoints + static_cast<int64_t>(downsampleFactor) - 1) / static_cast<int64_t>(downsampleFactor))
            : static_cast<uint32_t>(nPoints);
        const uint32_t allocBatches =
            (allocPoints + PointCloud::kComputeBatchSize - 1) / PointCloud::kComputeBatchSize;

        // Pre-allocate the 5 SSBOs to full size.  When GL_ARB_sparse_buffer is
        // available we reserve sparsely (glBufferStorage + SPARSE bit) and commit
        // physical pages on demand as chunks arrive (Schütz), so VRAM tracks the
        // loaded portion; otherwise we fall back to a plain glBufferData
        // reservation.  Either way VRAM scales with the cloud (no artificial cap)
        // and allocation fails cleanly if the cloud exceeds VRAM.
        bool useSparse = sparseBuffersAvailable();
        auto allocAll = [&]() -> bool {
            deleteComputeSSBOs(pc);
            auto alloc = [&](GLuint& id, GLsizeiptr bytes) -> bool {
                glGenBuffers(1, &id);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
                if (useSparse)
                    glBufferStorage(GL_SHADER_STORAGE_BUFFER, bytes, nullptr,
                                    GL_DYNAMIC_STORAGE_BIT | GL_SPARSE_STORAGE_BIT_ARB);
                else
                    glBufferData(GL_SHADER_STORAGE_BUFFER, bytes, nullptr, GL_STATIC_DRAW);
                return glGetError() == GL_NO_ERROR;
            };
            bool ok = true;
            ok = alloc(pc.computeBatchSSBO,  static_cast<GLsizeiptr>(allocBatches) * sizeof(ComputeBatch)) && ok;
            ok = alloc(pc.computeXyz4bSSBO,  static_cast<GLsizeiptr>(allocPoints)  * sizeof(uint32_t)) && ok;
            ok = alloc(pc.computeXyz8bSSBO,  static_cast<GLsizeiptr>(allocPoints)  * sizeof(uint32_t)) && ok;
            ok = alloc(pc.computeXyz12bSSBO, static_cast<GLsizeiptr>(allocPoints)  * sizeof(uint32_t)) && ok;
            ok = alloc(pc.computeRGBASSBO,   static_cast<GLsizeiptr>(allocPoints)  * sizeof(uint32_t)) && ok;
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
            return ok;
        };
        bool ok = allocAll();
        if (!ok && useSparse) {
            // Sparse storage was reported available but the driver rejected it;
            // disable sparse globally and retry with a normal reservation.
            std::cerr << "[LAS-Stream] sparse allocation rejected – retrying non-sparse\n";
            s_sparseAvailable = false;
            useSparse = false;
            ok = allocAll();
        }
        if (!ok) {
            std::cerr << "[LAS-Stream] GPU allocation failed for " << allocPoints
                      << " points – file likely exceeds available VRAM\n";
            deleteComputeSSBOs(pc);
            pc.boundsMin = glm::vec3( FLT_MAX);
            pc.boundsMax = glm::vec3(-FLT_MAX);
            return pc; // isLoaded() == false → caller reports failure
        }

        pc.numBatches             = 0;
        pc.totalPointCount        = 0;
        pc.computePointsPerThread = (PointCloud::kComputeBatchSize + 127) / 128;
        pc.streamTargetCount      = allocPoints;

        pc.stream = std::make_shared<PointCloudStream>();
        pc.stream->targetPoints  = allocPoints;
        pc.stream->targetBatches = allocBatches;
        pc.stream->resort        = mortonResort;
        pc.stream->sparse        = useSparse;
        PointCloudStream* S = pc.stream.get();
        S->worker = std::thread(streamWorkerLAS, S, filePath, downsampleFactor,
                                sx, sy, sz, ox, oy, oz, cx, cy, cz,
                                hasColor, colorScale, nPoints);

        std::cout << "[LAS-Stream] " << (comp ? "LAZ" : "LAS") << " " << filePath
                  << " – streaming " << nPoints << " points (" << allocBatches << " batches), "
                  << (mortonResort ? "two-phase (file-order → Morton resort)" : "file-order only")
                  << (useSparse ? ", sparse VRAM" : "") << "\n";
        return pc;
    }

    // Multi-file progressive loader: scans every header for a shared centre, then
    // kicks off one background stream per file.  All files load concurrently, so
    // multi-tile sets fill in together instead of one-after-another.
    std::vector<PointCloud> PointCloudLoader::beginLoadLASMultipleProgressive(
        const std::vector<std::string>& filePaths, size_t downsampleFactor, bool mortonResort) {
        if (filePaths.empty()) return {};

        double gMinX =  DBL_MAX, gMinY =  DBL_MAX, gMinZ =  DBL_MAX;
        double gMaxX = -DBL_MAX, gMaxY = -DBL_MAX, gMaxZ = -DBL_MAX;
        int    valid = 0;
        for (const auto& path : filePaths) {
            laszip_POINTER r = nullptr; laszip_BOOL c = 0;
            if (laszip_create(&r)) continue;
            if (laszip_open_reader(r, path.c_str(), &c)) { laszip_destroy(r); continue; }
            laszip_header_struct* h = nullptr; laszip_get_header_pointer(r, &h);
            gMinX = std::min(gMinX, h->min_x); gMaxX = std::max(gMaxX, h->max_x);
            gMinY = std::min(gMinY, h->min_y); gMaxY = std::max(gMaxY, h->max_y);
            gMinZ = std::min(gMinZ, h->min_z); gMaxZ = std::max(gMaxZ, h->max_z);
            ++valid;
            laszip_close_reader(r); laszip_destroy(r);
        }
        if (valid == 0) { std::cerr << "[LAS-Stream-Multi] No valid headers\n"; return {}; }

        const glm::dvec3 center((gMinX + gMaxX) * 0.5, (gMinY + gMaxY) * 0.5, (gMinZ + gMaxZ) * 0.5);
        std::vector<PointCloud> out;
        out.reserve(filePaths.size());
        for (const auto& path : filePaths) {
            PointCloud pc = beginLoadLASProgressive(path, downsampleFactor, &center, mortonResort);
            if (pc.isLoaded()) { pc.filePath = path; out.push_back(std::move(pc)); }
            else std::cerr << "[LAS-Stream-Multi] Failed to start: " << path << "\n";
        }
        return out;
    }

    // Per-frame pump (GL/main thread).  Phase 1: drain ready file-order chunks
    // into the pre-allocated SSBOs (committing sparse pages first), growing
    // numBatches so the rasterizer draws newly-arrived points.  Phase 2: once the
    // worker publishes the globally Morton-sorted arrays, overwrite the SSBOs in
    // place (visually identical re-ordering) for full render speed.  No-op for
    // non-streaming clouds.
    void PointCloudLoader::updateStreaming(PointCloud& pc) {
        PointCloudStream* S = pc.stream.get();
        if (!S) return;

        const GLsizeiptr ptBufBytes  = static_cast<GLsizeiptr>(pc.streamTargetCount) * sizeof(uint32_t);
        const GLsizeiptr batBufBytes = static_cast<GLsizeiptr>(S->targetBatches) * sizeof(ComputeBatch);

        auto put = [&](GLuint id, GLintptr off, GLsizeiptr byt, const void* data, GLsizeiptr total) {
            glBindBuffer(GL_SHADER_STORAGE_BUFFER, id);
            commitSparseRange(S->sparse, off, byt, total);
            glBufferSubData(GL_SHADER_STORAGE_BUFFER, off, byt, data);
        };

        // ── Phase 1: drain ready file-order chunks ───────────────────────────
        constexpr int maxChunksPerFrame = 8; // cap upload work per frame
        int processed = 0;
        for (;;) {
            StreamChunk ck;
            {
                std::unique_lock<std::mutex> lk(S->mtx);
                if (S->ready.empty()) break;
                ck = std::move(S->ready.front());
                S->ready.pop();
            }
            S->cv.notify_all(); // release backpressure for the worker

            const GLintptr   pOff = static_cast<GLintptr>(ck.firstPoint) * sizeof(uint32_t);
            const GLsizeiptr pByt = static_cast<GLsizeiptr>(ck.numPoints) * sizeof(uint32_t);
            put(pc.computeXyz4bSSBO,  pOff, pByt, ck.xyz4b.data(),  ptBufBytes);
            put(pc.computeXyz8bSSBO,  pOff, pByt, ck.xyz8b.data(),  ptBufBytes);
            put(pc.computeXyz12bSSBO, pOff, pByt, ck.xyz12b.data(), ptBufBytes);
            put(pc.computeRGBASSBO,   pOff, pByt, ck.rgba.data(),   ptBufBytes);
            put(pc.computeBatchSSBO,
                static_cast<GLintptr>(ck.firstBatch) * sizeof(ComputeBatch),
                static_cast<GLsizeiptr>(ck.batches.size()) * sizeof(ComputeBatch),
                ck.batches.data(), batBufBytes);

            pc.totalPointCount += ck.numPoints;
            pc.numBatches      += static_cast<uint32_t>(ck.batches.size());
            if (++processed >= maxChunksPerFrame) break;
        }
        glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);

        // ── Phase 2: in-place swap to the global Morton-sorted arrays ─────────
        // Only after every phase-1 chunk is uploaded (numBatches final, pages
        // committed). Boundaries align to kComputeBatchSize and re-ordering is
        // visually identical, so this single overwrite is safe.
        if (S->resortReady.load()) {
            bool drained = false;
            { std::lock_guard<std::mutex> lk(S->mtx); drained = S->ready.empty(); }
            if (drained) {
                const GLsizeiptr nBy = static_cast<GLsizeiptr>(S->r_xyz4b.size()) * sizeof(uint32_t);
                const GLsizeiptr bBy = static_cast<GLsizeiptr>(S->r_batches.size()) * sizeof(ComputeBatch);
                put(pc.computeXyz4bSSBO,  0, nBy, S->r_xyz4b.data(),  ptBufBytes);
                put(pc.computeXyz8bSSBO,  0, nBy, S->r_xyz8b.data(),  ptBufBytes);
                put(pc.computeXyz12bSSBO, 0, nBy, S->r_xyz12b.data(), ptBufBytes);
                put(pc.computeRGBASSBO,   0, nBy, S->r_rgba.data(),   ptBufBytes);
                put(pc.computeBatchSSBO,  0, bBy, S->r_batches.data(), batBufBytes);
                glBindBuffer(GL_SHADER_STORAGE_BUFFER, 0);
                pc.numBatches      = static_cast<uint32_t>(S->r_batches.size());
                pc.totalPointCount = static_cast<uint32_t>(S->r_xyz4b.size());
                std::cout << "[LAS-Stream] '" << pc.name << "' Morton resort applied ("
                          << pc.totalPointCount << " points, " << pc.numBatches << " batches)\n";
                pc.stream.reset(); // done (dtor joins the finished worker)
                return;
            }
        }

        // ── Finalize: file-order path (no resort) or a failed phase 2 ────────
        if (S->finished.load() && !S->resortReady.load()) {
            bool drained = false;
            { std::lock_guard<std::mutex> lk(S->mtx); drained = S->ready.empty(); }
            if (drained) {
                const bool     failed = S->failed.load();
                const uint32_t loaded = pc.totalPointCount;
                const uint32_t nb     = pc.numBatches;
                pc.stream.reset(); // dtor joins the (already-finished) worker
                if (failed)
                    std::cerr << "[LAS-Stream] '" << pc.name << "' finished with errors ("
                              << loaded << " points loaded)\n";
                else
                    std::cout << "[LAS-Stream] '" << pc.name << "' complete (file-order): "
                              << loaded << " points, " << nb << " batches\n";
            }
        }
    }

    // Set the GL extension loader (e.g. glfwGetProcAddress) so sparse buffers can
    // be probed lazily on first use. Call once from main after the GL context and
    // GLAD are initialised. Safe to pass nullptr (disables sparse buffers).
    void PointCloudLoader::initGLExtensions(void* (*procLoader)(const char*)) {
        s_glProcLoader  = procLoader;
        s_sparseChecked = false; // re-probe on next allocation
    }

} // namespace Engine