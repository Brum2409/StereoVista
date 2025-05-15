#include "CursorManager.h"
#include <GLFW/glfw3.h>
#include <iostream>
#include <imgui.h>

extern Camera camera;
extern int windowWidth;
extern int windowHeight;

namespace Cursor {
    CursorManager::CursorManager() :
        m_cursorPosition(0.0f),
        m_cursorPositionValid(false),
        m_showOrbitCenter(false),
        m_orbitCenterColor(0.0f, 1.0f, 0.0f, 0.7f),
        m_orbitCenterSphereRadius(0.2f),
        m_windowWidth(1920),
        m_windowHeight(1080),
        m_lastX(0.0f),
        m_lastY(0.0f)
    {
        m_sphereCursor = std::make_unique<SphereCursor>();
        m_fragmentCursor = std::make_unique<FragmentCursor>();
        m_planeCursor = std::make_unique<PlaneCursor>();
    }

    CursorManager::~CursorManager() {
        cleanup();
    }

    void CursorManager::initialize() {
        m_sphereCursor->initialize();
        m_fragmentCursor->initialize();
        m_planeCursor->initialize();

        m_windowWidth = windowWidth;
        m_windowHeight = windowHeight;
    }

    void CursorManager::updateCursorPosition(GLFWwindow* window, const glm::mat4& projection, const glm::mat4& view, Engine::Shader* shader) {
        // Skip if ImGui wants mouse input
        if (ImGui::GetIO().WantCaptureMouse) {
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            return;
        }

        // During orbiting, maintain cursor at the captured position
        if (camera.IsOrbiting) {
            m_cursorPositionValid = true;
            // Assuming g_capturedCursorPos is set elsewhere when orbiting starts
            return;
        }

        // Only update if not animating
        if (camera.IsAnimating) {
            return;
        }

        // Get cursor position
        double xpos, ypos;
        glfwGetCursorPos(window, &xpos, &ypos);
        m_lastX = static_cast<float>(xpos);
        m_lastY = static_cast<float>(ypos);

        m_windowWidth = windowWidth;
        m_windowHeight = windowHeight;

        // Read depth at cursor position
        float depth = 0.0;
        glReadPixels(m_lastX, (float)m_windowHeight - m_lastY, 1, 1, GL_DEPTH_COMPONENT, GL_FLOAT, &depth);

        // Convert cursor position to world space
        glm::mat4 vpInv = glm::inverse(projection * view);
        glm::vec4 ndc = glm::vec4(
            (m_lastX / (float)m_windowWidth) * 2.0 - 1.0,
            1.0 - (m_lastY / (float)m_windowHeight) * 2.0,
            depth * 2.0 - 1.0,
            1.0
        );
        auto worldPosH = vpInv * ndc;
        auto worldPos = worldPosH / worldPosH.w;
        auto isHit = depth != 1.0;

        // Update cursor state
        if (isHit && (m_sphereCursor->isVisible() || m_fragmentCursor->isVisible() || m_planeCursor->isVisible())) {
            m_cursorPositionValid = true;
            m_cursorPosition = glm::vec3(worldPos);

            // Update all cursor positions
            m_sphereCursor->setPosition(m_cursorPosition);
            m_sphereCursor->setPositionValid(true);
            m_fragmentCursor->setPosition(m_cursorPosition);
            m_fragmentCursor->setPositionValid(true);
            m_planeCursor->setPosition(m_cursorPosition);
            m_planeCursor->setPositionValid(true);

            // Update radius for sphere cursor based on camera distance
            m_sphereCursor->calculateRadius(camera.Position);

            if (camera.IsPanning || (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)) return;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_HIDDEN);
        }
        else {
            m_cursorPositionValid = false;

            // Update all cursor validity
            m_sphereCursor->setPositionValid(false);
            m_fragmentCursor->setPositionValid(false);
            m_planeCursor->setPositionValid(false);

            if (camera.IsPanning || (glfwGetMouseButton(window, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS)) return;
            glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        }
    }

    void CursorManager::renderCursors(const glm::mat4& projection, const glm::mat4& view) {
        if (m_sphereCursor->isVisible()) {
            m_sphereCursor->render(projection, view, camera.Position);
        }

        if (m_planeCursor->isVisible()) {
            m_planeCursor->render(projection, view, camera.Position);
        }

        // Fragment cursor is rendered in the fragment shader via updateShaderUniforms
    }

    void CursorManager::updateShaderUniforms(Engine::Shader* shader) {
        if (!shader) return;

        // Set cursor position for fragment shader
        shader->setVec4("cursorPos",
            camera.IsOrbiting ?
            glm::vec4(m_cursorPosition, true) : // Always valid during orbiting
            glm::vec4(m_cursorPosition, m_cursorPositionValid ? 1.0f : 0.0f)
        );

        // Update fragment cursor uniforms if visible
        if (m_fragmentCursor->isVisible()) {
            m_fragmentCursor->updateShaderUniforms(shader);
        }
        else {
            shader->setFloat("baseOuterRadius", 0.0f);
            shader->setFloat("baseOuterBorderThickness", 0.0f);
            shader->setFloat("baseInnerRadius", 0.0f);
            shader->setFloat("baseInnerBorderThickness", 0.0f);
            shader->setVec4("outerCursorColor", glm::vec4(0.0f));
            shader->setVec4("innerCursorColor", glm::vec4(0.0f));
            shader->setBool("showFragmentCursor", false);
        }
    }

    void CursorManager::renderOrbitCenter(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& orbitPoint) {
        if (!m_showOrbitCenter) return;

        // Use the sphere cursor's shader to render the orbit center
        auto sphereShader = m_sphereCursor->getShader();
        if (!sphereShader) return;

        // Enable depth testing and blending
        glEnable(GL_DEPTH_TEST);
        glDepthFunc(GL_LESS);
        glEnable(GL_BLEND);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

        // Set up sphere shader for orbit center
        sphereShader->use();
        sphereShader->setMat4("projection", projection);
        sphereShader->setMat4("view", view);

        // Create model matrix for orbit center
        glm::mat4 model = glm::translate(glm::mat4(1.0f), orbitPoint);
        model = glm::scale(model, glm::vec3(m_orbitCenterSphereRadius));

        // Set shader uniforms
        sphereShader->setMat4("model", model);
        sphereShader->setVec3("viewPos", camera.Position);
        sphereShader->setVec4("sphereColor", m_orbitCenterColor);
        sphereShader->setFloat("transparency", 1.0f);
        sphereShader->setFloat("edgeSoftness", 0.0f);
        sphereShader->setFloat("centerTransparencyFactor", 0.0f);

        // Render orbit center sphere
        glBindVertexArray(m_sphereCursor->getVAO());
        glDrawElements(GL_TRIANGLES, m_sphereCursor->getIndices().size(), GL_UNSIGNED_INT, 0);
        glBindVertexArray(0);

        glDisable(GL_BLEND);
    }

    void CursorManager::cleanup() {
        m_sphereCursor->cleanup();
        m_fragmentCursor->cleanup();
        m_planeCursor->cleanup();
    }
}