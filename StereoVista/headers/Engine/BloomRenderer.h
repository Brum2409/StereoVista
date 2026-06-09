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

  // FXAA (post-tonemap anti-aliasing). When enabled the final composite is
  // rendered to an offscreen LDR buffer and resolved to the screen by an FXAA
  // pass; when disabled the composite is presented directly (no extra cost).
  bool fxaaEnabled = true;
  float fxaaSubpixel = 0.75f;       // sub-pixel aliasing removal [0..1]
  float fxaaEdgeThreshold = 0.166f; // relative edge threshold (~0.063..0.333)

  // Framebuffer objects
  GLuint hdrFBO = 0;
  GLuint bloomFBO[2] = {0, 0}; // Ping-pong framebuffers
  GLuint hdrColorBuffer = 0;
  GLuint hdrBrightBuffer = 0;
  GLuint bloomColorBuffers[2] = {0, 0};
  // Schütz Phase 1: depth stored as a texture so the EDL pass can sample it
  GLuint depthTexture = 0;
  // FXAA intermediate: tone-mapped LDR result, sampled by the FXAA resolve pass
  GLuint ldrFBO = 0;
  GLuint ldrColorBuffer = 0;

  // Shaders
  Shader *blurShader = nullptr;
  Shader *finalShader = nullptr;
  Shader *fxaaShader = nullptr;

  // Screen quad for post-processing
  GLuint quadVAO = 0;
  GLuint quadVBO = 0;
};

// Schütz Phase 1: Eye-Dome Lighting settings
struct EDLSettings {
  bool enabled  = false;
  float strength = 1.0f;   // shading contrast  (0 = off, 1 = default, 5 = strong)
  float radius   = 1.5f;   // neighbour-sample radius in pixels
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

  // Apply bloom effect and render final result.
  // edlSettings / nearPlane / farPlane are used to compute Eye-Dome Lighting
  // when EDL is enabled; they are ignored when EDL is disabled.
  // presentOffsetX / presentOffsetY shift the final composited image within the
  // target framebuffer so it can land in a sub-rectangle (e.g. the free area
  // beside the docked GUI panels). Defaults present at the origin (full window).
  void applyBloom(GLuint sceneTexture, const BloomSettings &settings,
                  GLenum drawBuffer = GL_BACK,
                  const EDLSettings *edlSettings = nullptr,
                  float nearPlane = 0.1f, float farPlane = 200.0f,
                  int presentOffsetX = 0, int presentOffsetY = 0);

  // Set SSAO texture to be applied during final composition
  void setSSAOTexture(GLuint ssaoTexture, bool ssaoEnabled);


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

  // SSAO integration
  GLuint m_ssaoTexture = 0;
  bool m_ssaoEnabled = false;

  // Setup framebuffers
  bool setupFramebuffers();

  // Setup screen quad
  void setupQuad();

  // Load shaders
  bool loadShaders();
};

} // namespace Engine
