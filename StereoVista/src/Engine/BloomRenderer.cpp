#include "Engine/BloomRenderer.h"
#include "Engine/Core.h"
#include <iostream>

namespace Engine {

BloomRenderer::BloomRenderer() : m_width(0), m_height(0) {}

BloomRenderer::~BloomRenderer() { cleanup(); }

bool BloomRenderer::initialize(int width, int height) {
  m_width = width;
  m_height = height;

  // Load shaders
  if (!loadShaders()) {
    std::cerr << "Failed to load bloom shaders" << std::endl;
    return false;
  }

  // Setup framebuffers
  if (!setupFramebuffers()) {
    std::cerr << "Failed to setup bloom framebuffers" << std::endl;
    return false;
  }

  // Setup screen quad
  setupQuad();

  m_initialized = true;
  return true;
}

void BloomRenderer::cleanup() {
  if (m_settings.hdrFBO != 0) {
    glDeleteFramebuffers(1, &m_settings.hdrFBO);
    m_settings.hdrFBO = 0;
  }

  if (m_settings.bloomFBO[0] != 0) {
    glDeleteFramebuffers(2, m_settings.bloomFBO);
    m_settings.bloomFBO[0] = m_settings.bloomFBO[1] = 0;
  }

  if (m_settings.hdrColorBuffer != 0) {
    glDeleteTextures(1, &m_settings.hdrColorBuffer);
    m_settings.hdrColorBuffer = 0;
  }

  if (m_settings.hdrBrightBuffer != 0) {
    glDeleteTextures(1, &m_settings.hdrBrightBuffer);
    m_settings.hdrBrightBuffer = 0;
  }

  if (m_settings.bloomColorBuffers[0] != 0) {
    glDeleteTextures(2, m_settings.bloomColorBuffers);
    m_settings.bloomColorBuffers[0] = m_settings.bloomColorBuffers[1] = 0;
  }

  // Schütz Phase 1: depth texture replaced the old renderbuffer
  if (m_settings.depthTexture != 0) {
    glDeleteTextures(1, &m_settings.depthTexture);
    m_settings.depthTexture = 0;
  }

  // FXAA intermediate LDR target
  if (m_settings.ldrFBO != 0) {
    glDeleteFramebuffers(1, &m_settings.ldrFBO);
    m_settings.ldrFBO = 0;
  }
  if (m_settings.ldrColorBuffer != 0) {
    glDeleteTextures(1, &m_settings.ldrColorBuffer);
    m_settings.ldrColorBuffer = 0;
  }

  if (m_settings.quadVAO != 0) {
    glDeleteVertexArrays(1, &m_settings.quadVAO);
    glDeleteBuffers(1, &m_settings.quadVBO);
    m_settings.quadVAO = m_settings.quadVBO = 0;
  }

  if (m_settings.blurShader) {
    delete m_settings.blurShader;
    m_settings.blurShader = nullptr;
  }

  if (m_settings.finalShader) {
    delete m_settings.finalShader;
    m_settings.finalShader = nullptr;
  }

  if (m_settings.fxaaShader) {
    delete m_settings.fxaaShader;
    m_settings.fxaaShader = nullptr;
  }

  m_initialized = false;
}

void BloomRenderer::resize(int width, int height) {
  if (width == m_width && height == m_height)
    return;

  m_width = width;
  m_height = height;

  if (m_initialized) {
    // Cleanup existing framebuffers
    if (m_settings.hdrFBO != 0) {
      glDeleteFramebuffers(1, &m_settings.hdrFBO);
      glDeleteFramebuffers(2, m_settings.bloomFBO);
      glDeleteTextures(1, &m_settings.hdrColorBuffer);
      glDeleteTextures(1, &m_settings.hdrBrightBuffer);
      glDeleteTextures(2, m_settings.bloomColorBuffers);
      if (m_settings.depthTexture != 0) {
        glDeleteTextures(1, &m_settings.depthTexture);
        m_settings.depthTexture = 0;
      }
      if (m_settings.ldrFBO != 0) {
        glDeleteFramebuffers(1, &m_settings.ldrFBO);
        m_settings.ldrFBO = 0;
      }
      if (m_settings.ldrColorBuffer != 0) {
        glDeleteTextures(1, &m_settings.ldrColorBuffer);
        m_settings.ldrColorBuffer = 0;
      }
    }

    // Recreate framebuffers with new size
    setupFramebuffers();
  }
}

bool BloomRenderer::setupFramebuffers() {
  // Drain any pre-existing GL errors so the per-step glGetError() checks below
  // only catch failures from this function. Without this, a stale error raised
  // by an unrelated earlier call (e.g. GL_INVALID_FRAMEBUFFER_OPERATION from a
  // draw against a momentarily-incomplete FBO) is misattributed to the texture
  // creation here, aborts setup, and leaves the HDR FBO incomplete -> the
  // "HDR Framebuffer not complete during bloom pass!" black frames.
  while (glGetError() != GL_NO_ERROR) {
  }

  // Create HDR framebuffer with two color attachments
  glGenFramebuffers(1, &m_settings.hdrFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_settings.hdrFBO);

  // Color attachment 0: HDR scene
  glGenTextures(1, &m_settings.hdrColorBuffer);
  glBindTexture(GL_TEXTURE_2D, m_settings.hdrColorBuffer);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA,
               GL_FLOAT, NULL);

