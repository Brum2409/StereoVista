#include "Tools/BrushTool.h"
#include <glm/gtx/rotate_vector.hpp>
#include <glm/gtx/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <iostream>

namespace Tools {

    BrushTool::BrushTool()
        : m_enabled(false)
        , m_brushRadius(0.5f)
        , m_selectedModelIndex(-1)
        , m_density(1.0f)
        , m_minSpacing(0.1f)
        , m_activeClusterIndex(-1)
        , m_rng(std::random_device{}())
        , m_dist(0.0f, 1.0f)
    {
    }

    BrushTool::~BrushTool() {
        clearAllInstances();
        cleanupIndicatorResources();
    }

    void BrushTool::enable() {
        m_enabled = true;
        std::cout << "Brush tool enabled" << std::endl;
    }

    void BrushTool::disable() {
        m_enabled = false;
        std::cout << "Brush tool disabled" << std::endl;
    }

    int BrushTool::createCluster(const std::string& name, int modelIndex) {
        if (modelIndex < 0) {
            std::cout << "Cannot create cluster: invalid model index " << modelIndex << std::endl;
            return -1;
        }

        m_clusters.emplace_back(name, modelIndex);
        int newIndex = m_clusters.size() - 1;
        m_activeClusterIndex = newIndex;
        std::cout << "Created cluster '" << name << "' at index " << newIndex << " with model index " << modelIndex << std::endl;
        return newIndex;
    }

    void BrushTool::deleteCluster(int clusterIndex) {
        if (clusterIndex < 0 || clusterIndex >= m_clusters.size()) {
            std::cout << "Invalid cluster index: " << clusterIndex << std::endl;
            return;
        }

        std::cout << "Deleting cluster '" << m_clusters[clusterIndex].name << "'" << std::endl;
        m_clusters.erase(m_clusters.begin() + clusterIndex);

        // Adjust active cluster index
        if (m_activeClusterIndex >= m_clusters.size()) {
            m_activeClusterIndex = m_clusters.empty() ? -1 : m_clusters.size() - 1;
        }
        else if (m_activeClusterIndex == clusterIndex) {
            m_activeClusterIndex = m_clusters.empty() ? -1 : 0;
        }
        else if (m_activeClusterIndex > clusterIndex) {
            m_activeClusterIndex--;
        }
    }

    void BrushTool::setActiveCluster(int clusterIndex) {
        if (clusterIndex < -1 || clusterIndex >= m_clusters.size()) {
            std::cout << "Invalid cluster index: " << clusterIndex << std::endl;
            return;
        }
        m_activeClusterIndex = clusterIndex;
        if (clusterIndex >= 0) {
            std::cout << "Active cluster set to '" << m_clusters[clusterIndex].name << "'" << std::endl;
        }
        else {
            std::cout << "No active cluster" << std::endl;
        }
    }

    BrushCluster* BrushTool::getCluster(int index) {
        if (index < 0 || index >= m_clusters.size()) return nullptr;
        return &m_clusters[index];
    }

    const BrushCluster* BrushTool::getCluster(int index) const {
        if (index < 0 || index >= m_clusters.size()) return nullptr;
        return &m_clusters[index];
    }

    void BrushTool::paintInstance(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& cameraPos, const glm::vec3& sourceModelScale) {
        // Check if we have an active cluster
        if (m_activeClusterIndex < 0 || m_activeClusterIndex >= m_clusters.size()) {
            std::cout << "No active cluster selected for painting" << std::endl;
            return;
        }

        BrushCluster* cluster = &m_clusters[m_activeClusterIndex];

        // Check minimum spacing
        if (m_minSpacing > 0.0f && isTooCloseToExisting(position, cluster)) {
            return;
        }

        // Create transform for the instance with cluster settings
        glm::mat4 transform = createInstanceTransform(position, normal, cameraPos, sourceModelScale, cluster);

        // Generate color variation
        glm::vec3 color = generateInstanceColor(glm::vec3(1.0f, 1.0f, 1.0f), cluster->colorVariation);

        // Add instance
        cluster->instances.emplace_back(transform, color);
        cluster->needsUpdate = true;

        std::cout << "Painted instance to cluster '" << cluster->name << "' at position ("
                  << position.x << ", " << position.y << ", " << position.z << ")" << std::endl;
    }

