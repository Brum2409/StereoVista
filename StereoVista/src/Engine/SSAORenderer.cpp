#include "Engine/SSAORenderer.h"
#include "Engine/Core.h"
#include <iostream>
#include <random>

namespace Engine {

static float lerp(float a, float b, float f) { return a + f * (b - a); }

SSAORenderer::SSAORenderer() : m_width(0), m_height(0) {}

SSAORenderer::~SSAORenderer() { cleanup(); }

bool SSAORenderer::initialize(int width, int height) {
  m_width = width;
  m_height = height;

  if (!loadShaders()) {
    std::cerr << "Failed to load SSAO shaders" << std::endl;
    return false;
  }

  if (!setupGBuffer()) {
    std::cerr << "Failed to setup SSAO G-buffer" << std::endl;
    return false;
  }

  if (!setupSSAOBuffers()) {
    std::cerr << "Failed to setup SSAO framebuffers" << std::endl;
    return false;
  }

  generateSampleKernel();
  generateNoiseTexture();
  setupQuad();

  m_initialized = true;
  return true;
}

void SSAORenderer::cleanup() {
  if (m_gBufferFBO != 0) {
    glDeleteFramebuffers(1, &m_gBufferFBO);
    m_gBufferFBO = 0;
  }
  if (m_gPosition != 0) {
    glDeleteTextures(1, &m_gPosition);
    m_gPosition = 0;
  }
  if (m_gNormal != 0) {
    glDeleteTextures(1, &m_gNormal);
    m_gNormal = 0;
  }
  if (m_gDepthRBO != 0) {
    glDeleteRenderbuffers(1, &m_gDepthRBO);
    m_gDepthRBO = 0;
  }
  if (m_ssaoFBO != 0) {
    glDeleteFramebuffers(1, &m_ssaoFBO);
    m_ssaoFBO = 0;
  }
  if (m_ssaoColorBuffer != 0) {
    glDeleteTextures(1, &m_ssaoColorBuffer);
    m_ssaoColorBuffer = 0;
  }
  if (m_ssaoBlurFBO != 0) {
    glDeleteFramebuffers(1, &m_ssaoBlurFBO);
    m_ssaoBlurFBO = 0;
  }
  if (m_ssaoColorBufferBlur != 0) {
    glDeleteTextures(1, &m_ssaoColorBufferBlur);
    m_ssaoColorBufferBlur = 0;
  }
  if (m_noiseTexture != 0) {
    glDeleteTextures(1, &m_noiseTexture);
    m_noiseTexture = 0;
  }
  if (m_quadVAO != 0) {
    glDeleteVertexArrays(1, &m_quadVAO);
    glDeleteBuffers(1, &m_quadVBO);
    m_quadVAO = m_quadVBO = 0;
  }
  if (m_geometryShader) {
    delete m_geometryShader;
    m_geometryShader = nullptr;
  }
  if (m_ssaoShader) {
    delete m_ssaoShader;
    m_ssaoShader = nullptr;
  }
  if (m_blurShader) {
    delete m_blurShader;
    m_blurShader = nullptr;
  }
  m_initialized = false;
}

void SSAORenderer::resize(int width, int height) {
  if (width == m_width && height == m_height)
    return;

  m_width = width;
  m_height = height;

  if (m_initialized) {
    // Delete existing framebuffers and textures
    if (m_gBufferFBO != 0) {
      glDeleteFramebuffers(1, &m_gBufferFBO);
      glDeleteTextures(1, &m_gPosition);
      glDeleteTextures(1, &m_gNormal);
      glDeleteRenderbuffers(1, &m_gDepthRBO);
    }
    if (m_ssaoFBO != 0) {
      glDeleteFramebuffers(1, &m_ssaoFBO);
      glDeleteTextures(1, &m_ssaoColorBuffer);
    }
    if (m_ssaoBlurFBO != 0) {
      glDeleteFramebuffers(1, &m_ssaoBlurFBO);
      glDeleteTextures(1, &m_ssaoColorBufferBlur);
    }

    // Recreate
    setupGBuffer();
    setupSSAOBuffers();
  }
}

bool SSAORenderer::setupGBuffer() {
  glGenFramebuffers(1, &m_gBufferFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferFBO);

  // Position color buffer (view-space, RGBA16F - w stores validity flag)
  glGenTextures(1, &m_gPosition);
  glBindTexture(GL_TEXTURE_2D, m_gPosition);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA,
               GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_gPosition, 0);

