#include "Loaders/Slpk/I3SGeometry.h"

#include "Loaders/Slpk/GeoAnchor.h"
#include "Loaders/Slpk/SlpkArchive.h"

// Vendored draco decoder subset (headers/libs/draco, pinned in the plan §3).
// Warnings are silenced at OUR include site, never by editing vendored files.
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4018 4127 4189 4244 4245 4267 4305 4389 4456 4457 4459 4701 4702 4996)
#endif
#include "draco/compression/decode.h"
#include "draco/metadata/geometry_metadata.h"
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstring>
#include <string>

namespace i3s {

namespace {

// ---- little-endian scalar readers (memcpy: alignment-safe on every target) --

float readF32(const uint8_t* p) {
    float v;
    std::memcpy(&v, p, 4);
    return v;
}

double readF64(const uint8_t* p) {
    double v;
    std::memcpy(&v, p, 8);
    return v;
}

uint64_t readUnsigned(const uint8_t* p, ScalarType t) {
    switch (t) {
    case ScalarType::UInt8: return *p;
    case ScalarType::Int8: return static_cast<uint64_t>(*reinterpret_cast<const int8_t*>(p));
    case ScalarType::UInt16: case ScalarType::Int16: {
        uint16_t v; std::memcpy(&v, p, 2); return v;
    }
    case ScalarType::UInt32: case ScalarType::Int32: {
        uint32_t v; std::memcpy(&v, p, 4); return v;
    }
    case ScalarType::UInt64: case ScalarType::Int64: {
        uint64_t v; std::memcpy(&v, p, 8); return v;
    }
    default: return 0;
    }
}

double readAsDouble(const uint8_t* p, ScalarType t) {
    if (t == ScalarType::Float32)
        return readF32(p);
    if (t == ScalarType::Float64)
        return readF64(p);
    return static_cast<double>(readUnsigned(p, t));
}

// ---- shared post-processing -------------------------------------------------

// Smooth normals accumulated per vertex slot (draco point splits at UV/feature
// seams keep hard edges reasonably intact; raw soup gets per-face normals
// because no slot is shared).
void computeNormals(std::vector<renderer::Vertex>& vertices,
                    const std::vector<uint32_t>& indices) {
    for (renderer::Vertex& v : vertices)
        v.normal = glm::vec3(0.0f);
    for (size_t i = 0; i + 2 < indices.size(); i += 3) {
        renderer::Vertex& a = vertices[indices[i]];
        renderer::Vertex& b = vertices[indices[i + 1]];
        renderer::Vertex& c = vertices[indices[i + 2]];
        // Cross product length = 2x area: bigger faces weigh more (Thurmer's
        // area weighting — robust default for photogrammetry + buildings).
        const glm::vec3 n = glm::cross(b.position - a.position, c.position - a.position);
        a.normal += n;
        b.normal += n;
        c.normal += n;
    }
    for (renderer::Vertex& v : vertices) {
        const float len = glm::length(v.normal);
        v.normal = len > 1e-12f ? v.normal / len : glm::vec3(0.0f, 1.0f, 0.0f);
    }
}

// Fold the uv-region atlas sub-rect into the UVs (plan M1: static remap on
// the CPU instead of touching the shared Vertex). regions = uint16 x4 per
// vertex: [uMin, vMin, uMax, vMax] / 65535. Wrap semantics (uv outside the
// unit range) cannot be folded — flagged, then clamped.
void foldUvRegion(renderer::Vertex& v, const uint16_t region[4], bool& wrapDetected) {
    constexpr float kInv = 1.0f / 65535.0f;
    const glm::vec2 rmin(region[0] * kInv, region[1] * kInv);
    const glm::vec2 rmax(region[2] * kInv, region[3] * kInv);
    glm::vec2 uv = v.uv;
    if (uv.x < -0.001f || uv.x > 1.001f || uv.y < -0.001f || uv.y > 1.001f)
        wrapDetected = true;
    uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));
    v.uv = rmin + uv * (rmax - rmin);
}

