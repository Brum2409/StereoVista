#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include "Engine/ThreeDConnexionSync.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

LONGLONG ThreeDConnexionSync::CompareFileTime2(const FILETIME& a, const FILETIME& b)
{
    ULARGE_INTEGER ua, ub;
    ua.LowPart  = a.dwLowDateTime;  ua.HighPart  = a.dwHighDateTime;
    ub.LowPart  = b.dwLowDateTime;  ub.HighPart  = b.dwHighDateTime;
    return static_cast<LONGLONG>(ua.QuadPart) - static_cast<LONGLONG>(ub.QuadPart);
}

FILETIME ThreeDConnexionSync::AddSeconds(const FILETIME& ft, LONGLONG seconds)
{
    ULARGE_INTEGER u;
    u.LowPart  = ft.dwLowDateTime;
    u.HighPart = ft.dwHighDateTime;
    u.QuadPart += static_cast<ULONGLONG>(seconds) * 10000000ULL; // 100-ns units
    FILETIME result;
    result.dwLowDateTime  = u.LowPart;
    result.dwHighDateTime = u.HighPart;
    return result;
}

// ---------------------------------------------------------------------------
// File discovery
// ---------------------------------------------------------------------------

bool ThreeDConnexionSync::TryFindFile(DWORD processId)
{
    m_processId = processId;

    // Build the directory path
    char localAppData[MAX_PATH] = {};
    if (!GetEnvironmentVariableA("LOCALAPPDATA", localAppData, MAX_PATH))
        return false;

    std::string dir = std::string(localAppData) +
                      "\\3Dconnexion\\3DxWare\\Cfg\\Ext\\";

    // Pattern: <PID>.*.settings.xml
    std::string pattern = dir + std::to_string(processId) + ".*.settings.xml";

    WIN32_FIND_DATAA fd = {};
    HANDLE hFind = FindFirstFileA(pattern.c_str(), &fd);
    if (hFind == INVALID_HANDLE_VALUE)
        return false;

    m_filePath  = dir + fd.cFileName;
    FindClose(hFind);

    m_connected = ReadFile();
    return m_connected;
}

// ---------------------------------------------------------------------------
// Polling
// ---------------------------------------------------------------------------

bool ThreeDConnexionSync::PollForChanges()
{
    if (!m_connected) {
        if (m_processId != 0)
            TryFindFile(m_processId);
        return false;
    }

    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (!GetFileAttributesExA(m_filePath.c_str(), GetFileExInfoStandard, &fad))
        return false; // File disappeared

    FILETIME currentMtime = fad.ftLastWriteTime;

    // Debounce: skip if within 2 seconds of our own write
    FILETIME debounceEnd = AddSeconds(m_lastWriteTime, 2);
    if (CompareFileTime2(currentMtime, debounceEnd) <= 0)
        return false;

    // No change since last read
    if (CompareFileTime2(currentMtime, m_lastReadTime) == 0)
        return false;

    if (!ReadFile())
        return false;

    if (OnSettingsChanged)
        OnSettingsChanged(m_settings);

    return true;
}

// ---------------------------------------------------------------------------
// XML read helpers
// ---------------------------------------------------------------------------

std::string ThreeDConnexionSync::GetSettingValue(const std::string& xml,
                                                  const std::string& id)
{
    // Look for: ID="<id>"  (or ID="<id>")
    std::string searchKey = "ID=\"" + id + "\"";
    size_t pos = xml.find(searchKey);
    if (pos == std::string::npos) {
        // Try case variations the driver sometimes uses
        searchKey = "id=\"" + id + "\"";
        pos = xml.find(searchKey);
    }
    if (pos == std::string::npos)
        return {};

    // Find <Value> after this position
    size_t vStart = xml.find("<Value>", pos);
    if (vStart == std::string::npos)
        return {};
    vStart += 7; // skip "<Value>"

    size_t vEnd = xml.find("</Value>", vStart);
    if (vEnd == std::string::npos)
        return {};

    return xml.substr(vStart, vEnd - vStart);
}

bool ThreeDConnexionSync::GetSettingBool(const std::string& xml,
                                          const std::string& id,
                                          bool defaultVal)
{
    std::string v = GetSettingValue(xml, id);
    if (v.empty()) return defaultVal;
    // Driver uses "1"/"0" or "true"/"false"
    return (v == "1" || v == "true" || v == "True");
}

int ThreeDConnexionSync::GetSettingInt(const std::string& xml,
                                        const std::string& id,
                                        int defaultVal)
{
    std::string v = GetSettingValue(xml, id);
    if (v.empty()) return defaultVal;
    try { return std::stoi(v); } catch (...) { return defaultVal; }
}

// ---------------------------------------------------------------------------
// ReadFile
// ---------------------------------------------------------------------------

