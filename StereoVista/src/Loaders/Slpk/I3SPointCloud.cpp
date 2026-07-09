#include "Loaders/Slpk/I3SPointCloud.h"

#include "Loaders/Slpk/GeoAnchor.h"
#include "Loaders/Slpk/SlpkArchive.h"

// Vendored LEPCC decoder (headers/libs/lepcc, pinned in the plan §3). Plain C
// API; every call takes a context, one context per call keeps workers
// independent (the context carries per-decode scratch state).
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4244 4267)
#endif
#include <lepcc/include/lepcc_c_api.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <cfloat>
#include <cstring>

namespace i3s {

namespace {

// RAII for the C context (exceptions from std::vector must not leak it).
struct LepccContext {
    lepcc_ContextHdl ctx = lepcc_createContext();
    ~LepccContext() {
        if (ctx)
            lepcc_deleteContext(&ctx);
    }
};

constexpr lepcc_status kLepccOk = 0; // (lepcc_status)ErrCode::Ok

// Reads the first existing variant of a resource. LEPCC blobs are their own
// compression, so 1.x stores them raw under tagged extensions (.pccxyz /
// .pccint / .pccrgb); 2.0 packages use plain .bin(.gz) — read() is
// gzip-transparent for those.
bool readFirst(const SlpkArchive& archive, const std::string& base,
               const char* const* extensions, size_t extensionCount,
               std::vector<uint8_t>& out) {
    for (size_t i = 0; i < extensionCount; ++i)
        if (archive.read(base + extensions[i], out) && !out.empty())
            return true;
    return false;
}

const AttributeField* findField(const LayerInfo& info, const char* encoding,
                                const char* name) {
    for (const AttributeField& f : info.attributeFields) {
        if (encoding && f.encoding == encoding)
            return &f;
        if (name && f.name == name && f.encoding.empty())
            return &f;
    }
    return nullptr;
}

} // namespace

bool I3SPointCloud::decodeXyz(const uint8_t* data, size_t size,
                              const NodeFrame& frame, std::vector<glm::vec3>& out,
                              glm::vec3& outMin, glm::vec3& outMax,
                              std::string& error) {
    LepccContext lepcc;
    if (!lepcc.ctx) {
        error = "lepcc: context creation failed";
        return false;
    }
    unsigned int pointCount = 0;
    if (lepcc_getPointCount(lepcc.ctx, data, static_cast<int>(size), &pointCount) !=
            kLepccOk ||
        pointCount == 0) {
        error = "lepcc: not an xyz blob (or empty)";
        return false;
    }
    // Guard a forged header before the multi-hundred-MB allocation: the blob
    // cannot describe more points than ~3 bytes each can encode.
    if (pointCount > size) {
        error = "lepcc: point count exceeds blob size";
        return false;
    }

    std::vector<double> xyz(size_t(pointCount) * 3);
    const unsigned char* cursor = data;
    unsigned int decoded = pointCount;
    if (lepcc_decodeXYZ(lepcc.ctx, &cursor, static_cast<int>(size), &decoded,
                        xyz.data()) != kLepccOk ||
        decoded == 0) {
        error = "lepcc: xyz decode failed";
        return false;
    }

    // LEPCC decodes ABSOLUTE layer-SR coordinates — subtract the node center
    // here (double), unlike the mesh path where buffers store deltas.
    out.resize(decoded);
    glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
    for (unsigned int i = 0; i < decoded; ++i) {
        const glm::dvec3 sr(xyz[size_t(i) * 3 + 0], xyz[size_t(i) * 3 + 1],
                            xyz[size_t(i) * 3 + 2]);
        glm::vec3 p;
        if (frame.geographicDeltas) {
            const glm::dvec3 ecef = geodeticToEcef(sr);
            p = glm::vec3(frame.ecefToApp * (ecef - frame.centerEcef));
        } else {
            p = frame.metersToApp * glm::vec3(sr - frame.centerSR);
        }
        out[i] = p;
        minB = glm::min(minB, p);
        maxB = glm::max(maxB, p);
    }
    outMin = minB;
    outMax = maxB;
    return true;
}

bool I3SPointCloud::decodeRgb(const uint8_t* data, size_t size,
                              std::vector<uint8_t>& out, std::string& error) {
    LepccContext lepcc;
    unsigned int count = 0;
    if (!lepcc.ctx ||
        lepcc_getRGBCount(lepcc.ctx, data, static_cast<int>(size), &count) !=
            kLepccOk ||
        count == 0 || count > size * 8) {
        error = "lepcc: not an rgb blob";
        return false;
    }
    out.resize(size_t(count) * 3);
    const unsigned char* cursor = data;
    unsigned int decoded = count;
    if (lepcc_decodeRGB(lepcc.ctx, &cursor, static_cast<int>(size), &decoded,
                        out.data()) != kLepccOk) {
        error = "lepcc: rgb decode failed";
        out.clear();
        return false;
    }
    out.resize(size_t(decoded) * 3);
    return true;
}

bool I3SPointCloud::decodeIntensity(const uint8_t* data, size_t size,
                                    std::vector<uint16_t>& out,
                                    std::string& error) {
    LepccContext lepcc;
    unsigned int count = 0;
    if (!lepcc.ctx ||
        lepcc_getIntensityCount(lepcc.ctx, data, static_cast<int>(size), &count) !=
            kLepccOk ||
        count == 0 || count > size * 8) {
        error = "lepcc: not an intensity blob";
        return false;
    }
    out.resize(count);
    const unsigned char* cursor = data;
    unsigned int decoded = count;
    if (lepcc_decodeIntensity(lepcc.ctx, &cursor, static_cast<int>(size), &decoded,
                              out.data()) != kLepccOk) {
        error = "lepcc: intensity decode failed";
        out.clear();
        return false;
    }
    out.resize(decoded);
    return true;
}

bool I3SPointCloud::decodeNode(const SlpkArchive& archive, const LayerInfo& info,
                               const NodeInfo& node, const NodeFrame& frame,
                               PointCloudData& out, std::string& error) {
    out = PointCloudData{};
    if (!node.mesh.hasGeometry) {
        error = "node has no geometry";
        return false;
    }
    const std::string nodeBase =
        "nodes/" + std::to_string(node.mesh.geometryResource);

    static const char* kXyzExtensions[] = { "/geometries/0.bin.pccxyz",
                                            "/geometries/0.bin" };
    std::vector<uint8_t> bytes;
    if (!readFirst(archive, nodeBase, kXyzExtensions, 2, bytes)) {
        error = "point geometry missing: " + nodeBase + "/geometries/0";
        return false;
    }
    if (!decodeXyz(bytes.data(), bytes.size(), frame, out.positions, out.localMin,
                   out.localMax, error)) {
        error += " (" + nodeBase + ")";
        return false;
    }

    // Attribute columns are best-effort: a bad column degrades colorization,
    // never the geometry. Counts must match the point count to be usable.
    auto attributeBase = [&](const AttributeField& field) {
        return nodeBase + "/attributes/" + field.key;
    };
    std::string attrError;

    if (const AttributeField* rgb = findField(info, "lepcc-rgb", "RGB")) {
        static const char* kRgbExtensions[] = { ".bin.pccrgb", ".bin" };
        if (readFirst(archive, attributeBase(*rgb), kRgbExtensions, 2, bytes)) {
            if (!decodeRgb(bytes.data(), bytes.size(), out.rgb, attrError) ||
                out.rgb.size() != out.pointCount() * 3) {
                out.rgb.clear();
                out.attributeWarning = "RGB column unusable";
            }
        }
    }
    if (const AttributeField* intensity =
            findField(info, "lepcc-intensity", "INTENSITY")) {
        static const char* kIntensityExtensions[] = { ".bin.pccint", ".bin" };
        if (readFirst(archive, attributeBase(*intensity), kIntensityExtensions, 2,
                      bytes)) {
            if (!decodeIntensity(bytes.data(), bytes.size(), out.intensity,
                                 attrError) ||
                out.intensity.size() != out.pointCount()) {
                out.intensity.clear();
                if (out.attributeWarning.empty())
                    out.attributeWarning = "intensity column unusable";
            }
        }
    }
    if (const AttributeField* classCode = findField(info, nullptr, "CLASS_CODE")) {
        // Raw column: the values array only, one byte per point.
        if (archive.read(attributeBase(*classCode) + ".bin", bytes)) {
            if (bytes.size() == out.pointCount())
                out.classCodes.assign(bytes.begin(), bytes.end());
            else if (out.attributeWarning.empty())
                out.attributeWarning = "class-code column count mismatch";
        }
    }
    return true;
}

} // namespace i3s
