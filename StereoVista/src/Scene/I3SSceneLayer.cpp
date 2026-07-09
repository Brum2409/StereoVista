#include "Scene/I3SSceneLayer.h"

#include "Core/Profiling.h"
#include "Loaders/Slpk/I3SLayer.h"
#include "Loaders/Slpk/SlpkArchive.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "RHI/UploadRing.h"
#include "Renderer/FrameSubmission.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/MaterialSystem.h"
#include "Renderer/OverlayDrawList.h"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cfloat>
#include <chrono>
#include <cmath>
#include <cstring>
#include <filesystem>

namespace scene {

namespace {

// ENU (x=east, y=north, z=up) -> app render world (Y-up, right-handed:
// x=east, y=up, z=-north). Columns are the images of the ENU basis vectors.
const glm::mat3 kEnuToApp(glm::vec3(1.0f, 0.0f, 0.0f),   // east   -> +X
                          glm::vec3(0.0f, 0.0f, -1.0f),  // north  -> -Z
                          glm::vec3(0.0f, 1.0f, 0.0f));  // up     -> +Y

// Distinct level colors for the inspector OBB display (linear-ish values;
// cycles past 8 levels).
const glm::vec4 kLevelColors[] = {
    { 1.00f, 0.35f, 0.25f, 1.0f }, // 0 red-orange
    { 1.00f, 0.75f, 0.15f, 1.0f }, // 1 amber
    { 0.55f, 0.95f, 0.25f, 1.0f }, // 2 lime
    { 0.20f, 0.90f, 0.75f, 1.0f }, // 3 teal
    { 0.25f, 0.60f, 1.00f, 1.0f }, // 4 azure
    { 0.60f, 0.40f, 1.00f, 1.0f }, // 5 violet
    { 1.00f, 0.40f, 0.85f, 1.0f }, // 6 magenta
    { 0.85f, 0.85f, 0.85f, 1.0f }, // 7 grey
};

// Gribb–Hartmann frustum planes from a [0,1]-depth clip matrix (house
// reverse-Z: both depth-range planes included, direction-agnostic). Planes
// are (n.xyz, d) with inside = dot(n,p)+d >= 0.
void extractFrustum(const glm::mat4& m, glm::vec4 out[6]) {
    const glm::vec4 r0(m[0][0], m[1][0], m[2][0], m[3][0]);
    const glm::vec4 r1(m[0][1], m[1][1], m[2][1], m[3][1]);
    const glm::vec4 r2(m[0][2], m[1][2], m[2][2], m[3][2]);
    const glm::vec4 r3(m[0][3], m[1][3], m[2][3], m[3][3]);
    out[0] = r3 + r0; // left
    out[1] = r3 - r0; // right
    out[2] = r3 + r1; // bottom
    out[3] = r3 - r1; // top
    out[4] = r2;      // z' >= 0 (far under reverse-Z)
    out[5] = r3 - r2; // z' <= w (near under reverse-Z)
    for (int i = 0; i < 6; ++i) {
        const float len = glm::length(glm::vec3(out[i]));
        if (len > 1e-12f)
            out[i] /= len;
    }
}

bool sphereInFrustum(const glm::vec4 planes[6], const glm::vec3& center,
                     float radius) {
    for (int i = 0; i < 6; ++i)
        if (glm::dot(glm::vec3(planes[i]), center) + planes[i].w < -radius)
            return false;
    return true;
}

// Backpressure: the work queue never grows past this many pending requests,
// bounding both worker latency to camera moves and ready-payload memory.
constexpr size_t kMaxQueuedRequests = 96;
// Split/merge hysteresis (plan §6.2): a split node merges back only when the
// metric falls 15% below the split threshold, so LOD boundaries don't flicker.
constexpr float kMergeHysteresis = 1.0f / 1.15f;
// Prefetch band (plan §6.1): children are requested at low priority once the
// (velocity-predicted) metric climbs past this fraction of the split
// threshold — one LOD ring beyond the cut, warmed before it is needed.
constexpr float kPrefetchBand = 0.75f;
// Camera-velocity look-ahead in frames (~0.25 s at 60 fps).
constexpr float kPredictFrames = 15.0f;
// A payload bigger than this fraction of the ring takes the blocking
// immediateSubmit path instead of starving the ring (giant root nodes).
constexpr int kBlockingPathRingDivisor = 2;
// Payloads whose want is older than this many frames stay cache-only.
constexpr uint32_t kWantFreshFrames = 3;
// VMA device-local pressure trigger (plan §6.5 ceiling).
constexpr double kVramPressureFraction = 0.85;

double nowSeconds() {
    return std::chrono::duration<double>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

// Panel-editable per-frame pump budgets, shared across layers (plan §6.5).
float I3SSceneLayer::sPumpBudgetMs = 3.0f;
int I3SSceneLayer::sPumpStageBudgetMB = 32;

I3SSceneLayer::I3SSceneLayer() = default;

I3SSceneLayer::~I3SSceneLayer() {
    stopStreaming();
}

bool I3SSceneLayer::load(const std::string& utf8Path) {
    sourcePath = utf8Path;
    name = std::filesystem::path(utf8Path).stem().string();

    archive_ = std::make_unique<i3s::SlpkArchive>();
    if (!archive_->open(utf8Path)) {
        error_ = archive_->error();
        return false;
    }
    std::string err;
    if (!i3s::I3SLayer::parseLayerInfo(*archive_, info, err)) {
        error_ = err;
        return false;
    }
    if (!info.name.empty())
        name = info.name;
    if (!i3s::I3SLayer::loadNodeTree(*archive_, info, tree, err)) {
        error_ = err;
        return false;
    }
    // 1.6 materials live in the per-node shared resources (no-op for 1.7+).
    if (!i3s::I3SLayer::parse16SharedResource(*archive_, tree, info, err)) {
        error_ = err;
        return false;
    }

    // Anchor at the root OBB center. Geographic CRS goes through the WGS84
    // ENU math; projected CRS is already metric (anchor = plain offset).
    const i3s::NodeInfo& root = tree.nodes[0];
    if (info.sr.isGeographic())
        anchor.setGeodetic(root.obbCenter);
    else
        anchor.setLocalMetric(root.obbCenter);

    // Convert every node OBB into app anchor space ONCE (double until the
    // final cast; values are node-relative-to-anchor => small floats).
    const glm::dmat3 ecefToEnu = anchor.ecefToEnuRotation();
    nodeBoxes.resize(tree.nodes.size());
    glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
    for (size_t i = 0; i < tree.nodes.size(); ++i) {
        const i3s::NodeInfo& n = tree.nodes[i];
        NodeBox& box = nodeBoxes[i];

        const glm::dvec3 enuCenter = anchor.toEnu(n.obbCenter);
        box.center = kEnuToApp * glm::vec3(enuCenter);
        box.geomCenter = kEnuToApp * glm::vec3(anchor.toEnu(n.geomCenter));
        box.halfSize = n.obbHalfSize;

        // OBB quaternion frame: ECEF for geographic layers (Cesium/loaders.gl
        // interpretation), the CRS frame for projected ones. Both end in ENU
        // then swizzle into the app's Y-up world.
        const glm::dmat3 axesSrc = glm::dmat3(glm::mat3_cast(n.obbQuat));
        const glm::dmat3 axesEnu =
            info.sr.isGeographic() ? ecefToEnu * axesSrc : axesSrc;
        box.axes = kEnuToApp * glm::mat3(axesEnu);

        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 sign((corner & 1) ? 1.0f : -1.0f,
                                 (corner & 2) ? 1.0f : -1.0f,
                                 (corner & 4) ? 1.0f : -1.0f);
            const glm::vec3 p = box.center + box.axes * (box.halfSize * sign);
            minB = glm::min(minB, p);
            maxB = glm::max(maxB, p);
        }
    }
    boundsMin = minB;
    boundsMax = maxB;

    // Per-node state arrays (fixed size once the tree is known).
    states_ = std::make_unique<std::atomic<uint8_t>[]>(tree.nodes.size());
    for (size_t i = 0; i < tree.nodes.size(); ++i)
        states_[i].store(static_cast<uint8_t>(tree.nodes[i].mesh.hasGeometry
                                                  ? NodeState::Unloaded
                                                  : NodeState::NoContent),
                         std::memory_order_relaxed);
    splitState_.assign(tree.nodes.size(), 0);
    drawnStamp_.assign(tree.nodes.size(), 0);
    wantStamp_.assign(tree.nodes.size(), 0);
    wantPriority_.assign(tree.nodes.size(), 0.0f);

    // PCSL symbology defaults (M3): column availability from the attribute
    // declarations, ramp bounds from the layer statistics, ArcGIS-style
    // classification palette. All still pure CPU (worker-thread safe).
    std::memcpy(classPalette, defaultClassPalette(), sizeof(classPalette));
    if (rendersPoints()) {
        std::string intensityKey, elevationKey, classKey;
        for (const i3s::AttributeField& f : info.attributeFields) {
            if (f.encoding == "lepcc-rgb" || f.name == "RGB")
                hasRgbColumn = true;
            else if (f.encoding == "lepcc-intensity" || f.name == "INTENSITY") {
                hasIntensityColumn = true;
                intensityKey = f.key;
            } else if (f.encoding == "embedded-elevation" || f.name == "ELEVATION")
                elevationKey = f.key;
            else if (f.name == "CLASS_CODE") {
                hasClassColumn = true;
                classKey = f.key;
            }
        }

        // Elevation bounds: statistics are absolute layer-SR heights; the app
        // frame is anchored at the root OBB center, so subtracting its height
        // maps them onto app Y (exact at the anchor, ramp-grade elsewhere).
        // Fall back to the node-box union when no statistics exist.
        elevationRampMin = boundsMin.y;
        elevationRampMax = boundsMax.y;
        i3s::AttributeStatistics stats;
        std::string statsErr;
        if (!elevationKey.empty() &&
            i3s::I3SLayer::loadStatistics(*archive_, elevationKey, stats,
                                          statsErr)) {
            const float anchorZ = static_cast<float>(root.obbCenter.z);
            elevationRampMin = static_cast<float>(stats.min) - anchorZ;
            elevationRampMax = static_cast<float>(stats.max) - anchorZ;
        }
        intensityRampMin = 0.0f;
        intensityRampMax = 255.0f;
        if (!intensityKey.empty() &&
            i3s::I3SLayer::loadStatistics(*archive_, intensityKey, stats,
                                          statsErr)) {
            intensityRampMin = static_cast<float>(stats.min);
            intensityRampMax = static_cast<float>(stats.max);
        }
        if (!classKey.empty())
            i3s::I3SLayer::loadStatistics(*archive_, classKey, classStats,
                                          statsErr);
        pointColorMode =
            hasRgbColumn ? PointColorMode::Rgb : PointColorMode::Elevation;
    }
    return true;
}

size_t I3SSceneLayer::countFilteredBoxes() const {
    size_t count = 0;
    for (const i3s::NodeInfo& n : tree.nodes) {
        const bool match = obbUpToLevel ? n.level <= obbLevel : n.level == obbLevel;
        if (match)
            ++count;
    }
    return count;
}

void I3SSceneLayer::appendObbOverlay(renderer::OverlayDrawList& overlay) const {
    if (nodeBoxes.empty())
        return;

    // Box edge list: pairs of corner indices (corner bit i = +halfSize axis i).
    static const int kEdges[12][2] = {
        { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, // x edges
        { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, // y edges
        { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 }, // z edges
    };

    int drawn = 0;
    for (size_t i = 0; i < nodeBoxes.size() && drawn < obbMaxBoxes; ++i) {
        const i3s::NodeInfo& n = tree.nodes[i];
        const bool match = obbUpToLevel ? n.level <= obbLevel : n.level == obbLevel;
        if (!match)
            continue;

        const NodeBox& box = nodeBoxes[i];
        glm::vec3 corners[8];
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 sign((corner & 1) ? 1.0f : -1.0f,
                                 (corner & 2) ? 1.0f : -1.0f,
                                 (corner & 4) ? 1.0f : -1.0f);
            corners[corner] = box.center + box.axes * (box.halfSize * sign);
        }
        const glm::vec4 color =
            kLevelColors[n.level % (sizeof(kLevelColors) / sizeof(kLevelColors[0]))];
        for (const auto& e : kEdges)
            overlay.line(corners[e[0]], corners[e[1]], color, 1.5f,
                         renderer::OverlayDepth::Occluded);
        ++drawn;
    }
}

// ---- worker pool ------------------------------------------------------------

void I3SSceneLayer::startStreaming() {
    if (streaming_ || !(rendersGeometry() || rendersPoints()) ||
        tree.nodes.empty() || !archive_)
        return;
    stopWorkers_ = false;
    // plan §5 M2: hw/2 - 1 workers, floor 2. hardware_concurrency() may
    // return 0 — guard the unsigned underflow.
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned count = hw > 6 ? hw / 2 - 1 : 2u;
    workers_.reserve(count);
    for (unsigned i = 0; i < count; ++i)
        workers_.emplace_back([this]() { workerLoop(); });
    streaming_ = true;
}

void I3SSceneLayer::stopStreaming() {
    {
        std::lock_guard<std::mutex> lock(workMutex_);
        stopWorkers_ = true;
    }
    workCv_.notify_all();
    for (std::thread& worker : workers_)
        if (worker.joinable())
            worker.join();
    workers_.clear();
    streaming_ = false;
}

i3s::NodeFrame I3SSceneLayer::frameForNode(const i3s::NodeInfo& node) const {
    i3s::NodeFrame frame;
    frame.normalFrame = info.normalFrame;
    frame.centerSR = node.geomCenter;
    if (anchor.isGeodetic()) {
        frame.geographicDeltas = true;
        frame.centerEcef = i3s::geodeticToEcef(node.geomCenter);
        frame.ecefToApp = glm::dmat3(kEnuToApp) * anchor.ecefToEnuRotation();
        // Node-local ENU meters -> ECEF (basis rows are E/N/U, so transpose)
        // -> anchor ENU -> app. Includes the node->anchor curvature twist.
        frame.metersToApp = glm::mat3(
            frame.ecefToApp * glm::transpose(i3s::enuBasis(node.geomCenter)));
        frame.ecefToAppF = glm::mat3(frame.ecefToApp);
    } else {
        frame.geographicDeltas = false;
        frame.metersToApp = kEnuToApp;
        frame.ecefToApp = glm::dmat3(kEnuToApp);
        frame.ecefToAppF = kEnuToApp;
    }
    return frame;
}

size_t I3SSceneLayer::PointPayload::cpuBytes() const {
    size_t bytes = rgb.size() + intensity.size() * 2 + classCodes.size() +
                   elevation.size() * 2;
    for (const PointRun& run : runs)
        bytes += (run.xyz4b.size() + run.xyz8b.size() + run.xyz12b.size()) * 4 +
                 sizeof(Engine::ComputeBatch);
    return bytes;
}

I3SSceneLayer::NodePayload I3SSceneLayer::decodePayload(uint32_t nodeIndex) const {
    NodePayload payload;
    payload.nodeIndex = nodeIndex;
    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    const i3s::NodeFrame frame = frameForNode(node);

    if (rendersPoints()) {
        // PCSL node: LEPCC decode, then quantize into pool-page runs right
        // here on the worker — the pump only copies bytes.
        i3s::PointCloudData data;
        std::string pointErr;
        {
            SV_ZONE_N("i3s point decode");
            if (!i3s::I3SPointCloud::decodeNode(*archive_, info, node, frame,
                                                data, pointErr)) {
                payload.failed = true;
                payload.error = pointErr;
                return payload;
            }
        }
        payload.error = data.attributeWarning; // non-fatal, surfaced once

        SV_ZONE_N("i3s point quantize");
        auto points = std::make_unique<PointPayload>();
        const uint32_t count = static_cast<uint32_t>(data.pointCount());
        points->pointCount = count;
        points->rgb = std::move(data.rgb);
        points->intensity = std::move(data.intensity);
        points->classCodes = std::move(data.classCodes);

        // The pool's local space is the layer anchor space: absolute point =
        // geometry center + node-relative decode output.
        const glm::vec3 base = nodeBoxes[nodeIndex].geomCenter;
        points->elevMin = base.y + data.localMin.y;
        points->elevMax = base.y + data.localMax.y;
        const float elevSpan =
            std::max(points->elevMax - points->elevMin, 1e-6f);
        points->elevation.resize(count);

        constexpr uint32_t kSteps30Bit = 1u << 30;
        constexpr uint32_t kMask10Bit = 1023u;
        points->runs.resize((count + kPointPageSize - 1) / kPointPageSize);
        for (size_t r = 0; r < points->runs.size(); ++r) {
            PointRun& run = points->runs[r];
            const uint32_t first = static_cast<uint32_t>(r) * kPointPageSize;
            const uint32_t n = std::min(kPointPageSize, count - first);

            glm::vec3 bMin(FLT_MAX), bMax(-FLT_MAX);
            for (uint32_t k = 0; k < n; ++k) {
                const glm::vec3 p = base + data.positions[first + k];
                bMin = glm::min(bMin, p);
                bMax = glm::max(bMax, p);
            }
            const glm::vec3 size = glm::max(bMax - bMin, glm::vec3(1e-6f));
            run.batch = { bMin.x, bMin.y, bMin.z, bMax.x, bMax.y, bMax.z,
                          static_cast<int>(n), 0 };
            run.xyz4b.resize(n);
            run.xyz8b.resize(n);
            run.xyz12b.resize(n);
            for (uint32_t k = 0; k < n; ++k) {
                const glm::vec3 p = base + data.positions[first + k];
                // Same 30-bit three-tier encoding as PointCloudLoader's
                // quantizePoint (the compute rasterizer decodes per batch).
                auto quantize = [&](float v, float lo, float span) -> uint32_t {
                    const float t = glm::clamp((v - lo) / span, 0.0f, 1.0f);
                    return std::min(
                        static_cast<uint32_t>(t * static_cast<float>(kSteps30Bit)),
                        kSteps30Bit - 1u);
                };
                const uint32_t xb = quantize(p.x, bMin.x, size.x);
                const uint32_t yb = quantize(p.y, bMin.y, size.y);
                const uint32_t zb = quantize(p.z, bMin.z, size.z);
                run.xyz4b[k] = ((xb >> 20) & kMask10Bit) |
                               (((yb >> 20) & kMask10Bit) << 10) |
                               (((zb >> 20) & kMask10Bit) << 20);
                run.xyz8b[k] = ((xb >> 10) & kMask10Bit) |
                               (((yb >> 10) & kMask10Bit) << 10) |
                               (((zb >> 10) & kMask10Bit) << 20);
                run.xyz12b[k] = (xb & kMask10Bit) | ((yb & kMask10Bit) << 10) |
                                ((zb & kMask10Bit) << 20);
                const float elev01 =
                    glm::clamp((p.y - points->elevMin) / elevSpan, 0.0f, 1.0f);
                points->elevation[first + k] =
                    static_cast<uint16_t>(elev01 * 65535.0f + 0.5f);
            }
        }
        payload.points = std::move(points);
        return payload;
    }

    std::string err;
    {
        SV_ZONE_N("i3s geometry decode");
        if (!i3s::I3SGeometry::decodeNode(*archive_, info, node, frame,
                                          payload.geometry, err)) {
            payload.failed = true;
            payload.error = err;
            return payload;
        }
    }

    bool unsupported = false;
    {
        SV_ZONE_N("i3s texture decode");
        if (!i3s::I3STexture::loadNodeTexture(*archive_, info, node,
                                              payload.texture, unsupported, err)) {
            // A texture problem degrades to untextured rendering — the node
            // still shows up; the panel + a one-shot toast explain why.
            payload.textureUnsupported = unsupported;
            payload.error = err;
            payload.texture = i3s::TextureData{};
        }
    }
    return payload;
}

void I3SSceneLayer::workerLoop() {
    SV_THREAD_NAME("i3s decode");
    for (;;) {
        uint32_t nodeIndex = 0;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workCv_.wait(lock, [this]() { return stopWorkers_ || !workQueue_.empty(); });
            if (stopWorkers_)
                return;
            nodeIndex = workQueue_.front();
            workQueue_.pop_front();
            queuedCount_.store(static_cast<uint32_t>(workQueue_.size()),
                               std::memory_order_relaxed);
            decodingCount_.fetch_add(1, std::memory_order_relaxed);
            states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Decoding),
                                     std::memory_order_relaxed);
        }

