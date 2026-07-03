#include "Scene/Scene.h"

#include <glm/glm.hpp>

#include <cmath>

// Procedural primitive geometry, ported from the GL ModelLoader factories
// (createCube/createSphere/createCylinder/createPlane/createTorus). Positions,
// normals, UVs, winding and tangents match the GL reference; the explicit
// bitangent stream became the tangent.w sign (bitangent = cross(N,T) * w).

namespace scene {

namespace {

using renderer::MeshData;
using renderer::Vertex;

constexpr float kPi = 3.14159265359f;

// Recover the GL loader's explicit bitangent as a handedness sign.
float bitangentSign(const glm::vec3& normal, const glm::vec3& tangent,
                    const glm::vec3& bitangent) {
    return glm::dot(glm::cross(normal, tangent), bitangent) < 0.0f ? -1.0f : 1.0f;
}

Vertex makeVertex(glm::vec3 position, glm::vec3 normal, glm::vec2 uv,
                  glm::vec3 tangent, glm::vec3 bitangent) {
    Vertex v;
    v.position = position;
    v.normal = normal;
    v.uv = uv;
    v.tangent = glm::vec4(tangent, bitangentSign(normal, tangent, bitangent));
    return v;
}

MeshData buildCube() {
    struct Face {
        glm::vec3 normal;
        glm::vec3 corners[4];
        glm::vec2 uvs[4];
    };
    // Same 24 vertices / 36 indices as the GL createCube.
    const Face faces[6] = {
        { { 0, 0, 1 },
          { { -0.5f, -0.5f, 0.5f }, { 0.5f, -0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f }, { -0.5f, 0.5f, 0.5f } },
          { { 0, 0 }, { 1, 0 }, { 1, 1 }, { 0, 1 } } },
        { { 0, 0, -1 },
          { { -0.5f, -0.5f, -0.5f }, { -0.5f, 0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f } },
          { { 1, 0 }, { 1, 1 }, { 0, 1 }, { 0, 0 } } },
        { { 0, 1, 0 },
          { { -0.5f, 0.5f, -0.5f }, { -0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, 0.5f }, { 0.5f, 0.5f, -0.5f } },
          { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } } },
        { { 0, -1, 0 },
          { { -0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, -0.5f }, { 0.5f, -0.5f, 0.5f }, { -0.5f, -0.5f, 0.5f } },
          { { 0, 1 }, { 1, 1 }, { 1, 0 }, { 0, 0 } } },
        { { 1, 0, 0 },
          { { 0.5f, -0.5f, -0.5f }, { 0.5f, 0.5f, -0.5f }, { 0.5f, 0.5f, 0.5f }, { 0.5f, -0.5f, 0.5f } },
          { { 0, 0 }, { 0, 1 }, { 1, 1 }, { 1, 0 } } },
        { { -1, 0, 0 },
          { { -0.5f, -0.5f, -0.5f }, { -0.5f, -0.5f, 0.5f }, { -0.5f, 0.5f, 0.5f }, { -0.5f, 0.5f, -0.5f } },
          { { 1, 0 }, { 0, 0 }, { 0, 1 }, { 1, 1 } } },
    };

