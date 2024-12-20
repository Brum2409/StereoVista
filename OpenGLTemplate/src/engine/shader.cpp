#include "core.h"

namespace Engine {
    Shader::Shader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath) {
        std::string vertexCode;
        std::string fragmentCode;
        std::string geometryCode;
        std::ifstream vShaderFile;
        std::ifstream fShaderFile;
        std::ifstream gShaderFile;

        vShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        fShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);
        gShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

        try {
            // Read vertex and fragment shaders
            vShaderFile.open(vertexPath);
            fShaderFile.open(fragmentPath);
            std::stringstream vShaderStream, fShaderStream;
            vShaderStream << vShaderFile.rdbuf();
            fShaderStream << fShaderFile.rdbuf();
            vShaderFile.close();
            fShaderFile.close();
            vertexCode = vShaderStream.str();
            fragmentCode = fShaderStream.str();

            // Read geometry shader if provided
            if (!geometryPath.empty()) {
                gShaderFile.open(geometryPath);
                std::stringstream gShaderStream;
                gShaderStream << gShaderFile.rdbuf();
                gShaderFile.close();
                geometryCode = gShaderStream.str();
            }
        }
        catch (std::ifstream::failure& e) {
            std::cout << "ERROR::SHADER::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
            throw std::runtime_error("Shader file reading failed");
        }

        const char* vShaderCode = vertexCode.c_str();
        const char* fShaderCode = fragmentCode.c_str();
        const char* gShaderCode = geometryCode.empty() ? nullptr : geometryCode.c_str();

        // Compile shaders
        GLuint vertex, fragment, geometry = 0;
        int success;
        char infoLog[512];

        // Vertex shader
        vertex = glCreateShader(GL_VERTEX_SHADER);
        glShaderSource(vertex, 1, &vShaderCode, NULL);
        glCompileShader(vertex);
        glGetShaderiv(vertex, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(vertex, 512, NULL, infoLog);
            std::cout << "ERROR::SHADER::VERTEX::COMPILATION_FAILED\n" << infoLog << std::endl;
            throw std::runtime_error("Vertex shader compilation failed");
        }

        // Fragment shader
        fragment = glCreateShader(GL_FRAGMENT_SHADER);
        glShaderSource(fragment, 1, &fShaderCode, NULL);
        glCompileShader(fragment);
        glGetShaderiv(fragment, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(fragment, 512, NULL, infoLog);
            std::cout << "ERROR::SHADER::FRAGMENT::COMPILATION_FAILED\n" << infoLog << std::endl;
            throw std::runtime_error("Fragment shader compilation failed");
        }

        // Geometry shader (if provided)
        if (gShaderCode != nullptr) {
            geometry = glCreateShader(GL_GEOMETRY_SHADER);
            glShaderSource(geometry, 1, &gShaderCode, NULL);
            glCompileShader(geometry);
            glGetShaderiv(geometry, GL_COMPILE_STATUS, &success);
            if (!success) {
                glGetShaderInfoLog(geometry, 512, NULL, infoLog);
                std::cout << "ERROR::SHADER::GEOMETRY::COMPILATION_FAILED\n" << infoLog << std::endl;
                throw std::runtime_error("Geometry shader compilation failed");
            }
        }

        // Shader program
        shaderID = glCreateProgram();
        glAttachShader(shaderID, vertex);
        glAttachShader(shaderID, fragment);
        if (geometry != 0)
            glAttachShader(shaderID, geometry);
        glLinkProgram(shaderID);

        glGetProgramiv(shaderID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shaderID, 512, NULL, infoLog);
            std::cout << "ERROR::SHADER::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
            throw std::runtime_error("Shader program linking failed");
        }

        // Delete shaders after linking
        glDeleteShader(vertex);
        glDeleteShader(fragment);
        if (geometry != 0)
            glDeleteShader(geometry);
    }

    Shader* loadShader(const std::string& vertexPath, const std::string& fragmentPath, const std::string& geometryPath) {
        std::vector<std::string> searchPaths = {
            "./shaders/",
            "./",
            "assets/shaders/"
        };

        for (const auto& basePath : searchPaths) {
            std::string fullVertexPath = basePath + vertexPath;
            std::string fullFragmentPath = basePath + fragmentPath;
            std::string fullGeometryPath = geometryPath.empty() ? "" : basePath + geometryPath;

            // Check if required files exist
            bool filesExist = std::ifstream(fullVertexPath).good() &&
                std::ifstream(fullFragmentPath).good() &&
                (geometryPath.empty() || std::ifstream(fullGeometryPath).good());

            if (filesExist) {
                try {
                    return new Shader(fullVertexPath, fullFragmentPath, fullGeometryPath);
                }
                catch (const std::exception& e) {
                    std::cerr << "Error loading shader from " << fullVertexPath << ", "
                        << fullFragmentPath;
                    if (!geometryPath.empty())
                        std::cerr << ", " << fullGeometryPath;
                    std::cerr << ": " << e.what() << std::endl;
                }
            }
        }

        throw std::runtime_error("Unable to find shader files");
    }

    // Use / Activate the shader
    void Shader::use() {
        glUseProgram(shaderID);
    }

    void Shader::setBool(const std::string& name, bool value) {
        glUniform1i(glGetUniformLocation(shaderID, name.c_str()), (int)value);
    }

    void Shader::setInt(const std::string& name, int value) {
        glUniform1i(glGetUniformLocation(shaderID, name.c_str()), value);
    }

    void Shader::setFloat(const std::string& name, float value) {
        glUniform1f(glGetUniformLocation(shaderID, name.c_str()), value);
    }

    void Shader::setMat3(const std::string& name, const glm::mat3& mat) {
        glUniformMatrix3fv(glGetUniformLocation(shaderID, name.c_str()), 1, GL_FALSE, glm::value_ptr(mat));
    }

    void Shader::setMat4(const std::string& name, const glm::mat4& mat) {
        glUniformMatrix4fv(glGetUniformLocation(shaderID, name.c_str()), 1, GL_FALSE, glm::value_ptr(mat));
    }

    void Shader::setVec2(const std::string& name, glm::vec2 vec) {
        glUniform2fv(glGetUniformLocation(shaderID, name.c_str()), 1, &vec[0]);
    }

    void Shader::setVec3(const std::string& name, glm::vec3 vec) {
        glUniform3fv(glGetUniformLocation(shaderID, name.c_str()), 1, &vec[0]);
    }

    void Shader::setVec4(const std::string& name, glm::vec4 vec) {
        glUniform4fv(glGetUniformLocation(shaderID, name.c_str()), 1, &vec[0]);
    }
}