    void BrushTool::clearAllInstances() {
        m_clusters.clear();
        m_paintedGroups.clear();
        m_activeClusterIndex = -1;
        std::cout << "Cleared all painted instances and clusters" << std::endl;
    }

    void BrushTool::clearInstancesForCluster(int clusterIndex) {
        if (clusterIndex < 0 || clusterIndex >= m_clusters.size()) {
            std::cout << "Invalid cluster index: " << clusterIndex << std::endl;
            return;
        }

        m_clusters[clusterIndex].instances.clear();
        m_clusters[clusterIndex].needsUpdate = true;
        std::cout << "Cleared instances for cluster '" << m_clusters[clusterIndex].name << "'" << std::endl;
    }

    void BrushTool::removeLastInstance() {
        if (m_activeClusterIndex < 0 || m_activeClusterIndex >= m_clusters.size()) {
            std::cout << "No active cluster selected" << std::endl;
            return;
        }

        BrushCluster* cluster = &m_clusters[m_activeClusterIndex];
        if (!cluster->instances.empty()) {
            cluster->instances.pop_back();
            cluster->needsUpdate = true;
            std::cout << "Removed last instance from cluster '" << cluster->name << "'" << std::endl;
        }
    }

    int BrushTool::getTotalInstanceCount() const {
        int total = 0;
        for (const auto& cluster : m_clusters) {
            total += cluster.instances.size();
        }
        for (const auto& group : m_paintedGroups) {
            total += group.instances.size();
        }
        return total;
    }

    int BrushTool::getInstanceCountForCluster(int clusterIndex) const {
        if (clusterIndex < 0 || clusterIndex >= m_clusters.size()) {
            return 0;
        }
        return m_clusters[clusterIndex].instances.size();
    }

