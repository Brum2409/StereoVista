#include "scene_manager.h"
#include <fstream>
#include <iostream>
#include <json.h>
#include "obj_loader.h"
#include "point_cloud_loader.h"

using json = nlohmann::json;

namespace Engine {

    void saveScene(const std::string& filename, const Engine::Scene& scene) {
        json j;

        for (const auto& model : scene.models) {
            json modelJson;
            modelJson["name"] = model.name;
            modelJson["path"] = model.path;
            modelJson["position"] = { model.position.x, model.position.y, model.position.z };
            modelJson["scale"] = { model.scale.x, model.scale.y, model.scale.z };
            modelJson["rotation"] = { model.rotation.x, model.rotation.y, model.rotation.z };
            modelJson["color"] = { model.color.r, model.color.g, model.color.b };
            modelJson["shininess"] = model.shininess;
            modelJson["emissive"] = model.emissive;

            modelJson["diffuseTexture"] = model.diffuseTexturePath;
            modelJson["hasCustomTexture"] = model.hasCustomTexture;
            modelJson["normalTexture"] = model.normalTexturePath;
            modelJson["specularTexture"] = model.specularTexturePath;
            modelJson["aoTexture"] = model.aoTexturePath;

            j["models"].push_back(modelJson);
        }

        // Save scene settings
        j["settings"]["separation"] = scene.settings.separation;
        j["settings"]["convergence"] = scene.settings.convergence;
        j["settings"]["nearPlane"] = scene.settings.nearPlane;
        j["settings"]["farPlane"] = scene.settings.farPlane;

        for (const auto& pointCloud : scene.pointClouds) {
            json pointCloudJson;
            pointCloudJson["name"] = pointCloud.name;
            pointCloudJson["filePath"] = pointCloud.filePath;
            pointCloudJson["position"] = { pointCloud.position.x, pointCloud.position.y, pointCloud.position.z };
            pointCloudJson["rotation"] = { pointCloud.rotation.x, pointCloud.rotation.y, pointCloud.rotation.z };
            pointCloudJson["scale"] = { pointCloud.scale.x, pointCloud.scale.y, pointCloud.scale.z };

            j["pointClouds"].push_back(pointCloudJson);
        }

        std::ofstream file(filename);
        file << std::setw(4) << j << std::endl;
    }

    Scene loadScene(const std::string& filename) {
        std::ifstream file(filename);
        json j;
        file >> j;

        Scene scene;
        std::map<std::string, Engine::ObjModel> loadedModels;

        for (const auto& modelJson : j["models"]) {
            std::string modelPath = modelJson["path"];
            Engine::ObjModel model;

            if (modelPath == "cube") {
                model = Engine::createCube(
                    glm::vec3(modelJson["color"][0], modelJson["color"][1], modelJson["color"][2]),
                    modelJson["shininess"],
                    modelJson["emissive"]
                );
            }
            else {
                auto it = loadedModels.find(modelPath);
                if (it == loadedModels.end()) {
                    std::ifstream modelFile(modelPath);
                    if (modelFile.good()) {
                        model = Engine::loadObjFile(modelPath);
                        loadedModels[modelPath] = model;
                    }
                    else {
                        std::cout << "Warning: Model file " << modelPath << " not found. Loading a cube instead." << std::endl;
                        model = Engine::createCube(
                            glm::vec3(modelJson["color"][0], modelJson["color"][1], modelJson["color"][2]),
                            modelJson["shininess"],
                            modelJson["emissive"]
                        );
                        model.name = "Cube (Replacement for " + modelJson["name"].get<std::string>() + ")";
                    }
                }
                else {
                    model = it->second;
                }
            }

            model.name = modelJson["name"];
            model.path = modelPath;
            model.position = glm::vec3(modelJson["position"][0], modelJson["position"][1], modelJson["position"][2]);
            model.scale = glm::vec3(modelJson["scale"][0], modelJson["scale"][1], modelJson["scale"][2]);
            model.rotation = glm::vec3(modelJson["rotation"][0], modelJson["rotation"][1], modelJson["rotation"][2]);
            model.color = glm::vec3(modelJson["color"][0], modelJson["color"][1], modelJson["color"][2]);
            model.shininess = modelJson["shininess"];
            model.emissive = modelJson["emissive"];

            model.diffuseTexturePath = modelJson.value("diffuseTexture", "");
            model.normalTexturePath = modelJson.value("normalTexture", "");
            model.specularTexturePath = modelJson.value("specularTexture", "");
            model.aoTexturePath = modelJson.value("aoTexture", "");

            // Load the actual textures
            if (!model.diffuseTexturePath.empty()) {
                model.texture = loadTextureFromFile(model.diffuseTexturePath.c_str());
            }
            if (!model.normalTexturePath.empty()) {
                model.normalMap = loadTextureFromFile(model.normalTexturePath.c_str());
            }
            if (!model.specularTexturePath.empty()) {
                model.specularMap = loadTextureFromFile(model.specularTexturePath.c_str());
            }
            if (!model.aoTexturePath.empty()) {
                model.aoMap = loadTextureFromFile(model.aoTexturePath.c_str());
            }

            scene.models.push_back(model);
        }

        // Load scene settings
        scene.settings.separation = j["settings"]["separation"];
        scene.settings.convergence = j["settings"]["convergence"];
        scene.settings.nearPlane = j["settings"]["nearPlane"];
        scene.settings.farPlane = j["settings"]["farPlane"];

        if (j.contains("pointClouds")) {
            for (const auto& pointCloudJson : j["pointClouds"]) {
                PointCloud pointCloud = Engine::PointCloudLoader::loadPointCloudFile(pointCloudJson["filePath"]);
                pointCloud.name = pointCloudJson["name"];
                pointCloud.position = glm::vec3(pointCloudJson["position"][0], pointCloudJson["position"][1], pointCloudJson["position"][2]);
                pointCloud.rotation = glm::vec3(pointCloudJson["rotation"][0], pointCloudJson["rotation"][1], pointCloudJson["rotation"][2]);
                pointCloud.scale = glm::vec3(pointCloudJson["scale"][0], pointCloudJson["scale"][1], pointCloudJson["scale"][2]);

                scene.pointClouds.push_back(pointCloud);
            }
        }

        return scene;
    }

 

}  // namespace Engine