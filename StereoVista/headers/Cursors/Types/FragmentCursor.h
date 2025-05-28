#pragma once

#include "Cursors/Base/Cursor.h"
#include "Gui/GuiTypes.h"

namespace Cursor {
    class FragmentCursor : public BaseCursor {
    public:
        FragmentCursor();
        ~FragmentCursor() override;

        // Implementation of base class virtual methods
        void initialize() override;
        void render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPosition);
        void cleanup() override;
        void updateShaderUniforms(Engine::Shader* shader) override;

        // Getters and setters for specific properties
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