        NodePayload payload = decodePayload(nodeIndex);
        decodedBytesWindow_.fetch_add(payload.cpuBytes(), std::memory_order_relaxed);
        // State flips BEFORE the payload is published: once the pump can see
        // the payload, no late store from this thread can clobber a state the
        // main thread has already advanced past Ready (Staging/Resident).
        states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Ready),
                                 std::memory_order_release);
        {
            std::lock_guard<std::mutex> lock(readyMutex_);
            readyQueue_.push_back(std::move(payload));
        }
        decodingCount_.fetch_sub(1, std::memory_order_relaxed);
    }
}

void I3SSceneLayer::pushWarning(const std::string& warning) {
    std::lock_guard<std::mutex> lock(readyMutex_);
    warnings_.push_back(warning);
}

std::vector<std::string> I3SSceneLayer::drainWarnings() {
    std::lock_guard<std::mutex> lock(readyMutex_);
    std::vector<std::string> out;
    out.swap(warnings_);
    return out;
}

// ---- pump: decoded payloads -> GPU residency (main thread) -------------------

void I3SSceneLayer::surfacePayloadWarnings(const NodePayload& payload) {
    // One-shot data-quality warnings (surfaced as toasts by the app).
    if (payload.points && !payload.error.empty() && !warnedAttributes_) {
        // Non-fatal PCSL column problems (count mismatch, decode failure) —
        // geometry still renders, colorization falls back.
        warnedAttributes_ = true;
        pushWarning(name + ": " + payload.error);
    }
    if (payload.geometry.nonWhiteColors && !warnedVertexColors_) {
        warnedVertexColors_ = true;
        pushWarning(name + ": per-vertex colors present — not rendered yet "
                           "(needs a vertex-stream extension, planned)");
    }
    if (payload.geometry.uvRegionWrapDetected && !warnedUvWrap_) {
        warnedUvWrap_ = true;
        pushWarning(name + ": uv-regions with wrap semantics — clamped "
                           "(atlas wrap needs a shader path, planned)");
    }
    if (payload.textureUnsupported && !warnedTexture_) {
        warnedTexture_ = true;
        pushWarning(name + ": " + payload.error);
    }
}

