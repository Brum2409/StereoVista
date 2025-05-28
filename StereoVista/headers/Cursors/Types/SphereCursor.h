#pragma once

#include "Cursors/Base/Cursor.h"
#include "Gui/GuiTypes.h"
#include <vector>

namespace Cursor {
    class SphereCursor : public BaseCursor {
    public:
        SphereCursor();
        ~SphereCursor() override;

        // Implementation of base class virtual methods
        void initialize() override;
        void render(const glm::mat4& projection, const glm::mat4& view, const glm::vec3& cameraPosition);
        void cleanup() override;
        void updateShaderUniforms(Engine::Shader* shader) override;

        // Sphere cursor specific methods
        float calculateRadius(const glm::vec3& cameraPosition);
        void generateMesh(float radius, unsigned int rings, unsigned int sectors);

        // Getters and setters for specific properties
        GUI::CursorScalingMode getScalingMode() const { return m_scalingMode; }
        void setScalingMode(GUI::CursorScalingMode mode) { m_scalingMode = mode; }
        float getFixedRadius() const { return m_fixedRadius; }
        void setFixedRadius(float radius) { m_fixedRadius = radius; }
        const glm::vec4& getColor() const { return m_color; }
        void setColor(const glm::vec4& color) { m_color = color; }
        float getTransparency() const { return m_transparency; }
        void setTransparency(float transparency) { m_transparency = transparency; }
        float getEdgeSoftness() const { return m_edgeSoftness; }
        void setEdgeSoftness(float softness) { m_edgeSoftness = softness; }
        float getCenterTransparency() const { return m_centerTransparency; }
        void setCenterTransparency(float transparency) { m_centerTransparency = transparency; }
        bool getShowInnerSphere() const { return m_showInnerSphere; }
        void setShowInnerSphere(bool show) { m_showInnerSphere = show; }
        const glm::vec4& getInnerSphereColor() const { return m_innerSphereColor; }
        void setInnerSphereColor(const glm::vec4& color) { m_innerSphereColor = color; }
        float getInnerSphereFactor() const { return m_innerSphereFactor; }
        void setInnerSphereFactor(float factor) { m_innerSphereFactor = factor; }
        float getMinDiff() const { return m_minDiff; }
        void setMinDiff(float diff) { m_minDiff = diff; }
        float getMaxDiff() const { return m_maxDiff; }
        void setMaxDiff(float diff) { m_maxDiff = diff; }
        float getCurrentRadius() const { return m_currentRadius; }

        Engine::Shader* getShader() const { return m_shader; }
        GLuint getVAO() const { return m_vao; }
        const std::vector<unsigned int>& getIndices() const { return m_indices; }

    private:
        friend class CursorManager;

        GLuint m_vao, m_vbo, m_ebo;
        std::vector<float> m_vertices;
        std::vector<unsigned int> m_indices;
        Engine::Shader* m_shader;

        // Specific properties
        GUI::CursorScalingMode m_scalingMode;
        float m_fixedRadius;
        float m_currentRadius;
        float m_minDiff;
        float m_maxDiff;
        glm::vec4 m_color;
        float m_transparency;
        float m_edgeSoftness;
        float m_centerTransparency;
        bool m_showInnerSphere;
        glm::vec4 m_innerSphereColor;
        float m_innerSphereFactor;
    };
}