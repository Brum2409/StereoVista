#include "Gui/CursorPreview3D.h"
#include "Engine/Shader.h"
#include "Cursors/Base/Cursor.h"
#include "Cursors/Types/SphereCursor.h"
#include "Cursors/Types/FragmentCursor.h"
#include "Cursors/Types/PlaneCursor.h"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <algorithm>
#include <cmath>
#include <iostream>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

namespace GUI {

// ───────────────────────────── embedded shaders ─────────────────────────────
namespace {

// Background gradient (full-screen, drawn directly in NDC).
const char* kGradientVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUv;
void main() {
    vUv = aPos * 0.5 + 0.5;
    gl_Position = vec4(aPos, 0.0, 1.0);
})";

const char* kGradientFS = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform vec3 uTop;
uniform vec3 uBottom;
void main() {
    vec3 c = mix(uBottom, uTop, vUv.y);
    // gentle radial vignette so the cube reads against the backdrop
    float d = distance(vUv, vec2(0.5));
    c *= 1.0 - smoothstep(0.45, 0.95, d) * 0.35;
    FragColor = vec4(c, 1.0);
})";

// Lit reference cube (simple two-light Phong with a fresnel rim).
const char* kLitVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
out vec3 vWorldPos;
out vec3 vNormal;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
uniform mat3 normalMatrix;
void main() {
    vec4 wp = model * vec4(aPos, 1.0);
    vWorldPos = wp.xyz;
    vNormal = normalMatrix * aNormal;
    gl_Position = projection * view * wp;
})";

const char* kLitFS = R"(#version 330 core
in vec3 vWorldPos;
in vec3 vNormal;
out vec4 FragColor;
uniform vec3 uViewPos;
uniform vec3 uBaseColor;
void main() {
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uViewPos - vWorldPos);
    vec3 L1 = normalize(vec3(0.6, 0.85, 0.55));
    vec3 L2 = normalize(vec3(-0.55, 0.25, 0.45));
    float d1 = max(dot(N, L1), 0.0);
    float d2 = max(dot(N, L2), 0.0) * 0.4;
    vec3 H = normalize(L1 + V);
    float spec = pow(max(dot(N, H), 0.0), 48.0) * 0.25;
    float fres = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.22;
    vec3 color = uBaseColor * (0.28 + d1 + d2) + vec3(spec) + vec3(fres);
    FragColor = vec4(color, 1.0);
})";

// Sphere cursor — identical maths to assets/shaders/cursors/sphere*.glsl so the
// preview matches the in-scene cursor exactly.
const char* kSphereVS = R"(#version 330 core
layout(location = 0) in vec3 aPos;
layout(location = 1) in vec3 aNormal;
out vec3 Normal;
out vec3 FragPos;
out vec3 ModelPos;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    ModelPos = aPos;
    FragPos = vec3(model * vec4(aPos, 1.0));
    Normal = mat3(transpose(inverse(model))) * aNormal;
    gl_Position = projection * view * model * vec4(aPos, 1.0);
})";

const char* kSphereFS = R"(#version 330 core
out vec4 FragColor;
in vec3 Normal;
in vec3 FragPos;
in vec3 ModelPos;
uniform vec3 viewPos;
uniform vec4 sphereColor;
uniform float transparency;
uniform float centerTransparencyFactor;
uniform float edgeSoftness;
uniform bool isInnerSphere;
uniform float innerSphereFactor;
void main() {
    vec3 norm = normalize(Normal);
    vec3 viewDir = normalize(viewPos - FragPos);
    float distFromCenter = length(ModelPos);
    float radius = isInnerSphere ? innerSphereFactor : 1.0;
    vec3 finalColor = sphereColor.rgb;
    float alpha = sphereColor.a * transparency;
    if (!isInnerSphere) {
        float edgeFactor = pow(1.0 - abs(dot(viewDir, norm)), edgeSoftness);
        float centerFactor = smoothstep(0.0, 1.0, distFromCenter / radius);
        float centerAlpha = mix(centerTransparencyFactor, 1.0, centerFactor);
        alpha *= mix(centerAlpha, 1.0, edgeFactor);
    }
    float rimFactor = 1.0 - max(0.0, dot(viewDir, norm));
    finalColor += sphereColor.rgb * pow(rimFactor, 3.0) * 0.3;
    FragColor = vec4(finalColor, alpha);
})";