void I3SSceneLayer::drainReadyQueue() {
    std::vector<NodePayload> drained;
    {
        std::lock_guard<std::mutex> lock(readyMutex_);
        drained.swap(readyQueue_);
    }
    for (NodePayload& payload : drained) {
        const uint32_t nodeIndex = payload.nodeIndex;
        if (payload.failed) {
            states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Failed),
                                     std::memory_order_relaxed);
            ++failedCount_;
            if (failedCount_ <= 3)
                pushWarning(name + ": node " + std::to_string(nodeIndex) +
                            " failed: " + payload.error);
            continue;
        }
        surfacePayloadWarnings(payload);
        cpuCacheBytes_ += payload.cpuBytes();
        readyCache_[nodeIndex] = std::move(payload); // state stays Ready
    }
}

void I3SSceneLayer::retireGraveyard(uint64_t completedFrameValue) {
    size_t kept = 0;
    for (size_t i = 0; i < graveyard_.size(); ++i) {
        if (graveyard_[i].retireValue <= completedFrameValue) {
            graveyard_[i].mesh.destroy();
        } else {
            if (kept != i)
                graveyard_[kept] = std::move(graveyard_[i]);
            ++kept;
        }
    }
    graveyard_.resize(kept);
}

void I3SSceneLayer::evictOverBudget(rhi::Device& device,
                                    renderer::MaterialSystem& materials,
                                    uint64_t frameRetireValue) {
    // The user slider is the primary cap; the VMA device-local budget is the
    // hard ceiling shared with everything else in the app (plan §6.5).
    const rhi::Device::MemoryBudget vram = device.deviceLocalBudget();
    deviceUsageMB_ = vram.usageBytes >> 20;
    deviceBudgetMB_ = vram.budgetBytes >> 20;
    vramPressure_ = vram.budgetBytes > 0 &&
                    double(vram.usageBytes) >
                        double(vram.budgetBytes) * kVramPressureFraction;

    const uint64_t budgetBytes =
        uint64_t(std::max(budgetGpuMB, 64)) * 1024ull * 1024ull;
    const size_t nodeTarget = static_cast<size_t>(std::max(budgetMaxNodes, 1));
    uint64_t byteTarget = budgetBytes;
    if (gpuBytes_ > budgetBytes)
        byteTarget = budgetBytes / 10 * 9; // shed below budget: no thrash at the line
    if (vramPressure_)
        byteTarget = std::min(byteTarget, gpuBytes_ / 10 * 9); // relieve 10%/frame

    if (gpuBytes_ <= byteTarget && residency_.size() <= nodeTarget)
        return;

    // LRU order over evictable nodes. A node drawn THIS frame stamp is part
    // of the current cut and never evicts — it must leave via traversal
    // first (plan §6.5).
    std::vector<std::pair<uint32_t, uint32_t>> candidates; // (drawnStamp, node)
    candidates.reserve(residency_.size());
    for (const auto& entry : residency_)
        if (drawnStamp_[entry.first] != frameStamp_)
            candidates.push_back({ drawnStamp_[entry.first], entry.first });
    std::sort(candidates.begin(), candidates.end());

    for (const auto& candidate : candidates) {
        if (gpuBytes_ <= byteTarget && residency_.size() <= nodeTarget)
            break;
        const uint32_t nodeIndex = candidate.second;
        auto it = residency_.find(nodeIndex);
        NodeResidency& r = it->second;
        graveyard_.push_back({ std::move(r.mesh), frameRetireValue });
        if (r.textureIndex != renderer::kInvalidTexture)
            materials.freeTexture(r.textureIndex, frameRetireValue);
        if (r.ownsMaterial)
            materials.freeMaterial(r.materialIndex);
        gpuBytes_ -= std::min(gpuBytes_, r.gpuBytes);
        residency_.erase(it);
        states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Unloaded),
                                 std::memory_order_relaxed);
        ++evictedCount_;
    }
}

bool I3SSceneLayer::stagePendingUpload(PendingUpload& upload, rhi::UploadRing& ring,
                                       int64_t& budgetStageBytes) {
    // Budget check is post-paid: a single node may overshoot the per-frame
    // byte budget once (classic amortization — otherwise a node bigger than
    // the budget could never load).
    if (!upload.meshStaged) {
        if (budgetStageBytes <= 0)
            return false;
        const i3s::GeometryData& g = upload.payload.geometry;
        if (!upload.mesh.stagePayload(ring, g.vertices.data(), g.vertices.size(),
                                      g.indices.data(), g.indices.size()))
            return false; // ring full — retry next frame
        upload.meshStaged = true;
        const int64_t bytes =
            int64_t(g.vertices.size() * sizeof(renderer::Vertex) +
                    g.indices.size() * sizeof(uint32_t));
        budgetStageBytes -= bytes;
        uploadedBytesWindow_ += uint64_t(bytes);
    }
    if (upload.texture.valid() && !upload.textureStaged) {
        if (budgetStageBytes <= 0)
            return false;
        // All mips in ONE transaction — the flush's whole-image layout
        // transition requires the complete chain (stageImage contract).
        const i3s::TextureData& tex = upload.payload.texture;
        const rhi::UploadRing::Marker marker = ring.mark();
        for (size_t level = 0; level < tex.mips.size(); ++level) {
            const i3s::TextureData::Mip& mip = tex.mips[level];
            if (!ring.stageImage(upload.texture, static_cast<uint32_t>(level),
                                 tex.data.data() + mip.offset, mip.size)) {
                ring.rollback(marker);
                return false;
            }
        }
        upload.textureStaged = true;
        budgetStageBytes -= int64_t(tex.data.size());
        uploadedBytesWindow_ += tex.data.size();
    }
    return upload.meshStaged && (!upload.texture.valid() || upload.textureStaged);
}

