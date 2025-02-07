// cursor_presets.cpp
#include "cursor_presets.h"
#include <fstream>
#include <json.h>
#include <filesystem>

using json = nlohmann::json;

namespace Engine {

    std::string CursorPresetManager::getPresetsFilePath() {
        return "cursor_presets.json";
    }

    void CursorPresetManager::savePreset(const std::string& name, const CursorPreset& preset) {
        json presets;
        std::string filePath = getPresetsFilePath();

        // Load existing presets
        if (std::filesystem::exists(filePath)) {
            std::ifstream file(filePath);
            file >> presets;
        }

        // Add or update the preset
        json presetJson;
        presetJson["name"] = preset.name;
        presetJson["showSphereCursor"] = preset.showSphereCursor;
        presetJson["showFragmentCursor"] = preset.showFragmentCursor;
        presetJson["fragmentBaseInnerRadius"] = preset.fragmentBaseInnerRadius;
        presetJson["sphereScalingMode"] = preset.sphereScalingMode;
        presetJson["sphereFixedRadius"] = preset.sphereFixedRadius;
        presetJson["sphereTransparency"] = preset.sphereTransparency;
        presetJson["showInnerSphere"] = preset.showInnerSphere;
        presetJson["cursorColor"] = { preset.cursorColor.r, preset.cursorColor.g, preset.cursorColor.b, preset.cursorColor.a };
        presetJson["innerSphereColor"] = { preset.innerSphereColor.r, preset.innerSphereColor.g, preset.innerSphereColor.b, preset.innerSphereColor.a };
        presetJson["innerSphereFactor"] = preset.innerSphereFactor;
        presetJson["cursorEdgeSoftness"] = preset.cursorEdgeSoftness;
        presetJson["cursorCenterTransparency"] = preset.cursorCenterTransparency;
        presetJson["showPlaneCursor"] = preset.showPlaneCursor;
        presetJson["planeDiameter"] = preset.planeDiameter;
        presetJson["planeColor"] = { preset.planeColor.r, preset.planeColor.g, preset.planeColor.b, preset.planeColor.a };


        presets[name] = presetJson;

        // Save to file
        std::ofstream file(filePath);
        file << std::setw(4) << presets << std::endl;
    }

    CursorPreset CursorPresetManager::loadPreset(const std::string& name) {
        std::string filePath = getPresetsFilePath();
        if (!std::filesystem::exists(filePath)) {
            throw std::runtime_error("Presets file not found");
        }

        std::ifstream file(filePath);
        json presets;
        file >> presets;

        if (presets.find(name) == presets.end()) {
            throw std::runtime_error("Preset not found");
        }

        json presetJson = presets[name];
        CursorPreset preset;

        // Use value() with defaults for each property
        preset.name = presetJson.value("name", "Default");
        preset.showSphereCursor = presetJson.value("showSphereCursor", false);
        preset.showFragmentCursor = presetJson.value("showFragmentCursor", false);
        preset.fragmentBaseInnerRadius = presetJson.value("fragmentBaseInnerRadius", 0.004f);
        preset.sphereScalingMode = presetJson.value("sphereScalingMode", 0);
        preset.sphereFixedRadius = presetJson.value("sphereFixedRadius", 0.05f);
        preset.sphereTransparency = presetJson.value("sphereTransparency", 0.7f);
        preset.showInnerSphere = presetJson.value("showInnerSphere", false);

        // Handle vec4 properties with proper defaults
        auto defaultCursorColor = json::array({ 1.0f, 0.0f, 0.0f, 0.7f });
        auto defaultInnerColor = json::array({ 0.0f, 1.0f, 0.0f, 1.0f });
        auto defaultPlaneColor = json::array({ 0.0f, 1.0f, 0.0f, 0.7f });

        auto cursorColorArr = presetJson.value("cursorColor", defaultCursorColor);
        auto innerColorArr = presetJson.value("innerSphereColor", defaultInnerColor);
        auto planeColorArr = presetJson.value("planeColor", defaultPlaneColor);

        preset.cursorColor = glm::vec4(cursorColorArr[0], cursorColorArr[1], cursorColorArr[2], cursorColorArr[3]);
        preset.innerSphereColor = glm::vec4(innerColorArr[0], innerColorArr[1], innerColorArr[2], innerColorArr[3]);
        preset.planeColor = glm::vec4(planeColorArr[0], planeColorArr[1], planeColorArr[2], planeColorArr[3]);

        preset.innerSphereFactor = presetJson.value("innerSphereFactor", 0.1f);
        preset.cursorEdgeSoftness = presetJson.value("cursorEdgeSoftness", 0.8f);
        preset.cursorCenterTransparency = presetJson.value("cursorCenterTransparency", 0.2f);

        // New plane cursor properties with defaults
        preset.showPlaneCursor = presetJson.value("showPlaneCursor", false);
        preset.planeDiameter = presetJson.value("planeDiameter", 0.5f);

        return preset;
    }

    std::vector<std::string> CursorPresetManager::getPresetNames() {
        std::vector<std::string> names;
        std::string filePath = getPresetsFilePath();

        if (std::filesystem::exists(filePath)) {
            std::ifstream file(filePath);
            json presets;
            file >> presets;

            for (auto& [name, _] : presets.items()) {
                names.push_back(name);
            }
        }

        return names;
    }

    void CursorPresetManager::deletePreset(const std::string& name) {
        std::string filePath = getPresetsFilePath();
        if (!std::filesystem::exists(filePath)) {
            return;
        }

        std::ifstream inFile(filePath);
        json presets;
        inFile >> presets;
        inFile.close();

        if (presets.find(name) != presets.end()) {
            presets.erase(name);

            std::ofstream outFile(filePath);
            outFile << std::setw(4) << presets << std::endl;
        }
    }

    CursorPreset CursorPresetManager::applyCursorPreset(const std::string& name) {
        return loadPreset(name);
    }

} // namespace Engine