#include "Scene/I3SSceneLayer.h"

#include "Loaders/Slpk/I3SLayer.h"
#include "Loaders/Slpk/SlpkArchive.h"
#include "RHI/Device.h"
#include "RHI/Texture.h"
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

constexpr double kPumpDefaultBudgetMs = 4.0;
// Backpressure: the work queue never grows past this many pending requests,
// bounding both worker latency to camera moves and ready-payload memory.
constexpr size_t kMaxQueuedRequests = 96;
// Split/merge hysteresis (plan §6.2): a split node merges back only when the
// metric falls 15% below the split threshold, so LOD boundaries don't flicker.
constexpr float kMergeHysteresis = 1.0f / 1.15f;

} // namespace

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

    // M1 per-node state arrays (fixed size once the tree is known).
    states_ = std::make_unique<std::atomic<uint8_t>[]>(tree.nodes.size());
    for (size_t i = 0; i < tree.nodes.size(); ++i)
        states_[i].store(static_cast<uint8_t>(tree.nodes[i].mesh.hasGeometry
                                                  ? NodeState::Unloaded
                                                  : NodeState::NoContent),
                         std::memory_order_relaxed);
    splitState_.assign(tree.nodes.size(), 0);
    drawnStamp_.assign(tree.nodes.size(), 0);
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

// ---- M1: streaming ------------------------------------------------------------

void I3SSceneLayer::startStreaming() {
    if (streaming_ || !rendersGeometry() || tree.nodes.empty() || !archive_)
        return;
    stopWorkers_ = false;
    const unsigned hw = std::thread::hardware_concurrency();
    const unsigned count = std::max(1u, std::min(4u, hw / 2));
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

I3SSceneLayer::NodePayload I3SSceneLayer::decodePayload(uint32_t nodeIndex) const {
    NodePayload payload;
    payload.nodeIndex = nodeIndex;
    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    const i3s::NodeFrame frame = frameForNode(node);

    std::string err;
    if (!i3s::I3SGeometry::decodeNode(*archive_, info, node, frame,
                                      payload.geometry, err)) {
        payload.failed = true;
        payload.error = err;
        return payload;
    }

    bool unsupported = false;
    if (!i3s::I3STexture::loadNodeTexture(*archive_, info, node, payload.texture,
                                          unsupported, err)) {
        // A texture problem degrades to untextured rendering — the node still
        // shows up; the panel + a one-shot toast explain why it looks flat.
        payload.textureUnsupported = unsupported;
        payload.error = err;
        payload.texture = i3s::TextureData{};
    }
    return payload;
}

void I3SSceneLayer::workerLoop() {
    for (;;) {
        uint32_t nodeIndex = 0;
        {
            std::unique_lock<std::mutex> lock(workMutex_);
            workCv_.wait(lock, [this]() { return stopWorkers_ || !workQueue_.empty(); });
            if (stopWorkers_)
                return;
            nodeIndex = workQueue_.front();
            workQueue_.pop_front();
            states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Decoding),
                                     std::memory_order_relaxed);
        }

        NodePayload payload = decodePayload(nodeIndex);
        const size_t bytes = payload.cpuBytes();
        {
            std::lock_guard<std::mutex> lock(readyMutex_);
            readyQueue_.push_back(std::move(payload));
        }
        cpuPendingBytes_.fetch_add(bytes, std::memory_order_relaxed);
        states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Ready),
                                 std::memory_order_release);
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

