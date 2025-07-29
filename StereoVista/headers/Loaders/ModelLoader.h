// model_loader.h
#pragma once

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "../Engine/Core.h"

// Material type enum for presets
enum class MaterialType {
    CONCRETE,  // Default
    METAL,
    PLASTIC,
    GLASS,
    WOOD,
    MARBLE,
    CUSTOM     // For manual settings
};

namespace Engine {

    class Shader;

    struct Texture {
        GLuint id;
        std::string type;
        std::string path;      // Original reference path from model file
        std::string fullPath;  // Full filesystem path of the actual texture file
    };

    class Mesh {
    public:
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;
        std::vector<Texture> textures;
        GLuint VAO, VBO, EBO;

        bool visible = true;
        glm::vec3 color = glm::vec3(1.0f);
        float shininess = 32.0f;
        float emissive = 0.0f;
        std::string name;  // Optional: for better identification

        Mesh(std::vector<Vertex> vertices, std::vector<GLuint> indices, std::vector<Texture> textures)
            : vertices(vertices), indices(indices), textures(textures),
            visible(true), color(1.0f), shininess(32.0f), emissive(0.0f) {
            setupMesh();
        }

        void Draw(Shader& shader);

    private:
        void setupMesh();
    };

    GLuint createDefaultWhiteTexture();

    class Model {
    public:
        // Constructor
        Model() = default;
        Model(const std::string& path);

        // Public methods
        void Draw(Shader& shader);

        // Public properties
        std::string name;
        std::string path;
        glm::vec3 position = glm::vec3(0.0f);
        glm::vec3 scale = glm::vec3(1.0f);
        glm::vec3 rotation = glm::vec3(0.0f);
        bool selected = false;
        glm::vec3 color = glm::vec3(1.0f);
        float shininess = 1.0f;
        float emissive = 0.0f;

        float diffuseReflectivity = 0.8f;    // Controls strength of diffuse reflection
        glm::vec3 specularColor = glm::vec3(1.0f); // Specular color (default white)
        float specularDiffusion = 0.5f;      // Controls glossiness/roughness
        float specularReflectivity = 0.0f;   // Controls strength of specular reflection
        float refractiveIndex = 1.0f;        // For glass/water effects (1.0 = no refraction)
        float transparency = 0.0f;

        MaterialType materialType = MaterialType::CONCRETE;  // Current material preset

        bool visible = true;
        float boundingSphereRadius = 0.0f;
        std::string directory;
        std::vector<bool> selectedMeshes;
        static GLuint TextureFromFile(const char* path, const std::string& directory, std::string& outFullPath);
        const std::vector<Mesh>& getMeshes() const { return meshes; }
        std::vector<Mesh>& getMeshes() { return meshes; }

        std::vector<Mesh> meshes;

        bool hasNormalMap() const {
            if (meshes.empty()) return false;
            for (const auto& texture : meshes[0].textures) {
                if (texture.type == "texture_normal") return true;
            }
            return false;
        }

        void initializeMeshSelection() {
            selectedMeshes.resize(meshes.size(), false);
        }

        bool hasSpecularMap() const {
            if (meshes.empty()) return false;
            for (const auto& texture : meshes[0].textures) {
                if (texture.type == "texture_specular") return true;
            }
            return false;
        }

        bool hasAOMap() const {
            if (meshes.empty()) return false;
            for (const auto& texture : meshes[0].textures) {
                if (texture.type == "texture_ao") return true;
            }
            return false;
        }

        // Apply material preset based on type
        void applyMaterialPreset(MaterialType type) {
            materialType = type;

            switch (type) {
            case MaterialType::CONCRETE:
                diffuseReflectivity = 0.8f;
                specularColor = glm::vec3(0.8f, 0.8f, 0.8f);
                specularDiffusion = 0.7f;       // More rough
                specularReflectivity = 0.1f;     // Low reflectivity
                refractiveIndex = 1.0f;          // No refraction
                transparency = 0.0f;             // Opaque
                break;

            case MaterialType::METAL:
                diffuseReflectivity = 0.4f;      // Lower diffuse for metals
                specularColor = glm::vec3(0.95f, 0.95f, 0.95f); // Bright specular
                specularDiffusion = 0.1f;        // Low diffusion (smooth)
                specularReflectivity = 0.9f;     // High reflectivity
                refractiveIndex = 1.0f;          // No refraction
                transparency = 0.0f;             // Opaque
                break;

            case MaterialType::PLASTIC:
                diffuseReflectivity = 0.7f;
                specularColor = glm::vec3(1.0f, 1.0f, 1.0f);
                specularDiffusion = 0.3f;        // Moderate smoothness
                specularReflectivity = 0.3f;     // Moderate reflection
                refractiveIndex = 1.05f;         // Slight refraction
                transparency = 0.0f;             // Opaque
                break;

            case MaterialType::GLASS:
                diffuseReflectivity = 0.1f;      // Very low diffuse
                specularColor = glm::vec3(1.0f, 1.0f, 1.0f);
                specularDiffusion = 0.05f;       // Very smooth
                specularReflectivity = 0.8f;     // High reflectivity
                refractiveIndex = 1.5f;          // High refraction
                transparency = 0.9f;             // Highly transparent
                break;

            case MaterialType::WOOD:
                diffuseReflectivity = 0.9f;      // High diffuse
                specularColor = glm::vec3(0.7f, 0.6f, 0.5f); // Warm specular
                specularDiffusion = 0.6f;        // Fairly rough
                specularReflectivity = 0.15f;    // Low reflectivity
                refractiveIndex = 1.0f;          // No refraction
                transparency = 0.0f;             // Opaque
                break;

            case MaterialType::MARBLE:
                diffuseReflectivity = 0.6f;      // Moderate diffuse
                specularColor = glm::vec3(0.9f, 0.9f, 0.9f);
                specularDiffusion = 0.25f;       // Fairly smooth
                specularReflectivity = 0.4f;     // Moderate reflectivity
                refractiveIndex = 1.0f;          // No refraction
                transparency = 0.0f;             // Opaque
                break;

            case MaterialType::CUSTOM:
                // Do nothing, keep current values
                break;
            }
        }

    private:
        std::vector<Texture> loadedTextures;

        void loadModel(const std::string& path);
        void processNode(aiNode* node, const aiScene* scene);
        Mesh processMesh(aiMesh* mesh, const aiScene* scene, size_t meshIndex);
        std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, const std::string& typeName);
        GLuint loadEmbeddedTexture(const std::string& embeddedPath, std::string& outFullPath);

    };

    // Factory functions
    Model* loadModel(const std::string& filePath);
    Model createCube(const glm::vec3& color, float shininess, float emissive);

}