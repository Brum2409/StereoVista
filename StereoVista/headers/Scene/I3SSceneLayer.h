#pragma once

// ============================================================================
// scene::I3SSceneLayer — an opened SLPK layer as a scene object.
// ----------------------------------------------------------------------------
// Parallel to scene::Model, NOT inside it: an I3S layer is a paged LOD
// hierarchy, not a mesh list. M0 delivered open + inspect (mmap + JSON + node
// tree + OBB overlay); M1 adds mesh rendering with the §6.1 pipeline shape:
//
//   decode workers (no Vulkan!)            main thread (owns all Vulkan)
//   ---------------------------            -----------------------------------
//   archive read + draco/raw decode        pumpGpuCreates(): drain ready queue,
//    + stb texture decode + geodetic         create MeshBuffer/Texture/material
//    transform -> NodePayload                under a time budget, flip Resident
//    -> ready queue                        submitDraws(): SSE traversal selects
//                                            the best RESIDENT cut, emits
//                                            DrawItems, requests missing nodes
//
// Nodes are requested on demand by the traversal (screen-space error, plan
// §6.2, with split/merge hysteresis) under node-count + GPU-byte budgets; no
// eviction yet (M2). "Never a hole": a node only splits when every child is
// coverable by resident content, so the finest loaded ancestor always draws.
//
// Coordinate frames: node data arrives in the layer CRS; GeoAnchor turns it
// into ENU meters at the layer anchor (root OBB center); the app render world
// is Y-up, so ENU (x=east, y=north, z=up) maps to app (x=east, y=up,
// z=-north). Decoded vertices are node-relative in the app frame — the model
// matrix is a pure translation to the node's OBB center.
//
// Threading: load() is pure CPU (worker); startStreaming()/stopStreaming()
// manage the decode pool; pumpGpuCreates/submitDraws/pickNodeAt are
// main-thread only. Destroying the layer joins the workers; the caller must
// ensure the GPU is idle first (resident MeshBuffers die with the layer).
// ============================================================================

#include "Loaders/Slpk/GeoAnchor.h"
#include "Loaders/Slpk/I3SGeometry.h"
#include "Loaders/Slpk/I3STexture.h"
#include "Loaders/Slpk/SlpkTypes.h"
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

    // ---- M1: mesh rendering ----------------------------------------------

    bool showGeometry = true;
    float lodScale = 1.0f;        // quality multiplier on the SSE metric
    int budgetMaxNodes = 4096;    // resident + in-flight node cap
    int budgetGpuMB = 2048;       // resident GPU byte cap (estimate-tracked)
    int pickedNode = -1;          // picking result surfaced in the panel

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

    // Main-thread pump: turns decoded payloads into GPU residency under a
    // time budget. Call once per frame, before the submission is built.
    void pumpGpuCreates(rhi::Device& device, renderer::MaterialSystem& materials,
                        double budgetMs);

    // Traversal + submission: selects the best resident cut for this frame's
    // camera (submission.views[0]) and appends DrawItems; queues load
    // requests for the nodes the cut is missing.
    void submitDraws(renderer::FrameSubmission& submission, uint32_t viewportHeight);

    // Deepest node drawn last frame whose OBB contains the world point;
    // -1 when none. Main thread.
    int pickNodeAt(const glm::vec3& worldPoint) const;

    // Load-path warnings for the app to toast (drained by the caller).
    std::vector<std::string> drainWarnings();

    // ---- streaming statistics (panel HUD; main-thread reads) ----
    struct Stats {
        uint32_t resident = 0;
        uint32_t decoding = 0;   // queued + on a worker
        uint32_t readyPending = 0;
        uint32_t failed = 0;
        uint32_t drawnLastFrame = 0;
        uint64_t gpuBytes = 0;
        uint64_t cpuPendingBytes = 0;
    };
    Stats stats() const;

private:
    // Per-node load state. Transitions: Unloaded -> Queued (traversal, main)
    // -> Decoding (worker) -> [ready queue] -> Resident/Failed (pump, main).
    enum class NodeState : uint8_t {
        Unloaded, Queued, Decoding, Ready, Resident, Failed, NoContent,
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

    // Main-thread GPU residency for one node.
    struct NodeResidency {
        renderer::MeshBuffer mesh;
        uint32_t materialIndex = 0;
        uint64_t gpuBytes = 0;
        uint32_t indexCount = 0;
    };

    i3s::NodeFrame frameForNode(const i3s::NodeInfo& node) const;
    NodePayload decodePayload(uint32_t nodeIndex) const;
    void workerLoop();

    // Traversal helpers (main thread).
    bool wantSplit(uint32_t nodeIndex, const glm::vec3& cameraPos,
                   float screenFactor);
    bool coverable(uint32_t nodeIndex) const;
    bool childrenCoverable(uint32_t nodeIndex) const;
    void traverse(uint32_t nodeIndex, renderer::FrameSubmission& submission,
                  const glm::vec3& cameraPos, float screenFactor,
                  const glm::vec4 frustum[6]);
    void emitDraw(uint32_t nodeIndex, renderer::FrameSubmission& submission);
    void requestNode(uint32_t nodeIndex);
    void pushWarning(const std::string& warning);

    std::unique_ptr<i3s::SlpkArchive> archive_;
    std::string error_;

    // ---- streaming state ----
    std::vector<std::thread> workers_;
    bool streaming_ = false;

    std::mutex workMutex_; // guards workQueue_ + state transitions
    std::condition_variable workCv_;
    std::deque<uint32_t> workQueue_;
    bool stopWorkers_ = false;

    std::mutex readyMutex_; // guards readyQueue_ + warnings_
    std::vector<NodePayload> readyQueue_;
    std::vector<std::string> warnings_;

    // Node state array (atomic: traversal reads while workers advance).
    std::unique_ptr<std::atomic<uint8_t>[]> states_;

    // Main-thread-only per-node data.
    std::vector<NodePayload> pendingCreates_; // drained under the pump budget
    std::unordered_map<uint32_t, NodeResidency> residency_;
    std::vector<uint8_t> splitState_;   // traversal hysteresis
    std::vector<uint32_t> drawnStamp_;  // frame stamp of the last emit
    uint32_t frameStamp_ = 0;
    uint32_t drawnLastFrame_ = 0;

    // Budgets/accounting (main thread; atomics where the panel reads).
    std::atomic<uint64_t> gpuBytes_{ 0 };
    std::atomic<uint64_t> cpuPendingBytes_{ 0 };
    std::atomic<uint32_t> residentCount_{ 0 };
    std::atomic<uint32_t> inFlightCount_{ 0 };
    std::atomic<uint32_t> failedCount_{ 0 };

    // One gpu material per untextured material definition (shared across
    // nodes); textured nodes get their own material entry (per-node texture).
    std::unordered_map<int, uint32_t> sharedMaterialByDef_;

    // Once-only warning latches.
    bool warnedVertexColors_ = false;
    bool warnedUvWrap_ = false;
    bool warnedTexture_ = false;
};

} // namespace scene
