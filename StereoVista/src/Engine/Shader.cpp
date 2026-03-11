#include "Engine/Core.h"

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

    Shader::Shader(const std::string& computePath, ComputeShaderTag) {
        std::string computeCode;
        std::ifstream cShaderFile;

        cShaderFile.exceptions(std::ifstream::failbit | std::ifstream::badbit);

        try {
            cShaderFile.open(computePath);
            std::stringstream cShaderStream;
            cShaderStream << cShaderFile.rdbuf();
            cShaderFile.close();
            computeCode = cShaderStream.str();
        }
        catch (std::ifstream::failure& e) {
            std::cout << "ERROR::SHADER::COMPUTE::FILE_NOT_SUCCESSFULLY_READ: " << e.what() << std::endl;
            throw std::runtime_error("Compute shader file reading failed");
        }

        // Normalise line endings: strip \r so CRLF files don't double-count
        // lines on drivers that treat \r and \n as separate line separators.
        computeCode.erase(std::remove(computeCode.begin(), computeCode.end(), '\r'),
                          computeCode.end());

        const char* cShaderCode = computeCode.c_str();

        // Compile compute shader
        GLuint compute;
        int success;
        char infoLog[4096];

        compute = glCreateShader(GL_COMPUTE_SHADER);
        if (compute == 0) {
            std::cout << "ERROR::SHADER::COMPUTE::GL_COMPUTE_SHADER not supported by this driver/context" << std::endl;
            throw std::runtime_error("GL_COMPUTE_SHADER not supported");
        }
        glShaderSource(compute, 1, &cShaderCode, NULL);
        glCompileShader(compute);
        glGetShaderiv(compute, GL_COMPILE_STATUS, &success);
        if (!success) {
            glGetShaderInfoLog(compute, sizeof(infoLog), NULL, infoLog);
            std::cout << "ERROR::SHADER::COMPUTE::COMPILATION_FAILED\n" << infoLog << std::endl;
            throw std::runtime_error("Compute shader compilation failed");
        }

        // Shader program
        shaderID = glCreateProgram();
        glAttachShader(shaderID, compute);
        glLinkProgram(shaderID);

        glGetProgramiv(shaderID, GL_LINK_STATUS, &success);
        if (!success) {
            glGetProgramInfoLog(shaderID, sizeof(infoLog), NULL, infoLog);
            std::cout << "ERROR::SHADER::COMPUTE::PROGRAM::LINKING_FAILED\n" << infoLog << std::endl;
            throw std::runtime_error("Compute shader program linking failed");
        }

        // Delete shader after linking
        glDeleteShader(compute);
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

    Shader* loadComputeShader(const std::string& computePath) {
        std::vector<std::string> searchPaths = {
            "./shaders/",
            "./",
            "assets/shaders/"
        };

        for (const auto& basePath : searchPaths) {
            std::string fullComputePath = basePath + computePath;

            // Check if file exists
            if (std::ifstream(fullComputePath).good()) {
                try {
                    return new Shader(fullComputePath, Shader::ComputeShaderTag{});
                }
                catch (const std::exception& e) {
                    std::cerr << "Error loading compute shader from " << fullComputePath
                              << ": " << e.what() << std::endl;
                }
            }
        }

        throw std::runtime_error("Unable to find compute shader file: " + computePath);
    }

    GLint Shader::getUniformLocation(const std::string& name) const {
        auto it = uniformLocationCache.find(name);
        if (it != uniformLocationCache.end())
            return it->second;
        GLint loc = glGetUniformLocation(shaderID, name.c_str());
        uniformLocationCache[name] = loc;
        return loc;
    }

    // Use / Activate the shader
    void Shader::use() {
        glUseProgram(shaderID);
    }

    void Shader::setBool(const std::string& name, bool value) {
        glUniform1i(getUniformLocation(name), (int)value);
    }

    void Shader::setInt(const std::string& name, int value) {
        glUniform1i(getUniformLocation(name), value);
    }

    void Shader::setFloat(const std::string& name, float value) {
        glUniform1f(getUniformLocation(name), value);
    }

    void Shader::setMat3(const std::string& name, const glm::mat3& mat) {
        glUniformMatrix3fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(mat));
    }

    void Shader::setMat4(const std::string& name, const glm::mat4& mat) {
        glUniformMatrix4fv(getUniformLocation(name), 1, GL_FALSE, glm::value_ptr(mat));
    }

    void Shader::setVec2(const std::string& name, glm::vec2 vec) {
        glUniform2fv(getUniformLocation(name), 1, &vec[0]);
    }

    void Shader::setVec3(const std::string& name, glm::vec3 vec) {
        glUniform3fv(getUniformLocation(name), 1, &vec[0]);
    }

    void Shader::setVec4(const std::string& name, glm::vec4 vec) {
        glUniform4fv(getUniformLocation(name), 1, &vec[0]);
    }

    void Shader::setIVec3(const std::string& name, int x, int y, int z) {
        glUniform3i(getUniformLocation(name), x, y, z);
    }
}