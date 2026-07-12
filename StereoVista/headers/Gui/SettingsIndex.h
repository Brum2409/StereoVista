#pragma once

// ============================================================================
//  Gui::SettingsIndex — the searchable settings index
//  (UI redesign Pass 6; docs/UI_REDESIGN.md §12, shared with the Pass-4 palette)
// ----------------------------------------------------------------------------
//  Every settings row is addressable: the Settings window filters on it, and the
//  command palette's ':' prefix searches it and DEEP-LINKS into the row (open
//  Settings -> select its category -> flash-highlight it).
//
//  This is the seam Pass 4 reserved (Palette::Source::Setting).
// ============================================================================

#include <string>
#include <vector>

namespace Gui {

// Settings categories, in sidebar order. Append-only.
enum class SettingsCategory : int {
    Interface = 0,
    Camera,
    Cursor,
    Stereo,
    Vr,
    Rendering,
    Environment,
    PointClouds,
    Files,
    Shortcuts,
    Count
};

const char* settingsCategoryName(SettingsCategory category);
const char* settingsCategoryIcon(SettingsCategory category);

struct SettingsIndexEntry {
    const char* label;    // the row's visible label ("Fly speed")
    const char* keywords; // extra search terms
    SettingsCategory category;
};

// The index the palette searches (a flat list of the addressable rows).
const std::vector<SettingsIndexEntry>& settingsIndex();

// Deep-link: reveal the Settings window, select `category` and flash `label`.
// Consumed by drawSettingsPanel on its next frame.
void focusSetting(SettingsCategory category, const char* label);

} // namespace Gui
