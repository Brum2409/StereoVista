#pragma once

// ============================================================================
// i3s::I3SAttributes — per-node attribute column reader (M4).
// ----------------------------------------------------------------------------
// Reads the binary per-field columns referenced by attributeStorageInfo:
//   1.7+ : nodes/<attributeResource>/attributes/<key>/0.bin.gz
//   1.6  : nodes/<id>/attributes/<key>/0.bin.gz (dir derived from the node's
//          resolved geometry path)
// Values are in the node's feature order (same order as GeometryData
// featureIds / the draco feature-index attribute), so column[featureIndex]
// is the picked feature's value — no per-feature lookup structure needed.
//
// Binary layout (validated against real ArcGIS packages + loaders.gl):
//   * declared header fields in order (numeric: {count}; strings:
//     {count, attributeValuesByteCount});
//   * one section per `ordering` entry ("attributeValues",
//     "attributeByteCounts", "ObjectIds");
//   * 8-byte scalar sections (Float64/Int64/UInt64/Oid64) are aligned to 8
//     bytes — ArcGIS pads 4 bytes after a 4-byte count header;
//   * string byte counts INCLUDE the null terminator; values are
//     concatenated UTF-8.
//
// Pure CPU + archive reads — worker-thread safe. No Vulkan, no renderer types.
// ============================================================================

#include "Loaders/Slpk/SlpkTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace i3s {

class SlpkArchive;

// One decoded column. Numeric values (including OIDs) widen to double for
// display/filter use — exact below 2^53, which covers every real OID scheme;
// strings stay strings. Multi-component fields (valuesPerElement > 1) store
// components back-to-back: element i spans [i*vpe, (i+1)*vpe).
struct AttributeColumn {
    bool isString = false;
    uint8_t valuesPerElement = 1;
    std::vector<double> numbers;
    std::vector<std::string> strings;

    size_t count() const {
        return isString ? strings.size()
                        : numbers.size() / (valuesPerElement ? valuesPerElement : 1);
    }
    // Element i formatted for display ("<null>"-safe, components
    // comma-joined). Returns "" when out of range.
    std::string displayValue(size_t index) const;
};

class I3SAttributes {
public:
    // Archive directory holding the node's attribute columns
    // ("nodes/<resource>/attributes"), or "" when the node has none.
    static std::string attributeDir(const LayerInfo& info, const NodeInfo& node);

    // Reads + decodes one field's column for the node. False + reason on a
    // malformed column; a missing resource fails with an "not in archive"
    // reason (group nodes and empty fields simply ship no column).
    static bool readColumn(const SlpkArchive& archive, const LayerInfo& info,
                           const NodeInfo& node, const AttributeField& field,
                           AttributeColumn& out, std::string& error);

    // Buffer-level decode (used by readColumn and the test harness).
    static bool decodeColumn(const uint8_t* data, size_t size,
                             const AttributeField& field, AttributeColumn& out,
                             std::string& error);
};

} // namespace i3s
