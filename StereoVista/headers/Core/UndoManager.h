#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "Engine/Data.h"
#include "Loaders/ModelLoader.h"

namespace Engine {

    // -----------------------------------------------------------------------
    // Universal undo/redo system.
    //
    // Actions are recorded *after* they have been performed, so undo() must
    // revert an action and redo() must re-apply it. Commands reference scene
    // objects by index; this is safe because commands are replayed in strict
    // LIFO order and every structural scene change (add/delete) goes through
    // this system. The history is cleared whenever a scene file is loaded,
    // since recorded indices would no longer match the new scene.
    // -----------------------------------------------------------------------

    class UndoCommand {
    public:
        virtual ~UndoCommand() = default;
        virtual void undo() = 0;
        virtual void redo() = 0;
        virtual std::string description() const = 0;
    };

    // Generic command built from a pair of closures. Used for property edits
    // (transforms, materials, light parameters) where before/after snapshots
    // are cheap to capture.
    class LambdaUndoCommand : public UndoCommand {
    public:
        LambdaUndoCommand(std::string description, std::function<void()> undoFn,
                          std::function<void()> redoFn)
            : desc(std::move(description)), undoFn(std::move(undoFn)),
              redoFn(std::move(redoFn)) {}

        void undo() override { if (undoFn) undoFn(); }
        void redo() override { if (redoFn) redoFn(); }
        std::string description() const override { return desc; }

    private:
        std::string desc;
        std::function<void()> undoFn;
        std::function<void()> redoFn;
    };

    class UndoManager {
    public:
        static UndoManager& instance();

        // Record an already-performed action. Clears the redo stack.
        void record(std::unique_ptr<UndoCommand> command);

        bool undo();
        bool redo();

        bool canUndo() const { return !undoStack.empty(); }
        bool canRedo() const { return !redoStack.empty(); }
        std::string undoDescription() const;
        std::string redoDescription() const;

        // Drops all history. Must be called when a scene file is loaded (the
        // recorded indices would no longer match) and once at shutdown while
        // the GL context is still valid (commands may own GPU resources of
        // deleted objects).
        void clear();

        // Invoked after every successful undo/redo so dependent systems
        // (voxelizer, SpaceMouse bounds, selection state) can resynchronize.
        void setSceneChangedCallback(std::function<void()> callback);

    private:
        UndoManager() = default;
        static constexpr size_t kMaxUndoEntries = 64;

        std::vector<std::unique_ptr<UndoCommand>> undoStack;
        std::vector<std::unique_ptr<UndoCommand>> redoStack;
        std::function<void()> sceneChangedCallback;
    };

    // -----------------------------------------------------------------------
    // High-level helpers used by the GUI and input callbacks. They operate on
    // the global scene containers (currentScene, pointLights, spotLights).
    // -----------------------------------------------------------------------
    namespace Undo {

        // Snapshot of every property the GUI panels can edit on a Mesh.
        struct MeshMaterialState {
            bool visible = true;
            glm::vec3 color = glm::vec3(1.0f);
            float shininess = 32.0f;
            float emissive = 0.0f;
        };

        // Snapshot of every property the GUI panels can edit on a Model.
        // Mesh geometry is intentionally not captured; mesh add/remove is
        // recorded as a separate structural command.
        struct ModelEditState {
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 rotation = glm::vec3(0.0f);
            glm::vec3 scale = glm::vec3(1.0f);
            bool visible = true;
            glm::vec3 color = glm::vec3(1.0f);
            float shininess = 1.0f;
            float emissive = 0.0f;
            float metallicFactor = 0.0f;
            float roughnessFactor = 0.5f;
            glm::vec3 F0 = glm::vec3(0.04f);
            float normalScale = 1.0f;
            float heightScale = 0.02f;
            float diffuseReflectivity = 0.8f;
            glm::vec3 specularColor = glm::vec3(1.0f);
            float specularDiffusion = 0.5f;
            float specularReflectivity = 0.0f;
            float refractiveIndex = 1.0f;
            float transparency = 0.0f;
            MaterialType materialType = MaterialType::CONCRETE;
            std::vector<MeshMaterialState> meshMaterials;

            static ModelEditState capture(const Model& model);
            void apply(Model& model) const;
            bool operator==(const ModelEditState& other) const;
            bool operator!=(const ModelEditState& other) const { return !(*this == other); }
        };

        // Snapshot of every property the GUI panels can edit on a PointCloud.
        struct PointCloudEditState {
            glm::vec3 position = glm::vec3(0.0f);
            glm::vec3 rotation = glm::vec3(0.0f);
            glm::vec3 scale = glm::vec3(1.0f);
            bool visible = true;
            float basePointSize = 2.0f;

            static PointCloudEditState capture(const PointCloud& pointCloud);
            void apply(PointCloud& pointCloud) const;
            bool operator==(const PointCloudEditState& other) const;
            bool operator!=(const PointCloudEditState& other) const { return !(*this == other); }
        };

        // Property edits. All of these are no-ops when before == after, so
        // callers can invoke them unconditionally at the end of an edit
        // gesture. Lights and the sun are small PODs and use full copies as
        // their edit snapshots.
        void recordModelEdit(int index, const ModelEditState& before, const ModelEditState& after);
        void recordPointCloudEdit(int index, const PointCloudEditState& before, const PointCloudEditState& after);
        void recordPointLightEdit(int index, const PointLight& before, const PointLight& after);
        void recordSpotLightEdit(int index, const SpotLight& before, const SpotLight& after);
        void recordSunEdit(const Sun& before, const Sun& after);

        // Viewport drag moves (Ctrl+drag). No-ops when before == after.
        void recordModelMoved(int index, const glm::vec3& before, const glm::vec3& after);
        void recordPointLightMoved(int index, const glm::vec3& before, const glm::vec3& after);
        void recordSpotLightMoved(int index, const glm::vec3& before, const glm::vec3& after);

        // Structural edits: these PERFORM the deletion and record it. Deleted
        // objects are moved into the undo entry (keeping their GPU resources
        // alive) and moved back into the scene on undo.
        void deleteModel(int index);
        void deletePointCloud(int index);
        void deletePointLight(int index);
        void deleteSpotLight(int index);
        void deleteMesh(int modelIndex, int meshIndex);
        void deleteSceneGroup(std::vector<int> modelIndices,
                              std::vector<int> pointCloudIndices,
                              std::vector<int> pointLightIndices,
                              std::vector<int> spotLightIndices);

        // Additions: call right after the new object was appended to its
        // container so the addition can be undone.
        void recordModelAdded(int index);
        void recordPointCloudAdded(int index);
        void recordPointLightAdded(int index);
        void recordSpotLightAdded(int index);

    }

}
