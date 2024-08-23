#include "obj_loader.h"
#include "tiny_obj_loader.h"
#include <iostream>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

namespace Engine {

    std::string getDirectoryPath(const std::string& filePath) {
        size_t found = filePath.find_last_of("/\\");
        return (found != std::string::npos) ? filePath.substr(0, found + 1) : "";
    }

    GLuint createDefaultWhiteTexture() {
        GLuint textureID;
        glGenTextures(1, &textureID);
        glBindTexture(GL_TEXTURE_2D, textureID);

        // Create a 1x1 white texture
        unsigned char texData[] = { 255, 255, 255, 255 };

        // Load the texture data
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, 1, 1, 0, GL_RGBA, GL_UNSIGNED_BYTE, texData);


        // Set texture parameters
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

        return textureID;
    }


    ObjModel loadObjFile(const std::string& filePath) {
        
        std::ifstream file(filePath);
        if (!file.good()) {
            std::cerr << "Error: File " << filePath << " does not exist." << std::endl;
            return ObjModel(); // Return an empty ObjModel
        }
        file.close();

        ObjModel model;
        model.path = filePath;
        model.name = filePath.substr(filePath.find_last_of("/\\") + 1);
        model.position = glm::vec3(0.0f);
        model.scale = glm::vec3(1.0f);
        model.rotation = glm::vec3(0.0f);

        tinyobj::ObjReaderConfig reader_config;
        reader_config.mtl_search_path = getDirectoryPath(filePath);
        std::cout << "MTL search path: " << reader_config.mtl_search_path << std::endl;

        tinyobj::ObjReader reader;

        if (!reader.ParseFromFile(filePath, reader_config)) {
            if (!reader.Error().empty()) {
                std::cerr << "TinyObjReader: " << reader.Error();
            }
            exit(1);
        }

        if (!reader.Warning().empty()) {
            std::cout << "TinyObjReader: " << reader.Warning();
        }

        auto& attrib = reader.GetAttrib();
        auto& shapes = reader.GetShapes();
        auto& materials = reader.GetMaterials();

        // Loop over shapes
        for (size_t s = 0; s < shapes.size(); s++) {
            size_t index_offset = 0;
            for (size_t f = 0; f < shapes[s].mesh.num_face_vertices.size(); f++) {
                size_t fv = size_t(shapes[s].mesh.num_face_vertices[f]);

                // Loop over vertices in the face.
                for (size_t v = 0; v < fv; v++) {
                    // access to vertex
                    tinyobj::index_t idx = shapes[s].mesh.indices[index_offset + v];

                    ObjVertex vertex;

                    vertex.position = {
                        attrib.vertices[3 * size_t(idx.vertex_index) + 0],
                        attrib.vertices[3 * size_t(idx.vertex_index) + 1],
                        attrib.vertices[3 * size_t(idx.vertex_index) + 2]
                    };

                    if (idx.normal_index >= 0) {
                        vertex.normal = {
                            attrib.normals[3 * size_t(idx.normal_index) + 0],
                            attrib.normals[3 * size_t(idx.normal_index) + 1],
                            attrib.normals[3 * size_t(idx.normal_index) + 2]
                        };
                    }

                    if (idx.texcoord_index >= 0) {
                        vertex.texCoords = {
                            attrib.texcoords[2 * size_t(idx.texcoord_index) + 0],
                            attrib.texcoords[2 * size_t(idx.texcoord_index) + 1]
                        };
                    }

                    model.vertices.push_back(vertex);
                    model.indices.push_back(model.indices.size());
                }
                index_offset += fv;
            }
        }

        // Load Textures
        bool textureLoaded = false;
        for (size_t i = 0; i < materials.size(); i++) {
            const tinyobj::material_t* mp = &materials[i];

            if (mp->diffuse_texname.length() > 0) {
                std::string texturePath = reader_config.mtl_search_path + mp->diffuse_texname;
                std::cout << "Attempting to load texture: " << texturePath << std::endl;

                GLuint textureID;
                glGenTextures(1, &textureID);
                glBindTexture(GL_TEXTURE_2D, textureID);

                int width, height, nrChannels;
                unsigned char* data = stbi_load(texturePath.c_str(), &width, &height, &nrChannels, 0);
                if (data)
                {
                    GLenum format;
                    if (nrChannels == 1)
                        format = GL_RED;
                    else if (nrChannels == 3)
                        format = GL_RGB;
                    else if (nrChannels == 4)
                        format = GL_RGBA;

                    glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0, format, GL_UNSIGNED_BYTE, data);
                    glGenerateMipmap(GL_TEXTURE_2D);

                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
                    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);

                    stbi_image_free(data);
                    std::cout << "Texture loaded successfully: " << texturePath << std::endl;
                    model.texture = textureID;
                    model.hasCustomTexture = true;
                    textureLoaded = true;
                }
                else
                {
                    std::cout << "Texture failed to load at path: " << texturePath << std::endl;
                    stbi_image_free(data);
                }
                break; // We only load the first texture for now
            }
        }

        // If no texture was loaded, use the default white texture
        if (!textureLoaded) {
            model.texture = createDefaultWhiteTexture();
            model.hasCustomTexture = false;
            std::cout << "Using default white texture for model: " << model.name << std::endl;
        }

        // Create VAO, VBO, EBO for the model
        glGenVertexArrays(1, &model.vao);
        glBindVertexArray(model.vao);

        GLuint vbo, ebo;
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, model.vertices.size() * sizeof(ObjVertex), model.vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, model.indices.size() * sizeof(GLuint), model.indices.data(), GL_STATIC_DRAW);

        // Vertex attributes
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, texCoords));
        glEnableVertexAttribArray(3);
        glVertexAttribPointer(3, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, tangent));
        glEnableVertexAttribArray(4);
        glVertexAttribPointer(4, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, bitangent));

        glBindVertexArray(0);

        return model;
    }

    ObjModel createCube(const glm::vec3& color, float shininess, float emissive) {
        ObjModel cube;
        cube.name = "Cube";
        cube.path = "cube";
        cube.color = color;
        cube.shininess = shininess;
        cube.emissive = emissive;
        cube.hasCustomTexture = false;

        static const GLfloat vertices[] = {
            -1.0f,-1.0f,-1.0f,  -1.0f,-1.0f, 1.0f,  -1.0f, 1.0f, 1.0f,
             1.0f, 1.0f,-1.0f,  -1.0f,-1.0f,-1.0f,  -1.0f, 1.0f,-1.0f,
             1.0f,-1.0f, 1.0f,  -1.0f,-1.0f,-1.0f,   1.0f,-1.0f,-1.0f,
             1.0f, 1.0f,-1.0f,   1.0f,-1.0f,-1.0f,  -1.0f,-1.0f,-1.0f,
            -1.0f,-1.0f,-1.0f,  -1.0f, 1.0f, 1.0f,  -1.0f, 1.0f,-1.0f,
             1.0f,-1.0f, 1.0f,  -1.0f,-1.0f, 1.0f,  -1.0f,-1.0f,-1.0f,
            -1.0f, 1.0f, 1.0f,  -1.0f,-1.0f, 1.0f,   1.0f,-1.0f, 1.0f,
             1.0f, 1.0f, 1.0f,   1.0f,-1.0f,-1.0f,   1.0f, 1.0f,-1.0f,
             1.0f,-1.0f,-1.0f,   1.0f, 1.0f, 1.0f,   1.0f,-1.0f, 1.0f,
             1.0f, 1.0f, 1.0f,   1.0f, 1.0f,-1.0f,  -1.0f, 1.0f,-1.0f,
             1.0f, 1.0f, 1.0f,  -1.0f, 1.0f,-1.0f,  -1.0f, 1.0f, 1.0f,
             1.0f, 1.0f, 1.0f,  -1.0f, 1.0f, 1.0f,   1.0f,-1.0f, 1.0f
        };

        for (int i = 0; i < 36; i++) {
            ObjVertex vertex;
            vertex.position = glm::vec3(vertices[i * 3], vertices[i * 3 + 1], vertices[i * 3 + 2]);
            vertex.normal = glm::normalize(vertex.position);  // Simple normal calculation
            vertex.texCoords = glm::vec2(0.0f, 0.0f);  // We're not using textures for the cube
            cube.vertices.push_back(vertex);
            cube.indices.push_back(i);
        }

        // Create VAO, VBO, EBO for the cube
        glGenVertexArrays(1, &cube.vao);
        glBindVertexArray(cube.vao);

        GLuint vbo, ebo;
        glGenBuffers(1, &vbo);
        glGenBuffers(1, &ebo);

        glBindBuffer(GL_ARRAY_BUFFER, vbo);
        glBufferData(GL_ARRAY_BUFFER, cube.vertices.size() * sizeof(ObjVertex), cube.vertices.data(), GL_STATIC_DRAW);

        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, ebo);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, cube.indices.size() * sizeof(GLuint), cube.indices.data(), GL_STATIC_DRAW);

        // Vertex attributes
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)0);
        glEnableVertexAttribArray(1);
        glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, normal));
        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(ObjVertex), (void*)offsetof(ObjVertex, texCoords));

        glBindVertexArray(0);

        cube.texture = createDefaultWhiteTexture();
        cube.position = glm::vec3(0.0f);
        cube.scale = glm::vec3(1.0f);
        cube.rotation = glm::vec3(0.0f);

        return cube;
    }

    GLuint loadTextureFromFile(const char* path) {
        GLuint textureID;
        glGenTextures(1, &textureID);

        int width, height, nrComponents;
        unsigned char* data = stbi_load(path, &width, &height, &nrComponents, 0);
        if (data) {
            GLenum format;
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
        }
        else {
            std::cout << "Texture failed to load at path: " << path << std::endl;
            stbi_image_free(data);
        }

        return textureID;
    }

}  // namespace Engine