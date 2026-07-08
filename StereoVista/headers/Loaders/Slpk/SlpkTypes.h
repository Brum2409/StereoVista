#pragma once

// ============================================================================
// i3s::SlpkTypes — the plain-data model the SLPK/I3S parsers produce.
// ----------------------------------------------------------------------------
// One normalized shape for every supported package version (1.6 -> 1.8 mesh
// profiles, PCSL 2.0 point clouds): I3SLayer.cpp is the ONLY place that knows
// about version differences; everything downstream (scene layer, traversal,
// inspector UI) consumes this. Deliberately light — no JSON, no Vulkan, no
// renderer types; glm + std only.
// ============================================================================

#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace i3s {

enum class LayerType {
    Unknown,
    Object3D,       // "3DObject"
    IntegratedMesh, // "IntegratedMesh" (same mesh profile as 3DObject)
    PointCloud,     // PCSL 2.0
    Point,          // feature points + symbols (not supported yet)
    Building,       // BSL: a 3D-object layer + sublayer JSON (not supported yet)
};

// How lodThreshold is interpreted (nodePages.lodSelectionMetricType, or the
// per-node lodSelection metric in 1.6 documents).
enum class LodMetric {
    Unknown,
    MaxScreenThresholdSQ, // projected OBB area, px^2 (1.7+ default)
    MaxScreenThreshold,   // projected MBS diameter, px (1.6 common)
    DistanceRangeFromDefaultCamera,
    DensityThreshold,     // PCSL 2.0 (points per unit area)
};

// store.normalReferenceFrame — how vertex normals are oriented (matters in M1).
enum class NormalReferenceFrame {
    Unknown,
    EastNorthUp,          // global scenes, 1.7+ default
    EarthCentered,        // ECEF; rotate into ENU at decode
    VertexReferenceFrame, // local scenes
};

struct SpatialReference {
    int wkid = 0;
    int latestWkid = 0;
    int vcsWkid = 0;
    std::string wkt;

    // Geographic (lon/lat degrees) vs projected (metric) horizontal CRS.
    // 4326 = WGS84, 4490 = CGCS2000 — the two geographic CRS I3S global
    // scenes use in practice.
    bool isGeographic() const {
        const int id = latestWkid != 0 ? latestWkid : wkid;
        return id == 4326 || id == 4490;
    }
};

struct AttributeField {
    std::string key;  // resource key, e.g. "f_3"
    std::string name; // field name, e.g. "OBJECTID"
    std::string valueType;
};

struct TextureSetDefinition {
    std::vector<std::string> formats; // "jpg", "png", "dds", "ktx2", "basis"
    bool atlas = false;
};

// Summary of one geometryDefinitions[] entry (1.7+). M1 uses this to pick the
// draco buffer and to know which attributes to expect; M0's inspector lists it.
struct GeometryDefinition {
    bool hasCompressed = false; // a draco geometryBuffers entry exists
    bool hasRaw = false;        // an uncompressed geometryBuffers entry exists
    bool hasNormal = false;
    bool hasUv0 = false;
    bool hasColor = false;
    bool hasUvRegion = false;
    bool hasFeatureId = false;
};

struct LayerInfo {
    LayerType type = LayerType::Unknown;
    std::string typeString;   // raw layerType for the inspector / diagnostics
    std::string version;      // store.version: "1.6" .. "2.0"
    int versionMajor = 0;
    int versionMinor = 0;
    std::string name;
    std::string profile;      // store.profile
    SpatialReference sr;
    NormalReferenceFrame normalFrame = NormalReferenceFrame::Unknown;

    // Node organization. hasNodePages selects the 1.7+ paged flat tree; the
    // 1.6 fallback walks per-node index documents from rootNodePath.
    bool hasNodePages = false;
    int nodesPerPage = 64;
    LodMetric lodMetric = LodMetric::Unknown;
    std::string rootNodePath; // 1.6: store.rootNode ("./nodes/root")

    // fullExtent (xmin,ymin,xmax,ymax,zmin,zmax) in the layer SR.
    bool hasFullExtent = false;
    double fullExtent[6] = { 0, 0, 0, 0, 0, 0 };

    std::vector<AttributeField> attributeFields;
    std::vector<TextureSetDefinition> textureSets;
    std::vector<GeometryDefinition> geometryDefs;
    size_t materialCount = 0;         // materialDefinitions[] size (parsed fully in M1)
    std::string textureEncodingSummary; // 1.6 store.textureEncoding, joined
    bool hasStatistics = false;       // statisticsInfo[] present
};

// Per-node mesh resource references, normalized across versions. For 1.7+
// these are the node-page integers; for 1.6 the node id string in
// NodeInfo::v16Id addresses the per-node resource folder instead.
struct NodeMesh {
    bool hasGeometry = false;
    int geometryDefinition = -1;   // index into LayerInfo::geometryDefs (1.7+)
    uint32_t geometryResource = 0; // nodes/<resource>/geometries/<buffer>.bin(.gz)
    int materialDefinition = -1;
    uint32_t materialResource = 0; // texture resource id
    uint32_t attributeResource = 0;
    uint64_t vertexCount = 0;
    uint64_t featureCount = 0;
};

// One node of the flattened bounding-volume hierarchy. Children are stored
// compactly in a side array (I3SNodeTree::childIndices) — 100k+ node layers
// are the norm, per-node vectors are heap churn for nothing.
struct NodeInfo {
    // OBB in the layer's spatial reference: center is (lon, lat, z-meters)
    // for geographic CRS or CRS units for projected; halfSize is meters; the
    // quaternion orients the box axes in ECEF for global scenes / the CRS
    // frame for local ones (matches CesiumJS + loaders.gl interpretation).
    glm::dvec3 obbCenter{ 0.0 };
    glm::vec3 obbHalfSize{ 0.0f };
    glm::quat obbQuat{ 1.0f, 0.0f, 0.0f, 0.0f }; // w,x,y,z ctor order
    bool obbSynthesizedFromMbs = false; // 1.6 node without an obb

    double lodThreshold = 0.0; // interpreted per LayerInfo::lodMetric
    int32_t parent = -1;
    uint32_t firstChild = 0; // range into I3SNodeTree::childIndices
    uint32_t childCount = 0;
    uint16_t level = 0;      // depth from root (root = 0)

    NodeMesh mesh;
    std::string v16Id; // 1.6 node id ("0-1-3"); empty for 1.7+ paged nodes
};

struct I3SNodeTree {
    std::vector<NodeInfo> nodes;        // root at index 0
    std::vector<uint32_t> childIndices; // NodeInfo::firstChild/childCount ranges
    uint16_t levelCount = 0;
    std::vector<uint32_t> levelNodeCounts; // nodes per level (inspector)
};

} // namespace i3s
