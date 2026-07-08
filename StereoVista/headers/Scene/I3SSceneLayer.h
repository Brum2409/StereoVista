#pragma once

// ============================================================================
// scene::I3SSceneLayer — an opened SLPK layer as a scene object.
// ----------------------------------------------------------------------------
// Parallel to scene::Model, NOT inside it: an I3S layer is a paged LOD
// hierarchy, not a mesh list. M0 scope: the layer opens (mmap + JSON + node
// tree, all CPU), anchors itself (double-precision geodetic -> local ENU) and
// draws its bounding-volume hierarchy through the overlay renderer for the
// SLPK inspector. Geometry rendering (M1) and streaming (M2) grow here.
//
// Coordinate frames: node data arrives in the layer CRS; GeoAnchor turns it
// into ENU meters at the layer anchor (root OBB center); the app render world
// is Y-up, so ENU (x=east, y=north, z=up) maps to app (x=east, y=up,
// z=-north) — north is into the screen for an identity camera.
//
// load() is pure CPU — the application runs it on a worker thread and only
// touches the finished object from the main thread (M0 has no GPU state at
// all; M1's GPU residency will be created main-thread-side in the pump).
// ============================================================================

#include "Loaders/Slpk/GeoAnchor.h"
#include "Loaders/Slpk/SlpkTypes.h"

#include <glm/glm.hpp>

#include <memory>
#include <string>
#include <vector>

namespace renderer {
class OverlayDrawList;
}

namespace i3s {
class SlpkArchive;
}

namespace scene {

class I3SSceneLayer {
public:
    I3SSceneLayer();
    ~I3SSceneLayer(); // out-of-line: unique_ptr over the fwd-declared archive
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
    };
    std::vector<NodeBox> nodeBoxes;

    // Union of all node boxes, app anchor space (drives world bounds, framing).
    glm::vec3 boundsMin{ 0.0f };
    glm::vec3 boundsMax{ 0.0f };

    // ---- inspector state (edited by the SLPK panel) ----
    bool showObbs = true;      // draw the BVH through the overlay renderer
    int obbLevel = 1;          // level selected by the panel slider
    bool obbUpToLevel = true;  // true: draw levels 0..obbLevel; false: only obbLevel
    int obbMaxBoxes = 2048;    // cap on boxes per frame (each box = 12 lines
                               // = 72 overlay vertices, uploaded dynamically)

    // Appends the OBB wireframes (colored by tree level) to the overlay.
    void appendObbOverlay(renderer::OverlayDrawList& overlay) const;

    // Boxes currently passing the level filter (for the panel readout).
    size_t countFilteredBoxes() const;

private:
    std::unique_ptr<i3s::SlpkArchive> archive_;
    std::string error_;
};

} // namespace scene
