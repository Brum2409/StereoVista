// model_loader.h
#pragma once

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/postprocess.h>
#include "core.h"

namespace Engine {

    class Shader; // Forward declaration

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

    private:


        std::vector<Texture> loadedTextures;

        void loadModel(const std::string& path);
        void processNode(aiNode* node, const aiScene* scene);
        Mesh processMesh(aiMesh* mesh, const aiScene* scene, size_t meshIndex);
        std::vector<Texture> loadMaterialTextures(aiMaterial* mat, aiTextureType type, const std::string& typeName);

    };

    // Factory functions
    Model* loadModel(const std::string& filePath);
    Model createCube(const glm::vec3& color, float shininess, float emissive);

} // namespace Engine