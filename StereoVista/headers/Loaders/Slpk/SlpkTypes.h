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
    std::string key;  // resource key, e.g. "f_3" (mesh) / "2" (PCSL)
    std::string name; // field name, e.g. "OBJECTID"
    std::string valueType;
    // PCSL: how the column is stored — "lepcc-rgb", "lepcc-intensity",
    // "embedded-elevation" (no resource; z carries it), or empty for a raw
    // value array.
    std::string encoding;
};

// One format alternative of a texture set: file stem + encoding, addressed as
// "nodes/<resource>/textures/<name>.<format>" in 1.7+ packages.
struct TextureFormat {
    std::string name;   // "0", "0_0_1", ...
    std::string format; // "jpg", "png", "dds", "ktx2", "basis"
};

struct TextureSetDefinition {
    std::vector<TextureFormat> formats;
    bool atlas = false;

    bool hasFormat(const char* fmt) const {
        for (const TextureFormat& f : formats)
            if (f.format == fmt)
                return true;
        return false;
    }
};

// ---- geometry buffer descriptions (M1) --------------------------------------

// Scalar element type of a geometry stream / header field.
enum class ScalarType : uint8_t {
    Unknown,
    Float32, Float64,
    UInt8, Int8,
    UInt16, Int16,
    UInt32, Int32,
    UInt64, Int64,
};

inline size_t scalarSize(ScalarType t) {
    switch (t) {
    case ScalarType::UInt8: case ScalarType::Int8: return 1;
    case ScalarType::UInt16: case ScalarType::Int16: return 2;
    case ScalarType::Float32: case ScalarType::UInt32: case ScalarType::Int32: return 4;
    case ScalarType::Float64: case ScalarType::UInt64: case ScalarType::Int64: return 8;
    default: return 0;
    }
}

// What a stream in a raw (uncompressed) geometry buffer means.
enum class VertexSemantic : uint8_t {
    Position, Normal, Uv0, Color, UvRegion,
    FeatureId,  // per-feature
    FaceRange,  // per-feature [firstFace, lastFace] inclusive
    Unknown,
};

// One stream of a raw geometry buffer, in archive order. Per-vertex streams
// are vertexCount elements; per-feature streams featureCount.
struct GeometryStream {
    VertexSemantic semantic = VertexSemantic::Unknown;
    ScalarType type = ScalarType::Unknown;
    uint8_t components = 0;
    bool perFeature = false;
};

// One leading header field of a raw buffer (1.6 defaultGeometrySchema.header;
// 1.7+ raw buffers carry an implicit {vertexCount, featureCount} pair when
// geometryBuffers[].offset == 8 — normalized into this same form).
struct GeometryHeaderField {
    std::string property; // "vertexCount", "featureCount", ...
    ScalarType type = ScalarType::Unknown;
};

// One geometryBuffers[] entry (1.7+), or the whole 1.6 defaultGeometrySchema
// normalized into the raw form.
struct GeometryBufferDesc {
    bool compressed = false; // draco ("compressedAttributes")
    // raw path: byte offset of the first stream (1.7 "offset", or the summed
    // header size for 1.6 schemas) + the leading count fields when known.
    uint32_t rawOffset = 0;
    std::vector<GeometryHeaderField> header; // leading fields, in order
    std::vector<GeometryStream> streams;     // vertex + feature streams, in order
    // draco path: declared attribute names ("position", "uv0", "color",
    // "feature-index", "normal", "uv-region"), informational.
    std::vector<std::string> compressedAttributes;

    bool has(VertexSemantic s) const {
        for (const GeometryStream& st : streams)
            if (st.semantic == s)
                return true;
        return false;
    }
    bool declaresCompressed(const char* name) const {
        for (const std::string& a : compressedAttributes)
            if (a == name)
                return true;
        return false;
    }
    size_t headerBytes() const {
        size_t bytes = 0;
        for (const GeometryHeaderField& f : header)
            bytes += scalarSize(f.type);
        return bytes;
    }
};

// One geometryDefinitions[] entry (1.7+) or the 1.6 schema (single raw
// buffer). Buffer order matches the archive's geometries/<index>.bin naming.
struct GeometryDefinition {
    std::vector<GeometryBufferDesc> buffers;

    // Convenience summaries (inspector + buffer selection).
    bool hasCompressed() const {
        for (const GeometryBufferDesc& b : buffers)
            if (b.compressed)
                return true;
        return false;
    }
    bool hasRaw() const {
        for (const GeometryBufferDesc& b : buffers)
            if (!b.compressed)
                return true;
        return false;
    }
};

// ---- materials (M1) ----------------------------------------------------------

// Normalized material definition: 1.7+ glTF-style materialDefinitions[] and
// the 1.6 sharedResource materialDefinitions both land here. Texture
// references are textureSetDefinitions indices (1.7+); the 1.6 per-node
// texture href lives on the node (NodeMesh::v16TexturePath) since 1.6 images
// are per-node resources.
struct MaterialDesc {
    glm::vec4 baseColor{ 1.0f, 1.0f, 1.0f, 1.0f };
    float metallicFactor = 0.0f;
    float roughnessFactor = 0.85f;
    glm::vec3 emissiveFactor{ 0.0f };
    float alphaCutoff = 0.25f;
    bool doubleSided = false;

    int baseColorTexture = -1;         // textureSetDefinitions index or -1
    int metallicRoughnessTexture = -1; // parsed; consumed in a later milestone
    int normalTexture = -1;
    int occlusionTexture = -1;
    int emissiveTexture = -1;
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
    std::vector<MaterialDesc> materials; // 1.7+: layer JSON; 1.6: sharedResource
    std::string textureEncodingSummary;  // 1.6 store.textureEncoding, joined
    bool hasStatistics = false;          // statisticsInfo[] present
};

// Per-node mesh resource references, normalized across versions. For 1.7+
// these are the node-page integers; for 1.6 the resolved archive paths below
// address the per-node resource folder instead.
struct NodeMesh {
    bool hasGeometry = false;
    int geometryDefinition = -1;   // index into LayerInfo::geometryDefs
    uint32_t geometryResource = 0; // nodes/<resource>/geometries/<buffer>.bin(.gz)
    int materialDefinition = -1;   // index into LayerInfo::materials
    uint32_t materialResource = 0; // texture resource id
    uint32_t attributeResource = 0;
    uint64_t vertexCount = 0;
    uint64_t featureCount = 0;
    // 1.6 resource addressing (resolved against the node document's directory
    // at tree load; empty for 1.7+ paged nodes).
    std::string v16GeometryPath; // "nodes/<id>/geometries/0"
    std::string v16TexturePath;  // "nodes/<id>/textures/0_0" (no extension)
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

    // Reference center of the node's GEOMETRY deltas: the MBS center for 1.6
    // nodes (which may differ from the OBB center), the OBB center for 1.7+
    // paged nodes (which have no MBS).
    glm::dvec3 geomCenter{ 0.0 };

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