void computeBounds(GeometryData& out) {
    glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
    for (const renderer::Vertex& v : out.vertices) {
        minB = glm::min(minB, v.position);
        maxB = glm::max(maxB, v.position);
    }
    if (out.vertices.empty())
        minB = maxB = glm::vec3(0.0f);
    out.localMin = minB;
    out.localMax = maxB;
}

// Normal source frame -> app frame (all bases are pure rotations, so normals
// transform like directions).
glm::mat3 normalToApp(const NodeFrame& frame) {
    switch (frame.normalFrame) {
    case NormalReferenceFrame::EarthCentered:
        return frame.ecefToAppF;
    case NormalReferenceFrame::EastNorthUp:
    case NormalReferenceFrame::VertexReferenceFrame:
    default:
        // Node-local ENU == the meters frame; local scenes idem. Unknown
        // defaults to east-north-up (the 1.7+ default).
        return frame.metersToApp;
    }
}

} // namespace

// ---- draco path ---------------------------------------------------------------

bool I3SGeometry::decodeDraco(const uint8_t* data, size_t size,
                              const NodeFrame& frame, GeometryData& out,
                              std::string& error) {
    draco::DecoderBuffer buffer;
    buffer.Init(reinterpret_cast<const char*>(data), size);

    auto typeOr = draco::Decoder::GetEncodedGeometryType(&buffer);
    if (!typeOr.ok()) {
        error = std::string("draco: not a draco buffer (") +
                typeOr.status().error_msg() + ")";
        return false;
    }
    if (typeOr.value() != draco::TRIANGULAR_MESH) {
        error = "draco: buffer is not a triangular mesh";
        return false;
    }

    draco::Decoder decoder;
    auto meshOr = decoder.DecodeMeshFromBuffer(&buffer);
    if (!meshOr.ok()) {
        error = std::string("draco: decode failed (") +
                meshOr.status().error_msg() + ")";
        return false;
    }
    const draco::Mesh& mesh = *meshOr.value();
    const uint32_t pointCount = mesh.num_points();
    if (pointCount == 0 || mesh.num_faces() == 0) {
        error = "draco: empty mesh";
        return false;
    }

    // Identify attributes: the i3s metadata tag wins, the draco semantic type
    // is the fallback (validated against DA12: position/uv/color arrive with
    // draco types, feature-index only via the metadata tag).
    const draco::PointAttribute* position = nullptr;
    const draco::PointAttribute* normal = nullptr;
    const draco::PointAttribute* uv0 = nullptr;
    const draco::PointAttribute* color = nullptr;
    const draco::PointAttribute* uvRegion = nullptr;
    const draco::PointAttribute* featureIndex = nullptr;
    const draco::AttributeMetadata* positionMd = nullptr;
    const draco::AttributeMetadata* featureIndexMd = nullptr;
    for (int i = 0; i < mesh.num_attributes(); ++i) {
        const draco::PointAttribute* att = mesh.attribute(i);
        std::string tag;
        const draco::AttributeMetadata* md =
            mesh.GetAttributeMetadataByAttributeId(i);
        if (md)
            md->GetEntryString("i3s-attribute-type", &tag);
        if (md && (tag == "position" ||
                   att->attribute_type() == draco::GeometryAttribute::POSITION))
            positionMd = md;
        if (md && tag == "feature-index")
            featureIndexMd = md;

        if (tag == "position")
            position = att;
        else if (tag == "normal")
            normal = att;
        else if (tag == "uv0")
            uv0 = att;
        else if (tag == "color")
            color = att;
        else if (tag == "uv-region" || tag == "region")
            uvRegion = att;
        else if (tag == "feature-index")
            featureIndex = att;
        else if (tag.empty()) {
            switch (att->attribute_type()) {
            case draco::GeometryAttribute::POSITION: position = att; break;
            case draco::GeometryAttribute::NORMAL: normal = att; break;
            case draco::GeometryAttribute::TEX_COORD:
                if (!uv0)
                    uv0 = att;
                break;
            case draco::GeometryAttribute::COLOR: color = att; break;
            case draco::GeometryAttribute::GENERIC:
                // uv-region rides as UInt16 x4; a lone integer is the
                // feature index.
                if (att->num_components() == 4 &&
                    att->data_type() == draco::DT_UINT16 && !uvRegion)
                    uvRegion = att;
                else if (att->num_components() == 1 && !featureIndex &&
                         (att->data_type() == draco::DT_UINT32 ||
                          att->data_type() == draco::DT_INT32 ||
                          att->data_type() == draco::DT_UINT64))
                    featureIndex = att;
                break;
            default: break;
            }
        }
    }
    if (!position) {
        error = "draco: mesh has no position attribute";
        return false;
    }

    // Geographic draco positions are SCALED DEGREE deltas from the node
    // center: x = dLon / i3s-scale_x, y = dLat / i3s-scale_y, z = meters.
    // (DA12 carries 1/111319.49 — the equatorial meters-per-degree — so the
    // raw values only LOOK like meters; the scale metadata is the truth.)
    // The exact ECEF conversion below matches the raw path. Without the
    // metadata the loaders.gl-compatible default is scale 1 (plain degrees).
    double scaleX = 1.0, scaleY = 1.0;
    if (positionMd) {
        positionMd->GetEntryDouble("i3s-scale_x", &scaleX);
        positionMd->GetEntryDouble("i3s-scale_y", &scaleY);
        if (scaleX <= 0.0)
            scaleX = 1.0;
        if (scaleY <= 0.0)
            scaleY = 1.0;
    }

    const glm::mat3 normalXf = normalToApp(frame);

    out.vertices.resize(pointCount);
    out.hadNormals = normal != nullptr;
    out.hadUvs = uv0 != nullptr;
    out.hadColors = color != nullptr;
    if (featureIndex)
        out.featureIndexPerVertex.resize(pointCount);

    for (uint32_t i = 0; i < pointCount; ++i) {
        const draco::PointIndex point(i);
        renderer::Vertex& v = out.vertices[i];

        float pos[3] = { 0, 0, 0 };
        if (!position->ConvertValue<float, 3>(position->mapped_index(point), pos)) {
            error = "draco: position conversion failed";
            return false;
        }
        if (frame.geographicDeltas) {
            const glm::dvec3 sr = frame.centerSR +
                                  glm::dvec3(double(pos[0]) * scaleX,
                                             double(pos[1]) * scaleY, double(pos[2]));
            const glm::dvec3 ecef = geodeticToEcef(sr);
            v.position = glm::vec3(frame.ecefToApp * (ecef - frame.centerEcef));
        } else {
            // Projected CRS: node-local metric deltas, one rotation to app.
            v.position = frame.metersToApp * glm::vec3(pos[0], pos[1], pos[2]);
        }

        if (normal) {
            float n[3] = { 0, 1, 0 };
            normal->ConvertValue<float, 3>(normal->mapped_index(point), n);
            const glm::vec3 xn = normalXf * glm::vec3(n[0], n[1], n[2]);
            const float len = glm::length(xn);
            v.normal = len > 1e-12f ? xn / len : glm::vec3(0.0f, 1.0f, 0.0f);
        }
        if (uv0) {
            float uv[2] = { 0, 0 };
            uv0->ConvertValue<float, 2>(uv0->mapped_index(point), uv);
            v.uv = glm::vec2(uv[0], uv[1]);
        }
        if (uvRegion) {
            uint16_t region[4] = { 0, 0, 65535, 65535 };
            uvRegion->ConvertValue<uint16_t, 4>(uvRegion->mapped_index(point), region);
            foldUvRegion(v, region, out.uvRegionWrapDetected);
            out.uvRegionsFolded = true;
        }
        if (color) {
            uint8_t rgba[4] = { 255, 255, 255, 255 };
            if (color->num_components() >= 3) {
                color->ConvertValue<uint8_t, 4>(color->mapped_index(point), rgba);
                if (rgba[0] != 255 || rgba[1] != 255 || rgba[2] != 255)
                    out.nonWhiteColors = true;
            }
        }
        if (featureIndex) {
            uint32_t fi = 0;
            featureIndex->ConvertValue<uint32_t, 1>(featureIndex->mapped_index(point),
                                                    &fi);
            out.featureIndexPerVertex[i] = fi;
        }
    }

    out.indices.reserve(size_t(mesh.num_faces()) * 3);
    for (draco::FaceIndex f(0); f < mesh.num_faces(); ++f) {
        const draco::Mesh::Face& face = mesh.face(f);
        out.indices.push_back(face[0].value());
        out.indices.push_back(face[1].value());
        out.indices.push_back(face[2].value());
    }

    // The node's feature-id list rides the feature-index attribute metadata
    // ("i3s-feature-ids", int32 array — validated against DA12).
    if (featureIndexMd) {
        std::vector<int32_t> ids;
        if (featureIndexMd->GetEntryIntArray("i3s-feature-ids", &ids) &&
            !ids.empty()) {
            out.featureIds.reserve(ids.size());
            for (int32_t id : ids)
                out.featureIds.push_back(
                    static_cast<uint64_t>(static_cast<uint32_t>(id)));
        }
    }

    if (!out.hadNormals)
        computeNormals(out.vertices, out.indices);
    computeBounds(out);
    return true;
}

