#include "Gui/CursorPreview3D.h"
#include "Engine/Shader.h"
#include "Cursors/Base/Cursor.h"
#include "Cursors/Types/SphereCursor.h"
#include "Cursors/Types/FragmentCursor.h"
#include "Cursors/Types/PlaneCursor.h"
#include <iostream>
#include <corecrt_math_defines.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace GUI {

CursorPreview3D::CursorPreview3D() :
    m_framebuffer(0),
    m_colorTexture(0),
    m_depthTexture(0),
    m_cubeVAO(0),
    m_cubeVBO(0),
    m_cubeEBO(0),
    m_sphereVAO(0),
    m_sphereVBO(0),
    m_sphereEBO(0),
    m_planeVAO(0),
    m_planeVBO(0),
    m_planeEBO(0),
    m_width(256),
    m_height(256),
    m_rotationX(20.0f),
    m_rotationY(45.0f),
    m_cameraPosition(0.0f, 0.0f, 3.0f),
    m_cubeShader(nullptr),
    m_sphereShader(nullptr),
    m_planeShader(nullptr),
    m_initialized(false)
{
}

CursorPreview3D::~CursorPreview3D() {
    cleanup();
}

void CursorPreview3D::initialize(int width, int height) {
    if (m_initialized) {
        cleanup();
    }

    m_width = width;
    m_height = height;

    generateCubeGeometry();
    generateSphereGeometry();
    generatePlaneGeometry();
    setupFramebuffer();
    setupCamera();

    // Load cube shader (using a simple basic shader for the cube)
    try {
        m_cubeShader = Engine::loadShader(
            "assets/shaders/core/vertexShader.glsl",
            "assets/shaders/core/fragmentShader.glsl"
        );
    } catch (const std::exception& e) {
        std::cerr << "Failed to load cube shader: " << e.what() << std::endl;
        m_cubeShader = nullptr;
    }

    // Load sphere shader (using the cursor sphere shader)
    try {
        m_sphereShader = Engine::loadShader(
            "assets/shaders/cursors/sphereVertexShader.glsl",
            "assets/shaders/cursors/sphereFragmentShader.glsl"
        );
    } catch (const std::exception& e) {
        std::cerr << "Failed to load sphere shader: " << e.what() << std::endl;
        m_sphereShader = nullptr;
    }

    // Load plane cursor shader
    try {
        m_planeShader = Engine::loadShader(
            "assets/shaders/cursors/planeCursorVertexShader.glsl",
            "assets/shaders/cursors/planeCursorFragmentShader.glsl"
        );
    } catch (const std::exception& e) {
        std::cerr << "Failed to load plane cursor shader: " << e.what() << std::endl;
        m_planeShader = nullptr;
    }

    m_initialized = true;
}