  // Check for OpenGL errors
  GLenum error = glGetError();
  if (error != GL_NO_ERROR) {
    std::cerr << "OpenGL error creating HDR color buffer: " << error
              << std::endl;
    return false;
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_settings.hdrColorBuffer, 0);

  // Color attachment 1: Bright areas for bloom
  glGenTextures(1, &m_settings.hdrBrightBuffer);
  glBindTexture(GL_TEXTURE_2D, m_settings.hdrBrightBuffer);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA,
               GL_FLOAT, NULL);

  // Check for OpenGL errors
  error = glGetError();
  if (error != GL_NO_ERROR) {
    std::cerr << "OpenGL error creating HDR bright buffer: " << error
              << std::endl;
    return false;
  }

  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT1, GL_TEXTURE_2D,
                         m_settings.hdrBrightBuffer, 0);

  // Tell OpenGL which color attachments to use
  GLuint attachments[2] = {GL_COLOR_ATTACHMENT0, GL_COLOR_ATTACHMENT1};
  glDrawBuffers(2, attachments);

  // Schütz Phase 1: depth as a TEXTURE so the EDL pass can sample it.
  // GL_DEPTH32F_STENCIL8 adds a stencil channel used by the two-pass point
  // cloud resolve (pass 1 writes stencil=1 for foreground points; pass 2 uses
  // stencil test to skip ~90-95% of pixels before the fragment shader runs).
  // Existing depth reads (EDL, etc.) are unaffected: with the default
  // GL_DEPTH_STENCIL_TEXTURE_MODE (GL_DEPTH_COMPONENT), sampling the texture
  // returns the depth component exactly as before.
  glGenTextures(1, &m_settings.depthTexture);
  glBindTexture(GL_TEXTURE_2D, m_settings.depthTexture);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_DEPTH32F_STENCIL8, m_width, m_height, 0,
               GL_DEPTH_STENCIL, GL_FLOAT_32_UNSIGNED_INT_24_8_REV, nullptr);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glBindTexture(GL_TEXTURE_2D, 0);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_TEXTURE_2D,
                         m_settings.depthTexture, 0);

  // Check framebuffer completeness
  GLenum status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "HDR Framebuffer not complete! Status: ";
    switch (status) {
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
      std::cerr << "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT" << std::endl;
      break;
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
      std::cerr << "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT" << std::endl;
      break;
    case GL_FRAMEBUFFER_UNSUPPORTED:
      std::cerr << "GL_FRAMEBUFFER_UNSUPPORTED" << std::endl;
      break;
    default:
      std::cerr << "Unknown error (" << status << ")" << std::endl;
      break;
    }
    return false;
  }

  // Create ping-pong framebuffers for blur
  glGenFramebuffers(2, m_settings.bloomFBO);
  glGenTextures(2, m_settings.bloomColorBuffers);

  for (unsigned int i = 0; i < 2; i++) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_settings.bloomFBO[i]);
    glBindTexture(GL_TEXTURE_2D, m_settings.bloomColorBuffers[i]);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA16F, m_width, m_height, 0, GL_RGBA,
                 GL_FLOAT, NULL);

    // Check for OpenGL errors
    error = glGetError();
    if (error != GL_NO_ERROR) {
      std::cerr << "OpenGL error creating bloom color buffer " << i << ": "
                << error << std::endl;
      return false;
    }

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                           m_settings.bloomColorBuffers[i], 0);

    // Check framebuffer completeness
    status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
    if (status != GL_FRAMEBUFFER_COMPLETE) {
      std::cerr << "Bloom Framebuffer " << i << " not complete! Status: ";
      switch (status) {
      case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
        std::cerr << "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT" << std::endl;
        break;
      case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
        std::cerr << "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT"
                  << std::endl;
        break;
      case GL_FRAMEBUFFER_UNSUPPORTED:
        std::cerr << "GL_FRAMEBUFFER_UNSUPPORTED" << std::endl;
        break;
      default:
        std::cerr << "Unknown error (" << status << ")" << std::endl;
        break;
      }
      return false;
    }
  }

  // FXAA intermediate target: holds the tone-mapped + gamma-encoded LDR image.
  // RGBA8 is exactly what FXAA expects (non-linear, 8-bit). LINEAR filtering is
  // required because the FXAA resolve samples at sub-texel offsets.
  glGenFramebuffers(1, &m_settings.ldrFBO);
  glBindFramebuffer(GL_FRAMEBUFFER, m_settings.ldrFBO);

  glGenTextures(1, &m_settings.ldrColorBuffer);
  glBindTexture(GL_TEXTURE_2D, m_settings.ldrColorBuffer);
  glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA,
               GL_UNSIGNED_BYTE, NULL);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
  glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         m_settings.ldrColorBuffer, 0);

  status = glCheckFramebufferStatus(GL_FRAMEBUFFER);
  if (status != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "FXAA LDR Framebuffer not complete! Status: " << status
              << std::endl;
    return false;
  }

  glBindFramebuffer(GL_FRAMEBUFFER, 0);
  return true;
}

