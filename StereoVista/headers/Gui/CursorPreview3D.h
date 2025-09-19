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
}

namespace GUI {
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
        void generateCubeGeometry();
        void generateSphereGeometry();
        void generatePlaneGeometry();
        void setupFramebuffer();
        void renderCube();
        void renderFallbackReference();
        void setupCamera();
        void renderPreviewSphere(const glm::vec4& color, float radius, bool showInner, const glm::vec4& innerColor, float innerFactor, const glm::vec3& position, float transparency, float edgeSoftness, float centerTransparency);
        void renderPreviewFragmentCursor(float radius, const glm::vec4& color, const glm::vec3& position);
        void renderPreviewPlaneCursor(float diameter, const glm::vec4& color, const glm::vec3& position);

        // OpenGL objects
        GLuint m_framebuffer;
        GLuint m_colorTexture;
        GLuint m_depthTexture;
        GLuint m_cubeVAO, m_cubeVBO, m_cubeEBO;
        GLuint m_sphereVAO, m_sphereVBO, m_sphereEBO;
        GLuint m_planeVAO, m_planeVBO, m_planeEBO;

        // Cube geometry
        std::vector<float> m_cubeVertices;
        std::vector<unsigned int> m_cubeIndices;

        // Sphere geometry
        std::vector<float> m_sphereVertices;
        std::vector<unsigned int> m_sphereIndices;

        // Plane geometry
        std::vector<float> m_planeVertices;
        std::vector<unsigned int> m_planeIndices;

        // Rendering properties
        int m_width, m_height;
        float m_rotationX, m_rotationY;
        glm::mat4 m_projectionMatrix;
        glm::mat4 m_viewMatrix;
        glm::vec3 m_cameraPosition;

        // Shaders
        Engine::Shader* m_cubeShader;
        Engine::Shader* m_sphereShader;
        Engine::Shader* m_planeShader;

        bool m_initialized;
    };
}