void I3SSceneLayer::finalizeResidency(PendingUpload& upload,
                                      renderer::MaterialSystem& materials) {
    const uint32_t nodeIndex = upload.payload.nodeIndex;

    uint32_t textureIndex = renderer::kInvalidTexture;
    if (upload.texture.valid())
        textureIndex = materials.addTexture(std::move(upload.texture));

    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    int defIndex = node.mesh.materialDefinition;
    if (defIndex < 0 && !info.materials.empty())
        defIndex = 0;
    const i3s::MaterialDesc materialDesc =
        (defIndex >= 0 && defIndex < static_cast<int>(info.materials.size()))
            ? info.materials[defIndex]
            : i3s::MaterialDesc{};

    uint32_t materialIndex = 0;
    bool ownsMaterial = false;
    const auto sharedIt = textureIndex == renderer::kInvalidTexture
                              ? sharedMaterialByDef_.find(defIndex)
                              : sharedMaterialByDef_.end();
    if (sharedIt != sharedMaterialByDef_.end()) {
        materialIndex = sharedIt->second;
    } else {
        renderer::gpu::MaterialData material{};
        material.baseColor = materialDesc.baseColor;
        material.metallic = materialDesc.metallicFactor;
        material.roughness = materialDesc.roughnessFactor;
        material.emissive = std::max(
            materialDesc.emissiveFactor.x,
            std::max(materialDesc.emissiveFactor.y, materialDesc.emissiveFactor.z));
        material.normalScale = 1.0f;
        material.albedoTexture = textureIndex;
        material.normalTexture = renderer::kInvalidTexture;
        material.metallicTexture = renderer::kInvalidTexture;
        material.roughnessTexture = renderer::kInvalidTexture;
        material.aoTexture = renderer::kInvalidTexture;
        // House convention (ModelImporter): a texture replaces the flat
        // base color rather than multiplying it.
        if (textureIndex != renderer::kInvalidTexture)
            material.baseColor = glm::vec4(1.0f);
        materialIndex = materials.addMaterial(material);
        if (textureIndex == renderer::kInvalidTexture)
            sharedMaterialByDef_[defIndex] = materialIndex;
        else
            ownsMaterial = true;
    }

    NodeResidency residency;
    residency.mesh = std::move(upload.mesh);
    residency.materialIndex = materialIndex;
    residency.textureIndex = textureIndex;
    residency.ownsMaterial = ownsMaterial;
    residency.gpuBytes = upload.gpuBytes; // accounted into gpuBytes_ at creation
    residency.indexCount = static_cast<uint32_t>(upload.payload.geometry.indices.size());
    residency_[nodeIndex] = std::move(residency);
    states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Resident),
                             std::memory_order_relaxed);
}

void I3SSceneLayer::trimCpuCache() {
    const uint64_t budget = uint64_t(std::max(budgetCpuMB, 64)) * 1024ull * 1024ull;
    if (cpuCacheBytes_ <= budget)
        return;
    // Cancelled-after-decode payloads were paid for and stay cached until the
    // budget forces them out — oldest want first (plan §6.1/§6.5).
    std::vector<std::pair<uint32_t, uint32_t>> order; // (wantStamp, node)
    order.reserve(readyCache_.size());
    for (const auto& entry : readyCache_)
        order.push_back({ wantStamp_[entry.first], entry.first });
    std::sort(order.begin(), order.end());
    for (const auto& victim : order) {
        if (cpuCacheBytes_ <= budget)
            break;
        auto it = readyCache_.find(victim.second);
        if (wantStamp_[victim.second] == frameStamp_)
            continue; // wanted right now — keep even over budget
        cpuCacheBytes_ -= std::min<uint64_t>(cpuCacheBytes_, it->second.cpuBytes());
        readyCache_.erase(it);
        states_[victim.second].store(static_cast<uint8_t>(NodeState::Unloaded),
                                     std::memory_order_relaxed);
    }
}

// ---- point pool (PCSL, M3) ----------------------------------------------------

// ArcGIS-style LAS classification colors (sRGB). Values beyond the table stay
// neutral gray; everything is editable per layer in the Scene Layers panel.
const glm::vec3* I3SSceneLayer::defaultClassPalette() {
    static glm::vec3 palette[256];
    static bool initialized = false;
    if (!initialized) {
        initialized = true;
        for (glm::vec3& c : palette)
            c = glm::vec3(0.59f, 0.59f, 0.59f);
        auto set = [&](int value, int r, int g, int b) {
            palette[value] = glm::vec3(r, g, b) / 255.0f;
        };
        set(0, 180, 180, 180);  // created, never classified
        set(1, 209, 209, 209);  // unassigned
        set(2, 166, 113, 3);    // ground
        set(3, 145, 200, 80);   // low vegetation
        set(4, 92, 179, 55);    // medium vegetation
        set(5, 42, 140, 40);    // high vegetation
        set(6, 230, 90, 60);    // building
        set(7, 255, 60, 220);   // low point (noise)
        set(8, 220, 220, 150);  // model key / reserved
        set(9, 60, 130, 240);   // water
        set(10, 150, 80, 180);  // rail
        set(11, 110, 110, 110); // road surface
        set(12, 240, 220, 90);  // overlap / reserved
        set(13, 250, 190, 60);  // wire guard
        set(14, 255, 230, 80);  // wire conductor
        set(15, 200, 140, 90);  // transmission tower
        set(16, 250, 160, 110); // wire structure connector
        set(17, 140, 100, 70);  // bridge deck
        set(18, 250, 60, 60);   // high noise
    }
    return palette;
}

namespace {

uint32_t packRgba8(const glm::vec3& srgb, uint32_t a8) {
    const uint32_t r8 =
        static_cast<uint32_t>(glm::clamp(srgb.r, 0.0f, 1.0f) * 255.0f + 0.5f);
    const uint32_t g8 =
        static_cast<uint32_t>(glm::clamp(srgb.g, 0.0f, 1.0f) * 255.0f + 0.5f);
    const uint32_t b8 =
        static_cast<uint32_t>(glm::clamp(srgb.b, 0.0f, 1.0f) * 255.0f + 0.5f);
    return (a8 << 24) | (b8 << 16) | (g8 << 8) | r8;
}

// Violet -> cyan -> green -> yellow -> red, the shape of ArcGIS's default
// elevation stretch (SMALL_AUTZEN's own drawingInfo carries these hues).
glm::vec3 elevationRamp(float t) {
    static const glm::vec3 stops[5] = {
        { 0.27f, 0.06f, 0.93f }, { 0.09f, 0.82f, 0.95f }, { 0.45f, 0.90f, 0.42f },
        { 0.95f, 0.96f, 0.20f }, { 1.00f, 0.29f, 0.13f },
    };
    t = glm::clamp(t, 0.0f, 1.0f) * 4.0f;
    const int seg = std::min(static_cast<int>(t), 3);
    return glm::mix(stops[seg], stops[seg + 1], t - float(seg));
}

} // namespace

uint32_t I3SSceneLayer::usedPages() const {
    uint32_t graveyardPages = 0;
    for (const PageGrave& grave : pageGraveyard_)
        graveyardPages += static_cast<uint32_t>(grave.pages.size());
    return poolPageCapacity() -
           static_cast<uint32_t>(freePages_.size()) - graveyardPages;
}

bool I3SSceneLayer::pointPoolFull() const {
    // Full for queueing purposes when free pages (graveyard excluded — those
    // return within a frame or two) drop under 5% of the pool.
    return pool_ && freePages_.size() < size_t(poolPageCapacity()) / 20 + 1;
}

void I3SSceneLayer::ensurePool(rhi::Device& device, rhi::UploadRing& ring) {
    if (budgetPoolPoints == failedPoolBudget_)
        return; // allocation failed at this size; wait for a budget change
    const uint32_t wanted = static_cast<uint32_t>(
        glm::clamp(budgetPoolPoints, 1 << 20, 256 << 20));
    if (pool_ && poolCapacityPoints_ == (wanted / kPointPageSize) * kPointPageSize)
        return;
    // (Re)create: a budget change drops all residency — the traversal
    // re-streams the cut. destroy() waits for the device (user action, rare).
    releasePointPool();
    const uint32_t capacity = (wanted / kPointPageSize) * kPointPageSize;
    auto pool = std::make_unique<renderer::PointCloudGpu>();
    // 2x batch capacity: the compacted batch array ping-pongs between two
    // halves so a rewrite never races the previous frame's reads (see the
    // header note at uploadedBatches_).
    if (!pool->create(device, &ring, capacity, 2 * (capacity / kPointPageSize),
                      name + " (i3s pool)")) {
        failedPoolBudget_ = budgetPoolPoints;
        pushWarning(name + ": point pool allocation failed (" +
                    std::to_string(capacity / 1000000) + "M points) — lower "
                    "the pool budget");
        return;
    }
    failedPoolBudget_ = -1;
    pool_ = std::move(pool);
    poolCapacityPoints_ = capacity;
    freePages_.resize(capacity / kPointPageSize);
    for (uint32_t i = 0; i < freePages_.size(); ++i)
        freePages_[i] = static_cast<uint32_t>(freePages_.size()) - 1 - i;
}

void I3SSceneLayer::releasePointPool() {
    for (auto& entry : pointStaging_) {
        states_[entry.first].store(static_cast<uint8_t>(NodeState::Unloaded),
                                   std::memory_order_relaxed);
    }
    for (auto& entry : pointResidency_) {
        states_[entry.first].store(static_cast<uint8_t>(NodeState::Unloaded),
                                   std::memory_order_relaxed);
    }
    pointStaging_.clear();
    pointResidency_.clear();
    pageGraveyard_.clear();
    freePages_.clear();
    uploadedBatches_.clear();
    drawnPointNodes_.clear();
    activeBatchHalf_ = 0;
    residentPoints_ = 0;
    poolCapacityPoints_ = 0;
    pool_.reset(); // destroy() drops its queued ring copies + waits
}

void I3SSceneLayer::retirePageGraveyard(uint64_t completedFrameValue) {
    size_t kept = 0;
    for (size_t i = 0; i < pageGraveyard_.size(); ++i) {
        if (pageGraveyard_[i].retireValue <= completedFrameValue) {
            freePages_.insert(freePages_.end(), pageGraveyard_[i].pages.begin(),
                              pageGraveyard_[i].pages.end());
        } else {
            if (kept != i)
                pageGraveyard_[kept] = std::move(pageGraveyard_[i]);
            ++kept;
        }
    }
    pageGraveyard_.resize(kept);
}