void CursorPreview3D::generateCubeGeometry() {
    // Use the same cube geometry as the project's createCube function
    // Each vertex has position (3 floats), normal (3 floats), texcoord (2 floats) = 8 floats per vertex
    m_cubeVertices = {
        // Front face
        -0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  0.0f, 0.0f,  // Bottom-left
         0.5f, -0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  1.0f, 0.0f,  // Bottom-right
         0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  1.0f, 1.0f,  // Top-right
        -0.5f,  0.5f,  0.5f,  0.0f, 0.0f, 1.0f,  0.0f, 1.0f,  // Top-left

        // Back face
        -0.5f, -0.5f, -0.5f,  0.0f, 0.0f, -1.0f, 1.0f, 0.0f,  // Bottom-left
        -0.5f,  0.5f, -0.5f,  0.0f, 0.0f, -1.0f, 1.0f, 1.0f,  // Top-left
         0.5f,  0.5f, -0.5f,  0.0f, 0.0f, -1.0f, 0.0f, 1.0f,  // Top-right
         0.5f, -0.5f, -0.5f,  0.0f, 0.0f, -1.0f, 0.0f, 0.0f,  // Bottom-right

        // Top face
        -0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 0.0f,  // Back-left
        -0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  0.0f, 1.0f,  // Front-left
         0.5f,  0.5f,  0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 1.0f,  // Front-right
         0.5f,  0.5f, -0.5f,  0.0f, 1.0f, 0.0f,  1.0f, 0.0f,  // Back-right

        // Bottom face
        -0.5f, -0.5f, -0.5f,  0.0f, -1.0f, 0.0f, 0.0f, 1.0f,  // Back-left
         0.5f, -0.5f, -0.5f,  0.0f, -1.0f, 0.0f, 1.0f, 1.0f,  // Back-right
         0.5f, -0.5f,  0.5f,  0.0f, -1.0f, 0.0f, 1.0f, 0.0f,  // Front-right
        -0.5f, -0.5f,  0.5f,  0.0f, -1.0f, 0.0f, 0.0f, 0.0f,  // Front-left

        // Right face
         0.5f, -0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  0.0f, 0.0f,  // Bottom-back
         0.5f,  0.5f, -0.5f,  1.0f, 0.0f, 0.0f,  0.0f, 1.0f,  // Top-back
         0.5f,  0.5f,  0.5f,  1.0f, 0.0f, 0.0f,  1.0f, 1.0f,  // Top-front
         0.5f, -0.5f,  0.5f,  1.0f, 0.0f, 0.0f,  1.0f, 0.0f,  // Bottom-front

        // Left face
        -0.5f, -0.5f, -0.5f,  -1.0f, 0.0f, 0.0f, 1.0f, 0.0f,  // Bottom-back
        -0.5f, -0.5f,  0.5f,  -1.0f, 0.0f, 0.0f, 0.0f, 0.0f,  // Bottom-front
        -0.5f,  0.5f,  0.5f,  -1.0f, 0.0f, 0.0f, 0.0f, 1.0f,  // Top-front
        -0.5f,  0.5f, -0.5f,  -1.0f, 0.0f, 0.0f, 1.0f, 1.0f   // Top-back
    };

    m_cubeIndices = {
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

    // Generate and setup VAO, VBO, EBO for cube
    glGenVertexArrays(1, &m_cubeVAO);
    glGenBuffers(1, &m_cubeVBO);
    glGenBuffers(1, &m_cubeEBO);

    glBindVertexArray(m_cubeVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, m_cubeVertices.size() * sizeof(float), m_cubeVertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_cubeIndices.size() * sizeof(unsigned int), m_cubeIndices.data(), GL_STATIC_DRAW);

    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)0);

    // Normal attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(3 * sizeof(float)));

    // Texture coordinate attribute (even though we might not use it)
    glEnableVertexAttribArray(2);
    glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, 8 * sizeof(float), (void*)(6 * sizeof(float)));

    glBindVertexArray(0);
}

void CursorPreview3D::generateSphereGeometry() {
    m_sphereVertices.clear();
    m_sphereIndices.clear();

    const unsigned int rings = 32;
    const unsigned int sectors = 32;
    const float radius = 1.0f;

    for (unsigned int i = 0; i <= rings; ++i) {
        float phi = M_PI * static_cast<float>(i) / static_cast<float>(rings);
        float y = std::cos(phi);
        float ringRadius = std::sin(phi);

        for (unsigned int j = 0; j <= sectors; ++j) {
            float theta = 2.0f * M_PI * static_cast<float>(j) / static_cast<float>(sectors);
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);

            // Position
            m_sphereVertices.push_back(x * radius);
            m_sphereVertices.push_back(y * radius);
            m_sphereVertices.push_back(z * radius);

            // Normal (same as position for unit sphere)
            m_sphereVertices.push_back(x);
            m_sphereVertices.push_back(y);
            m_sphereVertices.push_back(z);
        }
    }

    // Generate indices
    for (unsigned int i = 0; i < rings; ++i) {
        for (unsigned int j = 0; j < sectors; ++j) {
            unsigned int current = i * (sectors + 1) + j;
            unsigned int next = current + sectors + 1;

            m_sphereIndices.push_back(current);
            m_sphereIndices.push_back(next);
            m_sphereIndices.push_back(current + 1);

            m_sphereIndices.push_back(current + 1);
            m_sphereIndices.push_back(next);
            m_sphereIndices.push_back(next + 1);
        }
    }

    // Generate and setup VAO, VBO, EBO for sphere
    glGenVertexArrays(1, &m_sphereVAO);
    glGenBuffers(1, &m_sphereVBO);
    glGenBuffers(1, &m_sphereEBO);

    glBindVertexArray(m_sphereVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, m_sphereVertices.size() * sizeof(float), m_sphereVertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_sphereIndices.size() * sizeof(unsigned int), m_sphereIndices.data(), GL_STATIC_DRAW);

    // Position attribute
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);

    // Normal attribute
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));

    glBindVertexArray(0);
}