// Camera-facing billboard used by both the plane disc and the fragment rings.
const char* kBillboardVS = R"(#version 330 core
layout(location = 0) in vec2 aPos;
out vec2 vUv;
uniform mat4 model;
uniform mat4 view;
uniform mat4 projection;
void main() {
    vUv = aPos;
    gl_Position = projection * view * model * vec4(aPos, 0.0, 1.0);
})";

// Plane cursor — a filled, soft-edged disc with a brighter rim. Output is
// premultiplied alpha (blend func GL_ONE, GL_ONE_MINUS_SRC_ALPHA).
const char* kDiscFS = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform vec4 uColor;
void main() {
    float r = length(vUv);
    float aa = max(fwidth(r), 1e-4);
    float disc = 1.0 - smoothstep(1.0 - aa * 1.5, 1.0, r);
    if (disc <= 0.001) discard;
    float rim = smoothstep(0.80, 0.96, r) * (1.0 - smoothstep(0.985, 1.0, r));
    vec3 rgb = uColor.rgb + rim * 0.55;
    float a = uColor.a * disc;
    FragColor = vec4(rgb * a, a);
})";

// Fragment cursor — two concentric rings (outer ring + inner dot/ring) matching
// the in-scene screen-space cursor. Output is premultiplied alpha.
const char* kRingFS = R"(#version 330 core
in vec2 vUv;
out vec4 FragColor;
uniform float uOuterR;
uniform float uOuterT;
uniform float uInnerR;
uniform float uInnerT;
uniform vec4 uOuterColor;
uniform vec4 uInnerColor;
float band(float r, float outer, float thick, float aa) {
    float t = max(thick, aa);            // keep a hairline visible at thickness 0
    float lo = outer - t;
    return smoothstep(lo - aa, lo, r) - smoothstep(outer - aa, outer, r);
}
void main() {
    float r = length(vUv);
    float aa = max(fwidth(r), 1e-4);
    float ob = clamp(band(r, uOuterR, uOuterT, aa), 0.0, 1.0);
    float ib = clamp(band(r, uInnerR, uInnerT, aa), 0.0, 1.0);
    float oa = ob * uOuterColor.a;
    float ia = ib * uInnerColor.a;
    // composite inner under outer (premultiplied "over")
    vec3 rgb = uInnerColor.rgb * ia;
    float a = ia;
    rgb = uOuterColor.rgb * oa + rgb * (1.0 - oa);
    a = oa + a * (1.0 - oa);
    if (a <= 0.001) discard;
    FragColor = vec4(rgb, a);
})";

GLuint compileStage(GLenum type, const char* src, const char* label) {
    GLuint shader = glCreateShader(type);
    glShaderSource(shader, 1, &src, nullptr);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetShaderInfoLog(shader, sizeof(log), nullptr, log);
        std::cerr << "[CursorPreview3D] " << label << " compile error: " << log << std::endl;
    }
    return shader;
}

// Compiles a vertex+fragment program from source and wraps it in an
// Engine::Shader (which owns/destroys the GL program). Returns nullptr on error.
Engine::Shader* buildProgram(const char* vs, const char* fs, const char* label) {
    GLuint v = compileStage(GL_VERTEX_SHADER, vs, label);
    GLuint f = compileStage(GL_FRAGMENT_SHADER, fs, label);
    GLuint program = glCreateProgram();
    glAttachShader(program, v);
    glAttachShader(program, f);
    glLinkProgram(program);
    glDeleteShader(v);
    glDeleteShader(f);
    GLint ok = GL_FALSE;
    glGetProgramiv(program, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[1024];
        glGetProgramInfoLog(program, sizeof(log), nullptr, log);
        std::cerr << "[CursorPreview3D] " << label << " link error: " << log << std::endl;
        glDeleteProgram(program);
        return nullptr;
    }
    return new Engine::Shader(program);
}

// Reference-cube placement.
constexpr float kCubeScale = 1.0f;            // edge length in world units
constexpr float kCubeHalf = kCubeScale * 0.5f; // half extent

} // namespace

// ───────────────────────────── lifecycle ────────────────────────────────────