void BloomRenderer::setupQuad() {
  float quadVertices[] = {
      // positions        // texture Coords
      -1.0f, 1.0f, 0.0f, 0.0f, 1.0f, -1.0f, -1.0f, 0.0f, 0.0f, 0.0f,
      1.0f,  1.0f, 0.0f, 1.0f, 1.0f, 1.0f,  -1.0f, 0.0f, 1.0f, 0.0f,
  };

  glGenVertexArrays(1, &m_settings.quadVAO);
  glGenBuffers(1, &m_settings.quadVBO);
  glBindVertexArray(m_settings.quadVAO);
  glBindBuffer(GL_ARRAY_BUFFER, m_settings.quadVBO);
  glBufferData(GL_ARRAY_BUFFER, sizeof(quadVertices), &quadVertices,
               GL_STATIC_DRAW);
  glEnableVertexAttribArray(0);
  glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 5 * sizeof(float), (void *)0);
  glEnableVertexAttribArray(1);
  glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, 5 * sizeof(float),
                        (void *)(3 * sizeof(float)));
}

bool BloomRenderer::loadShaders() {
  try {
    // Load blur shader
    m_settings.blurShader =
        Engine::loadShader("bloom/blur.vert", "bloom/blur.frag");
    if (!m_settings.blurShader) {
      std::cerr << "Failed to load blur shader" << std::endl;
      return false;
    }

    // Load final composition shader
    m_settings.finalShader =
        Engine::loadShader("bloom/final.vert", "bloom/final.frag");
    if (!m_settings.finalShader) {
      std::cerr << "Failed to load final composition shader" << std::endl;
      if (m_settings.blurShader) {
        delete m_settings.blurShader;
        m_settings.blurShader = nullptr;
      }
      return false;
    }

    // Validate shader uniforms
    m_settings.blurShader->use();

    // Check if uniform exists before setting
    GLint imageLocation =
        glGetUniformLocation(m_settings.blurShader->getID(), "image");
    if (imageLocation == -1) {
      std::cerr << "Warning: 'image' uniform not found in blur shader"
                << std::endl;
    } else {
      m_settings.blurShader->setInt("image", 0);
    }

    m_settings.finalShader->use();

    // Check if uniforms exist before setting
    GLint hdrBufferLocation =
        glGetUniformLocation(m_settings.finalShader->getID(), "hdrBuffer");
    GLint bloomBlurLocation =
        glGetUniformLocation(m_settings.finalShader->getID(), "bloomBlur");

    if (hdrBufferLocation == -1) {
      std::cerr << "Warning: 'hdrBuffer' uniform not found in final shader"
                << std::endl;
    } else {
      m_settings.finalShader->setInt("hdrBuffer", 0);
    }

    if (bloomBlurLocation == -1) {
      std::cerr << "Warning: 'bloomBlur' uniform not found in final shader"
                << std::endl;
    } else {
      m_settings.finalShader->setInt("bloomBlur", 1);
    }

    // Initialize SSAO sampler to texture unit 2 (bound later if SSAO enabled)
    m_settings.finalShader->setInt("ssaoTexture", 2);
    m_settings.finalShader->setBool("enableSSAO", false);

    // Schütz Phase 1: EDL uniforms – texture unit 3 for depth, off by default
    m_settings.finalShader->setInt("depthTexture", 3);
    m_settings.finalShader->setBool("enableEDL", false);

    // FXAA resolve shader (reuses the fullscreen-quad vertex shader). Optional:
    // if it fails to load, FXAA is simply unavailable and the pipeline presents
    // the composite directly.
    m_settings.fxaaShader =
        Engine::loadShader("bloom/final.vert", "bloom/fxaa.frag");
    if (!m_settings.fxaaShader) {
      std::cerr << "Warning: failed to load FXAA shader; FXAA disabled"
                << std::endl;
    } else {
      m_settings.fxaaShader->use();
      m_settings.fxaaShader->setInt("screenTexture", 0);
    }

    return true;
  } catch (const std::exception &e) {
    std::cerr << "Failed to load bloom shaders: " << e.what() << std::endl;

    // Cleanup on failure
    if (m_settings.blurShader) {
      delete m_settings.blurShader;
      m_settings.blurShader = nullptr;
    }
    if (m_settings.finalShader) {
      delete m_settings.finalShader;
      m_settings.finalShader = nullptr;
    }

    return false;
  }
}