void CursorPreview3D::generatePlaneGeometry() {
    // Create a simple quad for the plane cursor
    m_planeVertices = {
        // Quad vertices (position only)
        -0.5f, 0.0f, -0.5f,  // Bottom-left
         0.5f, 0.0f, -0.5f,  // Bottom-right
         0.5f, 0.0f,  0.5f,  // Top-right
        -0.5f, 0.0f,  0.5f   // Top-left
    };

    m_planeIndices = {
        0, 1, 2,  // First triangle
        2, 3, 0   // Second triangle
    };

    // Generate and setup VAO, VBO, EBO for plane
    glGenVertexArrays(1, &m_planeVAO);
    glGenBuffers(1, &m_planeVBO);
    glGenBuffers(1, &m_planeEBO);

    glBindVertexArray(m_planeVAO);

    glBindBuffer(GL_ARRAY_BUFFER, m_planeVBO);
    glBufferData(GL_ARRAY_BUFFER, m_planeVertices.size() * sizeof(float), m_planeVertices.data(), GL_STATIC_DRAW);

    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_planeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, m_planeIndices.size() * sizeof(unsigned int), m_planeIndices.data(), GL_STATIC_DRAW);

    // Position attribute (only position, no normal for simple plane shader)
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 3 * sizeof(float), (void*)0);

    glBindVertexArray(0);
}

