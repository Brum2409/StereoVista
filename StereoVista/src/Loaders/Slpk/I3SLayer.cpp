#include "Loaders/Slpk/I3SLayer.h"

#include "Loaders/Slpk/SlpkArchive.h"

#include <json.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <string>
#include <unordered_set>
#include <vector>

namespace i3s {

namespace {

using nlohmann::json;

// ---- tolerant field access (version quirks must not throw) -----------------

double getD(const json& j, const char* key, double def) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<double>() : def;
}

int getI(const json& j, const char* key, int def) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_number()) ? it->get<int>() : def;
}

std::string getS(const json& j, const char* key, const std::string& def = {}) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_string()) ? it->get<std::string>() : def;
}

bool getB(const json& j, const char* key, bool def) {
    const auto it = j.find(key);
    return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
}

bool getVec3(const json& j, const char* key, glm::dvec3& out) {
    const auto it = j.find(key);
    if (it == j.end() || !it->is_array() || it->size() < 3)
        return false;
    for (int i = 0; i < 3; ++i)
        if (!(*it)[i].is_number())
            return false;
    out = glm::dvec3((*it)[0].get<double>(), (*it)[1].get<double>(),
                     (*it)[2].get<double>());
    return true;
}

bool parseJsonBytes(const std::vector<uint8_t>& bytes, json& out) {
    out = json::parse(bytes.begin(), bytes.end(), /*cb=*/nullptr,
                      /*allow_exceptions=*/false);
    return !out.is_discarded();
}

// OBB from an i3s "obb" object: center in layer-SR coordinates, halfSize in
// meters, quaternion [x,y,z,w] orienting the box axes (ECEF frame for global
// scenes, CRS frame for local ones).
bool parseObb(const json& obb, NodeInfo& node) {
    if (!obb.is_object())
        return false;
    glm::dvec3 center;
    if (!getVec3(obb, "center", center))
        return false;
    glm::dvec3 halfSize;
    if (!getVec3(obb, "halfSize", halfSize))
        return false;
    node.obbCenter = center;
    node.obbHalfSize = glm::vec3(halfSize);
    node.obbQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    const auto q = obb.find("quaternion");
    if (q != obb.end() && q->is_array() && q->size() >= 4 && (*q)[0].is_number()) {
        const glm::quat raw(static_cast<float>((*q)[3].get<double>()),  // w
                            static_cast<float>((*q)[0].get<double>()),  // x
                            static_cast<float>((*q)[1].get<double>()),  // y
                            static_cast<float>((*q)[2].get<double>())); // z
        // Normalize defensively: mat3_cast of a non-unit quaternion scales
        // the box axes (and a zero quaternion would NaN the whole tree).
        const float len2 = glm::dot(raw, raw);
        if (len2 > 1e-6f)
            node.obbQuat = raw * glm::inversesqrt(len2);
    }
    return true;
}

// 1.6 minimum bounding sphere [lon, lat, height, radiusMeters].
bool parseMbsCenter(const json& node16, glm::dvec3& outCenter, double& outRadius) {
    const auto it = node16.find("mbs");
    if (it == node16.end() || !it->is_array() || it->size() < 4)
        return false;
    for (int i = 0; i < 4; ++i)
        if (!(*it)[i].is_number())
            return false;
    outCenter = glm::dvec3((*it)[0].get<double>(), (*it)[1].get<double>(),
                           (*it)[2].get<double>());
    outRadius = (*it)[3].get<double>();
    return true;
}

// 1.6 MBS -> synthetic axis-aligned OBB (identity orientation, iso half-size).
bool parseMbsAsObb(const json& node16, NodeInfo& node) {
    glm::dvec3 center;
    double radius = 0.0;
    if (!parseMbsCenter(node16, center, radius))
        return false;
    node.obbCenter = center;
    node.obbHalfSize = glm::vec3(static_cast<float>(radius));
    node.obbQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    node.obbSynthesizedFromMbs = true;
    return true;
}

ScalarType scalarTypeFromString(const std::string& s) {
    if (s == "Float32") return ScalarType::Float32;
    if (s == "Float64") return ScalarType::Float64;
    if (s == "UInt8") return ScalarType::UInt8;
    if (s == "Int8") return ScalarType::Int8;
    if (s == "UInt16") return ScalarType::UInt16;
    if (s == "Int16") return ScalarType::Int16;
    if (s == "UInt32" || s == "Oid32") return ScalarType::UInt32;
    if (s == "Int32") return ScalarType::Int32;
    if (s == "UInt64" || s == "Oid64") return ScalarType::UInt64;
    if (s == "Int64") return ScalarType::Int64;
    return ScalarType::Unknown;
}

VertexSemantic vertexSemanticFromString(const std::string& s) {
    if (s == "position") return VertexSemantic::Position;
    if (s == "normal") return VertexSemantic::Normal;
    if (s == "uv0") return VertexSemantic::Uv0;
    if (s == "color") return VertexSemantic::Color;
    if (s == "uvRegion" || s == "region") return VertexSemantic::UvRegion;
    if (s == "featureId" || s == "id") return VertexSemantic::FeatureId;
    if (s == "faceRange") return VertexSemantic::FaceRange;
    return VertexSemantic::Unknown;
}