void BloomRenderer::beginBloomPass() {
  if (!m_initialized)
    return;

  // Bind HDR framebuffer for rendering
  glBindFramebuffer(GL_FRAMEBUFFER, m_settings.hdrFBO);

  // Verify framebuffer is complete
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    std::cerr << "HDR Framebuffer not complete during bloom pass!" << std::endl;
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    return;
  }

  glViewport(0, 0, m_width, m_height);

  // Clear both color attachments
  glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
}

void BloomRenderer::applyBloom(GLuint sceneTexture,
                               const BloomSettings &settings,
                               GLenum drawBuffer,
                               const EDLSettings *edlSettings,
                               float nearPlane, float farPlane,
                               int presentOffsetX, int presentOffsetY) {
  if (!m_initialized)
    return;

  // Always render with bloom pipelinecd, but set intensity to 0 if bloom is
  // disabled This ensures consistent HDR rendering regardless of bloom state
  float effectiveBloomIntensity = settings.enabled ? settings.intensity : 0.0f;

  // Disable depth testing for post-processing
  glDisable(GL_DEPTH_TEST);

  // Blur bright areas using ping-pong technique
  bool horizontal = true;
  bool first_iteration = true;

  if (m_settings.blurShader) {
    m_settings.blurShader->use();

    for (int i = 0; i < settings.blurPasses; i++) {
      glBindFramebuffer(GL_FRAMEBUFFER, m_settings.bloomFBO[horizontal]);

      // Validate framebuffer binding
      if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        std::cerr << "Bloom framebuffer not complete during blur pass " << i
                  << std::endl;
        break;
      }

      glViewport(0, 0, m_width, m_height);
      glClear(GL_COLOR_BUFFER_BIT);

      // Set horizontal/vertical blur uniform
      m_settings.blurShader->setBool("horizontal", horizontal);

      // Bind appropriate texture for this blur pass with validation
      glActiveTexture(GL_TEXTURE0);
      GLuint sourceTexture;
      if (first_iteration) {
        // First iteration uses bright buffer
        sourceTexture = m_settings.hdrBrightBuffer;
      } else {
        // Subsequent iterations use ping-pong buffers
        sourceTexture = m_settings.bloomColorBuffers[!horizontal];
      }

      glBindTexture(GL_TEXTURE_2D, sourceTexture);

      // Validate texture binding
      GLint boundTexture;
      glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
      if (boundTexture != (GLint)sourceTexture) {
        std::cerr << "Failed to bind blur source texture in pass " << i
                  << std::endl;
        break;
      }

      m_settings.blurShader->setInt("image", 0);

      renderQuad();

      // Switch to next buffer
      horizontal = !horizontal;
      if (first_iteration)
        first_iteration = false;
    }
  }

  // FXAA active when requested and the shader + LDR target are available. When
  // active, the tone-mapped composite is rendered to the offscreen LDR buffer
  // (full resolution, origin 0,0) and an FXAA pass below resolves it to the
  // screen sub-rectangle. When inactive, the composite presents directly.
  bool fxaaActive = settings.fxaaEnabled && m_settings.fxaaShader != nullptr &&
                    m_settings.ldrFBO != 0;

  // Final composition. Present into the requested sub-rectangle so the image
  // lands in the free area beside the docked GUI panels (offsets default to 0,
  // i.e. full-window present). The clear still affects the whole eye buffer so
  // the reserved strips are a clean color behind the (opaque) GUI panels.
  if (fxaaActive) {
    glBindFramebuffer(GL_FRAMEBUFFER, m_settings.ldrFBO);
    glViewport(0, 0, m_width, m_height);
  } else {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(drawBuffer); // Set the target draw buffer for stereo support
    glViewport(presentOffsetX, presentOffsetY, m_width, m_height);
  }
  glClear(GL_COLOR_BUFFER_BIT);

  if (m_settings.finalShader) {
    m_settings.finalShader->use();

    // Bind HDR scene texture with validation
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_settings.hdrColorBuffer);

    // Validate HDR texture binding
    GLint boundTexture;
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
    if (boundTexture != (GLint)m_settings.hdrColorBuffer) {
      std::cerr << "Failed to bind HDR color buffer in bloom composition"
                << std::endl;
      return;
    }

    m_settings.finalShader->setInt("hdrBuffer", 0);

    // Bind blurred bloom texture (use the last written buffer) with validation
    glActiveTexture(GL_TEXTURE1);
    GLuint bloomTexture = m_settings.bloomColorBuffers[!horizontal];
    glBindTexture(GL_TEXTURE_2D, bloomTexture);

    // Validate bloom texture binding
    glGetIntegerv(GL_TEXTURE_BINDING_2D, &boundTexture);
    if (boundTexture != (GLint)bloomTexture) {
      std::cerr << "Failed to bind bloom texture in composition" << std::endl;
      return;
    }

    m_settings.finalShader->setInt("bloomBlur", 1);

    // Bind SSAO texture if available
    m_settings.finalShader->setBool("enableSSAO", m_ssaoEnabled);
    if (m_ssaoEnabled && m_ssaoTexture != 0) {
      glActiveTexture(GL_TEXTURE2);
      glBindTexture(GL_TEXTURE_2D, m_ssaoTexture);
      m_settings.finalShader->setInt("ssaoTexture", 2);
    }

    // Schütz Phase 1: bind depth texture + EDL uniforms (texture unit 3)
    bool edlActive = edlSettings && edlSettings->enabled &&
                     m_settings.depthTexture != 0;
    m_settings.finalShader->setBool("enableEDL", edlActive);
    if (edlActive) {
      glActiveTexture(GL_TEXTURE3);
      glBindTexture(GL_TEXTURE_2D, m_settings.depthTexture);
      m_settings.finalShader->setInt("depthTexture", 3);
      m_settings.finalShader->setFloat("edlStrength", edlSettings->strength);
      m_settings.finalShader->setFloat("edlRadius",   edlSettings->radius);
      m_settings.finalShader->setFloat("edlNear", nearPlane);
      m_settings.finalShader->setFloat("edlFar",  farPlane);
      m_settings.finalShader->setVec2(
          "edlTexelSize",
          glm::vec2(1.0f / static_cast<float>(m_width),
                    1.0f / static_cast<float>(m_height)));
    }

    // Set all uniforms - always enable bloom but use effective intensity (0 if
    // disabled)
    m_settings.finalShader->setBool(
        "enableBloom", true); // Always true for consistent pipeline
    m_settings.finalShader->setFloat("bloomIntensity", effectiveBloomIntensity);
    m_settings.finalShader->setFloat("exposure", settings.exposure);
    m_settings.finalShader->setInt("toneMapOperator", settings.toneMapOperator);

    // Color grading
    m_settings.finalShader->setFloat("cgContrast", settings.contrast);
    m_settings.finalShader->setFloat("cgSaturation", settings.saturation);
    m_settings.finalShader->setBool("enableVignette", settings.vignetteEnabled);
    m_settings.finalShader->setFloat("vignetteIntensity",
                                     settings.vignetteIntensity);
    m_settings.finalShader->setFloat("vignetteRadius", settings.vignetteRadius);
    m_settings.finalShader->setFloat("vignetteSoftness",
                                     settings.vignetteSoftness);

    renderQuad();

    // Unbind textures to avoid state issues
    glActiveTexture(GL_TEXTURE3);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE2);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, 0);
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
  }

  // FXAA resolve: sample the tone-mapped LDR buffer and present the
  // anti-aliased result into the screen sub-rectangle. Runs only when the
  // composite above targeted the offscreen LDR buffer.
  if (fxaaActive && m_settings.finalShader) {
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDrawBuffer(drawBuffer); // restore stereo draw buffer for the present
    glViewport(presentOffsetX, presentOffsetY, m_width, m_height);
    glClear(GL_COLOR_BUFFER_BIT);

    m_settings.fxaaShader->use();
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, m_settings.ldrColorBuffer);
    m_settings.fxaaShader->setInt("screenTexture", 0);
    m_settings.fxaaShader->setVec2(
        "inverseScreenSize",
        glm::vec2(1.0f / static_cast<float>(m_width),
                  1.0f / static_cast<float>(m_height)));
    m_settings.fxaaShader->setFloat("fxaaSubpixel", settings.fxaaSubpixel);
    m_settings.fxaaShader->setFloat("fxaaEdgeThreshold",
                                    settings.fxaaEdgeThreshold);

    renderQuad();

    glBindTexture(GL_TEXTURE_2D, 0);
  }

  // Re-enable depth testing
  glEnable(GL_DEPTH_TEST);
}

