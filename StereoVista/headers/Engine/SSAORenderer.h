#pragma once

#include "Engine/Shader.h"
#include <glad/glad.h>
#include <glm/glm.hpp>
#include <vector>

namespace Engine {

struct SSAOSettings {
  bool enabled = false;
  int kernelSize = 64;    // Number of SSAO samples (16-64)
  float radius = 0.5f;    // Sample radius in view space
  float bias = 0.025f;    // Depth comparison bias
  float power = 1.0f;     // Occlusion intensity/power curve
};

class SSAORenderer {
public:
  SSAORenderer();
  ~SSAORenderer();

  // Initialize SSAO system
  bool initialize(int width, int height);

  // Cleanup all resources
  void cleanup();

  // Resize framebuffers when window changes
  void resize(int width, int height);

  // Geometry pass: render scene positions/normals to G-buffer
  // Call this before rendering geometry, then render models, then call
  // endGeometryPass()
  void beginGeometryPass();
  void endGeometryPass();

  // SSAO pass: compute ambient occlusion from G-buffer
  void computeSSAO(const glm::mat4 &projection);

  // Blur pass: smooth the SSAO result
  void blurSSAO();

  // Get the final blurred SSAO texture (GL_RED, single channel)
  GLuint getSSAOTexture() const { return m_ssaoColorBufferBlur; }

  // Get the G-buffer textures (for debugging)
  GLuint getPositionTexture() const { return m_gPosition; }
  GLuint getNormalTexture() const { return m_gNormal; }

  // Get the geometry pass shader (for rendering models)
  Shader *getGeometryShader() const { return m_geometryShader; }

  // Get settings
  SSAOSettings &getSettings() { return m_settings; }
  const SSAOSettings &getSettings() const { return m_settings; }

  // Render a fullscreen quad
  void renderQuad();

  // Check if initialized
  bool isInitialized() const { return m_initialized; }

private:
  SSAOSettings m_settings;
  int m_width, m_height;
  bool m_initialized = false;

  // G-buffer
  GLuint m_gBufferFBO = 0;
  GLuint m_gPosition = 0;   // View-space position (RGBA16F)
  GLuint m_gNormal = 0;     // View-space normal (RGB16F)
  GLuint m_gDepthRBO = 0;   // Depth renderbuffer

  // SSAO
  GLuint m_ssaoFBO = 0;
  GLuint m_ssaoColorBuffer = 0; // Single-channel occlusion (GL_RED)

  // SSAO Blur
  GLuint m_ssaoBlurFBO = 0;
  GLuint m_ssaoColorBufferBlur = 0; // Blurred occlusion (GL_RED)

  // Noise texture
  GLuint m_noiseTexture = 0;

  // Sample kernel
  std::vector<glm::vec3> m_ssaoKernel;

  // Shaders
  Shader *m_geometryShader = nullptr;
  Shader *m_ssaoShader = nullptr;
  Shader *m_blurShader = nullptr;

  // Screen quad
  GLuint m_quadVAO = 0;
  GLuint m_quadVBO = 0;

  // Setup methods
  bool setupGBuffer();
  bool setupSSAOBuffers();
  void generateSampleKernel();
  void generateNoiseTexture();
  bool loadShaders();
  void setupQuad();
};

} // namespace Engine
