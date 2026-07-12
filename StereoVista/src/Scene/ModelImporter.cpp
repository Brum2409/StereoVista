#include "Scene/Scene.h"

#include "RHI/Device.h"
#include "RHI/Texture.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/MaterialSystem.h"

#include <assimp/GltfMaterial.h> // AI_MATKEY_GLTF_ALPHAMODE / _ALPHACUTOFF
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <assimp/scene.h>

#include <glm/gtc/matrix_inverse.hpp>

#include <stb_image.h>

#include <algorithm>
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
    // Embedded textures are referenced either as "*N" indices or by name
    // (FBX embeds under the original filename) — resolve against the scene's
    // embedded table first, only then search the filesystem.
    if (!refPath.empty() &&
        (refPath[0] == '*' || ctx.aiScene_->GetEmbeddedTexture(refPath.c_str())))
        return loadEmbeddedTexture(ctx, refPath, srgb);
    return loadTextureFile(ctx, refPath, srgb, flipVertically);
}

// aiMatrix4x4 is row-major; glm is column-major.
glm::mat4 toGlm(const aiMatrix4x4& m) {
    return glm::mat4(m.a1, m.b1, m.c1, m.d1,  // column 0
                     m.a2, m.b2, m.c2, m.d2,
                     m.a3, m.b3, m.c3, m.d3,
                     m.a4, m.b4, m.c4, m.d4);
}

