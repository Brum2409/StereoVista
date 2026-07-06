#include "Scene/Scene.h"

#include <glm/glm.hpp>

#include <cfloat>

// ============================================================================
// Object picking (Vulkan rewrite). The GL era's "simple first solution" was a
// per-triangle ray cast against retained CPU geometry — O(triangles) and a
// full second copy of every mesh in RAM. We do it better here:
//
//   The async GPU depth readback already hands us the EXACT world-space surface
//   point under the cursor (the same one-frame-late, stall-free path the 3D
//   cursor rides). Given that point, identifying the object is a trivial,
//   rotation-correct AABB test in each model's local space — O(models·meshes),
//   no triangles, no retained geometry, no BVH, and no extra render target.
//
// The test returns the SMALLEST (most specific) mesh AABB that contains the
// point, which yields both the owning model and the individual sub-mesh — so
// the caller can select a whole model or drill down into one of its meshes
// (Assimp: a Model is the imported file, its meshes are the file's sub-parts).
// ============================================================================

namespace scene {

bool pickModelAtPoint(const Scene& scene, const glm::vec3& worldPoint, RayHit& out) {
    out = RayHit{};
    out.position = worldPoint;

    float bestVolume = FLT_MAX;
    for (size_t mi = 0; mi < scene.models.size(); ++mi) {
        const Model& model = scene.models[mi];
        if (!model.visible)
            continue;

        // Transform the world point once into the model's local space, so the
        // AABB test is correct under rotation/scale (a rotated OBB in world
        // space is an axis-aligned box in local space).
        const glm::mat4 invModel = glm::inverse(model.modelMatrix());
        const glm::vec3 local = glm::vec3(invModel * glm::vec4(worldPoint, 1.0f));

        for (size_t meshIdx = 0; meshIdx < model.meshes.size(); ++meshIdx) {
            const ModelMesh& mesh = model.meshes[meshIdx];
            if (mesh.boundsMin.x > mesh.boundsMax.x)
                continue; // empty mesh (never populated)

            const glm::vec3 extent = mesh.boundsMax - mesh.boundsMin;
            // Tolerance: the surface point sits ON the AABB face (and flat
            // meshes like planes have a zero-thickness axis), so admit a small
            // slab scaled to the mesh size.
            const float eps =
                glm::max(glm::max(extent.x, extent.y), extent.z) * 0.02f + 1e-4f;
            if (local.x < mesh.boundsMin.x - eps || local.x > mesh.boundsMax.x + eps ||
                local.y < mesh.boundsMin.y - eps || local.y > mesh.boundsMax.y + eps ||
                local.z < mesh.boundsMin.z - eps || local.z > mesh.boundsMax.z + eps)
                continue;

            const float volume = (extent.x + 2.0f * eps) * (extent.y + 2.0f * eps) *
                                 (extent.z + 2.0f * eps);
            if (volume < bestVolume) {
                bestVolume = volume;
                out.hit = true;
                out.modelIndex = static_cast<int>(mi);
                out.meshIndex = static_cast<int>(meshIdx);
            }
        }
    }
    return out.hit;
}

} // namespace scene
