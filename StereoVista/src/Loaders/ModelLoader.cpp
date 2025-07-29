// model_loader.cpp
#include "Loaders/ModelLoader.h"
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
        if (!visible) return;

        unsigned int diffuseNr = 0;
        unsigned int specularNr = 0;
        unsigned int normalNr = 0;
        unsigned int aoNr = 0;

        // Bind appropriate textures
        for (unsigned int i = 0; i < textures.size(); i++) {
            glActiveTexture(GL_TEXTURE0 + i);

            std::string name = textures[i].type;

            if (name == "texture_diffuse") {
                diffuseNr++;
            }
            else if (name == "texture_specular") {
                specularNr++;
            }
            else if (name == "texture_normal") {
                normalNr++;
            }
            else if (name == "texture_ao") {
                aoNr++;
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
        
        // Original working flags to preserve normals and textures
        const aiScene* scene = importer.ReadFile(path,
            aiProcess_Triangulate |
            aiProcess_GenNormals |
            aiProcess_CalcTangentSpace |
            aiProcess_JoinIdenticalVertices |
            aiProcess_SortByPType);

        if (!scene) {
            std::cerr << "ERROR: Assimp failed to load model '" << path << "'" << std::endl;
            std::cerr << "Reason: " << importer.GetErrorString() << std::endl;
            return;
        }
        
        if (scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE) {
            std::cerr << "WARNING: Model '" << path << "' loaded with incomplete data" << std::endl;
        }
        
        if (!scene->mRootNode) {
            std::cerr << "ERROR: No root node found in model '" << path << "'" << std::endl;
            return;
        }
        
        std::cout << "Successfully loaded model: " << path << std::endl;
        std::cout << "Meshes: " << scene->mNumMeshes << ", Materials: " << scene->mNumMaterials << std::endl;

        directory = path.substr(0, path.find_last_of('/'));
        processNode(scene->mRootNode, scene);
        
        std::cout << "Model processing complete. Total meshes processed: " << meshes.size() << std::endl;
    }

    void Model::processNode(aiNode* node, const aiScene* scene) {
        // Process all meshes in current node
        for (unsigned int i = 0; i < node->mNumMeshes; i++) {
            aiMesh* mesh = scene->mMeshes[node->mMeshes[i]];
            meshes.push_back(processMesh(mesh, scene, meshes.size())); // Pass current mesh count as index
        }

        // Process children nodes
        for (unsigned int i = 0; i < node->mNumChildren; i++) {
            processNode(node->mChildren[i], scene);
        }
    }

    Mesh Model::processMesh(aiMesh* mesh, const aiScene* scene, size_t meshIndex) {
        std::vector<Vertex> vertices;
        std::vector<GLuint> indices;
        std::vector<Texture> textures;

        // Process vertices
        for (unsigned int i = 0; i < mesh->mNumVertices; i++) {
            Vertex vertex;

            // Position (always present)
            vertex.position.x = mesh->mVertices[i].x;
            vertex.position.y = mesh->mVertices[i].y;
            vertex.position.z = mesh->mVertices[i].z;

            // Normals (check if available)
            if (mesh->HasNormals()) {
                vertex.normal.x = mesh->mNormals[i].x;
                vertex.normal.y = mesh->mNormals[i].y;
                vertex.normal.z = mesh->mNormals[i].z;
            } else {
                vertex.normal = glm::vec3(0.0f, 1.0f, 0.0f); // Default up vector
            }

            // Texture coordinates (check if available)
            if (mesh->mTextureCoords[0]) {
                vertex.texCoords.x = mesh->mTextureCoords[0][i].x;
                vertex.texCoords.y = mesh->mTextureCoords[0][i].y;
            } else {
                vertex.texCoords = glm::vec2(0.0f, 0.0f);
            }

            // Tangents and bitangents (check if available)
            if (mesh->mTangents && mesh->mBitangents) {
                vertex.tangent.x = mesh->mTangents[i].x;
                vertex.tangent.y = mesh->mTangents[i].y;
                vertex.tangent.z = mesh->mTangents[i].z;

                vertex.bitangent.x = mesh->mBitangents[i].x;
                vertex.bitangent.y = mesh->mBitangents[i].y;
                vertex.bitangent.z = mesh->mBitangents[i].z;
            } else {
                // Calculate default tangent and bitangent
                vertex.tangent = glm::vec3(1.0f, 0.0f, 0.0f);
                vertex.bitangent = glm::vec3(0.0f, 0.0f, 1.0f);
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

            // Load textures for this specific mesh
            std::vector<Texture> diffuseMaps = loadMaterialTextures(material,
                aiTextureType_DIFFUSE, "texture_diffuse");
            textures.insert(textures.end(), diffuseMaps.begin(), diffuseMaps.end());

            std::vector<Texture> specularMaps = loadMaterialTextures(material,
                aiTextureType_SPECULAR, "texture_specular");
            textures.insert(textures.end(), specularMaps.begin(), specularMaps.end());

            std::vector<Texture> normalMaps = loadMaterialTextures(material,
                aiTextureType_HEIGHT, "texture_normal");
            textures.insert(textures.end(), normalMaps.begin(), normalMaps.end());

            std::vector<Texture> aoMaps = loadMaterialTextures(material,
                aiTextureType_AMBIENT_OCCLUSION, "texture_ao");
            textures.insert(textures.end(), aoMaps.begin(), aoMaps.end());

            // Store material properties
            aiColor3D color(0.f, 0.f, 0.f);
            material->Get(AI_MATKEY_COLOR_DIFFUSE, color);
        }

        Mesh result(vertices, indices, textures);

        // Initialize mesh-specific properties from material if available
        if (mesh->mMaterialIndex >= 0) {
            aiMaterial* material = scene->mMaterials[mesh->mMaterialIndex];

            aiColor3D color(0.f, 0.f, 0.f);
            if (AI_SUCCESS == material->Get(AI_MATKEY_COLOR_DIFFUSE, color)) {
                result.color = glm::vec3(color.r, color.g, color.b);
            }

            float shininess = 32.0f;
            if (AI_SUCCESS == material->Get(AI_MATKEY_SHININESS, shininess)) {
                result.shininess = shininess;
            }

            // Set name if available
            if (mesh->mName.length > 0) {
                result.name = mesh->mName.C_Str();
            }
            else {
                result.name = "Mesh_" + std::to_string(meshIndex);
            }
        }

        return result;
    }

    std::vector<Texture> Model::loadMaterialTextures(aiMaterial* mat, aiTextureType type, const std::string& typeName) {
        std::vector<Texture> textures;
        unsigned int textureCount = mat->GetTextureCount(type);
        
        if (textureCount > 0) {
            std::cout << "Loading " << textureCount << " textures of type: " << typeName << std::endl;
        }
        
        for (unsigned int i = 0; i < textureCount; i++) {
            aiString texturePath;
            aiTextureMapping mapping;
            unsigned int uvIndex;
            float blend;
            aiTextureOp operation;
            aiTextureMapMode mapMode[3];
            
            if (mat->GetTexture(type, i, &texturePath, &mapping, &uvIndex, &blend, &operation, mapMode) != AI_SUCCESS) {
                std::cerr << "Failed to get texture " << i << " of type " << typeName << std::endl;
                continue;
            }

            // Check if texture was loaded before (with path and type)
            bool skip = false;
            for (const auto& loadedTex : loadedTextures) {
                if (std::strcmp(loadedTex.path.c_str(), texturePath.C_Str()) == 0 &&
                    loadedTex.type == typeName) {
                    textures.push_back(loadedTex);
                    skip = true;
                    std::cout << "Reusing cached texture: " << texturePath.C_Str() << std::endl;
                    break;
                }
            }

            if (!skip) {
                Texture texture;
                texture.path = texturePath.C_Str();
                texture.type = typeName;
                
                // Check if this is an embedded texture (starts with '*')
                std::string pathStr = texturePath.C_Str();
                if (pathStr[0] == '*') {
                    // Handle embedded texture
                    texture.id = loadEmbeddedTexture(pathStr, texture.fullPath);
                } else {
                    // Handle file texture
                    texture.id = TextureFromFile(texturePath.C_Str(), directory, texture.fullPath);
                }
                
                if (texture.id != 0) {
                    textures.push_back(texture);
                    loadedTextures.push_back(texture);
                    std::cout << "Successfully loaded texture: " << texture.path << " -> " << texture.fullPath << std::endl;
                } else {
                    std::cerr << "Failed to load texture: " << texture.path << std::endl;
                }
            }
        }
        return textures;
    }

    GLuint Model::TextureFromFile(const char* path, const std::string& directory, std::string& outFullPath) {
        // Get the base directory without the model file name
        std::string baseDir = directory;
        size_t lastSlash = baseDir.find_last_of("/\\");
        if (lastSlash != std::string::npos) {
            baseDir = baseDir.substr(0, lastSlash);
        }

        // Get just the filename from the path and try different extensions
        std::string textureName = std::string(path);
        size_t lastSlashTex = textureName.find_last_of("/\\");
        if (lastSlashTex != std::string::npos) {
            textureName = textureName.substr(lastSlashTex + 1);
        }

        // Remove extension if present
        size_t lastDot = textureName.find_last_of('.');
        std::string baseName = (lastDot != std::string::npos) ?
            textureName.substr(0, lastDot) : textureName;

        // Try different extensions in order of preference
        std::vector<std::string> extensions = {
            ".png", ".jpg", ".jpeg", ".tga", ".bmp", ".dds", ".tiff", ".psd", ".gif", ".hdr", ".pic"
        };

        std::vector<std::string> pathsToTry;

        // For each extension
        for (const auto& ext : extensions) {
            std::string textureNameWithExt = baseName + ext;

            // Build comprehensive list of paths to try (common texture directory patterns)
            pathsToTry.push_back(directory + "/" + textureNameWithExt);                    // Same directory as model
            pathsToTry.push_back(directory + "/textures/" + textureNameWithExt);           // textures subdirectory
            pathsToTry.push_back(directory + "/texture/" + textureNameWithExt);            // texture subdirectory 
            pathsToTry.push_back(directory + "/materials/" + textureNameWithExt);          // materials subdirectory
            pathsToTry.push_back(directory + "/images/" + textureNameWithExt);             // images subdirectory
            pathsToTry.push_back(directory + "/maps/" + textureNameWithExt);               // maps subdirectory
            pathsToTry.push_back(baseDir + "/" + textureNameWithExt);                     // Parent directory
            pathsToTry.push_back(baseDir + "/textures/" + textureNameWithExt);            // Parent/textures
            pathsToTry.push_back("textures/" + textureNameWithExt);                       // Relative textures
            pathsToTry.push_back("./textures/" + textureNameWithExt);                     // Current/textures
            pathsToTry.push_back("../textures/" + textureNameWithExt);                    // Parent/textures
            pathsToTry.push_back("./" + textureNameWithExt);                              // Current directory
        }

        // Try to load the texture from each path
        unsigned char* data = nullptr;
        int width, height, nrComponents;
        std::string successPath;

        // Enable verbose debugging for stb_image
        stbi_set_flip_vertically_on_load(true);

        for (const auto& tryPath : pathsToTry) {
            // Convert to canonical path to handle ".." and "." in paths
            std::filesystem::path canonicalPath;
            try {
                if (std::filesystem::exists(tryPath)) {
                    canonicalPath = std::filesystem::canonical(tryPath);
                    std::cout << "Attempting to load texture: " << canonicalPath.string() << std::endl;

                    // Read file into memory first
                    std::ifstream file(canonicalPath, std::ios::binary | std::ios::ate);
                    if (!file.is_open()) {
                        continue;
                    }

                    std::streamsize size = file.tellg();
                    file.seekg(0, std::ios::beg);

                    std::vector<char> buffer(size);
                    if (!file.read(buffer.data(), size)) {
                        continue;
                    }

                    // Try to load from memory
                    data = stbi_load_from_memory(
                        reinterpret_cast<unsigned char*>(buffer.data()),
                        static_cast<int>(size),
                        &width, &height, &nrComponents, 0
                    );

                    if (data) {
                        successPath = canonicalPath.string();
                        outFullPath = successPath;
                        break;
                    }
                    else {
                        std::cout << "STB failed to load " << canonicalPath.string()
                            << ": " << stbi_failure_reason() << std::endl;
                    }
                }
            }
            catch (const std::filesystem::filesystem_error& e) {
                std::cout << "Filesystem error while trying path " << tryPath
                    << ": " << e.what() << std::endl;
                continue;
            }
        }

        GLuint textureID;
        glGenTextures(1, &textureID);

        if (data) {
            GLenum format;
            GLenum internalFormat;
            if (nrComponents == 1) {
                format = GL_RED;
                internalFormat = GL_R8;
            }
            else if (nrComponents == 2) {
                format = GL_RG;
                internalFormat = GL_RG8;
            }
            else if (nrComponents == 3) {
                format = GL_RGB;
                internalFormat = GL_RGB8;
            }
            else if (nrComponents == 4) {
                format = GL_RGBA;
                internalFormat = GL_RGBA8;
            }
            else {
                std::cout << "Unexpected number of components: " << nrComponents << std::endl;
                format = GL_RGB;
                internalFormat = GL_RGB8;
            }

            glBindTexture(GL_TEXTURE_2D, textureID);
            // Use internalFormat instead of format for the internal format parameter
            glTexImage2D(GL_TEXTURE_2D, 0, internalFormat, width, height, 0, format, GL_UNSIGNED_BYTE, data);
            glGenerateMipmap(GL_TEXTURE_2D);

            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

            stbi_image_free(data);
            std::cout << "Successfully loaded texture: " << successPath
                << " (" << width << "x" << height << ", " << nrComponents << " components)" << std::endl;
        }
        else {
            std::cout << "Failed to load texture from all attempted paths for: " << textureName << std::endl;
            std::cout << "Last STB Image error: " << stbi_failure_reason() << std::endl;

            // Create a default colored texture
            unsigned char defaultColor[] = { 255, 0, 255, 255 };  // Magenta
            glBindTexture(GL_TEXTURE_2D, textureID);
            glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, defaultColor);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
            glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        }

        return textureID;
    }

    GLuint Model::loadEmbeddedTexture(const std::string& embeddedPath, std::string& outFullPath) {
        // Extract the index from the embedded path (e.g., "*0", "*1", etc.)
        int textureIndex = std::stoi(embeddedPath.substr(1));
        
        std::cout << "Loading embedded texture with index: " << textureIndex << std::endl;
        
        // For now, return a placeholder texture since we don't have the scene context here
        // In a complete implementation, you'd need to pass the aiScene* to access embedded textures
        outFullPath = "embedded_texture_" + std::to_string(textureIndex);
        
        // Create a placeholder colored texture to indicate missing embedded texture support
        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);
        
        unsigned char defaultColor[] = { 255, 255, 0, 255 };  // Yellow for embedded textures
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, defaultColor);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        
        std::cout << "Warning: Embedded texture support not fully implemented. Using placeholder." << std::endl;
        
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

        for (unsigned int i = 0; i < meshes.size(); i++) {
            if (meshes[i].visible) {  // Only draw if mesh is visible
                shader.setInt("currentMeshIndex", i);
                meshes[i].Draw(shader);
            }
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


        cubeModel.diffuseReflectivity = 0.8f;
        cubeModel.specularColor = glm::vec3(1.0f);
        cubeModel.specularDiffusion = 0.5f;
        cubeModel.specularReflectivity = 0.0f;
        cubeModel.refractiveIndex = 1.0f;
        cubeModel.transparency = 0.0f;

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