void BloomRenderer::renderQuad() {
  if (m_settings.quadVAO == 0)
    return;

  glBindVertexArray(m_settings.quadVAO);
  glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
  glBindVertexArray(0);
}

void BloomRenderer::setSSAOTexture(GLuint ssaoTexture, bool ssaoEnabled) {
  m_ssaoTexture = ssaoTexture;
  m_ssaoEnabled = ssaoEnabled;
}

bool BloomRenderer::validateBloomSystem() const {
  if (!m_initialized) {
    std::cerr << "Bloom validation failed: System not initialized" << std::endl;
    return false;
  }

  // Check framebuffer objects
  if (m_settings.hdrFBO == 0) {
    std::cerr << "Bloom validation failed: HDR framebuffer not created"
              << std::endl;
    return false;
  }

  if (m_settings.bloomFBO[0] == 0 || m_settings.bloomFBO[1] == 0) {
    std::cerr << "Bloom validation failed: Ping-pong framebuffers not created"
              << std::endl;
    return false;
  }

  // Check textures
  if (m_settings.hdrColorBuffer == 0 || m_settings.hdrBrightBuffer == 0) {
    std::cerr << "Bloom validation failed: HDR textures not created"
              << std::endl;
    return false;
  }

  if (m_settings.bloomColorBuffers[0] == 0 ||
      m_settings.bloomColorBuffers[1] == 0) {
    std::cerr << "Bloom validation failed: Bloom textures not created"
              << std::endl;
    return false;
  }

  // Check shaders
  if (!m_settings.blurShader || !m_settings.finalShader) {
    std::cerr << "Bloom validation failed: Shaders not loaded" << std::endl;
    return false;
  }

  // Check quad VAO
  if (m_settings.quadVAO == 0) {
    std::cerr << "Bloom validation failed: Quad VAO not created" << std::endl;
    return false;
  }

  return true;
}

} // namespace Engine