// One attribute value descriptor ({"type": "Float32", "component": 3} in
// 1.7+ geometry buffers; {"valueType": ..., "valuesPerElement": ...} in 1.6
// schemas) -> a GeometryStream.
bool parseStream(const json& j, VertexSemantic semantic, bool perFeature,
                 GeometryStream& out) {
    if (!j.is_object())
        return false;
    std::string type = getS(j, "type");
    if (type.empty())
        type = getS(j, "valueType");
    int components = getI(j, "component", getI(j, "valuesPerElement", 0));
    out.semantic = semantic;
    out.type = scalarTypeFromString(type);
    out.components = static_cast<uint8_t>(components);
    out.perFeature = perFeature || getS(j, "binding") == "per-feature";
    return out.type != ScalarType::Unknown && components > 0;
}

// 1.7+ geometryBuffers[] entry. Raw buffers store their streams in the
// spec's canonical order (position, normal, uv0, color, uvRegion, then the
// per-feature id + faceRange) regardless of JSON key order.
void parseGeometryBuffer17(const json& b, GeometryBufferDesc& out) {
    const auto compIt = b.find("compressedAttributes");
    if (compIt != b.end() && compIt->is_object()) {
        out.compressed = true;
        const auto attrsIt = compIt->find("attributes");
        if (attrsIt != compIt->end() && attrsIt->is_array())
            for (const json& a : *attrsIt)
                if (a.is_string())
                    out.compressedAttributes.push_back(a.get<std::string>());
        return;
    }

    // The 8-byte offset prefix is the (vertexCount, featureCount) header pair
    // 1.7 writers emit; any other nonzero offset is skipped as opaque padding
    // (counts then come from the node page).
    const int offset = getI(b, "offset", 0);
    out.rawOffset = static_cast<uint32_t>(offset < 0 ? 0 : offset);
    if (offset == 8) {
        out.header.push_back({ "vertexCount", ScalarType::UInt32 });
        out.header.push_back({ "featureCount", ScalarType::UInt32 });
    }

    struct KnownStream { const char* key; VertexSemantic semantic; bool perFeature; };
    static const KnownStream kOrder[] = {
        { "position", VertexSemantic::Position, false },
        { "normal", VertexSemantic::Normal, false },
        { "uv0", VertexSemantic::Uv0, false },
        { "color", VertexSemantic::Color, false },
        { "uvRegion", VertexSemantic::UvRegion, false },
        { "featureId", VertexSemantic::FeatureId, true },
        { "faceRange", VertexSemantic::FaceRange, true },
    };
    for (const KnownStream& k : kOrder) {
        const auto it = b.find(k.key);
        if (it == b.end())
            continue;
        GeometryStream stream;
        if (parseStream(*it, k.semantic, k.perFeature, stream))
            out.streams.push_back(stream);
    }
}

// 1.6 store.defaultGeometrySchema -> ONE raw buffer description (header +
// ordered vertex streams + ordered per-feature streams).
void parseGeometrySchema16(const json& schema, GeometryDefinition& outDef) {
    GeometryBufferDesc buf;

    const auto headerIt = schema.find("header");
    if (headerIt != schema.end() && headerIt->is_array()) {
        for (const json& h : *headerIt) {
            if (!h.is_object())
                continue;
            GeometryHeaderField field;
            field.property = getS(h, "property");
            field.type = scalarTypeFromString(getS(h, "type", getS(h, "valueType")));
            if (field.type != ScalarType::Unknown)
                buf.header.push_back(std::move(field));
        }
    }
    buf.rawOffset = static_cast<uint32_t>(buf.headerBytes());

    const auto attrsIt = schema.find("vertexAttributes");
    const auto orderIt = schema.find("ordering");
    if (attrsIt != schema.end() && attrsIt->is_object() && orderIt != schema.end() &&
        orderIt->is_array()) {
        for (const json& key : *orderIt) {
            if (!key.is_string())
                continue;
            const std::string name = key.get<std::string>();
            const auto a = attrsIt->find(name);
            if (a == attrsIt->end())
                continue;
            GeometryStream stream;
            if (parseStream(*a, vertexSemanticFromString(name), false, stream))
                buf.streams.push_back(stream);
        }
    }

    const auto featAttrsIt = schema.find("featureAttributes");
    const auto featOrderIt = schema.find("featureAttributeOrder");
    if (featAttrsIt != schema.end() && featAttrsIt->is_object() &&
        featOrderIt != schema.end() && featOrderIt->is_array()) {
        for (const json& key : *featOrderIt) {
            if (!key.is_string())
                continue;
            const std::string name = key.get<std::string>();
            const auto a = featAttrsIt->find(name);
            if (a == featAttrsIt->end())
                continue;
            GeometryStream stream;
            if (parseStream(*a, vertexSemanticFromString(name), true, stream))
                buf.streams.push_back(stream);
        }
    }

    outDef.buffers.push_back(std::move(buf));
}

// glTF-style texture reference: {"textureSetDefinitionId": 0}.
int parseTextureRef(const json& material, const char* key) {
    const auto it = material.find(key);
    if (it == material.end() || !it->is_object())
        return -1;
    return getI(*it, "textureSetDefinitionId", -1);
}

