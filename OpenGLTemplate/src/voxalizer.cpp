#include "voxalizer.h"
#include <iostream>
#include <random>

namespace Engine {

    Voxelizer::Voxelizer(int resolution)
        : m_resolution(resolution)
        , m_voxelGridSize(10.0f)
        , m_voxelTexture(0)
        , m_voxelShader(nullptr)
        , m_visualizationShader(nullptr)
        , m_worldPositionShader(nullptr)
        , m_voxelCubeShader(nullptr)
        , m_quadVAO(0)
        , m_quadVBO(0)
        , m_cubeVAO(0)
        , m_cubeVBO(0)
        , m_frontFBO(0)
        , m_backFBO(0)
        , m_frontTexture(0)
        , m_backTexture(0)
        , m_voxelInstanceVBO(0)
        , voxelOpacity(0.5f)
        , voxelColorIntensity(1.0f) {

        initializeVoxelTexture();
        initializeVisualization();

        // Setup default light
        m_lights.push_back({
            glm::vec3(0.0f, 5.0f, 0.0f),
            glm::vec3(1.0f, 1.0f, 1.0f)
            });

        try {
            // Load voxelization shader
            m_voxelShader = Engine::loadShader(
                "voxelization.vert",
                "voxelization.frag",
                "voxelization.geom"
            );

            // Load visualization shaders
            m_visualizationShader = Engine::loadShader(
                "voxel_visualization.vert",
                "voxel_visualization.frag"
            );

            // Load world position shader for cube face capture
            m_worldPositionShader = Engine::loadShader(
                "world_position.vert",
                "world_position.frag"
            );

            // Load voxel cube shader for individual voxel rendering
            m_voxelCubeShader = Engine::loadShader(
                "voxel_cube.vert",
                "voxel_cube.frag"
            );

            // Add define for step length
            const char* stepLengthDefine = "#define STEP_LENGTH 0.005f\n";
            GLuint visualShaderID = m_visualizationShader->getID();
            glShaderSource(visualShaderID, 1, &stepLengthDefine, NULL);
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to load voxelization shaders: " << e.what() << std::endl;
            throw;
        }

        // Generate instance VBO for voxel instances
        glGenBuffers(1, &m_voxelInstanceVBO);
    }

    Voxelizer::~Voxelizer() {
        glDeleteTextures(1, &m_voxelTexture);
        glDeleteTextures(1, &m_frontTexture);
        glDeleteTextures(1, &m_backTexture);
        glDeleteFramebuffers(1, &m_frontFBO);
        glDeleteFramebuffers(1, &m_backFBO);
        glDeleteVertexArrays(1, &m_quadVAO);
        glDeleteBuffers(1, &m_quadVBO);
        glDeleteVertexArrays(1, &m_cubeVAO);
        glDeleteBuffers(1, &m_cubeVBO);
        glDeleteBuffers(1, &m_voxelInstanceVBO);

        delete m_voxelShader;
        delete m_visualizationShader;
        delete m_worldPositionShader;
        delete m_voxelCubeShader;
    }

    void Voxelizer::initializeVoxelTexture() {
        glGenTextures(1, &m_voxelTexture);
        glBindTexture(GL_TEXTURE_3D, m_voxelTexture);

        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA8,
            m_resolution, m_resolution, m_resolution,
            0, GL_RGBA, GL_FLOAT, nullptr);

