#include "Loaders/Slpk/I3SAttributes.h"

#include "Loaders/Slpk/SlpkArchive.h"

#include <algorithm>
#include <cstdio>
#include <cstring>

namespace i3s {

namespace {

// Case-insensitive ASCII compare (section names appear as "ObjectIds",
// "attributeValues", "attributeByteCounts" with varying capitalization).
bool iequals(const std::string& a, const char* b) {
    size_t i = 0;
    for (; i < a.size() && b[i] != '\0'; ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z')
            ca = static_cast<char>(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z')
            cb = static_cast<char>(cb - 'A' + 'a');
        if (ca != cb)
            return false;
    }
    return i == a.size() && b[i] == '\0';
}

// Reads one little-endian scalar at `offset` widened to double, advancing
// offset. False when it does not fit.
bool readScalar(const uint8_t* data, size_t size, size_t& offset, ScalarType type,
                double& out) {
    const size_t bytes = scalarSize(type);
    if (bytes == 0 || offset + bytes > size)
        return false;
    // memcpy: the mmap'd archive gives no alignment guarantee.
    switch (type) {
    case ScalarType::UInt8: {
        uint8_t v;
        std::memcpy(&v, data + offset, 1);
        out = v;
        break;
    }
    case ScalarType::Int8: {
        int8_t v;
        std::memcpy(&v, data + offset, 1);
        out = v;
        break;
    }
    case ScalarType::UInt16: {
        uint16_t v;
        std::memcpy(&v, data + offset, 2);
        out = v;
        break;
    }
    case ScalarType::Int16: {
        int16_t v;
        std::memcpy(&v, data + offset, 2);
        out = v;
        break;
    }
    case ScalarType::UInt32: {
        uint32_t v;
        std::memcpy(&v, data + offset, 4);
        out = v;
        break;
    }
    case ScalarType::Int32: {
        int32_t v;
        std::memcpy(&v, data + offset, 4);
        out = v;
        break;
    }
    case ScalarType::UInt64: {
        uint64_t v;
        std::memcpy(&v, data + offset, 8);
        out = static_cast<double>(v);
        break;
    }
    case ScalarType::Int64: {
        int64_t v;
        std::memcpy(&v, data + offset, 8);
        out = static_cast<double>(v);
        break;
    }
    case ScalarType::Float32: {
        float v;
        std::memcpy(&v, data + offset, 4);
        out = v;
        break;
    }
    case ScalarType::Float64: {
        double v;
        std::memcpy(&v, data + offset, 8);
        out = v;
        break;
    }
    default:
        return false;
    }
    offset += bytes;
    return true;
}

} // namespace

std::string AttributeColumn::displayValue(size_t index) const {
    if (isString)
        return index < strings.size() ? strings[index] : std::string();
    const size_t vpe = valuesPerElement ? valuesPerElement : 1;
    const size_t first = index * vpe;
    if (first + vpe > numbers.size())
        return std::string();
    std::string out;
    for (size_t c = 0; c < vpe; ++c) {
        if (c)
            out += ", ";
        const double v = numbers[first + c];
        char buf[64];
        // Integers (the overwhelmingly common case) print without a
        // fractional tail; real fractions keep full float precision.
        if (v == static_cast<double>(static_cast<long long>(v)) &&
            v >= -9.007199254740992e15 && v <= 9.007199254740992e15)
            std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(v));
        else
            std::snprintf(buf, sizeof(buf), "%.6g", v);
        out += buf;
    }
    return out;
}

std::string I3SAttributes::attributeDir(const LayerInfo& info,
                                        const NodeInfo& node) {
    if (!node.mesh.hasGeometry)
        return std::string();
    if (!node.mesh.v16GeometryPath.empty()) {
        // 1.6: "nodes/<id>/geometries/0" -> "nodes/<id>/attributes".
        const std::string& geom = node.mesh.v16GeometryPath;
        const size_t pos = geom.rfind("/geometries/");
        if (pos == std::string::npos)
            return std::string();
        return geom.substr(0, pos) + "/attributes";
    }
    (void)info;
    return "nodes/" + std::to_string(node.mesh.attributeResource) + "/attributes";
}

bool I3SAttributes::readColumn(const SlpkArchive& archive, const LayerInfo& info,
                               const NodeInfo& node, const AttributeField& field,
                               AttributeColumn& out, std::string& error) {
    out = AttributeColumn{};
    const std::string dir = attributeDir(info, node);
    if (dir.empty()) {
        error = "node has no attribute resource";
        return false;
    }
    std::vector<uint8_t> bytes;
    // Mesh profiles nest the column one level deep (attributes/<key>/0.bin);
    // some cookers (and PCSL stores) write attributes/<key>.bin instead.
    if (!archive.read(dir + "/" + field.key + "/0.bin", bytes) &&
        !archive.read(dir + "/" + field.key + ".bin", bytes)) {
        error = "column not in archive: " + dir + "/" + field.key;
        return false;
    }
    if (!decodeColumn(bytes.data(), bytes.size(), field, out, error)) {
        error += " (" + dir + "/" + field.key + ")";
        return false;
    }
    return true;
}

bool I3SAttributes::decodeColumn(const uint8_t* data, size_t size,
                                 const AttributeField& field,
                                 AttributeColumn& out, std::string& error) {
    out = AttributeColumn{};
    out.isString = field.valuesAreStrings;
    out.valuesPerElement = field.valuesPerElement ? field.valuesPerElement : 1;

    if (!out.isString && scalarSize(field.valueScalar) == 0) {
        error = "unsupported value type '" + field.valueType + "'";
        return false;
    }

    // ---- header fields, in declared order --------------------------------
    // Every real package declares at least {count}; strings add
    // {attributeValuesByteCount}. A missing declaration falls back to the
    // universal single-count header rather than failing the whole column.
    size_t offset = 0;
    uint64_t count = 0;
    uint64_t valuesByteCount = 0;
    bool haveCount = false;
    std::vector<GeometryHeaderField> header = field.header;
    if (header.empty())
        header.push_back({ "count", ScalarType::UInt32 });
    for (const GeometryHeaderField& h : header) {
        double v = 0.0;
        if (!readScalar(data, size, offset, h.type, v)) {
            error = "column truncated in header";
            return false;
        }
        if (v < 0.0)
            v = 0.0;
        if (iequals(h.property, "count")) {
            count = static_cast<uint64_t>(v);
            haveCount = true;
        } else if (iequals(h.property, "attributeValuesByteCount")) {
            valuesByteCount = static_cast<uint64_t>(v);
        }
    }
    if (!haveCount) {
        error = "column header declares no count";
        return false;
    }
    // A column can never hold more elements than one byte each.
    if (count > size) {
        error = "column count is implausible";
        return false;
    }

    // ---- sections, in ordering order --------------------------------------
    std::vector<std::string> ordering = field.ordering;
    if (ordering.empty()) {
        if (out.isString)
            ordering = { "attributeByteCounts", "attributeValues" };
        else
            ordering = { "attributeValues" };
    }

    std::vector<uint64_t> byteCounts;
    for (const std::string& section : ordering) {
        if (iequals(section, "attributeByteCounts")) {
            const ScalarType bcType = field.byteCountScalar != ScalarType::Unknown
                                          ? field.byteCountScalar
                                          : ScalarType::UInt32;
            const size_t bcSize = scalarSize(bcType);
            if (bcSize == 0) {
                error = "unsupported byte-count type";
                return false;
            }
            byteCounts.reserve(static_cast<size_t>(count));
            for (uint64_t i = 0; i < count; ++i) {
                double v = 0.0;
                if (!readScalar(data, size, offset, bcType, v)) {
                    error = "column truncated in byte counts";
                    return false;
                }
                byteCounts.push_back(v > 0.0 ? static_cast<uint64_t>(v) : 0);
            }
        } else if (iequals(section, "attributeValues") ||
                   iequals(section, "ObjectIds")) {
            if (out.isString) {
                if (byteCounts.size() != count) {
                    error = "string column without byte counts";
                    return false;
                }
                uint64_t total = 0;
                for (uint64_t bc : byteCounts)
                    total += bc;
                if (valuesByteCount != 0 && valuesByteCount != total) {
                    error = "string byte counts disagree with the header";
                    return false;
                }
                if (offset + total > size) {
                    error = "column truncated in string values";
                    return false;
                }
                out.strings.reserve(static_cast<size_t>(count));
                for (uint64_t i = 0; i < count; ++i) {
                    size_t len = static_cast<size_t>(byteCounts[i]);
                    // Byte counts include the null terminator; empty (0) and
                    // unterminated writers both occur in the wild.
                    const char* s = reinterpret_cast<const char*>(data + offset);
                    if (len > 0 && s[len - 1] == '\0')
                        --len;
                    out.strings.emplace_back(s, len);
                    offset += static_cast<size_t>(byteCounts[i]);
                }
            } else {
                const size_t elemBytes = scalarSize(field.valueScalar);
                // ArcGIS aligns 8-byte scalars: 4 pad bytes follow the 4-byte
                // count header (validated against loaders.gl + real packages).
                if (elemBytes == 8 && (offset % 8) != 0)
                    offset += 8 - (offset % 8);
                const uint64_t valueCount = count * out.valuesPerElement;
                if (valueCount > (size - std::min(offset, size)) / elemBytes) {
                    error = "column truncated in values";
                    return false;
                }
                out.numbers.reserve(static_cast<size_t>(valueCount));
                for (uint64_t i = 0; i < valueCount; ++i) {
                    double v = 0.0;
                    if (!readScalar(data, size, offset, field.valueScalar, v)) {
                        error = "column truncated in values";
                        return false;
                    }
                    out.numbers.push_back(v);
                }
            }
        }
        // Unknown section names are skipped: without a size we cannot advance,
        // but every real writer orders the value section last anyway.
    }

    if (out.count() != count) {
        error = "column decoded no values";
        return false;
    }
    return true;
}

} // namespace i3s