// 1.7+ materialDefinitions[] entry (glTF metallic-roughness style).
MaterialDesc parseMaterialDefinition17(const json& m) {
    MaterialDesc desc;
    desc.doubleSided = getB(m, "doubleSided", false);
    desc.alphaCutoff = static_cast<float>(getD(m, "alphaCutoff", 0.25));
    desc.normalTexture = parseTextureRef(m, "normalTexture");
    desc.occlusionTexture = parseTextureRef(m, "occlusionTexture");
    desc.emissiveTexture = parseTextureRef(m, "emissiveTexture");

    const auto emIt = m.find("emissiveFactor");
    if (emIt != m.end() && emIt->is_array() && emIt->size() >= 3 &&
        (*emIt)[0].is_number()) {
        desc.emissiveFactor = glm::vec3(static_cast<float>((*emIt)[0].get<double>()),
                                        static_cast<float>((*emIt)[1].get<double>()),
                                        static_cast<float>((*emIt)[2].get<double>()));
    }

    const auto pbrIt = m.find("pbrMetallicRoughness");
    if (pbrIt != m.end() && pbrIt->is_object()) {
        const json& pbr = *pbrIt;
        const auto bcIt = pbr.find("baseColorFactor");
        if (bcIt != pbr.end() && bcIt->is_array() && bcIt->size() >= 4 &&
            (*bcIt)[0].is_number()) {
            desc.baseColor =
                glm::vec4(static_cast<float>((*bcIt)[0].get<double>()),
                          static_cast<float>((*bcIt)[1].get<double>()),
                          static_cast<float>((*bcIt)[2].get<double>()),
                          static_cast<float>((*bcIt)[3].get<double>()));
        }
        // glTF defaults are metallic=1/roughness=1 INSIDE a pbr block; keep
        // the friendlier dielectric default only when the block is absent.
        desc.metallicFactor = static_cast<float>(getD(pbr, "metallicFactor", 1.0));
        desc.roughnessFactor = static_cast<float>(getD(pbr, "roughnessFactor", 1.0));
        desc.baseColorTexture = parseTextureRef(pbr, "baseColorTexture");
        desc.metallicRoughnessTexture =
            parseTextureRef(pbr, "metallicRoughnessTexture");
    }
    return desc;
}

LodMetric lodMetricFromString(const std::string& s) {
    if (s == "maxScreenThresholdSQ")
        return LodMetric::MaxScreenThresholdSQ;
    if (s == "maxScreenThreshold" || s == "screenSpaceRelative")
        return LodMetric::MaxScreenThreshold;
    if (s == "distanceRangeFromDefaultCamera")
        return LodMetric::DistanceRangeFromDefaultCamera;
    if (s == "density-threshold")
        return LodMetric::DensityThreshold;
    return LodMetric::Unknown;
}

// Resolve a 1.6 href ("../0-1", "./nodes/root") against a base directory
// inside the archive; collapses "." and "..".
std::string resolveHref(const std::string& baseDir, const std::string& href) {
    std::vector<std::string> parts;
    auto push = [&parts](const std::string& s, size_t from, size_t to) {
        if (to <= from)
            return;
        const std::string seg = s.substr(from, to - from);
        if (seg == ".")
            return;
        if (seg == "..") {
            if (!parts.empty())
                parts.pop_back();
            return;
        }
        parts.push_back(seg);
    };
    auto split = [&push](const std::string& s) {
        size_t start = 0;
        for (size_t i = 0; i <= s.size(); ++i) {
            if (i == s.size() || s[i] == '/' || s[i] == '\\') {
                push(s, start, i);
                start = i + 1;
            }
        }
    };
    split(baseDir);
    split(href);
    std::string out;
    for (size_t i = 0; i < parts.size(); ++i) {
        if (i)
            out.push_back('/');
        out += parts[i];
    }
    return out;
}

// Fills level / levelCount / levelNodeCounts by BFS over the child ranges.
// Also validates connectivity: returns the number of reachable nodes.
size_t computeLevels(I3SNodeTree& tree) {
    if (tree.nodes.empty())
        return 0;
    size_t reached = 0;
    std::deque<uint32_t> queue;
    std::vector<bool> seen(tree.nodes.size(), false);
    tree.nodes[0].level = 0;
    seen[0] = true;
    queue.push_back(0);
    uint16_t maxLevel = 0;
    while (!queue.empty()) {
        const uint32_t idx = queue.front();
        queue.pop_front();
        ++reached;
        const NodeInfo& n = tree.nodes[idx];
        maxLevel = std::max(maxLevel, n.level);
        for (uint32_t c = 0; c < n.childCount; ++c) {
            const uint32_t child = tree.childIndices[n.firstChild + c];
            if (child >= tree.nodes.size() || seen[child])
                continue;
            seen[child] = true;
            tree.nodes[child].level = static_cast<uint16_t>(n.level + 1);
            if (tree.nodes[child].parent < 0)
                tree.nodes[child].parent = static_cast<int32_t>(idx);
            queue.push_back(child);
        }
    }
    tree.levelCount = static_cast<uint16_t>(maxLevel + 1);
    tree.levelNodeCounts.assign(tree.levelCount, 0);
    for (size_t i = 0; i < tree.nodes.size(); ++i)
        if (seen[i])
            ++tree.levelNodeCounts[tree.nodes[i].level];
    return reached;
}

// ---- node tree: 1.7+ mesh node pages ----------------------------------------

