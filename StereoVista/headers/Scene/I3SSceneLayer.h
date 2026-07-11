#pragma once

// ============================================================================
// scene::I3SSceneLayer — an opened SLPK layer as a scene object.
// ----------------------------------------------------------------------------
// Parallel to scene::Model, NOT inside it: an I3S layer is a paged LOD
// hierarchy, not a mesh list. M0 delivered open + inspect (mmap + JSON + node
// tree + OBB overlay); M1 added mesh rendering; M2 upgrades the pipeline to
// the full streaming design (plan §6):
//
//   decode workers (no Vulkan!)            main thread (owns all Vulkan)
//   ---------------------------            -----------------------------------
//   archive read + draco/raw decode        pump(): evict LRU over budget ->
//    + jpg/png/ktx2 texture decode           drain decoded payloads into the
//    (KTX2/Basis -> BC7, CPU mip              CPU cache -> create device
//     chains) + geodetic transform            buffers + stage payloads through
//    -> NodePayload -> ready queue            the UploadRing (time + byte
//                                             budgets) -> flip Resident
//                                           submitDraws(): SSE traversal picks
//                                             the best RESIDENT cut, emits
//                                             DrawItems, collects wants +
//                                             prefetch, REBUILDS the work
//                                             queue by priority (cancelling
//                                             stale requests)
//
// Streaming properties (M2): per-frame re-prioritization by screen-space-
// error contribution; cancellation of no-longer-wanted requests (decoded
// payloads stay in a byte-capped CPU cache — they were paid for); a prefetch
// band one LOD past the cut along the camera velocity; CPU + GPU byte
// budgets with LRU eviction (a node drawn this frame is never evicted — it
// leaves via traversal first); geometry + texture bytes ride the UploadRing
// (no blocking immediateSubmit in the steady state); GPU destruction is
// deferred on the renderer's frame timeline (mesh graveyard here, texture
// graveyard in MaterialSystem). "Never a hole": a node only splits when
// every child is coverable by resident content, so the finest loaded
// ancestor always draws.
//
// Coordinate frames: node data arrives in the layer CRS; GeoAnchor turns it
// into ENU meters at the layer anchor (root OBB center); the app render world
// is Y-up, so ENU (x=east, y=north, z=up) maps to app (x=east, y=up,
// z=-north). Decoded vertices are node-relative in the app frame — the model
// matrix is a pure translation to the node's OBB center.
//
// Threading: load() is pure CPU (worker); startStreaming()/stopStreaming()
// manage the decode pool; pump/submitDraws/pickNodeAt/releaseGpu are
// main-thread only. Destroying the layer joins the workers; the caller must
// ensure the GPU is idle first (remaining GPU residency dies with the layer)
// and call releaseGpu() to hand texture slots back (else they leak).
// ============================================================================

#include "Engine/Data.h" // ComputeBatch (PCSL pool batches, M3)
#include "Loaders/Slpk/GeoAnchor.h"
#include "Loaders/Slpk/I3SAttributes.h"
#include "Loaders/Slpk/I3SGeometry.h"
#include "Loaders/Slpk/I3SPointCloud.h"
#include "Loaders/Slpk/I3STexture.h"
#include "Loaders/Slpk/SlpkTypes.h"
#include "RHI/Texture.h" // PendingUpload holds an rhi::Texture by value
#include "Renderer/MeshBuffer.h"
#include "Renderer/PointCloudGpu.h"

#include <glm/glm.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace renderer {
class OverlayDrawList;
class MaterialSystem;
struct FrameSubmission;
}

namespace rhi {
class Device;
class UploadRing;
}

namespace i3s {
class SlpkArchive;
}

namespace scene {

class I3SSceneLayer {
public:
    I3SSceneLayer();
    ~I3SSceneLayer(); // joins workers; GPU residency dies here (device idle!)
    I3SSceneLayer(const I3SSceneLayer&) = delete;
    I3SSceneLayer& operator=(const I3SSceneLayer&) = delete;