void I3SSceneLayer::pumpGpuCreates(rhi::Device& device,
                                   renderer::MaterialSystem& materials,
                                   double budgetMs) {
    if (!streaming_)
        return;
    {
        std::lock_guard<std::mutex> lock(readyMutex_);
        for (NodePayload& payload : readyQueue_)
            pendingCreates_.push_back(std::move(payload));
        readyQueue_.clear();
    }

    if (budgetMs <= 0.0)
        budgetMs = kPumpDefaultBudgetMs;
    const auto start = std::chrono::steady_clock::now();
    auto withinBudget = [&]() {
        const double elapsed =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() -
                                                      start)
                .count();
        return elapsed < budgetMs;
    };

    size_t processed = 0;
    while (processed < pendingCreates_.size() && withinBudget()) {
        NodePayload& payload = pendingCreates_[processed++];
        const uint32_t nodeIndex = payload.nodeIndex;
        cpuPendingBytes_.fetch_sub(payload.cpuBytes(), std::memory_order_relaxed);
        inFlightCount_.fetch_sub(1, std::memory_order_relaxed);

        if (payload.failed) {
            states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Failed),
                                     std::memory_order_relaxed);
            failedCount_.fetch_add(1, std::memory_order_relaxed);
            if (failedCount_.load(std::memory_order_relaxed) <= 3)
                pushWarning(name + ": node " + std::to_string(nodeIndex) +
                            " failed: " + payload.error);
            continue;
        }

        // One-shot data-quality warnings (surfaced as toasts by the app).
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

        // GPU material: texture (if any) + material entry. Untextured nodes
        // share one entry per material definition.
        uint32_t textureIndex = renderer::kInvalidTexture;
        uint64_t textureBytes = 0;
        if (payload.texture.valid() &&
            materials.textureCount() + 1 < renderer::MaterialSystem::kTextureCapacity) {
            rhi::TextureDesc desc{};
            desc.format = VK_FORMAT_R8G8B8A8_SRGB; // base color: hardware sRGB decode
            desc.extent = { uint32_t(payload.texture.width),
                            uint32_t(payload.texture.height) };
            desc.mipLevels = rhi::computeMipCount(desc.extent.width, desc.extent.height);
            desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
            desc.debugName = "i3s texture";
            rhi::Texture texture;
            texture.create(device, desc);
            texture.upload(payload.texture.rgba.data(), payload.texture.rgba.size());
            textureIndex = materials.addTexture(std::move(texture));
            // Mip chain ~ 4/3 of level 0.
            textureBytes = uint64_t(payload.texture.rgba.size()) * 4 / 3;
        }

        const i3s::NodeInfo& node = tree.nodes[nodeIndex];
        int defIndex = node.mesh.materialDefinition;
        if (defIndex < 0 && !info.materials.empty())
            defIndex = 0;
        const i3s::MaterialDesc materialDesc =
            (defIndex >= 0 && defIndex < static_cast<int>(info.materials.size()))
                ? info.materials[defIndex]
                : i3s::MaterialDesc{};

        uint32_t materialIndex = 0;
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
        }

        NodeResidency residency;
        renderer::MeshData meshData;
        meshData.vertices = std::move(payload.geometry.vertices);
        meshData.indices = std::move(payload.geometry.indices);
        residency.indexCount = static_cast<uint32_t>(meshData.indices.size());
        residency.materialIndex = materialIndex;
        residency.gpuBytes = uint64_t(meshData.vertices.size()) * sizeof(renderer::Vertex) +
                             uint64_t(meshData.indices.size()) * sizeof(uint32_t) +
                             textureBytes;
        residency.mesh.create(device, meshData, "i3s node");

        gpuBytes_.fetch_add(residency.gpuBytes, std::memory_order_relaxed);
        residency_[nodeIndex] = std::move(residency);
        residentCount_.fetch_add(1, std::memory_order_relaxed);
        states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Resident),
                                 std::memory_order_relaxed);
    }
    pendingCreates_.erase(pendingCreates_.begin(),
                          pendingCreates_.begin() + processed);
}

// ---- M1: traversal + submission --------------------------------------------------

bool I3SSceneLayer::coverable(uint32_t nodeIndex) const {
    const NodeState state =
        static_cast<NodeState>(states_[nodeIndex].load(std::memory_order_relaxed));
    if (state == NodeState::Resident)
        return true;
    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    if (node.mesh.hasGeometry)
        return false; // content exists but is not on the GPU yet
    // Group node: coverable when every child subtree is (vacuously true for
    // an empty group — there is nothing to show there at all).
    return childrenCoverable(nodeIndex);
}

bool I3SSceneLayer::childrenCoverable(uint32_t nodeIndex) const {
    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    for (uint32_t c = 0; c < node.childCount; ++c)
        if (!coverable(tree.childIndices[node.firstChild + c]))
            return false;
    return true;
}

