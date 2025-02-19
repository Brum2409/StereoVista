#include "voxalizer.h"

namespace Engine {

    Voxelizer::Voxelizer(int resolution)
        : m_resolution(resolution)
        , m_voxelGridSize(10.0f)
        , m_voxelTexture(0)
        , m_debugCubeVAO(0)
        , m_debugCubeVBO(0)
        , m_voxelShader(nullptr)
        , m_debugShader(nullptr) {
        initializeVoxelTexture();
        initializeDebugCube();
        loadShaders();
    }

    Voxelizer::~Voxelizer() {
        glDeleteTextures(1, &m_voxelTexture);
        glDeleteVertexArrays(1, &m_debugCubeVAO);
        glDeleteBuffers(1, &m_debugCubeVBO);
        delete m_voxelShader;
        delete m_debugShader;
    }

    void Voxelizer::initializeVoxelTexture() {
        glGenTextures(1, &m_voxelTexture);
        glBindTexture(GL_TEXTURE_3D, m_voxelTexture);

        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8,
            m_resolution, m_resolution, m_resolution,
            0, GL_RGBA, GL_FLOAT, nullptr);
    }

    void Voxelizer::initializeDebugCube() {
        float vertices[] = {
            // Front face
            -0.5f, -0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
            -0.5f,  0.5f,  0.5f,
            -0.5f, -0.5f,  0.5f,

            // Back face
            -0.5f, -0.5f, -0.5f,
            -0.5f,  0.5f, -0.5f,
             0.5f,  0.5f, -0.5f,
             0.5f,  0.5f, -0.5f,
             0.5f, -0.5f, -0.5f,
            -0.5f, -0.5f, -0.5f,

            // Top face
            -0.5f,  0.5f, -0.5f,
            -0.5f,  0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
             0.5f,  0.5f, -0.5f,
            -0.5f,  0.5f, -0.5f,

            // Bottom face
            -0.5f, -0.5f, -0.5f,
             0.5f, -0.5f, -0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
            -0.5f, -0.5f,  0.5f,
            -0.5f, -0.5f, -0.5f,

            // Right face
             0.5f, -0.5f, -0.5f,
             0.5f,  0.5f, -0.5f,
             0.5f,  0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f, -0.5f, -0.5f,

             // Left face
             -0.5f, -0.5f, -0.5f,
             -0.5f, -0.5f,  0.5f,
             -0.5f,  0.5f,  0.5f,
             -0.5f,  0.5f,  0.5f,
             -0.5f,  0.5f, -0.5f,
             -0.5f, -0.5f, -0.5f
        };

        glGenVertexArrays(1, &m_debugCubeVAO);
        glGenBuffers(1, &m_debugCubeVBO);

        glBindVertexArray(m_debugCubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_debugCubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);

        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);
        glEnableVertexAttribArray(0);

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
    }

    void Voxelizer::loadShaders() {
        try {
            m_voxelShader = Engine::loadShader(
                "voxelVertexShader.glsl",
                "voxelFragmentShader.glsl",
                "voxelGeometryShader.glsl"
            );

            m_debugShader = Engine::loadShader(
                "voxelDebugVertex.glsl",
                "voxelDebugFragment.glsl"
            );
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to load voxelization shaders: " << e.what() << std::endl;
            throw;
        }
    }

    void Voxelizer::update(const glm::vec3& cameraPos, const std::vector<Model>& models) {
        
        /*if (!m_voxelShader || !m_debugShader) {
            std::cerr << "Shaders not initialized properly!" << std::endl;
            return;
        }

        // Add debug output
        std::cout << "Updating voxels: Grid size = " << m_voxelGridSize
            << ", Resolution = " << m_resolution
            << ", Models count = " << models.size() << std::endl;
        // Clear voxel texture
        */

        glClearTexImage(m_voxelTexture, 0, GL_RGBA, GL_FLOAT, nullptr);

        // Bind voxel texture for writing
        glBindImageTexture(0, m_voxelTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);

        m_voxelShader->use();
        m_voxelShader->setVec3("cameraPos", cameraPos);
        m_voxelShader->setFloat("voxelGridSize", m_voxelGridSize);

        // Voxelize each model
        for (const auto& model : models) {
            if (!model.visible) continue;

            glm::mat4 modelMatrix = glm::translate(glm::mat4(1.0f), model.position);
            modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x), glm::vec3(1, 0, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y), glm::vec3(0, 1, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z), glm::vec3(0, 0, 1));
            modelMatrix = glm::scale(modelMatrix, model.scale);

            m_voxelShader->setMat4("model", modelMatrix);
            m_voxelShader->setVec3("objectColor", model.color);

            for (const auto& mesh : model.getMeshes()) {
                if (!mesh.visible) continue;

                // Bind textures if available
                bool hasTexture = !mesh.textures.empty();
                m_voxelShader->setBool("hasTexture", hasTexture);
                if (hasTexture) {
                    glActiveTexture(GL_TEXTURE0);
                    glBindTexture(GL_TEXTURE_2D, mesh.textures[0].id);
                    m_voxelShader->setInt("diffuseTexture", 0);
                }

                // Draw mesh
                glBindVertexArray(mesh.VAO);
                glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
            }
        }
    }

    void Voxelizer::renderDebugVisualization(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view) {
        if (!showDebugVisualization) return;

        m_debugShader->use();
        m_debugShader->setMat4("view", view);
        m_debugShader->setMat4("projection", projection);

        // Bind voxel texture
        glActiveTexture(GL_TEXTURE0);
        glBindTexture(GL_TEXTURE_3D, m_voxelTexture);
        m_debugShader->setInt("voxelTexture", 0);

        // Enable transparency
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Render each non-empty voxel
        float halfGridSize = m_voxelGridSize * 0.5f;
        float voxelSize = m_voxelGridSize / float(m_resolution);

        glm::vec3 startPos = cameraPos - glm::vec3(halfGridSize);

        for (int x = 0; x < m_resolution; x++) {
            for (int y = 0; y < m_resolution; y++) {
                for (int z = 0; z < m_resolution; z++) {
                    // Sample voxel texture
                    glm::vec3 voxelPos = glm::vec3(
                        float(x) / float(m_resolution),
                        float(y) / float(m_resolution),
                        float(z) / float(m_resolution)
                    );

                    // Set voxel position uniform for fragment shader
                    m_debugShader->setVec3("voxelPos", voxelPos);

                    // Calculate world position
                    glm::vec3 worldPos = startPos + glm::vec3(
                        float(x) * voxelSize,
                        float(y) * voxelSize,
                        float(z) * voxelSize
                    );

                    // Create model matrix for this voxel
                    glm::mat4 model = glm::translate(glm::mat4(1.0f), worldPos);
                    model = glm::scale(model, glm::vec3(debugVoxelSize));
                    m_debugShader->setMat4("model", model);

                    // Render voxel cube
                    glBindVertexArray(m_debugCubeVAO);
                    glDrawArrays(GL_TRIANGLES, 0, 36);
                }
            }
        }

        glDisable(GL_BLEND);
    }

} // namespace Engine