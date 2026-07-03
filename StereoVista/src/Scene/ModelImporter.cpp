#include "Scene/Scene.h"

#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/MaterialSystem.h"

#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <stb_image.h>

#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

// Assimp import ported from the GL ModelLoader (processNode/processMesh/
// loadMaterialTextures/TextureFromFile/loadEmbeddedTexture). The parsing and
// texture-search behaviour is kept; only the GPU side changed: GL texture
// objects became bindless rhi::Textures (albedo = sRGB view, data maps =
// UNORM — playbook C.6) and VAO/VBO/EBO became renderer::MeshBuffer.
// Import flags match the GL preferences defaults (preferences themselves
// return in a later phase).

namespace scene {

namespace {

namespace fs = std::filesystem;

constexpr uint32_t kNoTexture = renderer::kInvalidTexture;

struct TextureCacheEntry {
    std::string key; // fullPath + srgb flag
    uint32_t index;
};

struct ImportContext {
    rhi::Device* device = nullptr;
    renderer::MaterialSystem* materials = nullptr;
    std::string directory; // model file's directory
    const aiScene* aiScene_ = nullptr;
    std::vector<TextureCacheEntry> cache;
};

uint32_t uploadPixels(ImportContext& ctx, const uint8_t* rgba, int width, int height,
                      bool srgb, const char* debugName) {
    rhi::TextureDesc desc{};
    desc.format = srgb ? VK_FORMAT_R8G8B8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
    desc.extent = { uint32_t(width), uint32_t(height) };
    desc.mipLevels = rhi::computeMipCount(uint32_t(width), uint32_t(height));
    desc.usage = VK_IMAGE_USAGE_SAMPLED_BIT;
    desc.debugName = debugName;
    rhi::Texture texture;
    texture.create(*ctx.device, desc);
    texture.upload(rgba, size_t(width) * height * 4);
    return ctx.materials->addTexture(std::move(texture));
}

// Port of Model::TextureFromFile — same multi-extension multi-directory
// search, same flip conventions (color maps flipped for GL-authored UVs,
// normal maps not).
uint32_t loadTextureFile(ImportContext& ctx, const std::string& refPath, bool srgb,
                         bool flipVertically) {
    std::string textureName = refPath;
    const size_t lastSlash = textureName.find_last_of("/\\");
    if (lastSlash != std::string::npos)
        textureName = textureName.substr(lastSlash + 1);
    const size_t lastDot = textureName.find_last_of('.');
    const std::string baseName =
        (lastDot != std::string::npos) ? textureName.substr(0, lastDot) : textureName;

    std::string parentDir = ctx.directory;
    const size_t parentSlash = parentDir.find_last_of("/\\");
    if (parentSlash != std::string::npos)
        parentDir = parentDir.substr(0, parentSlash);

    static const char* kExtensions[] = { ".png", ".jpg", ".jpeg", ".tga", ".bmp",
                                         ".dds", ".tif", ".tiff", ".psd", ".gif",
                                         ".hdr", ".pic" };
    std::vector<std::string> pathsToTry;
    for (const char* ext : kExtensions) {
        const std::string name = baseName + ext;
        pathsToTry.push_back(ctx.directory + "/" + name);
        pathsToTry.push_back(ctx.directory + "/textures/" + name);
        pathsToTry.push_back(ctx.directory + "/texture/" + name);
        pathsToTry.push_back(ctx.directory + "/materials/" + name);
        pathsToTry.push_back(ctx.directory + "/images/" + name);
        pathsToTry.push_back(ctx.directory + "/maps/" + name);
        pathsToTry.push_back(parentDir + "/" + name);
        pathsToTry.push_back(parentDir + "/textures/" + name);
        pathsToTry.push_back("textures/" + name);
        pathsToTry.push_back("./" + name);
    }

    stbi_set_flip_vertically_on_load(flipVertically);
    for (const std::string& tryPath : pathsToTry) {
        std::error_code ec;
        if (!fs::exists(tryPath, ec))
            continue;
        const std::string fullPath = fs::canonical(tryPath, ec).string();
        if (ec)
            continue;

        const std::string cacheKey = fullPath + (srgb ? "|srgb" : "|unorm");
        for (const TextureCacheEntry& entry : ctx.cache)
            if (entry.key == cacheKey)
                return entry.index;

        int width = 0, height = 0, comp = 0;
        uint8_t* data = stbi_load(fullPath.c_str(), &width, &height, &comp, 4);
        if (!data)
            continue;
        const uint32_t index = uploadPixels(ctx, data, width, height, srgb,
                                            textureName.c_str());
        stbi_image_free(data);
        ctx.cache.push_back({ cacheKey, index });
        std::cout << "Loaded texture " << fullPath << " (" << width << "x" << height
                  << ")\n";
        return index;
    }

    std::cerr << "WARNING: texture '" << refPath
              << "' not found near " << ctx.directory << " — rendering without it\n";
    return kNoTexture;
}

// Port of Model::loadEmbeddedTexture ('*N' / named references inside the
// model file). Embedded images use a top-left origin handled by assimp — no
// flip.
uint32_t loadEmbeddedTexture(ImportContext& ctx, const std::string& refPath, bool srgb) {
    const std::string cacheKey = "embedded:" + refPath + (srgb ? "|srgb" : "|unorm");
    for (const TextureCacheEntry& entry : ctx.cache)
        if (entry.key == cacheKey)
            return entry.index;

    const aiTexture* embedded =
        ctx.aiScene_ ? ctx.aiScene_->GetEmbeddedTexture(refPath.c_str()) : nullptr;
    if (!embedded) {
        std::cerr << "WARNING: unresolved embedded texture '" << refPath << "'\n";
        return kNoTexture;
    }

    uint32_t index = kNoTexture;
    if (embedded->mHeight == 0) {
        // Compressed blob (PNG/JPG...): mWidth is the byte size.
        stbi_set_flip_vertically_on_load(false);
        int width = 0, height = 0, comp = 0;
        uint8_t* data = stbi_load_from_memory(
            reinterpret_cast<const uint8_t*>(embedded->pcData),
            static_cast<int>(embedded->mWidth), &width, &height, &comp, 4);
        if (!data) {
            std::cerr << "WARNING: failed to decode embedded texture '" << refPath
                      << "': " << stbi_failure_reason() << "\n";
            return kNoTexture;
        }
        index = uploadPixels(ctx, data, width, height, srgb, "embedded texture");
        stbi_image_free(data);
    } else {
        // Raw BGRA8888 aiTexel array.
        const size_t texels = size_t(embedded->mWidth) * embedded->mHeight;
        std::vector<uint8_t> rgba(texels * 4);
        const aiTexel* src = embedded->pcData;
        for (size_t i = 0; i < texels; ++i) {
            rgba[i * 4 + 0] = src[i].r;
            rgba[i * 4 + 1] = src[i].g;
            rgba[i * 4 + 2] = src[i].b;
            rgba[i * 4 + 3] = src[i].a;
        }
        index = uploadPixels(ctx, rgba.data(), int(embedded->mWidth),
                             int(embedded->mHeight), srgb, "embedded texture");
    }
    if (index != kNoTexture)
        ctx.cache.push_back({ cacheKey, index });
    return index;
}

// First texture of the given assimp type, as a bindless index.
uint32_t loadFirstTexture(ImportContext& ctx, const aiMaterial* material,
                          aiTextureType type, bool srgb, bool flipVertically) {
    if (material->GetTextureCount(type) == 0)
        return kNoTexture;
    aiString path;
    if (material->GetTexture(type, 0, &path) != AI_SUCCESS)
        return kNoTexture;
    const std::string refPath = path.C_Str();
    if (!refPath.empty() && refPath[0] == '*')
        return loadEmbeddedTexture(ctx, refPath, srgb);
    return loadTextureFile(ctx, refPath, srgb, flipVertically);
}

ModelMesh processMesh(ImportContext& ctx, const aiMesh* mesh, size_t meshIndex) {
    renderer::MeshData data;
    data.vertices.reserve(mesh->mNumVertices);

    ModelMesh out;
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        renderer::Vertex vertex;
        vertex.position = { mesh->mVertices[i].x, mesh->mVertices[i].y,
                            mesh->mVertices[i].z };
        if (mesh->HasNormals())
            vertex.normal = { mesh->mNormals[i].x, mesh->mNormals[i].y,
                              mesh->mNormals[i].z };
        if (mesh->mTextureCoords[0])
            vertex.uv = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
        if (mesh->mTangents && mesh->mBitangents) {
            const glm::vec3 tangent(mesh->mTangents[i].x, mesh->mTangents[i].y,
                                    mesh->mTangents[i].z);
            const glm::vec3 bitangent(mesh->mBitangents[i].x, mesh->mBitangents[i].y,
                                      mesh->mBitangents[i].z);
            const float sign =
                glm::dot(glm::cross(vertex.normal, tangent), bitangent) < 0.0f ? -1.0f
                                                                               : 1.0f;
            vertex.tangent = glm::vec4(tangent, sign);
        }
        out.boundsMin = glm::min(out.boundsMin, vertex.position);
        out.boundsMax = glm::max(out.boundsMax, vertex.position);
        data.vertices.push_back(vertex);
    }

    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace& face = mesh->mFaces[f];
        for (unsigned int j = 0; j < face.mNumIndices; ++j)
            data.indices.push_back(face.mIndices[j]);
    }