  // Normal color buffer (view-space, RGB16F)
  glGenTextures(1, &m_gNormal);
  glBindTexture(GL_TEXTURE_2D, m_gNormal);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA,
               GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         m_gNormal, 0);

  // Tell OpenGL which color attachments we'll use
  GLuint attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, attachments);

  // Depth renderbuffer
  glGenRenderbuffers(1, &m_gDepthRBO);
  glBindRenderbuffer(GL_RENDERBUFFER, m_gDepthRBO);
  glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, m_width,
                        m_height);
  glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,
                            GL_RENDERBUFFER, m_gDepthRBO);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "SSAO G-buffer framebuffer not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

bool SSAORenderer::setupSSAOBuffers() {
  // SSAO color buffer
  glGenFramebuffers(1, &m_ssaoFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);

  glGenTextures(1, &m_ssaoColorBuffer);
  glBindTexture(GL_TEXTURE_2D, m_ssaoColorBuffer);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_width, m_height, 0, GL_RED,
               GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_ssaoColorBuffer, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "SSAO framebuffer not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return false;
  }

  // SSAO blur buffer
  glGenFramebuffers(1, &m_ssaoBlurFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);

  glGenTextures(1, &m_ssaoColorBufferBlur);
  glBindTexture(GL_TEXTURE_2D, m_ssaoColorBufferBlur);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RED, m_width, m_height, 0, GL_RED,
               GL_FLOAT, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_ssaoColorBufferBlur, 0);

  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "SSAO blur framebuffer not complete!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

void SSAORenderer::generateSampleKernel() {
  std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
  std::default_random_engine generator;

  m_ssaoKernel.clear();
  for (unsigned int i = 0; i < 64; ++i) {
    // Sample in tangent space hemisphere: x,y in [-1,1], z in [0,1]
    glm::vec3 sample(randomFloats(generator) * 2.0f - 1.0f,
                     randomFloats(generator) * 2.0f - 1.0f,
                     randomFloats(generator));
    sample = glm::normalize(sample);
    sample *= randomFloats(generator);

    // Scale samples so they're more aligned to center of hemisphere
    // (accelerating interpolation to place more samples closer to the origin)
    float scale = static_cast<float>(i) / 64.0f;
    scale = lerp(0.1f, 1.0f, scale * scale);
    sample *= scale;

    m_ssaoKernel.push_back(sample);
  }
}

void SSAORenderer::generateNoiseTexture() {
  std::uniform_real_distribution<float> randomFloats(0.0f, 1.0f);
  std::default_random_engine generator;

  // Generate 4x4 noise texture of random rotation vectors around z-axis
  std::vector<glm::vec3> ssaoNoise;
  for (unsigned int i = 0; i < 16; i++) {
    glm::vec3 noise(randomFloats(generator) * 2.0f - 1.0f,
                    randomFloats(generator) * 2.0f - 1.0f,
                    0.0f); // Rotate around z-axis (in tangent space)
    ssaoNoise.push_back(noise);
  }

  glGenTextures(1, &m_noiseTexture);
  glBindTexture(GL_TEXTURE_2D, m_noiseTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, 4, 4, 0, GL_RGB, GL_FLOAT,
               &ssaoNoise[0]);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_REPEAT);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_REPEAT);
}