bool loadMeshNodePages(const SlpkArchive& archive, const LayerInfo& info,
                       I3SNodeTree& out, std::string& error) {
    const int perPage = std::max(1, info.nodesPerPage);
    std::vector<uint8_t> bytes;
    for (int page = 0;; ++page) {
        const std::string path = "nodepages/" + std::to_string(page) + ".json";
        if (!archive.read(path, bytes)) {
            if (page == 0) {
                error = "node page 0 missing (declared nodePages, version " +
                        info.version + ")";
                return false;
            }
            break;
        }
        json doc;
        if (!parseJsonBytes(bytes, doc)) {
            error = "node page " + std::to_string(page) + " is not valid JSON";
            return false;
        }
        const auto nodesIt = doc.find("nodes");
        if (nodesIt == doc.end() || !nodesIt->is_array()) {
            error = "node page " + std::to_string(page) + " has no nodes[]";
            return false;
        }
        size_t inPage = 0;
        for (const json& jn : *nodesIt) {
            NodeInfo node;
            const auto obbIt = jn.find("obb");
            if (obbIt == jn.end() || !parseObb(*obbIt, node)) {
                error = "node page " + std::to_string(page) +
                        ": node without a valid obb";
                return false;
            }
            node.geomCenter = node.obbCenter; // 1.7+: geometry deltas ref the OBB
            node.lodThreshold = getD(jn, "lodThreshold", 0.0);
            node.parent = getI(jn, "parentIndex", -1);

            node.firstChild = static_cast<uint32_t>(out.childIndices.size());
            const auto childIt = jn.find("children");
            if (childIt != jn.end() && childIt->is_array()) {
                for (const json& c : *childIt)
                    if (c.is_number())
                        out.childIndices.push_back(c.get<uint32_t>());
            }
            node.childCount =
                static_cast<uint32_t>(out.childIndices.size()) - node.firstChild;

            const auto meshIt = jn.find("mesh");
            if (meshIt != jn.end() && meshIt->is_object()) {
                const auto geomIt = meshIt->find("geometry");
                if (geomIt != meshIt->end() && geomIt->is_object()) {
                    node.mesh.hasGeometry = true;
                    node.mesh.geometryDefinition = getI(*geomIt, "definition", -1);
                    node.mesh.geometryResource =
                        static_cast<uint32_t>(getI(*geomIt, "resource", 0));
                    node.mesh.vertexCount =
                        static_cast<uint64_t>(getD(*geomIt, "vertexCount", 0.0));
                    node.mesh.featureCount =
                        static_cast<uint64_t>(getD(*geomIt, "featureCount", 0.0));
                }
                const auto matIt = meshIt->find("material");
                if (matIt != meshIt->end() && matIt->is_object()) {
                    node.mesh.materialDefinition = getI(*matIt, "definition", -1);
                    node.mesh.materialResource =
                        static_cast<uint32_t>(getI(*matIt, "resource", 0));
                }
                const auto attrIt = meshIt->find("attribute");
                if (attrIt != meshIt->end() && attrIt->is_object())
                    node.mesh.attributeResource =
                        static_cast<uint32_t>(getI(*attrIt, "resource", 0));
            }

            out.nodes.push_back(std::move(node));
            ++inPage;
        }
        if (inPage < static_cast<size_t>(perPage))
            break; // partial page = last page
    }
    return true;
}

// ---- node tree: PCSL 2.0 node pages (implicit child ranges) ------------------

bool loadPointCloudNodePages(const SlpkArchive& archive, const LayerInfo& info,
                             I3SNodeTree& out, std::string& error) {
    const int perPage = std::max(1, info.nodesPerPage);
    std::vector<uint8_t> bytes;
    for (int page = 0;; ++page) {
        const std::string path = "nodepages/" + std::to_string(page) + ".json";
        if (!archive.read(path, bytes)) {
            if (page == 0) {
                error = "point-cloud node page 0 missing (PCSL " + info.version + ")";
                return false;
            }
            break;
        }
        json doc;
        if (!parseJsonBytes(bytes, doc)) {
            error = "node page " + std::to_string(page) + " is not valid JSON";
            return false;
        }
        const auto nodesIt = doc.find("nodes");
        if (nodesIt == doc.end() || !nodesIt->is_array()) {
            error = "node page " + std::to_string(page) + " has no nodes[]";
            return false;
        }
        size_t inPage = 0;
        for (const json& jn : *nodesIt) {
            NodeInfo node;
            const auto obbIt = jn.find("obb");
            if (obbIt == jn.end() || !parseObb(*obbIt, node)) {
                error = "node page " + std::to_string(page) +
                        ": node without a valid obb";
                return false;
            }
            node.geomCenter = node.obbCenter;
            node.lodThreshold = getD(jn, "lodThreshold", 0.0);
            // PCSL children are an implicit contiguous range in the flat array.
            const uint32_t firstChild =
                static_cast<uint32_t>(getD(jn, "firstChild", 0.0));
            const uint32_t childCount =
                static_cast<uint32_t>(getD(jn, "childCount", 0.0));
            node.firstChild = static_cast<uint32_t>(out.childIndices.size());
            for (uint32_t c = 0; c < childCount; ++c)
                out.childIndices.push_back(firstChild + c);
            node.childCount = childCount;
            node.mesh.hasGeometry = true;
            node.mesh.geometryResource =
                static_cast<uint32_t>(getI(jn, "resourceId", static_cast<int>(out.nodes.size())));
            node.mesh.vertexCount = static_cast<uint64_t>(getD(jn, "vertexCount", 0.0));
            out.nodes.push_back(std::move(node));
            ++inPage;
        }
        if (inPage < static_cast<size_t>(perPage))
            break;
    }
    return true;
}