bool I3SSceneLayer::wantSplit(uint32_t nodeIndex, const glm::vec3& cameraPos,
                              float screenFactor) {
    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    if (node.childCount == 0)
        return false;
    if (node.lodThreshold <= 0.0)
        return true; // no threshold = always refine (typical group root)

    const NodeBox& box = nodeBoxes[nodeIndex];
    const float radius = glm::length(box.halfSize);
    const float dist = glm::length(cameraPos - box.center);
    if (dist <= radius)
        return true; // camera inside the node: always refine

    // Projected bounding-sphere size (the spec's lodSelection metrics).
    const float diameterPx = (2.0f * radius * screenFactor) / dist;

    float metric = 0.0f;
    float threshold = static_cast<float>(node.lodThreshold);
    switch (info.lodMetric) {
    case i3s::LodMetric::MaxScreenThresholdSQ:
        metric = 0.25f * 3.14159265f * diameterPx * diameterPx; // pi/4 * d^2
        break;
    case i3s::LodMetric::DistanceRangeFromDefaultCamera:
        // maxError is a distance: refine when the camera is closer than it.
        // Expressed as metric > threshold  <=>  dist < threshold.
        metric = threshold * threshold / std::max(dist, 1e-3f);
        break;
    case i3s::LodMetric::MaxScreenThreshold:
    default:
        metric = diameterPx;
        break;
    }
    metric *= std::max(lodScale, 0.01f);

    // Hysteresis: once split, merge back only when the metric drops 15% under
    // the split threshold.
    const bool wasSplit = splitState_[nodeIndex] != 0;
    const float effective = wasSplit ? threshold * kMergeHysteresis : threshold;
    const bool split = metric > effective;
    splitState_[nodeIndex] = split ? 1 : 0;
    return split;
}

void I3SSceneLayer::requestNode(uint32_t nodeIndex) {
    // Budgets: node count counts resident + everything still in flight;
    // GPU bytes gate on what is actually resident (no eviction yet — once
    // the budget is hit the cut simply stops refining).
    const uint32_t inFlight = inFlightCount_.load(std::memory_order_relaxed);
    const uint32_t resident = residentCount_.load(std::memory_order_relaxed);
    if (resident + inFlight >= static_cast<uint32_t>(std::max(budgetMaxNodes, 1)))
        return;
    if (gpuBytes_.load(std::memory_order_relaxed) >=
        uint64_t(std::max(budgetGpuMB, 64)) * 1024ull * 1024ull)
        return;

    std::lock_guard<std::mutex> lock(workMutex_);
    if (workQueue_.size() >= kMaxQueuedRequests)
        return;
    const NodeState state =
        static_cast<NodeState>(states_[nodeIndex].load(std::memory_order_relaxed));
    if (state != NodeState::Unloaded)
        return;
    states_[nodeIndex].store(static_cast<uint8_t>(NodeState::Queued),
                             std::memory_order_relaxed);
    workQueue_.push_back(nodeIndex);
    inFlightCount_.fetch_add(1, std::memory_order_relaxed);
    workCv_.notify_one();
}

void I3SSceneLayer::emitDraw(uint32_t nodeIndex,
                             renderer::FrameSubmission& submission) {
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
    ++drawnLastFrame_;
}