    // Opens the package and builds the full CPU-side model (layer info, node
    // tree, anchor-space boxes). No Vulkan, no GL — worker-thread safe.
    // Returns false with error() set on failure.
    bool load(const std::string& utf8Path);

    const std::string& error() const { return error_; }
    const i3s::SlpkArchive& archive() const { return *archive_; }

    // ---- identity / state ----
    std::string sourcePath;
    std::string name;
    bool visible = true;

    // ---- parsed data (valid after load) ----
    i3s::LayerInfo info;
    i3s::I3SNodeTree tree;
    i3s::GeoAnchor anchor;

    // Per-node OBB in app render space (anchor space, Y-up): rotation columns
    // are the box axes, halfSize the extents along them. Parallel to
    // tree.nodes.
    struct NodeBox {
        glm::vec3 center;
        glm::mat3 axes;
        glm::vec3 halfSize;
        // App-frame position of the node's geometry reference center (1.6 MBS
        // center vs OBB center — decoded vertices are relative to THIS).
        glm::vec3 geomCenter;
    };
    std::vector<NodeBox> nodeBoxes;

    // Union of all node boxes, app anchor space (drives world bounds, framing).
    glm::vec3 boundsMin{ 0.0f };
    glm::vec3 boundsMax{ 0.0f };

    // ---- inspector state (edited by the SLPK panel) ----
    bool showObbs = false;     // draw the BVH through the overlay renderer
    int obbLevel = 1;          // level selected by the panel slider
    bool obbUpToLevel = true;  // true: draw levels 0..obbLevel; false: only obbLevel
    int obbMaxBoxes = 2048;    // cap on boxes per frame (each box = 12 lines
                               // = 72 overlay vertices, uploaded dynamically)

    // ---- inspector v1 (M4) ----
    bool tintByLevel = false;    // color mesh nodes by tree level (DrawItem::tint)
    bool layerWireframe = false; // line-mode debug pipeline (needs GPU support;
                                 // silently falls back to fill when absent)
    bool highlightPicked = true; // warm tint + OBB outline on the picked node
    bool hoverInfo = false;      // panel hover readout drives hoverNode below
    int hoverNode = -1;          // node under the mouse (set by the panel per
                                 // frame; -1 = none)

    // True when appendObbOverlay would emit anything (level display on, or a
    // picked/hovered node outline is due) — the app-side overlay gate.
    bool wantsObbOverlay() const {
        return showObbs || (highlightPicked && pickedNode >= 0) ||
               (hoverInfo && hoverNode >= 0);
    }

    // Appends the OBB wireframes (colored by tree level) to the overlay,
    // plus picked/hovered node outlines (drawn even when showObbs is off).
    void appendObbOverlay(renderer::OverlayDrawList& overlay) const;

    // Boxes currently passing the level filter (for the panel readout).
    size_t countFilteredBoxes() const;

    // ---- mesh streaming (M1 blocking loads -> M2 full pipeline) ------------

    bool showGeometry = true;
    float lodScale = 1.0f;        // quality multiplier on the SSE metric
    bool prefetch = true;         // one LOD past the cut along camera velocity
    int budgetMaxNodes = 4096;    // resident + in-pipeline node cap
    int budgetGpuMB = 2048;       // resident GPU byte cap (exact-tracked),
                                  // additionally ceilinged by the VMA budget
    int budgetCpuMB = 1024;       // decoded-payload cache cap
    int pickedNode = -1;          // picking result surfaced in the panel

    // Per-frame pump budgets, shared across ALL layers (panel-editable;
    // plan §6.5 defaults). Statics: they parameterize the app-level pump.
    static float sPumpBudgetMs;
    static int sPumpStageBudgetMB;

    // True for layer types the mesh path renders (3DObject / IntegratedMesh).
    bool rendersGeometry() const {
        return info.type == i3s::LayerType::Object3D ||
               info.type == i3s::LayerType::IntegratedMesh;
    }
    // True for layer types the point-cloud pool renders (PCSL, M3).
    bool rendersPoints() const {
        return info.type == i3s::LayerType::PointCloud;
    }
    // False for recognized-but-unrenderable types (feature-symbol "Point",
    // BSL "Building"): they open for the inspector only and must not drive
    // camera framing or the scene world bounds.
    bool rendersAnything() const {
        return rendersGeometry() || rendersPoints();
    }