// ---- node tree: 1.6 per-node index documents ---------------------------------

bool loadNodeDocuments16(const SlpkArchive& archive, const LayerInfo& info,
                         I3SNodeTree& out, std::string& error) {
    std::string rootDir = info.rootNodePath.empty() ? std::string("nodes/root")
                                                    : info.rootNodePath;
    rootDir = resolveHref("", rootDir);

    struct Pending {
        std::string dir;   // "nodes/<id>"
        int32_t parent;    // parent node index (-1 for root)
        uint32_t selfIndex; // pre-assigned index in out.nodes
    };

    // BFS with pre-assigned indices: children get their slots (and the parent
    // its child range) when the PARENT document is processed; each child doc
    // then fills its own slot. Keeps child ranges contiguous.
    std::deque<Pending> queue;
    out.nodes.emplace_back();
    queue.push_back(Pending{ rootDir, -1, 0 });

    // A node directory may be referenced once only — an href cycle (or a
    // shared child) in a malformed package would otherwise spin this BFS
    // forever, minting fresh node slots each lap.
    std::unordered_set<std::string> visitedDirs;
    visitedDirs.insert(rootDir);

    std::vector<uint8_t> bytes;
    while (!queue.empty()) {
        const Pending item = queue.front();
        queue.pop_front();

        const std::string docPath = item.dir + "/3dNodeIndexDocument.json";
        if (!archive.read(docPath, bytes)) {
            error = "missing node document: " + docPath;
            return false;
        }
        json doc;
        if (!parseJsonBytes(bytes, doc)) {
            error = "node document is not valid JSON: " + docPath;
            return false;
        }

        NodeInfo& node = out.nodes[item.selfIndex];
        node.parent = item.parent;
        node.v16Id = getS(doc, "id");

        const auto obbIt = doc.find("obb");
        bool haveBounds = obbIt != doc.end() && parseObb(*obbIt, node);
        if (!haveBounds)
            haveBounds = parseMbsAsObb(doc, node);
        if (!haveBounds) {
            error = "node without obb/mbs: " + docPath;
            return false;
        }
        // 1.6 geometry deltas reference the MBS center (which can differ from
        // the OBB center); fall back to the box center without one.
        double mbsRadius = 0.0;
        if (!parseMbsCenter(doc, node.geomCenter, mbsRadius))
            node.geomCenter = node.obbCenter;

        // lodSelection: prefer the layer's metric, else take what's there.
        const auto lodIt = doc.find("lodSelection");
        if (lodIt != doc.end() && lodIt->is_array()) {
            double fallback = 0.0;
            bool matched = false;
            for (const json& sel : *lodIt) {
                if (!sel.is_object())
                    continue;
                const LodMetric metric = lodMetricFromString(getS(sel, "metricType"));
                const double maxError = getD(sel, "maxError", 0.0);
                if (metric == info.lodMetric && metric != LodMetric::Unknown) {
                    node.lodThreshold = maxError;
                    matched = true;
                    break;
                }
                if (fallback == 0.0)
                    fallback = maxError;
            }
            if (!matched && node.lodThreshold == 0.0)
                node.lodThreshold = fallback;
        }

        const auto geomIt = doc.find("geometryData");
        node.mesh.hasGeometry =
            geomIt != doc.end() && geomIt->is_array() && !geomIt->empty();
        if (node.mesh.hasGeometry) {
            node.mesh.geometryDefinition = 0; // 1.6: the one defaultGeometrySchema
            const std::string href =
                (*geomIt)[0].is_object() ? getS((*geomIt)[0], "href") : std::string();
            node.mesh.v16GeometryPath =
                resolveHref(item.dir, href.empty() ? "./geometries/0" : href);
        }
        const auto texDataIt = doc.find("textureData");
        if (texDataIt != doc.end() && texDataIt->is_array() && !texDataIt->empty() &&
            (*texDataIt)[0].is_object()) {
            const std::string href = getS((*texDataIt)[0], "href");
            if (!href.empty())
                node.mesh.v16TexturePath = resolveHref(item.dir, href);
        }
        node.mesh.vertexCount = static_cast<uint64_t>(getD(doc, "vertexCount", 0.0));
        node.mesh.featureCount = static_cast<uint64_t>(getD(doc, "featureCount", 0.0));

        // Register children: assign contiguous indices now, enqueue their docs.
        node.firstChild = static_cast<uint32_t>(out.childIndices.size());
        const auto childIt = doc.find("children");
        if (childIt != doc.end() && childIt->is_array()) {
            for (const json& c : *childIt) {
                if (!c.is_object())
                    continue;
                const std::string childId = getS(c, "id");
                const std::string href = getS(c, "href");
                std::string childDir;
                if (!href.empty())
                    childDir = resolveHref(item.dir, href);
                else if (!childId.empty())
                    childDir = "nodes/" + childId;
                else
                    continue;
                if (!visitedDirs.insert(childDir).second)
                    continue; // cycle / duplicate reference — drop it
                const uint32_t childIndex = static_cast<uint32_t>(out.nodes.size());
                out.nodes.emplace_back();
                out.childIndices.push_back(childIndex);
                queue.push_back(Pending{ childDir,
                                         static_cast<int32_t>(item.selfIndex),
                                         childIndex });
            }
        }
        // NOTE: out.nodes may have reallocated inside the loop above — 'node'
        // must not be used past this point. childCount is set via index:
        out.nodes[item.selfIndex].childCount =
            static_cast<uint32_t>(out.childIndices.size()) -
            out.nodes[item.selfIndex].firstChild;
    }
    return true;
}

} // namespace