CursorPreview3D::CursorPreview3D() :
    m_msaaFBO(0), m_msaaColorRBO(0), m_msaaDepthRBO(0),
    m_resolveFBO(0), m_colorTexture(0), m_samples(4),
    m_cubeVAO(0), m_cubeVBO(0), m_cubeEBO(0),
    m_sphereVAO(0), m_sphereVBO(0), m_sphereEBO(0),
    m_quadVAO(0), m_quadVBO(0),
    m_cubeIndexCount(0), m_sphereIndexCount(0),
    m_width(256), m_height(256),
    m_rotationX(22.0f), m_rotationY(38.0f),
    m_cameraPosition(0.0f, 0.0f, 3.6f),
    m_litShader(nullptr), m_sphereShader(nullptr),
    m_discShader(nullptr), m_ringShader(nullptr), m_gradientShader(nullptr),
    m_initialized(false)
{
}

CursorPreview3D::~CursorPreview3D() {
    cleanup();
}

void CursorPreview3D::initialize(int width, int height) {
    if (m_initialized) {
        cleanup();
    }

    m_width = width;
    m_height = height;

    generateCubeGeometry();
    generateSphereGeometry();
    generateQuadGeometry();
    setupFramebuffer();
    setupCamera();

    m_initialized = compileShaders();
    if (!m_initialized) {
        std::cerr << "[CursorPreview3D] shader setup failed; preview disabled." << std::endl;
    }
}

bool CursorPreview3D::compileShaders() {
    m_gradientShader = buildProgram(kGradientVS, kGradientFS, "gradient");
    m_litShader      = buildProgram(kLitVS, kLitFS, "lit-cube");
    m_sphereShader   = buildProgram(kSphereVS, kSphereFS, "sphere-cursor");
    m_discShader     = buildProgram(kBillboardVS, kDiscFS, "plane-cursor");
    m_ringShader     = buildProgram(kBillboardVS, kRingFS, "fragment-cursor");

    return m_gradientShader && m_litShader && m_sphereShader &&
           m_discShader && m_ringShader;
}

// ───────────────────────────── geometry ─────────────────────────────────────

