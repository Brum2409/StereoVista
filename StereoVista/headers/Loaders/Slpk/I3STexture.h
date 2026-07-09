#pragma once

// ============================================================================
// i3s::I3STexture — node texture resolution + decode (jpg/png -> RGBA8).
// ----------------------------------------------------------------------------
// Pure CPU (stb_image), worker-thread safe, no Vulkan: the output is plain
// RGBA8 pixels; the main-thread pump creates the rhi::Texture (mips come from
// the existing blit path — fine for RGBA8 in M1, plan §5).
//
// Format coverage in M1: jpg + png. KTX2/Basis (-> BC7) is the M2 texture
// milestone; legacy DDS is detected and reported, not decoded. I3S UVs use a
// top-left origin like the image rows stb returns — no vertical flip.
// ============================================================================

#include "Loaders/Slpk/SlpkTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace i3s {

class SlpkArchive;

struct TextureData {
    int width = 0;
    int height = 0;
    std::vector<uint8_t> rgba; // width * height * 4

    bool valid() const { return width > 0 && height > 0 && !rgba.empty(); }
    size_t cpuBytes() const { return rgba.size(); }
};

class I3STexture {
public:
    // Loads the node's base-color texture. Returns:
    //   true  + out.valid()   — decoded;
    //   true  + !out.valid()  — the node has no texture (not an error);
    //   false                 — a texture exists but couldn't be used; error
    //                           says why (unsupported format / decode failure)
    //                           and unsupportedFormat distinguishes "ktx2/dds
    //                           only" from corrupt data.
    static bool loadNodeTexture(const SlpkArchive& archive, const LayerInfo& info,
                                const NodeInfo& node, TextureData& out,
                                bool& unsupportedFormat, std::string& error);

    // Decodes a jpg/png blob (used by loadNodeTexture and the test harness).
    static bool decodeImage(const uint8_t* data, size_t size, TextureData& out,
                            std::string& error);
};

} // namespace i3s
