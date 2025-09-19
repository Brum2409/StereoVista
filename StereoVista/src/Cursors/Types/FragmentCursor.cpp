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

        // Calculate unified scaling - we need to use the current cursor position
        // The fragment shader will use viewPos and cursorPos to calculate distance
        // We'll modify the base radius values to incorporate our unified scaling

        // For now, we'll approximate the scaling by using a base scale factor
        // The fragment shader's internal scaling will be on top of this
        float unifiedScaleFactor = 1.0f;

        switch (m_scalingMode) {
        case GUI::CURSOR_NORMAL:
            unifiedScaleFactor = 1.0f;  // No scaling
            break;
        case GUI::CURSOR_FIXED:
            unifiedScaleFactor = 1.0f;  // Let fragment shader handle distance scaling
            break;
        case GUI::CURSOR_CONSTRAINED_DYNAMIC:
            unifiedScaleFactor = 1.0f;  // Let fragment shader handle distance scaling (default)
            break;
        case GUI::CURSOR_LOGARITHMIC:
            unifiedScaleFactor = 1.0f;  // Let fragment shader handle distance scaling
            break;
        }

        // Set fragment shader uniforms with unified scaling applied
        shader->setFloat("baseOuterRadius", m_settings.baseOuterRadius * unifiedScaleFactor);
        shader->setFloat("baseOuterBorderThickness", m_settings.baseOuterBorderThickness * unifiedScaleFactor);
        shader->setFloat("baseInnerRadius", m_settings.baseInnerRadius * unifiedScaleFactor);
        shader->setFloat("baseInnerBorderThickness", m_settings.baseInnerBorderThickness * unifiedScaleFactor);
        shader->setVec4("outerCursorColor", m_settings.outerColor);
        shader->setVec4("innerCursorColor", m_settings.innerColor);
        shader->setBool("showFragmentCursor", true);

        // Pass scaling mode to fragment shader for custom scaling behavior
        shader->setInt("fragmentCursorScalingMode", static_cast<int>(m_scalingMode));
        shader->setFloat("fragmentCursorBaseSize", m_baseSize);
        shader->setFloat("fragmentCursorMinDiff", m_minDiff);
        shader->setFloat("fragmentCursorMaxDiff", m_maxDiff);
    }
}