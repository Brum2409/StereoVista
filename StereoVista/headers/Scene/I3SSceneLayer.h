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

#include "Loaders/Slpk/GeoAnchor.h"
#include "Loaders/Slpk/I3SGeometry.h"
#include "Loaders/Slpk/I3STexture.h"
#include "Loaders/Slpk/SlpkTypes.h"
#include "RHI/Texture.h" // PendingUpload holds an rhi::Texture by value
#include "Renderer/MeshBuffer.h"

#include <glm/glm.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
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

    // Appends the OBB wireframes (colored by tree level) to the overlay.
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

    // What a worker produces for one node (CPU only).
    struct NodePayload {
        uint32_t nodeIndex = 0;
        bool failed = false;
        bool textureUnsupported = false;
        std::string error;
        i3s::GeometryData geometry;
        i3s::TextureData texture;
        size_t cpuBytes() const { return geometry.cpuBytes() + texture.cpuBytes(); }
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
        uint64_t gpuBytes = 0;
        uint32_t indexCount = 0;
    };

    // A mesh whose node was evicted: destroyed once the frame timeline passes
    // retireValue (an in-flight frame may still draw it).
    struct MeshGrave {
        renderer::MeshBuffer mesh;
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
};

} // namespace scene
