#pragma once

// ============================================================================
// i3s::I3SPointCloud — PCSL node decode (LEPCC xyz/rgb/intensity + raw
// attribute columns) into renderer-ready point arrays.
// ----------------------------------------------------------------------------
// Pure CPU, worker-thread safe, no Vulkan — the M3 pipeline shape mirrors the
// mesh path: decode on a worker, upload on the main thread. Output positions
// are node-relative in the APP render frame (Y-up), built from the same
// NodeFrame the mesh decoder uses; unlike mesh buffers, LEPCC blobs decode to
// ABSOLUTE layer-SR coordinates (lon/lat/h doubles for geographic CRS), so
// the node-relative subtraction happens here, per point, in double.
//
// Resource naming, validated against a real ArcGIS-cooked PCSL
// (Esri/lepcc testData/SMALL_AUTZEN_LAS_All.slpk):
//   * geometry:  nodes/<res>/geometries/0.bin.pccxyz   (1.x, raw LEPCC blob)
//                nodes/<res>/geometries/0.bin(.gz)     (2.0)
//   * lepcc columns: nodes/<res>/attributes/<key>.bin.pccint / .pccrgb / .bin
//   * raw columns:   nodes/<res>/attributes/<key>.bin.gz (values only,
//                    count == point count, no header)
// Attribute roles come from LayerInfo::attributeFields (name + encoding —
// "lepcc-rgb", "lepcc-intensity", raw CLASS_CODE); ELEVATION is
// "embedded-elevation" (rides z, no resource).
// ============================================================================

#include "Loaders/Slpk/I3SGeometry.h" // NodeFrame
#include "Loaders/Slpk/SlpkTypes.h"

#include <glm/glm.hpp>

#include <cstdint>
#include <string>
#include <vector>

namespace i3s {

class SlpkArchive;

// Decoded node point payload. Positions are node-relative, app frame.
struct PointCloudData {
    std::vector<glm::vec3> positions;
    std::vector<uint8_t> rgb;        // 3 B/point when the layer has RGB
    std::vector<uint16_t> intensity; // per point when present
    std::vector<uint8_t> classCodes; // per point when present (raw column)

    glm::vec3 localMin{ 0.0f };
    glm::vec3 localMax{ 0.0f };
    // Non-fatal attribute problems (count mismatch, decode failure) — the
    // node still renders, colorization falls back.
    std::string attributeWarning;

    size_t pointCount() const { return positions.size(); }
    size_t cpuBytes() const {
        return positions.size() * sizeof(glm::vec3) + rgb.size() +
               intensity.size() * sizeof(uint16_t) + classCodes.size();
    }
};

class I3SPointCloud {
public:
    // Reads + decodes one PCSL node: LEPCC xyz (required) + whatever RGB /
    // intensity / class-code columns the layer declares. Fails loudly (false
    // + reason) only when the geometry itself is missing or corrupt.
    static bool decodeNode(const SlpkArchive& archive, const LayerInfo& info,
                           const NodeInfo& node, const NodeFrame& frame,
                           PointCloudData& out, std::string& error);

    // Blob-level entry points (decodeNode internals; also the test-harness
    // surface). decodeXyz transforms into the app frame via `frame`.
    static bool decodeXyz(const uint8_t* data, size_t size,
                          const NodeFrame& frame, std::vector<glm::vec3>& out,
                          glm::vec3& outMin, glm::vec3& outMax,
                          std::string& error);
    static bool decodeRgb(const uint8_t* data, size_t size,
                          std::vector<uint8_t>& out, std::string& error);
    static bool decodeIntensity(const uint8_t* data, size_t size,
                                std::vector<uint16_t>& out, std::string& error);
};

} // namespace i3s