    void BrushTool::renderInstances(Engine::Shader* shader, const std::vector<Engine::Model>& models) {
        // Render all clusters
        static int frameCount = 0;
        bool shouldLog = (frameCount++ % 60 == 0); // Log every 60 frames

        if (shouldLog && !m_clusters.empty()) {
            std::cout << "Rendering " << m_clusters.size() << " clusters:" << std::endl;
        }

        for (auto& cluster : m_clusters) {
            if (shouldLog) {
                std::cout << "  Cluster '" << cluster.name << "': " << cluster.instances.size() << " instances, model " << cluster.sourceModelIndex << std::endl;
            }

            if (cluster.instances.empty()) continue;
            if (cluster.sourceModelIndex < 0 || cluster.sourceModelIndex >= models.size()) continue;

            const Engine::Model& sourceModel = models[cluster.sourceModelIndex];
            if (!sourceModel.visible) continue;

            // Update GPU buffer if needed
            if (cluster.needsUpdate) {
                if (cluster.instanceVBO == 0)
                    glGenBuffers(1, &cluster.instanceVBO);

                GLsizeiptr newSize = (GLsizeiptr)(cluster.instances.size() * sizeof(InstanceData));
                glBindBuffer(GL_ARRAY_BUFFER, cluster.instanceVBO);

                if (cluster.instanceBufferCapacity > 0 && newSize <= cluster.instanceBufferCapacity) {
                    // Same or smaller count: update data in-place without GPU reallocation
                    glBufferSubData(GL_ARRAY_BUFFER, 0, newSize, cluster.instances.data());
                } else {
                    // Buffer needs to grow (or first allocation)
                    glBufferData(GL_ARRAY_BUFFER, newSize, cluster.instances.data(), GL_DYNAMIC_DRAW);
                    cluster.instanceBufferCapacity = newSize;
                }

                cluster.needsUpdate = false;
            }

            // Skip cluster if instanceVBO creation failed
            if (cluster.instanceVBO == 0) continue;

            // Set material properties from source model
            shader->setBool("material.hasNormalMap", sourceModel.hasNormalMap());
            shader->setBool("material.hasSpecularMap", sourceModel.hasSpecularMap());
            shader->setBool("material.hasAOMap", sourceModel.hasAOMap());
            shader->setFloat("material.hasTexture", !sourceModel.getMeshes().empty() && !sourceModel.getMeshes()[0].textures.empty() ? 1.0f : 0.0f);
            shader->setVec3("material.objectColor", sourceModel.color);
            shader->setFloat("material.shininess", sourceModel.shininess);
            shader->setFloat("material.emissive", sourceModel.emissive);
            shader->setFloat("material.metallicFactor", sourceModel.metallicFactor);
            shader->setFloat("material.roughnessFactor", sourceModel.roughnessFactor);

            // Render each mesh of the model with instancing
            for (size_t meshIdx = 0; meshIdx < sourceModel.meshes.size(); meshIdx++) {
                const auto& mesh = sourceModel.meshes[meshIdx];
                if (!mesh.visible) continue;
                if (mesh.VAO == 0) continue; // Skip meshes with invalid VAO

                // Set mesh-specific material properties
                shader->setInt("material.numDiffuseTextures", 0);
                shader->setInt("material.numSpecularTextures", 0);
                shader->setInt("material.numNormalTextures", 0);

                // Bind textures from the mesh
                int diffuseNr = 0, specularNr = 0, normalNr = 0, aoNr = 0;
                for (size_t i = 0; i < mesh.textures.size(); i++) {
                    glActiveTexture(GL_TEXTURE0 + i);
                    std::string number;
                    std::string name = mesh.textures[i].type;

                    if (name == "texture_diffuse") {
                        number = std::to_string(diffuseNr++);
                        shader->setInt("material.numDiffuseTextures", diffuseNr);
                    }
                    else if (name == "texture_specular") {
                        number = std::to_string(specularNr++);
                        shader->setInt("material.numSpecularTextures", specularNr);
                    }
                    else if (name == "texture_normal") {
                        number = std::to_string(normalNr++);
                        shader->setInt("material.numNormalTextures", normalNr);
                    }
                    else if (name == "texture_ao") {
                        number = std::to_string(aoNr++);
                    }

                    shader->setInt(("material.textures[" + std::to_string(i) + "]").c_str(), i);
                    glBindTexture(GL_TEXTURE_2D, mesh.textures[i].id);
                }

                // Verify VAO is valid before binding
                if (mesh.VAO == 0) {
                    std::cout << "Warning: Skipping mesh with invalid VAO (model index: " << cluster.sourceModelIndex << ")" << std::endl;
                    continue;
                }

                glBindVertexArray(mesh.VAO);

                // Verify VAO is now bound
                GLint currentVAO = 0;
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &currentVAO);
                if (currentVAO == 0) {
                    std::cout << "Error: Failed to bind VAO " << mesh.VAO << std::endl;
                    continue;
                }

                // Setup instance attributes (model matrix + color)
                setupInstanceAttributes(mesh.VAO);

                // Bind instance buffer
                if (cluster.instanceVBO == 0) {
                    std::cout << "Error: instanceVBO is 0 for cluster '" << cluster.name << "'" << std::endl;
                    continue;
                }

                glBindBuffer(GL_ARRAY_BUFFER, cluster.instanceVBO);

                // Verify buffer is bound
                GLint currentBuffer = 0;
                glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &currentBuffer);
                if (currentBuffer != cluster.instanceVBO) {
                    std::cout << "Error: Failed to bind instance buffer for cluster '" << cluster.name << "'" << std::endl;
                    continue;
                }

                // Verify VAO is still bound before setting up attributes
                GLint stillBoundVAO = 0;
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &stillBoundVAO);
                if (stillBoundVAO == 0) {
                    std::cout << "Error: VAO no longer bound before attribute setup for cluster '" << cluster.name << "'" << std::endl;
                    continue;
                }