// ---- layer document ----------------------------------------------------------

bool I3SLayer::parseLayerInfo(const SlpkArchive& archive, LayerInfo& out,
                              std::string& error) {
    out = LayerInfo{};

    std::vector<uint8_t> bytes;
    if (!archive.read("3dSceneLayer.json", bytes)) {
        error = "3dSceneLayer.json(.gz) not found — not a scene layer package "
                "(BSL sub-layer packages are not supported yet)";
        return false;
    }
    json doc;
    if (!parseJsonBytes(bytes, doc)) {
        error = "3dSceneLayer.json is not valid JSON";
        return false;
    }

    out.name = getS(doc, "name");
    out.typeString = getS(doc, "layerType");
    if (out.typeString == "3DObject")
        out.type = LayerType::Object3D;
    else if (out.typeString == "IntegratedMesh")
        out.type = LayerType::IntegratedMesh;
    else if (out.typeString == "PointCloud")
        out.type = LayerType::PointCloud;
    else if (out.typeString == "Point")
        out.type = LayerType::Point;
    else if (out.typeString == "Building")
        out.type = LayerType::Building;

    const auto srIt = doc.find("spatialReference");
    if (srIt != doc.end() && srIt->is_object()) {
        out.sr.wkid = getI(*srIt, "wkid", 0);
        out.sr.latestWkid = getI(*srIt, "latestWkid", 0);
        out.sr.vcsWkid = getI(*srIt, "vcsWkid", 0);
        out.sr.wkt = getS(*srIt, "wkt");
    }

    const auto storeIt = doc.find("store");
    if (storeIt != doc.end() && storeIt->is_object()) {
        const json& store = *storeIt;
        out.version = getS(store, "version");
        out.profile = getS(store, "profile");
        out.rootNodePath = getS(store, "rootNode");

        const std::string frame = getS(store, "normalReferenceFrame");
        if (frame == "east-north-up")
            out.normalFrame = NormalReferenceFrame::EastNorthUp;
        else if (frame == "earth-centered")
            out.normalFrame = NormalReferenceFrame::EarthCentered;
        else if (frame == "vertex-reference-frame")
            out.normalFrame = NormalReferenceFrame::VertexReferenceFrame;

        // 1.6 texture encodings ("image/jpeg", "image/vnd-ms.dds", ...).
        const auto encIt = store.find("textureEncoding");
        if (encIt != store.end() && encIt->is_array()) {
            for (const json& e : *encIt) {
                if (!e.is_string())
                    continue;
                if (!out.textureEncodingSummary.empty())
                    out.textureEncodingSummary += ", ";
                out.textureEncodingSummary += e.get<std::string>();
            }
        }

        // Point-cloud layers keep their paging info under store.index; the
        // page size key changed between PCSL versions ("nodePerIndexBlock" in
        // 1.x stores, "nodesPerPage" in 2.0).
        const auto indexIt = store.find("index");
        if (indexIt != store.end() && indexIt->is_object()) {
            out.hasNodePages = true;
            out.nodesPerPage = getI(*indexIt, "nodesPerPage",
                                    getI(*indexIt, "nodePerIndexBlock", 64));
            out.lodMetric =
                lodMetricFromString(getS(*indexIt, "lodSelectionMetricType"));
        }

        // store.extent [xmin, ymin, xmax, ymax] — z filled by fullExtent below.
        const auto extIt = store.find("extent");
        if (extIt != store.end() && extIt->is_array() && extIt->size() >= 4 &&
            (*extIt)[0].is_number()) {
            out.hasFullExtent = true;
            out.fullExtent[0] = (*extIt)[0].get<double>();
            out.fullExtent[1] = (*extIt)[1].get<double>();
            out.fullExtent[2] = (*extIt)[2].get<double>();
            out.fullExtent[3] = (*extIt)[3].get<double>();
        }
    }

    const auto verIt = doc.find("version"); // PCSL keeps version at layer level
    if (out.version.empty() && verIt != doc.end() && verIt->is_string())
        out.version = verIt->get<std::string>();
    if (!out.version.empty()) {
        const size_t dot = out.version.find('.');
        out.versionMajor = std::atoi(out.version.c_str());
        if (dot != std::string::npos)
            out.versionMinor = std::atoi(out.version.c_str() + dot + 1);
    }

    // Version defaults when store.normalReferenceFrame is absent (loaders.gl
    // behaviour, needed for correct lighting in M1): 1.6-era global scenes
    // wrote ECEF normals, 1.7+ node-local east-north-up.
    if (out.normalFrame == NormalReferenceFrame::Unknown) {
        if (!out.sr.isGeographic())
            out.normalFrame = NormalReferenceFrame::VertexReferenceFrame;
        else if (out.versionMajor == 1 && out.versionMinor <= 6)
            out.normalFrame = NormalReferenceFrame::EarthCentered;
        else
            out.normalFrame = NormalReferenceFrame::EastNorthUp;
    }

    // 1.7+ mesh profiles: nodePages at layer level.
    const auto npIt = doc.find("nodePages");
    if (npIt != doc.end() && npIt->is_object()) {
        out.hasNodePages = true;
        out.nodesPerPage = getI(*npIt, "nodesPerPage", 64);
        out.lodMetric = lodMetricFromString(getS(*npIt, "lodSelectionMetricType"));
    }
    if (out.lodMetric == LodMetric::Unknown && !out.hasNodePages)
        out.lodMetric = LodMetric::MaxScreenThreshold; // 1.6 common default

    const auto feIt = doc.find("fullExtent");
    if (feIt != doc.end() && feIt->is_object()) {
        out.hasFullExtent = true;
        out.fullExtent[0] = getD(*feIt, "xmin", 0.0);
        out.fullExtent[1] = getD(*feIt, "ymin", 0.0);
        out.fullExtent[2] = getD(*feIt, "xmax", 0.0);
        out.fullExtent[3] = getD(*feIt, "ymax", 0.0);
        out.fullExtent[4] = getD(*feIt, "zmin", 0.0);
        out.fullExtent[5] = getD(*feIt, "zmax", 0.0);
    }

    const auto attrIt = doc.find("attributeStorageInfo");
    if (attrIt != doc.end() && attrIt->is_array()) {
        for (const json& a : *attrIt) {
            if (!a.is_object())
                continue;
            AttributeField field;
            field.key = getS(a, "key");
            field.name = getS(a, "name");
            const auto valIt = a.find("attributeValues");
            if (valIt != a.end() && valIt->is_object())
                field.valueType = getS(*valIt, "valueType");
            out.attributeFields.push_back(std::move(field));
        }
    }

    const auto texIt = doc.find("textureSetDefinitions");
    if (texIt != doc.end() && texIt->is_array()) {
        for (const json& t : *texIt) {
            if (!t.is_object())
                continue;
            TextureSetDefinition def;
            def.atlas = getB(t, "atlas", false);
            const auto fmtIt = t.find("formats");
            if (fmtIt != t.end() && fmtIt->is_array()) {
                for (const json& f : *fmtIt) {
                    if (!f.is_object())
                        continue;
                    TextureFormat fmt;
                    fmt.name = getS(f, "name");
                    fmt.format = getS(f, "format");
                    def.formats.push_back(std::move(fmt));
                }
            }
            out.textureSets.push_back(std::move(def));
        }
    }

    const auto geoIt = doc.find("geometryDefinitions");
    if (geoIt != doc.end() && geoIt->is_array()) {
        for (const json& g : *geoIt) {
            GeometryDefinition def;
            if (g.is_object()) {
                const auto bufIt = g.find("geometryBuffers");
                if (bufIt != g.end() && bufIt->is_array()) {
                    for (const json& b : *bufIt) {
                        if (!b.is_object())
                            continue;
                        GeometryBufferDesc desc;
                        parseGeometryBuffer17(b, desc);
                        def.buffers.push_back(std::move(desc));
                    }
                }
            }
            out.geometryDefs.push_back(std::move(def));
        }
    } else if (storeIt != doc.end() && storeIt->is_object()) {
        // 1.6: ONE schema for every node, under store.defaultGeometrySchema.
        const auto schemaIt = storeIt->find("defaultGeometrySchema");
        if (schemaIt != storeIt->end() && schemaIt->is_object() &&
            out.type != LayerType::PointCloud) {
            GeometryDefinition def;
            parseGeometrySchema16(*schemaIt, def);
            if (!def.buffers.empty() && !def.buffers[0].streams.empty())
                out.geometryDefs.push_back(std::move(def));
        }
    }

    const auto matIt = doc.find("materialDefinitions");
    if (matIt != doc.end() && matIt->is_array())
        for (const json& m : *matIt)
            out.materials.push_back(m.is_object() ? parseMaterialDefinition17(m)
                                                  : MaterialDesc{});

    const auto statIt = doc.find("statisticsInfo");
    out.hasStatistics = statIt != doc.end() && statIt->is_array() && !statIt->empty();

    if (out.type == LayerType::Unknown) {
        error = "unsupported layerType \"" + out.typeString + "\" (version " +
                out.version + ")";
        return false;
    }
    return true;
}