    // ---- point-cloud rendering (M3) ----------------------------------------
    // One PointCloudGpu pool per layer; resident nodes own fixed 2048-point
    // pages (one ComputeBatch each). Colorization is baked CPU-side into the
    // pool's rgba section — mode/palette/bound edits re-bake progressively
    // through the ring (no shader changes; the elevation mode could not be
    // computed in the lookup pass anyway, it never sees positions).

    enum class PointColorMode : int {
        Rgb = 0,
        Intensity = 1,
        Classification = 2,
        Elevation = 3,
    };
    PointColorMode pointColorMode = PointColorMode::Rgb;
    float densityTarget = 0.25f; // traversal target, points per pixel^2
    int budgetPoolPoints = 8000000; // pool capacity; applied on (re)create
    // Ramp bounds, seeded from the layer statistics at load (panel-editable).
    float intensityRampMin = 0.0f;
    float intensityRampMax = 255.0f;
    float elevationRampMin = 0.0f; // app-frame Y (anchor space)
    float elevationRampMax = 1.0f;
    // Classification palette (value-indexed, sRGB); ArcGIS-style defaults.
    glm::vec3 classPalette[256];
    // Which columns the layer declares (panel enables modes accordingly).
    bool hasRgbColumn = false;
    bool hasIntensityColumn = false;
    bool hasClassColumn = false;
    i3s::AttributeStatistics classStats; // labels/values for the palette UI

    // Call after editing pointColorMode / palette / ramp bounds: resident
    // nodes re-bake + re-upload their rgba pages over the next pumps.
    void markPointColorsDirty() { ++recolorEpoch_; }

    static const glm::vec3* defaultClassPalette(); // [256]

    // Spawns the decode workers (call once after load(), main thread). No-op
    // for non-mesh layers or when already running.
    void startStreaming();
    // Stops + joins the workers (destructor calls this).
    void stopStreaming();

    // Main-thread pump (once per frame, before the submission is built):
    // LRU-evicts over budget, retires the mesh graveyard, and turns decoded
    // payloads into GPU residency through the UploadRing. budgetMs /
    // budgetStageBytes are decremented so the caller can share them across
    // layers (byte budget is post-paid — it may go negative by one node,
    // which is what lets a node bigger than the per-frame budget ever load).
    // frameRetireValue/completedFrameValue come from the Renderer's timeline.
    void pump(rhi::Device& device, renderer::MaterialSystem& materials,
              rhi::UploadRing& ring, uint64_t frameRetireValue,
              uint64_t completedFrameValue, double& budgetMs,
              int64_t& budgetStageBytes);

    // Traversal + submission: selects the best resident cut for this frame's
    // camera (submission.views[0]), appends DrawItems, and re-prioritizes the
    // load queue from this frame's wants (cancelling stale requests).
    void submitDraws(renderer::FrameSubmission& submission, uint32_t viewportHeight);

    // Unload path (main thread, before destruction): cancels staged-but-
    // unflushed ring copies and hands every bindless texture slot + material
    // entry back to the MaterialSystem (deferred on retireValue =
    // Renderer::frameRetireValue()). The caller still waitIdle()s before the
    // layer object (and its remaining MeshBuffers) is destroyed.
    void releaseGpu(renderer::MaterialSystem& materials, rhi::UploadRing& ring,
                    uint64_t retireValue);

    // Deepest node drawn last frame whose OBB contains the world point;
    // -1 when none. Main thread.
    int pickNodeAt(const glm::vec3& worldPoint) const;

    // ---- feature picking + attributes (M4, mesh layers) --------------------

