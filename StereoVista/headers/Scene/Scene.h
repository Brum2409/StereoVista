#pragma once

#include "Renderer/FrameSubmission.h"
#include "Renderer/MeshBuffer.h"

#include <glm/glm.hpp>

#include <cfloat>
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
// Interim scene host for the Vulkan migration: the render-relevant subset of
// the GL SceneManager's scene (models + point lights + camera pose), enough
// to load office.scene and imported model files for Phase 3 verification.
// The full SceneManager (undo, snapshots, save, merge, measurements, clip
// planes...) returns in later phases; this module is the seam it will fill.
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
    std::string name;
    glm::vec3 position{ 0.0f };
    glm::vec3 rotationDeg{ 0.0f }; // applied X then Y then Z (GL parity)
    glm::vec3 scale{ 1.0f };
    bool visible = true;
    float emissive = 0.0f;
    std::vector<ModelMesh> meshes;

    glm::mat4 modelMatrix() const;
    // Uniform-scale shortcut like the GL renderModels; inverse-transpose
    // otherwise.
    glm::mat3 normalMatrix(const glm::mat4& model) const;
};

struct PointLight {
    glm::vec3 position{ 0.0f };
    glm::vec3 color{ 1.0f };
    float intensity = 1.0f;
    float attenLinear = 0.09f;
    float attenQuadratic = 0.032f;
    bool castsShadows = true;
    float radius = 0.05f; // emitter world radius (PCSS penumbra width)
};

struct CameraPose {
    bool valid = false;
    glm::vec3 position{ 3.0f, 3.0f, 7.0f };
    glm::vec3 front{ 0.0f, 0.0f, -1.0f };
};

struct Scene {
    std::string sourcePath;
    std::vector<Model> models;
    std::vector<PointLight> pointLights;
    CameraPose camera;
    glm::vec3 worldBoundsMin{ -5.0f };
    glm::vec3 worldBoundsMax{ 5.0f };

    void computeWorldBounds();
};

// office.scene-style JSON: models are either procedural primitives
// (primitiveType cube/sphere/cylinder/plane/torus) or model file paths
// (loaded through the Assimp importer). Throws std::runtime_error on a
// malformed file; a missing file is the caller's problem (check first).
Scene loadSceneFile(const std::string& path, rhi::Device& device,
                    renderer::MaterialSystem& materials);

// Fallback when no scene file is found: a small primitive arrangement with
// one point light so every install shows something verifiable.
Scene createDefaultScene(rhi::Device& device, renderer::MaterialSystem& materials);

// ---- Building blocks (used by the loaders; exposed for tools/tests) ----

enum class PrimitiveType { Cube, Sphere, Cylinder, Plane, Torus };

// CPU geometry, ported verbatim from the GL ModelLoader factories.
renderer::MeshData buildPrimitive(PrimitiveType type);

// Assimp import: file -> one Model with per-mesh materials (textures loaded
// via stb into the bindless registry). Returns false if Assimp cannot load
// the file.
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
// (out.modelIndex == -1) when the point lies in empty space. Defined in
// ScenePicking.cpp.
bool pickModelAtPoint(const Scene& scene, const glm::vec3& worldPoint, RayHit& out);

} // namespace scene
