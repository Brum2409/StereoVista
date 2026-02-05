#include "Core/Voxalizer.h"
#include <iostream>
#include <random>

namespace Engine {

    Voxelizer::Voxelizer(int resolution)
        : m_resolution(resolution)
        , m_voxelGridSize(10.0f)
        , m_voxelTexture(0)
        , m_voxelShader(nullptr)
        , m_mipmapComputeShader(nullptr)
        , m_voxelCubeShader(nullptr)
        , m_cubeVAO(0)
        , m_cubeVBO(0)
        , m_voxelInstanceVBO(0)
        , voxelOpacity(1.0f)
        , voxelColorIntensity(1.0f) {

        initializeVoxelTexture();
        initializeVisualization();

        // Lights are synced from the scene via setLights() before each update.

        try {
            // Load voxelization shader
            m_voxelShader = Engine::loadShader(
                "voxelization/voxelization.vert",
                "voxelization/voxelization.frag",
                "voxelization/voxelization.geom"
            );

            // Load voxel cube shader for individual voxel rendering
            m_voxelCubeShader = Engine::loadShader(
                "voxelization/voxel_cube.vert",
                "voxelization/voxel_cube.frag"
            );

            // Load compute shader for alpha-weighted mipmap generation
            m_mipmapComputeShader = Engine::loadComputeShader(
                "voxelization/voxel_mipmap.comp"
            );
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
        glDeleteVertexArrays(1, &m_cubeVAO);
        glDeleteBuffers(1, &m_cubeVBO);
        glDeleteBuffers(1, &m_voxelInstanceVBO);

        delete m_voxelShader;
        delete m_voxelCubeShader;
        delete m_mipmapComputeShader;
    }

    void Voxelizer::initializeVoxelTexture() {
        glGenTextures(1, &m_voxelTexture);
        glBindTexture(GL_TEXTURE_3D, m_voxelTexture);

        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MIN_FILTER, GL_LINEAR_MIPMAP_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_3D, GL_TEXTURE_WRAP_R, GL_CLAMP_TO_EDGE);

        glTexImage3D(GL_TEXTURE_3D, 0, GL_RGBA16F,
            m_resolution, m_resolution, m_resolution,
            0, GL_RGBA, GL_FLOAT, nullptr);

        glGenerateMipmap(GL_TEXTURE_3D);
    }

    void Voxelizer::initializeVisualization() {
        // Setup unit cube for visualization
        setupUnitCube();
    }


