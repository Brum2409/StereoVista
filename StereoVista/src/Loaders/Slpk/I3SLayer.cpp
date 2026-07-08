#include "Loaders/Slpk/I3SLayer.h"

#include "Loaders/Slpk/SlpkArchive.h"

#include <json.h>

#include <algorithm>
#include <cstdlib>
#include <deque>
#include <string>
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
    const auto q = obb.find("quaternion");
    if (q != obb.end() && q->is_array() && q->size() >= 4 && (*q)[0].is_number()) {
        node.obbQuat = glm::quat(static_cast<float>((*q)[3].get<double>()),  // w
                                 static_cast<float>((*q)[0].get<double>()),  // x
                                 static_cast<float>((*q)[1].get<double>()),  // y
                                 static_cast<float>((*q)[2].get<double>())); // z
    } else {
        node.obbQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    }
    return true;
}

// 1.6 minimum bounding sphere [lon, lat, height, radiusMeters] -> synthetic
// axis-aligned OBB (identity orientation, iso half-size).
bool parseMbsAsObb(const json& node16, NodeInfo& node) {
    const auto it = node16.find("mbs");
    if (it == node16.end() || !it->is_array() || it->size() < 4)
        return false;
    for (int i = 0; i < 4; ++i)
        if (!(*it)[i].is_number())
            return false;
    node.obbCenter = glm::dvec3((*it)[0].get<double>(), (*it)[1].get<double>(),
                                (*it)[2].get<double>());
    const float r = static_cast<float>((*it)[3].get<double>());
    node.obbHalfSize = glm::vec3(r);
    node.obbQuat = glm::quat(1.0f, 0.0f, 0.0f, 0.0f);
    node.obbSynthesizedFromMbs = true;
    return true;
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
            if (fmtIt != t.end() && fmtIt->is_array())
                for (const json& f : *fmtIt)
                    if (f.is_object())
                        def.formats.push_back(getS(f, "format"));
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
                        if (b.contains("compressedAttributes")) {
                            def.hasCompressed = true;
                            continue;
                        }
                        def.hasRaw = true;
                        def.hasNormal = def.hasNormal || b.contains("normal");
                        def.hasUv0 = def.hasUv0 || b.contains("uv0");
                        def.hasColor = def.hasColor || b.contains("color");
                        def.hasUvRegion = def.hasUvRegion || b.contains("uvRegion");
                        def.hasFeatureId = def.hasFeatureId || b.contains("featureId");
                    }
                }
            }
            out.geometryDefs.push_back(def);
        }
    }

    const auto matIt = doc.find("materialDefinitions");
    if (matIt != doc.end() && matIt->is_array())
        out.materialCount = matIt->size();

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

    const size_t reached = computeLevels(out);
    if (reached == 0) {
        error = "root node unreachable (corrupt hierarchy)";
        return false;
    }
    return true;
}

} // namespace i3s