void I3SSceneLayer::evictPointsOverBudget(uint64_t frameRetireValue) {
    // Keep >= 10% of the pool free (page-granular LRU; a node drawn last
    // frame is part of the current cut and never evicts).
    const uint32_t capacity = poolPageCapacity();
    if (capacity == 0)
        return;
    const uint32_t targetUsed = capacity - capacity / 10;
    if (usedPages() <= targetUsed)
        return;

    std::vector<std::pair<uint32_t, uint32_t>> candidates; // (drawnStamp, node)
    candidates.reserve(pointResidency_.size());
    for (const auto& entry : pointResidency_)
        if (drawnStamp_[entry.first] != frameStamp_)
            candidates.push_back({ drawnStamp_[entry.first], entry.first });
    std::sort(candidates.begin(), candidates.end());

    for (const auto& candidate : candidates) {
        if (usedPages() <= targetUsed)
            break;
        auto it = pointResidency_.find(candidate.second);
        pageGraveyard_.push_back({ std::move(it->second.pages),
                                   frameRetireValue });
        residentPoints_ -= std::min<uint64_t>(residentPoints_,
                                              it->second.pointCount);
        pointResidency_.erase(it);
        states_[candidate.second].store(static_cast<uint8_t>(NodeState::Unloaded),
                                        std::memory_order_relaxed);
        ++evictedCount_;
    }
}

void I3SSceneLayer::bakeRunColors(const PointResidency& node, size_t first,
                                  size_t count, std::vector<uint32_t>& out) const {
    out.resize(count);
    const float intensitySpan =
        std::max(intensityRampMax - intensityRampMin, 1e-6f);
    const float elevSpan = std::max(node.elevMax - node.elevMin, 1e-6f);
    const float rampSpan =
        std::max(elevationRampMax - elevationRampMin, 1e-6f);
    for (size_t k = 0; k < count; ++k) {
        const size_t i = first + k;
        // Alpha carries normalized intensity, like the LAS loaders.
        uint32_t a8 = 255;
        float intensity01 = 0.0f;
        if (i < node.intensity.size()) {
            intensity01 = glm::clamp(
                (float(node.intensity[i]) - intensityRampMin) / intensitySpan,
                0.0f, 1.0f);
            a8 = static_cast<uint32_t>(intensity01 * 255.0f + 0.5f);
        }
        switch (pointColorMode) {
        case PointColorMode::Rgb:
            if (i * 3 + 2 < node.rgb.size()) {
                out[k] = (a8 << 24) | (uint32_t(node.rgb[i * 3 + 2]) << 16) |
                         (uint32_t(node.rgb[i * 3 + 1]) << 8) |
                         uint32_t(node.rgb[i * 3 + 0]);
            } else {
                out[k] = (a8 << 24) | 0x00ffffffu;
            }
            break;
        case PointColorMode::Intensity:
            out[k] = packRgba8(glm::vec3(intensity01), a8);
            break;
        case PointColorMode::Classification:
            out[k] = packRgba8(
                classPalette[i < node.classCodes.size() ? node.classCodes[i] : 0],
                a8);
            break;
        case PointColorMode::Elevation:
        default: {
            const float y = node.elevMin +
                            (float(node.elevation[i]) / 65535.0f) * elevSpan;
            out[k] = packRgba8(
                elevationRamp((y - elevationRampMin) / rampSpan), a8);
            break;
        }
        }
    }
}

bool I3SSceneLayer::stagePointUpload(PointStaging& upload, rhi::UploadRing& ring,
                                     int64_t& budgetStageBytes) {
    PointPayload& points = *upload.payload.points;
    // Bake against a residency view so staging and re-bakes share one path.
    PointResidency view;
    view.rgb = std::move(points.rgb);
    view.intensity = std::move(points.intensity);
    view.classCodes = std::move(points.classCodes);
    view.elevation = std::move(points.elevation);
    view.elevMin = points.elevMin;
    view.elevMax = points.elevMax;

    bool ok = true;
    while (upload.runsStaged < points.runs.size()) {
        if (budgetStageBytes <= 0 && upload.runsStaged > 0) {
            ok = false; // post-paid: at least one run lands per pump
            break;
        }
        const size_t r = upload.runsStaged;
        const PointRun& run = points.runs[r];
        const uint32_t page = upload.pages[r];
        const VkDeviceSize pointOffset =
            VkDeviceSize(page) * kPointPageSize * sizeof(uint32_t);
        const VkDeviceSize bytes = VkDeviceSize(run.xyz4b.size()) * sizeof(uint32_t);

        if (upload.runsStaged == 0)
            upload.bakedEpoch = recolorEpoch_;
        bakeRunColors(view, r * size_t(kPointPageSize), run.xyz4b.size(),
                      rgbaScratch_);

        const rhi::UploadRing::Marker marker = ring.mark();
        if (!ring.stage(pool_->storage, pool_->xyz4bOffset + pointOffset,
                        run.xyz4b.data(), bytes) ||
            !ring.stage(pool_->storage, pool_->xyz8bOffset + pointOffset,
                        run.xyz8b.data(), bytes) ||
            !ring.stage(pool_->storage, pool_->xyz12bOffset + pointOffset,
                        run.xyz12b.data(), bytes) ||
            !ring.stage(pool_->storage, pool_->rgbaOffset + pointOffset,
                        rgbaScratch_.data(), bytes)) {
            ring.rollback(marker); // full — resume at this run next pump
            ok = false;
            break;
        }
        budgetStageBytes -= int64_t(bytes) * 4;
        uploadedBytesWindow_ += uint64_t(bytes) * 4;
        ++upload.runsStaged;
    }

    // Hand the columns back for the next attempt / residency.
    points.rgb = std::move(view.rgb);
    points.intensity = std::move(view.intensity);
    points.classCodes = std::move(view.classCodes);
    points.elevation = std::move(view.elevation);
    return ok && upload.runsStaged == points.runs.size();
}

void I3SSceneLayer::finalizePointResidency(PointStaging& upload) {
    const uint32_t nodeIndex = upload.payload.nodeIndex;
    PointPayload& points = *upload.payload.points;

    PointResidency residency;
    residency.pages = std::move(upload.pages);
    residency.batches.resize(points.runs.size());
    for (size_t r = 0; r < points.runs.size(); ++r) {
        residency.batches[r] = points.runs[r].batch;
        residency.batches[r].firstPoint =
            static_cast<int>(residency.pages[r] * kPointPageSize);
    }
    residency.rgb = std::move(points.rgb);
    residency.intensity = std::move(points.intensity);
    residency.classCodes = std::move(points.classCodes);
    residency.elevation = std::move(points.elevation);
    residency.elevMin = points.elevMin;
    residency.elevMax = points.elevMax;
    residency.pointCount = points.pointCount;
    residency.bakedEpoch = upload.bakedEpoch;
    residentPoints_ += points.pointCount;
    pointResidency_[nodeIndex] = std::move(residency);
    states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Resident),
                             std::memory_order_relaxed);
}

void I3SSceneLayer::recolorResidentPoints(rhi::UploadRing& ring,
                                          int64_t& budgetStageBytes) {
    recolorPending_ = 0;
    if (!pool_)
        return;
    SV_ZONE_N("i3s point recolor");
    for (auto& entry : pointResidency_) {
        PointResidency& node = entry.second;
        if (node.bakedEpoch == recolorEpoch_)
            continue;
        if (budgetStageBytes <= 0) {
            ++recolorPending_;
            continue; // keep counting for the HUD
        }
        // All pages of a node re-bake in one transaction (a split node would
        // otherwise flicker two palettes); retried next pump on a full ring.
        // Known benign race: the previous frame may still read the old rgba
        // dwords while this frame's copy lands (no cross-frame barrier) —
        // worst case is one frame of mixed colors during a recolor. Indices
        // and positions are never rewritten in place, only colors.
        const rhi::UploadRing::Marker marker = ring.mark();
        bool staged = true;
        int64_t bytes = 0;
        for (size_t r = 0; r < node.batches.size(); ++r) {
            const size_t count = size_t(node.batches[r].numPoints);
            bakeRunColors(node, r * size_t(kPointPageSize), count, rgbaScratch_);
            const VkDeviceSize pointOffset =
                VkDeviceSize(node.pages[r]) * kPointPageSize * sizeof(uint32_t);
            if (!ring.stage(pool_->storage, pool_->rgbaOffset + pointOffset,
                            rgbaScratch_.data(), count * sizeof(uint32_t))) {
                staged = false;
                break;
            }
            bytes += int64_t(count * sizeof(uint32_t));
        }
        if (!staged) {
            ring.rollback(marker);
            ++recolorPending_;
            continue;
        }
        budgetStageBytes -= bytes;
        uploadedBytesWindow_ += uint64_t(bytes);
        node.bakedEpoch = recolorEpoch_;
    }
}

