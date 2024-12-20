// model_loader.cpp
#include "model_loader.h"
#include <stb_image.h>
#include <map>
#include <filesystem>

namespace Engine {

    void Mesh::setupMesh() {
        glGenVertexArrays(1, &VAO);
        glGenBuffers(1, &VBO);
        glGenBuffers(1, &EBO);

        glBindVertexArray(VAO);
        glBindBuffer(GL_ARRAY_BUFFER, VBO);
        glBufferData(GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), &vertices[0], GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, EBO);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(GLuint), &indices[0], GL_STATIC_DRAW);

        // Vertex positions
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)0);

        // Vertex normals
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal));

        // Vertex texture coords
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, texCoords));

        // Vertex tangent
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));

        // Vertex bitangent
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, bitangent));

        glBindVertexArray(0);
    }

    void Mesh::Draw(Shader& shader) {
        unsigned int diffuseNr = 0;
        unsigned int specularNr = 0;
        unsigned int normalNr = 0;
        unsigned int aoNr = 0;

        // Bind appropriate textures
        for (unsigned int i = 0; i < textures.size(); i++) {
            glActiveTexture(GL_TEXTURE0 + i);

            int textureUnit = 0;
            std::string name = textures[i].type;

            if (name == "texture_diffuse") {
                textureUnit = diffuseNr++;
            }
            else if (name == "texture_specular") {
                textureUnit = specularNr++;
            }
            else if (name == "texture_normal") {
                textureUnit = normalNr++;
            }
            else if (name == "texture_ao") {
                textureUnit = aoNr++;
            }

            // Set texture unit in shader
            shader.setInt("material.textures[" + std::to_string(i) + "]", i);

            // Bind texture
            glBindTexture(GL_TEXTURE_2D, textures[i].id);
        }

        // Set texture counts
        shader.setInt("material.numDiffuseTextures", diffuseNr);
        shader.setInt("material.numSpecularTextures", specularNr);
        shader.setInt("material.numNormalTextures", normalNr);

        // Draw mesh
        glBindVertexArray(VAO);
        glDrawElements(GL_TRIANGLES, indices.size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);
    }

    Model::Model(const std::string& path) {
        // Save the original path
        this->path = path;

        directory = path.substr(0, path.find_last_of("/\\"));

        // Extract filename without extension for the model name
        std::string filename = path.substr(path.find_last_of("/\\") + 1);
        name = filename.substr(0, filename.find_last_of('.'));

        loadModel(path);

        // Calculate bounding sphere radius
        float maxDistSq = 0.0f;
        for (const auto& mesh : meshes) {
            for (const auto& vertex : mesh.vertices) {
                float distSq = glm::dot(vertex.position, vertex.position);
                maxDistSq = std::max(maxDistSq, distSq);
            }
        }
        boundingSphereRadius = std::sqrt(maxDistSq);
    }

    void Model::loadModel(const std::string& path) {
        Assimp::Importer importer;
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType);

        if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE || !scene->mRootNode) {
            std::cerr << "Assimp error: " << importer.GetErrorString() << std::endl;
            return;
        }

        directory = path.substr(0, path.find_last_of('/'));
        processNode(scene->mRootNode, scene);
    }

    void Model::processNode(aiNode* node, const aiScene* scene) {
        // Process all meshes in current node
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene));
        }

        // Process children nodes
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    }

    Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene) {
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;
        std::vector<Texture> textures;

        // Process vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;

            vertex.position.x = mesh->mVertices[i].x;
            vertex.position.y = mesh->mVertices[i].y;
            vertex.position.z = mesh->mVertices[i].z;

            if (mesh->HasNormals()) {
                vertex.normal.x = mesh->mNormals[i].x;
                vertex.normal.y = mesh->mNormals[i].y;
                vertex.normal.z = mesh->mNormals[i].z;
            }

            if (mesh->mTextureCoords[0]) {
                vertex.texCoords.x = mesh->mTextureCoords[0][i].x;
                vertex.texCoords.y = mesh->mTextureCoords[0][i].y;

                vertex.tangent.x = mesh->mTangents[i].x;
                vertex.tangent.y = mesh->mTangents[i].y;
                vertex.tangent.z = mesh->mTangents[i].z;

                vertex.bitangent.x = mesh->mBitangents[i].x;
                vertex.bitangent.y = mesh->mBitangents[i].y;
                vertex.bitangent.z = mesh->mBitangents[i].z;
            }
            else {
                vertex.texCoords = glm::vec2(0.0f);
            }

            vertices.push_back(vertex);
        }

        // Process indices
        for (unsigned int i = 0; i < mesh->mNumFaces; i++) {
            aiFace face = mesh->mFaces[i];
            for (unsigned int j = 0; j < face.mNumIndices; j++) {
                indices.push_back(face.mIndices[j]);
            }
        }

        // Process material
        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

            // Load textures for this mesh
            std::vector<Texture> diffuseMaps = loadMaterialTextures(material, aiTextureType_DIFFUSE, "texture_diffuse");
            textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());

            std::vector<Texture> specularMaps = loadMaterialTextures(material, aiTextureType_SPECULAR, "texture_specular");
            textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());

            std::vector<Texture> normalMaps = loadMaterialTextures(material, aiTextureType_HEIGHT, "texture_normal");
            textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());

            std::vector<Texture> aoMaps = loadMaterialTextures(material, aiTextureType_AMBIENT_OCCLUSION, "texture_ao");
            textures.insert(textures.end(), aoMaps.begin(), aoMaps.end());
        }

        return Mesh(vertices, indices, textures);
    }

    std::vector<Texture> Model::loadMaterialTextures(aiMaterial* mat, aiTextureType type, const std::string& typeName) {
        std::vector<Texture> textures;
        for (unsigned int i = 0; i < mat->GetTextureCount(type); i++) {
            aiString str;
            mat->GetTexture(type, i, &str);

            // Check if texture was loaded before
            bool skip = false;
            for (const auto& loadedTex : loadedTextures) {
                if (std::strcmp(loadedTex.path.c_str(), str.C_Str()) == 0) {
                    textures.push_back(loadedTex);
                    skip = true;
                    break;
                }
            }

            if (!skip) {
                Texture texture;
                // Store both the original reference path and the full successful path
                texture.path = str.C_Str();
                texture.id = TextureFromFile(str.C_Str(), directory, texture.fullPath); // Modified to store full path
                texture.type = typeName;
                textures.push_back(texture);
                loadedTextures.push_back(texture);
            }
        }
        return textures;
    }

    GLuint Model::TextureFromFile(const char* path, const std::string& directory, std::string& outFullPath) {

        // First, get the base directory without the model file name
        std::string dir = directory;
        size_t lastSlash = dir.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            dir = dir.substr(0, lastSlash);
        }

        // Get just the filename from the path
        std::string textureName = std::string(path);
        size_t lastSlashTex = textureName.find_last_of("/\\");
        if (lastSlashTex != std::string::npos) {
            textureName = textureName.substr(lastSlashTex + 1);
        }

        // Map common texture names to actual file names
        std::map<std::string, std::vector<std::string>> textureAliases = {
            {"diffuse.jpg", {"diffuse.jpg", "ao.jpg", "albedo.jpg"}},
            {"specular.jpg", {"specular.jpg", "roughness.jpg", "metallic.jpg"}},
            {"normal.png", {"normal.png", "normalmap.png"}}
        };

        // Create list of paths to try
        std::vector<std::string> pathsToTry;

        // If the texture name is in our alias map, try all its variants
        auto it = textureAliases.find(textureName);
        if (it != textureAliases.end()) {
            for (const auto& alias : it->second) {
                pathsToTry.push_back(dir + "/" + alias);
                pathsToTry.push_back(dir + "/textures/" + alias);
                pathsToTry.push_back("textures/" + alias);
                pathsToTry.push_back(alias);
            }
        }
        else {
            // If not in alias map, try original name
            pathsToTry.push_back(dir + "/" + textureName);
            pathsToTry.push_back(dir + "/textures/" + textureName);
            pathsToTry.push_back("textures/" + textureName);
            pathsToTry.push_back(textureName);
        }

        // Try each path
        unsigned char* data = nullptr;
        int width, height, nrComponents;
        std::string successPath;

        for (const auto& tryPath : pathsToTry) {
            std::cout << "Trying path: " << tryPath << std::endl;
            data = stbi_load(tryPath.c_str(), &width, &height, &nrComponents, 0);
            if (data) {
                successPath = tryPath;
                // Get full path using canonical form
                std::error_code ec;
                std::filesystem::path canonicalPath = std::filesystem::canonical(tryPath, ec);
                outFullPath = ec ? tryPath : canonicalPath.string(); // If canonical fails, use original path
                break;
            }
        }

        GLuint textureID;
        glGenTextures(1, &textureID);

        if (data) {
            GLenum format = GL_RGB;
            if (nrComponents == 1)
                format = GL_RED;
            else if (nrComponents == 3)
                format = GL_RGB;
            else if (nrComponents == 4)
                format = GL_RGBA;

            glBindTexture(GL_TEXTURE_2D, textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            stbi_image_free(data);
            std::cout << "Successfully loaded texture: " << successPath << std::endl;
        }
        else {
            std::cout << "Failed to load texture from all attempted paths for: " << textureName << std::endl;
            std::cout << "STB Image error: " << stbi_failure_reason() << std::endl;

            // Create a default white texture
            unsigned char white[] = { 255, 255, 255, 255 };
            glBindTexture(GL_TEXTURE_2D, textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, white);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        return textureID;
    }

    void Model::Draw(Shader& shader) {
        // Set material properties
        shader.setBool("material.hasNormalMap", hasNormalMap());
        shader.setBool("material.hasSpecularMap", hasSpecularMap());
        shader.setBool("material.hasAOMap", hasAOMap());
        shader.setFloat("material.hasTexture", !meshes.empty() && !meshes[0].textures.empty() ? 1.0f : 0.0f);

        // Explicitly set the object color before drawing
        shader.setVec3("material.objectColor", color);  // Make sure this is called
        shader.setFloat("material.shininess", shininess);
        shader.setFloat("material.emissive", emissive);

        // Draw all meshes
        for (unsigned int i = 0; i < meshes.size(); i++) {
            meshes[i].Draw(shader);
        }

        // Reset textures
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_2D, 0);
    }

    Model* loadModel(const std::string& filePath) {
        return new Model(filePath);
    }



    Engine::Model createCube(const glm::vec3& color, float shininess, float emissive) {
        // Create vertices and indices for a cube
        std::vector<Vertex> vertices = {
            // Front face
            {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 0.0f}},  // Bottom-left
            {{0.5f, -0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 0.0f}},   // Bottom-right
            {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {1.0f, 1.0f}},    // Top-right
            {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f, 1.0f}, {0.0f, 1.0f}},   // Top-left

            // Back face
            {{-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 0.0f}}, // Bottom-left
            {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {1.0f, 1.0f}},  // Top-left
            {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 1.0f}},   // Top-right
            {{0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f}, {0.0f, 0.0f}},  // Bottom-right

            // Top face
            {{-0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 0.0f}},  // Back-left
            {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {0.0f, 1.0f}},   // Front-left
            {{0.5f, 0.5f, 0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 1.0f}},    // Front-right
            {{0.5f, 0.5f, -0.5f}, {0.0f, 1.0f, 0.0f}, {1.0f, 0.0f}},   // Back-right

            // Bottom face
            {{-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 1.0f}}, // Back-left
            {{0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 1.0f}},  // Back-right
            {{0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {1.0f, 0.0f}},   // Front-right
            {{-0.5f, -0.5f, 0.5f}, {0.0f, -1.0f, 0.0f}, {0.0f, 0.0f}},  // Front-left

            // Right face
            {{0.5f, -0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},  // Bottom-back
            {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},   // Top-back
            {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}},    // Top-front
            {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}},   // Bottom-front

            // Left face
            {{-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 0.0f}}, // Bottom-back
            {{-0.5f, -0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 0.0f}},  // Bottom-front
            {{-0.5f, 0.5f, 0.5f}, {-1.0f, 0.0f, 0.0f}, {0.0f, 1.0f}},   // Top-front
            {{-0.5f, 0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f}, {1.0f, 1.0f}}   // Top-back
        };

        // Calculate tangent and bitangent vectors
        for (auto& vertex : vertices) {
            vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
            vertex.bitangent = glm::vec3(0.0f, 1.0f, 0.0f);
        }

        // Create indices for the cube
        std::vector<unsigned int> indices = {
            // Front face
            0, 1, 2, 2, 3, 0,
            // Back face
            4, 5, 6, 6, 7, 4,
            // Top face
            8, 9, 10, 10, 11, 8,
            // Bottom face
            12, 13, 14, 14, 15, 12,
            // Right face
            16, 17, 18, 18, 19, 16,
            // Left face
            20, 21, 22, 22, 23, 20
        };

        // Create a single mesh for the cube
        Mesh cubeMesh(vertices, indices, std::vector<Texture>());

        // Create a Model and add the mesh
        Model cubeModel("cube");
        cubeModel.meshes.push_back(cubeMesh);

        // Set model properties
        static int cubeCounter = 0;
        cubeModel.name = "Cube_" + std::to_string(cubeCounter++);
        cubeModel.color = color;  // Make sure this is set
        cubeModel.shininess = shininess;
        cubeModel.emissive = emissive;
        cubeModel.position = glm::vec3(0.0f);
        cubeModel.scale = glm::vec3(1.0f);
        cubeModel.rotation = glm::vec3(0.0f);
        cubeModel.visible = true;

        return cubeModel;
    }

    inline GLuint createDefaultWhiteTexture() {
        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        unsigned char texData[] = { 255, 255, 255, 255 };
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        return textureID;
    }


} // namespace Engine