    void Voxelizer::setupUnitCube() {
        // Unit cube vertices with correct counter-clockwise winding order
        float cubeVertices[] = {
            // Back face (facing -Z, CCW when viewed from outside)
            -0.5f, -0.5f, -0.5f,
            -0.5f,  0.5f, -0.5f,
             0.5f,  0.5f, -0.5f,
             0.5f,  0.5f, -0.5f,
             0.5f, -0.5f, -0.5f,
            -0.5f, -0.5f, -0.5f,
            
            // Front face (facing +Z, CCW when viewed from outside)
            -0.5f, -0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
            -0.5f,  0.5f,  0.5f,
            -0.5f, -0.5f,  0.5f,
            
            // Left face (facing -X, CCW when viewed from outside)
            -0.5f,  0.5f,  0.5f,
            -0.5f,  0.5f, -0.5f,
            -0.5f, -0.5f, -0.5f,
            -0.5f, -0.5f, -0.5f,
            -0.5f, -0.5f,  0.5f,
            -0.5f,  0.5f,  0.5f,
            
            // Right face (facing +X, CCW when viewed from outside)
             0.5f,  0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f, -0.5f, -0.5f,
             0.5f, -0.5f, -0.5f,
             0.5f,  0.5f, -0.5f,
             0.5f,  0.5f,  0.5f,
            
            // Bottom face (facing -Y, CCW when viewed from outside)
            -0.5f, -0.5f, -0.5f,
             0.5f, -0.5f, -0.5f,
             0.5f, -0.5f,  0.5f,
             0.5f, -0.5f,  0.5f,
            -0.5f, -0.5f,  0.5f,
            -0.5f, -0.5f, -0.5f,
            
            // Top face (facing +Y, CCW when viewed from outside)
            -0.5f,  0.5f, -0.5f,
            -0.5f,  0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
             0.5f,  0.5f,  0.5f,
             0.5f,  0.5f, -0.5f,
            -0.5f,  0.5f, -0.5f
        };

        glGenVertexArrays(1, &m_cubeVAO);
        glGenBuffers(1, &m_cubeVBO);

        glBindVertexArray(m_cubeVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
        glBufferData(GL_ARRAY_BUFFER, sizeof(cubeVertices), cubeVertices, GL_STATIC_DRAW);

        // Position attribute
        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

        glBindVertexArray(0);
    }

    // This function was moved to the constructor

    void Voxelizer::update(const glm::vec3& cameraPos, const std::vector<Model>& models) {
        // Skip re-voxelization if nothing changed (caching)
        if (!m_needsRevoxelization) {
            return;
        }

        // Clear all mipmap levels of the voxel texture to avoid stale data
        int numLevels = static_cast<int>(std::floor(std::log2(m_resolution))) + 1;
        for (int level = 0; level < numLevels; level++) {
            glClearTexImage(m_voxelTexture, level, GL_RGBA, GL_FLOAT, nullptr);
        }

        // Bind voxel texture for writing
        glBindImageTexture(0, m_voxelTexture, 0, GL_TRUE, 0, GL_READ_WRITE, GL_RGBA16F);

        // Use standard resolution viewport
        int voxelizationRes = m_resolution;
        glViewport(0, 0, voxelizationRes, voxelizationRes);

        // Disable depth test and enable MSAA-based conservative approximation
        glDisable(GL_DEPTH_TEST);
        glDisable(GL_CULL_FACE);
        glDisable(GL_BLEND);
        
        // Enable MSAA for conservative rasterization approximation
        glEnable(GL_MULTISAMPLE);
        glEnable(GL_SAMPLE_ALPHA_TO_COVERAGE);
        
        // Force sample shading to ensure all samples are processed

            glEnable(GL_SAMPLE_SHADING);
            glMinSampleShading(1.0f); // Force all samples to be shaded


        m_voxelShader->use();

        // Set light uniforms
        m_voxelShader->setInt("numberOfLights", static_cast<int>(m_lights.size()));
        for (size_t i = 0; i < m_lights.size(); i++) {
            std::string lightName = "pointLights[" + std::to_string(i) + "]";
            m_voxelShader->setVec3(lightName + ".position", m_lights[i].position);
            m_voxelShader->setVec3(lightName + ".color", m_lights[i].color);
        }

        // CRITICAL: Pass the actual grid size and center to the shader
        m_voxelShader->setFloat("gridSize", m_voxelGridSize);
        m_voxelShader->setVec3("gridCenter", m_gridCenter);

        // Pass resolution for triangle dilation (conservative rasterization)
        m_voxelShader->setInt("voxelResolution", m_resolution);

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

            // Set transformation matrices
            m_voxelShader->setMat4("M", modelMatrix);
            m_voxelShader->setMat4("V", glm::mat4(1.0f)); // Identity for voxelization
            m_voxelShader->setMat4("P", glm::mat4(1.0f)); // Identity for voxelization

            // Set material properties
            m_voxelShader->setVec3("material.diffuseColor", model.color);
            m_voxelShader->setVec3("material.specularColor", model.specularColor);
            m_voxelShader->setFloat("material.diffuseReflectivity", model.diffuseReflectivity);
            m_voxelShader->setFloat("material.specularReflectivity", model.specularReflectivity);
            m_voxelShader->setFloat("material.specularDiffusion", model.specularDiffusion);
            m_voxelShader->setFloat("material.emissivity", model.emissive);
            m_voxelShader->setFloat("material.refractiveIndex", model.refractiveIndex);
            m_voxelShader->setFloat("material.transparency", model.transparency);

            // Draw each mesh of the model
            for (const auto& mesh : model.getMeshes()) {
                // Skip invisible meshes
                if (!mesh.visible) continue;

                // Set texture information
                bool hasTexture = !mesh.textures.empty();
                m_voxelShader->setBool("material.hasTexture", hasTexture);

                // Bind textures
                if (hasTexture) {
                    unsigned int diffuseNr = 0;

                    for (unsigned int i = 0; i < mesh.textures.size(); i++) {
                        // Get texture number (diffuse_0, diffuse_1, etc.)
                        std::string number;
                        std::string name = mesh.textures[i].type;

                        if (name == "texture_diffuse")
                            number = std::to_string(diffuseNr++);

                        // Use only the first diffuse texture for voxelization
                        if (i == 0) {
                            glActiveTexture(GL_TEXTURE0);
                            glBindTexture(GL_TEXTURE_2D, mesh.textures[i].id);
                            m_voxelShader->setInt("material.textures[0]", 0);
                        }
                    }
                }

                // Draw mesh
                glBindVertexArray(mesh.VAO);
                glDrawElements(GL_TRIANGLES, mesh.indices.size(), GL_UNSIGNED_INT, 0);
            }
        }

        // Ensure all imageStore writes from the voxelization pass are visible
        // before the compute shader reads them for mipmap generation.
        glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

        // Generate mipmaps using compute shader for alpha-weighted averaging
        generateMipmaps();

        // Ensure mipmaps are visible for subsequent texture sampling (cone tracing).
        glMemoryBarrier(GL_TEXTURE_FETCH_BARRIER_BIT);

        // Mark voxel data as needing update for visualization
        m_voxelDataNeedsUpdate = true;

        // Clear dirty flag - voxelization is now up to date
        m_needsRevoxelization = false;

        // Reset state
        glEnable(GL_DEPTH_TEST);
        glEnable(GL_CULL_FACE);
        
        // Disable MSAA
        glDisable(GL_MULTISAMPLE);
        glDisable(GL_SAMPLE_ALPHA_TO_COVERAGE);

            glDisable(GL_SAMPLE_SHADING);
        
    }


