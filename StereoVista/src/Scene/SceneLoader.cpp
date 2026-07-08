#include "Scene/Scene.h"

#include "RHI/Device.h"
#include "Renderer/GpuTypes.h"
#include "Renderer/MaterialSystem.h"

#include <glm/gtc/matrix_inverse.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <json.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>

// office.scene-style JSON, parsing the same keys the GL SceneManager writes
// (src/Core/SceneManager.cpp saveScene/loadScene). Only the render-relevant
// subset is consumed here: primitive + file models with transform/material,
// point lights, and the camera pose. Point clouds, spot lights (never lit by
// the GL renderer either — playbook C.4), clip planes and per-mesh texture
// overrides return with the full SceneManager in later phases.

namespace scene {

namespace {

using nlohmann::json;

glm::vec3 readVec3(const json& j, const char* key, glm::vec3 fallback) {
    if (!j.contains(key) || !j[key].is_array() || j[key].size() < 3)
        return fallback;
    return { j[key][0].get<float>(), j[key][1].get<float>(), j[key][2].get<float>() };
}

PrimitiveType primitiveFromString(const std::string& name) {
    if (name == "sphere") return PrimitiveType::Sphere;
    if (name == "cylinder") return PrimitiveType::Cylinder;
    if (name == "plane") return PrimitiveType::Plane;
    if (name == "torus") return PrimitiveType::Torus;
    return PrimitiveType::Cube; // GL loader's default
}

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
    model.meshes.push_back(makeMesh(device, buildPrimitive(type), name,
                                    materials.addMaterial(material)));
    return model;
}

} // namespace

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

void Scene::computeWorldBounds() {
    glm::vec3 minB(FLT_MAX), maxB(-FLT_MAX);
    for (const Model& model : models) {
        if (!model.visible)
            continue;
        const glm::mat4 m = model.modelMatrix();
        for (const ModelMesh& mesh : model.meshes) {
            if (mesh.boundsMin.x > mesh.boundsMax.x)
                continue; // empty mesh
            for (int corner = 0; corner < 8; ++corner) {
                const glm::vec3 local((corner & 1) ? mesh.boundsMax.x : mesh.boundsMin.x,
                                      (corner & 2) ? mesh.boundsMax.y : mesh.boundsMin.y,
                                      (corner & 4) ? mesh.boundsMax.z : mesh.boundsMin.z);
                const glm::vec3 world = glm::vec3(m * glm::vec4(local, 1.0f));
                minB = glm::min(minB, world);
                maxB = glm::max(maxB, world);
            }
        }
    }
    for (const std::unique_ptr<I3SSceneLayer>& layer : i3sLayers) {
        if (!layer || !layer->visible || layer->nodeBoxes.empty())
            continue;
        minB = glm::min(minB, layer->boundsMin);
        maxB = glm::max(maxB, layer->boundsMax);
    }
    if (minB.x <= maxB.x) {
        worldBoundsMin = minB;
        worldBoundsMax = maxB;
    }
}

Scene loadSceneFile(const std::string& path, rhi::Device& device,
                    renderer::MaterialSystem& materials) {
    std::ifstream file(path);
    if (!file)
        throw std::runtime_error("cannot open scene file: " + path);
    json sceneJson;
    try {
        file >> sceneJson;
    } catch (const std::exception& e) {
        throw std::runtime_error("malformed scene JSON in " + path + ": " + e.what());
    }

    Scene scene;
    scene.sourcePath = path;
    const std::filesystem::path sceneDir = std::filesystem::path(path).parent_path();

    if (sceneJson.contains("camera")) {
        const json& cam = sceneJson["camera"];
        scene.camera.position = readVec3(cam, "position", scene.camera.position);
        scene.camera.front =
            glm::normalize(readVec3(cam, "front", scene.camera.front));
        scene.camera.valid = true;
    }

    for (const json& modelJson : sceneJson.value("models", json::array())) {
        try {
            Model model;
            if (modelJson.contains("localPath") && !modelJson.contains("primitiveType")) {
                const std::filesystem::path modelPath =
                    sceneDir / modelJson["localPath"].get<std::string>();
                if (!importModelFile(modelPath.string(), device, materials, model))
                    continue; // importer already logged the reason
            } else {
                const glm::vec3 color = readVec3(modelJson, "color", glm::vec3(0.8f));
                model = makePrimitiveModel(
                    device, materials,
                    primitiveFromString(modelJson.value("primitiveType", "cube")),
                    modelJson.value("name", std::string("Primitive")), color,
                    modelJson.value("metallicFactor", 0.0f),
                    modelJson.value("roughnessFactor", 0.5f),
                    modelJson.value("emissive", 0.0f));
            }

            model.name = modelJson.value("name", model.name);
            model.position = readVec3(modelJson, "position", glm::vec3(0.0f));
            model.rotationDeg = readVec3(modelJson, "rotation", glm::vec3(0.0f));
            model.scale = readVec3(modelJson, "scale", glm::vec3(1.0f));
            model.visible = modelJson.value("visible", true);
            model.emissive = modelJson.value("emissive", model.emissive);
            scene.models.push_back(std::move(model));
        } catch (const std::exception& e) {
            std::cerr << "WARNING: skipping model in " << path << ": " << e.what()
                      << "\n";
        }
    }

    for (const json& lightJson : sceneJson.value("pointLights", json::array())) {
        PointLight light;
        light.position = readVec3(lightJson, "position", glm::vec3(0.0f));
        light.color = readVec3(lightJson, "color", glm::vec3(1.0f));
        light.intensity = lightJson.value("intensity", 1.0f);
        light.attenLinear = lightJson.value("linear", light.attenLinear);
        light.attenQuadratic = lightJson.value("quadratic", light.attenQuadratic);
        light.castsShadows = lightJson.value("castShadows", true);
        light.radius = lightJson.value("radius", light.radius); // not in GL scenes
        scene.pointLights.push_back(light);
    }

    scene.computeWorldBounds();
    std::cout << "Loaded scene " << path << " (" << scene.models.size() << " models, "
              << scene.pointLights.size() << " point lights)\n";
    return scene;
}

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
    light.position = { 1.5f, 3.0f, 2.0f };
    light.color = { 1.0f, 0.95f, 0.9f };
    light.intensity = 2.5f;
    scene.pointLights.push_back(light);

    scene.camera.valid = true;
    scene.camera.position = { 4.0f, 3.0f, 6.0f };
    scene.camera.front = glm::normalize(glm::vec3(-0.55f, -0.35f, -0.76f));

    scene.computeWorldBounds();
    std::cout << "No scene file found - using the built-in default scene\n";
    return scene;
}

} // namespace scene