void CursorPreview3D::generateCubeGeometry() {
    // Position (3) + normal (3) per vertex, one set of 4 verts per face.
    const float v[] = {
        // Front (+Z)
        -0.5f, -0.5f,  0.5f,  0, 0, 1,   0.5f, -0.5f,  0.5f,  0, 0, 1,
         0.5f,  0.5f,  0.5f,  0, 0, 1,  -0.5f,  0.5f,  0.5f,  0, 0, 1,
        // Back (-Z)
         0.5f, -0.5f, -0.5f,  0, 0,-1,  -0.5f, -0.5f, -0.5f,  0, 0,-1,
        -0.5f,  0.5f, -0.5f,  0, 0,-1,   0.5f,  0.5f, -0.5f,  0, 0,-1,
        // Top (+Y)
        -0.5f,  0.5f,  0.5f,  0, 1, 0,   0.5f,  0.5f,  0.5f,  0, 1, 0,
         0.5f,  0.5f, -0.5f,  0, 1, 0,  -0.5f,  0.5f, -0.5f,  0, 1, 0,
        // Bottom (-Y)
        -0.5f, -0.5f, -0.5f,  0,-1, 0,   0.5f, -0.5f, -0.5f,  0,-1, 0,
         0.5f, -0.5f,  0.5f,  0,-1, 0,  -0.5f, -0.5f,  0.5f,  0,-1, 0,
        // Right (+X)
         0.5f, -0.5f,  0.5f,  1, 0, 0,   0.5f, -0.5f, -0.5f,  1, 0, 0,
         0.5f,  0.5f, -0.5f,  1, 0, 0,   0.5f,  0.5f,  0.5f,  1, 0, 0,
        // Left (-X)
        -0.5f, -0.5f, -0.5f, -1, 0, 0,  -0.5f, -0.5f,  0.5f, -1, 0, 0,
        -0.5f,  0.5f,  0.5f, -1, 0, 0,  -0.5f,  0.5f, -0.5f, -1, 0, 0,
    };

    std::vector<unsigned int> idx;
    idx.reserve(36);
    for (unsigned int f = 0; f < 6; ++f) {
        unsigned int b = f * 4;
        idx.insert(idx.end(), { b, b + 1, b + 2, b + 2, b + 3, b });
    }
    m_cubeIndexCount = static_cast<GLsizei>(idx.size());

    glGenVertexArrays(1, &m_cubeVAO);
    glGenBuffers(1, &m_cubeVBO);
    glGenBuffers(1, &m_cubeEBO);

    glBindVertexArray(m_cubeVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_cubeVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(v), v, GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_cubeEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

void CursorPreview3D::generateSphereGeometry() {
    std::vector<float> verts;
    std::vector<unsigned int> idx;

    const unsigned int rings = 32;
    const unsigned int sectors = 32;

    for (unsigned int i = 0; i <= rings; ++i) {
        float phi = static_cast<float>(M_PI) * static_cast<float>(i) / static_cast<float>(rings);
        float y = std::cos(phi);
        float ringRadius = std::sin(phi);
        for (unsigned int j = 0; j <= sectors; ++j) {
            float theta = 2.0f * static_cast<float>(M_PI) * static_cast<float>(j) / static_cast<float>(sectors);
            float x = ringRadius * std::cos(theta);
            float z = ringRadius * std::sin(theta);
            verts.insert(verts.end(), { x, y, z, x, y, z }); // pos == normal (unit sphere)
        }
    }
    for (unsigned int i = 0; i < rings; ++i) {
        for (unsigned int j = 0; j < sectors; ++j) {
            unsigned int cur = i * (sectors + 1) + j;
            unsigned int nxt = cur + sectors + 1;
            idx.insert(idx.end(), { cur, nxt, cur + 1, cur + 1, nxt, nxt + 1 });
        }
    }
    m_sphereIndexCount = static_cast<GLsizei>(idx.size());

    glGenVertexArrays(1, &m_sphereVAO);
    glGenBuffers(1, &m_sphereVBO);
    glGenBuffers(1, &m_sphereEBO);

    glBindVertexArray(m_sphereVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_sphereVBO);
    glBufferData(GL_ARRAY_BUFFER, verts.size() * sizeof(float), verts.data(), GL_STATIC_DRAW);
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, m_sphereEBO);
    glBufferData(GL_ELEMENT_ARRAY_BUFFER, idx.size() * sizeof(unsigned int), idx.data(), GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 3, GL_FLOAT, GL_FALSE, 6 * sizeof(float), (void*)(3 * sizeof(float)));
    glBindVertexArray(0);
}

void CursorPreview3D::generateQuadGeometry() {
    // Unit quad in the XY plane spanning [-1, 1]; reused for the background
    // gradient and the camera-facing billboards.
    const float q[] = {
        -1.0f, -1.0f,   1.0f, -1.0f,   1.0f,  1.0f,
        -1.0f, -1.0f,   1.0f,  1.0f,  -1.0f,  1.0f,
    };
    glGenVertexArrays(1, &m_quadVAO);
    glGenBuffers(1, &m_quadVBO);
    glBindVertexArray(m_quadVAO);
    glBindBuffer(GL_ARRAY_BUFFER, m_quadVBO);
    glBufferData(GL_ARRAY_BUFFER, sizeof(q), q, GL_STATIC_DRAW);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0);
    glBindVertexArray(0);
}

void CursorPreview3D::setupFramebuffer() {
    // Clamp the sample count to what the driver actually supports.
    GLint maxSamples = 1;
    glGetIntegerv(GL_MAX_SAMPLES, &maxSamples);
    m_samples = std::max(1, std::min(m_samples, maxSamples));

    // Multisampled render target.
    glGenFramebuffers(1, &m_msaaFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFBO);

    glGenRenderbuffers(1, &m_msaaColorRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaColorRBO);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_RGBA8, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, m_msaaColorRBO);

    glGenRenderbuffers(1, &m_msaaDepthRBO);
    glBindRenderbuffer(GL_RENDERBUFFER, m_msaaDepthRBO);
    glRenderbufferStorageMultisample(GL_RENDERBUFFER, m_samples, GL_DEPTH_COMPONENT24, m_width, m_height);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT, GL_RENDERBUFFER, m_msaaDepthRBO);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[CursorPreview3D] multisample framebuffer incomplete." << std::endl;

    // Single-sample resolve target sampled by ImGui.
    glGenFramebuffers(1, &m_resolveFBO);
    glBindFramebuffer(GL_FRAMEBUFFER, m_resolveFBO);

    glGenTextures(1, &m_colorTexture);
    glBindTexture(GL_TEXTURE_2D, m_colorTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_width, m_height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_colorTexture, 0);

    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
        std::cerr << "[CursorPreview3D] resolve framebuffer incomplete." << std::endl;

    glBindFramebuffer(GL_FRAMEBUFFER, 0);
}

