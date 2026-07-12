#include "Scene/Scene.h"

#include "RHI/Device.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/MaterialSystem.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <cmath>
#include <iostream>

// Scene host building blocks: primitive/mesh construction, the built-in
// default scene, and the scene::Scene identity/lookup/bounds methods.
// File I/O (v1/v2/v3 .scene load, v3 save) lives in SceneDocument.cpp.

namespace scene {

namespace {

// Uploads the geometry and computes the local bounds the shadow fit needs.
ModelMesh makeMesh(rhi::Device& device, const renderer::MeshData& data,
                   std::string name, uint32_t materialIndex) {
    ModelMesh mesh;
    mesh.name = std::move(name);
    mesh.materialIndex = materialIndex;
    for (const renderer::Vertex& vertex : data.vertices) {
        mesh.boundsMin = glm::min(mesh.boundsMin, vertex.position);
        mesh.boundsMax = glm::max(mesh.boundsMax, vertex.position);
    }
    mesh.buffer.create(device, data, mesh.name.c_str());
    return mesh;
}

const char* primitiveTypeName(PrimitiveType type) {
    switch (type) {
    case PrimitiveType::Sphere:   return "sphere";
    case PrimitiveType::Cylinder: return "cylinder";
    case PrimitiveType::Plane:    return "plane";
    case PrimitiveType::Torus:    return "torus";
    default:                      return "cube";
    }
}

} // namespace

PrimitiveType primitiveTypeFromString(const std::string& name) {
    if (name == "sphere") return PrimitiveType::Sphere;
    if (name == "cylinder") return PrimitiveType::Cylinder;
    if (name == "plane") return PrimitiveType::Plane;
    if (name == "torus") return PrimitiveType::Torus;
    return PrimitiveType::Cube; // GL loader's default
}

Model makePrimitiveModel(rhi::Device& device, renderer::MaterialSystem& materials,
                         PrimitiveType type, const std::string& name,
                         glm::vec3 color, float metallic, float roughness,
                         float emissive) {
    renderer::gpu::MaterialData material{};
    material.baseColor = glm::vec4(color, 1.0f);
    material.metallic = metallic;
    material.roughness = roughness;
    material.emissive = emissive;
    material.normalScale = 1.0f;
    material.albedoTexture = renderer::kInvalidTexture;
    material.normalTexture = renderer::kInvalidTexture;
    material.metallicTexture = renderer::kInvalidTexture;
    material.roughnessTexture = renderer::kInvalidTexture;
    material.aoTexture = renderer::kInvalidTexture;

    Model model;
    model.name = name;
    model.emissive = emissive;
    model.primitiveType = primitiveTypeName(type);
    model.meshes.push_back(makeMesh(device, buildPrimitive(type), name,
                                    materials.addMaterial(material)));
    return model;
}

glm::mat4 Model::modelMatrix() const {
    // GL parity (main.cpp renderModels): T * Rx * Ry * Rz * S, degrees.
    glm::mat4 m(1.0f);
    m = glm::translate(m, position);
    m = glm::rotate(m, glm::radians(rotationDeg.x), glm::vec3(1, 0, 0));
    m = glm::rotate(m, glm::radians(rotationDeg.y), glm::vec3(0, 1, 0));
    m = glm::rotate(m, glm::radians(rotationDeg.z), glm::vec3(0, 0, 1));
    m = glm::scale(m, scale);
    return m;
}

glm::mat3 Model::normalMatrix(const glm::mat4& model) const {
    // Uniform scale: rotation survives mat3 truncation and the shader
    // normalizes away the scale factor. Non-uniform needs inverse-transpose.
    if (std::abs(scale.x - scale.y) < 1e-6f && std::abs(scale.x - scale.z) < 1e-6f)
        return glm::mat3(model);
    return glm::inverseTranspose(glm::mat3(model));
}

// ── Identity ─────────────────────────────────────────────────────────────────

void Scene::ensureIds() {
    for (Model& m : models)
        if (m.id == 0) m.id = allocateId();
    for (Engine::PointCloud& pc : pointClouds)
        if (pc.id == 0) pc.id = allocateId();
    for (std::unique_ptr<I3SSceneLayer>& layer : i3sLayers)
        if (layer && layer->id == 0) layer->id = allocateId();
    for (PointLight& light : pointLights)
        if (light.id == 0) light.id = allocateId();
    for (Engine::Measurement& m : measurements)
        if (m.id == 0) m.id = allocateId();
    for (Engine::ClipPlane& p : clipPlanes)
        if (p.id == 0) p.id = allocateId();
    for (Group& g : groups)
        if (g.id == 0) g.id = allocateId();
}

// ── Lookups ──────────────────────────────────────────────────────────────────

namespace {
template <typename Vec, typename IdOf>
int indexOfId(const Vec& vec, uint64_t id, IdOf idOf) {
    if (id == 0)
        return -1;
    for (size_t i = 0; i < vec.size(); ++i)
        if (idOf(vec[i]) == id)
            return static_cast<int>(i);
    return -1;
}
} // namespace

int Scene::modelIndexOf(uint64_t id) const {
    return indexOfId(models, id, [](const Model& m) { return m.id; });
}
int Scene::pointCloudIndexOf(uint64_t id) const {
    return indexOfId(pointClouds, id,
                     [](const Engine::PointCloud& pc) { return pc.id; });
}
int Scene::layerIndexOf(uint64_t id) const {
    return indexOfId(i3sLayers, id,
                     [](const std::unique_ptr<I3SSceneLayer>& l) {
                         return l ? l->id : uint64_t(0);
                     });
}
int Scene::lightIndexOf(uint64_t id) const {
    return indexOfId(pointLights, id, [](const PointLight& l) { return l.id; });
}
int Scene::measurementIndexOf(uint64_t id) const {
    return indexOfId(measurements, id,
                     [](const Engine::Measurement& m) { return m.id; });
}
int Scene::clipPlaneIndexOf(uint64_t id) const {
    return indexOfId(clipPlanes, id,
                     [](const Engine::ClipPlane& p) { return p.id; });
}

Group* Scene::findGroup(uint64_t id) {
    if (id == 0)
        return nullptr;
    for (Group& g : groups)
        if (g.id == id)
            return &g;
    return nullptr;
}
const Group* Scene::findGroup(uint64_t id) const {
    return const_cast<Scene*>(this)->findGroup(id);
}

bool Scene::resolve(SceneItemRef& ref) const {
    using Kind = SceneItemRef::Kind;
    switch (ref.kind) {
    case Kind::Model:
        ref.index = modelIndexOf(ref.id);
        return ref.index >= 0;
    case Kind::Mesh: {
        ref.index = modelIndexOf(ref.id);
        if (ref.index < 0)
            return false;
        return ref.sub >= 0 &&
               ref.sub < static_cast<int>(models[ref.index].meshes.size());
    }
    case Kind::PointCloud:
        ref.index = pointCloudIndexOf(ref.id);
        return ref.index >= 0;
    case Kind::SceneLayer:
        ref.index = layerIndexOf(ref.id);
        return ref.index >= 0;
    case Kind::PointLight:
        ref.index = lightIndexOf(ref.id);
        return ref.index >= 0;
    case Kind::Measurement:
        ref.index = measurementIndexOf(ref.id);
        return ref.index >= 0;
    case Kind::ClipPlane:
        ref.index = clipPlaneIndexOf(ref.id);
        return ref.index >= 0;
    case Kind::Group:
        return findGroup(ref.id) != nullptr;
    case Kind::Sun:
    case Kind::Environment:
        return true; // singletons — always resolvable
    default:
        return false;
    }
}

// ── Effective (group-aware) flags ────────────────────────────────────────────

bool Scene::groupChainVisible(uint64_t groupId) const {
    int guard = 0; // corrupt files could form a parent cycle — never hang
    while (groupId != 0 && guard++ < 64) {
        const Group* g = findGroup(groupId);
        if (!g)
            return true;
        if (!g->visible)
            return false;
        groupId = g->parentId;
    }
    return true;
}

bool Scene::groupChainLocked(uint64_t groupId) const {
    int guard = 0;
    while (groupId != 0 && guard++ < 64) {
        const Group* g = findGroup(groupId);
        if (!g)
            return false;
        if (g->locked)
            return true;
        groupId = g->parentId;
    }
    return false;
}

// ── Bounds ───────────────────────────────────────────────────────────────────

void Scene::computeWorldBounds() {
    glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
    auto unionCorners = [&](const glm::mat4& m, glm::vec3 lo, glm::vec3 hi) {
        for (int corner = 0; corner < 8; ++corner) {
            const glm::vec3 local((corner & 1) ? hi.x : lo.x,
                                  (corner & 2) ? hi.y : lo.y,
                                  (corner & 4) ? hi.z : lo.z);
            const glm::vec3 world = glm::vec3(m * glm::vec4(local, 1.0f));
            minB = glm::min(minB, world);
            maxB = glm::max(maxB, world);
        }
    };

    for (const Model& model : models) {
        // Effective visibility: own flag AND the group chain (Pass 1 groups).
        if (!model.visible || !groupChainVisible(model.groupId))
            continue;
        const glm::mat4 m = model.modelMatrix();
        for (const ModelMesh& mesh : model.meshes) {
            if (mesh.boundsMin.x > mesh.boundsMax.x)
                continue; // empty mesh
            unionCorners(m, mesh.boundsMin, mesh.boundsMax);
        }
    }
    // Point clouds are scene content since Pass 1 (they live here now); the
    // GL app never counted them into the world bounds — improvement, not port.
    for (const Engine::PointCloud& pc : pointClouds) {
        if (!pc.visible || !groupChainVisible(pc.groupId) || !pc.hasBounds())
            continue;
        glm::mat4 m(1.0f);
        m = glm::translate(m, pc.position);
        m = glm::rotate(m, glm::radians(pc.rotation.x), glm::vec3(1, 0, 0));
        m = glm::rotate(m, glm::radians(pc.rotation.y), glm::vec3(0, 1, 0));
        m = glm::rotate(m, glm::radians(pc.rotation.z), glm::vec3(0, 0, 1));
        m = glm::scale(m, pc.scale);
        unionCorners(m, pc.boundsMin, pc.boundsMax);
    }
    for (const std::unique_ptr<I3SSceneLayer>& layer : i3sLayers) {
        // Inspector-only layer types draw nothing — their (possibly planetary)
        // bounds must not stretch the sun-shadow fit over the real scene.
        if (!layer || !layer->visible || layer->nodeBoxes.empty() ||
            !layer->rendersAnything() || !groupChainVisible(layer->groupId))
            continue;
        minB = glm::min(minB, layer->boundsMin);
        maxB = glm::max(maxB, layer->boundsMax);
    }
    if (minB.x <= maxB.x) {
        worldBoundsMin = minB;
        worldBoundsMax = maxB;
    }
}

// ── Default scene ────────────────────────────────────────────────────────────

Scene createDefaultScene(rhi::Device& device, renderer::MaterialSystem& materials) {
    Scene scene;
    scene.sourcePath = "<default scene>";

    Model floor = makePrimitiveModel(device, materials, PrimitiveType::Plane, "Floor",
                                     { 0.55f, 0.55f, 0.58f }, 0.0f, 0.85f, 0.0f);
    floor.scale = { 12.0f, 1.0f, 12.0f };
    scene.models.push_back(std::move(floor));

    Model cube = makePrimitiveModel(device, materials, PrimitiveType::Cube, "Cube",
                                    { 0.75f, 0.25f, 0.2f }, 0.0f, 0.45f, 0.0f);
    cube.position = { -1.6f, 0.5f, 0.0f };
    scene.models.push_back(std::move(cube));

    Model sphere = makePrimitiveModel(device, materials, PrimitiveType::Sphere,
                                      "Sphere", { 0.9f, 0.9f, 0.92f }, 1.0f, 0.15f,
                                      0.0f);
    sphere.position = { 0.0f, 0.5f, 1.2f };
    scene.models.push_back(std::move(sphere));

    Model torus = makePrimitiveModel(device, materials, PrimitiveType::Torus, "Torus",
                                     { 0.2f, 0.45f, 0.8f }, 0.0f, 0.3f, 0.0f);
    torus.position = { 1.6f, 0.5f, -0.4f };
    torus.scale = glm::vec3(2.0f);
    scene.models.push_back(std::move(torus));

    PointLight light;
    light.name = "Point light";
    light.position = { 1.5f, 3.0f, 2.0f };
    light.color = { 1.0f, 0.95f, 0.9f };
    light.intensity = 2.5f;
    scene.pointLights.push_back(light);

    scene.camera.valid = true;
    scene.camera.position = { 4.0f, 3.0f, 6.0f };
    scene.camera.front = glm::normalize(glm::vec3(-0.55f, -0.35f, -0.76f));

    scene.ensureIds();
    scene.computeWorldBounds();
    std::cout << "No scene file found - using the built-in default scene\n";
    return scene;
}

} // namespace scene
