#include "Core/UndoManager.h"
#include "Core/SceneManager.h"

#include <algorithm>
#include <iostream>
#include <type_traits>

// Global scene containers owned by main.cpp.
extern Engine::Scene currentScene;
extern std::vector<Engine::PointLight> pointLights;
extern std::vector<Engine::SpotLight> spotLights;
extern Engine::Sun sun;

namespace Engine {

    // ======================================================================
    // UndoManager
    // ======================================================================

    UndoManager& UndoManager::instance() {
        static UndoManager manager;
        return manager;
    }

    void UndoManager::record(std::unique_ptr<UndoCommand> command) {
        if (!command) return;
        undoStack.push_back(std::move(command));
        if (undoStack.size() > kMaxUndoEntries) {
            undoStack.erase(undoStack.begin());
        }
        // A new action invalidates everything that was undone before it.
        redoStack.clear();
        if (modifiedCallback) modifiedCallback();
    }

    bool UndoManager::undo() {
        if (undoStack.empty()) return false;
        std::unique_ptr<UndoCommand> command = std::move(undoStack.back());
        undoStack.pop_back();
        command->undo();
        std::cout << "Undo: " << command->description() << std::endl;
        redoStack.push_back(std::move(command));
        if (sceneChangedCallback) sceneChangedCallback();
        if (modifiedCallback) modifiedCallback();
        return true;
    }

    bool UndoManager::redo() {
        if (redoStack.empty()) return false;
        std::unique_ptr<UndoCommand> command = std::move(redoStack.back());
        redoStack.pop_back();
        command->redo();
        std::cout << "Redo: " << command->description() << std::endl;
        undoStack.push_back(std::move(command));
        if (sceneChangedCallback) sceneChangedCallback();
        if (modifiedCallback) modifiedCallback();
        return true;
    }

    std::string UndoManager::undoDescription() const {
        return undoStack.empty() ? std::string() : undoStack.back()->description();
    }

    std::string UndoManager::redoDescription() const {
        return redoStack.empty() ? std::string() : redoStack.back()->description();
    }

    void UndoManager::clear() {
        undoStack.clear();
        redoStack.clear();
    }

    void UndoManager::setSceneChangedCallback(std::function<void()> callback) {
        sceneChangedCallback = std::move(callback);
    }

    void UndoManager::setModifiedCallback(std::function<void()> callback) {
        modifiedCallback = std::move(callback);
    }

    // ======================================================================
    // Commands
    // ======================================================================

    namespace Undo {

        // Frees the GL objects of a point cloud held by a discarded undo
        // entry. PointCloud's destructor does not release them (live clouds
        // share handles through moves), so the undo system must.
        static void freePointCloudGpuResources(PointCloud& pc) {
            if (pc.vao) { glDeleteVertexArrays(1, &pc.vao); pc.vao = 0; }
            if (pc.vbo) { glDeleteBuffers(1, &pc.vbo); pc.vbo = 0; }
            if (pc.chunkOutlineVAO) { glDeleteVertexArrays(1, &pc.chunkOutlineVAO); pc.chunkOutlineVAO = 0; }
            if (pc.chunkOutlineVBO) { glDeleteBuffers(1, &pc.chunkOutlineVBO); pc.chunkOutlineVBO = 0; }
            GLuint ssbos[] = { pc.computeBatchSSBO, pc.computeXyz12bSSBO, pc.computeXyz8bSSBO,
                               pc.computeXyz4bSSBO, pc.computeRGBASSBO };
            for (GLuint ssbo : ssbos) {
                if (ssbo) glDeleteBuffers(1, &ssbo);
            }
            pc.computeBatchSSBO = pc.computeXyz12bSSBO = pc.computeXyz8bSSBO = 0;
            pc.computeXyz4bSSBO = pc.computeRGBASSBO = 0;
            pc.numBatches = 0;
        }

        // GPU resources owned by an object stranded in a discarded undo entry
        // are disposed here. Models and meshes never freed their GL buffers
        // on deletion before this system existed, so only point clouds need
        // explicit disposal.
        template <typename T>
        static void disposeItem(T&) {}
        static void disposeItem(PointCloud& pc) { freePointCloudGpuResources(pc); }

        // Ownership ping-pong command for objects living in one of the global
        // scene vectors. While the object is deleted from the scene the
        // command owns it (keeping GPU resources alive so undo can restore it
        // cheaply); while it is in the scene the command holds an empty
        // placeholder. Works for both deletions and additions: an addition is
        // a deletion with undo/redo swapped.
        template <typename T>
        class VectorItemCommand : public UndoCommand {
        public:
            using ContainerGetter = std::vector<T>& (*)();