void CursorPreview3D::setupFramebuffer() {
    // Generate framebuffer
    glGenFramebuffers(1, &m_framebuffer);
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);

    // Generate color texture
    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGB, m_width, m_height, 0, GL_RGB, GL_UNSIGNED_BYTE, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    // Generate depth texture
    glGenTextures(1, &m_depthTexture);
    glBindTexture(GL_TEXTURE_2D, m_depthTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH_COMPONENT, m_width, m_height, 0, GL_DEPTH_COMPONENT, GL_FLOAT, NULL);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_TEXTURE_2D, m_depthTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Framebuffer not complete!" << std::endl;
    }

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CursorPreview3D::setupCamera() {
    m_projectionMatrix = glm::perspective(glm::radians(45.0f), (float)m_width / (float)m_height, 0.1f, 100.0f);
    m_viewMatrix = glm::lookAt(m_cameraPosition, glm::vec3(0.0f, 0.0f, 0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
}

void CursorPreview3D::updateRotation(float deltaX, float deltaY) {
    m_rotationY += deltaX * 0.5f;
    m_rotationX += deltaY * 0.5f;

    // Clamp X rotation to avoid flipping
    m_rotationX = glm::clamp(m_rotationX, -89.0f, 89.0f);
}

void CursorPreview3D::renderCube() {
    if (m_cubeVAO == 0) return;

    // Try the sphere shader first as fallback since we know it works
    Engine::Shader* activeShader = m_sphereShader ? m_sphereShader : m_cubeShader;
    if (!activeShader) return;

    activeShader->use();

    // Create model matrix with rotation and scaling to make cube more visible
    glm::mat4 model = glm::mat4(1.0f);
    model = glm::rotate(model, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    model = glm::rotate(model, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));
    model = glm::scale(model, glm::vec3(0.8f)); // Slightly smaller cube to fit better in view

    activeShader->setMat4("model", model);
    activeShader->setMat4("view", m_viewMatrix);
    activeShader->setMat4("projection", m_projectionMatrix);

    // If using sphere shader, set sphere-specific uniforms
    if (activeShader == m_sphereShader) {
        activeShader->setVec3("viewPos", m_cameraPosition);
        activeShader->setVec4("sphereColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.3f)); // Semi-transparent white
        activeShader->setFloat("transparency", 0.3f);
        activeShader->setFloat("edgeSoftness", 0.0f); // Sharp edges for cube
        activeShader->setFloat("centerTransparency", 0.3f);
        activeShader->setFloat("innerSphereFactor", 1.0f);
        activeShader->setBool("isInnerSphere", false);
    } else {
        // Try cube shader uniforms with extensive fallbacks
        try {
            activeShader->setVec3("lightPos", glm::vec3(2.0f, 2.0f, 2.0f));
            activeShader->setVec3("viewPos", m_cameraPosition);
            activeShader->setVec3("objectColor", glm::vec3(1.0f, 1.0f, 1.0f));
            activeShader->setVec3("lightColor", glm::vec3(1.0f, 1.0f, 1.0f));
        } catch (...) {
            try {
                activeShader->setVec3("color", glm::vec3(1.0f, 1.0f, 1.0f));
            } catch (...) {
                try {
                    activeShader->setVec4("sphereColor", glm::vec4(1.0f, 1.0f, 1.0f, 1.0f));
                } catch (...) {}
            }
        }
    }

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glBindVertexArray(m_cubeVAO);

    // Render simple solid white cube
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    // Set white color for the cube
    if (activeShader == m_sphereShader) {
        activeShader->setVec4("sphereColor", glm::vec4(1.0f, 1.0f, 1.0f, 0.8f)); // White with slight transparency
        activeShader->setFloat("transparency", 0.8f);
    } else {
        try {
            activeShader->setVec3("objectColor", glm::vec3(1.0f, 1.0f, 1.0f));
        } catch (...) {}
    }

    glDrawElements(GL_TRIANGLES, m_cubeIndices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void CursorPreview3D::renderFallbackReference() {
    // If cube rendering fails, render some simple reference lines using sphere shader
    if (!m_sphereShader) return;

    m_sphereShader->use();

    // Render some reference spheres at the corners of where the cube should be
    glm::vec3 corners[] = {
        glm::vec3(-0.4f, -0.4f, -0.4f), glm::vec3(0.4f, -0.4f, -0.4f),
        glm::vec3(-0.4f, 0.4f, -0.4f), glm::vec3(0.4f, 0.4f, -0.4f),
        glm::vec3(-0.4f, -0.4f, 0.4f), glm::vec3(0.4f, -0.4f, 0.4f),
        glm::vec3(-0.4f, 0.4f, 0.4f), glm::vec3(0.4f, 0.4f, 0.4f)
    };

    if (m_sphereVAO != 0) {
        for (int i = 0; i < 8; i++) {
            glm::mat4 cubeRotation = glm::mat4(1.0f);
            cubeRotation = glm::rotate(cubeRotation, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
            cubeRotation = glm::rotate(cubeRotation, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));

            glm::vec3 rotatedCorner = glm::vec3(cubeRotation * glm::vec4(corners[i], 1.0f));

            glm::mat4 model = glm::mat4(1.0f);
            model = glm::translate(model, rotatedCorner);
            model = glm::scale(model, glm::vec3(0.05f)); // Small reference spheres

            m_sphereShader->setMat4("model", model);
            m_sphereShader->setMat4("view", m_viewMatrix);
            m_sphereShader->setMat4("projection", m_projectionMatrix);
            m_sphereShader->setVec3("viewPos", m_cameraPosition);
            m_sphereShader->setVec4("sphereColor", glm::vec4(1.0f, 1.0f, 0.0f, 1.0f)); // Bright yellow
            m_sphereShader->setFloat("transparency", 1.0f);
            m_sphereShader->setFloat("edgeSoftness", 0.0f);
            m_sphereShader->setFloat("centerTransparency", 1.0f);
            m_sphereShader->setFloat("innerSphereFactor", 1.0f);
            m_sphereShader->setBool("isInnerSphere", false);

            glBindVertexArray(m_sphereVAO);
            glDrawElements(GL_TRIANGLES, m_sphereIndices.size(), GL_UNSIGNED_INT, 0);
        }
        glBindVertexArray(0);
    }
}

void CursorPreview3D::render(Cursor::BaseCursor* cursor) {
    if (!m_initialized || !cursor) return;

    // Save current viewport and framebuffer
    GLint viewport[4];
    glGetIntegerv(GL_VIEWPORT, viewport);
    GLint currentFramebuffer;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &currentFramebuffer);

    // Bind our framebuffer
    glBindFramebuffer(GL_FRAMEBUFFER, m_framebuffer);
    glViewport(0, 0, m_width, m_height);

    // Clear the framebuffer with darker background to show wireframes better
    glClearColor(0.1f, 0.1f, 0.1f, 1.0f); // Darker background
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    // Render the cube first
    renderCube();

    // Render the cursor based on its type - completely independent preview
    // Position cursor at one of the cube edges (front-top-right corner like the yellow dots)
    glm::vec3 cursorPosition(0.4f, 0.4f, 0.4f); // At cube corner where yellow dots are

    if (cursor->isVisible()) {
        // Calculate scale in preview context using preview camera
        cursor->setPosition(cursorPosition);  // Temporarily set position for scaling calculation
        float previewScale = cursor->calculateScale(m_cameraPosition);

        if (auto* sphereCursor = dynamic_cast<Cursor::SphereCursor*>(cursor)) {
            // Use the unified scaling system with preview context
            float basePreviewRadius = 0.2f;
            float scaledRadius = basePreviewRadius * previewScale;
            renderPreviewSphere(
                sphereCursor->getColor(),
                scaledRadius,
                sphereCursor->getShowInnerSphere(),
                sphereCursor->getInnerSphereColor(),
                sphereCursor->getInnerSphereFactor(),
                cursorPosition,
                sphereCursor->getTransparency(),
                sphereCursor->getEdgeSoftness(),
                sphereCursor->getCenterTransparency()
            );
        }
        else if (auto* fragmentCursor = dynamic_cast<Cursor::FragmentCursor*>(cursor)) {
            // Use the unified scaling system for fragment cursor
            float basePreviewRadius = 0.2f;
            float scaledRadius = basePreviewRadius * previewScale;
            renderPreviewFragmentCursor(
                scaledRadius,
                fragmentCursor->getOuterColor(),
                cursorPosition
            );
        }
        else if (auto* planeCursor = dynamic_cast<Cursor::PlaneCursor*>(cursor)) {
            // Use the unified scaling system for plane cursor
            float basePreviewDiameter = 0.3f;
            float scaledDiameter = basePreviewDiameter * previewScale;
            renderPreviewPlaneCursor(
                scaledDiameter,
                planeCursor->getColor(),
                cursorPosition
            );
        }
    }

    // Restore previous framebuffer and viewport
    glBindFramebuffer(GL_FRAMEBUFFER, currentFramebuffer);
    glViewport(viewport[0], viewport[1], viewport[2], viewport[3]);
}

void CursorPreview3D::renderPreviewSphere(const glm::vec4& color, float radius, bool showInner, const glm::vec4& innerColor, float innerFactor, const glm::vec3& position, float transparency, float edgeSoftness, float centerTransparency) {
    if (!m_sphereShader || m_sphereVAO == 0) return;

    // Save current OpenGL state
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLint blendSrc, blendDst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDst);
    GLboolean depthMaskState;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskState);
    GLint cullFaceMode;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);

    // Set required state for sphere rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    m_sphereShader->use();

    // Create model matrix with cube rotation, then position and scale the sphere
    glm::mat4 cubeRotation = glm::mat4(1.0f);
    cubeRotation = glm::rotate(cubeRotation, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    cubeRotation = glm::rotate(cubeRotation, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));

    // Transform position by cube rotation, then translate and scale
    glm::vec3 rotatedPosition = glm::vec3(cubeRotation * glm::vec4(position, 1.0f));

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, rotatedPosition);
    model = glm::scale(model, glm::vec3(radius));

    m_sphereShader->setMat4("view", m_viewMatrix);
    m_sphereShader->setMat4("projection", m_projectionMatrix);
    m_sphereShader->setVec3("viewPos", m_cameraPosition);
    m_sphereShader->setFloat("innerSphereFactor", innerFactor);

    glBindVertexArray(m_sphereVAO);

    // First pass: Render back faces (exactly like real sphere cursor)
    glDepthMask(GL_TRUE);
    glCullFace(GL_FRONT);

    if (showInner) {
        m_sphereShader->setBool("isInnerSphere", true);
        m_sphereShader->setVec4("sphereColor", innerColor);
        m_sphereShader->setFloat("transparency", 1.0f);
        glm::mat4 innerModel = glm::scale(model, glm::vec3(innerFactor));
        m_sphereShader->setMat4("model", innerModel);
        glDrawElements(GL_TRIANGLES, m_sphereIndices.size(), GL_UNSIGNED_INT, 0);
    }

    m_sphereShader->setBool("isInnerSphere", false);
    m_sphereShader->setVec4("sphereColor", color);
    m_sphereShader->setFloat("transparency", transparency);
    m_sphereShader->setFloat("edgeSoftness", edgeSoftness);
    m_sphereShader->setFloat("centerTransparencyFactor", centerTransparency);
    m_sphereShader->setMat4("model", model);
    glDrawElements(GL_TRIANGLES, m_sphereIndices.size(), GL_UNSIGNED_INT, 0);

    // Second pass: Render front faces (exactly like real sphere cursor)
    glDepthMask(GL_FALSE);
    glCullFace(GL_BACK);

    if (showInner) {
        m_sphereShader->setBool("isInnerSphere", true);
        m_sphereShader->setVec4("sphereColor", innerColor);
        m_sphereShader->setFloat("transparency", 1.0f);
        glm::mat4 innerModel = glm::scale(model, glm::vec3(innerFactor));
        m_sphereShader->setMat4("model", innerModel);
        glDrawElements(GL_TRIANGLES, m_sphereIndices.size(), GL_UNSIGNED_INT, 0);
    }

    m_sphereShader->setBool("isInnerSphere", false);
    m_sphereShader->setVec4("sphereColor", color);
    m_sphereShader->setFloat("transparency", transparency);
    m_sphereShader->setFloat("edgeSoftness", edgeSoftness);
    m_sphereShader->setFloat("centerTransparencyFactor", centerTransparency);
    m_sphereShader->setMat4("model", model);
    glDrawElements(GL_TRIANGLES, m_sphereIndices.size(), GL_UNSIGNED_INT, 0);

    // Restore previous OpenGL state
    if (blendEnabled) {
        glEnable(GL_BLEND);
        glBlendFunc(blendSrc, blendDst);
    } else {
        glDisable(GL_BLEND);
    }
    glDepthMask(depthMaskState);
    glCullFace(cullFaceMode);
    glBindVertexArray(0);
}

