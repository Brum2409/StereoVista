// cursor_presets.h
#pragma once

#include <string>
#include <vector>
#include "core.h"

namespace Engine {

    struct CursorPreset {
        std::string name;
        bool showSphereCursor;
        bool showFragmentCursor;
        float fragmentBaseInnerRadius;
        int sphereScalingMode;
        float sphereFixedRadius;
        float sphereTransparency;
        bool showInnerSphere;
        glm::vec4 cursorColor;
        glm::vec4 innerSphereColor;
        float innerSphereFactor;
        float cursorEdgeSoftness;
        float cursorCenterTransparency;
        bool showPlaneCursor;
        float planeDiameter;
        glm::vec4 planeColor;
    };

    class CursorPresetManager {
    public:
        static void savePreset(const std::string& name, const CursorPreset& preset);
        static CursorPreset loadPreset(const std::string& name);
        static std::vector<std::string> getPresetNames();
        static void deletePreset(const std::string& name);
        static CursorPreset applyCursorPreset(const std::string& name);

    private:
        static std::string getPresetsFilePath();
    };

} // namespace Engine