            // For removals the constructor performs the erase, taking
            // ownership of the object. For additions the object must already
            // be in the container at `index`.
            VectorItemCommand(ContainerGetter getContainer, int index, bool isRemoval,
                              std::string description)
                : getContainer(getContainer), index(index), isRemoval(isRemoval),
                  desc(std::move(description)) {
                if (isRemoval) takeFromScene();
            }

            ~VectorItemCommand() override {
                if (owning) disposeItem(item);
            }

            void undo() override { isRemoval ? returnToScene() : takeFromScene(); }
            void redo() override { isRemoval ? takeFromScene() : returnToScene(); }
            std::string description() const override { return desc; }

        private:
            void takeFromScene() {
                std::vector<T>& container = getContainer();
                if (index < 0 || index >= static_cast<int>(container.size())) return;
                item = std::move(container[index]);
                container.erase(container.begin() + index);
                owning = true;
            }

            void returnToScene() {
                if (!owning) return;
                std::vector<T>& container = getContainer();
                int at = std::min(index, static_cast<int>(container.size()));
                container.insert(container.begin() + at, std::move(item));
                owning = false;
            }

            ContainerGetter getContainer;
            int index;
            bool isRemoval;
            bool owning = false;
            std::string desc;
            T item{};
        };

        static std::vector<Model>& modelContainer() { return ::currentScene.models; }
        static std::vector<PointCloud>& pointCloudContainer() { return ::currentScene.pointClouds; }
        static std::vector<PointLight>& pointLightContainer() { return ::pointLights; }
        static std::vector<SpotLight>& spotLightContainer() { return ::spotLights; }

        // Removes a single mesh from a model and restores it on undo. Looks
        // the model up by index on every replay because the models vector may
        // reallocate. The stored mesh copy shares the original GL buffers
        // (meshes never free them), so re-inserting it is cheap and safe.
        class DeleteMeshCommand : public UndoCommand {
        public:
            DeleteMeshCommand(int modelIndex, int meshIndex)
                : modelIndex(modelIndex), meshIndex(meshIndex),
                  mesh(::currentScene.models[modelIndex].getMeshes()[meshIndex]) {
                removeMesh();
            }

            void undo() override {
                Model* model = getModel();
                if (!model) return;
                auto& meshes = model->getMeshes();
                int at = std::min(meshIndex, static_cast<int>(meshes.size()));
                meshes.insert(meshes.begin() + at, mesh);
                model->initializeMeshSelection();
            }

            void redo() override { removeMesh(); }

            std::string description() const override {
                std::string modelName = (modelIndex >= 0 && modelIndex < static_cast<int>(::currentScene.models.size()))
                                            ? ::currentScene.models[modelIndex].name
                                            : "model";
                return "Delete mesh from '" + modelName + "'";
            }

        private:
            Model* getModel() {
                if (modelIndex < 0 || modelIndex >= static_cast<int>(::currentScene.models.size()))
                    return nullptr;
                return &::currentScene.models[modelIndex];
            }

            void removeMesh() {
                Model* model = getModel();
                if (!model) return;
                auto& meshes = model->getMeshes();
                if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size())) return;
                meshes.erase(meshes.begin() + meshIndex);
                model->initializeMeshSelection();
            }

