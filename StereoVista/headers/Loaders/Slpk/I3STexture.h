#pragma once

// ============================================================================
// i3s::I3STexture — node texture resolution + decode.
// ----------------------------------------------------------------------------
// Pure CPU, worker-thread safe, no Vulkan: the output is a full CPU-side mip
// chain the main-thread pump uploads verbatim (Texture::uploadMips or
// UploadRing::stageImage — the streaming path never blits, plan §6.4).
//
// Format coverage (M2):
//   * jpg / png  -> RGBA8 + a CPU-generated sRGB-correct box-filter mip chain;
//   * ktx2       -> BC7 via the vendored basis_universal transcoder (ETC1S and
//                   UASTC, zstd supercompression included) — mip chain as
//                   stored in the file;
//   * basis      -> same transcoder, .basis container (some cookers label
//                   ktx2 payloads "basis"; magic bytes decide, not the name);
//   * dds        -> detected and reported, not decoded (legacy; M4+ if ever).
// I3S UVs use a top-left origin like the rows stb/basisu return — no flip.
// ============================================================================

#include "Loaders/Slpk/SlpkTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace i3s {

class SlpkArchive;

struct TextureData {
    enum class Format : uint8_t {
        None,
        Rgba8, // VK_FORMAT_R8G8B8A8_SRGB, 4 B/texel
        Bc7,   // VK_FORMAT_BC7_SRGB_BLOCK, 4x4 blocks, 16 B/block
    };
    struct Mip {
        int width = 0;
        int height = 0;
        size_t offset = 0; // into data
        size_t size = 0;   // tightly packed
    };

    Format format = Format::None;
    int width = 0;  // level-0 extent
    int height = 0;
    std::vector<Mip> mips;     // level 0 first; count = the texture's mip count
    std::vector<uint8_t> data; // all mip payloads, in mips[] order

    bool valid() const { return format != Format::None && !mips.empty(); }
    size_t cpuBytes() const { return data.size(); }
};

class I3STexture {
public:
    // Loads the node's base-color texture. Returns:
    //   true  + out.valid()   — decoded;
    //   true  + !out.valid()  — the node has no texture (not an error);
    //   false                 — a texture exists but couldn't be used; error
    //                           says why (unsupported format / decode failure)
    //                           and unsupportedFormat distinguishes "dds only"
    //                           from corrupt data.
    static bool loadNodeTexture(const SlpkArchive& archive, const LayerInfo& info,
                                const NodeInfo& node, TextureData& out,
                                bool& unsupportedFormat, std::string& error);

    // Decodes a jpg/png blob into RGBA8 + a CPU mip chain (worker-safe).
    static bool decodeImage(const uint8_t* data, size_t size, TextureData& out,
                            std::string& error);

    // Transcodes a KTX2 or .basis blob (ETC1S / UASTC) into a BC7 mip chain
    // (worker-safe; magic bytes select the container).
    static bool decodeBasis(const uint8_t* data, size_t size, TextureData& out,
                            std::string& error);
};

} // namespace i3s