                // Enable instance attributes
                // Locations 5-8: mat4 model matrix (4 vec4s)
                for (int i = 0; i < 4; i++) {
                    glEnableVertexAttribArray(5 + i);
                    glVertexAttribPointer(5 + i, 4, GL_FLOAT, GL_FALSE,
                        sizeof(InstanceData),
                        (void*)(sizeof(glm::vec4) * i));
                    glVertexAttribDivisor(5 + i, 1);
                }

                // Verify VAO is still bound before setting up attribute 9
                glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &stillBoundVAO);
                if (stillBoundVAO == 0) {
                    std::cout << "Error: VAO lost after setting up attributes 5-8 for cluster '" << cluster.name << "'" << std::endl;
                    continue;
                }

                // Location 9: vec3 instance color
                glEnableVertexAttribArray(9);
                glVertexAttribPointer(9, 3, GL_FLOAT, GL_FALSE,
                    sizeof(InstanceData),
                    (void*)(sizeof(glm::mat4)));
                glVertexAttribDivisor(9, 1);

                // Draw instanced
                glDrawElementsInstanced(GL_TRIANGLES, mesh.indices.size(),
                    GL_UNSIGNED_INT, 0, cluster.instances.size());

                // Cleanup
                for (int i = 0; i < 4; i++) {
                    glVertexAttribDivisor(5 + i, 0);
                }
                glVertexAttribDivisor(9, 0);

                glBindVertexArray(0);
            }
        }
    }

    void BrushTool::updateInstanceBuffers() {
        for (auto& cluster : m_clusters) {
            cluster.needsUpdate = true;
        }
        for (auto& group : m_paintedGroups) {
            group.needsUpdate = true;
        }
    }

    PaintedModelGroup* BrushTool::findOrCreateGroup(int modelIndex) {
        // Try to find existing group
        for (auto& group : m_paintedGroups) {
            if (group.sourceModelIndex == modelIndex) {
                return &group;
            }
        }

        // Create new group
        m_paintedGroups.emplace_back(modelIndex);
        return &m_paintedGroups.back();
    }

    glm::mat4 BrushTool::createInstanceTransform(const glm::vec3& position, const glm::vec3& normal, const glm::vec3& cameraPos, const glm::vec3& sourceModelScale, const BrushCluster* cluster) {
        glm::mat4 transform = glm::mat4(1.0f);

        // Translation
        transform = glm::translate(transform, position);

        // Rotation (align to normal or random)
        if (cluster->alignToNormal) {
            // Create a rotation matrix that aligns the Y-axis (up) with the normal
            glm::vec3 normalizedNormal = glm::normalize(normal);

            // Build an orthonormal basis from the normal
            glm::vec3 tangent, bitangent;

            // Find a vector that's not parallel to the normal
            glm::vec3 helper = glm::abs(normalizedNormal.y) < 0.999f ?
                glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);

            // Create tangent perpendicular to normal
            tangent = glm::normalize(glm::cross(helper, normalizedNormal));

            // Create bitangent perpendicular to both (ensuring right-handed system)
            bitangent = glm::normalize(glm::cross(normalizedNormal, tangent));

            // Build rotation matrix with normal as Y-axis (up)
            // Using column-major order for GLM
            glm::mat3 rotationMatrix;
            rotationMatrix[0] = tangent;          // X-axis
            rotationMatrix[1] = normalizedNormal; // Y-axis (up)
            rotationMatrix[2] = bitangent;        // Z-axis

            // Ensure right-handed coordinate system by checking determinant
            float det = glm::determinant(rotationMatrix);
            if (det < 0.0f) {
                // Flip one axis to make it right-handed
                rotationMatrix[2] = -rotationMatrix[2];
            }

            // Apply rotation to transform
            transform = transform * glm::mat4(rotationMatrix);
        }

        // Additional random rotation around the normal/up axis
        if (cluster->rotationRandomization > 0.0f) {
            float randomAngle = m_dist(m_rng) * glm::two_pi<float>() * cluster->rotationRandomization;
            glm::vec3 upAxis = cluster->alignToNormal ? normal : glm::vec3(0.0f, 1.0f, 0.0f);
            transform = glm::rotate(transform, randomAngle, glm::normalize(upAxis));
        }

        // Scale: apply source model's scale multiplied by random factor between min and max
        float randomScaleFactor = cluster->minScale + m_dist(m_rng) * (cluster->maxScale - cluster->minScale);
        glm::vec3 finalScale = sourceModelScale * randomScaleFactor;
        transform = glm::scale(transform, finalScale);

        return transform;
    }

    glm::vec3 BrushTool::generateInstanceColor(const glm::vec3& baseColor, float colorVariation) {
        if (colorVariation <= 0.0f) {
            return baseColor;
        }

        glm::vec3 variation(
            (m_dist(m_rng) - 0.5f) * 2.0f * colorVariation,
            (m_dist(m_rng) - 0.5f) * 2.0f * colorVariation,
            (m_dist(m_rng) - 0.5f) * 2.0f * colorVariation
        );

        glm::vec3 color = baseColor + variation;
        return glm::clamp(color, 0.0f, 1.0f);
    }

    bool BrushTool::isTooCloseToExisting(const glm::vec3& position, const BrushCluster* cluster) {
        for (const auto& instance : cluster->instances) {
            // Extract position from model matrix
            glm::vec3 existingPos(instance.modelMatrix[3]);
            float distance = glm::length(position - existingPos);

            if (distance < m_minSpacing) {
                return true;
            }
        }
        return false;
    }

    void BrushTool::setupInstanceAttributes(GLuint vao) {
        // This function sets up the vertex attribute divisors for instanced rendering
        // The actual attribute setup is done in renderInstances
    }

    namespace {
        const char* kIndicatorVertexSrc = R"(
#version 330 core
layout (location = 0) in vec3 aPos;

uniform mat4 uViewProj;

void main() {
    gl_Position = uViewProj * vec4(aPos, 1.0);
}
)";

        const char* kIndicatorFragmentSrc = R"(
