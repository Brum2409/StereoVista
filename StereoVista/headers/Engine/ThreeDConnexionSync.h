#pragma once

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <functional>
#include <string>

class ThreeDConnexionSync {
public:
    struct TdxSettings {
        std::string motionModel     = "Helicopter"; // Helicopter|Object|Fly|Walk|Orbit|Target|Drive
        bool autoPivot              = false;
        std::string pivotVisibility = "HidePivot";  // ShowPivot|ShowMovingPivot|HidePivot
        bool lockHorizon            = false;
        bool suspendInput           = false;
        bool lockTo3dViews          = false;
        bool moveObjects            = false;
        bool autokeyAnimation       = false;
        bool selectionFollower      = true;
        int  firstPersonEaseOut     = 600;   // 0–1000
        int  floorQueryRate         = 1;     // 1–100
        bool lockSketchPlane        = true;
    };

    // Find the settings file for a given process ID.
    // Returns true if the file was found and successfully read.
    bool TryFindFile(DWORD processId);

    // Poll for external changes to the file (call ~once per 60 frames).
    // Returns true if the file was re-read (fires OnSettingsChanged).
    bool PollForChanges();

    // Overwrite the XML file with the given settings.
    // Returns true on success.
    bool WriteSettings(const TdxSettings& s);

    const TdxSettings& GetSettings() const { return m_settings; }
    bool IsConnected() const { return m_connected; }

    // Fired after an external change is detected and re-read.
    std::function<void(const TdxSettings&)> OnSettingsChanged;

private:
    std::string m_filePath;
    TdxSettings m_settings;
    std::string m_appInfoBlock;     // Preserved <AppInfo> XML block
    FILETIME    m_lastReadTime  {};
    FILETIME    m_lastWriteTime {};  // Set after our own write (for debounce)
    DWORD       m_processId     = 0;
    bool        m_connected     = false;

    bool ReadFile();
    static std::string GetSettingValue(const std::string& xml, const std::string& id);
    static bool        GetSettingBool(const std::string& xml, const std::string& id, bool defaultVal);
    static int         GetSettingInt(const std::string& xml, const std::string& id, int defaultVal);
    static std::string BuildXml(const TdxSettings& s, DWORD pid, const std::string& appInfoBlock);

    // Compare two FILETIME values; returns negative / 0 / positive like strcmp.
    static LONGLONG CompareFileTime2(const FILETIME& a, const FILETIME& b);
    // Add 'seconds' to a FILETIME.
    static FILETIME AddSeconds(const FILETIME& ft, LONGLONG seconds);
};