    // The feature under the last pick, with its attribute row (name, value).
    struct PickedFeature {
        bool valid = false;      // a pick ran against a decodable node
        uint32_t nodeIndex = 0;
        int featureIndex = -1;   // node feature order; -1 = unresolved
        uint64_t featureId = 0;  // OID from the geometry buffer
        bool hasFeatureId = false;
        float distance = 0.0f;   // picked point -> closest triangle (m)
        std::vector<std::pair<std::string, std::string>> attributes;
        std::string warning;     // why the feature/attributes are partial
    };
    PickedFeature pickedFeature;

    // Resolves the feature under the picked world point within the given
    // node and reads its attribute row. Synchronous (re-decodes the node's
    // geometry — click-rate only; the last node's decode + columns are
    // cached). Main thread. Fills pickedFeature; false when the node cannot
    // be decoded at all.
    bool pickFeatureAt(const glm::vec3& worldPoint, int nodeIndex);

    // ---- daylight (M4; the panel drives the app sun from this) -------------
    struct Daylight {
        bool driveSun = false;   // while on, the panel updates the sun each frame
        int year = 2026;
        int month = 6;
        int day = 21;
        float localHour = 12.0f;      // site-local clock time [0,24)
        float utcOffsetHours = 0.0f;  // site timezone (seeded from longitude)
    };
    Daylight daylight;

    // Maps an ENU direction (x=east, y=north, z=up) into the app render
    // frame — the same fixed swizzle the decoders use (east, up, -north).
    static glm::vec3 appDirectionFromEnu(const glm::dvec3& enu);

    // Load-path warnings for the app to toast (drained by the caller).
    std::vector<std::string> drainWarnings();

    // ---- streaming statistics (panel HUD; main-thread reads) ----
    struct Stats {
        uint32_t queued = 0;     // waiting for a worker
        uint32_t decoding = 0;   // on a worker right now
        uint32_t ready = 0;      // decoded, in the CPU cache
        uint32_t staging = 0;    // device buffers created, ring copies pending
        uint32_t resident = 0;
        uint32_t failed = 0;
        uint32_t drawnLastFrame = 0;
        uint64_t gpuBytes = 0;      // resident + staging (exact)
        uint64_t cpuCacheBytes = 0; // decoded payloads awaiting upload
        uint32_t evicted = 0;       // cumulative LRU evictions
        uint32_t cancelled = 0;     // cumulative queue cancellations
        uint32_t graveyard = 0;     // meshes awaiting timeline retirement
        float decodeRateMBs = 0.0f; // worker output over the last window
        float uploadRateMBs = 0.0f; // bytes staged to the GPU, same window
        uint64_t deviceUsageMB = 0; // VMA device-local heap usage / budget
        uint64_t deviceBudgetMB = 0;
        // Point-pool residency (PCSL layers only).
        uint64_t poolPointsUsed = 0;     // page-granular commitment
        uint64_t poolPointsCapacity = 0; // pool reservation
        uint64_t residentPoints = 0;     // exact decoded points resident
        uint32_t drawnBatches = 0;       // compacted batch count last frame
        uint32_t recolorPending = 0;     // nodes awaiting a color re-bake
    };
    Stats stats() const;

private:
    // Per-node load state. Queued<->Unloaded flips happen under workMutex_
    // (queue rebuild vs worker pop); the rest advance on the owning thread.
    enum class NodeState : uint8_t {
        Unloaded,  // nothing held
        Queued,    // in workQueue_
        Decoding,  // on a worker
        Ready,     // NodePayload in the CPU cache (readyCache_)
        Staging,   // device buffers created, ring copies not yet complete
        Resident,  // drawable
        Failed,
        NoContent, // group node without geometry
    };

    // One pool page worth of quantized points, produced on the worker. The
    // batch bounds are in anchor space; firstPoint is filled at stage time
    // (page assignment is a main-thread decision).
    struct PointRun {
        Engine::ComputeBatch batch{};
        std::vector<uint32_t> xyz4b;
        std::vector<uint32_t> xyz8b;
        std::vector<uint32_t> xyz12b;
    };