void I3SSceneLayer::pumpPoints(rhi::Device& device, rhi::UploadRing& ring,
                               double& budgetMs, int64_t& budgetStageBytes,
                               uint64_t frameRetireValue,
                               uint64_t completedFrameValue,
                               const std::function<double()>& elapsedMs) {
    SV_ZONE_N("i3s point pump");
    ensurePool(device, ring);
    if (!pool_)
        return;
    const rhi::Device::MemoryBudget vram = device.deviceLocalBudget();
    deviceUsageMB_ = vram.usageBytes >> 20;
    deviceBudgetMB_ = vram.budgetBytes >> 20;
    retirePageGraveyard(completedFrameValue);
    evictPointsOverBudget(frameRetireValue);

    // In-flight staging first (pages already committed).
    for (auto it = pointStaging_.begin(); it != pointStaging_.end();) {
        if (budgetMs - elapsedMs() <= 0.0 || budgetStageBytes <= 0)
            break;
        if (stagePointUpload(it->second, ring, budgetStageBytes)) {
            finalizePointResidency(it->second);
            it = pointStaging_.erase(it);
        } else {
            ++it;
        }
    }

    // New uploads from the CPU cache, freshest + biggest-on-screen first.
    std::vector<std::pair<float, uint32_t>> candidates; // (-priority, node)
    for (const auto& entry : readyCache_) {
        const uint32_t nodeIndex = entry.first;
        if (frameStamp_ - wantStamp_[nodeIndex] > kWantFreshFrames)
            continue;
        candidates.push_back({ -wantPriority_[nodeIndex], nodeIndex });
    }
    std::sort(candidates.begin(), candidates.end());

    for (const auto& candidate : candidates) {
        if (budgetMs - elapsedMs() <= 0.0 || budgetStageBytes <= 0)
            break;
        const uint32_t nodeIndex = candidate.second;
        auto cacheIt = readyCache_.find(nodeIndex);
        if (!cacheIt->second.points || cacheIt->second.points->runs.empty()) {
            states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Failed),
                                     std::memory_order_relaxed);
            ++failedCount_;
            cpuCacheBytes_ -=
                std::min<uint64_t>(cpuCacheBytes_, cacheIt->second.cpuBytes());
            readyCache_.erase(cacheIt);
            continue;
        }
        const size_t pagesNeeded = cacheIt->second.points->runs.size();
        if (pagesNeeded > size_t(poolPageCapacity())) {
            if (!warnedNodeOverPool_) {
                warnedNodeOverPool_ = true;
                pushWarning(name + ": a node exceeds the whole point pool — "
                            "raise the pool budget");
            }
            states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Failed),
                                     std::memory_order_relaxed);
            ++failedCount_;
            cpuCacheBytes_ -=
                std::min<uint64_t>(cpuCacheBytes_, cacheIt->second.cpuBytes());
            readyCache_.erase(cacheIt);
            continue;
        }
        if (pagesNeeded > freePages_.size())
            break; // pool saturated; eviction/graveyard frees pages later

        PointStaging upload;
        upload.payload = std::move(cacheIt->second);
        cpuCacheBytes_ -=
            std::min<uint64_t>(cpuCacheBytes_, upload.payload.cpuBytes());
        readyCache_.erase(cacheIt);

        upload.pages.resize(pagesNeeded);
        for (size_t p = 0; p < pagesNeeded; ++p) {
            upload.pages[p] = freePages_.back();
            freePages_.pop_back();
        }
        states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Staging),
                                 std::memory_order_relaxed);
        if (stagePointUpload(upload, ring, budgetStageBytes)) {
            finalizePointResidency(upload);
        } else {
            pointStaging_[nodeIndex] = std::move(upload);
        }
    }

    recolorResidentPoints(ring, budgetStageBytes);
    SV_PLOT("i3s poolPages", double(usedPages()));
}

void I3SSceneLayer::pump(rhi::Device& device, renderer::MaterialSystem& materials,
                         rhi::UploadRing& ring, uint64_t frameRetireValue,
                         uint64_t completedFrameValue, double& budgetMs,
                         int64_t& budgetStageBytes) {
    if (!streaming_)
        return;
    SV_ZONE_N("i3s pump");
    pumpRing_ = &ring;
    const auto start = std::chrono::steady_clock::now();
    auto elapsedMs = [&]() {
        return std::chrono::duration<double, std::milli>(
                   std::chrono::steady_clock::now() - start)
            .count();
    };

    if (rendersPoints()) {
        drainReadyQueue();
        pumpPoints(device, ring, budgetMs, budgetStageBytes, frameRetireValue,
                   completedFrameValue, elapsedMs);
        trimCpuCache();

        const double now = nowSeconds();
        if (rateWindowStart_ == 0.0)
            rateWindowStart_ = now;
        const double window = now - rateWindowStart_;
        if (window >= 0.5) {
            const double mb = 1024.0 * 1024.0;
            decodeRateMBs_ = float(double(decodedBytesWindow_.exchange(
                                       0, std::memory_order_relaxed)) /
                                   window / mb);
            uploadRateMBs_ = float(double(uploadedBytesWindow_) / window / mb);
            uploadedBytesWindow_ = 0;
            rateWindowStart_ = now;
        }
        budgetMs -= elapsedMs();
        return;
    }

    drainReadyQueue();
    evictOverBudget(device, materials, frameRetireValue);
    retireGraveyard(completedFrameValue);

    // In-flight staging first — those nodes already hold device memory.
    for (auto it = staging_.begin(); it != staging_.end();) {
        if (budgetMs - elapsedMs() <= 0.0 || budgetStageBytes <= 0)
            break;
        if (stagePendingUpload(it->second, ring, budgetStageBytes)) {
            finalizeResidency(it->second, materials);
            it = staging_.erase(it);
        } else {
            ++it;
        }
    }

    // New uploads from the CPU cache: freshest + biggest-on-screen first.
    const uint64_t budgetBytes =
        uint64_t(std::max(budgetGpuMB, 64)) * 1024ull * 1024ull;
    std::vector<std::pair<float, uint32_t>> candidates; // (-priority, node)
    for (const auto& entry : readyCache_) {
        const uint32_t nodeIndex = entry.first;
        if (frameStamp_ - wantStamp_[nodeIndex] > kWantFreshFrames)
            continue; // cache-only until wanted again
        candidates.push_back({ -wantPriority_[nodeIndex], nodeIndex });
    }
    std::sort(candidates.begin(), candidates.end());

    for (const auto& candidate : candidates) {
        if (budgetMs - elapsedMs() <= 0.0 || budgetStageBytes <= 0)
            break;
        if (gpuBytes_ >= budgetBytes || vramPressure_)
            break;
        if (residency_.size() + staging_.size() >=
            static_cast<size_t>(std::max(budgetMaxNodes, 1)))
            break;
        if (!materials.textureSlotAvailable())
            break;

        const uint32_t nodeIndex = candidate.second;
        auto cacheIt = readyCache_.find(nodeIndex);
        PendingUpload upload;
        upload.payload = std::move(cacheIt->second);
        cpuCacheBytes_ -= std::min<uint64_t>(cpuCacheBytes_, upload.payload.cpuBytes());
        readyCache_.erase(cacheIt);

        const i3s::GeometryData& g = upload.payload.geometry;
        const i3s::TextureData& tex = upload.payload.texture;
        const uint64_t meshBytes = uint64_t(g.vertices.size()) * sizeof(renderer::Vertex) +
                                   uint64_t(g.indices.size()) * sizeof(uint32_t);
        upload.gpuBytes = meshBytes + tex.data.size();

        if (g.vertices.empty() || g.indices.empty()) {
            states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Failed),
                                     std::memory_order_relaxed);
            ++failedCount_;
            continue;
        }

        gpuBytes_ += upload.gpuBytes;

        rhi::Texture texture;
        if (tex.valid()) {
            rhi::TextureDesc desc{};
            desc.format = tex.format == i3s::TextureData::Format::Bc7
                              ? VK_FORMAT_BC7_SRGB_BLOCK
                              : VK_FORMAT_R8G8B8A8_SRGB;
            desc.extent = { uint32_t(tex.width), uint32_t(tex.height) };
            desc.mipLevels = static_cast<uint32_t>(tex.mips.size());
            desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
            desc.debugName = "i3s texture";
            texture.create(device, desc);
        }

        if (upload.payload.cpuBytes() >
            size_t(ring.capacity() / kBlockingPathRingDivisor)) {
            // Giant node (rare — oversized roots): the ring would starve, so
            // take the blocking loading-time path instead (plan §6.3 keeps it).
            renderer::MeshData meshData;
            meshData.vertices = std::move(upload.payload.geometry.vertices);
            meshData.indices = upload.payload.geometry.indices; // keep for count
            upload.mesh.create(device, meshData, "i3s node");
            if (tex.valid()) {
                std::vector<rhi::Texture::MipData> mips(tex.mips.size());
                for (size_t level = 0; level < tex.mips.size(); ++level)
                    mips[level] = { tex.data.data() + tex.mips[level].offset,
                                    tex.mips[level].size };
                texture.uploadMips(mips.data(), static_cast<uint32_t>(mips.size()));
            }
            upload.texture = std::move(texture);
            upload.meshStaged = true;
            upload.textureStaged = true;
            uploadedBytesWindow_ += upload.gpuBytes;
            finalizeResidency(upload, materials);
            continue;
        }

        upload.mesh.createForStreaming(device,
                                       static_cast<uint32_t>(g.vertices.size()),
                                       static_cast<uint32_t>(g.indices.size()),
                                       "i3s node");
        upload.texture = std::move(texture);
        states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Staging),
                                 std::memory_order_relaxed);
        if (stagePendingUpload(upload, ring, budgetStageBytes)) {
            finalizeResidency(upload, materials);
        } else {
            staging_[nodeIndex] = std::move(upload); // retry next pump
        }
    }

    trimCpuCache();

    // Throughput window for the HUD.
    const double now = nowSeconds();
    if (rateWindowStart_ == 0.0)
        rateWindowStart_ = now;
    const double window = now - rateWindowStart_;
    if (window >= 0.5) {
        const double mb = 1024.0 * 1024.0;
        decodeRateMBs_ = float(double(decodedBytesWindow_.exchange(
                                   0, std::memory_order_relaxed)) /
                               window / mb);
        uploadRateMBs_ = float(double(uploadedBytesWindow_) / window / mb);
        uploadedBytesWindow_ = 0;
        rateWindowStart_ = now;
    }
    SV_PLOT("i3s gpuMB", double(gpuBytes_) / (1024.0 * 1024.0));

    budgetMs -= elapsedMs();
}

void I3SSceneLayer::releaseGpu(renderer::MaterialSystem& materials,
                               rhi::UploadRing& ring, uint64_t retireValue) {
    // Point layers: the pool owns every GPU byte; its destroy() drops queued
    // ring copies and waits for the device itself.
    releasePointPool();

    // Cancel staged-but-unflushed ring copies before their destinations die
    // with this layer (the caller waitIdle()s for the flushed ones).
    for (auto& entry : staging_) {
        entry.second.mesh.dropPendingCopies(ring);
        if (entry.second.texture.valid())
            ring.dropPendingFor(entry.second.texture);
    }
    staging_.clear();

    // Hand every bindless slot + material entry back (deferred destroy).
    for (auto& entry : residency_) {
        NodeResidency& r = entry.second;
        if (r.textureIndex != renderer::kInvalidTexture)
            materials.freeTexture(r.textureIndex, retireValue);
        if (r.ownsMaterial)
            materials.freeMaterial(r.materialIndex);
        r.textureIndex = renderer::kInvalidTexture;
        r.ownsMaterial = false;
    }
    for (const auto& shared : sharedMaterialByDef_)
        materials.freeMaterial(shared.second);
    sharedMaterialByDef_.clear();
}

