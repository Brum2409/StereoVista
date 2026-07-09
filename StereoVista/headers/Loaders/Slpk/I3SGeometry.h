#pragma once

// ============================================================================
// i3s::I3SGeometry — node geometry decode (draco + raw) into renderer-ready
// vertex/index arrays.
// ----------------------------------------------------------------------------
// Pure CPU, worker-thread safe, no Vulkan. Output vertices are node-relative
// in the APP render frame (Y-up), ready for a translate-only model matrix:
// the caller (scene::I3SSceneLayer) builds a NodeFrame from its GeoAnchor and
// this module applies it — all geodetic math stays in GeoAnchor.
//
// Source frames (validated against DA12_subset.slpk, see the plan's M1 field
// notes):
//   * raw buffers (1.6 + 1.7 legacy) store (dLon deg, dLat deg, dz m) deltas
//     from the node center for geographic CRS, metric deltas for projected;
//   * draco buffers store SCALED degree deltas — the position attribute's
//     i3s-scale_x/y metadata (typically 1/111319.49) recovers degrees.
// Geographic deltas are converted per vertex through ECEF in double; both
// buffer kinds of the same node land on identical geometry (harness-checked).
// Normals follow store.normalReferenceFrame (earth-centered rotated via the
// anchor basis, east-north-up via the node basis); when the chosen buffer has
// none they are computed from the faces (smooth, per shared position).
//
// uv-regions are folded into the UVs at decode time (atlas sub-rect remap);
// wrap semantics (uv outside [0,1]) can't be folded and set a flag instead.
// Per-vertex colors do NOT fit the shared 48-byte Vertex — they are detected
// (nonWhiteColors) and dropped for now (recorded in the plan).
// ============================================================================

#include "Loaders/Slpk/SlpkTypes.h"
#include "Renderer/MeshBuffer.h" // renderer::Vertex / MeshData (no Vulkan types used here)

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace i3s {

class SlpkArchive;

// Per-node coordinate context, built once per node by the scene layer.
struct NodeFrame {
    // Raw-path x/y deltas are degrees (geographic CRS) vs meters (projected).
    bool geographicDeltas = false;
    glm::dvec3 centerSR{ 0.0 };   // node center in the layer SR (= OBB center)
    glm::dvec3 centerEcef{ 0.0 }; // geodetic mode only
    glm::dmat3 ecefToApp{ 1.0 };  // kEnuToApp * anchor ENU basis (double)
    // Node-local ENU (or projected-CRS) meters -> app frame. Includes the
    // node->anchor curvature twist for geodetic layers.
    glm::mat3 metersToApp{ 1.0f };
    glm::mat3 ecefToAppF{ 1.0f }; // float cast of ecefToApp (normals)
    NormalReferenceFrame normalFrame = NormalReferenceFrame::Unknown;
};

// Decoded node geometry. Vertices are node-relative, app frame.
struct GeometryData {
    std::vector<renderer::Vertex> vertices;
    std::vector<uint32_t> indices;
    // Per-vertex index into the node's feature list (empty when the buffer
    // carries no feature information). featureIds are the 64-bit OIDs when
    // the buffer stores them (raw path); may be empty on the draco path.
    std::vector<uint32_t> featureIndexPerVertex;
    std::vector<uint64_t> featureIds;

    bool hadNormals = false;   // buffer-supplied (false => computed here)
    bool hadUvs = false;
    bool hadColors = false;
    bool nonWhiteColors = false;   // dropped colors carried real data
    bool uvRegionsFolded = false;
    bool uvRegionWrapDetected = false; // wrap semantics can't be folded

    glm::vec3 localMin{ 0.0f }; // bounds of the decoded positions
    glm::vec3 localMax{ 0.0f };

    size_t cpuBytes() const {
        return vertices.size() * sizeof(renderer::Vertex) +
               indices.size() * sizeof(uint32_t) +
               featureIndexPerVertex.size() * sizeof(uint32_t) +
               featureIds.size() * sizeof(uint64_t);
    }
};

class I3SGeometry {
public:
    // Reads + decodes the node's geometry resource out of the archive:
    // chooses the best buffer of the node's geometry definition (prefers the
    // one with real normals; draco on a tie), decodes, transforms into the
    // app frame, folds uv-regions and computes normals when needed. Fails
    // loudly with a reason.
    static bool decodeNode(const SlpkArchive& archive, const LayerInfo& info,
                           const NodeInfo& node, const NodeFrame& frame,
                           GeometryData& out, std::string& error);

    // Buffer-level entry points (used by decodeNode and the test harness).
    static bool decodeDraco(const uint8_t* data, size_t size,
                            const NodeFrame& frame, GeometryData& out,
                            std::string& error);
    static bool decodeRaw(const uint8_t* data, size_t size,
                          const GeometryBufferDesc& desc, uint64_t vertexCount,
                          uint64_t featureCount, const NodeFrame& frame,
                          GeometryData& out, std::string& error);
};

} // namespace i3s