void CursorPreview3D::setupCamera() {
    m_projectionMatrix = glm::perspective(glm::radians(38.0f),
                                          (float)m_width / (float)m_height, 0.1f, 100.0f);
    m_viewMatrix = glm::lookAt(m_cameraPosition, glm::vec3(0.0f),
                               glm::vec3(0.0f, 1.0f, 0.0f));
}

void CursorPreview3D::updateRotation(float deltaX, float deltaY) {
    m_rotationY += deltaX * 0.5f;
    m_rotationX += deltaY * 0.5f;
    m_rotationX = glm::clamp(m_rotationX, -89.0f, 89.0f);
}

// ───────────────────────────── rendering ────────────────────────────────────

void CursorPreview3D::render(Cursor::BaseCursor* cursor) {
    if (!m_initialized || !cursor) return;

    // Save the caller's framebuffer / viewport so we leave the scene untouched.
    GLint prevViewport[4];
    glGetIntegerv(GL_VIEWPORT, prevViewport);
    GLint prevFBO = 0;
    glGetIntegerv(GL_FRAMEBUFFER_BINDING, &prevFBO);

    glBindFramebuffer(GL_FRAMEBUFFER, m_msaaFBO);
    glViewport(0, 0, m_width, m_height);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glm::mat4 modelRotation(1.0f);
    modelRotation = glm::rotate(modelRotation, glm::radians(m_rotationX), glm::vec3(1, 0, 0));
    modelRotation = glm::rotate(modelRotation, glm::radians(m_rotationY), glm::vec3(0, 1, 0));

    drawBackground();
    drawReferenceCube(modelRotation);

    if (cursor->isVisible()) {
        if (auto* s = dynamic_cast<Cursor::SphereCursor*>(cursor)) {
            drawSphereCursor(s, modelRotation);
        } else if (auto* fcur = dynamic_cast<Cursor::FragmentCursor*>(cursor)) {
            drawFragmentCursor(fcur);
        } else if (auto* p = dynamic_cast<Cursor::PlaneCursor*>(cursor)) {
            drawPlaneCursor(p);
        }
    }

    // Resolve MSAA into the texture ImGui samples.
    glBindFramebuffer(GL_READ_FRAMEBUFFER, m_msaaFBO);
    glBindFramebuffer(GL_DRAW_FRAMEBUFFER, m_resolveFBO);
    glBlitFramebuffer(0, 0, m_width, m_height, 0, 0, m_width, m_height,
                      GL_COLOR_BUFFER_BIT, GL_LINEAR);

    // Restore caller state and a neutral default render state.
    glBindFramebuffer(GL_FRAMEBUFFER, prevFBO);
    glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
    glDisable(GL_BLEND);
    glDisable(GL_CULL_FACE);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glUseProgram(0);
    glBindVertexArray(0);
}