// ---- traversal + submission --------------------------------------------------

bool I3SSceneLayer::coverable(uint32_t nodeIndex, const glm::vec4 frustum[6]) const {
    const NodeState state =
        static_cast<NodeState>(states_[nodeIndex].load(std::memory_order_relaxed));
    if (state == NodeState::Resident)
        return true;
    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    if (node.mesh.hasGeometry)
        return false; // content exists but is not on the GPU yet
    // Group node: coverable when every child subtree is (vacuously true for
    // an empty group — there is nothing to show there at all).
    return childrenCoverable(nodeIndex, frustum);
}

bool I3SSceneLayer::childrenCoverable(uint32_t nodeIndex,
                                      const glm::vec4 frustum[6]) const {
    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    for (uint32_t c = 0; c < node.childCount; ++c) {
        const uint32_t child = tree.childIndices[node.firstChild + c];
        // An out-of-frustum child is vacuously covered: it contributes no
        // pixels, is never requested (the want loop skips it), and would
        // otherwise stall the split forever — the cut could never refine
        // while ANY sibling is off-screen. It loads when it turns into view
        // (the parent covers the transient gap per the traversal rules).
        const NodeBox& box = nodeBoxes[child];
        if (!sphereInFrustum(frustum, box.center, glm::length(box.halfSize) * 1.15f))
            continue;
        if (!coverable(child, frustum))
            return false;
    }
    return true;
}

float I3SSceneLayer::nodeMetric(uint32_t nodeIndex, const glm::vec3& cameraPos,
                                float screenFactor) const {
    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    const NodeBox& box = nodeBoxes[nodeIndex];
    const float radius = glm::length(box.halfSize);
    const float dist = glm::length(cameraPos - box.center);
    if (dist <= radius)
        return FLT_MAX; // camera inside the node: always refine

    // Projected bounding-sphere size (the spec's lodSelection metrics).
    const float diameterPx = (2.0f * radius * screenFactor) / dist;

    float metric = 0.0f;
    const float threshold = static_cast<float>(node.lodThreshold);
    switch (info.lodMetric) {
    case i3s::LodMetric::MaxScreenThresholdSQ:
        metric = 0.25f * 3.14159265f * diameterPx * diameterPx; // pi/4 * d^2
        break;
    case i3s::LodMetric::DistanceRangeFromDefaultCamera:
        // maxError is a distance: refine when the camera is closer than it.
        // Expressed as metric > threshold  <=>  dist < threshold.
        metric = threshold * threshold / std::max(dist, 1e-3f);
        break;
    case i3s::LodMetric::DensityThreshold: {
        // PCSL: lodThreshold is the node's effective 2D area (world units^2).
        // Screen density Ds = pointCount / (area * (px/m)^2); split when Ds
        // falls under the density target. Normalized so metric > 1 = split
        // (wantSplit compares against 1 for this metric).
        const float pxPerMeter = screenFactor / dist;
        const float areaPx = threshold * pxPerMeter * pxPerMeter;
        const float pointCount = static_cast<float>(node.mesh.vertexCount);
        if (threshold <= 0.0f || pointCount <= 0.0f) {
            // Degenerate node page entries: refine once the box dominates
            // the view (~512 px projected diameter).
            metric = diameterPx / 512.0f;
        } else {
            metric = std::max(densityTarget, 1e-3f) * areaPx / pointCount;
        }
        break;
    }
    case i3s::LodMetric::MaxScreenThreshold:
    default:
        metric = diameterPx;
        break;
    }
    return metric * std::max(lodScale, 0.01f);
}

bool I3SSceneLayer::wantSplit(uint32_t nodeIndex, float metric) {
    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    if (node.childCount == 0)
        return false;
    // The density metric is self-normalized (nodeMetric folds the node's
    // threshold in); every other metric compares against the node threshold.
    const bool densityMetric = info.lodMetric == i3s::LodMetric::DensityThreshold;
    if (!densityMetric && node.lodThreshold <= 0.0)
        return true; // no threshold = always refine (typical group root)

    // Hysteresis: once split, merge back only when the metric drops 15% under
    // the split threshold.
    const float threshold =
        densityMetric ? 1.0f : static_cast<float>(node.lodThreshold);
    const bool wasSplit = splitState_[nodeIndex] != 0;
    const float effective = wasSplit ? threshold * kMergeHysteresis : threshold;
    const bool split = metric > effective;
    splitState_[nodeIndex] = split ? 1 : 0;
    return split;
}

void I3SSceneLayer::want(uint32_t nodeIndex, float priority, bool isPrefetch) {
    const NodeState state =
        static_cast<NodeState>(states_[nodeIndex].load(std::memory_order_relaxed));
    if (state == NodeState::Failed || state == NodeState::NoContent)
        return;
    if (wantStamp_[nodeIndex] == frameStamp_)
        priority = std::max(priority, wantPriority_[nodeIndex]);
    wantStamp_[nodeIndex] = frameStamp_;
    wantPriority_[nodeIndex] = priority;
    // Only Unloaded/Queued nodes need queue placement; Ready/Staging advance
    // through the pump, which reads the stamps refreshed above.
    if (state == NodeState::Unloaded || state == NodeState::Queued)
        wants_.push_back({ priority, nodeIndex, isPrefetch });
}

void I3SSceneLayer::rebuildWorkQueue() {
    SV_ZONE_N("i3s queue rebuild");
    // Real wants before prefetch, then biggest screen contribution first
    // (plan §6.1: re-prioritized every frame).
    std::sort(wants_.begin(), wants_.end(), [](const Want& a, const Want& b) {
        if (a.isPrefetch != b.isPrefetch)
            return !a.isPrefetch;
        return a.priority > b.priority;
    });

    const uint64_t budgetBytes =
        uint64_t(std::max(budgetGpuMB, 64)) * 1024ull * 1024ull;
    const bool gpuFull = rendersPoints()
                             ? pointPoolFull()
                             : (gpuBytes_ >= budgetBytes || vramPressure_);
    const uint32_t nodeBudget = static_cast<uint32_t>(std::max(budgetMaxNodes, 1));

    std::lock_guard<std::mutex> lock(workMutex_);

    // Cancel-then-requeue: everything still Queued falls back to Unloaded;
    // wants re-add what this frame's cut still needs, in priority order.
    std::vector<uint32_t> previous(workQueue_.begin(), workQueue_.end());
    workQueue_.clear();
    for (uint32_t nodeIndex : previous)
        if (static_cast<NodeState>(states_[nodeIndex].load(
                std::memory_order_relaxed)) == NodeState::Queued)
            states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Unloaded),
                                     std::memory_order_relaxed);

    if (!gpuFull) {
        // Everything already holding a node slot (the queue is empty here).
        const uint32_t pipeline =
            static_cast<uint32_t>(residency_.size() + staging_.size() +
                                  pointResidency_.size() + pointStaging_.size() +
                                  readyCache_.size()) +
            decodingCount_.load(std::memory_order_relaxed);
        const size_t capacityLeft =
            pipeline < nodeBudget ? size_t(nodeBudget - pipeline) : 0;
        const size_t cap = std::min(kMaxQueuedRequests, capacityLeft);
        for (const Want& w : wants_) {
            if (workQueue_.size() >= cap)
                break;
            // Prefetch only fills the queue's back half — it must never
            // crowd out a hole the cut actually has.
            if (w.isPrefetch && workQueue_.size() >= cap / 2)
                break;
            if (static_cast<NodeState>(states_[w.nodeIndex].load(
                    std::memory_order_relaxed)) != NodeState::Unloaded)
                continue;
            states_[w.nodeIndex].store(static_cast<uint8_t>(NodeState::Queued),
                                       std::memory_order_relaxed);
            workQueue_.push_back(w.nodeIndex);
        }
    }

    // Anything from the old queue that did not re-enter was cancelled.
    for (uint32_t nodeIndex : previous)
        if (static_cast<NodeState>(states_[nodeIndex].load(
                std::memory_order_relaxed)) == NodeState::Unloaded)
            ++cancelledCount_;

    queuedCount_.store(static_cast<uint32_t>(workQueue_.size()),
                       std::memory_order_relaxed);
    wants_.clear();
    if (!workQueue_.empty())
        workCv_.notify_all();
}

void I3SSceneLayer::emitDraw(uint32_t nodeIndex,
                             renderer::FrameSubmission& submission) {
    if (rendersPoints()) {
        // Point nodes collect into this frame's drawn set; submitPointDraws
        // compacts them into the pool's batch array after the traversal.
        if (pointResidency_.find(nodeIndex) == pointResidency_.end())
            return;
        drawnPointNodes_.push_back(nodeIndex);
        drawnStamp_[nodeIndex] = frameStamp_;
        wantStamp_[nodeIndex] = frameStamp_;
        ++drawnLastFrame_;
        return;
    }

    const auto it = residency_.find(nodeIndex);
    if (it == residency_.end() || !it->second.mesh.valid())
        return;
    const NodeBox& box = nodeBoxes[nodeIndex];

    renderer::DrawItem draw;
    draw.mesh = &it->second.mesh;
    // Decoded vertices are relative to the node's geometry center in the app
    // frame: translation only, identity normal matrix.
    draw.model = glm::translate(glm::mat4(1.0f), box.geomCenter);
    draw.normalMatrix = glm::mat3(1.0f);
    draw.materialIndex = it->second.materialIndex;
    draw.castsShadows = true;
    draw.worldBoundsCenter = box.center;
    draw.worldBoundsRadius = glm::length(box.halfSize);
    submission.draws.push_back(draw);

    drawnStamp_[nodeIndex] = frameStamp_;
    wantStamp_[nodeIndex] = frameStamp_; // drawn = wanted (LRU freshness)
    ++drawnLastFrame_;
}

