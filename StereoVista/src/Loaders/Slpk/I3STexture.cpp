#include "Loaders/Slpk/I3STexture.h"

#include "Loaders/Slpk/SlpkArchive.h"

#include <stb_image.h>

#include <cstring>

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

// The material's texture set (1.7+), or the layer's single set (1.6).
const TextureSetDefinition* textureSetFor(const LayerInfo& info, int setIndex) {
    if (setIndex < 0)
        return nullptr;
    if (setIndex < static_cast<int>(info.textureSets.size()))
        return &info.textureSets[setIndex];
    return info.textureSets.empty() ? nullptr : &info.textureSets[0];
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
    out.width = width;
    out.height = height;
    out.rgba.assign(pixels, pixels + size_t(width) * height * 4);
    stbi_image_free(pixels);
    return true;
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

    if (!node.mesh.v16TexturePath.empty()) {
        // 1.6: one image per node, addressed by href without an extension.
        // Encodings come from the shared resource / store.textureEncoding.
        static const char* kExtensions[] = { ".jpg", ".png", "", ".jpeg", ".bin" };
        for (const char* ext : kExtensions) {
            if (!archive.read(node.mesh.v16TexturePath + ext, bytes) || bytes.empty())
                continue;
            if (looksLikeDds(bytes.data(), bytes.size()) ||
                looksLikeKtx2(bytes.data(), bytes.size())) {
                unsupportedSeen = "dds/ktx2";
                continue;
            }
            return decodeImage(bytes.data(), bytes.size(), out, error);
        }
        if (!unsupportedSeen.empty()) {
            unsupportedFormat = true;
            error = "texture format not supported yet (" + unsupportedSeen + "): " +
                    node.mesh.v16TexturePath;
            return false;
        }
        return true; // declared but absent: render untextured, don't fail the node
    }

    const TextureSetDefinition* set = textureSetFor(info, material->baseColorTexture);
    if (!set || set->formats.empty())
        return true;

    // Prefer the formats we can decode; remember what we skipped so an
    // all-ktx2 package reports "unsupported" instead of "missing".
    const std::string base =
        "nodes/" + std::to_string(node.mesh.materialResource) + "/textures/";
    for (int pass = 0; pass < 2; ++pass) {
        for (const TextureFormat& fmt : set->formats) {
            const bool decodable = fmt.format == "jpg" || fmt.format == "png" ||
                                   fmt.format == "jpeg";
            if ((pass == 0) != decodable)
                continue;
            if (!decodable) {
                unsupportedSeen = fmt.format;
                continue;
            }
            const std::string name = fmt.name.empty() ? "0" : fmt.name;
            if (!archive.read(base + name + "." + fmt.format, bytes) || bytes.empty())
                continue;
            return decodeImage(bytes.data(), bytes.size(), out, error);
        }
    }
    if (!unsupportedSeen.empty()) {
        unsupportedFormat = true;
        error = "texture formats not supported yet (" + unsupportedSeen +
                "; ktx2 lands in M2): " + base;
        return false;
    }
    return true;
}

} // namespace i3s
