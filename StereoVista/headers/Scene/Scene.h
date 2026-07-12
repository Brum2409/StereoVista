#pragma once

#include "Engine/Data.h" // Engine::PointCloud / Measurement / ClipPlane
#include "Renderer/FrameSubmission.h"
#include "Renderer/MeshBuffer.h"
#include "Scene/I3SSceneLayer.h"
#include "Scene/SceneItems.h"

#include <glm/glm.hpp>

#include <cfloat>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace rhi {
class Device;
}

namespace renderer {
class MaterialSystem;
}

namespace scene {

// ============================================================================
// The scene host — since UI redesign Pass 1 the ONE home of everything loaded
// or created ("one heart", docs/UI_REDESIGN.md §7): models, point clouds,
// SLPK/I3S layers, point lights, measurements, clip planes and user groups.
// Every persisted object carries a stable uint64_t ObjectId from this scene's
// monotonic counter (contract C3); scene::SceneDocument serializes the whole
// thing as the v3 .scene format (and still loads GL-era v1/v2 files forever).
// The tools that edit measurements / clip planes (MeasurementTool,
// ClipPlaneTool) bind to the vectors here instead of owning private copies.
// ============================================================================

struct ModelMesh {
    std::string name;
    renderer::MeshBuffer buffer;
    uint32_t materialIndex = 0;
    // Local-space bounds of this mesh (pre-transform).
    glm::vec3 boundsMin{ FLT_MAX };
    glm::vec3 boundsMax{ -FLT_MAX };
};

struct Model {
    uint64_t id = 0;       // persistent ObjectId (0 = not yet assigned)
    uint64_t groupId = 0;  // owning user group (0 = none)
    std::string name;
    bool locked = false;   // outliner lock: not viewport-selectable/editable
    glm::vec3 position{ 0.0f };
    glm::vec3 rotationDeg{ 0.0f }; // applied X then Y then Z (GL parity)
    glm::vec3 scale{ 1.0f };
    bool visible = true;
    float emissive = 0.0f;
    // Rebuild/serialization source: a non-empty primitiveType means a
    // procedural primitive ("cube"/"sphere"/...); otherwise sourcePath is the
    // absolute path of the imported model file (v3 stores it scene-relative
    // with this absolute fallback).
    std::string primitiveType;
    std::string sourcePath;
    std::vector<ModelMesh> meshes;

    glm::mat4 modelMatrix() const;
    // Uniform-scale shortcut like the GL renderModels; inverse-transpose
    // otherwise.
    glm::mat3 normalMatrix(const glm::mat4& model) const;
};

struct PointLight {
    uint64_t id = 0;
    uint64_t groupId = 0;
    std::string name;      // v3: lights have display names
    bool locked = false;
    bool visible = true;   // outliner eye: an invisible light doesn't render
    glm::vec3 position{ 0.0f };
    glm::vec3 color{ 1.0f };
    float intensity = 1.0f;
    float attenLinear = 0.09f;
    float attenQuadratic = 0.032f;
    bool castsShadows = true;
    float radius = 0.05f; // emitter world radius (PCSS penumbra width)
};

// User group (v3): a named folder in the Outliner. parentId nests groups;
// objects reference their group via their own groupId. Effective visibility/
// lock of an object = its own flag AND every ancestor group's flag.
struct Group {
    uint64_t id = 0;
    std::string name;
    bool visible = true;
    bool locked = false;
    uint64_t parentId = 0; // owning group (0 = root)
};

struct CameraPose {
    bool valid = false;
    glm::vec3 position{ 3.0f, 3.0f, 7.0f };
    glm::vec3 front{ 0.0f, 0.0f, -1.0f };
};

struct Scene {
    std::string sourcePath;
    std::vector<Model> models;
    std::vector<Engine::PointCloud> pointClouds; // moved here from Application
    std::vector<PointLight> pointLights;
    // Opened SLPK/I3S layers (M0: parsed + inspectable; M1 renders them).
    // unique_ptr keeps layers pointer-stable — they own GPU residency.
    std::vector<std::unique_ptr<I3SSceneLayer>> i3sLayers;
    // Annotations & tool output. The measurement / clip-plane tools bind to
    // these vectors (Tools::*::bindStorage) so the Outliner and the scene
    // document see the same objects the tools edit.
    std::vector<Engine::Measurement> measurements;
    std::vector<Engine::ClipPlane> clipPlanes;
    std::vector<Group> groups;
    CameraPose camera;
    glm::vec3 worldBoundsMin{ -5.0f };
    glm::vec3 worldBoundsMax{ 5.0f };