            int modelIndex;
            int meshIndex;
            Mesh mesh;
        };

        // Groups several commands into one undo entry (e.g. deleting a whole
        // scene group). Children are undone in reverse order so per-container
        // indices stay consistent.
        class CompositeCommand : public UndoCommand {
        public:
            explicit CompositeCommand(std::string description) : desc(std::move(description)) {}

            void add(std::unique_ptr<UndoCommand> command) {
                children.push_back(std::move(command));
            }

            bool empty() const { return children.empty(); }

            void undo() override {
                for (auto it = children.rbegin(); it != children.rend(); ++it) {
                    (*it)->undo();
                }
            }

            void redo() override {
                for (auto& child : children) {
                    child->redo();
                }
            }

            std::string description() const override { return desc; }

        private:
            std::string desc;
            std::vector<std::unique_ptr<UndoCommand>> children;
        };

        // ==================================================================
        // Edit-state snapshots
        // ==================================================================

        ModelEditState ModelEditState::capture(const Model& model) {
            ModelEditState s;
            s.position = model.position;
            s.rotation = model.rotation;
            s.scale = model.scale;
            s.visible = model.visible;
            s.color = model.color;
            s.shininess = model.shininess;
            s.emissive = model.emissive;
            s.metallicFactor = model.metallicFactor;
            s.roughnessFactor = model.roughnessFactor;
            s.F0 = model.F0;
            s.normalScale = model.normalScale;
            s.heightScale = model.heightScale;
            s.diffuseReflectivity = model.diffuseReflectivity;
            s.specularColor = model.specularColor;
            s.specularDiffusion = model.specularDiffusion;
            s.specularReflectivity = model.specularReflectivity;
            s.refractiveIndex = model.refractiveIndex;
            s.transparency = model.transparency;
            s.materialType = model.materialType;
            s.meshMaterials.reserve(model.getMeshes().size());
            for (const auto& mesh : model.getMeshes()) {
                MeshMaterialState m;
                m.visible = mesh.visible;
                m.color = mesh.color;
                m.shininess = mesh.shininess;
                m.emissive = mesh.emissive;
                s.meshMaterials.push_back(m);
            }
            return s;
        }

        void ModelEditState::apply(Model& model) const {
            model.position = position;
            model.rotation = rotation;
            model.scale = scale;
            model.visible = visible;
            model.color = color;
            model.shininess = shininess;
            model.emissive = emissive;
            model.metallicFactor = metallicFactor;
            model.roughnessFactor = roughnessFactor;
            model.F0 = F0;
            model.normalScale = normalScale;
            model.heightScale = heightScale;
            model.diffuseReflectivity = diffuseReflectivity;
            model.specularColor = specularColor;
            model.specularDiffusion = specularDiffusion;
            model.specularReflectivity = specularReflectivity;
            model.refractiveIndex = refractiveIndex;
            model.transparency = transparency;
            model.materialType = materialType;
            // Mesh materials are restored only when the mesh count still
            // matches; mesh add/remove is its own structural command.
            auto& meshes = model.getMeshes();
            if (meshMaterials.size() == meshes.size()) {
                for (size_t i = 0; i < meshes.size(); i++) {
                    meshes[i].visible = meshMaterials[i].visible;
                    meshes[i].color = meshMaterials[i].color;
                    meshes[i].shininess = meshMaterials[i].shininess;
                    meshes[i].emissive = meshMaterials[i].emissive;
                }
            }
        }

        bool ModelEditState::operator==(const ModelEditState& other) const {
            if (!(position == other.position && rotation == other.rotation &&
                  scale == other.scale && visible == other.visible &&
                  color == other.color && shininess == other.shininess &&
                  emissive == other.emissive &&
                  metallicFactor == other.metallicFactor &&
                  roughnessFactor == other.roughnessFactor && F0 == other.F0 &&
                  normalScale == other.normalScale &&
                  heightScale == other.heightScale &&
                  diffuseReflectivity == other.diffuseReflectivity &&
                  specularColor == other.specularColor &&
                  specularDiffusion == other.specularDiffusion &&
                  specularReflectivity == other.specularReflectivity &&
                  refractiveIndex == other.refractiveIndex &&
                  transparency == other.transparency &&
                  materialType == other.materialType &&
                  meshMaterials.size() == other.meshMaterials.size()))
                return false;
            for (size_t i = 0; i < meshMaterials.size(); i++) {
                const MeshMaterialState& a = meshMaterials[i];
                const MeshMaterialState& b = other.meshMaterials[i];
                if (!(a.visible == b.visible && a.color == b.color &&
                      a.shininess == b.shininess && a.emissive == b.emissive))
                    return false;
            }
            return true;
        }

        PointCloudEditState PointCloudEditState::capture(const PointCloud& pointCloud) {
            PointCloudEditState s;
            s.position = pointCloud.position;
            s.rotation = pointCloud.rotation;
            s.scale = pointCloud.scale;
            s.visible = pointCloud.visible;
            s.basePointSize = pointCloud.basePointSize;
            return s;
        }

        void PointCloudEditState::apply(PointCloud& pointCloud) const {
            pointCloud.position = position;
            pointCloud.rotation = rotation;
            pointCloud.scale = scale;
            pointCloud.visible = visible;
            pointCloud.basePointSize = basePointSize;
        }

        bool PointCloudEditState::operator==(const PointCloudEditState& other) const {
            return position == other.position && rotation == other.rotation &&
                   scale == other.scale && visible == other.visible &&
                   basePointSize == other.basePointSize;
        }

        static bool lightEquals(const PointLight& a, const PointLight& b) {
            return a.position == b.position && a.color == b.color &&
                   a.intensity == b.intensity && a.linear == b.linear &&
                   a.quadratic == b.quadratic && a.castShadows == b.castShadows;
        }

        static bool lightEquals(const SpotLight& a, const SpotLight& b) {
            return a.position == b.position && a.direction == b.direction &&
                   a.color == b.color && a.intensity == b.intensity &&
                   a.innerCutOff == b.innerCutOff && a.outerCutOff == b.outerCutOff &&
                   a.castShadows == b.castShadows;
        }

        static bool sunEquals(const Sun& a, const Sun& b) {
            return a.direction == b.direction && a.color == b.color &&
                   a.intensity == b.intensity && a.enabled == b.enabled;
        }

        // ==================================================================
        // Property-edit records
        // ==================================================================

        void recordModelEdit(int index, const ModelEditState& before, const ModelEditState& after) {
            if (before == after) return;
            UndoManager::instance().record(std::make_unique<LambdaUndoCommand>(
                "Edit model properties",
                [index, before]() {
                    if (index >= 0 && index < static_cast<int>(::currentScene.models.size()))
                        before.apply(::currentScene.models[index]);
                },
                [index, after]() {
                    if (index >= 0 && index < static_cast<int>(::currentScene.models.size()))
                        after.apply(::currentScene.models[index]);
                }));
        }

        void recordPointCloudEdit(int index, const PointCloudEditState& before, const PointCloudEditState& after) {
            if (before == after) return;
            UndoManager::instance().record(std::make_unique<LambdaUndoCommand>(
                "Edit point cloud properties",
                [index, before]() {
                    if (index >= 0 && index < static_cast<int>(::currentScene.pointClouds.size()))
                        before.apply(::currentScene.pointClouds[index]);
                },
                [index, after]() {
                    if (index >= 0 && index < static_cast<int>(::currentScene.pointClouds.size()))
                        after.apply(::currentScene.pointClouds[index]);
                }));
        }

        void recordPointLightEdit(int index, const PointLight& before, const PointLight& after) {
            if (lightEquals(before, after)) return;
            UndoManager::instance().record(std::make_unique<LambdaUndoCommand>(
                "Edit point light",
                [index, before]() {
                    if (index >= 0 && index < static_cast<int>(::pointLights.size()))
                        ::pointLights[index] = before;
                },
                [index, after]() {
                    if (index >= 0 && index < static_cast<int>(::pointLights.size()))
                        ::pointLights[index] = after;
                }));
        }

        void recordSpotLightEdit(int index, const SpotLight& before, const SpotLight& after) {
            if (lightEquals(before, after)) return;
            UndoManager::instance().record(std::make_unique<LambdaUndoCommand>(
                "Edit spot light",
                [index, before]() {
                    if (index >= 0 && index < static_cast<int>(::spotLights.size()))
                        ::spotLights[index] = before;
                },
                [index, after]() {
                    if (index >= 0 && index < static_cast<int>(::spotLights.size()))
                        ::spotLights[index] = after;
                }));
        }

        void recordSunEdit(const Sun& before, const Sun& after) {
            if (sunEquals(before, after)) return;
            UndoManager::instance().record(std::make_unique<LambdaUndoCommand>(
                "Edit sun light",
                [before]() { ::sun = before; },
                [after]() { ::sun = after; }));
        }

        // ==================================================================
        // Viewport drag moves
        // ==================================================================

        void recordModelMoved(int index, const glm::vec3& before, const glm::vec3& after) {
            if (before == after) return;
            std::string name = (index >= 0 && index < static_cast<int>(::currentScene.models.size()))
                                   ? ::currentScene.models[index].name
                                   : "model";
            UndoManager::instance().record(std::make_unique<LambdaUndoCommand>(
                "Move '" + name + "'",
                [index, before]() {
                    if (index >= 0 && index < static_cast<int>(::currentScene.models.size()))
                        ::currentScene.models[index].position = before;
                },
                [index, after]() {
                    if (index >= 0 && index < static_cast<int>(::currentScene.models.size()))
                        ::currentScene.models[index].position = after;
                }));
        }

        void recordPointLightMoved(int index, const glm::vec3& before, const glm::vec3& after) {
            if (before == after) return;
            UndoManager::instance().record(std::make_unique<LambdaUndoCommand>(
                "Move point light",
                [index, before]() {
                    if (index >= 0 && index < static_cast<int>(::pointLights.size()))
                        ::pointLights[index].position = before;
                },
                [index, after]() {
                    if (index >= 0 && index < static_cast<int>(::pointLights.size()))
                        ::pointLights[index].position = after;
                }));
        }

        void recordSpotLightMoved(int index, const glm::vec3& before, const glm::vec3& after) {
            if (before == after) return;
            UndoManager::instance().record(std::make_unique<LambdaUndoCommand>(
                "Move spot light",
                [index, before]() {
                    if (index >= 0 && index < static_cast<int>(::spotLights.size()))
                        ::spotLights[index].position = before;
                },
                [index, after]() {
                    if (index >= 0 && index < static_cast<int>(::spotLights.size()))
                        ::spotLights[index].position = after;
                }));
        }

        // ==================================================================
        // Structural edits
        // ==================================================================

        void deleteModel(int index) {
            if (index < 0 || index >= static_cast<int>(::currentScene.models.size())) return;
            std::string name = ::currentScene.models[index].name;
            UndoManager::instance().record(std::make_unique<VectorItemCommand<Model>>(
                &modelContainer, index, true, "Delete '" + name + "'"));
        }

        void deletePointCloud(int index) {
            if (index < 0 || index >= static_cast<int>(::currentScene.pointClouds.size())) return;
            std::string name = ::currentScene.pointClouds[index].name;
            UndoManager::instance().record(std::make_unique<VectorItemCommand<PointCloud>>(
                &pointCloudContainer, index, true, "Delete '" + name + "'"));
        }

        void deletePointLight(int index) {
            if (index < 0 || index >= static_cast<int>(::pointLights.size())) return;
            UndoManager::instance().record(std::make_unique<VectorItemCommand<PointLight>>(
                &pointLightContainer, index, true, "Delete point light"));
        }

        void deleteSpotLight(int index) {
            if (index < 0 || index >= static_cast<int>(::spotLights.size())) return;
            UndoManager::instance().record(std::make_unique<VectorItemCommand<SpotLight>>(
                &spotLightContainer, index, true, "Delete spot light"));
        }

        void deleteMesh(int modelIndex, int meshIndex) {
            if (modelIndex < 0 || modelIndex >= static_cast<int>(::currentScene.models.size())) return;
            auto& meshes = ::currentScene.models[modelIndex].getMeshes();
            if (meshIndex < 0 || meshIndex >= static_cast<int>(meshes.size())) return;
            UndoManager::instance().record(std::make_unique<DeleteMeshCommand>(modelIndex, meshIndex));
        }

        void deleteSceneGroup(std::vector<int> modelIndices,
                              std::vector<int> pointCloudIndices,
                              std::vector<int> pointLightIndices,
                              std::vector<int> spotLightIndices) {
            auto composite = std::make_unique<CompositeCommand>("Delete scene group");

            // Erase from highest to lowest index so the remaining indices in
            // each container stay valid while the group is taken apart.
            auto addRemovals = [&composite](std::vector<int>& indices, auto containerGetter,
                                            const char* what) {
                using ItemType = typename std::remove_reference<decltype(containerGetter()[0])>::type;
                std::sort(indices.rbegin(), indices.rend());
                for (int index : indices) {
                    if (index < 0 || index >= static_cast<int>(containerGetter().size())) continue;
                    composite->add(std::make_unique<VectorItemCommand<ItemType>>(
                        containerGetter, index, true, what));
                }
            };

            addRemovals(modelIndices, &modelContainer, "Delete model");
            addRemovals(pointCloudIndices, &pointCloudContainer, "Delete point cloud");
            addRemovals(pointLightIndices, &pointLightContainer, "Delete point light");
            addRemovals(spotLightIndices, &spotLightContainer, "Delete spot light");

            if (!composite->empty()) {
                UndoManager::instance().record(std::move(composite));
            }
        }

        // ==================================================================
        // Addition records
        // ==================================================================

        void recordModelAdded(int index) {
            if (index < 0 || index >= static_cast<int>(::currentScene.models.size())) return;
            std::string name = ::currentScene.models[index].name;
            UndoManager::instance().record(std::make_unique<VectorItemCommand<Model>>(
                &modelContainer, index, false, "Add '" + name + "'"));
        }

        void recordPointCloudAdded(int index) {
            if (index < 0 || index >= static_cast<int>(::currentScene.pointClouds.size())) return;
            std::string name = ::currentScene.pointClouds[index].name;
            UndoManager::instance().record(std::make_unique<VectorItemCommand<PointCloud>>(
                &pointCloudContainer, index, false, "Add '" + name + "'"));
        }

        void recordPointLightAdded(int index) {
            if (index < 0 || index >= static_cast<int>(::pointLights.size())) return;
            UndoManager::instance().record(std::make_unique<VectorItemCommand<PointLight>>(
                &pointLightContainer, index, false, "Add point light"));
        }

        void recordSpotLightAdded(int index) {
            if (index < 0 || index >= static_cast<int>(::spotLights.size())) return;
            UndoManager::instance().record(std::make_unique<VectorItemCommand<SpotLight>>(
                &spotLightContainer, index, false, "Add spot light"));
        }

    }

}
