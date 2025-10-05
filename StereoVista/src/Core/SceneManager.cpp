#include "Core/SceneManager.h"
#include <fstream>
#include <iostream>
#include <json.h>
#include "Loaders/PointCloudLoader.h"
#include <filesystem>
#include <unordered_set>
#include <utility>

using json = nlohmann::json;

namespace Engine {

    void saveScene(const std::string& filename, const Scene& scene, const Camera& camera) {
        try {
            // Ensure filename has .scene extension
            std::filesystem::path scenePath(filename);
            if (scenePath.extension() != ".scene") {
                scenePath.replace_extension(".scene");
            }

            std::string sceneName = scenePath.stem().string();
            std::filesystem::path sceneDir = scenePath.parent_path() / sceneName;

            // Create directory structure
            std::filesystem::create_directories(sceneDir / "models");
            std::filesystem::create_directories(sceneDir / "pointClouds");

            // Create scene data json
            json sceneJson;

            // Save current camera state
            auto cameraState = camera.GetState();
            sceneJson["camera"]["position"] = {cameraState.position.x, cameraState.position.y, cameraState.position.z};
            sceneJson["camera"]["front"] = {cameraState.front.x, cameraState.front.y, cameraState.front.z};
            sceneJson["camera"]["up"] = {cameraState.up.x, cameraState.up.y, cameraState.up.z};
            sceneJson["camera"]["yaw"] = cameraState.yaw;
            sceneJson["camera"]["pitch"] = cameraState.pitch;
            sceneJson["camera"]["zoom"] = cameraState.zoom;
            sceneJson["camera"]["orientation"] = {cameraState.orientation.x, cameraState.orientation.y, cameraState.orientation.z, cameraState.orientation.w};

            // Save models
            json modelsJson = json::array();
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
                modelJson["visible"] = model.visible;

                // Save PBR material properties
                modelJson["metallicFactor"] = model.metallicFactor;
                modelJson["roughnessFactor"] = model.roughnessFactor;
                modelJson["F0"] = { model.F0.x, model.F0.y, model.F0.z };
                modelJson["normalScale"] = model.normalScale;
                modelJson["heightScale"] = model.heightScale;

                // Save VCT/legacy material properties
                modelJson["diffuseReflectivity"] = model.diffuseReflectivity;
                modelJson["specularColor"] = { model.specularColor.r, model.specularColor.g, model.specularColor.b };
                modelJson["specularDiffusion"] = model.specularDiffusion;
                modelJson["specularReflectivity"] = model.specularReflectivity;
                modelJson["refractiveIndex"] = model.refractiveIndex;
                modelJson["transparency"] = model.transparency;

                // Check if this is a file-based model (not a primitive)
                bool isPrimitive = (model.path == "cube" || model.path == "sphere" ||
                                  model.path == "cylinder" || model.path == "plane" ||
                                  model.path == "torus" || model.path.empty());

                // Save primitive type for proper reconstruction
                if (isPrimitive && !model.path.empty()) {
                    modelJson["primitiveType"] = model.path;
                }

                if (!model.path.empty() && !isPrimitive) {
                    // Create model-specific directory
                    std::filesystem::path modelDir = sceneDir / "models" / model.name;
                    std::filesystem::create_directories(modelDir);

                    // Copy original model file and all associated files
                    std::filesystem::path originalModelPath(model.path);
                    if (std::filesystem::exists(originalModelPath)) {
                        std::string mainModelFilename = originalModelPath.filename().string();
                        std::filesystem::path mainModelNewPath = modelDir / mainModelFilename;

                        try {
                            // Copy the main model file
                            std::filesystem::copy_file(originalModelPath, mainModelNewPath,
                                std::filesystem::copy_options::overwrite_existing);
                            modelJson["localPath"] = "models/" + model.name + "/" + mainModelFilename;

                            // Get the model's directory and base name for finding associated files
                            std::filesystem::path originalModelDir = originalModelPath.parent_path();
                            std::string baseName = originalModelPath.stem().string();
                            std::string extension = originalModelPath.extension().string();

                            // Copy associated files based on model format
                            if (extension == ".gltf" || extension == ".glb") {
                                // For GLTF: copy .bin files with same base name
                                std::filesystem::path binFile = originalModelDir / (baseName + ".bin");
                                if (std::filesystem::exists(binFile)) {
                                    std::filesystem::copy_file(binFile, modelDir / binFile.filename(),
                                        std::filesystem::copy_options::overwrite_existing);
                                    std::cout << "Copied GLTF binary: " << binFile.filename() << std::endl;
                                }
                            }
                            else if (extension == ".obj") {
                                // For OBJ: copy .mtl file with same base name
                                std::filesystem::path mtlFile = originalModelDir / (baseName + ".mtl");
                                if (std::filesystem::exists(mtlFile)) {
                                    std::filesystem::copy_file(mtlFile, modelDir / mtlFile.filename(),
                                        std::filesystem::copy_options::overwrite_existing);
                                    std::cout << "Copied OBJ material: " << mtlFile.filename() << std::endl;
                                }
                            }
                            else if (extension == ".fbx" || extension == ".dae" || extension == ".blend") {
                                // For FBX/Collada/Blender: may have embedded or external textures
                                // Additional files are typically handled by texture copying below
                            }
                        }
                        catch (const std::exception& e) {
                            std::cerr << "Failed to copy model file " << model.path << ": " << e.what() << std::endl;
                        }
                    } else {
                        std::cerr << "Warning: Model file not found: " << originalModelPath << std::endl;
                    }

                    // Save mesh-specific texture information
                    json meshesJson = json::array();
                    for (size_t meshIndex = 0; meshIndex < model.getMeshes().size(); meshIndex++) {
                        const auto& mesh = model.getMeshes()[meshIndex];

                        if (mesh.textures.empty()) {
                            continue; // Skip meshes with no textures
                        }

                        json meshJson;
                        meshJson["meshIndex"] = meshIndex;
                        json texturesJson = json::array();

                        for (const auto& texture : mesh.textures) {
                            if (texture.fullPath.empty()) {
                                std::cerr << "Warning: Texture has empty fullPath: " << texture.type << std::endl;
                                continue;
                            }

                            // Copy texture file
                            try {
                                std::filesystem::path texturePath(texture.fullPath);
                                if (!std::filesystem::exists(texturePath)) {
                                    std::cerr << "Warning: Texture file not found: " << texturePath << std::endl;
                                    continue;
                                }

                                std::string newTextureName = texturePath.filename().string();
                                std::filesystem::path newTexturePath = modelDir / newTextureName;

                                std::filesystem::copy_file(texturePath, newTexturePath,
                                    std::filesystem::copy_options::overwrite_existing);

                                json textureJson;
                                textureJson["type"] = texture.type;
                                textureJson["path"] = texture.path;
                                textureJson["filename"] = newTextureName;
                                texturesJson.push_back(textureJson);
                            }
                            catch (const std::exception& e) {
                                std::cerr << "Failed to copy texture " << texture.fullPath << ": " << e.what() << std::endl;
                            }
                        }

                        if (!texturesJson.empty()) {
                            meshJson["textures"] = texturesJson;
                            meshesJson.push_back(meshJson);
                        }
                    }

                    // Only add meshes array if we have mesh data
                    if (!meshesJson.empty()) {
                        modelJson["meshes"] = meshesJson;
                    }
                }

                modelsJson.push_back(modelJson);
            }
            sceneJson["models"] = modelsJson;

            // Save point clouds
            json pointCloudsJson = json::array();
            for (const auto& pointCloud : scene.pointClouds) {
                json pointCloudJson;
                pointCloudJson["name"] = pointCloud.name;
                pointCloudJson["position"] = { pointCloud.position.x, pointCloud.position.y, pointCloud.position.z };
                pointCloudJson["rotation"] = { pointCloud.rotation.x, pointCloud.rotation.y, pointCloud.rotation.z };
                pointCloudJson["scale"] = { pointCloud.scale.x, pointCloud.scale.y, pointCloud.scale.z };

                std::string pcFilename = pointCloud.name + ".pcb";
                std::filesystem::path pcPath = sceneDir / "pointClouds" / pcFilename;
                Engine::PointCloudLoader::exportToBinary(pointCloud, pcPath.string());

                pointCloudJson["dataPath"] = "pointClouds/" + pcFilename;
                pointCloudsJson.push_back(pointCloudJson);
            }
            sceneJson["pointClouds"] = pointCloudsJson;

            // Save point lights
            json pointLightsJson = json::array();
            for (const auto& pointLight : scene.pointLights) {
                json pointLightJson;
                pointLightJson["position"] = { pointLight.position.x, pointLight.position.y, pointLight.position.z };
                pointLightJson["color"] = { pointLight.color.x, pointLight.color.y, pointLight.color.z };
                pointLightJson["intensity"] = pointLight.intensity;
                pointLightsJson.push_back(pointLightJson);
            }
            sceneJson["pointLights"] = pointLightsJson;

            // Save spot lights
            json spotLightsJson = json::array();
            for (const auto& spotLight : scene.spotLights) {
                json spotLightJson;
                spotLightJson["position"] = { spotLight.position.x, spotLight.position.y, spotLight.position.z };
                spotLightJson["direction"] = { spotLight.direction.x, spotLight.direction.y, spotLight.direction.z };
                spotLightJson["color"] = { spotLight.color.x, spotLight.color.y, spotLight.color.z };
                spotLightJson["intensity"] = spotLight.intensity;
                spotLightJson["innerCutOff"] = spotLight.innerCutOff;
                spotLightJson["outerCutOff"] = spotLight.outerCutOff;
                spotLightsJson.push_back(spotLightJson);
            }
            sceneJson["spotLights"] = spotLightsJson;

            std::cout << "\n=== Scene Save Summary ===" << std::endl;
            std::cout << "Models saved: " << scene.models.size() << std::endl;
            std::cout << "Point clouds saved: " << scene.pointClouds.size() << std::endl;
            std::cout << "Point lights saved: " << scene.pointLights.size() << std::endl;
            std::cout << "Spot lights saved: " << scene.spotLights.size() << std::endl;
            std::cout << "Scene directory: " << sceneDir << std::endl;
            std::cout << "========================\n" << std::endl;

            // Write scene file with chunking support
            std::string jsonStr = sceneJson.dump(4);
            const size_t maxChunkSize = 100 * 1024 * 1024; // 100MB chunks

            if (jsonStr.size() > maxChunkSize) {
                // Split into multiple files if too large
                size_t numChunks = (jsonStr.size() + maxChunkSize - 1) / maxChunkSize;

                for (size_t i = 0; i < numChunks; i++) {
                    std::string chunkFilename = scenePath.string() + "." + std::to_string(i);
                    std::ofstream chunkFile(chunkFilename);
                    if (!chunkFile.is_open()) {
                        throw std::runtime_error("Failed to create scene chunk file: " + chunkFilename);
                    }

                    size_t start = i * maxChunkSize;
                    size_t length = std::min(maxChunkSize, jsonStr.size() - start);
                    chunkFile << jsonStr.substr(start, length);
                }

                // Write metadata file
                std::ofstream metaFile(scenePath);
                json metaJson;
                metaJson["numChunks"] = numChunks;
                metaFile << std::setw(4) << metaJson << std::endl;
            }
            else {
                // Write single file
                std::ofstream sceneFile(scenePath);
                if (!sceneFile.is_open()) {
                    throw std::runtime_error("Failed to create scene file: " + scenePath.string());
                }
                sceneFile << jsonStr;
            }
        }
        catch (const std::exception& e) {
            throw std::runtime_error("Failed to save scene: " + std::string(e.what()));
        }
    }

    Scene loadScene(const std::string& filename, Camera& camera) {
        Scene scene;
        json sceneJson;
        std::filesystem::path scenePath;
        std::filesystem::path sceneDir;

        try {
            // Check if this is a chunked file
            std::ifstream metaFile(filename);
            if (!metaFile.is_open()) {
                throw std::runtime_error("Failed to open scene file: " + filename);
            }

            json metaJson;
            metaFile >> metaJson;

            if (metaJson.contains("numChunks")) {
                // Load chunked file
                std::string combinedJson;
                size_t numChunks = metaJson["numChunks"];

                for (size_t i = 0; i < numChunks; i++) {
                    std::string chunkFilename = filename + "." + std::to_string(i);
                    std::ifstream chunkFile(chunkFilename);
                    if (!chunkFile.is_open()) {
                        throw std::runtime_error("Failed to open scene chunk file: " + chunkFilename);
                    }

                    std::string chunk((std::istreambuf_iterator<char>(chunkFile)),
                        std::istreambuf_iterator<char>());
                    combinedJson += chunk;
                }
                sceneJson = json::parse(combinedJson);
            }
            else {
                // Regular single file
                metaFile.seekg(0);
                metaFile >> sceneJson;
            }

            // Get scene directory
            scenePath = std::filesystem::path(filename);
            sceneDir = scenePath.parent_path() / scenePath.stem();

            // Verify scene directory exists
            if (!std::filesystem::exists(sceneDir)) {
                throw std::runtime_error("Scene directory not found: " + sceneDir.string());
            }

            // Load camera state if it exists in the scene
            if (sceneJson.contains("camera")) {
                const auto& cameraJson = sceneJson["camera"];

                // Position
                if (cameraJson.contains("position") && cameraJson["position"].is_array() &&
                    cameraJson["position"].size() == 3) {
                    scene.cameraState.position = {
                        cameraJson["position"][0].get<float>(),
                        cameraJson["position"][1].get<float>(),
                        cameraJson["position"][2].get<float>()
                    };
                }

                // Front vector
                if (cameraJson.contains("front") && cameraJson["front"].is_array() &&
                    cameraJson["front"].size() == 3) {
                    scene.cameraState.front = {
                        cameraJson["front"][0].get<float>(),
                        cameraJson["front"][1].get<float>(),
                        cameraJson["front"][2].get<float>()
                    };
                }

                // Up vector
                if (cameraJson.contains("up") && cameraJson["up"].is_array() &&
                    cameraJson["up"].size() == 3) {
                    scene.cameraState.up = {
                        cameraJson["up"][0].get<float>(),
                        cameraJson["up"][1].get<float>(),
                        cameraJson["up"][2].get<float>()
                    };
                }

                // Other camera properties
                if (cameraJson.contains("yaw")) {
                    scene.cameraState.yaw = cameraJson["yaw"].get<float>();
                }
                if (cameraJson.contains("pitch")) {
                    scene.cameraState.pitch = cameraJson["pitch"].get<float>();
                }
                if (cameraJson.contains("zoom")) {
                    scene.cameraState.zoom = cameraJson["zoom"].get<float>();
                }

                // Orientation quaternion
                if (cameraJson.contains("orientation") && cameraJson["orientation"].is_array() &&
                    cameraJson["orientation"].size() == 4) {
                    scene.cameraState.orientation = {
                        cameraJson["orientation"][3].get<float>(), // w component first for glm::quat constructor
                        cameraJson["orientation"][0].get<float>(), // x
                        cameraJson["orientation"][1].get<float>(), // y
                        cameraJson["orientation"][2].get<float>()  // z
                    };
                }

                // Apply the loaded camera state
                camera.SetState(scene.cameraState);
            }

            // Load models
            if (sceneJson.contains("models")) {
                for (const auto& modelJson : sceneJson["models"]) {
                    try {
                        Model model;

                        // Check if this is a primitive or file-based model
                        bool isPrimitive = modelJson.contains("primitiveType");

                        if (!isPrimitive && modelJson.contains("localPath")) {
                            // Load file-based model
                            std::filesystem::path modelPath = sceneDir / modelJson["localPath"].get<std::string>();

                            // Verify model file exists
                            if (!std::filesystem::exists(modelPath)) {
                                std::cerr << "Error: Model file not found: " << modelPath << std::endl;
                                continue;
                            }

                            // Check for associated files (like .bin for GLTF)
                            std::string extension = modelPath.extension().string();
                            if (extension == ".gltf") {
                                std::filesystem::path binFile = modelPath.parent_path() / (modelPath.stem().string() + ".bin");
                                if (!std::filesystem::exists(binFile)) {
                                    std::cerr << "Warning: GLTF binary file not found: " << binFile << std::endl;
                                    std::cerr << "Model may fail to load or be incomplete." << std::endl;
                                }
                            }

                            // Load model from file
                            std::cout << "Loading model from: " << modelPath << std::endl;
                            Model* loadedModel = Engine::loadModel(modelPath.string());
                            if (!loadedModel) {
                                std::cerr << "Error: Failed to load model: " << modelPath << std::endl;
                                continue;
                            }

                            model = *loadedModel;
                            delete loadedModel;

                            // Set paths - use the scene-local path as the new path
                            model.path = modelPath.string();
                            model.directory = modelPath.parent_path().string();

                            std::cout << "Successfully loaded model with " << model.getMeshes().size() << " meshes" << std::endl;

                            // Clear all existing textures before loading saved ones
                            for (auto& mesh : model.getMeshes()) {
                                mesh.textures.clear();
                            }

                            // Load mesh-specific textures
                            if (modelJson.contains("meshes")) {
                                for (const auto& meshJson : modelJson["meshes"]) {
                                    size_t meshIndex = meshJson["meshIndex"].get<size_t>();

                                    // Validate mesh index
                                    if (meshIndex >= model.getMeshes().size()) {
                                        std::cerr << "Warning: Invalid mesh index " << meshIndex
                                                  << " for model with " << model.getMeshes().size() << " meshes" << std::endl;
                                        continue;
                                    }

                                    auto& mesh = model.getMeshes()[meshIndex];

                                    if (meshJson.contains("textures")) {
                                        for (const auto& textureJson : meshJson["textures"]) {
                                            Texture texture;
                                            texture.type = textureJson["type"].get<std::string>();
                                            texture.path = textureJson["path"].get<std::string>();

                                            // Build texture path: sceneDir/models/modelName/filename
                                            std::filesystem::path texturePath = modelPath.parent_path() /
                                                textureJson["filename"].get<std::string>();

                                            // Verify texture file exists
                                            if (!std::filesystem::exists(texturePath)) {
                                                std::cerr << "Warning: Texture file not found: " << texturePath << std::endl;
                                                continue;
                                            }

                                            texture.fullPath = texturePath.string();
                                            texture.id = Model::TextureFromFile(
                                                texturePath.filename().string().c_str(),
                                                texturePath.parent_path().string(),
                                                texture.fullPath
                                            );

                                            mesh.textures.push_back(texture);
                                            std::cout << "Loaded texture: " << texture.type << " -> " << texturePath << std::endl;
                                        }
                                    }
                                }
                            }
                        }
                        else {
                            // Creating a primitive
                            glm::vec3 color = glm::vec3(
                                modelJson["color"][0].get<float>(),
                                modelJson["color"][1].get<float>(),
                                modelJson["color"][2].get<float>()
                            );
                            float shininess = modelJson.value("shininess", 1.0f);
                            float emissive = modelJson.value("emissive", 0.0f);

                            // Get primitive type
                            std::string primitiveType = modelJson.value("primitiveType", "cube");

                            if (primitiveType == "sphere") {
                                model = Engine::createSphere(color, shininess, emissive);
                            }
                            else if (primitiveType == "cylinder") {
                                model = Engine::createCylinder(color, shininess, emissive);
                            }
                            else if (primitiveType == "plane") {
                                model = Engine::createPlane(color, shininess, emissive);
                            }
                            else if (primitiveType == "torus") {
                                model = Engine::createTorus(color, shininess, emissive);
                            }
                            else {
                                // Default to cube
                                model = Engine::createCube(color, shininess, emissive);
                            }

                            model.color = color;
                        }

                        // Set common model properties
                        model.name = modelJson["name"].get<std::string>();
                        model.position = glm::vec3(
                            modelJson["position"][0].get<float>(),
                            modelJson["position"][1].get<float>(),
                            modelJson["position"][2].get<float>()
                        );
                        model.scale = glm::vec3(
                            modelJson["scale"][0].get<float>(),
                            modelJson["scale"][1].get<float>(),
                            modelJson["scale"][2].get<float>()
                        );
                        model.rotation = glm::vec3(
                            modelJson["rotation"][0].get<float>(),
                            modelJson["rotation"][1].get<float>(),
                            modelJson["rotation"][2].get<float>()
                        );
                        model.color = glm::vec3(
                            modelJson["color"][0].get<float>(),
                            modelJson["color"][1].get<float>(),
                            modelJson["color"][2].get<float>()
                        );
                        model.shininess = modelJson.value("shininess", 1.0f);
                        model.emissive = modelJson.value("emissive", 0.0f);
                        model.visible = modelJson.value("visible", true);

                        // Load PBR material properties
                        model.metallicFactor = modelJson.value("metallicFactor", 0.0f);
                        model.roughnessFactor = modelJson.value("roughnessFactor", 0.5f);
                        if (modelJson.contains("F0") && modelJson["F0"].is_array() && modelJson["F0"].size() == 3) {
                            model.F0 = glm::vec3(
                                modelJson["F0"][0].get<float>(),
                                modelJson["F0"][1].get<float>(),
                                modelJson["F0"][2].get<float>()
                            );
                        }
                        model.normalScale = modelJson.value("normalScale", 1.0f);
                        model.heightScale = modelJson.value("heightScale", 0.02f);

                        // Load VCT/legacy material properties
                        model.diffuseReflectivity = modelJson.value("diffuseReflectivity", 0.8f);
                        if (modelJson.contains("specularColor") && modelJson["specularColor"].is_array() && modelJson["specularColor"].size() == 3) {
                            model.specularColor = glm::vec3(
                                modelJson["specularColor"][0].get<float>(),
                                modelJson["specularColor"][1].get<float>(),
                                modelJson["specularColor"][2].get<float>()
                            );
                        }
                        model.specularDiffusion = modelJson.value("specularDiffusion", 0.5f);
                        model.specularReflectivity = modelJson.value("specularReflectivity", 0.0f);
                        model.refractiveIndex = modelJson.value("refractiveIndex", 1.0f);
                        model.transparency = modelJson.value("transparency", 0.0f);

                        scene.models.push_back(model);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load model: " << e.what() << std::endl;
                    }
                }
            }

            // Load point clouds
            if (sceneJson.contains("pointClouds")) {
                for (const auto& pointCloudJson : sceneJson["pointClouds"]) {
                    try {
                        if (!pointCloudJson.contains("dataPath") ||
                            !pointCloudJson.contains("name") ||
                            !pointCloudJson.contains("position") ||
                            !pointCloudJson.contains("rotation") ||
                            !pointCloudJson.contains("scale")) {
                            throw std::runtime_error("Point cloud JSON missing required fields");
                        }

                        std::filesystem::path pcPath = sceneDir / pointCloudJson["dataPath"].get<std::string>();
                        PointCloud pointCloud = std::move(Engine::PointCloudLoader::loadFromBinary(pcPath.string()));

                        pointCloud.name = pointCloudJson["name"];
                        pointCloud.position = glm::vec3(
                            pointCloudJson["position"][0].get<float>(),
                            pointCloudJson["position"][1].get<float>(),
                            pointCloudJson["position"][2].get<float>()
                        );
                        pointCloud.rotation = glm::vec3(
                            pointCloudJson["rotation"][0].get<float>(),
                            pointCloudJson["rotation"][1].get<float>(),
                            pointCloudJson["rotation"][2].get<float>()
                        );
                        pointCloud.scale = glm::vec3(
                            pointCloudJson["scale"][0].get<float>(),
                            pointCloudJson["scale"][1].get<float>(),
                            pointCloudJson["scale"][2].get<float>()
                        );

                        scene.pointClouds.push_back(std::move(pointCloud));
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load point cloud: " << e.what() << std::endl;
                    }
                }
            }

            // Load point lights
            if (sceneJson.contains("pointLights")) {
                for (const auto& pointLightJson : sceneJson["pointLights"]) {
                    try {
                        PointLight pointLight;
                        pointLight.position = glm::vec3(
                            pointLightJson["position"][0].get<float>(),
                            pointLightJson["position"][1].get<float>(),
                            pointLightJson["position"][2].get<float>()
                        );
                        pointLight.color = glm::vec3(
                            pointLightJson["color"][0].get<float>(),
                            pointLightJson["color"][1].get<float>(),
                            pointLightJson["color"][2].get<float>()
                        );
                        pointLight.intensity = pointLightJson.value("intensity", 1.0f);
                        
                        scene.pointLights.push_back(pointLight);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load point light: " << e.what() << std::endl;
                    }
                }
            }

            // Load spot lights
            if (sceneJson.contains("spotLights")) {
                for (const auto& spotLightJson : sceneJson["spotLights"]) {
                    try {
                        SpotLight spotLight;
                        spotLight.position = glm::vec3(
                            spotLightJson["position"][0].get<float>(),
                            spotLightJson["position"][1].get<float>(),
                            spotLightJson["position"][2].get<float>()
                        );
                        spotLight.direction = glm::vec3(
                            spotLightJson["direction"][0].get<float>(),
                            spotLightJson["direction"][1].get<float>(),
                            spotLightJson["direction"][2].get<float>()
                        );
                        spotLight.color = glm::vec3(
                            spotLightJson["color"][0].get<float>(),
                            spotLightJson["color"][1].get<float>(),
                            spotLightJson["color"][2].get<float>()
                        );
                        spotLight.intensity = spotLightJson.value("intensity", 1.0f);
                        spotLight.innerCutOff = spotLightJson.value("innerCutOff", 0.9f);
                        spotLight.outerCutOff = spotLightJson.value("outerCutOff", 0.82f);
                        
                        scene.spotLights.push_back(spotLight);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load spot light: " << e.what() << std::endl;
                    }
                }
            }
            std::cout << "\n=== Scene Load Summary ===" << std::endl;
            std::cout << "Models loaded: " << scene.models.size() << std::endl;
            std::cout << "Point clouds loaded: " << scene.pointClouds.size() << std::endl;
            std::cout << "Point lights loaded: " << scene.pointLights.size() << std::endl;
            std::cout << "Spot lights loaded: " << scene.spotLights.size() << std::endl;
            std::cout << "========================\n" << std::endl;
        }
        catch (const std::exception& e) {
            throw std::runtime_error("Failed to load scene: " + std::string(e.what()));
        }

        return scene;
    }

    void loadModelData(Model& model, const std::string& filename) {
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                throw std::runtime_error("Failed to open model data file: " + filename);
            }

            json modelJson;
            file >> modelJson;

            model.position = glm::vec3(
                modelJson["position"][0].get<float>(),
                modelJson["position"][1].get<float>(),
                modelJson["position"][2].get<float>()
            );
            model.rotation = glm::vec3(
                modelJson["rotation"][0].get<float>(),
                modelJson["rotation"][1].get<float>(),
                modelJson["rotation"][2].get<float>()
            );
            model.scale = glm::vec3(
                modelJson["scale"][0].get<float>(),
                modelJson["scale"][1].get<float>(),
                modelJson["scale"][2].get<float>()
            );
            model.visible = modelJson.value("visible", true);
        }
        catch (const std::exception& e) {
            throw std::runtime_error("Failed to load model data: " + std::string(e.what()));
        }
    }

    void saveModelData(const Model& model, const std::string& filename) {
        try {
            json modelJson;
            modelJson["position"] = { model.position.x, model.position.y, model.position.z };
            modelJson["rotation"] = { model.rotation.x, model.rotation.y, model.rotation.z };
            modelJson["scale"] = { model.scale.x, model.scale.y, model.scale.z };
            modelJson["visible"] = model.visible;

            std::ofstream file(filename);
            if (!file.is_open()) {
                throw std::runtime_error("Failed to create model data file: " + filename);
            }
            file << std::setw(4) << modelJson << std::endl;
        }
        catch (const std::exception& e) {
            throw std::runtime_error("Failed to save model data: " + std::string(e.what()));
        }
    }

}  // namespace Engine