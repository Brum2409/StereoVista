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
        preset.name = presetJson["name"];
        preset.showSphereCursor = presetJson["showSphereCursor"];
        preset.showFragmentCursor = presetJson["showFragmentCursor"];
        preset.fragmentBaseInnerRadius = presetJson["fragmentBaseInnerRadius"];
        preset.sphereScalingMode = presetJson["sphereScalingMode"];
        preset.sphereFixedRadius = presetJson["sphereFixedRadius"];
        preset.sphereTransparency = presetJson["sphereTransparency"];
        preset.showInnerSphere = presetJson["showInnerSphere"];
        preset.cursorColor = glm::vec4(presetJson["cursorColor"][0], presetJson["cursorColor"][1], presetJson["cursorColor"][2], presetJson["cursorColor"][3]);
        preset.innerSphereColor = glm::vec4(presetJson["innerSphereColor"][0], presetJson["innerSphereColor"][1], presetJson["innerSphereColor"][2], presetJson["innerSphereColor"][3]);
        preset.innerSphereFactor = presetJson["innerSphereFactor"];
        preset.cursorEdgeSoftness = presetJson["cursorEdgeSoftness"];
        preset.cursorCenterTransparency = presetJson["cursorCenterTransparency"];

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