    void Voxelizer::renderDebugVisualization(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view) {
        if (!showDebugVisualization) return;
        
        // Always render as voxel cubes (ray-cast visualization removed)
        renderVoxelsAsCubes(cameraPos, projection, view);
    }

    void Voxelizer::updateVisibleVoxels(const glm::vec3& cameraPos) {
        // Clear previous visible voxels
        m_visibleVoxels.clear();

        // Bind the 3D texture
        glBindTexture(GL_TEXTURE_3D, m_voxelTexture);

        // Implement proper distance-based multi-level sampling with constant density
        // Calculate effective resolution based on grid size to maintain constant voxel density
        float baseGridSize = 10.0f; // Reference grid size
        float densityScale = m_voxelGridSize / baseGridSize; // How much more/less dense we need
        
        int maxMipLevels = static_cast<int>(std::log2(m_resolution)) + 1;
        
        for (int level = 0; level < maxMipLevels; level++) {
            int levelResolution = m_resolution >> level;
            if (levelResolution < 1) break;
            
            // Get texture data for this mipmap level
            std::vector<glm::vec4> voxelData(levelResolution * levelResolution * levelResolution);
            glGetTexImage(GL_TEXTURE_3D, level, GL_RGBA, GL_FLOAT, voxelData.data());
            
            int stride = 1 << level;
            
            // Calculate sampling density to maintain constant voxel density
            // Larger grid size = need more samples to maintain density
            int samplingStride = std::max(1, static_cast<int>(baseGridSize / m_voxelGridSize));
            int effectiveSamples = levelResolution * samplingStride;
            
            // Loop through voxels with density-adjusted sampling
            for (int x = 0; x < effectiveSamples; x += samplingStride) {
                for (int y = 0; y < effectiveSamples; y += samplingStride) {
                    for (int z = 0; z < effectiveSamples; z += samplingStride) {
                        // Map back to texture coordinates
                        int tx = std::min(x / samplingStride, levelResolution - 1);
                        int ty = std::min(y / samplingStride, levelResolution - 1);
                        int tz = std::min(z / samplingStride, levelResolution - 1);
                        // Calculate index using texture coordinates
                        int index = (tz * levelResolution * levelResolution) + (ty * levelResolution) + tx;

                        // Check if this voxel has any color data
                        glm::vec4 voxelColor = voxelData[index];
                        if (voxelColor.a > 0.001f || (voxelColor.r + voxelColor.g + voxelColor.b) > 0.001f) {
                            // Calculate normalized position based on virtual sampling position
                            float nx = ((float)x + 0.5f) / (float)effectiveSamples;
                            float ny = ((float)y + 0.5f) / (float)effectiveSamples;
                            float nz = ((float)z + 0.5f) / (float)effectiveSamples;

                            // Convert back to world space using the SAME mapping as voxelization shader
                            // This must match the voxelization.frag: normalizedPos = (worldPos - gridCenter + halfGrid) / gridSize
                            float halfGrid = m_voxelGridSize * 0.5f;
                            glm::vec3 position(
                                (nx * m_voxelGridSize) - halfGrid + m_gridCenter.x,
                                (ny * m_voxelGridSize) - halfGrid + m_gridCenter.y,
                                (nz * m_voxelGridSize) - halfGrid + m_gridCenter.z
                            );

                            // Calculate distance from camera
                            float distanceFromCamera = glm::length(position - cameraPos);
                            
                            // In debug mode, use a fixed mipmap level instead of distance-based LOD
                            bool shouldShowVoxel;
                            if (showDebugVisualization) {
                                // In debug mode, only show voxels from the current debug state (m_state)
                                shouldShowVoxel = (level == m_state);
                            } else {
                                // Normal mode: use distance-based LOD selection
                                int appropriateLOD = calculateMipmapLevel(distanceFromCamera);
                                shouldShowVoxel = (appropriateLOD == level);
                            }
                            
                            // Only show voxel if it meets the LOD criteria
                            if (shouldShowVoxel) {
                                // Add to visible voxels with LOD information
                                VoxelData voxel;
                                voxel.position = position;
                                voxel.color = voxelColor;
                                voxel.mipmapLevel = level;
                                m_visibleVoxels.push_back(voxel);
                            }
                        }
                    }
                }
            }
        }

        // Allow more voxels to be displayed for better scene coverage
        const size_t maxVoxels = 200000;
        if (m_visibleVoxels.size() > maxVoxels) {
            std::random_device rd;
            std::mt19937 g(rd());
            std::shuffle(m_visibleVoxels.begin(), m_visibleVoxels.end(), g);
            m_visibleVoxels.resize(maxVoxels);
        }

        // Update the instance VBO with the new voxel data
        if (!m_visibleVoxels.empty()) {
            glBindBuffer(GL_ARRAY_BUFFER, m_voxelInstanceVBO);

            // Create a buffer for position, color, and mipmap level data
            std::vector<float> instanceData;
            instanceData.reserve(m_visibleVoxels.size() * 8); // 3 for position, 4 for color, 1 for mipmap level

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

                // Mipmap level (as float for GPU upload)
                instanceData.push_back(static_cast<float>(voxel.mipmapLevel));
            }

            // Upload instance data
            glBufferData(GL_ARRAY_BUFFER, instanceData.size() * sizeof(float), instanceData.data(), GL_DYNAMIC_DRAW);

            // Set up the VAO for the cube with instance data
            glBindVertexArray(m_cubeVAO);

            // Position attribute (location 2)
            glEnableVertexAttribArray(2);
            glVertexAttribPointer(2, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);
            glVertexAttribDivisor(2, 1); // This makes it instanced

            // Color attribute (location 3)
            glEnableVertexAttribArray(3);
            glVertexAttribPointer(3, 4, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));
            glVertexAttribDivisor(3, 1); // This makes it instanced