void CursorPreview3D::drawBackground() {
    if (!m_gradientShader) return;
    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glDisable(GL_BLEND);
    m_gradientShader->use();
    m_gradientShader->setVec3("uTop", glm::vec3(0.085f, 0.095f, 0.115f));
    m_gradientShader->setVec3("uBottom", glm::vec3(0.16f, 0.17f, 0.20f));
    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void CursorPreview3D::drawReferenceCube(const glm::mat4& modelRotation) {
    if (!m_litShader || m_cubeVAO == 0) return;

    glEnable(GL_DEPTH_TEST);
    glDepthMask(GL_TRUE);
    glEnable(GL_CULL_FACE);
    glCullFace(GL_BACK);
    glDisable(GL_BLEND);

    glm::mat4 model = modelRotation * glm::scale(glm::mat4(1.0f), glm::vec3(kCubeScale));
    glm::mat3 normalMatrix = glm::mat3(glm::transpose(glm::inverse(model)));

    m_litShader->use();
    m_litShader->setMat4("model", model);
    m_litShader->setMat4("view", m_viewMatrix);
    m_litShader->setMat4("projection", m_projectionMatrix);
    m_litShader->setMat3("normalMatrix", normalMatrix);
    m_litShader->setVec3("uViewPos", m_cameraPosition);
    m_litShader->setVec3("uBaseColor", glm::vec3(0.50f, 0.53f, 0.60f));

    glBindVertexArray(m_cubeVAO);
    glDrawElements(GL_TRIANGLES, m_cubeIndexCount, GL_UNSIGNED_INT, 0);
    glBindVertexArray(0);
}

void CursorPreview3D::drawSphereCursor(Cursor::SphereCursor* cursor, const glm::mat4& modelRotation) {
    if (!m_sphereShader || m_sphereVAO == 0) return;

    // Map the cursor's base size to a representative preview radius that always
    // stays nicely framed while still reacting to the size setting.
    float baseSize = std::max(cursor->getBaseSize(), 1e-4f);
    float radius = glm::clamp(0.28f * (0.5f + 0.5f * (baseSize / 0.05f)), 0.15f, 0.40f);

    // Anchor the sphere on the +Z face of the cube so it half-embeds in the
    // surface, exactly as it would sit on geometry in the scene.
    glm::vec3 anchorModel(kCubeHalf * 0.4f, kCubeHalf * 0.3f, kCubeHalf);
    glm::vec3 anchorWorld = glm::vec3(modelRotation * glm::vec4(anchorModel, 1.0f));

    glm::mat4 model = glm::translate(glm::mat4(1.0f), anchorWorld);
    model = glm::scale(model, glm::vec3(radius));

    glEnable(GL_BLEND);
    // Blend RGB normally but keep the framebuffer alpha at 1 so the resolved
    // preview texture stays fully opaque for ImGui.
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);
    glEnable(GL_DEPTH_TEST);
    glEnable(GL_CULL_FACE);

    m_sphereShader->use();
    m_sphereShader->setMat4("view", m_viewMatrix);
    m_sphereShader->setMat4("projection", m_projectionMatrix);
    m_sphereShader->setVec3("viewPos", m_cameraPosition);

    bool showInner = cursor->getShowInnerSphere();
    float innerFactor = cursor->getInnerSphereFactor();
    glm::vec4 color = cursor->getColor();
    glm::vec4 innerColor = cursor->getInnerSphereColor();
    float transparency = cursor->getTransparency();
    float edgeSoftness = cursor->getEdgeSoftness();
    float centerTransparency = cursor->getCenterTransparency();

    m_sphereShader->setFloat("innerSphereFactor", innerFactor);
    glm::mat4 innerModel = glm::scale(model, glm::vec3(innerFactor));

    glBindVertexArray(m_sphereVAO);

    // Two passes (back faces with depth write, then front faces) – matches the
    // in-scene SphereCursor so transparency layering looks identical.
    for (int pass = 0; pass < 2; ++pass) {
        if (pass == 0) { glDepthMask(GL_TRUE);  glCullFace(GL_FRONT); }
        else           { glDepthMask(GL_FALSE); glCullFace(GL_BACK);  }

        if (showInner) {
            m_sphereShader->setBool("isInnerSphere", true);
            m_sphereShader->setVec4("sphereColor", innerColor);
            m_sphereShader->setFloat("transparency", 1.0f);
            m_sphereShader->setMat4("model", innerModel);
            glDrawElements(GL_TRIANGLES, m_sphereIndexCount, GL_UNSIGNED_INT, 0);
        }

        m_sphereShader->setBool("isInnerSphere", false);
        m_sphereShader->setVec4("sphereColor", color);
        m_sphereShader->setFloat("transparency", transparency);
        m_sphereShader->setFloat("edgeSoftness", edgeSoftness);
        m_sphereShader->setFloat("centerTransparencyFactor", centerTransparency);
        m_sphereShader->setMat4("model", model);
        glDrawElements(GL_TRIANGLES, m_sphereIndexCount, GL_UNSIGNED_INT, 0);
    }

    glBindVertexArray(0);
    glDepthMask(GL_TRUE);
    glCullFace(GL_BACK);
}

void CursorPreview3D::drawPlaneCursor(Cursor::PlaneCursor* cursor) {
    if (!m_discShader || m_quadVAO == 0) return;

    // The plane cursor billboards to face the camera and lies over the surface,
    // so draw it centered, facing the camera, on top of the cube.
    float diameter = cursor->getDiameter();
    float radius = glm::clamp(0.12f + diameter * 0.09f, 0.12f, 0.55f);

    glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(radius));

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    // Premultiplied output; keep destination alpha at 1 for an opaque preview.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_discShader->use();
    m_discShader->setMat4("model", model);
    m_discShader->setMat4("view", m_viewMatrix);
    m_discShader->setMat4("projection", m_projectionMatrix);
    m_discShader->setVec4("uColor", cursor->getColor());

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