void I3SSceneLayer::traverse(uint32_t nodeIndex,
                             renderer::FrameSubmission& submission,
                             const glm::vec3& cameraPos, float screenFactor,
                             const glm::vec4 frustum[6]) {
    const NodeBox& box = nodeBoxes[nodeIndex];
    // 1.15 margin keeps the right stereo eye covered by the view-0 frustum.
    if (!sphereInFrustum(frustum, box.center, glm::length(box.halfSize) * 1.15f))
        return;

    const i3s::NodeInfo& node = tree.nodes[nodeIndex];
    const NodeState state =
        static_cast<NodeState>(states_[nodeIndex].load(std::memory_order_relaxed));

    if (wantSplit(nodeIndex, cameraPos, screenFactor)) {
        if (childrenCoverable(nodeIndex)) {
            for (uint32_t c = 0; c < node.childCount; ++c)
                traverse(tree.childIndices[node.firstChild + c], submission,
                         cameraPos, screenFactor, frustum);
            return;
        }
        // Children incomplete: request the missing ones (only those inside
        // the frustum) and keep showing this node meanwhile — never a hole.
        for (uint32_t c = 0; c < node.childCount; ++c) {
            const uint32_t child = tree.childIndices[node.firstChild + c];
            const NodeBox& childBox = nodeBoxes[child];
            if (!sphereInFrustum(frustum, childBox.center,
                                 glm::length(childBox.halfSize) * 1.15f))
                continue;
            if (static_cast<NodeState>(states_[child].load(
                    std::memory_order_relaxed)) == NodeState::Unloaded &&
                tree.nodes[child].mesh.hasGeometry)
                requestNode(child);
        }
        if (state == NodeState::Resident) {
            emitDraw(nodeIndex, submission);
        } else {
            // Nothing of our own to show: draw whatever coverable child
            // subtrees exist so partial content appears ASAP.
            for (uint32_t c = 0; c < node.childCount; ++c) {
                const uint32_t child = tree.childIndices[node.firstChild + c];
                if (coverable(child))
                    traverse(child, submission, cameraPos, screenFactor, frustum);
            }
            if (state == NodeState::Unloaded && node.mesh.hasGeometry)
                requestNode(nodeIndex);
        }
        return;
    }

    // This node IS the right LOD for this camera.
    if (state == NodeState::Resident)
        emitDraw(nodeIndex, submission);
    else if (state == NodeState::Unloaded && node.mesh.hasGeometry)
        requestNode(nodeIndex);
}

void I3SSceneLayer::submitDraws(renderer::FrameSubmission& submission,
                                uint32_t viewportHeight) {
    drawnLastFrame_ = 0;
    if (!visible || !showGeometry || !streaming_ || tree.nodes.empty())
        return;
    ++frameStamp_;

    const renderer::ViewCamera& camera = submission.views[0];
    // proj[1][1] = 1/tan(fovy/2) (sign carries the house Y-flip).
    const float proj11 = std::fabs(camera.proj[1][1]);
    const float screenFactor = 0.5f * float(viewportHeight) * proj11;

    glm::vec4 frustum[6];
    extractFrustum(camera.proj * camera.view, frustum);

    traverse(0, submission, camera.position, screenFactor, frustum);
}

int I3SSceneLayer::pickNodeAt(const glm::vec3& worldPoint) const {
    int best = -1;
    int bestLevel = -1;
    for (const auto& entry : residency_) {
        const uint32_t nodeIndex = entry.first;
        if (drawnStamp_[nodeIndex] != frameStamp_)
            continue; // only nodes that are part of the current cut
        const NodeBox& box = nodeBoxes[nodeIndex];
        const glm::vec3 local = glm::transpose(box.axes) * (worldPoint - box.center);
        const glm::vec3 extent = box.halfSize + glm::vec3(0.05f); // pick slack
        if (std::fabs(local.x) > extent.x || std::fabs(local.y) > extent.y ||
            std::fabs(local.z) > extent.z)
            continue;
        const int level = tree.nodes[nodeIndex].level;
        if (level > bestLevel) {
            bestLevel = level;
            best = static_cast<int>(nodeIndex);
        }
    }
    return best;
}

I3SSceneLayer::Stats I3SSceneLayer::stats() const {
    Stats stats;
    stats.resident = residentCount_.load(std::memory_order_relaxed);
    stats.decoding = inFlightCount_.load(std::memory_order_relaxed);
    stats.failed = failedCount_.load(std::memory_order_relaxed);
    stats.drawnLastFrame = drawnLastFrame_;
    stats.gpuBytes = gpuBytes_.load(std::memory_order_relaxed);
    stats.cpuPendingBytes = cpuPendingBytes_.load(std::memory_order_relaxed);
    stats.readyPending = static_cast<uint32_t>(pendingCreates_.size());
    return stats;
}

} // namespace scene