    out.name = mesh->mName.length > 0 ? mesh->mName.C_Str()
                                      : ("Mesh_" + std::to_string(meshIndex));

    // Per-mesh bindless material from the assimp material.
    renderer::gpu::MaterialData material{};
    material.baseColor = glm::vec4(1.0f);
    material.metallic = 0.0f;
    material.roughness = 0.5f;
    material.emissive = 0.0f;
    material.normalScale = 1.0f;
    material.albedoTexture = kNoTexture;
    material.normalTexture = kNoTexture;
    material.metallicTexture = kNoTexture;
    material.roughnessTexture = kNoTexture;
    material.aoTexture = kNoTexture;

    if (mesh->mMaterialIndex < ctx.aiScene_->mNumMaterials) {
        const aiMaterial* ai = ctx.aiScene_->mMaterials[mesh->mMaterialIndex];

        aiColor3D diffuse(1.0f, 1.0f, 1.0f);
        if (ai->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
            material.baseColor = glm::vec4(diffuse.r, diffuse.g, diffuse.b, 1.0f);

        // Color maps are flipped like the GL loader (its GL-orientation
        // default); normal maps are not (its flipUVs default = false).
        material.albedoTexture =
            loadFirstTexture(ctx, ai, aiTextureType_DIFFUSE, true, true);
        material.normalTexture =
            loadFirstTexture(ctx, ai, aiTextureType_NORMALS, false, false);
        if (material.normalTexture == kNoTexture) // .obj convention
            material.normalTexture =
                loadFirstTexture(ctx, ai, aiTextureType_HEIGHT, false, false);
        material.metallicTexture =
            loadFirstTexture(ctx, ai, aiTextureType_METALNESS, false, true);
        material.roughnessTexture =
            loadFirstTexture(ctx, ai, aiTextureType_DIFFUSE_ROUGHNESS, false, true);
        material.aoTexture =
            loadFirstTexture(ctx, ai, aiTextureType_AMBIENT_OCCLUSION, false, true);
    }
    // GL parity: textured models sample the map INSTEAD of the color.
    if (material.albedoTexture != kNoTexture)
        material.baseColor = glm::vec4(1.0f);

    out.materialIndex = ctx.materials->addMaterial(material);
    out.buffer.create(*ctx.device, data, out.name.c_str());
    return out;
}

void processNode(ImportContext& ctx, const aiNode* node, Model& model) {
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = ctx.aiScene_->mMeshes[node->mMeshes[i]];
        if (mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
            continue;
        model.meshes.push_back(processMesh(ctx, mesh, model.meshes.size()));
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        processNode(ctx, node->mChildren[i], model);
}

} // namespace

bool importModelFile(const std::string& path, rhi::Device& device,
                     renderer::MaterialSystem& materials, Model& out) {
    std::error_code ec;
    if (!fs::exists(path, ec)) {
        std::cerr << "ERROR: model file not found: " << path << "\n";
        return false;
    }

    Assimp::Importer importer;
    // GL preferences defaults: triangulate + smooth normals + tangents +
    // join vertices + sort by type.
    const unsigned int flags = aiProcess_Triangulate | aiProcess_GenSmoothNormals |
                               aiProcess_CalcTangentSpace |
                               aiProcess_JoinIdenticalVertices | aiProcess_SortByPType;
    const aiScene* aiScene_ = importer.ReadFile(path, flags);
    if (!aiScene_ || !aiScene_->mRootNode) {
        std::cerr << "ERROR: Assimp failed to load '" << path
                  << "': " << importer.GetErrorString() << "\n";
        return false;
    }

    ImportContext ctx;
    ctx.device = &device;
    ctx.materials = &materials;
    ctx.aiScene_ = aiScene_;
    const size_t lastSlash = path.find_last_of("/\\");
    ctx.directory = (lastSlash != std::string::npos) ? path.substr(0, lastSlash) : ".";

    out = Model{};
    const std::string filename =
        (lastSlash != std::string::npos) ? path.substr(lastSlash + 1) : path;
    out.name = filename.substr(0, filename.find_last_of('.'));
    processNode(ctx, aiScene_->mRootNode, out);

    if (out.meshes.empty()) {
        std::cerr << "ERROR: '" << path << "' contained no drawable meshes\n";
        return false;
    }
    std::cout << "Imported model " << path << " (" << out.meshes.size()
              << " meshes)\n";
    return true;
}

} // namespace scene