    MeshData data;
    for (const Face& face : faces) {
        const uint32_t base = static_cast<uint32_t>(data.vertices.size());
        for (int i = 0; i < 4; ++i)
            data.vertices.push_back(makeVertex(face.corners[i], face.normal,
                                               face.uvs[i], { 1, 0, 0 }, { 0, 1, 0 }));
        data.indices.insert(data.indices.end(),
                            { base, base + 1, base + 2, base + 2, base + 3, base });
    }
    return data;
}

MeshData buildSphere(int rings = 20, int sectors = 20) {
    MeshData data;
    const float radius = 0.5f;

    for (int r = 0; r <= rings; ++r) {
        const float theta = r * kPi / rings;
        const float sinTheta = std::sin(theta);
        const float cosTheta = std::cos(theta);
        for (int s = 0; s <= sectors; ++s) {
            const float phi = s * 2.0f * kPi / sectors;
            const float sinPhi = std::sin(phi);
            const float cosPhi = std::cos(phi);

            const glm::vec3 position(radius * sinTheta * cosPhi, radius * cosTheta,
                                     radius * sinTheta * sinPhi);
            const glm::vec3 normal = glm::normalize(position);
            const glm::vec3 tangent(-sinPhi, 0.0f, cosPhi);
            data.vertices.push_back(makeVertex(
                position, normal,
                { float(s) / sectors, float(r) / rings }, tangent,
                glm::cross(normal, tangent)));
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sectors; ++s) {
            const uint32_t current = r * (sectors + 1) + s;
            const uint32_t next = current + sectors + 1;
            data.indices.insert(data.indices.end(),
                                { current, current + 1, next,
                                  current + 1, next + 1, next });
        }
    }
    return data;
}

MeshData buildCylinder(int sectors = 20) {
    MeshData data;
    const float radius = 0.5f;
    const float height = 1.0f;

    // Side rings (bottom+top interleaved, like the GL factory).
    for (int i = 0; i <= sectors; ++i) {
        const float angle = 2.0f * kPi * i / sectors;
        const float x = radius * std::cos(angle);
        const float z = radius * std::sin(angle);
        const glm::vec3 normal = glm::normalize(glm::vec3(x, 0.0f, z));
        const glm::vec3 tangent(-std::sin(angle), 0.0f, std::cos(angle));
        data.vertices.push_back(makeVertex({ x, -height / 2, z }, normal,
                                           { float(i) / sectors, 0.0f }, tangent,
                                           { 0, 1, 0 }));
        data.vertices.push_back(makeVertex({ x, height / 2, z }, normal,
                                           { float(i) / sectors, 1.0f }, tangent,
                                           { 0, 1, 0 }));
    }

    const uint32_t bottomCapStart = static_cast<uint32_t>(data.vertices.size());
    for (int i = 0; i <= sectors; ++i) {
        const float angle = 2.0f * kPi * i / sectors;
        const float x = radius * std::cos(angle);
        const float z = radius * std::sin(angle);
        data.vertices.push_back(makeVertex(
            { x, -height / 2, z }, { 0, -1, 0 },
            { (x + radius) / (2 * radius), (z + radius) / (2 * radius) },
            { 1, 0, 0 }, { 0, 0, 1 }));
    }
    const uint32_t bottomCenter = static_cast<uint32_t>(data.vertices.size());
    data.vertices.push_back(makeVertex({ 0, -height / 2, 0 }, { 0, -1, 0 },
                                       { 0.5f, 0.5f }, { 1, 0, 0 }, { 0, 0, 1 }));

    const uint32_t topCapStart = static_cast<uint32_t>(data.vertices.size());
    for (int i = 0; i <= sectors; ++i) {
        const float angle = 2.0f * kPi * i / sectors;
        const float x = radius * std::cos(angle);
        const float z = radius * std::sin(angle);
        data.vertices.push_back(makeVertex(
            { x, height / 2, z }, { 0, 1, 0 },
            { (x + radius) / (2 * radius), (z + radius) / (2 * radius) },
            { 1, 0, 0 }, { 0, 0, 1 }));
    }
    const uint32_t topCenter = static_cast<uint32_t>(data.vertices.size());
    data.vertices.push_back(makeVertex({ 0, height / 2, 0 }, { 0, 1, 0 },
                                       { 0.5f, 0.5f }, { 1, 0, 0 }, { 0, 0, 1 }));

    for (int i = 0; i < sectors; ++i) {
        const uint32_t current = i * 2;
        const uint32_t next = ((i + 1) % (sectors + 1)) * 2;
        data.indices.insert(data.indices.end(),
                            { current, current + 1, next,
                              next, current + 1, next + 1 });
    }
    for (int i = 0; i < sectors; ++i) {
        const uint32_t currentBottom = bottomCapStart + i;
        const uint32_t nextBottom = bottomCapStart + ((i + 1) % (sectors + 1));
        const uint32_t currentTop = topCapStart + i;
        const uint32_t nextTop = topCapStart + ((i + 1) % (sectors + 1));
        data.indices.insert(data.indices.end(),
                            { bottomCenter, currentBottom, nextBottom,
                              topCenter, nextTop, currentTop });
    }
    return data;
}

MeshData buildPlane() {
    MeshData data;
    data.vertices = {
        makeVertex({ -0.5f, 0.0f, -0.5f }, { 0, 1, 0 }, { 0, 0 }, { 1, 0, 0 }, { 0, 0, 1 }),
        makeVertex({ 0.5f, 0.0f, -0.5f }, { 0, 1, 0 }, { 1, 0 }, { 1, 0, 0 }, { 0, 0, 1 }),
        makeVertex({ 0.5f, 0.0f, 0.5f }, { 0, 1, 0 }, { 1, 1 }, { 1, 0, 0 }, { 0, 0, 1 }),
        makeVertex({ -0.5f, 0.0f, 0.5f }, { 0, 1, 0 }, { 0, 1 }, { 1, 0, 0 }, { 0, 0, 1 }),
    };
    data.indices = { 0, 3, 1, 1, 3, 2 };
    return data;
}

MeshData buildTorus(int rings = 20, int sides = 20) {
    MeshData data;
    const float majorRadius = 0.4f;
    const float minorRadius = 0.1f;

    for (int r = 0; r < rings; ++r) {
        const float theta = 2.0f * kPi * r / rings;
        const float cosTheta = std::cos(theta);
        const float sinTheta = std::sin(theta);
        for (int s = 0; s < sides; ++s) {
            const float phi = 2.0f * kPi * s / sides;
            const float cosPhi = std::cos(phi);
            const float sinPhi = std::sin(phi);

            const glm::vec3 position((majorRadius + minorRadius * cosPhi) * cosTheta,
                                     minorRadius * sinPhi,
                                     (majorRadius + minorRadius * cosPhi) * sinTheta);
            const glm::vec3 center(majorRadius * cosTheta, 0.0f, majorRadius * sinTheta);
            const glm::vec3 normal = glm::normalize(position - center);
            const glm::vec3 tangent(-sinTheta, 0.0f, cosTheta);
            data.vertices.push_back(makeVertex(position, normal,
                                               { float(r) / rings, float(s) / sides },
                                               tangent, glm::cross(normal, tangent)));
        }
    }
    for (int r = 0; r < rings; ++r) {
        for (int s = 0; s < sides; ++s) {
            const uint32_t current = r * sides + s;
            const uint32_t next = ((r + 1) % rings) * sides + s;
            const uint32_t currentNext = r * sides + ((s + 1) % sides);
            const uint32_t nextNext = ((r + 1) % rings) * sides + ((s + 1) % sides);
            data.indices.insert(data.indices.end(),
                                { current, currentNext, next,
                                  currentNext, nextNext, next });
        }
    }
    return data;
}

} // namespace

renderer::MeshData buildPrimitive(PrimitiveType type) {
    switch (type) {
    case PrimitiveType::Cube: return buildCube();
    case PrimitiveType::Sphere: return buildSphere();
    case PrimitiveType::Cylinder: return buildCylinder();
    case PrimitiveType::Plane: return buildPlane();
    case PrimitiveType::Torus: return buildTorus();
    }
    return buildCube();
}

} // namespace scene
