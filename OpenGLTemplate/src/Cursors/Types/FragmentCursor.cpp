#include "Cursors/Types/FragmentCursor.h"
#include "Engine/Shader.h"

namespace Cursor {
    FragmentCursor::FragmentCursor() : BaseCursor() {
        m_name = "FragmentCursor";

        // Initialize with default settings
        m_settings.baseOuterRadius = 0.04f;
        m_settings.baseOuterBorderThickness = 0.005f;
        m_settings.baseInnerRadius = 0.004f;
        m_settings.baseInnerBorderThickness = 0.005f;
        m_settings.outerColor = glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
        m_settings.innerColor = glm::vec4(1.0f, 1.0f, 1.0f, 0.5f);
    }

    FragmentCursor::~FragmentCursor() {
        cleanup();
    }

    void FragmentCursor::initialize() {
        // This cursor is implemented entirely in fragment shader
    }

    void FragmentCursor::render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPosition) {
        // This cursor is rendered via the main shader
    }

    void FragmentCursor::cleanup() {

    }

    void FragmentCursor::updateShaderUniforms(Engine::Shader* shader) {
        if (!shader || !m_visible) return;

        // Set fragment shader uniforms for cursor rendering
        shader->setFloat("baseOuterRadius", m_settings.baseOuterRadius);
        shader->setFloat("baseOuterBorderThickness", m_settings.baseOuterBorderThickness);
        shader->setFloat("baseInnerRadius", m_settings.baseInnerRadius);
        shader->setFloat("baseInnerBorderThickness", m_settings.baseInnerBorderThickness);
        shader->setVec4("outerCursorColor", m_settings.outerColor);
        shader->setVec4("innerCursorColor", m_settings.innerColor);
        shader->setBool("showFragmentCursor", true);
    }
}