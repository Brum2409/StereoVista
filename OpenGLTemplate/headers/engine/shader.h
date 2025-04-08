// shader.h
#pragma once
#include "core.h"
#include <glm/gtc/type_ptr.hpp>

namespace Engine {
    class Shader {
    private:
        unsigned int shaderID;

    public:
        // Modified constructor to optionally include geometry shader
        Shader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath = "");
        ~Shader() {
            if (shaderID != 0) {
                glDeleteProgram(shaderID);
            }
        }

        void use();
        void setBool(const std::string& name, bool value);
        void setInt(const std::string& name, int value);
        void setFloat(const std::string& name, float value);
        void setMat3(const std::string& name, const glm::mat3& mat);
        void setMat4(const std::string& name, const glm::mat4& mat);
        void setVec2(const std::string& name, glm::vec2 vec);
        void setVec3(const std::string& name, glm::vec3 vec);
        void setVec4(const std::string& name, glm::vec4 vec);
        GLuint getID() const { return shaderID; }

        // Helper method to check if shader is valid
        bool isValid() const { return shaderID != 0 && glIsProgram(shaderID); }

        Shader(GLuint programID) {
            shaderID = programID;
        }
    };

    // Modified shader loader function
    Shader* loadShader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath = "");
}