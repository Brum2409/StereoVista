#ifndef OBJ_LOADER_H
#define OBJ_LOADER_H

#include <vector>
#include <string>
#include <map>
#include <glm/glm.hpp>
#include "core.h" 

namespace Engine {

    struct ObjVertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoords;
    };

    struct ObjModel {
        std::string name;
        std::string path;
        std::vector<ObjVertex> vertices;
        std::vector<GLuint> indices;
        GLuint vao;
        GLuint texture;
        GLuint normalMap;
        GLuint specularMap;
        GLuint aoMap;
        glm::vec3 position;
        glm::vec3 scale;
        glm::vec3 rotation;
        bool selected;
        glm::vec3 color;
        float shininess;
        float emissive;
        bool hasCustomTexture;

        std::string diffuseTexturePath;
        std::string normalTexturePath;
        std::string specularTexturePath;
        std::string aoTexturePath;

        ObjModel() : selected(false), color(1.0f, 1.0f, 1.0f), shininess(32.0f), emissive(0.0f),
            texture(0), normalMap(0), specularMap(0), aoMap(0) {}
    };

    // Function declarations
    ObjModel loadObjFile(const std::string& filePath);
    GLuint createDefaultWhiteTexture();
    ObjModel createCube(const glm::vec3& color, float shininess, float emissive);
    GLuint loadTextureFromFile(const char* path);

}  // namespace Engine

#endif // OBJ_LOADER_H