    // ── Identity (contract C3) ───────────────────────────────────────────────
    // Monotonic per-scene ObjectId counter, serialized in v3. allocateId hands
    // out the next id; ensureIds sweeps every container and assigns ids to
    // objects still carrying 0 (tool-created measurements/planes, legacy
    // loads, imports) — called once per frame by the Application, it is a few
    // integer compares per object.
    uint64_t nextObjectId = 1;
    uint64_t allocateId() { return nextObjectId++; }
    void ensureIds();

    // ── Ref resolution ───────────────────────────────────────────────────────
    // Refresh ref.index from ref.id (and validate ref.sub for meshes).
    // Returns false when the object no longer exists in this scene.
    bool resolve(SceneItemRef& ref) const;
    // Convenience index lookups by id (-1 when absent).
    int modelIndexOf(uint64_t id) const;
    int pointCloudIndexOf(uint64_t id) const;
    int layerIndexOf(uint64_t id) const;
    int lightIndexOf(uint64_t id) const;
    int measurementIndexOf(uint64_t id) const;
    int clipPlaneIndexOf(uint64_t id) const;
    Group* findGroup(uint64_t id);
    const Group* findGroup(uint64_t id) const;

    // ── Effective (group-aware) flags ────────────────────────────────────────
    // True when the group chain starting at groupId is fully visible/unlocked
    // (0 or an unknown id count as visible/unlocked).
    bool groupChainVisible(uint64_t groupId) const;
    bool groupChainLocked(uint64_t groupId) const;

    void computeWorldBounds();
};

// Fallback when no scene file is found: a small primitive arrangement with
// one point light so every install shows something verifiable. (.scene file
// I/O — v1/v2/v3 load, v3 save, merge — lives in Scene/SceneDocument.h.)
Scene createDefaultScene(rhi::Device& device, renderer::MaterialSystem& materials);

// ---- Building blocks (used by the loaders; exposed for tools/tests) ----

enum class PrimitiveType { Cube, Sphere, Cylinder, Plane, Torus };

PrimitiveType primitiveTypeFromString(const std::string& name);

// CPU geometry, ported verbatim from the GL ModelLoader factories.
renderer::MeshData buildPrimitive(PrimitiveType type);

// One-mesh primitive model with a fresh material (baseColor/metallic/
// roughness/emissive). Sets Model::primitiveType so the scene document can
// rebuild it. Used by the scene loaders and Duplicate.
Model makePrimitiveModel(rhi::Device& device, renderer::MaterialSystem& materials,
                         PrimitiveType type, const std::string& name,
                         glm::vec3 color, float metallic, float roughness,
                         float emissive);

// Assimp import: file -> one Model with per-mesh materials (textures loaded
// via stb into the bindless registry). Returns false if Assimp cannot load
// the file. Sets Model::sourcePath.
bool importModelFile(const std::string& path, rhi::Device& device,
                     renderer::MaterialSystem& materials, Model& out);

// ---- Picking ----

// Result of identifying the object under a world-space surface point. meshIndex
// pins the specific sub-mesh (Assimp: a Model is the imported file, its meshes
// are the file's sub-parts) so callers can select a whole model or drill down.
struct RayHit {
    bool      hit        = false;
    glm::vec3 position   = glm::vec3(0.0f); // the queried world point
    glm::vec3 normal     = glm::vec3(0.0f, 1.0f, 0.0f);
    int       modelIndex = -1;
    int       meshIndex  = -1;
};

// Identify the model + sub-mesh whose surface passes through a world-space
// point (from the GPU depth pick). Rotation-correct AABB test in each model's
// local space — no per-triangle work, no retained CPU geometry. Returns false
// (out.modelIndex == -1) when the point lies in empty space. Skips models
// hidden by their group chain. Defined in ScenePicking.cpp.
bool pickModelAtPoint(const Scene& scene, const glm::vec3& worldPoint, RayHit& out);

} // namespace scene
