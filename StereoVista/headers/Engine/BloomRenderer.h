#pragma once

#include "Engine/Shader.h"
#include <glad/glad.h>


namespace Engine {

struct BloomSettings {
  bool enabled = false;
  float threshold = 1.0f;  // Brightness threshold for bloom extraction
  float intensity = 0.04f; // Bloom effect intensity
  int blurPasses = 10;     // Number of blur iterations
  float exposure = 1.0f;   // HDR exposure
  int toneMapOperator = 0; // 0=Reinhard, 1=ACES, 2=Filmic

  // Framebuffer objects
  GLuint hdrFBO = 0;
  GLuint bloomFBO[2] = {0, 0}; // Ping-pong framebuffers
  GLuint hdrColorBuffer = 0;
  GLuint hdrBrightBuffer = 0;
  GLuint bloomColorBuffers[2] = {0, 0};
  GLuint rboDepth = 0; // Depth renderbuffer

  // Shaders
  Shader *blurShader = nullptr;
  Shader *finalShader = nullptr;

  // Screen quad for post-processing
  GLuint quadVAO = 0;
  GLuint quadVBO = 0;
};

class BloomRenderer {
public:
  BloomRenderer();
  ~BloomRenderer();

  // Initialize bloom system
  bool initialize(int width, int height);

  // Cleanup resources
  void cleanup();

  // Resize framebuffers
  void resize(int width, int height);

  // Begin bloom rendering (bind HDR framebuffer)
  void beginBloomPass();

  // Apply bloom effect and render final result
  void applyBloom(GLuint sceneTexture, const BloomSettings &settings,
                  GLenum drawBuffer = GL_BACK);

  // Get bloom settings
  BloomSettings &getSettings() { return m_settings; }

  // Render a fullscreen quad
  void renderQuad();

  // Validate bloom system functionality
  bool validateBloomSystem() const;

private:
  BloomSettings m_settings;
  int m_width, m_height;
  bool m_initialized = false;

  // Setup framebuffers
  bool setupFramebuffers();

  // Setup screen quad
  void setupQuad();

  // Load shaders
  bool loadShaders();
};

} // namespace Engine