            // Mipmap level attribute (location 4)
            glEnableVertexAttribArray(4);
            glVertexAttribPointer(4, 1, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(7 * sizeof(float)));
            glVertexAttribDivisor(4, 1); // This makes it instanced

            glBindVertexArray(0);
        }

        m_voxelDataNeedsUpdate = false;
    }

    void Voxelizer::renderVoxelsAsCubes(const glm::vec3& cameraPos, const glm::mat4& projection, const glm::mat4& view) {
        // Update voxel data if needed
        if (m_voxelDataNeedsUpdate || m_visibleVoxels.empty()) {
            updateVisibleVoxels(cameraPos);
        }

        // Skip rendering if no visible voxels
        if (m_visibleVoxels.empty()) {
            return;
        }

        // Setup rendering state for solid voxel cubes
        glDisable(GL_BLEND);
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        
        // Disable face culling to show all cube faces for debugging
        glDisable(GL_CULL_FACE);

        // Use voxel cube shader
        m_voxelCubeShader->use();

        // Set shader uniforms
        m_voxelCubeShader->setMat4("model", glm::mat4(1.0f));
        m_voxelCubeShader->setMat4("view", view);
        m_voxelCubeShader->setMat4("projection", projection);
        m_voxelCubeShader->setVec3("viewPos", cameraPos);
        m_voxelCubeShader->setFloat("opacity", voxelOpacity);
        m_voxelCubeShader->setFloat("colorIntensity", voxelColorIntensity);

        // Set visualization mode uniform
        m_voxelCubeShader->setInt("visualizationMode", static_cast<int>(visualizationMode));

        // Set base voxel size (level 0) - independent of grid size as per NVIDIA paper
        m_voxelCubeShader->setFloat("baseVoxelSize", debugVoxelSize);
        m_voxelCubeShader->setInt("resolution", m_resolution);

        // Make sure the cube VAO is properly set up
        if (m_cubeVAO == 0) {
            setupUnitCube();
        }

        // Render all voxel instances
        glBindVertexArray(m_cubeVAO);
        glDrawArraysInstanced(GL_TRIANGLES, 0, 36, m_visibleVoxels.size());

        // Reset state
        glBindVertexArray(0);
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

    void Voxelizer::autoFitGridToScene(const std::vector<Model>& models, float paddingFactor) {
        // Compute scene AABB using the existing AABB struct from BVH.h
        AABB sceneBounds;

        for (const auto& model : models) {
            if (!model.visible) continue;

            // Build model transformation matrix
            glm::mat4 modelMatrix = glm::mat4(1.0f);
            modelMatrix = glm::translate(modelMatrix, model.position);
            modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.x), glm::vec3(1, 0, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.y), glm::vec3(0, 1, 0));
            modelMatrix = glm::rotate(modelMatrix, glm::radians(model.rotation.z), glm::vec3(0, 0, 1));
            modelMatrix = glm::scale(modelMatrix, model.scale);

            // Expand bounds with all mesh vertices (transformed to world space)
            for (const auto& mesh : model.getMeshes()) {
                if (!mesh.visible) continue;

                for (const auto& vertex : mesh.vertices) {
                    glm::vec4 worldPos = modelMatrix * glm::vec4(vertex.position, 1.0f);
                    sceneBounds.expand(glm::vec3(worldPos));
                }
            }
        }

        // Check if we found any valid geometry
        if (!sceneBounds.isValid()) {
            std::cerr << "Warning: No visible geometry found for auto-fit grid" << std::endl;
            return;
        }

        // Compute grid center and size
        glm::vec3 center = sceneBounds.getCenter();
        glm::vec3 size = sceneBounds.getSize();

        // Use the largest dimension to create a cubic grid (voxels are cubic)
        float maxDimension = std::max({size.x, size.y, size.z});

        // Apply padding factor to ensure geometry doesn't touch grid edges
        float gridSize = maxDimension * paddingFactor;

        // Ensure minimum grid size to avoid degenerate cases
        gridSize = std::max(gridSize, 0.1f);

        // Apply the computed values and mark dirty for re-voxelization
        m_gridCenter = center;
        m_voxelGridSize = gridSize;
        m_needsRevoxelization = true;

        std::cout << "Auto-fit grid: center=(" << center.x << ", " << center.y << ", " << center.z
                  << "), size=" << gridSize << std::endl;
    }

    void Voxelizer::generateMipmaps() {
        if (!m_mipmapComputeShader) {
            // Fallback to standard mipmap generation if compute shader failed to load
            glBindTexture(GL_TEXTURE_3D, m_voxelTexture);
            glGenerateMipmap(GL_TEXTURE_3D);
            return;
        }

        // Calculate number of mipmap levels
        int numLevels = static_cast<int>(std::floor(std::log2(m_resolution))) + 1;

        m_mipmapComputeShader->use();

        // Generate each mipmap level using compute shader
        int srcSize = m_resolution;
        for (int level = 0; level < numLevels - 1; level++) {
            int dstSize = std::max(1, srcSize / 2);

            // Bind source mipmap level for reading
            glBindImageTexture(0, m_voxelTexture, level, GL_TRUE, 0, GL_READ_ONLY, GL_RGBA16F);

            // Bind destination mipmap level for writing
            glBindImageTexture(1, m_voxelTexture, level + 1, GL_TRUE, 0, GL_WRITE_ONLY, GL_RGBA16F);

            // Dispatch compute shader
            // Work groups of 8x8x8, so we need ceiling division
            int groupsX = (dstSize + 7) / 8;
            int groupsY = (dstSize + 7) / 8;
            int groupsZ = (dstSize + 7) / 8;

            glDispatchCompute(groupsX, groupsY, groupsZ);

            // Memory barrier to ensure writes are visible for next level
            glMemoryBarrier(GL_SHADER_IMAGE_ACCESS_BARRIER_BIT);

            srcSize = dstSize;
        }
    }

} // namespace Engine