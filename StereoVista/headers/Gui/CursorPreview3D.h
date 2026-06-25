#pragma once

#include "Engine/Core.h"
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <vector>

namespace Engine {
    class Shader;
}

namespace Cursor {
    class BaseCursor;
    class SphereCursor;
    class FragmentCursor;
    class PlaneCursor;
}

namespace GUI {
    // Renders a small, self-contained 3D preview of the active cursor sitting on
    // a lit reference cube. The preview is independent of the main scene: it owns
    // its own framebuffer (multisampled for smooth edges), geometry and shaders,
    // so it faithfully reproduces every cursor type and every appearance value
    // without disturbing the live render state.
    class CursorPreview3D {
    public:
        CursorPreview3D();
        ~CursorPreview3D();

        void initialize(int width = 256, int height = 256);
        void render(Cursor::BaseCursor* cursor);
        void cleanup();

        GLuint getTextureID() const { return m_colorTexture; }
        int getWidth() const { return m_width; }
        int getHeight() const { return m_height; }

        void setRotation(float x, float y) { m_rotationX = x; m_rotationY = y; }
        void updateRotation(float deltaX, float deltaY);

    private:
        // Setup helpers
        void generateCubeGeometry();
        void generateSphereGeometry();
        void generateQuadGeometry();
        void setupFramebuffer();
        void setupCamera();
        bool compileShaders();

        // Per-frame drawing
        void drawBackground();
        void drawReferenceCube(const glm::mat4& modelRotation);
        void drawSphereCursor(Cursor::SphereCursor* cursor, const glm::mat4& modelRotation);
        void drawFragmentCursor(Cursor::FragmentCursor* cursor);
        void drawPlaneCursor(Cursor::PlaneCursor* cursor);

        // Framebuffer (multisampled -> resolved color texture for ImGui)
        GLuint m_msaaFBO;
        GLuint m_msaaColorRBO;
        GLuint m_msaaDepthRBO;
        GLuint m_resolveFBO;
        GLuint m_colorTexture;
        int m_samples;

        // Geometry
        GLuint m_cubeVAO, m_cubeVBO, m_cubeEBO;
        GLuint m_sphereVAO, m_sphereVBO, m_sphereEBO;
        GLuint m_quadVAO, m_quadVBO;
        GLsizei m_cubeIndexCount;
        GLsizei m_sphereIndexCount;

        // View / interaction
        int m_width, m_height;
        float m_rotationX, m_rotationY;
        glm::vec3 m_cameraPosition;
        glm::mat4 m_projectionMatrix;
        glm::mat4 m_viewMatrix;

        // Shaders (owned; compiled from embedded source)
        Engine::Shader* m_litShader;      // reference cube
        Engine::Shader* m_sphereShader;   // sphere cursor
        Engine::Shader* m_discShader;     // plane cursor (camera-facing disc)
        Engine::Shader* m_ringShader;     // fragment cursor (concentric rings)
        Engine::Shader* m_gradientShader; // background

        bool m_initialized;
    };
}