void CursorPreview3D::renderPreviewFragmentCursor(float radius, const glm::vec4& color, const glm::vec3& position) {
    // Fragment cursor is a 2D circle effect - use the sphere shader with modified settings to approximate this
    if (!m_sphereShader || m_sphereVAO == 0) return;

    // Save current OpenGL state
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLint blendSrc, blendDst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDst);
    GLboolean depthMaskState;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskState);
    GLint cullFaceMode;
    glGetIntegerv(GL_CULL_FACE_MODE, &cullFaceMode);

    // Set required state for fragment cursor rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);

    m_sphereShader->use();

    // Create model matrix with cube rotation, then position and scale
    glm::mat4 cubeRotation = glm::mat4(1.0f);
    cubeRotation = glm::rotate(cubeRotation, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    cubeRotation = glm::rotate(cubeRotation, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));

    // Transform position by cube rotation, then translate and scale
    glm::vec3 rotatedPosition = glm::vec3(cubeRotation * glm::vec4(position, 1.0f));

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, rotatedPosition);
    model = glm::scale(model, glm::vec3(radius));

    m_sphereShader->setMat4("model", model);
    m_sphereShader->setMat4("view", m_viewMatrix);
    m_sphereShader->setMat4("projection", m_projectionMatrix);
    m_sphereShader->setVec3("viewPos", m_cameraPosition);

    // Set sphere properties to mimic the 2D circle effect
    // Make it more opaque and with sharp edges to simulate the fragment cursor
    m_sphereShader->setVec4("sphereColor", color);
    m_sphereShader->setFloat("transparency", 1.0f); // Fully opaque
    m_sphereShader->setFloat("edgeSoftness", 0.0f); // Sharp edges like the fragment cursor
    m_sphereShader->setFloat("centerTransparency", 1.0f); // No center transparency
    m_sphereShader->setFloat("innerSphereFactor", 0.5f); // Small inner area
    m_sphereShader->setBool("isInnerSphere", false);

    glBindVertexArray(m_sphereVAO);

    // Render only front faces for a more 2D-like appearance
    glDepthMask(GL_TRUE);
    glCullFace(GL_BACK);
    glDrawElements(GL_TRIANGLES, m_sphereIndices.size(), GL_UNSIGNED_INT, 0);

    // Restore previous OpenGL state
    if (blendEnabled) {
        glEnable(GL_BLEND);
        glBlendFunc(blendSrc, blendDst);
    } else {
        glDisable(GL_BLEND);
    }
    glDepthMask(depthMaskState);
    glCullFace(cullFaceMode);
    glBindVertexArray(0);
}