    // Worker output for one PCSL node: quantized position runs + the compact
    // per-point colorization columns (kept CPU-side while resident so a
    // mode/palette change re-bakes rgba without re-decoding, ~8 B/point).
    struct PointPayload {
        std::vector<PointRun> runs;
        std::vector<uint8_t> rgb;        // 3 B/point or empty
        std::vector<uint16_t> intensity; // per point or empty
        std::vector<uint8_t> classCodes; // per point or empty
        std::vector<uint16_t> elevation; // app-frame Y quantized over the node
        float elevMin = 0.0f;            // dequant range (absolute app Y)
        float elevMax = 0.0f;
        uint32_t pointCount = 0;
        size_t cpuBytes() const;
    };

    // What a worker produces for one node (CPU only).
    struct NodePayload {
        uint32_t nodeIndex = 0;
        bool failed = false;
        bool textureUnsupported = false;
        std::string error;
        i3s::GeometryData geometry;
        i3s::TextureData texture;
        std::unique_ptr<PointPayload> points; // PCSL layers only
        size_t cpuBytes() const {
            return geometry.cpuBytes() + texture.cpuBytes() +
                   (points ? points->cpuBytes() : 0);
        }
    };

    // A payload on its way to the GPU (buffers created, ring staging may span
    // frames when the ring is contended).
    struct PendingUpload {
        NodePayload payload;
        renderer::MeshBuffer mesh;
        rhi::Texture texture; // created only when payload.texture.valid()
        bool meshStaged = false;
        bool textureStaged = false;
        uint64_t gpuBytes = 0;
    };

    // Main-thread GPU residency for one node.
    struct NodeResidency {
        renderer::MeshBuffer mesh;
        uint32_t materialIndex = 0;
        uint32_t textureIndex = 0; // renderer::kInvalidTexture when untextured
        bool ownsMaterial = false; // false = shared per-definition entry
        bool twoSided = false;     // material doubleSided -> DrawItem::twoSided
        uint64_t gpuBytes = 0;
        uint32_t indexCount = 0;
    };

    // A mesh whose node was evicted: destroyed once the frame timeline passes
    // retireValue (an in-flight frame may still draw it).
    struct MeshGrave {
        renderer::MeshBuffer mesh;
        uint64_t retireValue = 0;
    };

    // A point node on its way into the pool: pages are allocated up front,
    // runs stage transactionally (resume at runsStaged on a full ring).
    struct PointStaging {
        NodePayload payload;
        std::vector<uint32_t> pages; // one per run
        size_t runsStaged = 0;
        uint32_t bakedEpoch = 0; // recolorEpoch_ the staged rgba was baked at
    };

    // Pool residency of one PCSL node. `batches` are final (firstPoint =
    // page * kPointPageSize); the colorization columns stay for re-bakes.
    struct PointResidency {
        std::vector<uint32_t> pages;
        std::vector<Engine::ComputeBatch> batches;
        std::vector<uint8_t> rgb;
        std::vector<uint16_t> intensity;
        std::vector<uint8_t> classCodes;
        std::vector<uint16_t> elevation;
        float elevMin = 0.0f;
        float elevMax = 0.0f;
        uint32_t pointCount = 0;
        uint32_t bakedEpoch = 0;
    };

    // Pages of an evicted node: reusable once the frame timeline passes
    // retireValue (the last uploaded batch array may still reference them).
    struct PageGrave {
        std::vector<uint32_t> pages;
        uint64_t retireValue = 0;
    };

    // One traversal want: nodes the cut is missing (real) or approaching
    // (prefetch), with the node's projected-metric priority.
    struct Want {
        float priority = 0.0f;
        uint32_t nodeIndex = 0;
        bool isPrefetch = false;
    };

    i3s::NodeFrame frameForNode(const i3s::NodeInfo& node) const;
    NodePayload decodePayload(uint32_t nodeIndex) const;
    void workerLoop();

