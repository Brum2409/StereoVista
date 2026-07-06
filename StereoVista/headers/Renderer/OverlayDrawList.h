#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

namespace renderer {

// ============================================================================
// CPU-side geometry builder for the unified overlay renderer (Phase 6,
// playbook C.9). Cursors, tools, the gizmo and plugins append world-space
// primitives here once per frame; OverlayPass uploads and draws the list on
// the backbuffer after tonemap, depth-testing against the scene depth.
//
// This replaces BOTH GL overlay patterns (cursors' file-loaded GLSL programs
// and the tools/plugins' compileOverlayProgram + per-tool VAOs): callers no
// longer own any GPU objects, they just describe geometry.
//
// Everything becomes triangles: lines are expanded to screen-space quads in
// the vertex shader (consistent width on every GPU — no wideLines feature),
// point markers and world-size billboards are camera-facing quads. Painter's
// order = call order within a depth mode; there is no sorting here (matching
// the GL tools, which also drew in submission order).
// ============================================================================

// How a batch interacts with the scene depth buffer (reverse-Z):
//   Always     no depth test — classic always-on-top overlay (gizmo).
//   Occluded   test GREATER_OR_EQUAL: visible where not behind scene geometry
//              (the GL tools' LEQUAL pass).
//   Hidden     test LESS: draws ONLY where scene geometry covers it — the
//              x-ray ghost pass (the GL tools' GL_GREATER pass).
enum class OverlayDepth : uint8_t { Always, Occluded, Hidden };

struct OverlayVertex {      // 44 bytes, mirrored in OverlayPass vertex layout
    glm::vec3 pos;
    glm::vec3 aux;          // LINE: other endpoint; SPHERE: world normal
    glm::vec4 params;       // kind-specific (see overlay.vert)
    uint32_t rgba;          // packed R8G8B8A8 unorm
};

struct OverlayBatch {
    uint32_t kind = 0;      // SV_OVERLAY_KIND_*
    OverlayDepth depth = OverlayDepth::Always;
    bool depthWrite = false;
    // VkCullModeFlags value (0 = none). Kept as uint32_t so this header stays
    // Vulkan-free for the tools that include it.
    uint32_t cullMode = 0;
    glm::vec4 paramsA{ 0.0f }; // push-constant paramsA (SPHERE / RINGS)
    glm::vec4 colorA{ 0.0f };  // push-constant colorA (RINGS / BACKGROUND)
    glm::vec4 colorB{ 0.0f };  // push-constant colorB (RINGS / BACKGROUND)
    uint32_t firstVertex = 0;
    uint32_t vertexCount = 0;
};

class OverlayDrawList {
public:
    void clear();
    bool empty() const { return batches_.empty(); }

    const std::vector<OverlayVertex>& vertices() const { return vertices_; }
    const std::vector<OverlayBatch>& batches() const { return batches_; }

    static uint32_t packColor(const glm::vec4& c);

    // ── Primitives ──────────────────────────────────────────────────────────
    void line(const glm::vec3& a, const glm::vec3& b, const glm::vec4& color,
              float widthPx, OverlayDepth depth = OverlayDepth::Occluded);
    // points.size() >= 2; closed adds the last->first segment.
    void polyline(const glm::vec3* points, size_t count, bool closed,
                  const glm::vec4& color, float widthPx,
                  OverlayDepth depth = OverlayDepth::Occluded);
    void triangle(const glm::vec3& a, const glm::vec3& b, const glm::vec3& c,
                  const glm::vec4& color, OverlayDepth depth = OverlayDepth::Occluded);
    // xyz triples, 3 per triangle.
    void triangles(const glm::vec3* verts, size_t vertexCount, const glm::vec4& color,
                   OverlayDepth depth = OverlayDepth::Occluded);
    // Round screen-space point marker (GL MeasurementTool point sprite look).
    void marker(const glm::vec3& center, float sizePx, const glm::vec4& color,
                OverlayDepth depth = OverlayDepth::Occluded);
    // Camera-facing world-space disc, flat color (in-scene plane cursor).
    void disc(const glm::vec3& center, float worldRadius, const glm::vec4& color,
              OverlayDepth depth = OverlayDepth::Occluded, bool softEdge = false);
    // Camera-facing ring pair billboard (CursorPreview3D fragment cursor).
    // Radii/thicknesses are normalised to worldRadius.
    void rings(const glm::vec3& center, float worldRadius, const glm::vec4& ringParams,
               const glm::vec4& outerColor, const glm::vec4& innerColor,
               OverlayDepth depth = OverlayDepth::Always);
    // Full-viewport gradient backdrop (CursorPreview3D).
    void background(const glm::vec4& topColor, const glm::vec4& bottomColor);

    // Shaded sphere-cursor surface: interleaved [pos.xyz, normal.xyz] floats
    // transformed by `model` on the CPU (a cursor sphere is ~1k vertices —
    // trivial). params = (transparency, edgeSoftness, centerAlpha, isInner);
    // cullMode: VK_CULL_MODE_FRONT_BIT/BACK_BIT for the GL two-pass trick.
    void sphereMesh(const float* posNormalInterleaved, const uint32_t* indices,
                    size_t indexCount, const glm::mat4& model, const glm::vec4& color,
                    const glm::vec4& params, uint32_t cullMode, bool depthWrite,
                    OverlayDepth depth = OverlayDepth::Occluded);

private:
    OverlayBatch& batchFor(uint32_t kind, OverlayDepth depth, bool depthWrite,
                           uint32_t cullMode);

    std::vector<OverlayVertex> vertices_;
    std::vector<OverlayBatch> batches_;
};

} // namespace renderer