void CursorPreview3D::renderPreviewPlaneCursor(float diameter, const glm::vec4& color, const glm::vec3& position) {
    // Use the proper plane cursor shader and geometry
    if (!m_planeShader || m_planeVAO == 0) return;

    // Save current OpenGL state
    GLboolean blendEnabled = glIsEnabled(GL_BLEND);
    GLint blendSrc, blendDst;
    glGetIntegerv(GL_BLEND_SRC_ALPHA, &blendSrc);
    glGetIntegerv(GL_BLEND_DST_ALPHA, &blendDst);
    GLboolean depthMaskState;
    glGetBooleanv(GL_DEPTH_WRITEMASK, &depthMaskState);

    // Set required state for plane cursor rendering
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    m_planeShader->use();

    // Create model matrix with cube rotation, then position and scale the plane
    glm::mat4 cubeRotation = glm::mat4(1.0f);
    cubeRotation = glm::rotate(cubeRotation, glm::radians(m_rotationX), glm::vec3(1.0f, 0.0f, 0.0f));
    cubeRotation = glm::rotate(cubeRotation, glm::radians(m_rotationY), glm::vec3(0.0f, 1.0f, 0.0f));

    // Transform position by cube rotation
    glm::vec3 rotatedPosition = glm::vec3(cubeRotation * glm::vec4(position, 1.0f));

    glm::mat4 model = glm::mat4(1.0f);
    model = glm::translate(model, rotatedPosition);
    model = glm::scale(model, glm::vec3(diameter, 1.0f, diameter)); // Scale x and z for diameter, keep y thin

    m_planeShader->setMat4("model", model);
    m_planeShader->setMat4("view", m_viewMatrix);
    m_planeShader->setMat4("projection", m_projectionMatrix);

    // Set plane color
    m_planeShader->setVec4("color", color);

    // Disable depth writing for transparent plane
    glDepthMask(GL_FALSE);

    glBindVertexArray(m_planeVAO);
    glDrawElements(GL_TRIANGLES, m_planeIndices.size(), GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);

    // Restore previous OpenGL state
    if (blendEnabled) {
        glEnable(GL_BLEND);
        glBlendFunc(blendSrc, blendDst);
    } else {
        glDisable(GL_BLEND);
    }
    glDepthMask(depthMaskState);
}