    // Traversal helpers (main thread).
    float nodeMetric(uint32_t nodeIndex, const glm::vec3& cameraPos,
                     float screenFactor) const;
    bool wantSplit(uint32_t nodeIndex, float metric);
    // Out-of-frustum children count as covered (they contribute no pixels and
    // are never requested — without this the cut could never refine while any
    // sibling is off-screen).
    bool coverable(uint32_t nodeIndex, const glm::vec4 frustum[6]) const;
    bool childrenCoverable(uint32_t nodeIndex, const glm::vec4 frustum[6]) const;
    void traverse(uint32_t nodeIndex, renderer::FrameSubmission& submission,
                  const glm::vec3& cameraPos, const glm::vec3& predictedPos,
                  float screenFactor, const glm::vec4 frustum[6]);
    void emitDraw(uint32_t nodeIndex, renderer::FrameSubmission& submission);
    void want(uint32_t nodeIndex, float priority, bool isPrefetch);
    void rebuildWorkQueue();

    // Pump stages (main thread).
    void drainReadyQueue();
    void evictOverBudget(rhi::Device& device, renderer::MaterialSystem& materials,
                         uint64_t frameRetireValue);
    void retireGraveyard(uint64_t completedFrameValue);
    // False = ring full / budget exhausted (kept in staging_, retried next pump).
    bool stagePendingUpload(PendingUpload& upload, rhi::UploadRing& ring,
                            int64_t& budgetStageBytes);
    void finalizeResidency(PendingUpload& upload, renderer::MaterialSystem& materials);
    void trimCpuCache();
    void surfacePayloadWarnings(const NodePayload& payload);
    void pushWarning(const std::string& warning);

    // Point-pool stages (main thread, M3).
    static constexpr uint32_t kPointPageSize = 2048; // points per page/batch
    uint32_t poolPageCapacity() const {
        return pool_ ? poolCapacityPoints_ / kPointPageSize : 0;
    }
    uint32_t usedPages() const;
    bool pointPoolFull() const;
    void ensurePool(rhi::Device& device, rhi::UploadRing& ring);
    void evictPointsOverBudget(uint64_t frameRetireValue);
    void retirePageGraveyard(uint64_t completedFrameValue);
    // Bakes rgba for `count` points starting at `first` of the node's columns
    // under the current colorization mode into out (resized).
    void bakeRunColors(const PointResidency& node, size_t first, size_t count,
                       std::vector<uint32_t>& out) const;
    // False = ring full (resumes at runsStaged next pump).
    bool stagePointUpload(PointStaging& upload, rhi::UploadRing& ring,
                          int64_t& budgetStageBytes);
    void finalizePointResidency(PointStaging& upload);
    void recolorResidentPoints(rhi::UploadRing& ring, int64_t& budgetStageBytes);
    void pumpPoints(rhi::Device& device, rhi::UploadRing& ring, double& budgetMs,
                    int64_t& budgetStageBytes, uint64_t frameRetireValue,
                    uint64_t completedFrameValue,
                    const std::function<double()>& elapsedMs);
    void submitPointDraws(renderer::FrameSubmission& submission);
    void releasePointPool();

    std::unique_ptr<i3s::SlpkArchive> archive_;
    std::string error_;

    // ---- worker pool ----
    std::vector<std::thread> workers_;
    bool streaming_ = false;

    std::mutex workMutex_; // guards workQueue_ + Queued<->Unloaded flips
    std::condition_variable workCv_;
    std::deque<uint32_t> workQueue_;
    bool stopWorkers_ = false;

    std::mutex readyMutex_; // guards readyQueue_ + warnings_
    std::vector<NodePayload> readyQueue_;
    std::vector<std::string> warnings_;

    // Node state array (atomic: traversal reads while workers advance).
    std::unique_ptr<std::atomic<uint8_t>[]> states_;

    // Main-thread-only per-node data.
    std::unordered_map<uint32_t, NodePayload> readyCache_; // decoded payloads
    std::unordered_map<uint32_t, PendingUpload> staging_;
    std::unordered_map<uint32_t, NodeResidency> residency_;
    std::vector<MeshGrave> graveyard_;