        glGenerateMipmap(GL_TEXTURE_3D);
    }

    void Voxelizer::initializeVisualization() {
        // Setup screen quad for visualization
        setupScreenQuad();

        // Setup unit cube for visualization
        setupUnitCube();

        // Create FBOs for front and back faces
        glGenFramebuffers(1, &m_frontFBO);
        glGenFramebuffers(1, &m_backFBO);

        // Generate textures for front and back face capture
        glGenTextures(1, &m_frontTexture);
        glBindTexture(GL_TEXTURE_2D, m_frontTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1024, 1024, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        glGenTextures(1, &m_backTexture);
        glBindTexture(GL_TEXTURE_2D, m_backTexture);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 1024, 1024, 0, GL_RGBA, GL_FLOAT, nullptr);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

        // Setup FBOs
        glBindFramebuffer(GL_FRAMEBUFFER, m_frontFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_frontTexture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "ERROR: Front face FBO is not complete!" << std::endl;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, m_backFBO);
        glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_backTexture, 0);

        if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
            std::cerr << "ERROR: Back face FBO is not complete!" << std::endl;
        }

        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Voxelizer::setupScreenQuad() {
        float quadVertices[] = {
            // positions        
            -1.0f,  1.0f, 0.0f,
            -1.0f, -1.0f, 0.0f,
             1.0f,  1.0f, 0.0f,
             1.0f, -1.0f, 0.0f,
        };

        glGenVertexArrays(1, &m_quadVAO);
        glGenBuffers(1, &m_quadVBO);

        glBindVertexArray(m_quadVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), quadVertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        glBindVertexArray(0);
    }

    void Voxelizer::setupUnitCube() {
        float cubeVertices[] = {
            // positions          
            -1.0f,  1.0f, -1.0f,
            -1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f, -1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,

            -1.0f, -1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f, -1.0f,  1.0f,
            -1.0f, -1.0f,  1.0f,

            -1.0f,  1.0f, -1.0f,
             1.0f,  1.0f, -1.0f,
             1.0f,  1.0f,  1.0f,
             1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f,  1.0f,
            -1.0f,  1.0f, -1.0f,

            -1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f, -1.0f,
             1.0f, -1.0f, -1.0f,
            -1.0f, -1.0f,  1.0f,
             1.0f, -1.0f,  1.0f
        };

        glGenVertexArrays(1, &m_cubeVAO);
        glGenBuffers(1, &m_cubeVBO);

        glBindVertexArray(m_cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        glBindVertexArray(0);
    }

    // This function was moved to the constructor

    void Voxelizer::update(const glm::vec3& cameraPos, const std::vector<Model>& models) {
        // Clear voxel texture
        glClearTexImage(m_voxelTexture, 0, GL_RGBA, GL_FLOAT, nullptr);

        // Bind voxel texture for writing
        glBindImageTexture(0, m_voxelTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA8);

        // Set viewport for voxelization
        glViewport(0, 0, m_resolution, m_resolution);

        // Disable depth test and face culling for voxelization
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);

        m_voxelShader->use();

        // Set light uniforms
        m_voxelShader->setInt("numberOfLights", static_cast<int>(m_lights.size()));
        for (size_t i = 0; i < m_lights.size(); i++) {
            std::string lightName = "pointLights[" + std::to_string(i) + "]";
            m_voxelShader->setVec3(lightName + ".position", m_lights[i].position);
            m_voxelShader->setVec3(lightName + ".color", m_lights[i].color);
        }

        m_voxelShader->setInt("mipmapLevel", m_state);

        // Voxelize each model
        for (const auto& model : models) {
            // Skip invisible models
            if (!model.visible) continue;

            // Create model matrix
            glm::mat4 modelMatrix = glm::mat4(1.0f);
            modelMatrix = glm::translate(modelMatrix, model.position);
            modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x), glm::vec3(1, 0, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y), glm::vec3(0, 1, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z), glm::vec3(0, 0, 1));
            modelMatrix = glm::scale(modelMatrix, model.scale);

            // Scale to voxel grid space (-1 to 1)
            glm::mat4 scaledModel = glm::scale(modelMatrix, glm::vec3(1.0f / (m_voxelGridSize * 0.5f)));

            m_voxelShader->setMat4("M", scaledModel);
            m_voxelShader->setMat4("V", glm::mat4(1.0f)); // Identity for voxelization
            m_voxelShader->setMat4("P", glm::mat4(1.0f)); // Identity for voxelization

            // Set material properties
            m_voxelShader->setVec3("material.diffuseColor", model.color);
            m_voxelShader->setVec3("material.specularColor", glm::vec3(1.0f));
            m_voxelShader->setFloat("material.diffuseReflectivity", 0.8f);
            m_voxelShader->setFloat("material.specularReflectivity", 0.2f);
            m_voxelShader->setFloat("material.emissivity", model.emissive);
            m_voxelShader->setFloat("material.transparency", 0.0f); // Default to opaque

            // Draw each mesh of the model
            for (const auto& mesh : model.getMeshes()) {
                // Skip invisible meshes
                if (!mesh.visible) continue;

                // Draw mesh
                glBindVertexArray(mesh.VAO);
                glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
            }
        }

        // Generate mipmaps for 3D texture
        glBindTexture(GL_TEXTURE_3D, m_voxelTexture);
        glGenerateMipmap(GL_TEXTURE_3D);

        // Mark voxel data as needing update for visualization
        m_voxelDataNeedsUpdate = true;

        // Reset state
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
    }

    void Voxelizer::renderCubeFaces(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view) {
        // Set up cube transforms
        glm::mat4 modelMatrix = glm::scale(glm::mat4(1.0f), glm::vec3(m_voxelGridSize * 0.5f));

        m_worldPositionShader->use();
        m_worldPositionShader->setMat4("M", modelMatrix);
        m_worldPositionShader->setMat4("V", view);
        m_worldPositionShader->setMat4("P", projection);

        // Capture front faces
        glBindFramebuffer(GL_FRAMEBUFFER, m_frontFBO);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glEnable(GL_CULL_FACE);
        glCullFace(GL_BACK);

        glBindVertexArray(m_cubeVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // Capture back faces
        glBindFramebuffer(GL_FRAMEBUFFER, m_backFBO);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        glCullFace(GL_FRONT);

        glBindVertexArray(m_cubeVAO);
        glDrawArrays(GL_TRIANGLES, 0, 36);

        // Reset state
        glCullFace(GL_BACK);
        glBindFramebuffer(GL_FRAMEBUFFER, 0);
    }

    void Voxelizer::renderDebugVisualization(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view) {
        if (!showDebugVisualization) return;

        // If using ray-cast visualization
        if (useRayCastVisualization) {
            // First render cube front and back faces
            renderCubeFaces(cameraPos, projection, view);

            // Then use those textures for ray-casting visualization
            m_visualizationShader->use();

            // Bind front and back textures
            glActiveTexture(GL_TEXTURE0);
            glBindTexture(GL_TEXTURE_2D, m_frontTexture);
            m_visualizationShader->setInt("textureFront", 0);

            glActiveTexture(GL_TEXTURE1);
            glBindTexture(GL_TEXTURE_2D, m_backTexture);
            m_visualizationShader->setInt("textureBack", 1);

            // Bind voxel 3D texture
            glActiveTexture(GL_TEXTURE2);
            glBindTexture(GL_TEXTURE_3D, m_voxelTexture);
            m_visualizationShader->setInt("texture3D", 2);

            // Set other uniforms
            m_visualizationShader->setVec3("cameraPosition", cameraPos / (m_voxelGridSize * 0.5f)); // Scale to voxel grid space
            m_visualizationShader->setMat4("V", view);
            m_visualizationShader->setInt("state", m_state); // Current mipmap level

            // Render fullscreen quad
            glBindVertexArray(m_quadVAO);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

            // Reset state
            glBindVertexArray(0);
            glActiveTexture(GL_TEXTURE0);
        }
        else {
            // Render scene as voxel cubes 
            renderVoxelsAsCubes(cameraPos, projection, view);
        }
    }

    void Voxelizer::updateVisibleVoxels() {
        // Clear previous visible voxels
        m_visibleVoxels.clear();

        // Calculate voxel size based on grid size and resolution
        float baseVoxelSize = m_voxelGridSize / m_resolution;

        // Bind the 3D texture
        glBindTexture(GL_TEXTURE_3D, m_voxelTexture);

        // Get texture data for the current mipmap level
        int effectiveResolution = m_resolution >> m_state; // Resolution at current mipmap level
        if (effectiveResolution < 1) effectiveResolution = 1;

        std::vector<glm::vec4> voxelData(effectiveResolution * effectiveResolution * effectiveResolution);
        glGetTexImage(GL_TEXTURE_3D, m_state, GL_RGBA, GL_FLOAT, voxelData.data());

        // Calculate stride and scaled voxel size
        int stride = 1 << m_state;
        float scaledVoxelSize = baseVoxelSize * stride; // Scale voxel size based on mipmap level
        float halfGrid = m_voxelGridSize * 0.5f;

        // Loop through voxels at the current mipmap level
        for (int x = 0; x < effectiveResolution; x++) {
            for (int y = 0; y < effectiveResolution; y++) {
                for (int z = 0; z < effectiveResolution; z++) {
                    // Calculate index in the downsampled mipmap
                    int index = (z * effectiveResolution * effectiveResolution) +
                        (y * effectiveResolution) + x;

                    // Check if this voxel is visible
                    if (voxelData[index].a > 0.01f) {
                        // Calculate world position using the scaled voxel size
                        // and account for the position within the reduced resolution grid
                        glm::vec3 position(
                            (x * stride) * baseVoxelSize - halfGrid + (scaledVoxelSize * 0.5f),
                            (y * stride) * baseVoxelSize - halfGrid + (scaledVoxelSize * 0.5f),
                            (z * stride) * baseVoxelSize - halfGrid + (scaledVoxelSize * 0.5f)
                        );

                        // Add to visible voxels
                        VoxelData voxel;
                        voxel.position = position;
                        voxel.color = voxelData[index];
                        m_visibleVoxels.push_back(voxel);
                    }
                }
            }
        }

        // If we have too many voxels, randomly subsample to prevent performance issues
        const size_t maxVoxels = 100000;
        if (m_visibleVoxels.size() > maxVoxels) {
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(m_visibleVoxels.begin(), m_visibleVoxels.end(), g);
            m_visibleVoxels.resize(maxVoxels);
        }

        // Update the instance VBO with the new voxel data
        if (!m_visibleVoxels.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, m_voxelInstanceVBO);

            // Create a buffer for position and color data
            std::vector<float> instanceData;
            instanceData.reserve(m_visibleVoxels.size() * 7); // 3 for position, 4 for color

            for (const auto& voxel : m_visibleVoxels) {
                // Position
                instanceData.push_back(voxel.position.x);
                instanceData.push_back(voxel.position.y);
                instanceData.push_back(voxel.position.z);

                // Color
                instanceData.push_back(voxel.color.r);
                instanceData.push_back(voxel.color.g);
                instanceData.push_back(voxel.color.b);
                instanceData.push_back(voxel.color.a);
            }

            // Upload instance data
            glBufferData(GL_ARRAY_BUFFER, instanceData.size() * sizeof(float), instanceData.data(), GL_DYNAMIC_DRAW);

            // Set up the VAO for the cube with instance data
            glBindVertexArray(m_cubeVAO);

            // Position attribute (location 2)
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)0);
            glVertexAttribDivisor(2, 1); // This makes it instanced

            // Color attribute (location 3)
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 7 * sizeof(float), (void*)(3 * sizeof(float)));
            glVertexAttribDivisor(3, 1); // This makes it instanced

            glBindVertexArray(0);
        }

        m_voxelDataNeedsUpdate = false;
    }

    void Voxelizer::renderVoxelsAsCubes(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view) {
        // Update voxel data if needed
        if (m_voxelDataNeedsUpdate || m_visibleVoxels.empty()) {
            updateVisibleVoxels();
        }

        // Skip rendering if no visible voxels
        if (m_visibleVoxels.empty()) {
            return;
        }

        // Setup rendering state
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glEnable(GL_CULL_FACE);
        glEnable(GL_DEPTH_TEST);

        // Use voxel cube shader
        m_voxelCubeShader->use();

        // Set shader uniforms
        m_voxelCubeShader->setMat4("model", glm::mat4(1.0f));
        m_voxelCubeShader->setMat4("view", view);
        m_voxelCubeShader->setMat4("projection", projection);
        m_voxelCubeShader->setVec3("viewPos", cameraPos);
        m_voxelCubeShader->setFloat("opacity", voxelOpacity);
        m_voxelCubeShader->setFloat("colorIntensity", voxelColorIntensity);

        // Calculate voxel size based on grid size and resolution - adjusted for mipmap level
        float baseVoxelSize = m_voxelGridSize / m_resolution;
        int stride = 1 << m_state;
        float scaledVoxelSize = baseVoxelSize * stride;
        m_voxelCubeShader->setFloat("voxelSize", scaledVoxelSize);

        // Make sure the cube VAO is properly set up
        if (m_cubeVAO == 0) {
            setupUnitCube();
        }

        // Render all voxel instances
        glBindVertexArray(m_cubeVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, m_visibleVoxels.size());

        // Reset state
        glBindVertexArray(0);
        glDisable(GL_BLEND);
    }

    void Voxelizer::increaseState() {
        int maxMipLevels = static_cast<int>(std::log2(m_resolution)) + 1;
        m_state = std::min(m_state + 1, maxMipLevels - 1);
        m_voxelDataNeedsUpdate = true;  // Add this line
    }

    void Voxelizer::decreaseState() {
        m_state = std::max(m_state - 1, 0);
        m_voxelDataNeedsUpdate = true;  // Add this line
    }

} // namespace Engine