bool ThreeDConnexionSync::ReadFile()
{
    std::ifstream ifs(m_filePath, std::ios::binary);
    if (!ifs.is_open()) {
        std::cerr << "[TdxSync] Cannot open: " << m_filePath << "\n";
        return false;
    }

    std::ostringstream ss;
    ss << ifs.rdbuf();
    std::string xml = ss.str();
    ifs.close();

    // Cache the AppInfo block for round-tripping
    {
        size_t aiStart = xml.find("<AppInfo");
        size_t aiEnd   = xml.find("</AppInfo>");
        if (aiStart != std::string::npos && aiEnd != std::string::npos) {
            m_appInfoBlock = xml.substr(aiStart, aiEnd + 10 - aiStart);
        }
    }

    // Parse all known settings
    m_settings.motionModel      = GetSettingValue(xml, "MotionModel");
    if (m_settings.motionModel.empty()) m_settings.motionModel = "Helicopter";

    m_settings.autoPivot        = GetSettingBool(xml, "AutoPivot",          false);
    m_settings.lockHorizon      = GetSettingBool(xml, "LockHorizon",        false);
    m_settings.suspendInput     = GetSettingBool(xml, "SuspendInput",       false);
    m_settings.lockTo3dViews    = GetSettingBool(xml, "LockTo3dViews",      false);
    m_settings.moveObjects      = GetSettingBool(xml, "MoveObjects",        false);
    m_settings.autokeyAnimation = GetSettingBool(xml, "AutokeyAnimation",   false);
    m_settings.selectionFollower= GetSettingBool(xml, "SelectionFollower",  true);
    m_settings.lockSketchPlane  = GetSettingBool(xml, "LockSketchPlane",    true);

    m_settings.firstPersonEaseOut = GetSettingInt(xml, "FirstPersonEaseOut", 600);
    m_settings.floorQueryRate     = GetSettingInt(xml, "FloorQueryRate",     1);

    m_settings.pivotVisibility  = GetSettingValue(xml, "PivotVisibility");
    if (m_settings.pivotVisibility.empty()) m_settings.pivotVisibility = "HidePivot";

    // Record the file's last-write time as our read time
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExA(m_filePath.c_str(), GetFileExInfoStandard, &fad))
        m_lastReadTime = fad.ftLastWriteTime;

    return true;
}

// ---------------------------------------------------------------------------
// BuildXml
// ---------------------------------------------------------------------------

std::string ThreeDConnexionSync::BuildXml(const TdxSettings& s,
                                           DWORD pid,
                                           const std::string& appInfoBlock)
{
    auto b2s = [](bool v) -> const char* { return v ? "1" : "0"; };

    std::ostringstream xml;
    xml << "<?xml version=\"1.0\" encoding=\"utf-8\"?>\r\n";
    xml << "<SpaceMouse>\r\n";

    if (!appInfoBlock.empty()) {
        xml << "  " << appInfoBlock << "\r\n";
    } else {
        xml << "  <AppInfo>\r\n";
        xml << "    <ProcessID>" << pid << "</ProcessID>\r\n";
        xml << "  </AppInfo>\r\n";
    }

    xml << "  <Settings>\r\n";

    auto setting = [&](const char* id, const std::string& val) {
        xml << "    <Setting ID=\"" << id << "\"><Value>" << val << "</Value></Setting>\r\n";
    };

    setting("MotionModel",       s.motionModel);
    setting("AutoPivot",         b2s(s.autoPivot));
    setting("PivotVisibility",   s.pivotVisibility);
    setting("LockHorizon",       b2s(s.lockHorizon));
    setting("SuspendInput",      b2s(s.suspendInput));
    setting("LockTo3dViews",     b2s(s.lockTo3dViews));
    setting("MoveObjects",       b2s(s.moveObjects));
    setting("AutokeyAnimation",  b2s(s.autokeyAnimation));
    setting("SelectionFollower", b2s(s.selectionFollower));
    setting("FirstPersonEaseOut",std::to_string(s.firstPersonEaseOut));
    setting("FloorQueryRate",    std::to_string(s.floorQueryRate));
    setting("LockSketchPlane",   b2s(s.lockSketchPlane));

    xml << "  </Settings>\r\n";
    xml << "</SpaceMouse>\r\n";

    return xml.str();
}

// ---------------------------------------------------------------------------
// WriteSettings
// ---------------------------------------------------------------------------

bool ThreeDConnexionSync::WriteSettings(const TdxSettings& s)
{
    if (!m_connected || m_filePath.empty())
        return false;

    std::string xml = BuildXml(s, m_processId, m_appInfoBlock);

    // Write atomically: temp file → rename
    std::string tmpPath = m_filePath + ".tmp";
    {
        std::ofstream ofs(tmpPath, std::ios::binary | std::ios::trunc);
        if (!ofs.is_open()) {
            std::cerr << "[TdxSync] Cannot write temp file: " << tmpPath << "\n";
            return false;
        }
        ofs << xml;
    }

    if (!MoveFileExA(tmpPath.c_str(), m_filePath.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        std::cerr << "[TdxSync] MoveFileEx failed: " << GetLastError() << "\n";
        DeleteFileA(tmpPath.c_str());
        return false;
    }

    // Record our write time for debounce
    WIN32_FILE_ATTRIBUTE_DATA fad = {};
    if (GetFileAttributesExA(m_filePath.c_str(), GetFileExInfoStandard, &fad)) {
        m_lastWriteTime = fad.ftLastWriteTime;
        m_lastReadTime  = fad.ftLastWriteTime; // We just wrote it; don't re-read
    }

    m_settings = s;
    return true;
}