void I3SSceneLayer::traverse(uint32_t nodeIndex,
                             renderer::FrameSubmission& submission,
                             const glm::vec3& cameraPos,
                             const glm::vec3& predictedPos, float screenFactor,
                             const glm::vec4 frustum[6]) {
    const NodeBox& box = nodeBoxes[nodeIndex];
    // 1.15 margin keeps the right stereo eye covered by the view-0 frustum.
    if (!sphereInFrustum(frustum, box.center, glm::length(box.halfSize) * 1.15f))
        return;

    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    const NodeState state =
        static_cast<NodeState>(states_[nodeIndex].load(std::memory_order_relaxed));
    const float metric = nodeMetric(nodeIndex, cameraPos, screenFactor);

    if (wantSplit(nodeIndex, metric)) {
        if (childrenCoverable(nodeIndex, frustum)) {
            for (uint32_t c = 0; c < node.childCount; ++c)
                traverse(tree.childIndices[node.firstChild + c], submission,
                         cameraPos, predictedPos, screenFactor, frustum);
            return;
        }
        // Children incomplete: want the missing ones (only those inside the
        // frustum) and keep showing this node meanwhile — never a hole.
        for (uint32_t c = 0; c < node.childCount; ++c) {
            const uint32_t child = tree.childIndices[node.firstChild + c];
            const NodeBox& childBox = nodeBoxes[child];
            if (!sphereInFrustum(frustum, childBox.center,
                                 glm::length(childBox.halfSize) * 1.15f))
                continue;
            if (tree.nodes[child].mesh.hasGeometry)
                want(child, nodeMetric(child, cameraPos, screenFactor), false);
        }
        if (state == NodeState::Resident) {
            emitDraw(nodeIndex, submission);
        } else {
            // Nothing of our own to show: draw whatever coverable child
            // subtrees exist so partial content appears ASAP.
            for (uint32_t c = 0; c < node.childCount; ++c) {
                const uint32_t child = tree.childIndices[node.firstChild + c];
                if (coverable(child, frustum))
                    traverse(child, submission, cameraPos, predictedPos,
                             screenFactor, frustum);
            }
            if (node.mesh.hasGeometry)
                want(nodeIndex, metric, false);
        }
        return;
    }

    // This node IS the right LOD for this camera.
    if (state == NodeState::Resident) {
        emitDraw(nodeIndex, submission);
        // Prefetch ring (plan §6.1): when the velocity-predicted camera is
        // approaching this node's split point, warm its children at low
        // priority — they arrive before the split instead of after.
        if (prefetch && node.childCount > 0 && node.lodThreshold > 0.0) {
            const float splitThreshold =
                info.lodMetric == i3s::LodMetric::DensityThreshold
                    ? 1.0f
                    : float(node.lodThreshold);
            const float predicted =
                nodeMetric(nodeIndex, predictedPos, screenFactor);
            if (predicted > splitThreshold * kPrefetchBand) {
                for (uint32_t c = 0; c < node.childCount; ++c) {
                    const uint32_t child = tree.childIndices[node.firstChild + c];
                    if (tree.nodes[child].mesh.hasGeometry)
                        want(child, predicted, true);
                }
            }
        }
    } else if (node.mesh.hasGeometry) {
        want(nodeIndex, metric, false);
    }
}

void I3SSceneLayer::submitDraws(renderer::FrameSubmission& submission,
                                uint32_t viewportHeight) {
    drawnLastFrame_ = 0;
    drawnPointNodes_.clear();
    if (!visible || !showGeometry || !streaming_ || tree.nodes.empty())
        return;
    SV_ZONE_N("i3s traversal");
    ++frameStamp_;

    const renderer::ViewCamera& camera = submission.views[0];
    // proj[1][1] = 1/tan(fovy/2) (sign carries the house Y-flip).
    const float proj11 = std::fabs(camera.proj[1][1]);
    const float screenFactor = 0.5f * float(viewportHeight) * proj11;

    glm::vec4 frustum[6];
    extractFrustum(camera.proj * camera.view, frustum);

    // Velocity-predicted camera for the prefetch band (~0.25 s ahead).
    glm::vec3 predicted = camera.position;
    if (haveCameraHistory_)
        predicted += (camera.position - prevCameraPos_) * kPredictFrames;
    prevCameraPos_ = camera.position;
    haveCameraHistory_ = true;

    traverse(0, submission, camera.position, predicted, screenFactor, frustum);
    rebuildWorkQueue();
    if (rendersPoints())
        submitPointDraws(submission);
}

void I3SSceneLayer::submitPointDraws(renderer::FrameSubmission& submission) {
    if (!pool_ || !pool_->valid())
        return;
    SV_ZONE_N("i3s point batches");

    // Compact this frame's drawn set into one batch array. Content usually
    // repeats frame to frame — only a changed set touches the ring (32 B per
    // batch; a few thousand batches = tens of KB).
    batchScratch_.clear();
    for (uint32_t nodeIndex : drawnPointNodes_) {
        const auto it = pointResidency_.find(nodeIndex);
        if (it == pointResidency_.end())
            continue;
        batchScratch_.insert(batchScratch_.end(), it->second.batches.begin(),
                             it->second.batches.end());
    }

    const bool changed =
        batchScratch_.size() != uploadedBatches_.size() ||
        (!batchScratch_.empty() &&
         std::memcmp(batchScratch_.data(), uploadedBatches_.data(),
                     batchScratch_.size() * sizeof(Engine::ComputeBatch)) != 0);
    if (changed) {
        // Write the INACTIVE half (ping-pong): the previous frame may still
        // be reading the active one — the inactive half was last read two
        // submissions ago, which the renderer's frame-slot wait has retired.
        const uint32_t half = activeBatchHalf_ ^ 1u;
        const VkDeviceSize halfBytes = VkDeviceSize(poolPageCapacity()) *
                                       renderer::PointCloudGpu::kBatchStride;
        if (batchScratch_.empty()) {
            uploadedBatches_.clear();
        } else if (pumpRing_ &&
                   batchScratch_.size() <= size_t(poolPageCapacity()) &&
                   pumpRing_->stage(
                       pool_->storage, pool_->batchesOffset + half * halfBytes,
                       batchScratch_.data(),
                       batchScratch_.size() * sizeof(Engine::ComputeBatch))) {
            uploadedBatches_ = batchScratch_;
            activeBatchHalf_ = half;
        }
        // else: ring full — keep drawing last frame's set (its pages are
        // intact; evicted pages retire on the frame timeline).
    }
    if (uploadedBatches_.empty())
        return;

    renderer::PointCloudDrawItem item;
    item.addresses = pool_->addresses;
    item.addresses.batches +=
        VkDeviceSize(activeBatchHalf_) * poolPageCapacity() *
        renderer::PointCloudGpu::kBatchStride;
    item.numBatches = static_cast<uint32_t>(uploadedBatches_.size());
    item.pointsPerThread =
        static_cast<int>((kPointPageSize + SV_PC_RASTER_WORKGROUP - 1) /
                         SV_PC_RASTER_WORKGROUP);
    item.model = glm::mat4(1.0f); // pool batches live in layer anchor space
    submission.pointClouds.push_back(item);
}

int I3SSceneLayer::pickNodeAt(const glm::vec3& worldPoint) const {
    int best = -1;
    int bestLevel = -1;
    auto consider = [&](uint32_t nodeIndex) {
        if (drawnStamp_[nodeIndex] != frameStamp_)
            return; // only nodes that are part of the current cut
        const NodeBox& box = nodeBoxes[nodeIndex];
        const glm::vec3 local = glm::transpose(box.axes) * (worldPoint - box.center);
        const glm::vec3 extent = box.halfSize + glm::vec3(0.05f); // pick slack
        if (std::fabs(local.x) > extent.x || std::fabs(local.y) > extent.y ||
            std::fabs(local.z) > extent.z)
            return;
        const int level = tree.nodes[nodeIndex].level;
        if (level > bestLevel) {
            bestLevel = level;
            best = static_cast<int>(nodeIndex);
        }
    };
    for (const auto& entry : residency_)
        consider(entry.first);
    for (const auto& entry : pointResidency_)
        consider(entry.first);
    return best;
}

I3SSceneLayer::Stats I3SSceneLayer::stats() const {
    Stats stats;
    stats.queued = queuedCount_.load(std::memory_order_relaxed);
    stats.decoding = decodingCount_.load(std::memory_order_relaxed);
    stats.ready = static_cast<uint32_t>(readyCache_.size());
    stats.staging = static_cast<uint32_t>(staging_.size() + pointStaging_.size());
    stats.resident =
        static_cast<uint32_t>(residency_.size() + pointResidency_.size());
    stats.poolPointsUsed = uint64_t(usedPages()) * kPointPageSize;
    stats.poolPointsCapacity = poolCapacityPoints_;
    stats.residentPoints = residentPoints_;
    stats.drawnBatches = static_cast<uint32_t>(uploadedBatches_.size());
    stats.recolorPending = recolorPending_;
    stats.failed = failedCount_;
    stats.drawnLastFrame = drawnLastFrame_;
    stats.gpuBytes = gpuBytes_;
    stats.cpuCacheBytes = cpuCacheBytes_;
    stats.evicted = evictedCount_;
    stats.cancelled = cancelledCount_;
    stats.graveyard = static_cast<uint32_t>(graveyard_.size());
    stats.decodeRateMBs = decodeRateMBs_;
    stats.uploadRateMBs = uploadRateMBs_;
    stats.deviceUsageMB = deviceUsageMB_;
    stats.deviceBudgetMB = deviceBudgetMB_;
    return stats;
}

} // namespace scene