void CursorPreview3D::cleanup() {
    if (m_framebuffer) {
        glDeleteFramebuffers(1, &m_framebuffer);
        m_framebuffer = 0;
    }
    if (m_colorTexture) {
        glDeleteTextures(1, &m_colorTexture);
        m_colorTexture = 0;
    }
    if (m_depthTexture) {
        glDeleteTextures(1, &m_depthTexture);
        m_depthTexture = 0;
    }
    if (m_cubeVAO) {
        glDeleteVertexArrays(1, &m_cubeVAO);
        m_cubeVAO = 0;
    }
    if (m_cubeVBO) {
        glDeleteBuffers(1, &m_cubeVBO);
        m_cubeVBO = 0;
    }
    if (m_cubeEBO) {
        glDeleteBuffers(1, &m_cubeEBO);
        m_cubeEBO = 0;
    }
    if (m_sphereVAO) {
        glDeleteVertexArrays(1, &m_sphereVAO);
        m_sphereVAO = 0;
    }
    if (m_sphereVBO) {
        glDeleteBuffers(1, &m_sphereVBO);
        m_sphereVBO = 0;
    }
    if (m_sphereEBO) {
        glDeleteBuffers(1, &m_sphereEBO);
        m_sphereEBO = 0;
    }
    if (m_planeVAO) {
        glDeleteVertexArrays(1, &m_planeVAO);
        m_planeVAO = 0;
    }
    if (m_planeVBO) {
        glDeleteBuffers(1, &m_planeVBO);
        m_planeVBO = 0;
    }
    if (m_planeEBO) {
        glDeleteBuffers(1, &m_planeEBO);
        m_planeEBO = 0;
    }
    if (m_cubeShader) {
        delete m_cubeShader;
        m_cubeShader = nullptr;
    }
    if (m_sphereShader) {
        delete m_sphereShader;
        m_sphereShader = nullptr;
    }
    if (m_planeShader) {
        delete m_planeShader;
        m_planeShader = nullptr;
    }

    m_initialized = false;
}

} // namespace GUI