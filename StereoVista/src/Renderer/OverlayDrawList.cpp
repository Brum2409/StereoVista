#include "Renderer/OverlayDrawList.h"

#include <algorithm>
#include <cstring>

namespace renderer {

namespace {

// Mirror of the SV_OVERLAY_KIND_* defines in assets/shaders_vk/overlay_types.h
// (that header is GLSL-shared and included by GpuTypes.h; the draw list keeps
// its own copy so tool code including this header stays lightweight).
constexpr uint32_t kKindFlat = 0;
constexpr uint32_t kKindLine = 1;
constexpr uint32_t kKindMarker = 2;
constexpr uint32_t kKindSphere = 3;
constexpr uint32_t kKindDisc = 4;
constexpr uint32_t kKindRings = 5;
constexpr uint32_t kKindBackground = 6;

bool sameParams(const glm::vec4& a, const glm::vec4& b) {
    return std::memcmp(&a, &b, sizeof(glm::vec4)) == 0;
}

} // namespace

void OverlayDrawList::clear() {
    vertices_.clear();
    batches_.clear();
}

uint32_t OverlayDrawList::packColor(const glm::vec4& c) {
    const auto b = [](float v) {
        return static_cast<uint32_t>(std::clamp(v, 0.0f, 1.0f) * 255.0f + 0.5f);
    };
    return b(c.r) | (b(c.g) << 8) | (b(c.b) << 16) | (b(c.a) << 24);
}

OverlayBatch& OverlayDrawList::batchFor(uint32_t kind, OverlayDepth depth,
                                        bool depthWrite, uint32_t cullMode) {
    if (!batches_.empty()) {
        OverlayBatch& last = batches_.back();
        if (last.kind == kind && last.depth == depth && last.depthWrite == depthWrite &&
            last.cullMode == cullMode && sameParams(last.paramsA, glm::vec4(0.0f)) &&
            sameParams(last.colorA, glm::vec4(0.0f)) &&
            sameParams(last.colorB, glm::vec4(0.0f)))
            return last;
    }
    OverlayBatch batch;
    batch.kind = kind;
    batch.depth = depth;
    batch.depthWrite = depthWrite;
    batch.cullMode = cullMode;
    batch.firstVertex = static_cast<uint32_t>(vertices_.size());
    batches_.push_back(batch);
    return batches_.back();
}

void OverlayDrawList::line(const glm::vec3& a, const glm::vec3& b,
                           const glm::vec4& color, float widthPx, OverlayDepth depth) {
    OverlayBatch& batch = batchFor(kKindLine, depth, false, 0);
    const uint32_t rgba = packColor(color);
    const float w = std::max(widthPx, 1.0f);
    // Two triangles: (A-,B-,B+) and (A-,B+,A+); params = (side, end, widthPx).
    const OverlayVertex quad[6] = {
        { a, b, { -1.0f, 0.0f, w, 0.0f }, rgba },
        { a, b, { -1.0f, 1.0f, w, 0.0f }, rgba },
        { a, b, { 1.0f, 1.0f, w, 0.0f }, rgba },
        { a, b, { -1.0f, 0.0f, w, 0.0f }, rgba },
        { a, b, { 1.0f, 1.0f, w, 0.0f }, rgba },
        { a, b, { 1.0f, 0.0f, w, 0.0f }, rgba },
    };
    vertices_.insert(vertices_.end(), quad, quad + 6);
    batch.vertexCount += 6;
}

void OverlayDrawList::polyline(const glm::vec3* points, size_t count, bool closed,
                               const glm::vec4& color, float widthPx,
                               OverlayDepth depth) {
    if (!points || count < 2)
        return;
    for (size_t i = 1; i < count; ++i)
        line(points[i - 1], points[i], color, widthPx, depth);
    if (closed)
        line(points[count - 1], points[0], color, widthPx, depth);
}

void OverlayDrawList::triangle(const glm::vec3& a, const glm::vec3& b,
                               const glm::vec3& c, const glm::vec4& color,
                               OverlayDepth depth) {
    const glm::vec3 verts[3] = { a, b, c };
    triangles(verts, 3, color, depth);
}

void OverlayDrawList::triangles(const glm::vec3* verts, size_t vertexCount,
                                const glm::vec4& color, OverlayDepth depth) {
    if (!verts || vertexCount < 3)
        return;
    const size_t usable = vertexCount - vertexCount % 3;
    OverlayBatch& batch = batchFor(kKindFlat, depth, false, 0);
    const uint32_t rgba = packColor(color);
    vertices_.reserve(vertices_.size() + usable);
    for (size_t i = 0; i < usable; ++i)
        vertices_.push_back({ verts[i], glm::vec3(0.0f), glm::vec4(0.0f), rgba });
    batch.vertexCount += static_cast<uint32_t>(usable);
}

void OverlayDrawList::marker(const glm::vec3& center, float sizePx,
                             const glm::vec4& color, OverlayDepth depth) {
    OverlayBatch& batch = batchFor(kKindMarker, depth, false, 0);
    const uint32_t rgba = packColor(color);
    const float s = std::max(sizePx, 1.0f);
    const glm::vec2 corners[6] = {
        { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 },
    };
    for (const glm::vec2& c : corners)
        vertices_.push_back({ center, glm::vec3(0.0f), { c.x, c.y, s, 0.0f }, rgba });
    batch.vertexCount += 6;
}

void OverlayDrawList::disc(const glm::vec3& center, float worldRadius,
                           const glm::vec4& color, OverlayDepth depth, bool softEdge) {
    // One camera-facing billboard quad; the fragment shader cuts the circle.
    // paramsA.x = 1 selects the hard-edged flat look (in-scene PlaneCursor);
    // 0 the soft/rimmed preview look. Own batch (carries push params).
    OverlayBatch batch;
    batch.kind = kKindDisc;
    batch.depth = depth;
    batch.paramsA = glm::vec4(softEdge ? 0.0f : 1.0f, 0.0f, 0.0f, 0.0f);
    batch.firstVertex = static_cast<uint32_t>(vertices_.size());
    const uint32_t rgba = packColor(color);
    const glm::vec2 corners[6] = {
        { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 },
    };
    for (const glm::vec2& c : corners)
        vertices_.push_back(
            { center, glm::vec3(0.0f), { c.x, c.y, worldRadius, 0.0f }, rgba });
    batch.vertexCount = 6;
    batches_.push_back(batch);
}

void OverlayDrawList::rings(const glm::vec3& center, float worldRadius,
                            const glm::vec4& ringParams, const glm::vec4& outerColor,
                            const glm::vec4& innerColor, OverlayDepth depth) {
    // Rings carry per-batch push params, so never merge with a previous batch.
    OverlayBatch batch;
    batch.kind = kKindRings;
    batch.depth = depth;
    batch.paramsA = ringParams;
    batch.colorA = outerColor;
    batch.colorB = innerColor;
    batch.firstVertex = static_cast<uint32_t>(vertices_.size());
    const glm::vec2 corners[6] = {
        { -1, -1 }, { 1, -1 }, { 1, 1 }, { -1, -1 }, { 1, 1 }, { -1, 1 },
    };
    for (const glm::vec2& c : corners)
        vertices_.push_back(
            { center, glm::vec3(0.0f), { c.x, c.y, worldRadius, 0.0f }, 0xFFFFFFFFu });
    batch.vertexCount = 6;
    batches_.push_back(batch);
}

void OverlayDrawList::background(const glm::vec4& topColor, const glm::vec4& bottomColor) {
    OverlayBatch batch;
    batch.kind = kKindBackground;
    batch.depth = OverlayDepth::Always;
    batch.colorA = topColor;
    batch.colorB = bottomColor;
    batch.firstVertex = static_cast<uint32_t>(vertices_.size());
    // NDC-space quad; params.xy = uv with v=1 at the TOP (NDC -1 with the
    // Vulkan downward Y).
    const OverlayVertex quad[6] = {
        { { -1, -1, 0 }, {}, { 0, 1, 0, 0 }, 0xFFFFFFFFu },
        { { 1, -1, 0 }, {}, { 1, 1, 0, 0 }, 0xFFFFFFFFu },
        { { 1, 1, 0 }, {}, { 1, 0, 0, 0 }, 0xFFFFFFFFu },
        { { -1, -1, 0 }, {}, { 0, 1, 0, 0 }, 0xFFFFFFFFu },
        { { 1, 1, 0 }, {}, { 1, 0, 0, 0 }, 0xFFFFFFFFu },
        { { -1, 1, 0 }, {}, { 0, 0, 0, 0 }, 0xFFFFFFFFu },
    };
    vertices_.insert(vertices_.end(), quad, quad + 6);
    batch.vertexCount = 6;
    batches_.push_back(batch);
}

void OverlayDrawList::sphereMesh(const float* posNormal, const uint32_t* indices,
                                 size_t indexCount, const glm::mat4& model,
                                 const glm::vec4& color, const glm::vec4& params,
                                 uint32_t cullMode, bool depthWrite, OverlayDepth depth) {
    if (!posNormal || !indices || indexCount < 3)
        return;
    // Sphere batches carry per-batch shade params — always a fresh batch.
    OverlayBatch batch;
    batch.kind = kKindSphere;
    batch.depth = depth;
    batch.depthWrite = depthWrite;
    batch.cullMode = cullMode;
    batch.paramsA = params;
    batch.firstVertex = static_cast<uint32_t>(vertices_.size());

    const glm::mat3 normalMat = glm::mat3(model); // cursors use uniform scale
    const uint32_t rgba = packColor(color);
    vertices_.reserve(vertices_.size() + indexCount);
    for (size_t i = 0; i < indexCount; ++i) {
        const float* v = posNormal + size_t(indices[i]) * 6;
        const glm::vec3 pos = glm::vec3(model * glm::vec4(v[0], v[1], v[2], 1.0f));
        const glm::vec3 normal = normalMat * glm::vec3(v[3], v[4], v[5]);
        vertices_.push_back({ pos, normal, glm::vec4(0.0f), rgba });
    }
    batch.vertexCount = static_cast<uint32_t>(indexCount - indexCount % 3);
    vertices_.resize(batch.firstVertex + batch.vertexCount);
    batches_.push_back(batch);
}

} // namespace renderer