#version 330 core
out vec4 FragColor;

uniform vec4 uColor;

void main() {
    FragColor = uColor;
}
)";
    }

    void BrushTool::ensureIndicatorResources() {
        if (m_indicatorVAO != 0) return;

        glGenVertexArrays(1, &m_indicatorVAO);
        glGenBuffers(1, &m_indicatorVBO);
        glBindVertexArray(m_indicatorVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_indicatorVBO);
        glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(glm::vec3), (void*)0);
        glEnableVertexAttribArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);

        auto compile = [](GLenum type, const char* src) -> GLuint {
            GLuint shader = glCreateShader(type);
            glShaderSource(shader, 1, &src, nullptr);
            glCompileShader(shader);
            GLint ok = GL_FALSE;
            glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
            if (!ok) {
                char log[1024];
                glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
                std::cerr << "[BrushTool] indicator shader compile error: " << log << std::endl;
            }
            return shader;
        };

        GLuint vs = compile(GL_VERTEX_SHADER, kIndicatorVertexSrc);
        GLuint fs = compile(GL_FRAGMENT_SHADER, kIndicatorFragmentSrc);
        m_indicatorProgram = glCreateProgram();
        glAttachShader(m_indicatorProgram, vs);
        glAttachShader(m_indicatorProgram, fs);
        glLinkProgram(m_indicatorProgram);
        glDeleteShader(vs);
        glDeleteShader(fs);

        GLint ok = GL_FALSE;
        glGetProgramiv(m_indicatorProgram, GL_LINK_STATUS, &ok);
        if (!ok) {
            char log[1024];
            glGetProgramInfoLog(m_indicatorProgram, sizeof(log), nullptr, log);
            std::cerr << "[BrushTool] indicator program link error: " << log << std::endl;
            glDeleteProgram(m_indicatorProgram);
            m_indicatorProgram = 0;
        }
    }

    void BrushTool::cleanupIndicatorResources() {
        if (m_indicatorVAO) { glDeleteVertexArrays(1, &m_indicatorVAO); m_indicatorVAO = 0; }
        if (m_indicatorVBO) { glDeleteBuffers(1, &m_indicatorVBO); m_indicatorVBO = 0; }
        if (m_indicatorProgram) { glDeleteProgram(m_indicatorProgram); m_indicatorProgram = 0; }
    }

    void BrushTool::renderBrushIndicator(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cursorPos, const glm::vec3& cursorNormal) {
        if (!m_enabled) return;

        ensureIndicatorResources();
        if (m_indicatorProgram == 0) return;

        // Create a circle of vertices around the cursor position
        const int segments = 64;
        std::vector<glm::vec3> circleVertices;
        circleVertices.reserve(segments);

        // Build an orthonormal basis from the cursor normal (same as instance alignment)
        glm::vec3 normalizedNormal = glm::normalize(cursorNormal);
        glm::vec3 helper = glm::abs(normalizedNormal.y) < 0.999f ?
            glm::vec3(0.0f, 1.0f, 0.0f) : glm::vec3(1.0f, 0.0f, 0.0f);
        glm::vec3 tangent = glm::normalize(glm::cross(normalizedNormal, helper));
        glm::vec3 bitangent = glm::normalize(glm::cross(normalizedNormal, tangent));

        // Generate circle vertices
        for (int i = 0; i < segments; i++) {
            float angle = (float)i / (float)segments * glm::two_pi<float>();
            float x = cos(angle) * m_brushRadius;
            float y = sin(angle) * m_brushRadius;

            // Calculate position in world space using the tangent/bitangent basis
            glm::vec3 offset = tangent * x + bitangent * y;
            glm::vec3 vertex = cursorPos + offset + normalizedNormal * 0.001f; // Slight offset to prevent z-fighting
            circleVertices.push_back(vertex);
        }

        // Save the GL state we touch
        GLint prevDepthFunc = GL_LESS;
        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
        GLboolean prevDepthMask = GL_TRUE;
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
        const GLboolean prevBlend = glIsEnabled(GL_BLEND);

        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
        glDepthMask(GL_FALSE);
        glDepthFunc(GL_LEQUAL);
        glLineWidth(3.0f);

        const glm::mat4 viewProj = projection * view;
        glUseProgram(m_indicatorProgram);
        glUniformMatrix4fv(glGetUniformLocation(m_indicatorProgram, "uViewProj"),
                           1, GL_FALSE, &viewProj[0][0]);

        glBindVertexArray(m_indicatorVAO);
        glBindBuffer(GL_ARRAY_BUFFER, m_indicatorVBO);
        glBufferData(GL_ARRAY_BUFFER,
                     static_cast<GLsizeiptr>(circleVertices.size() * sizeof(glm::vec3)),
                     circleVertices.data(), GL_DYNAMIC_DRAW);

        // Pass 1: depth-tested, opaque
        glUniform4f(glGetUniformLocation(m_indicatorProgram, "uColor"),
                    1.0f, 0.6f, 0.1f, 0.9f);
        glDrawArrays(GL_LINE_LOOP, 0, static_cast<GLsizei>(circleVertices.size()));

        // Pass 2: faint through-geometry hint so the brush stays visible
        glDepthFunc(GL_ALWAYS);
        glUniform4f(glGetUniformLocation(m_indicatorProgram, "uColor"),
                    1.0f, 0.6f, 0.1f, 0.25f);
        glDrawArrays(GL_LINE_LOOP, 0, static_cast<GLsizei>(circleVertices.size()));

        glBindBuffer(GL_ARRAY_BUFFER, 0);
        glBindVertexArray(0);
        glUseProgram(0);

        // Restore state
        glLineWidth(1.0f);
        glDepthFunc(prevDepthFunc);
        glDepthMask(prevDepthMask);
        if (!prevBlend) glDisable(GL_BLEND);
    }

} // namespace Tools