// ---- raw path -------------------------------------------------------------------

bool I3SGeometry::decodeRaw(const uint8_t* data, size_t size,
                            const GeometryBufferDesc& desc, uint64_t vertexCount,
                            uint64_t featureCount, const NodeFrame& frame,
                            GeometryData& out, std::string& error) {
    // Header: named count fields override the node-page values (the buffer is
    // the source of truth; page counts have been observed as 0 on some nodes).
    size_t cursor = 0;
    for (const GeometryHeaderField& field : desc.header) {
        const size_t bytes = scalarSize(field.type);
        if (cursor + bytes > size) {
            error = "raw geometry: truncated header";
            return false;
        }
        const uint64_t value = readUnsigned(data + cursor, field.type);
        if (field.property == "vertexCount")
            vertexCount = value;
        else if (field.property == "featureCount")
            featureCount = value;
        cursor += bytes;
    }
    cursor = std::max<size_t>(cursor, desc.rawOffset);

    if (vertexCount == 0) {
        error = "raw geometry: vertex count unknown (no header field, no node count)";
        return false;
    }
    if (vertexCount % 3 != 0) {
        error = "raw geometry: vertex count " + std::to_string(vertexCount) +
                " is not a triangle soup";
        return false;
    }
    // Sanity cap: a raw buffer cannot describe more vertices than it has bytes.
    if (vertexCount > size / 12 + 1) {
        error = "raw geometry: vertex count exceeds buffer size";
        return false;
    }

    // Locate every stream (archive order, counts per binding).
    struct Located {
        const uint8_t* data = nullptr;
        const GeometryStream* stream = nullptr;
    };
    Located position, normal, uv0, color, uvRegion, featureId, faceRange;
    for (const GeometryStream& stream : desc.streams) {
        const uint64_t count = stream.perFeature ? featureCount : vertexCount;
        const size_t bytes =
            size_t(count) * stream.components * scalarSize(stream.type);
        if (cursor + bytes > size) {
            error = "raw geometry: truncated stream";
            return false;
        }
        Located located{ data + cursor, &stream };
        switch (stream.semantic) {
        case VertexSemantic::Position: position = located; break;
        case VertexSemantic::Normal: normal = located; break;
        case VertexSemantic::Uv0: uv0 = located; break;
        case VertexSemantic::Color: color = located; break;
        case VertexSemantic::UvRegion: uvRegion = located; break;
        case VertexSemantic::FeatureId: featureId = located; break;
        case VertexSemantic::FaceRange: faceRange = located; break;
        default: break; // unknown stream: skipped by the cursor advance
        }
        cursor += bytes;
    }
    if (!position.data) {
        error = "raw geometry: no position stream";
        return false;
    }
    if (position.stream->components < 3) {
        error = "raw geometry: position stream needs 3 components";
        return false;
    }

    if (normal.data && normal.stream->components < 3)
        normal = Located{}; // 2-component normals would read past their stride

    const glm::mat3 normalXf = normalToApp(frame);
    const size_t posElem = scalarSize(position.stream->type);
    const size_t posStride = posElem * position.stream->components;

    out.vertices.resize(size_t(vertexCount));
    out.hadNormals = normal.data != nullptr;
    out.hadUvs = uv0.data != nullptr;
    out.hadColors = color.data != nullptr;

    for (size_t i = 0; i < vertexCount; ++i) {
        renderer::Vertex& v = out.vertices[i];
        const uint8_t* p = position.data + i * posStride;
        const glm::dvec3 delta(readAsDouble(p, position.stream->type),
                               readAsDouble(p + posElem, position.stream->type),
                               readAsDouble(p + 2 * posElem, position.stream->type));
        if (frame.geographicDeltas) {
            // (dLon deg, dLat deg, dz m) from the node center: exact per-vertex
            // conversion through ECEF in double (plan §7) — no scale-factor
            // approximations, no cracks between adjacent nodes.
            const glm::dvec3 sr = frame.centerSR + delta;
            const glm::dvec3 ecef = geodeticToEcef(sr);
            v.position = glm::vec3(frame.ecefToApp * (ecef - frame.centerEcef));
        } else {
            v.position = frame.metersToApp * glm::vec3(delta);
        }
    }

    if (normal.data) {
        const size_t elem = scalarSize(normal.stream->type);
        const size_t stride = elem * normal.stream->components;
        for (size_t i = 0; i < vertexCount; ++i) {
            const uint8_t* p = normal.data + i * stride;
            const glm::vec3 n(static_cast<float>(readAsDouble(p, normal.stream->type)),
                              static_cast<float>(readAsDouble(p + elem, normal.stream->type)),
                              static_cast<float>(readAsDouble(p + 2 * elem, normal.stream->type)));
            const glm::vec3 xn = normalXf * n;
            const float len = glm::length(xn);
            out.vertices[i].normal =
                len > 1e-12f ? xn / len : glm::vec3(0.0f, 1.0f, 0.0f);
        }
    }

    if (uv0.data && uv0.stream->type == ScalarType::Float32 &&
        uv0.stream->components >= 2) {
        const size_t stride = 4 * uv0.stream->components;
        for (size_t i = 0; i < vertexCount; ++i) {
            const uint8_t* p = uv0.data + i * stride;
            out.vertices[i].uv = glm::vec2(readF32(p), readF32(p + 4));
        }
    }

    if (uvRegion.data && uvRegion.stream->type == ScalarType::UInt16 &&
        uvRegion.stream->components == 4) {
        for (size_t i = 0; i < vertexCount; ++i) {
            uint16_t region[4];
            std::memcpy(region, uvRegion.data + i * 8, 8);
            foldUvRegion(out.vertices[i], region, out.uvRegionWrapDetected);
        }
        out.uvRegionsFolded = true;
    }

    if (color.data && color.stream->type == ScalarType::UInt8 &&
        color.stream->components >= 3) {
        const size_t stride = color.stream->components;
        for (size_t i = 0; i < vertexCount && !out.nonWhiteColors; ++i) {
            const uint8_t* p = color.data + i * stride;
            if (p[0] != 255 || p[1] != 255 || p[2] != 255)
                out.nonWhiteColors = true;
        }
    }

    // Triangle soup: trivial index list (MeshBuffer draws indexed).
    out.indices.resize(size_t(vertexCount));
    for (uint32_t i = 0; i < vertexCount; ++i)
        out.indices[i] = i;

    // Per-feature ids + face ranges -> per-vertex feature indices.
    if (featureId.data && featureCount > 0) {
        const size_t elem = scalarSize(featureId.stream->type);
        out.featureIds.resize(size_t(featureCount));
        for (size_t f = 0; f < featureCount; ++f)
            out.featureIds[f] =
                readUnsigned(featureId.data + f * elem * featureId.stream->components,
                             featureId.stream->type);
    }
    if (faceRange.data && featureCount > 0 &&
        faceRange.stream->type == ScalarType::UInt32 &&
        faceRange.stream->components == 2) {
        out.featureIndexPerVertex.assign(size_t(vertexCount), 0);
        for (size_t f = 0; f < featureCount; ++f) {
            uint32_t range[2];
            std::memcpy(range, faceRange.data + f * 8, 8);
            // Inclusive face range; 3 soup vertices per face.
            const size_t first = std::min<size_t>(size_t(range[0]) * 3, vertexCount);
            const size_t last = std::min<size_t>(size_t(range[1]) * 3 + 3, vertexCount);
            for (size_t i = first; i < last; ++i)
                out.featureIndexPerVertex[i] = static_cast<uint32_t>(f);
        }
    }

    if (!out.hadNormals)
        computeNormals(out.vertices, out.indices);
    computeBounds(out);
    return true;
}

