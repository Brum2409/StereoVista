#include "Scene/I3SSceneLayer.h"

#include "Loaders/Slpk/I3SLayer.h"
#include "Loaders/Slpk/SlpkArchive.h"
#include "Renderer/OverlayDrawList.h"

#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cfloat>
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

} // namespace

I3SSceneLayer::I3SSceneLayer() = default;
I3SSceneLayer::~I3SSceneLayer() = default;

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

} // namespace scene