bool SSAORenderer::loadShaders() {
  try {
    m_geometryShader =
        Engine::loadShader("ssao/geometry.vert", "ssao/geometry.frag");
    if (!m_geometryShader) {
      std::cerr << "Failed to load SSAO geometry shader" << std::endl;
      return false;
    }

    m_ssaoShader = Engine::loadShader("ssao/ssao.vert", "ssao/ssao.frag");
    if (!m_ssaoShader) {
      std::cerr << "Failed to load SSAO shader" << std::endl;
      return false;
    }

    m_blurShader =
        Engine::loadShader("ssao/ssao_blur.vert", "ssao/ssao_blur.frag");
    if (!m_blurShader) {
      std::cerr << "Failed to load SSAO blur shader" << std::endl;
      return false;
    }

    // Set texture unit defaults
    m_ssaoShader->use();
    m_ssaoShader->setInt("gPosition", 0);
    m_ssaoShader->setInt("gNormal", 1);
    m_ssaoShader->setInt("texNoise", 2);

    m_blurShader->use();
    m_blurShader->setInt("ssaoInput", 0);

    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to load SSAO shaders: " << e.what() << std::endl;
    if (m_geometryShader) {
      delete m_geometryShader;
      m_geometryShader = nullptr;
    }
    if (m_ssaoShader) {
      delete m_ssaoShader;
      m_ssaoShader = nullptr;
    }
    if (m_blurShader) {
      delete m_blurShader;
      m_blurShader = nullptr;
    }
    return false;
  }
}

void SSAORenderer::setupQuad() {
  float quadVertices[] = {
      // positions        // texture Coords
      -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
      1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
  };

  glGenVertexArrays(1, &m_quadVAO);
  glGenBuffers(1, &m_quadVBO);
  glBindVertexArray(m_quadVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
  glBindVertexArray(0);
}

void SSAORenderer::beginGeometryPass() {
  if (!m_initialized)
    return;

  glBindFramebuffer(GL_FRAMEBUFFER, m_gBufferFBO);
  glViewport(0, 0, m_width, m_height);

  // Save current clear color, then clear G-buffer with zeros
  // (position w=0 marks empty/background pixels)
  GLfloat prevClearColor[4];
  glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);
  glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
  glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2],
               prevClearColor[3]);
}

void SSAORenderer::endGeometryPass() {
  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SSAORenderer::computeSSAO(const glm::mat4 &projection) {
  if (!m_initialized || !m_ssaoShader)
    return;

  glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoFBO);
  glViewport(0, 0, m_width, m_height);
  glClear(GL_COLOR_BUFFER_BIT);

  m_ssaoShader->use();

  // Send kernel samples
  for (unsigned int i = 0; i < 64; ++i) {
    m_ssaoShader->setVec3("samples[" + std::to_string(i) + "]",
                          m_ssaoKernel[i]);
  }
  m_ssaoShader->setMat4("projection", projection);
  m_ssaoShader->setInt("kernelSize", m_settings.kernelSize);
  m_ssaoShader->setFloat("radius", m_settings.radius);
  m_ssaoShader->setFloat("bias", m_settings.bias);
  m_ssaoShader->setFloat("power", m_settings.power);

  // Noise scale: screen dimensions / noise texture size (4x4)
  m_ssaoShader->setVec2("noiseScale",
                        glm::vec2(static_cast<float>(m_width) / 4.0f,
                                  static_cast<float>(m_height) / 4.0f));

  // Bind G-buffer textures
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_gPosition);
  glActiveTexture(GL_TEXTURE1);
  glBindTexture(GL_TEXTURE_2D, m_gNormal);
  glActiveTexture(GL_TEXTURE2);
  glBindTexture(GL_TEXTURE_2D, m_noiseTexture);

  renderQuad();

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SSAORenderer::blurSSAO() {
  if (!m_initialized || !m_blurShader)
    return;

  glBindFramebuffer(GL_FRAMEBUFFER, m_ssaoBlurFBO);
  glViewport(0, 0, m_width, m_height);
  glClear(GL_COLOR_BUFFER_BIT);

  m_blurShader->use();
  glActiveTexture(GL_TEXTURE0);
  glBindTexture(GL_TEXTURE_2D, m_ssaoColorBuffer);

  renderQuad();

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void SSAORenderer::renderQuad() {
  if (m_quadVAO == 0)
    return;

  glBindVertexArray(m_quadVAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

} // namespace Engine