// ---- node tree dispatch --------------------------------------------------------

bool I3SLayer::loadNodeTree(const SlpkArchive& archive, const LayerInfo& info,
                            I3SNodeTree& out, std::string& error) {
    out = I3SNodeTree{};

    bool ok = false;
    if (info.type == LayerType::PointCloud)
        ok = loadPointCloudNodePages(archive, info, out, error);
    else if (info.hasNodePages)
        ok = loadMeshNodePages(archive, info, out, error);
    else if (!info.rootNodePath.empty() || archive.exists("nodes/root/3dNodeIndexDocument.json"))
        ok = loadNodeDocuments16(archive, info, out, error);
    else {
        error = "no nodePages and no 1.6 root node document (version " +
                info.version + ", profile " + info.profile + ")";
        return false;
    }
    if (!ok)
        return false;

    if (out.nodes.empty()) {
        error = "node tree is empty";
        return false;
    }

    // Sanitize child references: traversal (and the scene layer's box array)
    // index these unchecked, so out-of-range indices from a corrupt page set
    // must not survive. Requiring child > parent index also breaks any cycle
    // a hostile file could encode (every real writer lays pages out
    // BFS-ordered — root first, children strictly after their parent — and
    // the 1.6 walk below builds indices that way by construction), which
    // guarantees traversal termination.
    for (size_t i = 0; i < out.nodes.size(); ++i) {
        NodeInfo& n = out.nodes[i];
        if (n.firstChild > out.childIndices.size() ||
            n.childCount > out.childIndices.size() - n.firstChild) {
            n.firstChild = 0;
            n.childCount = 0;
            continue;
        }
        uint32_t kept = 0;
        for (uint32_t c = 0; c < n.childCount; ++c) {
            const uint32_t child = out.childIndices[n.firstChild + c];
            if (child > i && child < out.nodes.size())
                out.childIndices[n.firstChild + kept++] = child;
        }
        n.childCount = kept;
    }

    const size_t reached = computeLevels(out);
    if (reached == 0) {
        error = "root node unreachable (corrupt hierarchy)";
        return false;
    }
    return true;
}