void CursorPreview3D::drawFragmentCursor(Cursor::FragmentCursor* cursor) {
    if (!m_ringShader || m_quadVAO == 0) return;

    const GUI::FragmentShaderCursorSettings& s = cursor->getSettings();

    // The fragment cursor is a screen-space ring pair. Normalise its radii by the
    // slider's maximum outer radius (0.3) so the largest setting fills the
    // billboard, preserving the relative proportions the user configures.
    constexpr float kRefMax = 0.30f;
    constexpr float kBillboardRadius = 0.55f;

    glm::mat4 model = glm::scale(glm::mat4(1.0f), glm::vec3(kBillboardRadius));

    glDisable(GL_DEPTH_TEST);
    glDepthMask(GL_FALSE);
    glEnable(GL_BLEND);
    // Premultiplied output; keep destination alpha at 1 for an opaque preview.
    glBlendFuncSeparate(GL_ONE, GL_ONE_MINUS_SRC_ALPHA, GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    m_ringShader->use();
    m_ringShader->setMat4("model", model);
    m_ringShader->setMat4("view", m_viewMatrix);
    m_ringShader->setMat4("projection", m_projectionMatrix);
    m_ringShader->setFloat("uOuterR", glm::clamp(s.baseOuterRadius / kRefMax, 0.0f, 1.0f));
    m_ringShader->setFloat("uOuterT", glm::clamp(s.baseOuterBorderThickness / kRefMax, 0.0f, 1.0f));
    m_ringShader->setFloat("uInnerR", glm::clamp(s.baseInnerRadius / kRefMax, 0.0f, 1.0f));
    m_ringShader->setFloat("uInnerT", glm::clamp(s.baseInnerBorderThickness / kRefMax, 0.0f, 1.0f));
    m_ringShader->setVec4("uOuterColor", s.outerColor);
    m_ringShader->setVec4("uInnerColor", s.innerColor);

    glBindVertexArray(m_quadVAO);
    glDrawArrays(GL_TRIANGLES, 0, 6);
    glBindVertexArray(0);

    glDisable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDepthMask(GL_TRUE);
    glEnable(GL_DEPTH_TEST);
}

// ───────────────────────────── teardown ─────────────────────────────────────

void CursorPreview3D::cleanup() {
    if (m_msaaColorRBO) { glDeleteRenderbuffers(1, &m_msaaColorRBO); m_msaaColorRBO = 0; }
    if (m_msaaDepthRBO) { glDeleteRenderbuffers(1, &m_msaaDepthRBO); m_msaaDepthRBO = 0; }
    if (m_msaaFBO)      { glDeleteFramebuffers(1, &m_msaaFBO); m_msaaFBO = 0; }
    if (m_resolveFBO)   { glDeleteFramebuffers(1, &m_resolveFBO); m_resolveFBO = 0; }
    if (m_colorTexture) { glDeleteTextures(1, &m_colorTexture); m_colorTexture = 0; }

    if (m_cubeVAO)   { glDeleteVertexArrays(1, &m_cubeVAO); m_cubeVAO = 0; }
    if (m_cubeVBO)   { glDeleteBuffers(1, &m_cubeVBO); m_cubeVBO = 0; }
    if (m_cubeEBO)   { glDeleteBuffers(1, &m_cubeEBO); m_cubeEBO = 0; }
    if (m_sphereVAO) { glDeleteVertexArrays(1, &m_sphereVAO); m_sphereVAO = 0; }
    if (m_sphereVBO) { glDeleteBuffers(1, &m_sphereVBO); m_sphereVBO = 0; }
    if (m_sphereEBO) { glDeleteBuffers(1, &m_sphereEBO); m_sphereEBO = 0; }
    if (m_quadVAO)   { glDeleteVertexArrays(1, &m_quadVAO); m_quadVAO = 0; }
    if (m_quadVBO)   { glDeleteBuffers(1, &m_quadVBO); m_quadVBO = 0; }

    delete m_litShader;      m_litShader = nullptr;
    delete m_sphereShader;   m_sphereShader = nullptr;
    delete m_discShader;     m_discShader = nullptr;
    delete m_ringShader;     m_ringShader = nullptr;
    delete m_gradientShader; m_gradientShader = nullptr;

    m_initialized = false;
}

} // namespace GUI
