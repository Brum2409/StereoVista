#pragma once

#include "Cursors/Base/Cursor.h"
#include "Gui/GuiTypes.h"

namespace renderer {
struct FragmentCursorState;
}

namespace Cursor {
    // Screen/world ring cursor drawn BY THE SCENE SHADER on geometry surfaces
    // (mesh.frag since Phase 6; the GL uber-shader before). This class only
    // holds settings and publishes them into the FrameSubmission.
    class FragmentCursor : public BaseCursor {
    public:
        FragmentCursor();
        ~FragmentCursor() override;

        void initialize() override;

        // Fills the renderer-side state (colors converted to linear there by
        // the caller if desired; we pass the authored values through).
        void fillState(renderer::FragmentCursorState& out) const;

        float getBaseOuterRadius() const { return m_settings.baseOuterRadius; }
        void setBaseOuterRadius(float radius) { m_settings.baseOuterRadius = radius; }
        float getBaseOuterBorderThickness() const { return m_settings.baseOuterBorderThickness; }
        void setBaseOuterBorderThickness(float thickness) { m_settings.baseOuterBorderThickness = thickness; }
        float getBaseInnerRadius() const { return m_settings.baseInnerRadius; }
        void setBaseInnerRadius(float radius) { m_settings.baseInnerRadius = radius; }
        float getBaseInnerBorderThickness() const { return m_settings.baseInnerBorderThickness; }
        void setBaseInnerBorderThickness(float thickness) { m_settings.baseInnerBorderThickness = thickness; }
        const glm::vec4& getOuterColor() const { return m_settings.outerColor; }
        void setOuterColor(const glm::vec4& color) { m_settings.outerColor = color; }
        const glm::vec4& getInnerColor() const { return m_settings.innerColor; }
        void setInnerColor(const glm::vec4& color) { m_settings.innerColor = color; }

        const GUI::FragmentShaderCursorSettings& getSettings() const { return m_settings; }
        void setSettings(const GUI::FragmentShaderCursorSettings& settings) { m_settings = settings; }

    private:
        GUI::FragmentShaderCursorSettings m_settings;
    };
}