// ---- node-level entry -------------------------------------------------------------

bool I3SGeometry::decodeNode(const SlpkArchive& archive, const LayerInfo& info,
                             const NodeInfo& node, const NodeFrame& frame,
                             GeometryData& out, std::string& error) {
    out = GeometryData{};
    if (!node.mesh.hasGeometry) {
        error = "node has no geometry";
        return false;
    }

    int defIndex = node.mesh.geometryDefinition;
    if (defIndex < 0 && !info.geometryDefs.empty())
        defIndex = 0;
    if (defIndex < 0 || defIndex >= static_cast<int>(info.geometryDefs.size())) {
        error = "node references geometry definition " + std::to_string(defIndex) +
                " which the layer does not declare";
        return false;
    }
    const GeometryDefinition& def = info.geometryDefs[defIndex];

    // Buffer choice: real normals beat computed ones (packages like DA12 ship
    // draco without normals next to a raw buffer with them — the raw parse is
    // cheaper than recomputing wrong shading); draco wins any tie.
    int dracoIndex = -1, rawIndex = -1;
    for (size_t b = 0; b < def.buffers.size(); ++b) {
        if (def.buffers[b].compressed && dracoIndex < 0)
            dracoIndex = static_cast<int>(b);
        else if (!def.buffers[b].compressed && rawIndex < 0)
            rawIndex = static_cast<int>(b);
    }
    int chosen = dracoIndex >= 0 ? dracoIndex : rawIndex;
    if (dracoIndex >= 0 && rawIndex >= 0 &&
        !def.buffers[dracoIndex].declaresCompressed("normal") &&
        def.buffers[rawIndex].has(VertexSemantic::Normal))
        chosen = rawIndex;
    if (chosen < 0) {
        error = "geometry definition declares no buffers";
        return false;
    }
    const GeometryBufferDesc& buffer = def.buffers[chosen];

    // Resource path: 1.6 nodes carry a resolved extensionless href ("x.bin"
    // tried too — ArcGIS stores "geometries/0.bin.gz"); 1.7+ pages address
    // nodes/<resource>/geometries/<bufferIndex>.
    std::string path = node.mesh.v16GeometryPath;
    if (path.empty())
        path = "nodes/" + std::to_string(node.mesh.geometryResource) +
               "/geometries/" + std::to_string(chosen) + ".bin";

    std::vector<uint8_t> bytes;
    if (!archive.read(path, bytes) &&
        !(node.mesh.v16GeometryPath.empty() ? false
                                            : archive.read(path + ".bin", bytes))) {
        error = "geometry resource missing: " + path;
        return false;
    }
    if (bytes.empty()) {
        error = "geometry resource empty: " + path;
        return false;
    }

    const bool ok =
        buffer.compressed
            ? decodeDraco(bytes.data(), bytes.size(), frame, out, error)
            : decodeRaw(bytes.data(), bytes.size(), buffer, node.mesh.vertexCount,
                        node.mesh.featureCount, frame, out, error);
    if (!ok)
        error += " (" + path + ")";
    return ok;
}

} // namespace i3s