    // ---- point-pool state (PCSL layers, M3; main thread) ----
    std::unique_ptr<renderer::PointCloudGpu> pool_;
    uint32_t poolCapacityPoints_ = 0; // pool_'s created capacity
    std::vector<uint32_t> freePages_; // LIFO free list
    std::unordered_map<uint32_t, PointStaging> pointStaging_;
    std::unordered_map<uint32_t, PointResidency> pointResidency_;
    std::vector<PageGrave> pageGraveyard_;
    uint32_t recolorEpoch_ = 0;
    uint32_t recolorPending_ = 0;
    uint64_t residentPoints_ = 0;
    std::vector<uint32_t> drawnPointNodes_; // filled by emitDraw per frame
    // The compacted batch array as last successfully ring-uploaded; item
    // emission uses its count so a full ring degrades to last frame's set.
    // The batches section is double-sized and ping-ponged: the ring flush
    // has no barrier against the PREVIOUS frame's reads (frames may overlap
    // on the queue), but the half being rewritten was last read two
    // submissions ago — retired by the renderer's frame-slot wait.
    std::vector<Engine::ComputeBatch> uploadedBatches_;
    uint32_t activeBatchHalf_ = 0;
    std::vector<Engine::ComputeBatch> batchScratch_;
    std::vector<uint32_t> rgbaScratch_;
    // Captured each pump so submitDraws can stage the batch array (the pump
    // always runs earlier in the frame).
    rhi::UploadRing* pumpRing_ = nullptr;
    int failedPoolBudget_ = -1; // don't retry a failed allocation every frame
    bool warnedNodeOverPool_ = false;
    bool warnedAttributes_ = false;
    std::vector<Want> wants_;           // collected by traverse, consumed by
                                        // rebuildWorkQueue every frame
    std::vector<uint8_t> splitState_;   // traversal hysteresis
    std::vector<uint32_t> drawnStamp_;  // frame stamp of the last emit
    std::vector<uint32_t> wantStamp_;   // frame stamp of the last want/draw
    std::vector<float> wantPriority_;   // metric at the last want (pump order)
    uint32_t frameStamp_ = 0;
    uint32_t drawnLastFrame_ = 0;
    glm::vec3 prevCameraPos_{ 0.0f };
    bool haveCameraHistory_ = false;

    // Budgets/accounting. Atomics only where workers write.
    uint64_t gpuBytes_ = 0;      // resident + staging, exact
    uint64_t cpuCacheBytes_ = 0; // readyCache_ payload bytes
    std::atomic<uint32_t> queuedCount_{ 0 };   // mirrors workQueue_.size()
    std::atomic<uint32_t> decodingCount_{ 0 };
    uint32_t failedCount_ = 0;
    uint32_t evictedCount_ = 0;
    uint32_t cancelledCount_ = 0;
    bool vramPressure_ = false; // VMA device-local heap near its budget

    // Throughput window for the HUD (rates over ~1/2 second).
    std::atomic<uint64_t> decodedBytesWindow_{ 0 }; // workers add
    uint64_t uploadedBytesWindow_ = 0;              // pump adds
    double rateWindowStart_ = 0.0;                  // steady-clock seconds
    float decodeRateMBs_ = 0.0f;
    float uploadRateMBs_ = 0.0f;
    uint64_t deviceUsageMB_ = 0;
    uint64_t deviceBudgetMB_ = 0;

    // One gpu material per untextured material definition (shared across
    // nodes); textured nodes get their own material entry (per-node texture).
    std::unordered_map<int, uint32_t> sharedMaterialByDef_;

    // Once-only warning latches.
    bool warnedVertexColors_ = false;
    bool warnedUvWrap_ = false;
    bool warnedTexture_ = false;

    // ---- feature-pick caches (M4; last picked node only, main thread) ----
    int pickGeomNode_ = -1;   // node pickGeom_ was decoded for
    i3s::GeometryData pickGeom_;
    int attrNode_ = -1;       // node attrColumns_ were read for
    // Parallel to info.attributeFields; a failed column stays empty.
    std::vector<i3s::AttributeColumn> attrColumns_;
    std::vector<std::string> attrErrors_;
};

} // namespace scene