ModelMesh processMesh(ImportContext& ctx, const aiMesh* mesh, size_t meshIndex,
                      const glm::mat4& nodeTransform) {
    renderer::MeshData data;
    data.vertices.reserve(mesh->mNumVertices);

    // Bake the accumulated node transform into the vertices (one DrawItem per
    // mesh keeps the render side flat). Directions use the inverse-transpose
    // so non-uniform node scales don't shear the normals; a mirroring
    // transform (negative determinant) flips the winding, undone below by
    // reversing each triangle.
    const glm::mat3 linear(nodeTransform);
    const glm::mat3 normalMat = glm::inverseTranspose(linear);
    const bool mirrored = glm::determinant(linear) < 0.0f;

    ModelMesh out;
    for (unsigned int i = 0; i < mesh->mNumVertices; ++i) {
        renderer::Vertex vertex;
        vertex.position = glm::vec3(
            nodeTransform * glm::vec4(mesh->mVertices[i].x, mesh->mVertices[i].y,
                                      mesh->mVertices[i].z, 1.0f));
        if (mesh->HasNormals()) {
            const glm::vec3 n = normalMat * glm::vec3(mesh->mNormals[i].x,
                                                      mesh->mNormals[i].y,
                                                      mesh->mNormals[i].z);
            const float len = glm::length(n);
            if (len > 1e-12f)
                vertex.normal = n / len;
        }
        if (mesh->mTextureCoords[0])
            vertex.uv = { mesh->mTextureCoords[0][i].x, mesh->mTextureCoords[0][i].y };
        if (mesh->mTangents && mesh->mBitangents) {
            const glm::vec3 tangent =
                linear * glm::vec3(mesh->mTangents[i].x, mesh->mTangents[i].y,
                                   mesh->mTangents[i].z);
            const glm::vec3 bitangent =
                linear * glm::vec3(mesh->mBitangents[i].x, mesh->mBitangents[i].y,
                                   mesh->mBitangents[i].z);
            const float sign =
                glm::dot(glm::cross(vertex.normal, tangent), bitangent) < 0.0f ? -1.0f
                                                                               : 1.0f;
            const float len = glm::length(tangent);
            vertex.tangent = glm::vec4(
                len > 1e-12f ? tangent / len : glm::vec3(1.0f, 0.0f, 0.0f), sign);
        }
        out.boundsMin = glm::min(out.boundsMin, vertex.position);
        out.boundsMax = glm::max(out.boundsMax, vertex.position);
        data.vertices.push_back(vertex);
    }

    for (unsigned int f = 0; f < mesh->mNumFaces; ++f) {
        const aiFace& face = mesh->mFaces[f];
        if (mirrored && face.mNumIndices == 3) {
            data.indices.push_back(face.mIndices[0]);
            data.indices.push_back(face.mIndices[2]);
            data.indices.push_back(face.mIndices[1]);
            continue;
        }
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
    material.alphaCutoff = 0.5f; // glTF default; unused while the flag is off

    if (mesh->mMaterialIndex < ctx.aiScene_->mNumMaterials) {
        const aiMaterial* ai = ctx.aiScene_->mMaterials[mesh->mMaterialIndex];

        // PBR base color (carries alpha) wins over the legacy diffuse color.
        aiColor4D base(1.0f, 1.0f, 1.0f, 1.0f);
        aiColor3D diffuse(1.0f, 1.0f, 1.0f);
        if (ai->Get(AI_MATKEY_BASE_COLOR, base) == AI_SUCCESS)
            material.baseColor = glm::vec4(base.r, base.g, base.b, base.a);
        else if (ai->Get(AI_MATKEY_COLOR_DIFFUSE, diffuse) == AI_SUCCESS)
            material.baseColor = glm::vec4(diffuse.r, diffuse.g, diffuse.b, 1.0f);

        // Every map is flipped the same way (GL-orientation load): mixing
        // flipped color maps with unflipped normal maps — the GL loader's
        // accident — sampled the two at different texels of the same UV.
        material.albedoTexture =
            loadFirstTexture(ctx, ai, aiTextureType_BASE_COLOR, true, true);
        if (material.albedoTexture == kNoTexture)
            material.albedoTexture =
                loadFirstTexture(ctx, ai, aiTextureType_DIFFUSE, true, true);
        material.normalTexture =
            loadFirstTexture(ctx, ai, aiTextureType_NORMALS, false, true);
        if (material.normalTexture == kNoTexture) // .obj convention
            material.normalTexture =
                loadFirstTexture(ctx, ai, aiTextureType_HEIGHT, false, true);
        material.metallicTexture =
            loadFirstTexture(ctx, ai, aiTextureType_METALNESS, false, true);
        material.roughnessTexture =
            loadFirstTexture(ctx, ai, aiTextureType_DIFFUSE_ROUGHNESS, false, true);
        material.aoTexture =
            loadFirstTexture(ctx, ai, aiTextureType_AMBIENT_OCCLUSION, false, true);

        // Metallic/roughness FACTORS multiply the maps in the shader, so a
        // missing factor must default to 1 when a map exists (glTF semantics)
        // — the old fixed 0.0 metallic zeroed every metalness texture.
        float metallic = 0.0f;
        if (ai->Get(AI_MATKEY_METALLIC_FACTOR, metallic) != AI_SUCCESS)
            metallic = material.metallicTexture != kNoTexture ? 1.0f : 0.0f;
        material.metallic = metallic;
        float roughness = 0.5f;
        if (ai->Get(AI_MATKEY_ROUGHNESS_FACTOR, roughness) != AI_SUCCESS)
            roughness = material.roughnessTexture != kNoTexture ? 1.0f : 0.5f;
        material.roughness = roughness;

        // The material model's emissive is scalar (emitted = albedo *
        // emissive); fold an authored emissive color to its max channel.
        aiColor3D emissive(0.0f, 0.0f, 0.0f);
        if (ai->Get(AI_MATKEY_COLOR_EMISSIVE, emissive) == AI_SUCCESS)
            material.emissive = std::max(emissive.r, std::max(emissive.g, emissive.b));

        // glTF cutout transparency (MASK) -> alpha-mask discard in mesh.frag.
        aiString alphaMode;
        if (ai->Get(AI_MATKEY_GLTF_ALPHAMODE, alphaMode) == AI_SUCCESS &&
            std::strcmp(alphaMode.C_Str(), "MASK") == 0) {
            material.flags |= SV_MATERIAL_ALPHA_MASK;
            float cutoff = 0.5f;
            ai->Get(AI_MATKEY_GLTF_ALPHACUTOFF, cutoff);
            material.alphaCutoff = cutoff;
        }
    }
    // GL parity: textured models sample the map INSTEAD of the color (alpha
    // included — the map's alpha drives the mask test).
    if (material.albedoTexture != kNoTexture)
        material.baseColor = glm::vec4(1.0f);

    out.materialIndex = ctx.materials->addMaterial(material);
    out.buffer.create(*ctx.device, data, out.name.c_str());
    return out;
}

void processNode(ImportContext& ctx, const aiNode* node, Model& model,
                 const glm::mat4& parentTransform) {
    // Accumulate the node hierarchy's transforms and bake them into each
    // mesh. The old loader dropped aiNode::mTransformation entirely, so any
    // format with a real node hierarchy (FBX, glTF, DAE) imported with every
    // part collapsed onto the model origin.
    const glm::mat4 transform = parentTransform * toGlm(node->mTransformation);
    for (unsigned int i = 0; i < node->mNumMeshes; ++i) {
        const aiMesh* mesh = ctx.aiScene_->mMeshes[node->mMeshes[i]];
        if (mesh->mNumVertices == 0 || mesh->mNumFaces == 0)
            continue;
        model.meshes.push_back(
            processMesh(ctx, mesh, model.meshes.size(), transform));
    }
    for (unsigned int i = 0; i < node->mNumChildren; ++i)
        processNode(ctx, node->mChildren[i], model, transform);
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
    // Absolute source path: Duplicate rebuilds from it, and the v3 scene
    // format stores it (scene-relative primary + this absolute fallback).
    {
        std::error_code ec;
        out.sourcePath = fs::absolute(path, ec).string();
        if (ec || out.sourcePath.empty())
            out.sourcePath = path;
    }
    processNode(ctx, aiScene_->mRootNode, out, glm::mat4(1.0f));

    if (out.meshes.empty()) {
        std::cerr << "ERROR: '" << path << "' contained no drawable meshes\n";
        return false;
    }
    std::cout << "Imported model " << path << " (" << out.meshes.size()
              << " meshes)\n";
    return true;
}

} // namespace scene