// ---- 1.6 shared resource (materials + texture definitions) --------------------

bool I3SLayer::parse16SharedResource(const SlpkArchive& archive,
                                     const I3SNodeTree& tree, LayerInfo& info,
                                     std::string& error) {
    if (!info.materials.empty())
        return true; // 1.7+ (layer-level definitions) or already parsed

    // The definitions are per-node but shared in practice; adopt the first
    // geometry node's document layer-wide (M1 simplification — see the plan).
    const NodeInfo* geomNode = nullptr;
    for (const NodeInfo& n : tree.nodes) {
        if (n.mesh.hasGeometry && !n.mesh.v16GeometryPath.empty()) {
            geomNode = &n;
            break;
        }
    }
    if (!geomNode)
        return true; // nothing to render, nothing to parse

    // node dir = ".../geometries/<x>" minus the last two segments; fall back
    // to the id-derived dir when the href pointed somewhere unexpected.
    std::string nodeDir = geomNode->mesh.v16GeometryPath;
    const size_t geoPos = nodeDir.rfind("/geometries/");
    if (geoPos != std::string::npos)
        nodeDir.resize(geoPos);
    else if (!geomNode->v16Id.empty())
        nodeDir = "nodes/" + geomNode->v16Id;

    std::vector<uint8_t> bytes;
    if (!archive.read(nodeDir + "/shared/sharedResource.json", bytes))
        return true; // optional resource; default material applies
    json doc;
    if (!parseJsonBytes(bytes, doc)) {
        error = "sharedResource.json is not valid JSON (" + nodeDir + ")";
        return false;
    }

    MaterialDesc desc;
    const auto matsIt = doc.find("materialDefinitions");
    if (matsIt != doc.end() && matsIt->is_object() && !matsIt->empty()) {
        const json& m = *matsIt->begin(); // single definition in practice
        const auto paramsIt = m.is_object() ? m.find("params") : m.end();
        if (paramsIt != m.end() && paramsIt->is_object()) {
            const json& p = *paramsIt;
            glm::dvec3 diffuse;
            if (getVec3(p, "diffuse", diffuse))
                desc.baseColor = glm::vec4(glm::vec3(diffuse), 1.0f);
            const double transparency = getD(p, "transparency", 0.0);
            if (transparency > 0.0 && transparency <= 1.0)
                desc.baseColor.a = static_cast<float>(1.0 - transparency);
            desc.doubleSided = getS(p, "cullFace", "none") == "none";
        }
    }
    const auto texIt = doc.find("textureDefinitions");
    if (texIt != doc.end() && texIt->is_object() && !texIt->empty()) {
        // Per-node image resources; the sentinel 0 means "the node's own
        // texture" (NodeMesh::v16TexturePath).
        desc.baseColorTexture = 0;
        const json& t = *texIt->begin();
        if (t.is_object()) {
            TextureSetDefinition set;
            set.atlas = getB(t, "atlas", false);
            const auto encIt = t.find("encoding");
            if (encIt != t.end() && encIt->is_array()) {
                for (const json& e : *encIt) {
                    if (!e.is_string())
                        continue;
                    const std::string enc = e.get<std::string>();
                    TextureFormat fmt;
                    if (enc == "image/jpeg" || enc == "image/jpg")
                        fmt.format = "jpg";
                    else if (enc == "image/png")
                        fmt.format = "png";
                    else if (enc == "image/vnd-ms.dds")
                        fmt.format = "dds";
                    else if (enc == "image/ktx2")
                        fmt.format = "ktx2";
                    else
                        continue;
                    set.formats.push_back(std::move(fmt));
                }
            }
            if (info.textureSets.empty())
                info.textureSets.push_back(std::move(set));
        }
    }
    info.materials.push_back(desc);
    return true;
}

} // namespace i3s
