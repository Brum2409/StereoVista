#include "Loaders/Slpk/I3STexture.h"

#include "Loaders/Slpk/SlpkArchive.h"

#include <stb_image.h>

// Vendored basis_universal transcoder (headers/libs/basisu, pinned in the
// plan §3; the implementation TU is BasisTranscoderImpl.cpp). The KTX2
// defines must match that TU — they gate header-visible declarations.
#define BASISD_SUPPORT_KTX2 1
#define BASISD_SUPPORT_KTX2_ZSTD 1
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable : 4018 4127 4244 4245 4267 4310 4324 4457 4458 4702 4996)
#endif
#include <basisu/transcoder/basisu_transcoder.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <mutex>

namespace i3s {

namespace {

bool looksLikeDds(const uint8_t* data, size_t size) {
    return size >= 4 && std::memcmp(data, "DDS ", 4) == 0;
}

bool looksLikeKtx2(const uint8_t* data, size_t size) {
    static const uint8_t kMagic[12] = { 0xAB, 0x4B, 0x54, 0x58, 0x20, 0x32,
                                        0x30, 0xBB, 0x0D, 0x0A, 0x1A, 0x0A };
    return size >= 12 && std::memcmp(data, kMagic, 12) == 0;
}

bool looksLikeBasis(const uint8_t* data, size_t size) {
    // .basis header magic: 'B' * 256 + 's' little-endian (0x73, 0x42... no:
    // basis_file_header::cBASISHeaderSig = 0x4273 -> bytes 0x73 0x42 "sB").
    return size >= 2 && data[0] == 0x73 && data[1] == 0x42;
}

// The material's texture set (1.7+), or the layer's single set (1.6).
const TextureSetDefinition* textureSetFor(const LayerInfo& info, int setIndex) {
    if (setIndex < 0)
        return nullptr;
    if (setIndex < static_cast<int>(info.textureSets.size()))
        return &info.textureSets[setIndex];
    return info.textureSets.empty() ? nullptr : &info.textureSets[0];
}

// basisu's global tables build once per process (not thread-safe to race).
void ensureTranscoderInit() {
    static std::once_flag once;
    std::call_once(once, []() { basist::basisu_transcoder_init(); });
}

// ---- sRGB-correct RGBA8 mip chain (jpg/png path; the streaming upload path
// never blits, so the workers supply every level) -----------------------------

float srgbToLinearLut(uint8_t v) {
    static float lut[256];
    static std::once_flag once;
    std::call_once(once, []() {
        for (int i = 0; i < 256; ++i) {
            const float s = float(i) / 255.0f;
            lut[i] = s <= 0.04045f ? s / 12.92f
                                   : std::pow((s + 0.055f) / 1.055f, 2.4f);
        }
    });
    return lut[v];
}

uint8_t linearToSrgb(float linear) {
    linear = std::min(std::max(linear, 0.0f), 1.0f);
    const float s = linear <= 0.0031308f
                        ? linear * 12.92f
                        : 1.055f * std::pow(linear, 1.0f / 2.4f) - 0.055f;
    return static_cast<uint8_t>(s * 255.0f + 0.5f);
}

// One 2:1 box-filter step, gamma-correct in color, linear in alpha. Handles
// odd/1-wide sources by clamping the second sample.
void downsampleRgba8(const uint8_t* src, int srcW, int srcH, uint8_t* dst,
                     int dstW, int dstH) {
    for (int y = 0; y < dstH; ++y) {
        const int sy0 = std::min(y * 2, srcH - 1);
        const int sy1 = std::min(y * 2 + 1, srcH - 1);
        for (int x = 0; x < dstW; ++x) {
            const int sx0 = std::min(x * 2, srcW - 1);
            const int sx1 = std::min(x * 2 + 1, srcW - 1);
            const uint8_t* s00 = src + (size_t(sy0) * srcW + sx0) * 4;
            const uint8_t* s01 = src + (size_t(sy0) * srcW + sx1) * 4;
            const uint8_t* s10 = src + (size_t(sy1) * srcW + sx0) * 4;
            const uint8_t* s11 = src + (size_t(sy1) * srcW + sx1) * 4;
            uint8_t* d = dst + (size_t(y) * dstW + x) * 4;
            for (int c = 0; c < 3; ++c)
                d[c] = linearToSrgb((srgbToLinearLut(s00[c]) + srgbToLinearLut(s01[c]) +
                                     srgbToLinearLut(s10[c]) + srgbToLinearLut(s11[c])) *
                                    0.25f);
            d[3] = static_cast<uint8_t>((int(s00[3]) + s01[3] + s10[3] + s11[3] + 2) / 4);
        }
    }
}

void buildRgba8MipChain(const uint8_t* level0, int width, int height,
                        TextureData& out) {
    out.format = TextureData::Format::Rgba8;
    out.width = width;
    out.height = height;

    size_t total = 0;
    int w = width, h = height;
    for (;;) {
        total += size_t(w) * h * 4;
        if (w == 1 && h == 1)
            break;
        w = std::max(w / 2, 1);
        h = std::max(h / 2, 1);
    }
    out.data.resize(total);
    out.mips.clear();

    std::memcpy(out.data.data(), level0, size_t(width) * height * 4);
    size_t offset = 0;
    w = width;
    h = height;
    for (;;) {
        const size_t bytes = size_t(w) * h * 4;
        out.mips.push_back({ w, h, offset, bytes });
        if (w == 1 && h == 1)
            break;
        const int nw = std::max(w / 2, 1);
        const int nh = std::max(h / 2, 1);
        downsampleRgba8(out.data.data() + offset, w, h,
                        out.data.data() + offset + bytes, nw, nh);
        offset += bytes;
        w = nw;
        h = nh;
    }
}

size_t bc7LevelBytes(uint32_t w, uint32_t h) {
    return size_t((w + 3) / 4) * ((h + 3) / 4) * 16;
}

} // namespace

bool I3STexture::decodeImage(const uint8_t* data, size_t size, TextureData& out,
                             std::string& error) {
    // Workers must not race the importer's global flip flag (ModelImporter
    // flips color maps on the main thread); pin this thread to "no flip" —
    // I3S UVs address the image rows top-down exactly as stb returns them.
    stbi_set_flip_vertically_on_load_thread(0);

    int width = 0, height = 0, comp = 0;
    stbi_uc* pixels = stbi_load_from_memory(data, static_cast<int>(size), &width,
                                            &height, &comp, 4);
    if (!pixels) {
        error = std::string("image decode failed: ") + stbi_failure_reason();
        return false;
    }
    buildRgba8MipChain(pixels, width, height, out);
    stbi_image_free(pixels);
    return true;
}

bool I3STexture::decodeBasis(const uint8_t* data, size_t size, TextureData& out,
                             std::string& error) {
    ensureTranscoderInit();
    const basist::transcoder_texture_format target =
        basist::transcoder_texture_format::cTFBC7_RGBA;

    if (looksLikeKtx2(data, size)) {
        basist::ktx2_transcoder tc;
        if (!tc.init(data, static_cast<uint32_t>(size))) {
            error = "ktx2 parse failed";
            return false;
        }
        if (tc.is_hdr()) {
            error = "ktx2 HDR payloads are not supported";
            return false;
        }
        if (!tc.start_transcoding()) {
            error = "ktx2 start_transcoding failed (corrupt or unsupported "
                    "supercompression)";
            return false;
        }
        const uint32_t levels = std::max(tc.get_levels(), 1u);
        out.format = TextureData::Format::Bc7;
        out.width = static_cast<int>(tc.get_width());
        out.height = static_cast<int>(tc.get_height());
        out.mips.clear();
        out.data.clear();
        for (uint32_t level = 0; level < levels; ++level) {
            basist::ktx2_image_level_info info{};
            if (!tc.get_image_level_info(info, level, 0, 0)) {
                error = "ktx2 level info failed at level " + std::to_string(level);
                return false;
            }
            const size_t bytes = size_t(info.m_total_blocks) * 16;
            const size_t offset = out.data.size();
            out.data.resize(offset + bytes);
            if (!tc.transcode_image_level(level, 0, 0, out.data.data() + offset,
                                          info.m_total_blocks, target)) {
                error = "ktx2 -> BC7 transcode failed at level " +
                        std::to_string(level);
                return false;
            }
            out.mips.push_back({ static_cast<int>(info.m_orig_width),
                                 static_cast<int>(info.m_orig_height), offset,
                                 bytes });
        }
        return true;
    }

    if (looksLikeBasis(data, size)) {
        basist::basisu_transcoder tc;
        if (!tc.validate_header(data, static_cast<uint32_t>(size))) {
            error = "basis header validation failed";
            return false;
        }
        basist::basisu_image_info imageInfo{};
        if (!tc.get_image_info(data, static_cast<uint32_t>(size), imageInfo, 0)) {
            error = "basis image info failed";
            return false;
        }
        if (!tc.start_transcoding(data, static_cast<uint32_t>(size))) {
            error = "basis start_transcoding failed";
            return false;
        }
        const uint32_t levels = std::max(imageInfo.m_total_levels, 1u);
        out.format = TextureData::Format::Bc7;
        out.width = static_cast<int>(imageInfo.m_orig_width);
        out.height = static_cast<int>(imageInfo.m_orig_height);
        out.mips.clear();
        out.data.clear();
        for (uint32_t level = 0; level < levels; ++level) {
            uint32_t origW = 0, origH = 0, blocks = 0;
            if (!tc.get_image_level_desc(data, static_cast<uint32_t>(size), 0,
                                         level, origW, origH, blocks)) {
                error = "basis level desc failed at level " + std::to_string(level);
                return false;
            }
            const size_t bytes = bc7LevelBytes(origW, origH);
            const size_t offset = out.data.size();
            out.data.resize(offset + bytes);
            if (!tc.transcode_image_level(data, static_cast<uint32_t>(size), 0,
                                          level, out.data.data() + offset,
                                          static_cast<uint32_t>(bytes / 16),
                                          target)) {
                error = "basis -> BC7 transcode failed at level " +
                        std::to_string(level);
                return false;
            }
            out.mips.push_back({ static_cast<int>(origW), static_cast<int>(origH),
                                 offset, bytes });
        }
        return true;
    }

    error = "not a ktx2/basis payload";
    return false;
}

bool I3STexture::loadNodeTexture(const SlpkArchive& archive, const LayerInfo& info,
                                 const NodeInfo& node, TextureData& out,
                                 bool& unsupportedFormat, std::string& error) {
    out = TextureData{};
    unsupportedFormat = false;

    const MaterialDesc* material = nullptr;
    if (node.mesh.materialDefinition >= 0 &&
        node.mesh.materialDefinition < static_cast<int>(info.materials.size()))
        material = &info.materials[node.mesh.materialDefinition];
    else if (!info.materials.empty())
        material = &info.materials[0];
    if (!material || material->baseColorTexture < 0)
        return true; // untextured material — not an error

    std::vector<uint8_t> bytes;
    std::string unsupportedSeen;

    // Decode a blob by its magic (1.6 hrefs are extensionless; 1.7+ names lie
    // often enough — "basis" entries holding ktx2 — that sniffing is the
    // reliable path everywhere).
    auto decodeBlob = [&](TextureData& tex, std::string& err) -> bool {
        if (looksLikeDds(bytes.data(), bytes.size())) {
            unsupportedSeen = "dds";
            return false;
        }
        if (looksLikeKtx2(bytes.data(), bytes.size()) ||
            looksLikeBasis(bytes.data(), bytes.size()))
            return decodeBasis(bytes.data(), bytes.size(), tex, err);
        return decodeImage(bytes.data(), bytes.size(), tex, err);
    };

    if (!node.mesh.v16TexturePath.empty()) {
        // 1.6: one image per node, addressed by href without an extension.
        // Encodings come from the shared resource / store.textureEncoding.
        static const char* kExtensions[] = { ".jpg", ".png", "", ".jpeg",
                                             ".ktx2", ".basis", ".bin" };
        for (const char* ext : kExtensions) {
            if (!archive.read(node.mesh.v16TexturePath + ext, bytes) || bytes.empty())
                continue;
            std::string err;
            if (decodeBlob(out, err))
                return true;
            if (!err.empty()) {
                error = err;
                return false;
            }
            // dds — remember and keep probing other extensions.
        }
        if (!unsupportedSeen.empty()) {
            unsupportedFormat = true;
            error = "texture format not supported (" + unsupportedSeen + "): " +
                    node.mesh.v16TexturePath;
            return false;
        }
        return true; // declared but absent: render untextured, don't fail the node
    }

    const TextureSetDefinition* set = textureSetFor(info, material->baseColorTexture);
    if (!set || set->formats.empty())
        return true;

    // Preference order: ktx2/basis first (BC7 = 4-8x less VRAM + bandwidth
    // than RGBA8, and the mip chain comes for free), then jpg/png. Remember
    // what we skipped so an all-dds package reports "unsupported" instead of
    // "missing".
    const std::string base =
        "nodes/" + std::to_string(node.mesh.materialResource) + "/textures/";
    for (int pass = 0; pass < 2; ++pass) {
        for (const TextureFormat& fmt : set->formats) {
            const bool compressed = fmt.format == "ktx2" || fmt.format == "basis";
            const bool decodable = compressed || fmt.format == "jpg" ||
                                   fmt.format == "png" || fmt.format == "jpeg";
            if (!decodable) {
                if (pass == 0)
                    unsupportedSeen = fmt.format;
                continue;
            }
            if ((pass == 0) != compressed)
                continue;
            const std::string name = fmt.name.empty() ? "0" : fmt.name;
            if (!archive.read(base + name + "." + fmt.format, bytes) || bytes.empty())
                continue;
            std::string err;
            if (decodeBlob(out, err))
                return true;
            if (!err.empty()) {
                error = err;
                return false;
            }
        }
    }
    if (!unsupportedSeen.empty()) {
        unsupportedFormat = true;
        error = "texture formats not supported (" + unsupportedSeen + "): " + base;
        return false;
    }
    return true;
}

} // namespace i3s
