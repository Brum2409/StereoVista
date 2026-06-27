#include "Core/SceneManager.h"
#include <fstream>
#include <iostream>
#include <json.h>
#include "Loaders/PointCloudLoader.h"
#include <filesystem>
#include <unordered_set>
#include <utility>
#include <ctime>

using json = nlohmann::json;

namespace Engine {

    namespace {
        // Local "now" as an ISO-8601-ish string (YYYY-MM-DD HH:MM:SS). Used for
        // the scene's created/modified metadata timestamps.
        std::string currentTimestamp() {
            std::time_t t = std::time(nullptr);
            std::tm tmBuf{};
#if defined(_WIN32)
            localtime_s(&tmBuf, &t);
#else
            localtime_r(&t, &tmBuf);
#endif
            char buf[32];
            std::strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tmBuf);
            return std::string(buf);
        }

        // Read a 3-component array from json into a glm::vec3, leaving the
        // destination untouched if the field is missing or malformed.
        void readVec3(const json& parent, const char* key, glm::vec3& out) {
            if (parent.contains(key) && parent[key].is_array() &&
                parent[key].size() == 3) {
                out = glm::vec3(parent[key][0].get<float>(),
                                parent[key][1].get<float>(),
                                parent[key][2].get<float>());
            }
        }

        // Write `content` to `target` atomically: stream to a sibling .tmp file,
        // flush, then rename over the destination. A crash mid-write therefore
        // leaves the previous scene file intact instead of a truncated one.
        void writeFileAtomic(const std::filesystem::path& target,
                             const std::string& content) {
            std::filesystem::path tmp = target;
            tmp += ".tmp";
            {
                std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
                if (!out.is_open()) {
                    throw std::runtime_error("Failed to create temp file: " +
                                             tmp.string());
                }
                out.write(content.data(),
                          static_cast<std::streamsize>(content.size()));
                out.flush();
                if (!out) {
                    throw std::runtime_error("Failed while writing: " +
                                             tmp.string());
                }
            }
            std::error_code ec;
            std::filesystem::rename(tmp, target, ec);
            if (ec) {
                // Some platforms refuse rename over an existing file; fall back
                // to remove + rename, then to a direct copy.
                std::filesystem::remove(target, ec);
                std::filesystem::rename(tmp, target, ec);
                if (ec) {
                    std::filesystem::copy_file(
                        tmp, target,
                        std::filesystem::copy_options::overwrite_existing, ec);
                    std::filesystem::remove(tmp, ec);
                    if (ec) {
                        throw std::runtime_error(
                            "Failed to finalize scene file: " + target.string());
                    }
                }
            }
        }
    } // namespace

    void saveScene(const std::string& filename, const Scene& scene,
                   const Camera& camera, const SceneSaveOptions& options) {
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

            // Format version + metadata header. Preserve the original creation
            // timestamp if the scene already had one (round-tripped on load).
            sceneJson["formatVersion"] = kSceneFormatVersion;
            json metaJson;
            metaJson["description"] = scene.metadata.description;
            metaJson["author"] = scene.metadata.author;
            metaJson["createdAt"] = scene.metadata.createdAt.empty()
                                        ? currentTimestamp()
                                        : scene.metadata.createdAt;
            metaJson["modifiedAt"] = currentTimestamp();
            metaJson["appVersion"] = "StereoVista";
            metaJson["counts"] = {
                {"models", scene.models.size()},
                {"pointClouds", scene.pointClouds.size()},
                {"pointLights", scene.pointLights.size()},
                {"spotLights", scene.spotLights.size()},
                {"measurements", scene.measurements.size()},
                {"clipPlanes", scene.clipPlanes.size()},
            };
            sceneJson["metadata"] = metaJson;

            // Save current camera state
            if (options.includeCamera) {
            auto cameraState = camera.GetState();
            sceneJson["camera"]["position"] = {cameraState.position.x, cameraState.position.y, cameraState.position.z};
            sceneJson["camera"]["front"] = {cameraState.front.x, cameraState.front.y, cameraState.front.z};
            sceneJson["camera"]["up"] = {cameraState.up.x, cameraState.up.y, cameraState.up.z};
            sceneJson["camera"]["yaw"] = cameraState.yaw;
            sceneJson["camera"]["pitch"] = cameraState.pitch;
            sceneJson["camera"]["zoom"] = cameraState.zoom;
            sceneJson["camera"]["orientation"] = {cameraState.orientation.x, cameraState.orientation.y, cameraState.orientation.z, cameraState.orientation.w};
            }

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
            int pcIndex = 0;
            std::unordered_set<std::string> usedPcFilenames;
            for (const auto& pointCloud : scene.pointClouds) {
                json pointCloudJson;
                pointCloudJson["name"] = pointCloud.name;
                pointCloudJson["position"] = { pointCloud.position.x, pointCloud.position.y, pointCloud.position.z };
                pointCloudJson["rotation"] = { pointCloud.rotation.x, pointCloud.rotation.y, pointCloud.rotation.z };
                pointCloudJson["scale"] = { pointCloud.scale.x, pointCloud.scale.y, pointCloud.scale.z };
                pointCloudJson["visible"] = pointCloud.visible;
                pointCloudJson["basePointSize"] = pointCloud.basePointSize;

                // Cloud names can contain path-hostile characters (they default
                // to the source filename) and may collide — sanitize and dedupe.
                std::string baseName = pointCloud.name.empty()
                    ? ("pointCloud_" + std::to_string(pcIndex)) : pointCloud.name;
                for (char& c : baseName) {
                    if (c == '/' || c == '\\' || c == ':' || c == '*' || c == '?' ||
                        c == '"' || c == '<' || c == '>' || c == '|') {
                        c = '_';
                    }
                }
                std::string pcFilename = baseName + ".pcb";
                while (!usedPcFilenames.insert(pcFilename).second) {
                    pcFilename = baseName + "_" + std::to_string(pcIndex) + ".pcb";
                    baseName += "_";
                }
                pcIndex++;

                // Export in LOCAL space (applyTransform = false): the transform
                // is stored in the JSON above and re-applied on load. Baking it
                // into the points as well would apply it twice.
                std::filesystem::path pcPath = sceneDir / "pointClouds" / pcFilename;
                if (!Engine::PointCloudLoader::exportToBinary(pointCloud, pcPath.string(), false)) {
                    std::cerr << "Warning: failed to write point cloud data for '"
                              << pointCloud.name << "' - it will be missing from the scene"
                              << std::endl;
                    continue;
                }

                pointCloudJson["dataPath"] = "pointClouds/" + pcFilename;
                pointCloudsJson.push_back(pointCloudJson);
            }
            sceneJson["pointClouds"] = pointCloudsJson;

            // Save measurements (world-space annotation points)
            if (options.includeMeasurements) {
            json measurementsJson = json::array();
            for (const auto& measurement : scene.measurements) {
                json measurementJson;
                measurementJson["type"] = static_cast<int>(measurement.type);
                measurementJson["name"] = measurement.name;
                measurementJson["color"] = { measurement.color.r, measurement.color.g, measurement.color.b };
                measurementJson["visible"] = measurement.visible;
                json pointsJson = json::array();
                for (const auto& p : measurement.points) {
                    pointsJson.push_back({ p.x, p.y, p.z });
                }
                measurementJson["points"] = pointsJson;
                measurementsJson.push_back(measurementJson);
            }
            sceneJson["measurements"] = measurementsJson;
            }

            // Save section / clip planes (world-space)
            if (options.includeClipPlanes) {
            json clipPlanesJson = json::array();
            for (const auto& plane : scene.clipPlanes) {
                json planeJson;
                planeJson["name"] = plane.name;
                planeJson["position"] = { plane.position.x, plane.position.y, plane.position.z };
                planeJson["normal"] = { plane.normal.x, plane.normal.y, plane.normal.z };
                planeJson["color"] = { plane.color.r, plane.color.g, plane.color.b };
                planeJson["enabled"] = plane.enabled;
                clipPlanesJson.push_back(planeJson);
            }
            sceneJson["clipPlanes"] = clipPlanesJson;
            }

            // Save lighting (sun + point/spot lights)
            if (options.includeLighting) {
            // Sun (directional key light)
            json sunJson;
            sunJson["direction"] = { scene.sun.direction.x, scene.sun.direction.y, scene.sun.direction.z };
            sunJson["color"] = { scene.sun.color.x, scene.sun.color.y, scene.sun.color.z };
            sunJson["intensity"] = scene.sun.intensity;
            sunJson["enabled"] = scene.sun.enabled;
            sceneJson["sun"] = sunJson;

            // Save point lights
            json pointLightsJson = json::array();
            for (const auto& pointLight : scene.pointLights) {
                json pointLightJson;
                pointLightJson["position"] = { pointLight.position.x, pointLight.position.y, pointLight.position.z };
                pointLightJson["color"] = { pointLight.color.x, pointLight.color.y, pointLight.color.z };
                pointLightJson["intensity"] = pointLight.intensity;
                pointLightJson["linear"] = pointLight.linear;
                pointLightJson["quadratic"] = pointLight.quadratic;
                pointLightJson["castShadows"] = pointLight.castShadows;
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
                spotLightJson["castShadows"] = spotLight.castShadows;
                spotLightsJson.push_back(spotLightJson);
            }
            sceneJson["spotLights"] = spotLightsJson;
            }

            // Save environment (skybox + lighting mode) so the scene reopens
            // with the look it was authored in.
            if (options.includeEnvironment) {
                json envJson;
                envJson["lightingMode"] = scene.environment.lightingMode;
                envJson["skyboxType"] = scene.environment.skyboxType;
                envJson["skyboxSolidColor"] = { scene.environment.skyboxSolidColor.r,
                                                scene.environment.skyboxSolidColor.g,
                                                scene.environment.skyboxSolidColor.b };
                envJson["skyboxGradientTop"] = { scene.environment.skyboxGradientTop.r,
                                                 scene.environment.skyboxGradientTop.g,
                                                 scene.environment.skyboxGradientTop.b };
                envJson["skyboxGradientBottom"] = { scene.environment.skyboxGradientBottom.r,
                                                    scene.environment.skyboxGradientBottom.g,
                                                    scene.environment.skyboxGradientBottom.b };
                envJson["selectedCubemap"] = scene.environment.selectedCubemap;
                envJson["skyboxHdrPath"] = scene.environment.skyboxHdrPath;
                envJson["skyboxExposure"] = scene.environment.skyboxExposure;
                sceneJson["environment"] = envJson;
            }

            std::cout << "\n=== Scene Save Summary ===" << std::endl;
            std::cout << "Models saved: " << scene.models.size() << std::endl;
            std::cout << "Point clouds saved: " << scene.pointClouds.size() << std::endl;
            std::cout << "Point lights saved: " << scene.pointLights.size() << std::endl;
            std::cout << "Spot lights saved: " << scene.spotLights.size() << std::endl;
            std::cout << "Scene directory: " << sceneDir << std::endl;
            std::cout << "========================\n" << std::endl;

            // Write scene file with chunking support. The compact option
            // minifies the JSON (no indentation) to shrink large scene files.
            std::string jsonStr = options.compact ? sceneJson.dump()
                                                   : sceneJson.dump(4);
            const size_t maxChunkSize = 100 * 1024 * 1024; // 100MB chunks

            if (jsonStr.size() > maxChunkSize) {
                // Split into multiple files if too large
                size_t numChunks = (jsonStr.size() + maxChunkSize - 1) / maxChunkSize;

                for (size_t i = 0; i < numChunks; i++) {
                    std::string chunkFilename = scenePath.string() + "." + std::to_string(i);
                    size_t start = i * maxChunkSize;
                    size_t length = std::min(maxChunkSize, jsonStr.size() - start);
                    writeFileAtomic(chunkFilename, jsonStr.substr(start, length));
                }

                // Write metadata file (atomic) pointing at the chunk count.
                json chunkMeta;
                chunkMeta["numChunks"] = numChunks;
                writeFileAtomic(scenePath, chunkMeta.dump(4));
            }
            else {
                // Write single file atomically (temp + rename) so a crash
                // mid-save never corrupts the previously saved scene.
                writeFileAtomic(scenePath, jsonStr);
            }
        }
        catch (const std::exception& e) {
            throw std::runtime_error("Failed to save scene: " + std::string(e.what()));
        }
    }

    Scene loadScene(const std::string& filename, Camera& camera,
                    SceneLoadReport* report) {
        Scene scene;
        json sceneJson;
        std::filesystem::path scenePath;
        std::filesystem::path sceneDir;

        // Helper to record a warning both to the console and (if requested) to
        // the caller's report so the UI can surface partial loads.
        auto warn = [&](const std::string& msg) {
            std::cerr << msg << std::endl;
            if (report) report->warnings.push_back(msg);
        };

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

            // Check if scene directory exists (it may not exist for scenes with no external assets)
            if (!std::filesystem::exists(sceneDir)) {
            }

            // Format version (untagged v1 files default to 1).
            int formatVersion = sceneJson.value("formatVersion", 1);
            if (report) report->formatVersion = formatVersion;

            // Metadata header (v2+).
            if (sceneJson.contains("metadata") && sceneJson["metadata"].is_object()) {
                const auto& m = sceneJson["metadata"];
                scene.metadata.description = m.value("description", std::string());
                scene.metadata.author = m.value("author", std::string());
                scene.metadata.createdAt = m.value("createdAt", std::string());
                scene.metadata.modifiedAt = m.value("modifiedAt", std::string());
                scene.metadata.appVersion = m.value("appVersion", std::string());
            }

            // Environment block (v2+): skybox + lighting mode.
            if (sceneJson.contains("environment") && sceneJson["environment"].is_object()) {
                const auto& e = sceneJson["environment"];
                scene.environment.present = true;
                scene.environment.lightingMode = e.value("lightingMode", 0);
                scene.environment.skyboxType = e.value("skyboxType", 1);
                readVec3(e, "skyboxSolidColor", scene.environment.skyboxSolidColor);
                readVec3(e, "skyboxGradientTop", scene.environment.skyboxGradientTop);
                readVec3(e, "skyboxGradientBottom", scene.environment.skyboxGradientBottom);
                scene.environment.selectedCubemap = e.value("selectedCubemap", 0);
                scene.environment.skyboxHdrPath = e.value("skyboxHdrPath", std::string());
                scene.environment.skyboxExposure = e.value("skyboxExposure", 0.2f);
            }

            // Sun (v2+).
            if (sceneJson.contains("sun") && sceneJson["sun"].is_object()) {
                const auto& s = sceneJson["sun"];
                readVec3(s, "direction", scene.sun.direction);
                readVec3(s, "color", scene.sun.color);
                scene.sun.intensity = s.value("intensity", scene.sun.intensity);
                scene.sun.enabled = s.value("enabled", scene.sun.enabled);
                scene.hasSun = true;
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
                                warn("Model file not found: " + modelPath.string());
                                if (report) report->modelsFailed++;
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
                                warn("Failed to load model: " + modelPath.string());
                                if (report) report->modelsFailed++;
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

                        // Set source scene path for grouping in GUI
                        model.sourceScenePath = filename;

                        scene.models.push_back(model);
                    }
                    catch (const std::exception& e) {
                        warn(std::string("Failed to load model: ") + e.what());
                        if (report) report->modelsFailed++;
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
                        pointCloud.visible = pointCloudJson.value("visible", true);
                        pointCloud.basePointSize = pointCloudJson.value("basePointSize", 2.0f);
                        pointCloud.filePath = pcPath.string();

                        // Set source scene path for grouping in GUI
                        pointCloud.sourceScenePath = filename;

                        if (!pointCloud.isLoaded()) {
                            warn("Point cloud '" + pointCloud.name +
                                 "' loaded with no points (data file: " +
                                 pcPath.string() + ")");
                        }

                        scene.pointClouds.push_back(std::move(pointCloud));
                    }
                    catch (const std::exception& e) {
                        warn(std::string("Failed to load point cloud: ") + e.what());
                        if (report) report->pointCloudsFailed++;
                    }
                }
            }

            // Load measurements
            if (sceneJson.contains("measurements")) {
                for (const auto& measurementJson : sceneJson["measurements"]) {
                    try {
                        Measurement measurement;
                        measurement.type = static_cast<Measurement::Type>(
                            measurementJson.value("type", 0));
                        measurement.name = measurementJson.value("name", std::string("Measurement"));
                        measurement.visible = measurementJson.value("visible", true);
                        if (measurementJson.contains("color") &&
                            measurementJson["color"].is_array() &&
                            measurementJson["color"].size() == 3) {
                            measurement.color = glm::vec3(
                                measurementJson["color"][0].get<float>(),
                                measurementJson["color"][1].get<float>(),
                                measurementJson["color"][2].get<float>()
                            );
                        }
                        if (measurementJson.contains("points") &&
                            measurementJson["points"].is_array()) {
                            for (const auto& pointJson : measurementJson["points"]) {
                                if (pointJson.is_array() && pointJson.size() == 3) {
                                    measurement.points.emplace_back(
                                        pointJson[0].get<float>(),
                                        pointJson[1].get<float>(),
                                        pointJson[2].get<float>());
                                }
                            }
                        }
                        if (!measurement.points.empty()) {
                            scene.measurements.push_back(std::move(measurement));
                        }
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load measurement: " << e.what() << std::endl;
                    }
                }
            }

            // Load section / clip planes
            if (sceneJson.contains("clipPlanes")) {
                for (const auto& planeJson : sceneJson["clipPlanes"]) {
                    try {
                        ClipPlane plane;
                        plane.name = planeJson.value("name", std::string("Plane"));
                        plane.enabled = planeJson.value("enabled", true);
                        if (planeJson.contains("position") &&
                            planeJson["position"].is_array() &&
                            planeJson["position"].size() == 3) {
                            plane.position = glm::vec3(
                                planeJson["position"][0].get<float>(),
                                planeJson["position"][1].get<float>(),
                                planeJson["position"][2].get<float>());
                        }
                        if (planeJson.contains("normal") &&
                            planeJson["normal"].is_array() &&
                            planeJson["normal"].size() == 3) {
                            plane.normal = glm::vec3(
                                planeJson["normal"][0].get<float>(),
                                planeJson["normal"][1].get<float>(),
                                planeJson["normal"][2].get<float>());
                        }
                        if (planeJson.contains("color") &&
                            planeJson["color"].is_array() &&
                            planeJson["color"].size() == 3) {
                            plane.color = glm::vec3(
                                planeJson["color"][0].get<float>(),
                                planeJson["color"][1].get<float>(),
                                planeJson["color"][2].get<float>());
                        }
                        scene.clipPlanes.push_back(std::move(plane));
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load clip plane: " << e.what() << std::endl;
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
                        pointLight.linear = pointLightJson.value("linear", pointLight.linear);
                        pointLight.quadratic = pointLightJson.value("quadratic", pointLight.quadratic);
                        pointLight.castShadows = pointLightJson.value("castShadows", true);

                        // Set source scene path for grouping in GUI
                        pointLight.sourceScenePath = filename;

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
                        spotLight.castShadows = spotLightJson.value("castShadows", true);

                        // Set source scene path for grouping in GUI
                        spotLight.sourceScenePath = filename;

                        scene.spotLights.push_back(spotLight);
                    }
                    catch (const std::exception& e) {
                        std::cerr << "Failed to load spot light: " << e.what() << std::endl;
                    }
                }
            }
            if (report) {
                report->modelsLoaded = static_cast<int>(scene.models.size());
                report->pointCloudsLoaded = static_cast<int>(scene.pointClouds.size());
                report->pointLightsLoaded = static_cast<int>(scene.pointLights.size());
                report->spotLightsLoaded = static_cast<int>(scene.spotLights.size());
                report->measurementsLoaded = static_cast<int>(scene.measurements.size());
                report->clipPlanesLoaded = static_cast<int>(scene.clipPlanes.size());
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

    SceneInfo loadSceneInfo(const std::string& filename) {
        SceneInfo info;
        info.path = filename;
        try {
            std::ifstream file(filename);
            if (!file.is_open()) {
                return info; // valid stays false
            }

            json sceneJson;
            file >> sceneJson;

            // Chunked scenes only store {"numChunks": N} in the header file; we
            // intentionally do not stitch the (potentially huge) chunks back
            // together just to preview counts.
            if (sceneJson.contains("numChunks")) {
                info.valid = true;
                info.formatVersion = sceneJson.value("formatVersion", 1);
                return info;
            }

            info.valid = true;
            info.formatVersion = sceneJson.value("formatVersion", 1);

            if (sceneJson.contains("metadata") && sceneJson["metadata"].is_object()) {
                const auto& m = sceneJson["metadata"];
                info.metadata.description = m.value("description", std::string());
                info.metadata.author = m.value("author", std::string());
                info.metadata.createdAt = m.value("createdAt", std::string());
                info.metadata.modifiedAt = m.value("modifiedAt", std::string());
                info.metadata.appVersion = m.value("appVersion", std::string());
            }

            auto arraySize = [&](const char* key) -> int {
                return (sceneJson.contains(key) && sceneJson[key].is_array())
                           ? static_cast<int>(sceneJson[key].size())
                           : 0;
            };
            info.modelCount = arraySize("models");
            info.pointCloudCount = arraySize("pointClouds");
            info.pointLightCount = arraySize("pointLights");
            info.spotLightCount = arraySize("spotLights");
            info.measurementCount = arraySize("measurements");
            info.clipPlaneCount = arraySize("clipPlanes");
        }
        catch (const std::exception& e) {
            std::cerr << "Failed to read scene info for '" << filename
                      << "': " << e.what() << std::endl;
            info.valid = false;
        }
        return info;
    }

}  // namespace Engine